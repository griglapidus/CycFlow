// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDWRITER_H
#define CYC_RECORDWRITER_H

#include "Core/RecBuffer.h"
#include "Core/Record.h"

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <atomic>

namespace cyc {

/**
 * @brief Asynchronous writer for RecBuffer using double-buffering strategy.
 *
 * This class allows a producer thread to write records continuously without
 * being blocked by the underlying storage mechanism (RecBuffer).
 *
 * @details
 * It maintains two intermediate buffers (A and B):
 * - **Active Buffer**: Used by the client to write new data via nextRecord().
 * - **Background Buffer**: Flushed to the target RecBuffer by a worker thread.
 *
 * When the active buffer fills up, the writer swaps it with the background buffer
 * and signals the worker thread.
 */
class CYCLIB_EXPORT RecordWriter {
public:
    /**
     * @brief Constructs the writer.
     * @param target Reference to the destination RecBuffer.
     * @param batchCapacity Number of records to hold in each intermediate buffer.
     * @param blockOnFull If true, writer waits for readers to free space.
     * If false, writer overwrites old data immediately.
     */
    RecordWriter(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull = true);

    /**
     * @brief Destructor.
     * Signals the worker to stop, flushes remaining data, and joins the thread.
     */
    ~RecordWriter();

    /**
     * @brief Prepares the next record slot for writing.
     *
     * @return A Record wrapper pointing to the current memory slot in the active buffer.
     *
     * @warning The returned Record object is valid ONLY until commitRecord() is called.
     * Calling commitRecord() may trigger a buffer swap, invalidating the pointer
     * held by the Record object. Do not store the Record object across commits.
     */
    Record nextRecord();

    /**
     * @brief Finalizes the current record and advances the write cursor.
     *
     * If the active buffer becomes full, this method triggers a buffer swap
     * and wakes up the background worker thread.
     */
    void commitRecord();

    /**
     * @brief Forcefully pushes all pending data to the target buffer.
     *
     * Blocks the calling thread until the background worker has finished
     * processing the current queue.
     */
    void flush();

private:
    /**
     * @brief Signals the worker thread to stop processing.
     */
    void stop();

    /**
     * @brief Swaps the active and background buffers.
     * @param blocking If true, waits for the worker to finish the previous batch.
     * If false, returns immediately if worker is busy or mutex is locked.
     * @return True if swapped, false if skipped (only applicable when blocking=false).
     */
    bool swapBuffers(bool blocking);

    /**
     * @brief Main loop for the background worker thread.
     * Waits for the 'hasWork' signal and pushes the background buffer to RecBuffer.
     */
    void workerLoop();

private:
    std::shared_ptr<RecBuffer> m_target; ///< Target storage.
    RecRule m_rule;          ///< Schema definition.
    size_t m_recSize;        ///< Size of a single record in bytes.
    size_t m_capacity;       ///< Capacity of intermediate buffers.
    size_t m_earlyThreshold; ///< Threshold (20%) to attempt early swapping.
    bool m_blockOnFull;

    size_t m_currentIdx;     ///< Current index in the active buffer.

    std::vector<uint8_t> m_bufferA;
    std::vector<uint8_t> m_bufferB;
    std::vector<uint8_t>* m_activeBuf; ///< Buffer currently being written to by user.
    std::vector<uint8_t>* m_bgBuf;     ///< Buffer currently being flushed by worker.

    std::thread m_worker;
    std::mutex m_mtx;
    std::condition_variable m_cv;      ///< Signals worker that work is available.
    std::condition_variable m_cv_done; ///< Signals user that flush is complete.

    std::atomic<size_t> m_bgCount;        ///< Number of records in the background buffer.
    bool m_running;          ///< Thread lifecycle flag.
    bool m_hasWork;          ///< Flag indicating if background buffer needs flushing.
};

} // namespace cyc

#endif // CYC_RECORDWRITER_H
