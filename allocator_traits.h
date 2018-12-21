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
