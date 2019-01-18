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
