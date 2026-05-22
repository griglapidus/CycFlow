// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDWRITERBASE_H
#define CYC_RECORDWRITERBASE_H

#include "Core/RecBuffer.h"
#include "Core/Record.h"

namespace cyc {

/**
 * @brief Tag type for selecting a writer implementation in producer constructors.
 *
 * Pass a default-constructed instance as the first constructor argument to choose
 * a writer type other than the default RecordWriter:
 * @code
 * CbfReader reader(UseWriter<RecordWriterZC>{}, "data.cbf");
 * @endcode
 *
 * @tparam WriterType Any class derived from RecordWriterBase.
 */
template<typename WriterType>
struct UseWriter {};

CYCLIB_SUPPRESS_C4251

/**
 * @class RecordWriterBase
 * @brief Abstract base class for RecBuffer writers.
 *
 * Holds common state (target buffer, record schema, timestamp field location)
 * and declares the write interface that all writer implementations must satisfy.
 * Two concrete implementations are provided:
 *
 * | Class           | Strategy              | Use when…                                            |
 * |-----------------|-----------------------|------------------------------------------------------|
 * | RecordWriter    | Double-buffered, async worker thread | Producer is fast; decouple from buffer backpressure |
 * | RecordWriterZC  | Single-buffered, synchronous        | Lower latency / simpler flow; writer can stall       |
 *
 * @see RecordWriter
 * @see RecordWriterZC
 */
class CYCLIB_EXPORT RecordWriterBase {
public:

    /**
     * @struct RecordBatch
     * @brief Writable view of a contiguous block of record memory.
     *
     * Returned by nextBatch(). The caller fills data[0 .. capacity*recordSize),
     * then calls commitBatch() with the actual number of records written.
     */
    struct RecordBatch {
        uint8_t*       data;        ///< Pointer to the first byte of the first record slot.
        size_t         capacity;    ///< Maximum number of records the block can hold.
        const RecRule& rule;        ///< Schema describing the record layout.
        size_t         recordSize;  ///< Size of one record in bytes.

        /** @brief Returns @c true if the batch contains at least one writable slot. */
        [[nodiscard]] bool isValid() const { return data != nullptr && capacity > 0; }
    };

    virtual ~RecordWriterBase() = default;

    // -------------------------------------------------------------------------
    // Single-record API
    // -------------------------------------------------------------------------

    /**
     * @brief Acquires the next available record slot.
     *
     * The returned Record points to zeroed memory. Fill it, then call
     * commitRecord(). If the internal buffer is full the call may block
     * (when blockOnFull was set to @c true during init).
     *
     * @return A Record view into the next writable slot.
     */
    virtual Record nextRecord() = 0;

    /**
     * @brief Commits the record acquired by the last nextRecord() call.
     *
     * Stamps a timestamp into the record if none was set by the caller, then
     * makes the record available for the underlying RecBuffer.
     */
    virtual void commitRecord() = 0;

    // -------------------------------------------------------------------------
    // Batch API
    // -------------------------------------------------------------------------

    /**
     * @brief Acquires a contiguous block of writable record slots.
     *
     * @param maxRecords Upper bound on the number of slots requested. The actual
     *                   capacity may be lower depending on available buffer space.
     * @param wait       If @c true, blocks until at least one slot is available.
     * @return A RecordBatch. Check RecordBatch::isValid() before writing.
     */
    virtual RecordBatch nextBatch(size_t maxRecords, bool wait = true) = 0;

    /**
     * @brief Commits @p count records written into the last batch.
     *
     * Stamps timestamps on records where none was set, then forwards the data
     * to the target RecBuffer.
     *
     * @param count Number of records actually written (must be ≤ batch.capacity).
     */
    virtual void commitBatch(size_t count) = 0;

    // -------------------------------------------------------------------------
    // Control
    // -------------------------------------------------------------------------

    /**
     * @brief Flushes all pending records to the target buffer synchronously.
     *
     * Blocks until all in-flight data has been delivered to RecBuffer.
     */
    virtual void flush() = 0;

    /**
     * @brief Returns the record schema of the target buffer.
     * @return Reference to the RecRule describing field layout and types.
     */
    [[nodiscard]] const RecRule& getRule() const;

protected:
    RecordWriterBase() = default;
    RecordWriterBase(const RecordWriterBase&) = delete;
    RecordWriterBase& operator=(const RecordWriterBase&) = delete;

    /**
     * @brief Common initialisation shared by all derived writers.
     *
     * Caches the record schema, record size, blockOnFull flag, and the
     * location of the TimeStamp field used for auto-timestamping.
     *
     * @param target       Shared pointer to the destination RecBuffer.
     * @param batchCapacity  Maximum number of records per batch.
     * @param blockOnFull  If @c true, stall when the target buffer is full;
     *                     if @c false, overwrite oldest data immediately.
     */
    void initBase(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull);

protected:
    std::shared_ptr<RecBuffer> m_target;           ///< Destination buffer.
    RecRule  m_rule;                               ///< Cached record schema.
    size_t   m_recSize        = 0;                 ///< Size of one record in bytes.
    size_t   m_capacity       = 0;                 ///< Maximum records per batch.
    bool     m_blockOnFull    = true;              ///< Stall on full buffer when @c true.
    int      m_timestampId    = 0;                 ///< Field ID of the TimeStamp attribute.
    size_t   m_timestampOffset = 0;                ///< Byte offset of TimeStamp within a record.
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDWRITERBASE_H
