// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDREADERBASE_H
#define CYC_RECORDREADERBASE_H

#include "Core/RecBuffer.h"
#include "Core/Record.h"
#include "Core/IRecBufferClient.h"

namespace cyc {

/**
 * @brief Tag type for selecting a reader implementation in consumer constructors.
 *
 * Pass a default-constructed instance as the first constructor argument to choose
 * a reader type other than the default RecordReader:
 * @code
 * CsvWriter writer(UseReader<RecordReaderZC>{}, "out.csv", buffer);
 * @endcode
 *
 * @tparam ReaderType Any class derived from RecordReaderBase.
 */
template<typename ReaderType>
struct UseReader {};

CYCLIB_SUPPRESS_C4251

/**
 * @class RecordReaderBase
 * @brief Abstract base class for RecBuffer readers.
 *
 * Holds common state (target buffer, record schema, global cursor) and declares
 * the interface that all reader implementations must satisfy. Two concrete
 * implementations are provided:
 *
 * | Class          | Strategy            | Use when…                                         |
 * |----------------|---------------------|---------------------------------------------------|
 * | RecordReader   | Double-buffered, async worker thread | Consumer is slow (disk I/O, network) |
 * | RecordReaderZC | Zero-copy, synchronous              | Consumer keeps up with writer (UI, fast pipeline) |
 *
 * @see RecordReader
 * @see RecordReaderZC
 */
class CYCLIB_EXPORT RecordReaderBase : public IRecBufferClient {
public:

    /**
     * @struct RecordBatch
     * @brief Read-only view of a contiguous block of record memory.
     *
     * The pointer lifetime depends on the concrete implementation:
     * - **RecordReader**: valid until the reader is destroyed or the next
     *   nextBatch() call replaces the active buffer.
     * - **RecordReaderZC**: valid only until the next nextBatch() or release()
     *   call — the memory lives directly inside the ring buffer.
     */
    struct RecordBatch {
        const uint8_t* data;   ///< Pointer to the first byte of the first record.
        size_t count;          ///< Number of records in this batch.
        const RecRule& rule;   ///< Schema describing the record layout.
        size_t recordSize;     ///< Size of one record in bytes.

        /** @brief Returns @c true if the batch contains at least one valid record. */
        [[nodiscard]] bool isValid() const { return data != nullptr && count > 0; }
    };

    virtual ~RecordReaderBase() = default;

    // -------------------------------------------------------------------------
    // IRecBufferClient
    // -------------------------------------------------------------------------

    /**
     * @brief Called by RecBuffer when new records have been written.
     * @note Internal — invoked by the buffer infrastructure, not by application code.
     */
    virtual void notifyDataAvailable() override = 0;

    /**
     * @brief Returns the global read cursor.
     *
     * The cursor is the absolute index of the next record to be consumed.
     * RecBuffer uses the minimum cursor across all registered readers to
     * determine how far the writer may advance before overwriting unread data.
     *
     * @return Absolute position in the lifetime write sequence of the buffer.
     */
    [[nodiscard]] uint64_t getCursor() const override final;

    // -------------------------------------------------------------------------
    // Control
    // -------------------------------------------------------------------------

    /**
     * @brief Stops the reader immediately, discarding any pending data.
     *
     * Unblocks any thread currently waiting in nextBatch(). The reader is
     * left in a stopped state; subsequent nextBatch() calls return invalid batches.
     */
    virtual void stop() = 0;

    /**
     * @brief Drains all records written up to the moment of this call, then stops.
     *
     * Unlike stop(), finish() guarantees that every record already in the buffer
     * at call time will be delivered to the consumer before the reader halts.
     *
     * @note Thread-safe; may be called from a different thread than the reader.
     */
    virtual void finish() = 0;

    // -------------------------------------------------------------------------
    // Read API
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the next available batch of records.
     *
     * @param maxRecords Upper bound on the number of records to return. The actual
     *                   count may be lower due to available data or ring-buffer
     *                   layout constraints.
     * @param wait       If @c true, blocks until at least one record is available
     *                   or the reader is stopped/finished.
     * @return A RecordBatch. Check RecordBatch::isValid() before accessing data.
     */
    virtual RecordBatch nextBatch(size_t maxRecords, bool wait = true) = 0;

    /**
     * @brief Explicitly releases the current pinned batch.
     *
     * Relevant only for RecordReaderZC: advances the cursor so the writer may
     * reuse the freed ring-buffer region. Calling this after processing a batch
     * but before the next nextBatch() is required when the caller must not hold
     * the pointer any longer.
     *
     * For RecordReader the default no-op is correct: the cursor is advanced by
     * the worker thread immediately after copying, independent of when the caller
     * processes the data.
     *
     * @see RecordReaderZC
     */
    virtual void release() {}

    /**
     * @brief Returns the next single record.
     *
     * Convenience wrapper around nextBatch(1, true). Blocks until a record is
     * available. The returned Record shares the same lifetime guarantees as the
     * underlying RecordBatch.
     *
     * @return A Record view. Check Record::isValid() before use.
     */
    Record nextRecord();

    /**
     * @brief Returns the record schema associated with the source buffer.
     * @return Reference to the RecRule describing field layout and types.
     */
    [[nodiscard]] const RecRule& getRule() const;

protected:
    RecordReaderBase() = default;
    RecordReaderBase(const RecordReaderBase&) = delete;
    RecordReaderBase& operator=(const RecordReaderBase&) = delete;

    /**
     * @brief Common initialisation shared by all derived readers.
     *
     * Sets schema fields, computes the initial cursor position (skipping history
     * that was written before the reader was created), and registers @c this as
     * a client of @p target.
     *
     * @note Must be called from each derived init() before any class-specific
     *       setup. Derived destructors are responsible for calling
     *       m_target->removeClient(this) after stopping any background threads.
     *
     * @param target        Shared pointer to the source RecBuffer.
     * @param batchCapacity Maximum number of records to fetch per batch.
     */
    void initBase(std::shared_ptr<RecBuffer> target, size_t batchCapacity);

protected:
    std::shared_ptr<RecBuffer> m_target;              ///< Source buffer this reader is subscribed to.
    RecRule  m_rule;                                  ///< Cached record schema.
    size_t   m_recSize  = 0;                          ///< Cached size of one record in bytes.
    size_t   m_capacity = 0;                          ///< Maximum records per batch.
    std::atomic<uint64_t> m_readerCursor{0};          ///< Global absolute read position; drives backpressure.
    std::atomic<bool>     m_running{false};           ///< @c true while the reader is active.
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDREADERBASE_H
