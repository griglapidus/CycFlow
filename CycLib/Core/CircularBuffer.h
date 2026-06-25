// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CIRCULARBUFFER_H
#define CYC_CIRCULARBUFFER_H

#include <vector>
#include <iterator>
#include <algorithm>
#include <cassert>
#include <type_traits>
#include <utility>

namespace cyc {

// =============================================================================
//  CircularBuffer — single-threaded ring buffer
// =============================================================================

/**
 * @class CircularBuffer
 * @brief Single-threaded circular buffer container with an STL-like API.
 *
 * Implements all ring-index arithmetic and element lifetime management over a
 * dynamic array. It performs **no synchronisation whatsoever**: every method
 * assumes the caller guarantees exclusive (for mutators) or shared (for const
 * accessors) access by some external means — either single-threaded use (e.g.
 * ChartModel on the UI thread) or an outer lock owned by the caller (e.g.
 * RecBuffer's data mutex serialises every access to its DynamicChunkBuffer).
 *
 * @warning **Not thread-safe.** A previous revision embedded a std::shared_mutex
 * here, but every consumer already serialised access externally, so the internal
 * locking was pure overhead and has been removed. If you ever need a
 * self-synchronising container, wrap this one in a thin lock layer rather than
 * reintroducing locks into the storage core.
 *
 * @tparam T Type of elements stored.
 * @tparam Allocator Allocator to use for memory management.
 */
template <typename T, typename Allocator = std::allocator<T>>
class CircularBuffer {
public:
    using self_type = CircularBuffer<T, Allocator>;
    using allocator_type = Allocator;
    using allocator_traits = std::allocator_traits<allocator_type>;

    using value_type = typename allocator_traits::value_type;
    using pointer = typename allocator_traits::pointer;
    using const_pointer = typename allocator_traits::const_pointer;
    using reference = value_type&;
    using const_reference = const value_type&;

    using size_type = typename allocator_traits::size_type;
    using difference_type = typename allocator_traits::difference_type;

    static_assert(std::is_same_v<T, value_type>,
                  "Allocator::value_type must match CircularBuffer::value_type");

    // --- Constructors & Destructor ---

    /**
     * @brief Constructs a circular buffer.
     * @param capacity Maximum number of elements.
     * @param allocator Allocator instance.
     */
    explicit CircularBuffer(size_type const capacity = 1,
                            allocator_type const& allocator = allocator_type())
        : m_capacity(capacity)
        , m_head(0)
        , m_size(0)
        , m_allocator(allocator)
        , m_array(nullptr)
    {
        assert(capacity > 0 && "Capacity must be greater than 0");
        m_array = allocator_traits::allocate(m_allocator, m_capacity);
    }

    /**
     * @brief Copy constructor.
     * @param other Buffer to copy from.
     */
    CircularBuffer(self_type const& other)
        : m_capacity(other.m_capacity)
        , m_head(0)
        , m_size(0)
        , m_allocator(allocator_traits::select_on_container_copy_construction(other.m_allocator))
        , m_array(nullptr)
    {
        m_array = allocator_traits::allocate(m_allocator, m_capacity);
        try {
            assign_into(other.begin(), other.end());
        } catch (...) {
            clear_impl();
            allocator_traits::deallocate(m_allocator, m_array, m_capacity);
            throw;
        }
    }

    /**
     * @brief Move constructor.
     * @param other Buffer to move from.
     */
    CircularBuffer(self_type&& other) noexcept
        : m_capacity(other.m_capacity)
        , m_head(other.m_head)
        , m_size(other.m_size)
        , m_allocator(std::move(other.m_allocator))
        , m_array(other.m_array)
    {
        other.m_array = nullptr;
        other.m_size = 0;
        other.m_head = 0;
        other.m_capacity = 0;
    }

    /**
     * @brief Destructor. Destroys all elements and deallocates memory.
     */
    ~CircularBuffer() {
        clear_impl();
        if (m_array) {
            allocator_traits::deallocate(m_allocator, m_array, m_capacity);
        }
    }

    // --- Assignment ---

    /// @brief Copy/move assignment via copy-and-swap.
    CircularBuffer& operator=(self_type other) {
        other.swap(*this);
        return *this;
    }

    /// @brief Swaps contents with another buffer.
    void swap(self_type& other) noexcept {
        if (this == &other) return;
        using std::swap;
        if (typename allocator_traits::propagate_on_container_swap()) {
            swap(m_allocator, other.m_allocator);
        }
        swap(m_capacity, other.m_capacity);
        swap(m_head, other.m_head);
        swap(m_size, other.m_size);
        swap(m_array, other.m_array);
    }

    // --- Bulk Operations ---

    /**
     * @brief Writes multiple elements at once.
     * If the input size exceeds capacity, older elements in the buffer are overwritten.
     * @param source Pointer to the source data array.
     * @param count Number of elements to write.
     */
    void push_many(const T* source, size_type count) {
        if (count == 0 || !source) return;

        size_type cap = m_capacity;
        if (count > cap) {
            source += (count - cap);
            count = cap;
        }

        size_type current_tail = (m_head + m_size) % cap;
        size_type chunk1 = std::min(count, cap - current_tail);
        size_type chunk2 = count - chunk1;

        copy_chunk_logic(source, m_array + current_tail, chunk1);
        copy_chunk_logic(source + chunk1, m_array, chunk2);

        size_type new_size = m_size + count;
        if (new_size > cap) {
            m_size = cap;
            m_head = (m_head + new_size - cap) % cap;
        } else {
            m_size = new_size;
        }
    }

    /**
     * @brief Peeks at multiple elements starting from an offset.
     * @param index Relative index (0 is the oldest element).
     * @param dest Pointer to the destination buffer.
     * @param count Number of elements to read.
     */
    void peek_many_at(size_type index, T* dest, size_type count) const {
        if (!dest || count == 0) return;

        size_type cap = m_capacity;
        assert(index + count <= m_size && "Read range exceeds buffer size");

        size_type start_pos = (m_head + index) % cap;
        size_type chunk1 = std::min(count, cap - start_pos);

        std::copy_n(m_array + start_pos, chunk1, dest);
        if (chunk1 < count) {
            std::copy_n(m_array, count - chunk1, dest + chunk1);
        }
    }

    /**
     * @brief Peeks at multiple elements starting from the beginning.
     * @param dest Pointer to the destination buffer.
     * @param count Number of elements to read.
     */
    void peek_many(T* dest, size_type count) const {
        peek_many_at(0, dest, count);
    }

    /**
     * @brief Removes multiple elements from the front of the buffer.
     * @param count Number of elements to pop.
     */
    void pop_many(size_type count) {
        pop_many_impl(count);
    }

    // --- Iterators ---

    template<typename ElemType>
    class iterator_impl {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = std::remove_const_t<ElemType>;
        using difference_type = typename allocator_traits::difference_type;
        using pointer = ElemType*;
        using reference = ElemType&;
        using buffer_type = std::conditional_t<std::is_const_v<ElemType>, const CircularBuffer<T, Allocator>, CircularBuffer<T, Allocator>>;

        iterator_impl() : m_buf(nullptr), m_pos(0) {}
        iterator_impl(buffer_type* buf, size_type pos) : m_buf(buf), m_pos(pos) {}

        reference operator*() const { return (*m_buf)[m_pos]; }
        pointer operator->() const { return &(operator*()); }

        iterator_impl& operator++() { ++m_pos; return *this; }
        iterator_impl operator++(int) { iterator_impl tmp(*this); ++m_pos; return tmp; }
        iterator_impl& operator--() { --m_pos; return *this; }
        iterator_impl operator--(int) { iterator_impl tmp(*this); --m_pos; return tmp; }

        iterator_impl operator+(difference_type n) const { return iterator_impl(m_buf, m_pos + n); }
        iterator_impl& operator+=(difference_type n) { m_pos += n; return *this; }
        iterator_impl operator-(difference_type n) const { return iterator_impl(m_buf, m_pos - n); }
        iterator_impl& operator-=(difference_type n) { m_pos -= n; return *this; }
        difference_type operator-(const iterator_impl& other) const { return static_cast<difference_type>(m_pos) - static_cast<difference_type>(other.m_pos); }

        bool operator==(const iterator_impl& other) const { return m_buf == other.m_buf && m_pos == other.m_pos; }
        bool operator!=(const iterator_impl& other) const { return !(*this == other); }
        bool operator<(const iterator_impl& other) const { return m_pos < other.m_pos; }
        bool operator>(const iterator_impl& other) const { return m_pos > other.m_pos; }
        bool operator<=(const iterator_impl& other) const { return m_pos <= other.m_pos; }
        bool operator>=(const iterator_impl& other) const { return m_pos >= other.m_pos; }

    private:
        friend class CircularBuffer;
        buffer_type* m_buf;
        size_type m_pos;
    };

    using iterator = iterator_impl<value_type>;
    using const_iterator = iterator_impl<const value_type>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, m_size); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, m_size); }
    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend() const { return const_iterator(this, m_size); }

    // --- Accessors ---

    [[nodiscard]] bool empty() const { return m_size == 0; }
    [[nodiscard]] bool full() const { return m_size == m_capacity; }
    [[nodiscard]] size_type capacity() const { return m_capacity; }
    [[nodiscard]] size_type max_size() const { return allocator_traits::max_size(m_allocator); }
    [[nodiscard]] size_type size() const { return m_size; }
    [[nodiscard]] allocator_type get_allocator() const { return m_allocator; }

    /// @brief Converts buffer to std::vector (snapshot copy).
    [[nodiscard]] std::vector<T> toStdVector() const {
        std::vector<T> v;
        v.reserve(m_size);
        for (size_type i = 0; i < m_size; ++i) v.push_back(at_unchecked(i));
        return v;
    }

    reference front() { assert(m_size > 0); return m_array[m_head]; }
    const_reference front() const { assert(m_size > 0); return m_array[m_head]; }
    reference back() { assert(m_size > 0); return m_array[(m_head + m_size - 1) % m_capacity]; }
    const_reference back() const { assert(m_size > 0); return m_array[(m_head + m_size - 1) % m_capacity]; }

    reference operator[](size_type const n) { assert(n < m_size); return at_unchecked(n); }
    const_reference operator[](size_type const n) const { assert(n < m_size); return at_unchecked(n); }
    reference at(size_type const n) { assert(n < m_size); return at_unchecked(n); }
    const_reference at(size_type const n) const { assert(n < m_size); return at_unchecked(n); }

    // --- Modifiers ---

    void clear() { clear_impl(); }

    void reserve(size_type const new_capacity) {
        if (new_capacity <= m_capacity) return;

        CircularBuffer tmp(new_capacity, m_allocator);

        size_type chunk1 = std::min(m_size, m_capacity - m_head);
        size_type chunk2 = m_size - chunk1;

        tmp.push_many(m_array + m_head, chunk1);
        if (chunk2 > 0) {
            tmp.push_many(m_array, chunk2);
        }

        using std::swap;
        swap(m_capacity, tmp.m_capacity);
        swap(m_array, tmp.m_array);
        swap(m_head, tmp.m_head);
        swap(m_size, tmp.m_size);
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        size_type write_idx = (m_head + m_size) % m_capacity;

        if (m_size == m_capacity) {
            allocator_traits::destroy(m_allocator, m_array + write_idx);
            allocator_traits::construct(m_allocator, m_array + write_idx, std::forward<Args>(args)...);
            m_head = (m_head + 1) % m_capacity;
        } else {
            allocator_traits::construct(m_allocator, m_array + write_idx, std::forward<Args>(args)...);
            ++m_size;
        }
    }

    void push_back(value_type const& item) { emplace_back(item); }
    void push_back(value_type&& item) { emplace_back(std::move(item)); }

    void pop_front() {
        assert(m_size > 0);
        allocator_traits::destroy(m_allocator, m_array + m_head);
        m_head = (m_head + 1) % m_capacity;
        --m_size;
    }

    void pop_back() {
        assert(m_size > 0);
        size_type tail = (m_head + m_size - 1) % m_capacity;
        allocator_traits::destroy(m_allocator, m_array + tail);
        --m_size;
    }

    const_pointer get_ptr_unsafe(size_type index) const {
        return m_array + ((m_head + index) % m_capacity);
    }

    size_type get_head_index_unsafe() const {
        return m_head;
    }

private:
    reference at_unchecked(size_type const index) const {
        return m_array[(m_head + index) % m_capacity];
    }

    void clear_impl() {
        if (m_size == 0) return;
        for (size_type n = 0; n < m_size; ++n) {
            allocator_traits::destroy(m_allocator, m_array + ((m_head + n) % m_capacity));
        }
        m_head = 0;
        m_size = 0;
    }

    void pop_many_impl(size_type count) {
        assert(count <= m_size);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            size_type current = m_head;
            for (size_type i = 0; i < count; ++i) {
                allocator_traits::destroy(m_allocator, m_array + current);
                current = (current + 1) % m_capacity;
            }
        }
        m_head = (m_head + count) % m_capacity;
        m_size -= count;
    }

    template <typename InputIterator>
    void assign_into(InputIterator from, InputIterator const to) {
        while (from != to) {
            size_type write_idx = (m_head + m_size) % m_capacity;
            if (m_size == m_capacity) {
                allocator_traits::destroy(m_allocator, m_array + write_idx);
                allocator_traits::construct(m_allocator, m_array + write_idx, *from);
                m_head = (m_head + 1) % m_capacity;
            } else {
                allocator_traits::construct(m_allocator, m_array + write_idx, *from);
                ++m_size;
            }
            ++from;
        }
    }

    void copy_chunk_logic(const T* src, T* dst, size_type n) {
        if (n == 0) return;
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::copy_n(src, n, dst);
        } else {
            for (size_type i = 0; i < n; ++i) {
                allocator_traits::destroy(m_allocator, dst + i);
                allocator_traits::construct(m_allocator, dst + i, src[i]);
            }
        }
    }

    size_type      m_capacity;
    size_type      m_head;
    size_type      m_size;
    allocator_type m_allocator;
    pointer        m_array;
};

template <typename T, typename A>
void swap(CircularBuffer<T, A>& lhs, CircularBuffer<T, A>& rhs) noexcept {
    lhs.swap(rhs);
}

} // namespace cyc

#endif // CYC_CIRCULARBUFFER_H
