// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFWRITER_H
#define CYC_CBFWRITER_H

#include "RecordConsumer.h"
#include "CbfFile.h"

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class CbfWriter
 * @brief Asynchronous writer for dumping records to CBF format.
 *
 * Employs a BatchRecordConsumer architecture to perform highly efficient,
 * zero-copy block writes directly from the internal buffer memory to the file.
 */
class CYCLIB_EXPORT CbfWriter : public BatchRecordConsumer {
public:
    /**
     * @brief Constructs the CBF writer.
     * @param filename Path to the output file.
     * @param buffer Source RecBuffer to read from.
     * @param autoStart Automatically start the worker thread.
     * @param batchSize Number of records to retrieve and write per chunk.
     */
    CbfWriter(const std::string& filename,
              std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true,
              size_t batchSize = 1000);

    ~CbfWriter() override;

    /**
     * @brief Sets an alias that will be embedded into the file headers.
     * Must be called before the writing begins.
     */
    void setAlias(const std::string& alias);

protected:
    /**
     * @brief Prepares the file, writes the header and opens the data section.
     */
    void onConsumeStart() override;

    /**
     * @brief Writes a contiguous memory block to the file in a single IO call.
     */
    void consumeBatch(const RecordReader::RecordBatch& batch) override;

    /**
     * @brief Finalizes the file (closes data section and stream).
     */
    void onConsumeStop() override;

private:
    std::string m_filename;
    std::string m_alias;
    CbfFile m_cbfFile;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_CBFWRITER_H
