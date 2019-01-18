```
Document number: P0894R1
Audience: Library Evolution Working Group
Link: https://github.com/2underscores-vic/realloc4cpp/blob/P0894R1/realloc4cpp.md
Reply to: Victor Dyachenko <__vic@ngs.ru>
Date: 2019-01-18
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

One can argue that today system allocators usually don't support such feature.
Well.

1. At least custom allocators can benefit right now.
2. The feature is completely optional, it doesn't affect existing allocators.
   ("You don't pay for what you don't use").
3. Support for the feature can be eventually added to the system allocators.
   Today they usually don't provide any form of reallocation appropriate for
   C++ because C++ containers don't use reallocation anyway. C++ doesn't use
   reallocation for containers because system allocators don't provide
   appropriate support... Let's break the vicious circle by adding such
   support to C++ containers first.

## Proposal

I propose to extend `std::allocator_traits` with additional functions:

```C++
template<class Alloc>
struct std::allocator_traits
{
    [[nodiscard]] static constexpr bool expand_by(
        Alloc &a, pointer p, size_type cur_size, size_type n);
    [[nodiscard]] static constexpr bool shrink_by(
        Alloc &a, pointer p, size_type cur_size, size_type n);
};
```

They call `a.expand_by(p, cur_size, n)` and `a.shrink_by(p, cur_size, n)`
respectively if that expression is well-formed; otherwise, just return `false`.
Returned `true` means that:

1. The request was satisfied,
2. The memory block length was changed, and
3. It is at least `cur_size + n` bytes length in `expand_by()` case or
   less than `cur_size` in `shrink_by()` case.

The main difference with `realloc()`'s behaivour is that an allocator doesn't
try to move any data, it is a caller's responsibility, the allocator just reports
success status.

It also can be implemented as a single function, like
`resize(a, p, cur_size, new_size)`, but such functions internally have code like
this:

```C++
if(new_size > cur_size) expand(...);
else if(new_size < cur_size) shrink(...);
```

so our approach is more clear and potentially more effective. Moreover when
this functions are used in `std::vector` or `std::string` we always know
what we want in every call point: to expand or to shrink.

`shrink_by()` function should return `false` even if the buffer can be shrunk
in-place but result closer to the requested `n` can be achieved using buffer
relocation.

## [N3495 - inplace realloc](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3495.htm)

This paper proposes the similar idea but the function throws `std::bad_alloc()`
when resizing is not supported by allocator. I don't find it practical. From
the user's point of view it usually worths nothing to know about the support
in principle. The main thing which matters is the result of the resize attempt:
success or not.

## [P0401 - Extensions to the Allocator interface](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0401r0.html) by Jonathan Wakely (Bonus #1)

Jonathan Wakely proposes different extension in his P0401 paper, sort of
feedback from the allocator - allow allocator to tell the actual size of the
allocated block. It can be combined with the idea from the original proposal:

```C++
template<class Alloc>
struct std::allocator_traits
{
    [[nodiscard]] static constexpr bool expand_by(
        Alloc &a, pointer p, size_type &size, size_type n);
    [[nodiscard]] static constexpr bool shrink_by(
        Alloc &a, pointer p, size_type &size, size_type n);
};
```

Now `size` is an input/output parameter. In case of success the allocator
can round up the requested size.

Note: The Standard Library allocators operate in terms of `sizeof(T)` elements
but general purpose memory allocators usually operate in bytes. So it's an open
question what to do when memory allocator returned a value that isn't a multiple
of `sizeof(T)`. Is specifying alignment as `alignas(T)` enough in such cases?

Actually we can have the same feedback not only for reallocation but for initial
allocation too in the same way. Just add one more optional function to the
allocator's interface and make `std::vector` use it via `allocator_traits`:

```C++
template<class Alloc>
struct std::allocator_traits
{
    [[nodiscard]] static constexpr pointer allocate_at_least(
        Alloc &a, size_type &size);
};
```

This function calls `a.allocate_at_least(size)` if defined or just
`a.allocate(size)` otherwise. `size` is an input/output parameter that returns
the actual allocated size.

## Usage (code)

The sample of usage with vector-like container (including the extension from
P0401) can be found [here](https://github.com/2underscores-vic/realloc4cpp/tree/P0894R1).

### Scenario 1: grow

```C++
void push_back(T v)
{
    if(next == buf.end()) // increase capacity first
    {
        if(buf.expand_by_at_least(buf.additional_capacity(1)))
        {
            // AWESOME!!! Buffer was expanded!
            // No need to move existing elements!
        }
        else // cannot expand, move the buffer as usual
        {
            // ...
        }
    }
    construct(next, std::move(v));
    ++next;
}
```

```C++
bool expand_by_at_least(size_type n)
{
    size_type capacity = this->capacity();
    if(!alloc_traits::expand_by(a, begin_, capacity, n)) return false;
    end_ = begin_ + capacity;
    return true;
}
```

### Scenario 2: shrink

```C++
void shrink_to_fit()
{
    if(size() == capacity()) return;
    if(buf.shrink_by(capacity() - size()))
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

```C++
bool shrink_by(size_type n)
{
    size_type capacity = this->capacity();
    if(!alloc_traits::shrink_by(a, begin_, capacity, n)) return false;
    end_ = begin_ + capacity;
    return true;
}
```

As it can be seen, in each scenario the code

```C++
allocate_new_buffer_and_move_data();
```

just becomes

```C++
if(!buf.resize_by(n))
    allocate_new_buffer_and_move_data();
```

If the allocator used doesn't implement `expand_by()` or `shrink_by()` functions
then `alloc_traits` call (and thus `buf.{expand|shrink}_by()`)
effectively turns into `if(!false)` (then into `if(false)`) so a smart enough
compiler can elliminate this fake check completely in the generated code.

The worst performance impact is in the case when allocator defines
these functions but they always return `false` and the call can't be inlined.
In such cases we will have additional (unsuccessful) function call plus
additional condition check. But if allocator is know in advance not being able
to resize allocated memory block it just shouldn't define `expand_by()` and
`shrink_by()` functions.

## Preferred and minimum requested size (Bonus #2)

It was suggested by Fabio Fracassi in
[std-proposals](https://groups.google.com/a/isocpp.org/d/msg/std-proposals/AeL6Q35t1j8/y869WOCRBAAJ)
list to consider adding one feature from
[N2045 - Improving STL Allocators](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2045.html)
proposal.

When we need to add `N` elements to the full vector (`size() == capacity()`),
we usually don't request additional memory for `N` elements. Instead a value
`M` >= `N` is calculated using one of the known growth strategies
(`additional_capacity()` function in my sample code). `M` is a "preferred number
of additional elements" here. It is possible that the allocator doesn't have
enough memory to expand the buffer for `M` additional elements but has for `N`.
So it is a reasonable strategy to request both, and the allocator can try to
satisfy at least the "least" request if the "preferred" one can't be satisfied.
Of course, the allocator has the right to adjust/align the request and allocate
slightly more, as before.

The extension can have the following form (all in one):

```C++
template<class Alloc>
struct std::allocator_traits
{
    [[nodiscard]] static constexpr bool expand_by(
        Alloc &a, pointer p, size_type &size,
        size_type preferred_n, size_type least_n);
    // shrink_by() is the same
};
```

This function calls `a.expand_by(p, size, preferred_n, least_n)` if well-formed.

In my example for `push_back()` the call

```C++
buf.expand_by_at_least(buf.additional_capacity(1))
```

becomes

```C++
buf.expand_by_at_least(buf.additional_capacity(1), 1)
```

## Howard Hinnant's stack_alloc and other arena/monotonic allocators

Howard Hinnant has proposed one ingenious trick to make STL containers allocate
the elements [on stack](https://howardhinnant.github.io/stack_alloc.html). It
uses monotonic buffer allocated on the stack and heap is used on its exhaustion.

When we preallocate 200 bytes buffer for vector of 4-byte `int`s we expect that
200 / 4 = **50** elements can be allocated on the stack without heap usage. But
if we run the test (see the link above) we will be disappoined... On my machine
I discover the buffer exhaustion on insertion of the 17th element. Why?

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

If we let the buffer just grow the picture becomes:

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
or vector always adds `least_n = 1` to every memory request (bonus #2).

## Existing practice, implementation experience and benchmarks

Required functionality is available today in [jemalloc](http://jemalloc.net/)
allocator. It has `xmallocx()` function that almost literally implements what
we need:

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
template<class T, std::size_t Alignment = alignof(T)>
struct reallocator
{
    using value_type = T;
    using size_type = std::size_t;
    using is_always_equal = std::true_type;
    template<class U> struct rebind { using other = reallocator<U>; };

    reallocator() = default;
    template<class U, std::size_t A2>
    constexpr reallocator(const reallocator<U,A2> &) noexcept {}

    [[nodiscard]] T *allocate(size_type n)
    {
        void *p = je_mallocx(n * sizeof(T), MALLOCX_ALIGN(Alignment));
        if(!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    [[nodiscard]] T *allocate_at_least(size_type &n)
    {
        auto *p = allocate(n);
        n = je_sallocx(p, MALLOCX_ALIGN(Alignment)) / sizeof(T);
        return p;
    }
    void deallocate(T *p, size_type n)
    {
        je_sdallocx(p, n, 0);
    }
    [[nodiscard]] bool expand_by(T *p,
        size_type &size, size_type preferred_n, size_type least_n)
    {
        const auto old_size = size;
        const auto new_size_bytes = je_xallocx(p,
            (old_size + least_n) * sizeof(T),
            (preferred_n - least_n) * sizeof(T),
            MALLOCX_ALIGN(Alignment)
        );
        const auto new_size = new_size_bytes / sizeof(T);
        if(new_size <= old_size) return false;
        size = new_size;
        return true;
    }
    [[nodiscard]] bool shrink_by(T *p, size_type &size, size_type n)
    {
        const auto old_size = size;
        const auto new_size_bytes = je_xallocx(p,
            (size - n) * sizeof(T), 0, MALLOCX_ALIGN(Alignment));
        const auto new_size = new_size_bytes / sizeof(T);
        if(new_size >= old_size) return false;
        size = new_size;
        return true;
    }
};
//////////////////////////////////////////////////////////////////////////////
template<class U, std::size_t A1, class V, std::size_t A2>
inline bool operator==(reallocator<U,A1>, reallocator<V,A2>) { return true; }
template<class U, std::size_t A1, class V, std::size_t A2>
inline bool operator!=(reallocator<U,A1>, reallocator<V,A2>) { return false; }
```

But are there any real benefits in the performance? Let's try to figure this
out. We will use vector-like container aware of our `expand_by()/shrink_by()`
functions with `std::allocator` and then with `reallocator`. Measurement will
be done for single `push_back()` call in situation when `size() == capacity()`
in CPU clocks 10 times.

Results for `int` elements:

```
std::allocator: 24650 | 24142 | 24472 | 24240 | 24452 | 25670 | 24324 | 24934 | 25524 | 23936
reallocator:    12380 |  8834 |  8682 |  8650 |  9464 |  9524 |  9150 |  8712 |  8396 |  8974

```

The mean values are:

- 24634 for `std::allocator` vs
- 9277 for `reallocator` with successfull in-place expansion

so `reallocator` gives us **2.66** times performance gain on successful expansion
call.

But let's try the same but for heavier object like `std::string`. The results
in the same test case:

```
std::allocator: 117792 | 115506 | 147692 | 115670 | 115432 | 115978 | 116404 | 115796 | 116892 | 117628
reallocator:     10388 |   9492 |   9978 |   8192 |   8636 |   8538 |  10402 |  10134 |   9886 |   9362
```

The mean values are:

- 119479 for `std::allocator` vs
- 9501 for `reallocator`

and performance gain is now **12.58** times!

Now let's check shrinking.

Shrinking `int`s:

```
std::allocator: 12154 | 12554 | 12356 | 12340 | 12290 | 12412 | 12318 | 12906 | 12397 | 12876
reallocator:     2730 |  1764 |  2054 |  1920 |  1992 |  1878 |  1824 |  1854 |  1856 |  1954
```

The mean values are 12460 vs 1983 - **6.28** times faster.

Shrinking `std::string`s:

```
std::allocator: 92140 | 87100 | 91216 | 93266 | 117312 | 92704 | 116548 | 85180 | 90478 | 87596
reallocator:     2578 |  2562 |  2534 |  2116 |   2246 |  2202 |   2676 |  2576 |  2732 |  2594
```

The mean values are 95354 vs 2482 - **38.42** times faster.

Performance gain summary:

```
            | expand | shrink
int         |  2.66  |  6.28
std::string | 12.58  | 38.42
```

Benchmark conditions:

```
CPU: Intel(R) Xeon(R) CPU E5-2690 v4 @ 2.60GHz (64 bit)
OS: CentOS Linux release 7.0.1406 (kernel 3.10.0-327.el7.x86_64)
jemalloc: v 5.1.0 (statically linked)
compiler: GCC v 8.2 (-O3 -flto)
```

More detailed benchmarks output can be found [here](https://github.com/2underscores-vic/realloc4cpp/tree/P0894R1/stats).

We also managed to make `std::vector` from GNU libstdc++ use our reallocator
with quite low efforts. To achive this we added 2 functions to the class:

```C++
bool _M_expand_buffer_by(size_type preferred_n, size_type least_n)
{
    size_type capacity = this->capacity();
    if(!reallocator_traits<_Alloc>::expand_by(this->_M_impl,
        this->_M_impl._M_start, capacity, preferred_n, least_n)) return false;
    this->_M_impl._M_end_of_storage = this->_M_impl._M_start + capacity;
    return true;
}
bool _M_shrink_buffer_by(size_type __n)
{
    size_type capacity = this->capacity();
    if(!reallocator_traits<_Alloc>::shrink_by(this->_M_impl,
        this->_M_impl._M_start, capacity, __n)) return false;
    this->_M_impl._M_end_of_storage = this->_M_impl._M_start + capacity;
    return true;
}
```

and function

```C++
size_type _M_additional_capacity(size_type __n)
{
    size_type cap = capacity();
    const size_type cap_remain = max_size() - cap;
    if(__n > cap_remain) __throw_length_error(__N("Exceeded max_size()"));
    return std::min(cap, cap_remain);
}
```

that replaces `_M_check_len()` for our purposes - it does the same but returns
only increment for capacity not the total resulting capacity.

`push_back()/emplace_back()` functions have the check:

```C++
if (this->_M_impl._M_finish != this->_M_impl._M_end_of_storage)
```

We replaced it with

```C++
if (this->_M_impl._M_finish != this->_M_impl._M_end_of_storage ||
    _M_expand_buffer_by(_M_additional_capacity(n), n)))
```

where `n` is a number of new elements (that is `1` in case of `push_back()`
et al.)

In `_M_shrink_to_fit()` (that is called by `shrink_to_fit()`) just

```C++
if(_M_shrink_buffer_by(capacity() - size())) return true;
```

was added before the last

```C++
return std::__shrink_to_fit_aux<vector>::_S_do_it(*this);
```

Now if we use `reallocator` as an allocator (or replace `std::allocator`
implementation too) `std::vector` is able to expand and shrink storage
on `push_back()` and `shrink_to_fit()` calls!

```C++
std::vector<int> v(4U << 10);
std::cout << "capacity = " << v.capacity() << ", size = " << v.size() << '\n';

std::cout << "Add element\n";
v.push_back(1);
std::cout << "capacity = " << v.capacity() << ", size = " << v.size() << '\n';

std::cout << successful_reallocs << " of " << realloc_attempts <<
    " successful reallocations\n";

std::cout << "Remove element\n";
v.pop_back();
std::cout << "capacity = " << v.capacity() << ", size = " << v.size() << '\n';

std::cout << "Shrink ot fit\n";
v.shrink_to_fit();
std::cout << "capacity = " << v.capacity() << ", size = " << v.size() << '\n';

std::cout << successful_reallocs << " of " << realloc_attempts <<
    " successful reallocations\n";
```

prints:

```
capacity = 4096, size = 4096
Add element
capacity = 8192, size = 4097
1 of 1 successful reallocations
Remove element
capacity = 8192, size = 4096
Shrink ot fit
capacity = 4096, size = 4096
2 of 2 successful reallocations
```

## Summary: What is proposed?

1. Add one of the proposed forms of `expand_by()` and `shrink_by()` to
   `std::allocator_traits` (we recommend bonus #2 or bonus #1).
2. Make `std::vector` and `std::string` use this new functions.

This two adoptions are enough to give the users an opportunity to effectively
use the standard containers with custom allocators like jemalloc or some sort
of arena/monotonic allocator. At the moment users have to use custom ad-hoc
containers like `folly::fbvector`. So advanced users can benefit in the short
run.

But it would be much better for the users while not being aware of allocators
at all still automatically use the feature every time they use `std::vector` or
`std::string`. To achieve that we need to

3. Add `expand_by()` and `shrink_by()` to `std::allocator` either mandatory or
   up to the library implementation.

The last item of the proposal is good to have but it can be postponed.

## Acknowledgements

Thanks to Antony Polukhin for representing this proposal in the Committee and
general support.

Thanks to Fabio Fracassi for his feedback on std-proposals forum.

## Annex A. Benchmark code

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
    static constexpr auto allocate_at_least_impl(Alloc2 &a, size_type &n, int)
    -> decltype(a.allocate_at_least(n))
    {
        return a.allocate_at_least(n);
    }
    template<class Alloc2>
    static constexpr pointer allocate_at_least_impl(Alloc2 &a, size_type &n, ...)
    {
        return a.allocate(n);
    }

    template<class Alloc2>
    static constexpr auto expand_by_impl(Alloc2 &a, pointer p, size_type &size,
        size_type preferred_n, size_type least_n, int)
    -> decltype(a.expand_by(p, size, preferred_n, least_n))
    {
        return a.expand_by(p, size, preferred_n, least_n);
    }
    template<class Alloc2>
    static constexpr bool expand_by_impl(
        Alloc2 & , pointer , size_type & , size_type , size_type , ...)
    {
        return false;
    }

    template<class Alloc2>
    static constexpr auto shrink_by_impl(
        Alloc2 &a, pointer p, size_type &size, size_type n, int)
    -> decltype(a.shrink_by(p, size, n))
    {
        return a.shrink_by(p, size, n);
    }
    template<class Alloc2>
    static constexpr bool shrink_by_impl(
        Alloc2 & , pointer , size_type & , size_type , ...)
    {
        return false;
    }
public:
    [[nodiscard]] static constexpr pointer allocate_at_least(
        Alloc &a, size_type &n)
    {
        return allocate_at_least_impl(a, n, 0);
    }
    [[nodiscard]] static constexpr bool expand_by(Alloc &a, pointer p,
        size_type &size, size_type preferred_n, size_type least_n)
    {
        return expand_by_impl(a, p, size, preferred_n, least_n, 0);
    }
    [[nodiscard]] static constexpr bool shrink_by(Alloc &a, pointer p,
        size_type &size, size_type n)
    {
        return shrink_by_impl(a, p, size, n, 0);
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
template<class T, std::size_t Alignment = alignof(T)>
struct reallocator
{
    using value_type = T;
    using size_type = std::size_t;
    using is_always_equal = std::true_type;
    template<class U> struct rebind { using other = reallocator<U>; };

    reallocator() = default;
    template<class U, std::size_t A2>
    constexpr reallocator(const reallocator<U,A2> &) noexcept {}

    [[nodiscard]] T *allocate(size_type n)
    {
        void *p = je_mallocx(n * sizeof(T), MALLOCX_ALIGN(Alignment));
        if(!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    [[nodiscard]] T *allocate_at_least(size_type &n)
    {
        auto *p = allocate(n);
        n = je_sallocx(p, MALLOCX_ALIGN(Alignment)) / sizeof(T);
        return p;
    }
    void deallocate(T *p, size_type n)
    {
        je_sdallocx(p, n, 0);
    }
    [[nodiscard]] bool expand_by(T *p,
        size_type &size, size_type preferred_n, size_type least_n)
    {
        const auto old_size = size;
        const auto new_size_bytes = je_xallocx(p,
            (old_size + least_n) * sizeof(T),
            (preferred_n - least_n) * sizeof(T),
            MALLOCX_ALIGN(Alignment)
        );
        const auto new_size = new_size_bytes / sizeof(T);
        if(new_size <= old_size) return false;
        size = new_size;
        return true;
    }
    [[nodiscard]] bool shrink_by(T *p, size_type &size, size_type n)
    {
        const auto old_size = size;
        const auto new_size_bytes = je_xallocx(p,
            (size - n) * sizeof(T), 0, MALLOCX_ALIGN(Alignment));
        const auto new_size = new_size_bytes / sizeof(T);
        if(new_size >= old_size) return false;
        size = new_size;
        return true;
    }
};
//////////////////////////////////////////////////////////////////////////////
template<class U, std::size_t A1, class V, std::size_t A2>
inline bool operator==(reallocator<U,A1>, reallocator<V,A2>) { return true; }
template<class U, std::size_t A1, class V, std::size_t A2>
inline bool operator!=(reallocator<U,A1>, reallocator<V,A2>) { return false; }

} // namespace

#endif // header guard
```

```C++
#include"reallocator.h"
#include"allocator_traits.h"
#include<stdexcept>
#include<utility>
#include<iterator>
#include<algorithm>
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
        begin_(A::allocate_at_least(*this, initial_capacity)),
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

    size_type additional_capacity(size_type n) const
    {
        size_type cap = capacity();
        const size_type cap_remain = max_capacity() - cap;
        if(n > cap_remain) throw std::length_error("Exceeded max_size()");
        return std::min(cap, cap_remain);
    }
    bool expand_by_at_least(size_type preferred_n, size_type least_n)
    {
        realloc_attempts++;
        size_type capacity = this->capacity();
        if(!A::expand_by(*this, begin_,
            capacity, preferred_n, least_n)) return false;
        end_ = begin_ + capacity;
        successful_reallocs++;
        return true;
    }
    bool shrink_by(size_type n)
    {
        realloc_attempts++;
        size_type capacity = this->capacity();
        if(!A::shrink_by(*this, begin_, capacity, n)) return false;
        end_ = begin_ + capacity;
        successful_reallocs++;
        return true;
    }

    template<class... Args>
    void construct(T *p, Args&&... args)
    {
        A::construct(*this, p, std::forward<Args>(args)...);
    }
    void destroy(T *p) { A::destroy(*this, p); }
    void swap(raw_buffer &o) noexcept
    {
        std::swap(begin_, o.begin_);
        std::swap(end_, o.end_);
        static_assert(typename A::is_always_equal(),
            "Add swap for the allocator here!");
    }

    size_type max_capacity() const { return ~size_type(0) / sizeof(T); }
    size_type capacity() const { return end_ - begin_; }
};
//////////////////////////////////////////////////////////////////////////////
// vector-like container
template<class T, class Allocator = std::allocator<T>>
class autogrow_array
{
    raw_buffer<T, Allocator> buf;
    T *next = buf.begin();
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
    size_type max_size() const { return buf.max_capacity(); }
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
        auto add_cap = buf.additional_capacity(1);
        if(buf.expand_by_at_least(add_cap, 1))
        {
            // AWESOME!!! Buffer was enlarged!
            // No need to move existing elements!
        }
        else // cannot extend, move the buffer as usual
        {
            raw_buffer<T,A> new_buf(size() + add_cap);
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
    if(buf.shrink_by(capacity() - size()))
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
    > arr((size_t(16) << 10) / sizeof(int));
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
    //for(int i = 3; i--;) arr.pop_back();
    t1 = rdtsc();
    arr.shrink_to_fit();
    t2 = rdtsc();
    std::cout << "capacity = " << arr.capacity() << ", size = " << arr.size() << ", time: " << (t2 - t1) << '\n';

    std::cout << successful_reallocs << " of " << realloc_attempts <<
        " successful reallocations\n";
}
```
