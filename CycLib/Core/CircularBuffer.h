// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CIRCULARBUFFER_H
#define CIRCULARBUFFER_H

#include <vector>
#include <memory>
#include <iterator>
#include <algorithm>
#include <cassert>
#include <type_traits>
#include <shared_mutex>
#include <mutex>

namespace cyc {

/**
 * @brief Thread-safe circular buffer container.
 *
 * This class implements a ring buffer using a dynamic array. It uses `std::shared_mutex`
 * to implement a "Multiple Readers, Single Writer" thread-safety model.
 *
 * @tparam T Type of elements stored.
 * @tparam Allocator Allocator to use for memory management.
 *
 * @warning **Thread-Safety Notice:**
 * While bulk operations (`push_many`, `peek_many`) and size checks are thread-safe,
 * the **iterators and reference accessors** (e.g., `begin()`, `end()`, `operator[]`, `front()`)
 * are **NOT thread-safe** if the buffer is being modified concurrently.
 *
 * If one thread resizes or overwrites the buffer, existing iterators or references
 * held by other threads may become invalid or point to inconsistent data.
 *
 * For safe concurrent access, prefer using:
 * - `peek_many()` to read a snapshot of data.
 * - `push_many()` to write data atomically.
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

    static_assert(std::is_same<T, value_type>::value,
                  "Allocator::value_type must match CircularBuffer::value_type");

    // --- Constructors ---

    /**
     * @brief Constructs a circular buffer with specific capacity.
     * @param capacity Maximum number of elements.
     * @param allocator Allocator instance.
     */
    explicit CircularBuffer(size_type const capacity = 1,
                            allocator_type const& allocator = allocator_type())
        : m_capacity(capacity)
        , m_allocator(allocator)
        , m_array(nullptr)
        , m_head(0)
        , m_size(0)
    {
        assert(capacity > 0 && "Capacity must be greater than 0");
        m_array = allocator_traits::allocate(m_allocator, m_capacity);
    }

    /**
     * @brief Copy constructor (Thread-Safe).
     * Locks the source buffer for reading (shared lock) during copy.
     * @param other Buffer to copy from.
     */
    CircularBuffer(self_type const& other)
        : m_capacity(other.m_capacity)
        , m_allocator(allocator_traits::select_on_container_copy_construction(other.m_allocator))
        , m_array(nullptr)
        , m_head(0)
        , m_size(0)
    {
        // Lock the other buffer for reading
        std::shared_lock<std::shared_mutex> lock(other.m_mtx);

        m_capacity = other.m_capacity;
        m_array = allocator_traits::allocate(m_allocator, m_capacity);
        try {
            assign_into_unsafe(other.begin_unsafe(), other.end_unsafe());
        } catch (...) {
            clear_unsafe();
            allocator_traits::deallocate(m_allocator, m_array, m_capacity);
            throw;
        }
    }

    /**
     * @brief Move constructor (Thread-Safe).
     * Locks the source buffer for writing (exclusive lock) during move.
     * @param other Buffer to move from.
     */
    CircularBuffer(self_type&& other) noexcept
        : m_capacity(0)
        , m_allocator(std::move(other.m_allocator))
        , m_array(nullptr)
        , m_head(0)
        , m_size(0)
    {
        std::unique_lock<std::shared_mutex> lock(other.m_mtx);

        m_capacity = other.m_capacity;
        m_array = other.m_array;
        m_head = other.m_head;
        m_size = other.m_size;

        // Leave other in valid empty state
        other.m_array = nullptr;
        other.m_size = 0;
        other.m_head = 0;
        other.m_capacity = 0;
    }

    /**
     * @brief Constructs a buffer from an iterator range.
     * @param from Start iterator.
     * @param to End iterator.
     * @param allocator Allocator instance.
     */
    template <typename InputIterator,
             typename = std::enable_if_t<!std::is_integral<InputIterator>::value>>
    CircularBuffer(InputIterator from, InputIterator to,
                   allocator_type const& allocator = allocator_type())
        : m_capacity(0)
        , m_allocator(allocator)
        , m_array(nullptr)
        , m_head(0)
        , m_size(0)
    {
        size_type const dist = static_cast<size_type>(std::distance(from, to));
        assert(dist > 0 && "Iterator range cannot be empty for initial construction");

        m_capacity = dist;
        m_array = allocator_traits::allocate(m_allocator, m_capacity);

        try {
            assign_into_unsafe(from, to);
        } catch (...) {
            clear_unsafe();
            allocator_traits::deallocate(m_allocator, m_array, m_capacity);
            throw;
        }
    }

    /**
     * @brief Destructor.
     * Destroys all elements and deallocates memory.
     */
    ~CircularBuffer() {
        clear_unsafe();
        if (m_array) {
            allocator_traits::deallocate(m_allocator, m_array, m_capacity);
        }
    }

    // --- Assignment ---

    /**
     * @brief Copy assignment operator (Thread-Safe).
     * Uses copy-and-swap idiom.
     */
    CircularBuffer& operator=(self_type other) {
        other.swap(*this);
        return *this;
    }

    /**
     * @brief Move assignment operator (Thread-Safe).
     * Locks both buffers exclusively.
     */
    CircularBuffer& operator=(self_type&& other) noexcept {
        if (this != &other) {
            std::scoped_lock<std::shared_mutex, std::shared_mutex> lock(m_mtx, other.m_mtx);

            clear_unsafe();
            allocator_traits::deallocate(m_allocator, m_array, m_capacity);

            m_capacity = other.m_capacity;
            m_allocator = std::move(other.m_allocator);
            m_array = other.m_array;
            m_head = other.m_head;
            m_size = other.m_size;

            other.m_array = nullptr;
            other.m_size = 0;
            other.m_head = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    /**
     * @brief Swaps contents with another buffer (Thread-Safe).
     * Locks both buffers exclusively to prevent deadlocks and races.
     * @param other Buffer to swap with.
     */
    void swap(self_type& other) noexcept {
        if (this == &other) return;

        // Lock both mutexes to avoid deadlock
        std::scoped_lock<std::shared_mutex, std::shared_mutex> lock(m_mtx, other.m_mtx);

        using std::swap;
        if (typename allocator_traits::propagate_on_container_swap()) {
            swap(m_allocator, other.m_allocator);
        }
        swap(m_capacity, other.m_capacity);
        swap(m_array, other.m_array);
        swap(m_head, other.m_head);
        swap(m_size, other.m_size);
    }

    // --- Bulk Operations (High Performance & Thread-Safe) ---

    /**
     * @brief Writes multiple elements at once (Thread-Safe).
     *
     * Acquires an exclusive lock. If the input size exceeds capacity,
     * older elements in the buffer are overwritten.
     *
     * @param source Pointer to the source data array.
     * @param count Number of elements to write.
     */
    void push_many(const T* source, size_type count) {
        if (count == 0) return;
        assert(source != nullptr);

        std::unique_lock<std::shared_mutex> lock(m_mtx);

        if (count > m_capacity) {
            source += (count - m_capacity);
            count = m_capacity;
        }

        auto copy_chunk_logic = [&](const T* src, T* dst, size_type n) {
            if (n == 0) return;
            if constexpr (std::is_trivially_copyable_v<T>) {
                std::copy_n(src, n, dst);
            } else {
                for (size_type i = 0; i < n; ++i) {
                    allocator_traits::destroy(m_allocator, dst + i);
                    allocator_traits::construct(m_allocator, dst + i, src[i]);
                }
            }
        };

        size_type const current_tail = (m_head + m_size) % m_capacity;
        size_type const chunk1 = std::min(count, m_capacity - current_tail);
        size_type const chunk2 = count - chunk1;

        copy_chunk_logic(source, m_array + current_tail, chunk1);
        copy_chunk_logic(source + chunk1, m_array, chunk2);

        size_type const new_size = m_size + count;
        if (new_size > m_capacity) {
            m_size = m_capacity;
            size_type const overflow = new_size - m_capacity;
            m_head = (m_head + overflow) % m_capacity;
        } else {
            m_size = new_size;
        }
    }

    /**
     * @brief Peeks at multiple elements starting from an offset (Thread-Safe).
     *
     * Acquires a shared lock (allows concurrent readers).
     *
     * @param index Relative index (0 is the oldest element).
     * @param dest Pointer to the destination buffer.
     * @param count Number of elements to read.
     */
    void peek_many_at(size_type index, T* dest, size_type count) const {
        assert(dest != nullptr);

        std::shared_lock<std::shared_mutex> lock(m_mtx);

        assert(index + count <= m_size && "Read range exceeds buffer size");

        size_type const start_pos = (m_head + index) % m_capacity;
        size_type const chunk1 = std::min(count, m_capacity - start_pos);

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
     * Acquires an exclusive lock.
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
        using value_type = typename std::remove_const<ElemType>::type;
        using difference_type = typename allocator_traits::difference_type;
        using pointer = ElemType*;
        using reference = ElemType&;
        using buffer_type = typename std::conditional<
            std::is_const<ElemType>::value,
            const CircularBuffer<T, Allocator>,
            CircularBuffer<T, Allocator>
            >::type;

        iterator_impl() : m_buf(nullptr), m_pos(0) {}
        iterator_impl(buffer_type* buf, size_type pos) : m_buf(buf), m_pos(pos) {}

        // Note: Dereferencing iterators is NOT thread-safe if another thread resizes/writes.
        reference operator*() const { return (*m_buf)[m_pos]; } // Uses operator[] which locks internally
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

    // Warning: Obtaining iterators is safe (locked), but using them concurrently is not.
    iterator begin() { return iterator(this, 0); }
    iterator end() { std::shared_lock<std::shared_mutex> lock(m_mtx); return iterator(this, m_size); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { std::shared_lock<std::shared_mutex> lock(m_mtx); return const_iterator(this, m_size); }
    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend() const { std::shared_lock<std::shared_mutex> lock(m_mtx); return const_iterator(this, m_size); }

    // --- Accessors (Thread-Safe) ---

    [[nodiscard]] bool empty() const { std::shared_lock<std::shared_mutex> lock(m_mtx); return m_size == 0; }
    [[nodiscard]] bool full() const { std::shared_lock<std::shared_mutex> lock(m_mtx); return m_size == m_capacity; }
    [[nodiscard]] size_type capacity() const { std::shared_lock<std::shared_mutex> lock(m_mtx); return m_capacity; }
    [[nodiscard]] size_type max_size() const { return allocator_traits::max_size(m_allocator); }
    [[nodiscard]] size_type size() const { std::shared_lock<std::shared_mutex> lock(m_mtx); return m_size; }
    [[nodiscard]] allocator_type get_allocator() const { return m_allocator; }

    /**
     * @brief Converts buffer to std::vector.
     * Snapshot operation (thread-safe).
     */
    [[nodiscard]] std::vector<T> toStdVector() const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        std::vector<T> v;
        v.reserve(m_size);
        for (size_type i = 0; i < m_size; ++i) v.push_back(at_unchecked(i));
        return v;
    }

    // WARNING: returning references is risky in concurrent code.
    reference front() { std::shared_lock<std::shared_mutex> lock(m_mtx); assert(m_size > 0); return m_array[m_head]; }
    const_reference front() const { std::shared_lock<std::shared_mutex> lock(m_mtx); assert(m_size > 0); return m_array[m_head]; }
    reference back() {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(m_size > 0);
        return m_array[(m_head + m_size - 1) % m_capacity];
    }
    const_reference back() const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(m_size > 0);
        return m_array[(m_head + m_size - 1) % m_capacity];
    }

    reference operator[](size_type const n) {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(n < m_size);
        return at_unchecked(n);
    }
    const_reference operator[](size_type const n) const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(n < m_size);
        return at_unchecked(n);
    }
    reference at(size_type const n) {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(n < m_size);
        return at_unchecked(n);
    }
    const_reference at(size_type const n) const {
        std::shared_lock<std::shared_mutex> lock(m_mtx);
        assert(n < m_size);
        return at_unchecked(n);
    }

    // --- Modifiers (Thread-Safe) ---

    /**
     * @brief Clears the buffer.
     * Acquires exclusive lock.
     */
    void clear() {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        clear_unsafe();
    }

    /**
     * @brief Increases the capacity of the buffer.
     *
     * Creates a new buffer, copies existing data (using block copy logic),
     * and swaps. This is an expensive operation but thread-safe.
     * @param new_capacity New maximum number of elements.
     */
    void reserve(size_type const new_capacity) {
        std::unique_lock<std::shared_mutex> lock(m_mtx);

        if (new_capacity <= m_capacity) return;

        CircularBuffer tmp(new_capacity, m_allocator);

        size_type const chunk1 = std::min(m_size, m_capacity - m_head);
        size_type const chunk2 = m_size - chunk1;

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

    /**
     * @brief Constructs an element in-place at the end.
     * If full, overwrites the oldest element.
     */
    template <typename... Args>
    void emplace_back(Args&&... args) {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        size_type const write_idx = (m_head + m_size) % m_capacity;

        if (m_size == m_capacity) {
            // Full: overwrite
            allocator_traits::destroy(m_allocator, m_array + write_idx);
            allocator_traits::construct(m_allocator, m_array + write_idx, std::forward<Args>(args)...);
            m_head = (m_head + 1) % m_capacity;
        } else {
            // Not full
            allocator_traits::construct(m_allocator, m_array + write_idx, std::forward<Args>(args)...);
            ++m_size;
        }
    }

    void push_back(value_type const& item) { emplace_back(item); }
    void push_back(value_type&& item) { emplace_back(std::move(item)); }

    /**
     * @brief Removes the first element.
     */
    void pop_front() {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        assert(m_size > 0);
        allocator_traits::destroy(m_allocator, m_array + m_head);
        m_head = (m_head + 1) % m_capacity;
        --m_size;
    }

    /**
     * @brief Removes the last element.
     */
    void pop_back() {
        std::unique_lock<std::shared_mutex> lock(m_mtx);
        assert(m_size > 0);
        size_type const tail = (m_head + m_size - 1) % m_capacity;
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
    // Helper to get iterators without locking (for internal use in constructors)
    iterator begin_unsafe() { return iterator(this, 0); }
    iterator end_unsafe() { return iterator(this, m_size); }

    reference at_unchecked(size_type const index) const {
        return m_array[(m_head + index) % m_capacity];
    }

    void clear_unsafe() {
        if (m_size == 0) return;
        for (size_type n = 0; n < m_size; ++n) {
            allocator_traits::destroy(m_allocator, m_array + ((m_head + n) % m_capacity));
        }
        m_head = 0;
        m_size = 0;
    }

    void pop_many_unsafe(size_type count) {
        assert(count <= m_size);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            size_type current = m_head;
            for(size_type i = 0; i < count; ++i) {
                allocator_traits::destroy(m_allocator, m_array + current);
                current = (current + 1) % m_capacity;
            }
        }
        m_head = (m_head + count) % m_capacity;
        m_size -= count;
    }

    template <typename InputIterator>
    void assign_into_unsafe(InputIterator from, InputIterator const to) {
        while (from != to) {
            size_type const write_idx = (m_head + m_size) % m_capacity;
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

    size_type      m_capacity;
    allocator_type m_allocator;
    pointer        m_array;
    size_type      m_head;
    size_type      m_size;

    mutable std::shared_mutex m_mtx;
};

/**
 * @brief Global swap specialization for CircularBuffer.
 */
template <typename T, typename A>
void swap(CircularBuffer<T, A>& lhs, CircularBuffer<T, A>& rhs) noexcept {
    lhs.swap(rhs);
}

} // namespace cyc

#endif // CIRCULARBUFFER_H
