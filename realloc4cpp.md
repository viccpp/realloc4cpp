```
Document number: P0894R0
Audience: Library Evolution Working Group
Reply to: Victor Dyachenko <__vic@ngs.ru>
Date: 2018-01-10
```

# `realloc()` for C++

## Abstract

It is a proposal to add a part of `realloc()` function behaviour to C++.

## Introduction

C language defines following subroutines to allocate/deallocate heap memory:

1. `malloc()`/`calloc()`
2. `free()`
3. `realloc()`

C++ provides some counterparts:

1. `malloc()`/`calloc()` - `operator new`/`operator new[]`
2. `free()` - `operator delete/operator delete[]`
3. `realloc()` - ?

As we can see, there is no counterpart for `realloc()`. And there is a solid
reason for that: `realloc()` copies the memory block if it cannot be just
expanded or shortened. It is unacceptable behaviour because objects in C++
are not trivially copyable in general. Therefore `realloc()` cannot be used in
generic code. However the ability to resize already allocated memory block can
be very useful. We could potentially get performance and safety gain:
no need to move the existing data (performance) and consequently no risk
to cause an exception by throwing move-operation (safety). Yes, it cannot be
guaranteed that every resize-request will be satisfied (it won't usually in
fact). And what do we do in such case? All we do today! Just fall back to the
current technics.

One can argue that today system allocators don't support such feature. Well.

1. At least custom allocators can benefit right now.
2. The feature is completely optional, it doesn't affect existing allocators.
   ("You don't pay for what you don't use").
3. Support for the feature can be eventually added to the system allocators.
   Today they don't provide any form of reallocation appropriate for C++
   because C++ containers don't use reallocation anyway. C++ doesn't use
   reallocation for containers because system allocators don't provide
   appropriate support... Let's break the vicious circle by adding such
   support to C++ containers first.

## Proposal

I propose to extend `std::allocator_traits` with additional function:

```C++
template<class Alloc>
struct std::allocator_traits
{
    static bool resize_allocated(Alloc &a, pointer p, size_type cur_size, size_type new_size);
};
```

It calls `a.resize_allocated(p, cur_size, new_size)` if that expression is
well-formed; otherwise, just returns `false`. Returned `true` means that:

1. The request was satisfied,
2. The memory block length was changed, and
3. It is at least `new_size` bytes length.

The main difference with `realloc()`'s behaivour is that the allocator doesn't
try to move any data, it is a caller's responsibility, the allocator just reports
the success.

## [P0401 - Extensions to the Allocator interface](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0401r0.html) by Jonathan Wakely (Bonus)

Jonathan Wakely proposes different extension in his P0401 paper, sort of
feedback from the allocator - allow allocator to tell the actual size of the
allocated block. It can be combined with the idea from the original proposal:

```C++
template<class Alloc>
struct std::allocator_traits
{
    static bool resize_allocated(Alloc &a, pointer p, size_type cur_size, size_type &new_size);
};
```

Now `new_size` is an input/output parameter. In case of success the allocator
can round up the requested size.

## [N3495 - inplace realloc](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3495.htm)

This paper proposes the similar idea but the function throws `std::bad_alloc()`
when resizing is not supported by allocator. I don't find it practical. From
the user's point of view it usually worths nothing to know about the support
in principle. The main thing which matters is the result of the resize attempt:
success or not.

## Usage (code)

The sample of usage with vector-like container (including the extension from
P0401) can be found [here](https://github.com/2underscores-vic/articles/blob/master/realloc4cpp/realloc4cpp.cpp).

### Scenario 1: grow

```C++
void push_back(T v)
{
    if(next == buf.end()) // increase capacity first
    {
        if(buf.resize(new_capacity()))
        {
            // AWESOME!!! Buffer was enlarged!
            // No need to move existing elements!
        }
        else // cannot extend, move the buffer as usual
        {
            // ...
        }
    }
    construct(next, std::move(v));
    ++next;
}
```

### Scenario 2: shrink

```C++
void shrink_to_fit()
{
    if(size() == capacity()) return;
    if(buf.resize(size()))
    {
        // AWESOME!!! Buffer was narrowed!
        // No need to move existing elements!
    }
    else // cannot narrow, allocate new buffer
    {
        // ...
    }
}
```

In the both cases `buf.resize()` is a function defined as

```C++
bool resize(size_type desired_capacity)
{
    auto n = desired_capacity;
    if(!alloc_traits::resize_allocated(a, begin_, capacity(), n))
        return false;
    assert(desired_capacity > capacity() ? n >= desired_capacity : true);
    end_ = begin_ + n;
    return true;
}
```

As it can be seen, in each scenario the code

```C++
allocate_new_buffer_and_move_data();
```

just becomes

```C++
if(!buf.resize(new_size()))
    allocate_new_buffer_and_move_data();
```

## Preferred and minimum requested size (Bonus #2)

It was suggested by Fabio Fracassi in
[std-proposals](https://groups.google.com/a/isocpp.org/d/msg/std-proposals/AeL6Q35t1j8/y869WOCRBAAJ)
list to consider adding one feature from
[N2045 - Improving STL Allocators](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2045.html)
proposal.

When we need to add `N` elements to the full vector (`size() == capacity()`),
we usually don't request additional memory for `N` elements. Instead a value
`M` >= `N` is calculated using one of known growth strategies (`new_capacity()`
function in my sample code). `M` is a "preferred size" here. It is possible
that the allocator doesn't have enough memory to expand the buffer for `M`
elements but has for `N`. So it is a reasonable strategy to request both, and
the allocator can try to satisfy at least the "minimal size" request if the
"preferred" one can't be satisfied. Of course, the allocator has the right
to adjust/align the request and allocate slightly more, as before.

The extension can have the following form (all in one):

```C++
template<class Alloc>
struct std::allocator_traits
{
    static bool resize_allocated(Alloc &a, pointer p, size_type cur_size,
        size_type &new_size);
    static bool resize_allocated(Alloc &a, pointer p, size_type cur_size,
        size_type &preferred_size, size_type min_size);
};
```

The first function calls `a.resize_allocated(p, cur_size, new_size)` if
well-formed; otherwise calls `resize_allocated(a, p, cur_size, new_size,
new_size)`.

And in my example for `push_back()` the call

```C++
buf.resize(new_capacity())
```

becomes

```C++
buf.resize(new_capacity(), 1)
```

## Acknowledgements

Thanks to Antony Polukhin for representing this proposal in the Committee and
general support.

Thanks to Fabio Fracassi for his feedback on std-proposals forum.

## Annex A. Sample code

```C++
#include<memory>
#include<utility>
#include<iterator>
#include<cassert>

namespace cpp_realloc {

//////////////////////////////////////////////////////////////////////////////
// Extended allocator_traits interface
template<class Alloc>
struct realloc_allocator_traits : public std::allocator_traits<Alloc>
{
    using pointer = typename std::allocator_traits<Alloc>::pointer;
    using size_type = typename std::allocator_traits<Alloc>::size_type;

    // `p` is a pointer to the memory block allocated before
    // `cur_size` is a current size of the memory block
    // `new_size` is IN/OUT parameter:
    //      IN: requested size
    //     OUT: reallocated size, in case of success (true) returned
    // Returns:
    //     false - cannot satisfy this request
    //      true - memory block was enlarged/narrowed. In case of enlarge-request
    //             returned `new_size` can be equal or greater than requested
    static bool resize_allocated(
        Alloc &a, pointer p, size_type cur_size, size_type &new_size)
    {
        // TODO: Return a.resize_allocated(p, cur_size, new_size) if defined
        return false;
    }
};
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
// Fixed-size memory buffer, can grow
template<class T, class Allocator = std::allocator<T>>
class raw_buffer : private Allocator
{
    T *begin_, *end_;
    using alloc_traits = realloc_allocator_traits<Allocator>;
public:
    using size_type = typename alloc_traits::size_type;

    constexpr raw_buffer() : begin_(nullptr), end_(begin_) {}
    explicit raw_buffer(size_type initial_capacity)
    :
        begin_(alloc_traits::allocate(*this, initial_capacity)),
        end_(begin_ + initial_capacity)
    {
    }
    raw_buffer(raw_buffer &&o) noexcept : begin_(o.begin_), end_(o.end_)
    {
        o.end_ = o.begin_ = nullptr;
    }
    raw_buffer(const raw_buffer & ) = delete;
    ~raw_buffer()
    {
        if(begin_) alloc_traits::deallocate(*this, begin_, capacity());
    }

    raw_buffer &operator=(raw_buffer &&o) noexcept { swap(o); }
    raw_buffer &operator=(const raw_buffer & ) = delete;

    Allocator get_allocator() const { return *this; }

    auto begin() { return begin_; }
    auto end() { return end_; }
    auto begin() const { return begin_; }
    auto end() const { return end_; }

    bool resize(size_type new_capacity)
    {
        auto n = new_capacity;
        if(!alloc_traits::resize_allocated(*this, begin_, capacity(), n))
            return false;
        assert(new_capacity > capacity() ? n >= new_capacity : true);
        end_ = begin_ + n;
        return true;
    }

    template<class... Args>
    void construct(T *p, Args &&... args)
    {
        alloc_traits::construct(*this, p, std::forward<Args>(args)...);
    }
    void destroy(T *p) { alloc_traits::destroy(*this, p); }
    void swap(raw_buffer &o) noexcept
    {
        std::swap(begin_, o.begin_);
        std::swap(end_, o.end_);
    }

    size_type capacity() const { return end_ - begin_; }
};
//////////////////////////////////////////////////////////////////////////////
// vector-like container
template<class T, class Allocator = std::allocator<T>>
class autogrow_array
{
    raw_buffer<T, Allocator> buf;
    T *next = buf.begin();
    auto new_capacity() const
    {
        // TODO: Add your grow algo here
        return capacity() + 1;
    }
public:
    using value_type = T;
    using allocator_type = Allocator;
    using size_type = typename std::allocator_traits<Allocator>::size_type;

    constexpr autogrow_array() = default;
    explicit autogrow_array(size_type );
    autogrow_array(const autogrow_array & ) = delete;
    autogrow_array &operator=(const autogrow_array & ) = delete;
    ~autogrow_array();

    allocator_type get_allocator() const { return buf.get_allocator(); }

    bool empty() const { return next == buf.begin(); }
    size_type size() const { return next - buf.begin(); }
    size_type capacity() const { return buf.capacity(); }

    void push_back(T );
    void pop_back();
    void clear();
    void shrink_to_fit();
};
//////////////////////////////////////////////////////////////////////////////
//----------------------------------------------------------------------------
template<class T, class A>
autogrow_array<T,A>::autogrow_array(size_type initial_capacity)
    : buf(initial_capacity)
{
}
//----------------------------------------------------------------------------
template<class T, class A>
autogrow_array<T,A>::~autogrow_array()
{
    clear();
}
//----------------------------------------------------------------------------
template<class T, class A>
void autogrow_array<T,A>::pop_back()
{
    assert(!empty());
    buf.destroy(next);
    --next;
}
//----------------------------------------------------------------------------
template<class T, class A>
void autogrow_array<T,A>::clear()
{
    while(!empty()) pop_back();
}
//----------------------------------------------------------------------------
template<class T, class A>
void autogrow_array<T,A>::push_back(T v)
{
    if(next == buf.end()) // increase capacity first
    {
        auto desired_capacity = new_capacity();
        if(buf.resize(desired_capacity))
        {
            // AWESOME!!! Buffer was enlarged!
            // No need to move existing elements!
            assert(capacity() >= desired_capacity);
        }
        else // cannot extend, move the buffer as usual
        {
            raw_buffer<T,A> new_buf(desired_capacity);
            // Using move even if move-ctr of T can throw for short
            next = std::uninitialized_copy(
                std::make_move_iterator(buf.begin()),
                std::make_move_iterator(next),
                new_buf.begin()
            );
            buf.swap(new_buf);
        }
    }
    buf.construct(next, std::move(v));
    ++next;
}
//----------------------------------------------------------------------------
template<class T, class A>
void autogrow_array<T,A>::shrink_to_fit()
{
    if(size() == capacity()) return;
    if(buf.resize(size()))
    {
        // AWESOME!!! Buffer was narrowed!
        // No need to move existing elements!
    }
    else // allocate new buffer
    {
        raw_buffer<T,A> new_buf(size());
        // Using move even if move-ctr of T can throw for short
        next = std::uninitialized_copy(
            std::make_move_iterator(buf.begin()),
            std::make_move_iterator(next),
            new_buf.begin()
        );
        buf.swap(new_buf);
    }
}
//----------------------------------------------------------------------------

} // namespace

#include<iostream>

int main()
{
    using namespace cpp_realloc;
    autogrow_array<int> arr(2);
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << '\n';

    std::cout << "Add element\n";
    arr.push_back(1);
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << '\n';

    std::cout << "Add element\n";
    arr.push_back(2);
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << '\n';

    std::cout << "Add element\n";
    arr.push_back(3);
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << '\n';

    std::cout << "Add element\n";
    arr.push_back(4);
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << '\n';

    std::cout << "Remove element\n";
    arr.pop_back();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << '\n';

    std::cout << "Shrink ot fit\n";
    arr.shrink_to_fit();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << '\n';
}
```
