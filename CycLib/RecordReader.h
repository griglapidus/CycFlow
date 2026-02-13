// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDREADER_H
#define CYC_RECORDREADER_H

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
 * @brief Asynchronous reader for RecBuffer with data prefetching.
 *
 * Minimizes read latency by fetching the next batch of records from the
 * RecBuffer in a background thread while the user processes the current batch.
 */
class CYCLIB_EXPORT RecordReader {
public:
    /**
     * @brief Constructs the reader.
     * @param target Shared pointer to the source RecBuffer.
     * @param batchCapacity Number of records to read in one batch.
     */
    RecordReader(std::shared_ptr<RecBuffer> target, size_t batchCapacity);

    /**
     * @brief Destructor.
     * Stops the background worker thread.
     */
    ~RecordReader();

    /**
     * @brief Retrieves the next available record.
     *
     * If the active buffer is empty, this method waits for the background thread
     * to provide a fresh batch of data.
     *
     * @return A Record wrapper pointing to the fetched data.
     */
    Record nextRecord();

    /**
     * @brief Returns the schema used by this reader.
     */
    const RecRule& getRule() const { return m_rule; }

    /**
     * @brief Notifies the reader that new data might be available in the source.
     *
     * Typically called by a writer or an event loop when the underlying RecBuffer
     * receives new data. Wakes up the worker thread if it was idle.
     */
    void notifyDataAvailable();

    /**
     * @brief Stops the background worker.
     */
    void stop();

    /**
     * @brief Initiates graceful shutdown.
     * Captures the current total written count from the source buffer and
     * continues running until all records up to that count are read.
     * After reaching the target, the reader stops automatically.
     */
    void finish();

    /**
    * @brief Checks if there is data immediately available in the active buffer
    * or if the background buffer is full and ready to swap.
    * Thread-safe relative to the worker thread.
    */
    bool hasData() const;

    /**
     * @brief Returns the total number of records processed by this reader.
     * Thread-safe.
     */
    uint64_t getCursor() const { return m_readerCursor.load(); }
private:
    /**
     * @brief Swaps the exhausted active buffer with the filled background buffer.
     */
    bool swapBuffers();

    /**
     * @brief Main loop for the background worker thread.
     * Reads data from RecBuffer into the background buffer.
     */
    void workerLoop();

private:
    std::shared_ptr<RecBuffer> m_target; ///< Source buffer.
    RecRule m_rule;          ///< Record schema.
    size_t m_recSize;        ///< Size of one record in bytes.
    size_t m_capacity;       ///< Batch size.

    std::atomic<uint64_t> m_readerCursor; ///< Global cursor position in the RecBuffer.

    size_t m_activeIdx;      ///< Current read index in the local active buffer.
    size_t m_activeCount;    ///< Number of valid records in the active buffer.

    std::vector<uint8_t> m_bufferA;
    std::vector<uint8_t> m_bufferB;
    std::vector<uint8_t>* m_activeBuf; ///< Buffer currently being read by user.
    std::vector<uint8_t>* m_bgBuf;     ///< Buffer currently being filled by worker.

    std::thread m_worker;
    std::mutex m_mtx;
    std::condition_variable m_cv_user;   ///< Signals user when data is ready.
    std::condition_variable m_cv_worker; ///< Signals worker to fetch more data.

    size_t m_bgCount;        ///< Number of records prepared in background.
    bool m_bgIsFull;         ///< True if background buffer is ready for swap.
    bool m_running;          ///< Thread lifecycle flag.
    // Новые поля для логики finish()
    bool m_finishing;           ///< Флаг режима завершения
    uint64_t m_finishTarget;    ///< Количество записей, которые нужно вычитать перед остановкой
};

} // namespace cyc

#endif // CYC_RECORDREADER_H
