// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_DYNAMICCHUNKBUFFER_H
#define CYC_DYNAMICCHUNKBUFFER_H

#include "CircularBuffer.h"

namespace cyc {

/**
 * @class DynamicChunkBuffer
 * @brief Low-level wrapper around CircularBuffer to handle fixed-size chunks.
 *
 * This class adapts the generic CircularBuffer to store raw byte data
 * organized in fixed-size chunks. It is agnostic to RecRule schemas.
 *
 * @details
 * Features:
 * - Thread-safe tracking of total items written via `std::atomic`.
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
        : m_totalWritten(0)          // 1. Строго соблюдаем порядок инициализации
        , m_chunkSize(chunkSize)     // 2.
        , m_buffer(itemCapacity * chunkSize) // 3.
    {
        assert(itemCapacity > 0);
        assert(chunkSize > 0);
    }

    /**
     * @brief Writes raw bytes (multiple chunks) into the buffer.
     * @param data Pointer to the source data.
     * @param count Number of chunks (not bytes) to write.
     */
    void push(const void* data, size_t count) {
        if (!data || count == 0) return;
        m_buffer.push_many(static_cast<const uint8_t*>(data), count * m_chunkSize);
        m_totalWritten.fetch_add(count, std::memory_order_release);
    }

    /**
     * @brief Reads chunks from a relative index within the current window.
     * @param index Offset in chunks from the oldest available data.
     * @param destination Pointer to the target memory.
     * @param count Number of chunks to read.
     */
    void readAt(size_t index, void* destination, size_t count) const {
        if (!destination || count == 0) return;
        m_buffer.peek_many_at(index * m_chunkSize, static_cast<uint8_t*>(destination), count * m_chunkSize);
    }

    /**
     * @brief Returns a pair containing the pointer to the chunk and a split flag.
     * @param index Chunk index.
     * @return Pair {pointer, isSplit}. If isSplit is true, the chunk wraps around the buffer end.
     */
    std::pair<const uint8_t*, bool> getChunkPtr(size_t index) const {
        size_t byteIndex = index * m_chunkSize;
        const uint8_t* ptr = m_buffer.get_ptr_unsafe(byteIndex);

        size_t capacityBytes = m_buffer.capacity();
        size_t head = m_buffer.get_head_index_unsafe();
        size_t physicalStart = (head + byteIndex) % capacityBytes;

        bool isSplit = (physicalStart + m_chunkSize) > capacityBytes;
        return {ptr, isSplit};
    }

    /**
     * @brief Gets the total number of chunks written since creation.
     * @note Thread-safe (atomic load with acquire semantics).
     * @return Total count.
     */
    [[nodiscard]] uint64_t getTotalWritten() const { return m_totalWritten.load(std::memory_order_acquire); }

    [[nodiscard]] size_t size() const { return m_buffer.size() / m_chunkSize; }
    [[nodiscard]] size_t capacity() const { return m_buffer.capacity() / m_chunkSize; }
    [[nodiscard]] size_t getChunkSize() const { return m_chunkSize; }

    void clear() { m_buffer.clear(); }

private:
    // Reordered to minimize internal padding bytes
    alignas(64) std::atomic<uint64_t> m_totalWritten;
    size_t m_chunkSize;
    cyc::CircularBuffer<uint8_t> m_buffer;
};

} // namespace cyc

#endif // CYC_DYNAMICCHUNKBUFFER_H
