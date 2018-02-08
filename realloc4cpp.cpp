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
