```
Document number: P0894R1
Audience: Library Evolution Working Group
Link: https://github.com/2underscores-vic/realloc4cpp/blob/P0894R1/realloc4cpp.md
Reply to: Victor Dyachenko <__vic@ngs.ru>
Date: 2018-12-12
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
guaranteed that every resize request will be satisfied (it won't usually in
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
    [[nodiscard]] static bool resize_allocated(
        Alloc &a, pointer p, size_type cur_size, size_type new_size);
};
```

It calls `a.resize_allocated(p, cur_size, new_size)` if that expression is
well-formed; otherwise, just returns `false`. Returned `true` means that:

1. The request was satisfied,
2. The memory block length was changed, and
3. It is at least `new_size` bytes length.

The main difference with `realloc()`'s behaivour is that an allocator doesn't
try to move any data, it is a caller's responsibility, the allocator just reports
the success status.

## [P0401 - Extensions to the Allocator interface](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0401r0.html) by Jonathan Wakely (Bonus)

Jonathan Wakely proposes different extension in his P0401 paper, sort of
feedback from the allocator - allow allocator to tell the actual size of the
allocated block. It can be combined with the idea from the original proposal:

```C++
template<class Alloc>
struct std::allocator_traits
{
    [[nodiscard]] static bool resize_allocated(
        Alloc &a, pointer p, size_type cur_size, size_type &new_size);
};
```

Now `new_size` is an input/output parameter. In case of success the allocator
can round up the requested size.

Note: The Standard Library allocators operate in terms of `sizeof(T)` elements
but general purpose memory allocators usually operate in bytes. So it's an open
question what to do when memory allocator returned a value that isn't a multiple
of `sizeof(T)`.

## [N3495 - inplace realloc](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3495.htm)

This paper proposes the similar idea but the function throws `std::bad_alloc()`
when resizing is not supported by allocator. I don't find it practical. From
the user's point of view it usually worths nothing to know about the support
in principle. The main thing which matters is the result of the resize attempt:
success or not.

## Usage (code)

The sample of usage with vector-like container (including the extension from
P0401) can be found [here](https://github.com/2underscores-vic/realloc4cpp/tree/P0894R1).

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

If the allocator used doesn't implement `resize_allocated()` function
then `alloc_traits::resize_allocated()` call (and thus `buf.resize()`)
effectively turns into `if(!false)` so a smart enough compiler can
elliminate this fake check completely in the generated code.

The worst performance impact is in the case when allocator defines
`resize_allocated()` but always returns `false` and the call can't be inlined.
In such cases we will have additional (unsuccessful) function call plus
additional condition check. But if allocator is know in advance not being able
to resize allocated memory block it just shouldn't define `resize_allocated()`.

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
    [[nodiscard]] static bool resize_allocated(
        Alloc &a, pointer p, size_type cur_size, size_type &new_size);
    [[nodiscard]] static bool resize_allocated(
        Alloc &a, pointer p, size_type cur_size, size_type &preferred_size,
        size_type min_size);
};
```

The first function calls `a.resize_allocated(p, cur_size, new_size)` if
well-formed; otherwise calls `resize_allocated(a, p, cur_size, new_size,
new_size)`.

The second function calls `a.resize_allocated(p, cur_size, preferred_size,
min_size)` if well-formed. Otherwise if expession `a.resize_allocated(p,
cur_size, preferred_size)` is well-formed calls that. If the call failed and
`at_least_size != preferred_size` assigns `preferred_size = at_least_size`
and calls the expression again.

In my example for `push_back()` the call

```C++
buf.resize(new_capacity())
```

becomes

```C++
buf.resize(new_capacity(), 1)
```

## Howard Hinnant's stack_alloc and other arena/monotonic allocators

Howard Hinnant has proposed one ingenious trick to make STL containers allocate
the elements [on stack](https://howardhinnant.github.io/stack_alloc.html). It
uses monotonic buffer allocated on the stack and heap is used on its exhaustion.

When we preallocate 200 bytes buffer for vector of 4-byte `int`s we expect that
200 / 4 = **50** elements can be allocated on the stack without heap usage. But
if we run the test (see the link above) we will be disappoined... On my machine
I discover the buffer exhaustion after inserting the 17th element. Why?

Because of GNU libstc++ vector's growth strategy and constant buffer relocations.
vector's capacity is doubled on each reallocation and there is always the moment
when both old and new buffer occupy a memory. So we have the following picture:

```
+++++++++++++++++++++++++++++++++++++++++++++++++ 50
* 1
 ** 2
   **** 4
       ******** 8
               **************** 16
                               ******************************** 32
```

The buffer is exhaused on attempt to allocate 32 elements. 1+2+4+8+16=31 memory
cells just can't be reused because `deallocate()` call is a dummy.

If we let the buffer just grow the picture become:

```
+++++++++++++++++++++++++++++++++++++++++++++++++ 50
************************************************* 50
```

or

```
+++++++++++++++++++++++++++++++++++++++++++++++++ 50
* 1
** 2
**** 4
******** 8
**************** 16
******************************** 32
********************************* 33
********************************** 34
...
************************************************* 50
```

Now at least 32 elements can be allocated with the bare minimun proposal.
And all 50 if allocator adjusts the first call to 50 elements (bonus #1)
or vector always adds `min_size = 1` to every memory request (bonus #2).

## Existing practice, implementation experience and benchmarks

The functionality is available today in [jemalloc](http://jemalloc.net/)
allocator. It has `xmallocx()` function that almost literally implements the
required functionality:

```C
size_t xallocx(void *ptr, size_t size, size_t extra, int flags);
```

> The xallocx() function resizes the allocation at ptr in place to be at least
> size bytes, and returns the real size of the allocation. If extra is non-zero,
> an attempt is made to resize the allocation to be at least (size + extra) bytes,
> though inability to allocate the extra byte(s) will not by itself result in
> failure to resize.

Facebook has class [`folly::fbvector`](https://github.com/facebook/folly/blob/master/folly/FBVector.h)
that is [able](https://github.com/facebook/folly/blob/master/folly/docs/FBVector.md#the-jemalloc-connection)
to use [this function](https://github.com/facebook/folly/blob/v2018.12.10.00/folly/FBVector.h#L1634)
to expand the capacity. We can do the same in STL but in allocator-agnostic way
using an allocator like this:

```C++
template<class T>
struct reallocator
{
    using value_type = T;
    using size_type = std::size_t;
    using is_always_equal = std::true_type;

    reallocator() = default;
    template<class U>
    constexpr reallocator(const reallocator<U> &) noexcept {}

    [[nodiscard]] T *allocate(size_type n)
    {
        void *p = je_mallocx(n * sizeof(T), 0);
        if(!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T *p, size_type n)
    {
        je_sdallocx(p, n, 0);
    }
    [[nodiscard]] bool resize_allocated(T *p,
        size_type cur_size, size_type &preferred_size, size_type at_least_size)
    {
        auto new_size_bytes = je_xallocx(p,
            at_least_size * sizeof(T),
            (preferred_size - at_least_size) * sizeof(T),
            0
        );
        auto new_size = new_size_bytes / sizeof(T);
        if(new_size > cur_size)
        {
            preferred_size = new_size;
            return true;
        }
        return false;
    }
};
template<class U, class V>
inline bool operator==(reallocator<U>, reallocator<V>) { return true; }
template<class U, class V>
inline bool operator!=(reallocator<U>, reallocator<V>) { return false; }
```

But are there any real benefits in the performance? Let's try to figure this out.
We will use vector-like container aware of our `resize_allocated()` function with
`std::allocator` and then with `reallocator`. Measurement will be done for single
`push_back()` call in situation when `size() == capacity()` in CPU clocks 10 times.

Results for `int` elements:

```
std::allocator: 25166 | 29296 | 25784 | 25052 | 26060 | 25402 | 24776 | 24774 | 24622 | 24552
reallocator:    13302 |  8650 |  8594 |  8124 |  8300 | 10826 |  8794 |  8708 |  8518 |  8650
```

The mean values are:

- 25548 for `std::allocator` vs
- 9246 for `reallocator` with successfull in-place expansion

so `reallocator` gives us **2.76** times performance gain on successful expansion
call.

But let's try the same but for heavier object like `std::string`. The results
in the same test case:

```
std::allocator: 124588 | 118914 | 119290 | 117594 | 143100 | 118296 | 119214 | 117896 | 116890 | 118542
reallocator:     14694 |  10526 |  10318 |  10234 |  10730 |  10466 |  10825 |  11184 |  10804 |  10874
```

The mean values are:

- 121432 for `std::allocator` vs
- 11066 for `reallocator`

and performance gain is now **10.97** times!

Benchmark conditions:

CPU: Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz (64 bit)
OS: CentOS Linux release 7.0.1406 (kernel 3.10.0-327.el7.x86_64)
jemalloc: v 5.1.0 (statically linked)
compiler: GCC v 8.2 (-O3 -flto)

## Summary: What is proposed?

1. Add one of the proposed forms of `resize_allocated()` to `std::allocator_traits`
   (we recommend bonus #2 with or w/o bonus #1).
2. Make `std::vector` and `std::string` use `std::allocator_traits::resize_allocated()`.

This two adoptions are enough to give the users an opportunity to effectively
use the standard containers with custom allocators like jemalloc or some sort
of arena/monotonic allocator. At the moment users have to use custom ad-hoc
containers like `folly::fbvector`. So advanced users can benefit in the short
run.

But it would be much better for the users while not being aware of allocators
at all still automatically use the feature every time they use `std::vector` or
`std::string`. To achieve that we need to

3. Add `resize_allocated()` to `std::allocator` either mandatory or up to
   the library implementation.

The last item of the proposal is good to have but it can be postponed.

## Acknowledgements

Thanks to Antony Polukhin for representing this proposal in the Committee and
general support.

Thanks to Fabio Fracassi for his feedback on std-proposals forum.

## Annex A. Sample code

```C++
#ifndef __ALLOCATOR_TRAITS_H
#define __ALLOCATOR_TRAITS_H

#include<memory>

namespace realloc4cpp {

//////////////////////////////////////////////////////////////////////////////
// Extended allocator_traits interface
template<class Alloc>
struct allocator_traits : public std::allocator_traits<Alloc>
{
    using typename std::allocator_traits<Alloc>::pointer;
    using typename std::allocator_traits<Alloc>::size_type;
private:
    template<class Alloc2>
    static auto resize_allocated_at_least(
        Alloc2 &a, pointer p, size_type cur_size,
        size_type &preferred_size, size_type at_least_size, int)
    -> decltype(a.resize_allocated(p, cur_size, preferred_size, at_least_size))
    {
        return a.resize_allocated(p, cur_size, preferred_size, at_least_size);
    }
    template<class Alloc2>
    static auto resize_allocated_at_least(
        Alloc2 &a, pointer p, size_type cur_size,
        size_type &preferred_size, size_type at_least_size, int)
    -> decltype(a.resize_allocated(p, cur_size, preferred_size))
    {
        auto new_size = preferred_size;
        if(a.resize_allocated(p, cur_size, preferred_size)) return true;
        if(at_least_size == new_size) return false;
        preferred_size = at_least_size;
        return a.resize_allocated(p, cur_size, preferred_size);
    }

    template<class Alloc2>
    static bool resize_allocated_at_least(
        Alloc2 & , pointer , size_type , size_type & , size_type , ...)
    {
        return false;
    }

    template<class Alloc2>
    static auto resize_allocated_exact(
        Alloc2 &a, pointer p, size_type cur_size, size_type &new_size, int)
    -> decltype(a.resize_allocated(p, cur_size, new_size))
    {
        return a.resize_allocated(p, cur_size, new_size);
    }
    template<class Alloc2>
    static bool resize_allocated_exact(
        Alloc2 &a, pointer p, size_type cur_size, size_type &new_size, ...)
    {
        return resize_allocated_at_least(a, p, cur_size, new_size, new_size);
    }
public:
    // `p` is a pointer to the memory block allocated before
    // `cur_size` is a current size of the memory block
    // `new_size` is IN/OUT parameter:
    //      IN: requested size
    //     OUT: reallocated size, in case of success (true) returned
    // Returns:
    //     false - cannot satisfy this request
    //      true - memory block was enlarged/narrowed. In case of enlarge-request
    //             returned `new_size` can be equal or greater than requested
    // Evaluates and returns the result of the the first
    // well-formed expession in the following order:
    //     1) a.resize_allocated(p, cur_size, new_size)
    //     2) a.resize_allocated(p, cur_size, new_size, new_size)
    //     3) false
    [[nodiscard]] static bool resize_allocated(Alloc &a, pointer p,
        size_type cur_size, size_type &new_size)
    {
        return resize_allocated_exact(a, p, cur_size, new_size, 0);
    }

    // Same as above but tries `preferred_size` as a `new_size` first
    // If failed tries `at_least_size` when  `at_least_size` != `preferred_size`
    // 1) If expression
    //    a.resize_allocated(p, cur_size, preferred_size, at_least_size)
    //    is well-formed calls and returns that;
    // 2) Otherwise if expession
    //    a.resize_allocated(p, cur_size, preferred_size)
    //    is well-formed calls that. If the call failed and
    //    at_least_size != preferred_size (initial value) assigns
    //    preferred_size = at_least_size and calls the expression again;
    // 3) Otherwise returns false.
    [[nodiscard]] static bool resize_allocated(Alloc &a, pointer p,
        size_type cur_size, size_type &preferred_size, size_type at_least_size)
    {
        return resize_allocated_at_least(a, p,
            cur_size, preferred_size, at_least_size, 0);
    }
};
//////////////////////////////////////////////////////////////////////////////

} // namespace

#endif // header guard
```

```C++
#ifndef __REALLOCATOR_H
#define __REALLOCATOR_H

#include<new>
#include<type_traits>
#include<jemalloc/jemalloc.h>

namespace realloc4cpp {

//////////////////////////////////////////////////////////////////////////////
template<class T>
struct reallocator
{
    using value_type = T;
    using size_type = std::size_t;
    using is_always_equal = std::true_type;

    reallocator() = default;
    template<class U>
    constexpr reallocator(const reallocator<U> &) noexcept {}

    [[nodiscard]] T *allocate(size_type n)
    {
        void *p = je_mallocx(n * sizeof(T), 0);//MALLOCX_ALIGN(alignof(T)));
        if(!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T *p, size_type n)
    {
        je_sdallocx(p, n, 0);
    }
    [[nodiscard]] bool resize_allocated(T *p,
        size_type cur_size, size_type &preferred_size, size_type at_least_size)
    {
        auto new_size_bytes = je_xallocx(p,
            at_least_size * sizeof(T),
            (preferred_size - at_least_size) * sizeof(T),
            0//MALLOCX_ALIGN(alignof(T))
        );
        auto new_size = new_size_bytes / sizeof(T);
        if(new_size > cur_size)
        {
            preferred_size = new_size;
            return true;
        }
        return false;
    }
};
//////////////////////////////////////////////////////////////////////////////
template<class U, class V>
inline bool operator==(reallocator<U>, reallocator<V>) { return true; }
template<class U, class V>
inline bool operator!=(reallocator<U>, reallocator<V>) { return false; }

} // namespace

#endif // header guard
```

```C++
#include"reallocator.h"
#include"allocator_traits.h"
#include<utility>
#include<iterator>
#include<cassert>

namespace realloc4cpp {

unsigned long realloc_attempts = 0, successful_reallocs = 0;

//////////////////////////////////////////////////////////////////////////////
// Fixed-size memory buffer, can grow
template<class T, class Allocator = std::allocator<T>>
class raw_buffer : private Allocator
{
    T *begin_, *end_;
    using A = allocator_traits<Allocator>;
public:
    using size_type = typename A::size_type;

    constexpr raw_buffer() : begin_(nullptr), end_(begin_) {}
    explicit raw_buffer(size_type initial_capacity)
    :
        begin_(A::allocate(*this, initial_capacity)),
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
        if(begin_) A::deallocate(*this, begin_, capacity());
    }

    raw_buffer &operator=(raw_buffer &&o) noexcept { swap(o); }
    raw_buffer &operator=(const raw_buffer & ) = delete;

    Allocator get_allocator() const { return *this; }

    auto begin() { return begin_; }
    auto end() { return end_; }
    auto begin() const { return begin_; }
    auto end() const { return end_; }

    bool resize(size_type new_capacity, size_type min_new_capacity)
    {
        assert(new_capacity >= min_new_capacity);
        realloc_attempts++;
        auto n = new_capacity;
        if(!A::resize_allocated(*this,
                begin_, capacity(), n, min_new_capacity))
            return false;
        assert(new_capacity > capacity() ?
            n >= new_capacity || n >= min_new_capacity : true);
        end_ = begin_ + n;
        successful_reallocs++;
        return true;
    }
    bool resize(size_type new_capacity)
    {
        return resize(new_capacity, new_capacity);
    }

    template<class... Args>
    void construct(T *p, Args &&... args)
    {
        A::construct(*this, p, std::forward<Args>(args)...);
    }
    void destroy(T *p) { A::destroy(*this, p); }
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
        return capacity() * 2U;
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
autogrow_array<T,A>::autogrow_array(size_type initial_size)
:
    buf(initial_size),
    next(std::uninitialized_fill_n(buf.begin(), initial_size, T{}))
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
        if(buf.resize(desired_capacity, 1))
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

inline unsigned long long rdtsc()
{
    unsigned aux;
    return __builtin_ia32_rdtscp(&aux);
}

int main()
{
    using namespace realloc4cpp;
    // Chunks <16KiB are allocated using slabs and cannot be resized so we
    // start with this minimal size (http://jemalloc.net/jemalloc.3.html)
    autogrow_array<int
#if 1
        , reallocator<int>
#endif
    > arr(4U << 10);
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << '\n';

    std::cout << "Add element\n";
    auto t1 = rdtsc();
    arr.push_back(1);
    auto t2 = rdtsc();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << ", time: " << (t2 - t1) << '\n';

    std::cout << "Add element\n";
    t1 = rdtsc();
    arr.push_back(2);
    t2 = rdtsc();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << ", time: " << (t2 - t1) << '\n';

    std::cout << "Add element\n";
    t1 = rdtsc();
    arr.push_back(3);
    t2 = rdtsc();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << ", time: " << (t2 - t1) << '\n';

    std::cout << "Add element\n";
    t1 = rdtsc();
    arr.push_back(4);
    t2 = rdtsc();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << ", time: " << (t2 - t1) << '\n';

    std::cout << "Remove element\n";
    t1 = rdtsc();
    arr.pop_back();
    t2 = rdtsc();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << ", time: " << (t2 - t1) << '\n';

    std::cout << "Shrink ot fit\n";
    t1 = rdtsc();
    arr.shrink_to_fit();
    t2 = rdtsc();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << ", time: " << (t2 - t1) << '\n';

    std::cout << successful_reallocs << " of " << realloc_attempts <<
        " successful reallocations\n";
}
```
