// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFREADER_H
#define CYC_CBFREADER_H

#include "RecordProducer.h"
#include "CbfFile.h"

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class CbfReader
 * @brief Reads CBF files and generates a stream of records.
 *
 * Uses BatchRecordProducer to directly copy large binary blocks from the disk
 * into the internal buffer, bypassing per-record allocations.
 */
class CYCLIB_EXPORT CbfReader : public BatchRecordProducer {
public:
    /**
     * @brief Constructs the CBF reader with the default RecordWriter (double-buffered).
     */
    CbfReader(const std::string& filename,
              size_t bufferCapacity = 100000,
              bool autoStart = true,
              size_t writerBatchSize = 1000);

    /**
     * @brief Constructs the CBF reader with an explicitly chosen writer type.
     * @code
     * CbfReader reader(UseWriter<RecordWriterZC>{}, "data.cbf");
     * @endcode
     */
    template<typename WriterType>
    CbfReader(UseWriter<WriterType>, const std::string& filename,
              size_t bufferCapacity = 100000,
              bool autoStart = true,
              size_t writerBatchSize = 1000)
        : m_filename(filename)
        , m_recordSize(0)
        , m_valid(false)
        , m_dataBytesRemaining(0)
    {
        init(UseWriter<WriterType>{}, bufferCapacity, writerBatchSize);
        if (autoStart) start();
    }

    ~CbfReader() override;

    [[nodiscard]] bool isValid() const;

protected:
    RecRule defineRule() override;

    /**
     * @brief Reads a raw byte block directly into the producer batch.
     */
    size_t produceBatch(const RecordWriter::RecordBatch& batch) override;

    void onProduceStop() override;

private:
    std::string m_filename;
    CbfFile m_cbfFile;
    size_t m_recordSize;
    bool m_valid;
    int64_t m_dataBytesRemaining;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_CBFREADER_H
