// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECBUFFER_H
#define CYC_RECBUFFER_H

#include "CycLib_global.h"
#include "DynamicChunkBuffer.h"
#include "RecRule.h"
#include <condition_variable>
#include <functional>

namespace cyc {

class RecordReader;
class RecordWriter;

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

    // --- Reader Management ---
    void addReaderForNotification(RecordReader* reader);
    void removeReaderForNotification(RecordReader* reader);
    // --- Writer Management ---
    void addWriter(RecordWriter* writer);
    void removeWriter(RecordWriter* writer);

    /**
     * @brief Calculates how many records can be written without overwriting unread data.
     * Checks the cursors of all registered readers.
     * @return Number of records available for writing.
     */
    size_t getAvailableWriteSpace() const;

    /**
     * @brief Блокирует поток до тех пор, пока не появится свободное место
     * или не сработает условие прерывания (stopCondition).
     * @param stopCondition Функция, возвращающая true, если ожидание нужно прервать (например, остановка писателя).
     */
    void waitForSpace(std::function<bool()> stopCondition);

    /**
     * @brief Уведомляет писателей о том, что место освободилось (вызывается читателями).
     */
    void notifyWriters();

private:
    void notifyReaders() const;
    size_t getAvailableWriteSpace_nolock() const;
private:
    RecRule m_rule;
    DynamicChunkBuffer m_impl;
    mutable std::shared_mutex m_dataRwMtx;
    std::vector<RecordReader*> m_readers;
    std::vector<RecordWriter*> m_writers;
    mutable std::mutex m_syncMtx;
    std::condition_variable m_spaceCv;
};

} // namespace cyc

#endif // CYC_RECBUFFER_H
