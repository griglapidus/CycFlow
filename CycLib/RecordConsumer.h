// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDCONSUMER_H
#define CYC_RECORDCONSUMER_H

#include "Core/CycLib_global.h"
#include "RecordReader.h"       // also pulls in RecordReaderBase.h
#include <thread>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordConsumer
 * @brief Base class for asynchronously consuming records from a RecBuffer.
 *
 * Manages a RecordReaderBase-derived reader and a background worker thread.
 * By default init() creates a RecordReader (double-buffered). To use a
 * different reader type pass a pre-constructed instance to the second overload:
 *
 * @code
 * consumer.init(std::make_unique<RecordReaderZC>(buffer, batchSize));
 * @endcode
 */
class CYCLIB_EXPORT RecordConsumer {
public:
    RecordConsumer();
    RecordConsumer(std::shared_ptr<RecBuffer> buffer, size_t readerBatchSize = 100);

    /**
     * @brief Constructs the consumer with an explicitly chosen reader type.
     *
     * @code
     * RecordConsumer consumer(UseReader<RecordReaderZC>{}, buffer, 512);
     * @endcode
     */
    template<typename ReaderType>
    RecordConsumer(UseReader<ReaderType>, std::shared_ptr<RecBuffer> buffer,
                   size_t readerBatchSize = 100)
        : RecordConsumer()
    {
        init<ReaderType>(buffer, readerBatchSize);
    }

    virtual ~RecordConsumer();

    /**
     * @brief Initialises the consumer with the specified reader type.
     *
     * @tparam ReaderType  Any class derived from RecordReaderBase.
     *                     Defaults to RecordReader (double-buffered).
     *                     Pass RecordReaderZC for zero-copy access.
     *
     * @code
     * consumer.init(buffer, batchSize);                     // RecordReader
     * consumer.init<RecordReaderZC>(buffer, batchSize);     // RecordReaderZC
     * @endcode
     */
    template<typename ReaderType = RecordReader>
    void init(std::shared_ptr<RecBuffer> buffer, size_t readerBatchSize = 100) {
        readerBatchSize = std::min(std::max(readerBatchSize, buffer->capacity() / 20),
                                   buffer->capacity());
        m_reader = std::make_unique<ReaderType>(buffer, readerBatchSize);
    }

    void start();
    void stop();

    /**
     * @brief Consumes all remaining records up to the current buffer cursor, then stops.
     */
    void finish();

    [[nodiscard]] bool isRunning() const;

protected:
    virtual void onConsumeStart() {}
    virtual void consumeRecord(const Record& rec) = 0;
    virtual void onConsumeStop() {}

    [[nodiscard]] const RecordReaderBase& getReader() const;

    virtual void workerLoop();

protected:
    std::unique_ptr<RecordReaderBase> m_reader;
    std::atomic<bool> m_running;
    std::thread       m_worker;
};

/**
 * @class BatchRecordConsumer
 * @brief Consumer variant for processing data in large contiguous blocks.
 */
class CYCLIB_EXPORT BatchRecordConsumer : public RecordConsumer {
public:
    using RecordConsumer::RecordConsumer;

protected:
    void consumeRecord(const Record& rec) override final {}

    /**
     * @brief Processes a batch of records at once.
     * The parameter type is RecordReader::RecordBatch which is an alias for
     * RecordReaderBase::RecordBatch — both names refer to the same type.
     */
    virtual void consumeBatch(const RecordReader::RecordBatch& batch) = 0;

    void workerLoop() override;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDCONSUMER_H
