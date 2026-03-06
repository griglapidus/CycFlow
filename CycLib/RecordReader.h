// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDREADER_H
#define CYC_RECORDREADER_H

#include "Core/RecBuffer.h"
#include "Core/Record.h"
#include "Core/IRecBufferClient.h"

#include <thread>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordReader
 * @brief Asynchronous reader for RecBuffer with double-buffered prefetching.
 *
 * Minimizes read latency by fetching the next batch of records from the
 * RecBuffer in a background thread while the user processes the current batch.
 */
class CYCLIB_EXPORT RecordReader : public IRecBufferClient {
public:
    /**
     * @struct RecordBatch
     * @brief Represents a contiguous block of fetched memory.
     */
    struct RecordBatch {
        const uint8_t* data;
        size_t count;
        const RecRule& rule;
        size_t recordSize;

        [[nodiscard]] bool isValid() const { return data != nullptr && count > 0; }
    };

    /**
     * @brief Constructs the reader.
     * @param target Shared pointer to the source RecBuffer.
     * @param batchCapacity Number of records to read in one batch.
     */
    RecordReader(std::shared_ptr<RecBuffer> target, size_t batchCapacity);
    ~RecordReader() override;

    // --- IRecBufferClient Implementation ---
    void notifyDataAvailable() override;
    [[nodiscard]] uint64_t getCursor() const override;

    // --- Control API ---
    void stop();
    void finish();

    // --- Read API ---

    /**
     * @brief Fetches the next available batch of records.
     * @param maxRecords Maximum records to return.
     * @param wait If true, blocks until data is available.
     * @return RecordBatch pointing to the data.
     */
    RecordBatch nextBatch(size_t maxRecords, bool wait = true);

    /**
     * @brief Fetches a single record.
     * @return Record object. Check isValid() to ensure data was retrieved.
     */
    Record nextRecord();

    [[nodiscard]] const RecRule& getRule() const { return m_rule; }

private:
    void workerLoop();
    bool swapBuffers();

private:
    std::shared_ptr<RecBuffer> m_target;  ///< Source buffer.
    RecRule m_rule;                       ///< Record schema.
    size_t m_recSize;                     ///< Size of one record in bytes.
    size_t m_capacity;                    ///< Batch size.

    std::atomic<uint64_t> m_readerCursor; ///< Global cursor position in the RecBuffer.

    size_t m_activeIdx;                   ///< Current read index in the local active buffer.
    size_t m_activeCount;                 ///< Number of valid records in the active buffer.

    std::vector<uint8_t> m_bufferA;
    std::vector<uint8_t> m_bufferB;
    std::vector<uint8_t>* m_activeBuf;    ///< Buffer currently being read by user.
    std::vector<uint8_t>* m_bgBuf;        ///< Buffer currently being filled by worker.

    std::thread m_worker;
    std::mutex m_mtx;
    std::condition_variable m_cv_user;    ///< Signals user when data is ready.
    std::condition_variable m_cv_worker;  ///< Signals worker to fetch more data.

    size_t m_bgCount;                     ///< Number of records prepared in background.
    bool m_bgIsFull;                      ///< True if background buffer is ready for swap.
    std::atomic<bool> m_running;
    bool m_finishing;
    uint64_t m_finishTarget;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDREADER_H
