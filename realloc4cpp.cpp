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
