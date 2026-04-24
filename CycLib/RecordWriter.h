// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDWRITER_H
#define CYC_RECORDWRITER_H

#include "Core/RecBuffer.h"
#include "Core/Record.h"

#include <thread>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordWriter
 * @brief Asynchronous writer for RecBuffer using a double-buffering strategy.
 *
 * This class allows a producer thread to write records continuously without
 * being blocked by the underlying storage mechanism (RecBuffer).
 *
 * @details
 * It maintains two intermediate buffers (A and B):
 * - **Active Buffer**: Used by the client to write new data via nextRecord() or nextBatch().
 * - **Background Buffer**: Flushed to the target RecBuffer by a worker thread.
 *
 * When the active buffer fills up, the writer swaps it with the background buffer
 * and signals the worker thread to push the data. Zero-allocation and zero-memset
 * policies are used for maximum throughput.
 */
class CYCLIB_EXPORT RecordWriter {
public:
    /**
     * @struct RecordBatch
     * @brief Represents a contiguous block of memory for batch writing.
     */
    struct RecordBatch {
        uint8_t* data;
        size_t capacity;
        const RecRule& rule;
        size_t recordSize;

        [[nodiscard]] bool isValid() const { return data != nullptr && capacity > 0; }
    };

    /**
     * @brief Constructs the writer.
     * @param target Reference to the destination RecBuffer.
     * @param batchCapacity Number of records to hold in each intermediate buffer.
     * @param blockOnFull If true, writer waits for readers to free space.
     * If false, writer overwrites old data immediately.
     */
    RecordWriter(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull = true);
    ~RecordWriter();

    // --- Single Record API ---

    /**
     * @brief Acquires the next available record slot in the active buffer.
     * @return A Record object pointing to the memory slot. Call commitRecord() after filling it.
     */
    Record nextRecord();

    /**
     * @brief Commits the previously acquired record, advancing the internal index.
     */
    void commitRecord();

    // --- Batch API ---

    /**
     * @brief Acquires a batch of records for bulk writing.
     * @param maxRecords Maximum number of records requested.
     * @param wait If true, blocks until the requested capacity is available.
     * @return A RecordBatch pointing to the available memory block.
     */
    RecordBatch nextBatch(size_t maxRecords, bool wait = true);

    /**
     * @brief Commits a specific number of records written to the current batch.
     * @param count Number of records successfully written.
     */
    void commitBatch(size_t count);

    // --- Control API ---

    /**
     * @brief Forcefully pushes all pending data from the active buffer to the target buffer.
     * Blocks until the background thread finishes writing.
     */
    void flush();

private:
    void stop();
    bool swapBuffers(bool blocking);
    void workerLoop();

private:
    std::shared_ptr<RecBuffer> m_target; ///< Target storage.
    RecRule m_rule;                      ///< Schema definition.
    size_t m_recSize;                    ///< Size of a single record in bytes.
    size_t m_capacity;                   ///< Capacity of intermediate buffers.
    size_t m_earlyThreshold;             ///< Threshold to attempt early swapping.
    bool m_blockOnFull;

    size_t m_currentIdx;                 ///< Current index in the active buffer.
    int m_timestampId;
    size_t m_timestampOffset;

    std::vector<uint8_t> m_bufferA;
    std::vector<uint8_t> m_bufferB;
    std::vector<uint8_t>* m_activeBuf;   ///< Buffer currently being written to by user.
    std::vector<uint8_t>* m_bgBuf;       ///< Buffer currently being flushed by worker.

    std::thread m_worker;
    std::mutex m_mtx;
    std::condition_variable m_cv;        ///< Signals worker that work is available.
    std::condition_variable m_cv_done;   ///< Signals user that flush is complete.

    size_t m_bgCount;                    ///< Number of records in the background buffer.
    std::atomic<bool> m_running;
    bool m_hasWork;                      ///< Flag indicating the worker has data to process.
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDWRITER_H
