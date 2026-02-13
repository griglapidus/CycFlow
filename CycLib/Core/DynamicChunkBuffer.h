// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef DYNAMICCHUNKBUFFER_H
#define DYNAMICCHUNKBUFFER_H

#include "CircularBuffer.h"
#include <cstdint>
#include <cassert>
#include <atomic>

namespace cyc {

/**
 * @brief Low-level wrapper around CircularBuffer to handle fixed-size chunks.
 *
 * This class adapts the generic CircularBuffer to store raw byte data
 * organized in fixed-size chunks. It is agnostic to RecRule schemas.
 *
 * Features:
 * - Thread-safe tracking of total items written via std::atomic.
 * - Optimized for block writes/reads.
 */
class DynamicChunkBuffer {
public:
    /**
     * @brief Constructs the buffer.
     * @param itemCapacity Maximum number of chunks the buffer can hold.
     * @param chunkSize Size of a single chunk in bytes.
     */
    DynamicChunkBuffer(size_t itemCapacity, size_t chunkSize)
        : m_chunkSize(chunkSize)
        , m_buffer(itemCapacity * chunkSize)
        , m_totalWritten(0)
    {
        assert(itemCapacity > 0);
        assert(chunkSize > 0);
    }

    /**
     * @brief Writes raw bytes (multiple chunks) into the buffer.
     *
     * @param data Pointer to the source data.
     * @param count Number of chunks (not bytes!) to write.
     */

    void push(const void* data, size_t count) {
        if (data == nullptr || count == 0) return;
        m_buffer.push_many(static_cast<const uint8_t*>(data), count * m_chunkSize);
        m_totalWritten.fetch_add(count, std::memory_order_release);
    }

    /**
     * @brief Reads chunks from a relative index within the current window.
     *
     * @param index Offset in chunks from the oldest available data.
     * @param destination Pointer to the target memory.
     * @param count Number of chunks to read.
     */
    void readAt(size_t index, void* destination, size_t count) const {
        if (destination == nullptr || count == 0) return;
        size_t const byteOffset = index * m_chunkSize;
        size_t const byteCount = count * m_chunkSize;
        m_buffer.peek_many_at(byteOffset, static_cast<uint8_t*>(destination), byteCount);
    }

    /**
     * @brief Gets the total number of chunks written since creation.
     * Thread-safe (atomic load with acquire semantics).
     * @return Total count.
     */
    uint64_t getTotalWritten() const {
        return m_totalWritten.load(std::memory_order_acquire);
    }

    size_t size() const { return m_buffer.size() / m_chunkSize; }
    size_t capacity() const { return m_buffer.capacity() / m_chunkSize; }
    size_t getChunkSize() const { return m_chunkSize; }
    void clear() { m_buffer.clear(); }

private:
    size_t m_chunkSize;
    cyc::CircularBuffer<uint8_t> m_buffer;
    std::atomic<uint64_t> m_totalWritten;
};

} // namespace cyc

#endif // DYNAMICCHUNKBUFFER_H
