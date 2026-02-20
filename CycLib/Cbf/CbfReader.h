// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFREADER_H
#define CYC_CBFREADER_H

#include "RecordProducer.h"
#include "CbfFile.h"

namespace cyc {

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
     * @brief Constructs the CBF Reader.
     * @param filename Path to the input file.
     * @param bufferCapacity Maximum records the circular buffer can hold.
     * @param autoStart Automatically start reading in a background thread.
     * @param writerBatchSize Number of records to read per physical I/O request.
     */
    CbfReader(const std::string& filename,
              size_t bufferCapacity = 100000,
              bool autoStart = true,
              size_t writerBatchSize = 1000);

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

} // namespace cyc

#endif // CYC_CBFREADER_H
