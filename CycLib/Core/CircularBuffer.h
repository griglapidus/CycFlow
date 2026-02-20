// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CIRCULARBUFFER_H
#define CYC_CIRCULARBUFFER_H

#include <vector>
#include <iterator>
#include <algorithm>
#include <cassert>
#include <type_traits>
#include <shared_mutex>
#include <mutex>
#include <atomic>

namespace cyc {

/**
 * @class CircularBuffer
 * @brief Thread-safe circular buffer container with STL-like API.
 *
 * Implements a ring buffer using a dynamic array. It uses `std::shared_mutex`
 * for safe read/write operations and `std::atomic` for lock-free state querying.
 *
 * @warning **Thread-Safety Notice:** Iterators and references returned by accessors
 * (e.g., `operator[]`, `front()`) are NOT thread-safe if the buffer is being modified
 * concurrently by another thread. Use bulk operations (`push_many`, `peek_many`)
 * for strictly safe concurrent access.
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
        m_array = allocator_traits::allocate(m_allocator, m_capacity.load(std::memory_order_relaxed));
    }

    /**
     * @brief Copy constructor (Thread-Safe).
     * Locks the source buffer for reading (shared lock) during copy.
     * @param other Buffer to copy from.
     */
    CircularBuffer(self_type const& other)
        : m_capacity(other.capacity())
        , m_head(0)
        , m_size(0)
        , m_allocator(allocator_traits::select_on_container_copy_construction(other.m_allocator))
        , m_array(nullptr)
    {
        std::shared_lock<std::shared_mutex> lock(other.m_mtx);
        m_array = allocator_traits::allocate(m_allocator, m_capacity.load(std::memory_order_relaxed));
        try {
            assign_into_unsafe(other.begin_unsafe(), other.end_unsafe());
        } catch (...) {
            clear_unsafe();
            allocator_traits::deallocate(m_allocator, m_array, m_capacity.load(std::memory_order_relaxed));
            throw;
        }
    }

    /**
     * @brief Move constructor (Thread-Safe).
     * Locks the source buffer for writing (exclusive lock) during move.
     * @param other Buffer to move from.
     */
    CircularBuffer(self_type&& other) noexcept
        : m_capacity(other.capacity())
        , m_head(other.m_head.load(std::memory_order_relaxed))
        , m_size(other.m_size.load(std::memory_order_relaxed))
        , m_allocator(std::move(other.m_allocator))
        , m_array(other.m_array)
    {
        std::unique_lock<std::shared_mutex> lock(other.m_mtx);
        other.m_array = nullptr;
        other.m_size.store(0, std::memory_order_relaxed);
        other.m_head.store(0, std::memory_order_relaxed);
        other.m_capacity.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Destructor. Destroys all elements and deallocates memory.
     */
    ~CircularBuffer() {
        clear_unsafe();
        if (m_array) {
            allocator_traits::deallocate(m_allocator, m_array, m_capacity.load(std::memory_order_relaxed));
        }
    }

    // --- Assignment ---

    /**
     * @brief Copy assignment operator (Thread-Safe).
     */
    CircularBuffer& operator=(self_type other) {
        other.swap(*this);
        return *this;
    }

    /**
     * @brief Swaps contents with another buffer (Thread-Safe).
     * Locks both buffers exclusively to prevent deadlocks and races.
     * @param other Buffer to swap with.
     */
    void swap(self_type& other) noexcept {
        if (this == &other) return;
        std::scoped_lock<std::shared_mutex, std::shared_mutex> lock(m_mtx, other.m_mtx);

        using std::swap;
        if (typename allocator_traits::propagate_on_container_swap()) {
            swap(m_allocator, other.m_allocator);
        }

        size_type tmp_cap = m_capacity.load(std::memory_order_relaxed);
        m_capacity.store(other.m_capacity.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.m_capacity.store(tmp_cap, std::memory_order_relaxed);

        size_type tmp_head = m_head.load(std::memory_order_relaxed);
        m_head.store(other.m_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.m_head.store(tmp_head, std::memory_order_relaxed);

        size_type tmp_size = m_size.load(std::memory_order_relaxed);
        m_size.store(other.m_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.m_size.store(tmp_size, std::memory_order_relaxed);

        swap(m_array, other.m_array);
    }

    // --- Bulk Operations ---

    /**
     * @brief Writes multiple elements at once (Thread-Safe).
     * If the input size exceeds capacity, older elements in the buffer are overwritten.
     * @param source Pointer to the source data array.
     * @param count Number of elements to write.
     */
    void push_many(const T* source, size_type count) {
        if (count == 0 || !source) return;
        std::unique_lock<std::shared_mutex> lock(m_mtx);

        size_type cap = m_capacity.load(std::memory_order_relaxed);
        if (count > cap) {
            source += (count - cap);
            count = cap;
        }

        size_type head = m_head.load(std::memory_order_relaxed);
        size_type sz = m_size.load(std::memory_order_relaxed);
        size_type current_tail = (head + sz) % cap;

        size_type chunk1 = std::min(count, cap - current_tail);
        size_type chunk2 = count - chunk1;

        copy_chunk_logic(source, m_array + current_tail, chunk1);
        copy_chunk_logic(source + chunk1, m_array, chunk2);

        size_type new_size = sz + count;
        if (new_size > cap) {
            m_size.store(cap, std::memory_order_release);
            m_head.store((head + new_size - cap) % cap, std::memory_order_release);
        } else {
            m_size.store(new_size, std::memory_order_release);
        }
    }

    /**
     * @brief Peeks at multiple elements starting from an offset (Thread-Safe).
     * @param index Relative index (0 is the oldest element).
     * @param dest Pointer to the destination buffer.
     * @param count Number of elements to read.
     */
    void peek_many_at(size_type index, T* dest, size_type count) const {
        if (!dest || count == 0) return;
        std::shared_lock<std::shared_mutex> lock(m_mtx);

        size_type cap = m_capacity.load(std::memory_order_relaxed);
        size_type head = m_head.load(std::memory_order_relaxed);
        size_type sz = m_size.load(std::memory_order_acquire);

        assert(index + count <= sz && "Read range exceeds buffer size");

        size_type start_pos = (head + index) % cap;
        size_type chunk1 = std::min(count, cap - start_pos);

        std::copy_n(m_array + start_pos, chunk1, dest);
        if (chunk1 < count) {
            std::copy_n(m_array, count - chunk1, dest + chunk1);
        }
    }

    /**
     * @brief Peeks at multiple elements starting from the beginning (Thread-Safe).
     * @param dest Pointer to the destination buffer.
     * @param count Number of elements to read.
     */
    void peek_many(T* dest, size_type count) const {
        peek_many_at(0, dest, count);
    }

    /**
     * @brief Removes multiple elements from the front of the buffer (Thread-Safe).
     * @param count Number of elements to pop.
     */
    void pop_many(size_type count) {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        pop_many_unsafe(count);
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
    iterator end() { return iterator(this, size()); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size()); }
    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend() const { return const_iterator(this, size()); }

    // --- Accessors (Lock-Free where applicable) ---

    [[nodiscard]] bool empty() const { return m_size.load(std::memory_order_acquire) == 0; }
    [[nodiscard]] bool full() const { return m_size.load(std::memory_order_acquire) == m_capacity.load(std::memory_order_relaxed); }
    [[nodiscard]] size_type capacity() const { return m_capacity.load(std::memory_order_relaxed); }
    [[nodiscard]] size_type max_size() const { return allocator_traits::max_size(m_allocator); }
    [[nodiscard]] size_type size() const { return m_size.load(std::memory_order_acquire); }
    [[nodiscard]] allocator_type get_allocator() const { return m_allocator; }

    /**
     * @brief Converts buffer to std::vector. Snapshot operation (thread-safe).
     */
    [[nodiscard]] std::vector<T> toStdVector() const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        std::vector<T> v;
        size_type sz = m_size.load(std::memory_order_acquire);
        v.reserve(sz);
        for (size_type i = 0; i < sz; ++i) v.push_back(at_unchecked(i));
        return v;
    }

    reference front() { std::shared_lock<std::shared_mutex> lock(m_mtx); assert(size() > 0); return m_array[m_head.load(std::memory_order_relaxed)]; }
    const_reference front() const { std::shared_lock<std::shared_mutex> lock(m_mtx); assert(size() > 0); return m_array[m_head.load(std::memory_order_relaxed)]; }
    reference back() {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(size() > 0);
        return m_array[(m_head.load(std::memory_order_relaxed) + size() - 1) % capacity()];
    }
    const_reference back() const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(size() > 0);
        return m_array[(m_head.load(std::memory_order_relaxed) + size() - 1) % capacity()];
    }

    reference operator[](size_type const n) {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(n < size());
        return at_unchecked(n);
    }
    const_reference operator[](size_type const n) const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(n < size());
        return at_unchecked(n);
    }
    reference at(size_type const n) {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(n < size());
        return at_unchecked(n);
    }
    const_reference at(size_type const n) const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(n < size());
        return at_unchecked(n);
    }

    // --- Modifiers ---

    void clear() {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        clear_unsafe();
    }

    void reserve(size_type const new_capacity) {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        size_type cap = m_capacity.load(std::memory_order_relaxed);
        if (new_capacity <= cap) return;

        CircularBuffer tmp(new_capacity, m_allocator);
        size_type sz = m_size.load(std::memory_order_relaxed);
        size_type head = m_head.load(std::memory_order_relaxed);

        size_type chunk1 = std::min(sz, cap - head);
        size_type chunk2 = sz - chunk1;

        tmp.push_many(m_array + head, chunk1);
        if (chunk2 > 0) {
            tmp.push_many(m_array, chunk2);
        }

        using std::swap;
        m_capacity.store(tmp.m_capacity.load(std::memory_order_relaxed), std::memory_order_relaxed);
        swap(m_array, tmp.m_array);
        m_head.store(tmp.m_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_size.store(tmp.m_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        size_type cap = m_capacity.load(std::memory_order_relaxed);
        size_type head = m_head.load(std::memory_order_relaxed);
        size_type sz = m_size.load(std::memory_order_relaxed);

        size_type write_idx = (head + sz) % cap;

        if (sz == cap) {
            allocator_traits::destroy(m_allocator, m_array + write_idx);
            allocator_traits::construct(m_allocator, m_array + write_idx, std::forward<Args>(args)...);
            m_head.store((head + 1) % cap, std::memory_order_release);
        } else {
            allocator_traits::construct(m_allocator, m_array + write_idx, std::forward<Args>(args)...);
            m_size.store(sz + 1, std::memory_order_release);
        }
    }

    void push_back(value_type const& item) { emplace_back(item); }
    void push_back(value_type&& item) { emplace_back(std::move(item)); }

    void pop_front() {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        size_type sz = m_size.load(std::memory_order_relaxed);
        assert(sz > 0);
        size_type head = m_head.load(std::memory_order_relaxed);

        allocator_traits::destroy(m_allocator, m_array + head);
        m_head.store((head + 1) % m_capacity.load(std::memory_order_relaxed), std::memory_order_release);
        m_size.store(sz - 1, std::memory_order_release);
    }

    void pop_back() {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        size_type sz = m_size.load(std::memory_order_relaxed);
        assert(sz > 0);
        size_type tail = (m_head.load(std::memory_order_relaxed) + sz - 1) % m_capacity.load(std::memory_order_relaxed);

        allocator_traits::destroy(m_allocator, m_array + tail);
        m_size.store(sz - 1, std::memory_order_release);
    }

    const_pointer get_ptr_unsafe(size_type index) const {
        return m_array + ((m_head.load(std::memory_order_relaxed) + index) % m_capacity.load(std::memory_order_relaxed));
    }

    size_type get_head_index_unsafe() const {
        return m_head.load(std::memory_order_relaxed);
    }

private:
    iterator begin_unsafe() { return iterator(this, 0); }
    iterator end_unsafe() { return iterator(this, m_size.load(std::memory_order_relaxed)); }

    reference at_unchecked(size_type const index) const {
        return m_array[(m_head.load(std::memory_order_relaxed) + index) % m_capacity.load(std::memory_order_relaxed)];
    }

    void clear_unsafe() {
        size_type sz = m_size.load(std::memory_order_relaxed);
        if (sz == 0) return;
        size_type head = m_head.load(std::memory_order_relaxed);
        size_type cap = m_capacity.load(std::memory_order_relaxed);

        for (size_type n = 0; n < sz; ++n) {
            allocator_traits::destroy(m_allocator, m_array + ((head + n) % cap));
        }
        m_head.store(0, std::memory_order_relaxed);
        m_size.store(0, std::memory_order_release);
    }

    void pop_many_unsafe(size_type count) {
        size_type sz = m_size.load(std::memory_order_relaxed);
        assert(count <= sz);
        size_type head = m_head.load(std::memory_order_relaxed);
        size_type cap = m_capacity.load(std::memory_order_relaxed);

        if constexpr (!std::is_trivially_destructible_v<T>) {
            size_type current = head;
            for(size_type i = 0; i < count; ++i) {
                allocator_traits::destroy(m_allocator, m_array + current);
                current = (current + 1) % cap;
            }
        }
        m_head.store((head + count) % cap, std::memory_order_release);
        m_size.store(sz - count, std::memory_order_release);
    }

    template <typename InputIterator>
    void assign_into_unsafe(InputIterator from, InputIterator const to) {
        while (from != to) {
            size_type sz = m_size.load(std::memory_order_relaxed);
            size_type head = m_head.load(std::memory_order_relaxed);
            size_type cap = m_capacity.load(std::memory_order_relaxed);
            size_type write_idx = (head + sz) % cap;

            if (sz == cap) {
                allocator_traits::destroy(m_allocator, m_array + write_idx);
                allocator_traits::construct(m_allocator, m_array + write_idx, *from);
                m_head.store((head + 1) % cap, std::memory_order_relaxed);
            } else {
                allocator_traits::construct(m_allocator, m_array + write_idx, *from);
                m_size.store(sz + 1, std::memory_order_relaxed);
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

    // Alignment to prevent false sharing between reader and writer threads
    alignas(64) std::atomic<size_type> m_capacity;
    alignas(64) std::atomic<size_type> m_head;
    alignas(64) std::atomic<size_type> m_size;

    allocator_type m_allocator;
    pointer        m_array;

    alignas(64) mutable std::shared_mutex m_mtx;
};

template <typename T, typename A>
void swap(CircularBuffer<T, A>& lhs, CircularBuffer<T, A>& rhs) noexcept {
    lhs.swap(rhs);
}

} // namespace cyc

#endif // CYC_CIRCULARBUFFER_H
