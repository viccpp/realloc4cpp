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
