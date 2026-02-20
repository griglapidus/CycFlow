// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECBUFFER_H
#define CYC_RECBUFFER_H

#include "CycLib_global.h"
#include "DynamicChunkBuffer.h"
#include "IRecBufferClient.h"
#include "RecRule.h"
#include "Record.h"
#include <condition_variable>
#include <functional>

namespace cyc {

/**
 * @class RecBuffer
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
     * @brief Reads data based on an absolute global cursor.
     * @note Thread-safe: recalculates relative offset under lock to avoid race conditions.
     * @param globalCursor The absolute position (totalWritten index) to read from.
     * @param dest Destination buffer.
     * @param count Number of records to read.
     * @return Number of records actually read. Returns 0 if globalCursor is invalid (overwritten).
     */
    size_t readFromGlobal(uint64_t globalCursor, void* dest, size_t count) const;

    /**
     * @brief Reads records relative to the current buffer window.
     * @param index Relative index (0 is the oldest available record in the buffer).
     * @param dest Destination memory buffer.
     * @param count Number of records to read.
     */
    void readRelative(size_t index, void* dest, size_t count) const;

    /**
     * @brief Provides zero-copy access to a record by index.
     * @param index Relative index of the record.
     * @param visitor Callback function to process the Record.
     * @warning The Record is only valid inside the callback. Do not store its pointer.
     */
    void processRecord(size_t index, std::function<void(const Record&)> visitor) const;

    /**
     * @brief Safely copies a record at a specific index into a pre-allocated Record object.
     * @param index Relative index of the record.
     * @param dest Pre-allocated Record object. Must have valid memory and matching size.
     * @return true if successful, false if index is out of bounds or dest is invalid.
     */
    bool copyRecord(size_t index, Record& dest) const;

    [[nodiscard]] const RecRule& getRule() const;
    [[nodiscard]] size_t getRecSize() const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t capacity() const;

    /**
     * @brief Gets the total number of records written over the lifetime of the buffer.
     * @return Total count.
     */
    [[nodiscard]] uint64_t getTotalWritten() const;

    [[nodiscard]] std::tuple<uint64_t, size_t> getTotalWrittenAndSize() const;

    void addClient(IRecBufferClient* client);
    void removeClient(IRecBufferClient* client);

    /**
     * @brief Calculates how many records can be written without overwriting unread data.
     * @return Number of records available for writing.
     */
    [[nodiscard]] size_t getAvailableWriteSpace() const;

    /**
     * @brief Blocks the thread until space becomes available or the stop condition is met.
     * @param stopCondition Function returning true if waiting should be interrupted.
     */
    void waitForSpace(const std::function<bool()>& stopCondition);

    /**
     * @brief Notifies writers that space has been freed.
     */
    void notifyWriters();

private:
    void notifyClients() const;
    size_t getAvailableWriteSpace_nolock() const;
    uint64_t calculateMinReadCursor_nolock() const;

private:
    RecRule m_rule;
    DynamicChunkBuffer m_impl;

    mutable std::shared_mutex m_dataRwMtx;
    mutable std::mutex m_syncMtx;
    std::condition_variable m_spaceCv;

    std::vector<IRecBufferClient*> m_clients;
    mutable uint64_t m_phantomReadCursor;
};

} // namespace cyc

#endif // CYC_RECBUFFER_H
