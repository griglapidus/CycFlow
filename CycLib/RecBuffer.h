// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECBUFFER_H
#define CYC_RECBUFFER_H

#include "CycLib_global.h"
#include "DynamicChunkBuffer.h"
#include "RecRule.h"

namespace cyc {


class AsyncRecordReader;
/**
 * @brief High-level storage class for structured records.
 *
 * Encapsulates the record schema (RecRule) and the underlying storage mechanism
 * (DynamicChunkBuffer). It bridges the gap between typed Record objects and
 * raw byte storage.
 */
class CYCLIB_EXPORT RecBuffer {
public:
    /**
     * @brief Constructs a buffer for a specific rule and capacity.
     * @param rule The schema definition for records stored in this buffer.
     * @param capacity Maximum number of records the buffer can hold.
     */
    RecBuffer(const RecRule& rule, size_t capacity);

    /**
     * @brief Writes raw record data into the buffer.
     * @param data Pointer to the raw data block containing multiple records.
     * @param count Number of records to write.
     */
    void push(const void* data, size_t count);

    /**
     * @brief Reads records relative to the current buffer window.
     *
     * Provides random access within the currently cached window of records.
     * Useful for history lookups.
     *
     * @param index Relative index (0 is the oldest available record in the buffer).
     * @param dest Destination memory buffer.
     * @param count Number of records to read.
     */
    void readRelative(size_t index, void* dest, size_t count) const;

    /**
     * @brief Gets the rule defining the record structure.
     * @return Reference to RecRule.
     */
    const RecRule& getRule() const;

    /**
     * @brief Gets the size of a single record in bytes.
     * @return Size in bytes.
     */
    size_t getRecSize() const;

    /**
     * @brief Gets the number of records currently stored in the buffer.
     * @return Count of records.
     */
    size_t size() const;

    /**
     * @brief Gets the maximum capacity of the buffer.
     * @return Capacity in records.
     */
    size_t capacity() const;

    /**
     * @brief Gets the total number of records written over the lifetime of the buffer.
     * This is a monotonically increasing counter, unlike size().
     * @return Total count.
     */
    uint64_t getTotalWritten() const;

    void addReaderForNotification(AsyncRecordReader* reader);
    void removeReaderForNotification(AsyncRecordReader* reader);

private:
    void notifyReaders();

private:
    RecRule m_rule;
    DynamicChunkBuffer m_impl;
    std::vector<AsyncRecordReader*> m_readers;
    mutable std::mutex m_readersMtx;
};

} // namespace cyc

#endif // CYC_RECBUFFER_H
