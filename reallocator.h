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
