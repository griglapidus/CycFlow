// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFWRITER_H
#define CYC_CBFWRITER_H

#include "RecordConsumer.h"
#include "CbfFile.h"
#include <atomic>

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
     * @brief Default constructor. Leaves the writer uninitialised.
     * Call init() before using the writer.
     */
    CbfWriter();

    /**
     * @brief Constructs the CBF writer with the default RecordReader (double-buffered).
     *
     * @param maxRecords  Rotate to a new file after this many records (0 = disabled).
     */
    CbfWriter(const std::string& filename,
              std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true,
              size_t batchSize = 1000,
              bool addTimestampSuffix = true,
              size_t maxRecords = 0);

    /**
     * @brief Constructs the CBF writer with an explicitly chosen reader type.
     * @code
     * CbfWriter writer(UseReader<RecordReaderZC>{}, "out.cbf", buffer);
     * @endcode
     */
    template<typename ReaderType>
    CbfWriter(UseReader<ReaderType>, const std::string& filename,
              std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t batchSize = 1000,
              bool addTimestampSuffix = true, size_t maxRecords = 0)
        : CbfWriter()
    {
        init(UseReader<ReaderType>{}, filename, buffer, autoStart, batchSize, addTimestampSuffix, maxRecords);
    }

    ~CbfWriter() override;

    /**
     * @brief Initialises the writer with the default RecordReader (double-buffered).
     * Must be called once on default-constructed instances.
     *
     * @param filename             Output CBF path.
     * @param buffer               Shared pointer to the source RecBuffer.
     * @param autoStart            If @c true, starts the worker thread immediately.
     * @param batchSize            Batch size for the internal reader.
     * @param addTimestampSuffix   If @c true, the current local time (with
     *                             millisecond precision) is inserted before the
     *                             extension: @c "Foo.cbf" → @c "Foo_2026-05-03_19-51-15-022.cbf".
     * @param maxRecords           Rotate to a new file after this many records (0 = disabled).
     */
    void init(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t batchSize = 1000,
              bool addTimestampSuffix = true, size_t maxRecords = 0) {
        init(UseReader<RecordReader>{}, filename, buffer, autoStart, batchSize, addTimestampSuffix, maxRecords);
    }

    /**
     * @brief Initialises the writer with an explicitly chosen reader type.
     *
     * @tparam ReaderType          Any class derived from RecordReaderBase.
     * @param filename             Output CBF path.
     * @param buffer               Shared pointer to the source RecBuffer.
     * @param autoStart            If @c true, starts the worker thread immediately.
     * @param batchSize            Batch size for the internal reader.
     * @param addTimestampSuffix   If @c true, the current local time (with
     *                             millisecond precision) is inserted before the
     *                             extension: @c "Foo.cbf" → @c "Foo_2026-05-03_19-51-15-022.cbf".
     * @param maxRecords           Rotate to a new file after this many records (0 = disabled).
     * @code
     * writer.init(UseReader<RecordReaderZC>{}, "out.cbf", buffer);
     * @endcode
     */
    template<typename ReaderType>
    void init(UseReader<ReaderType>, const std::string& filename, std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t batchSize = 1000,
              bool addTimestampSuffix = true, size_t maxRecords = 0) {
        m_baseFilename = filename;
        m_addTimestampSuffix = addTimestampSuffix;
        m_maxRecords = maxRecords;
        m_recordCount = 0;
        m_filename = addTimestampSuffix ? createSuffixedFilename(filename) : filename;
        RecordConsumer::init(UseReader<ReaderType>{}, buffer, batchSize);
        if (autoStart) start();
    }

    /**
     * @brief Sets an alias that will be embedded into the file headers.
     * Must be called before the writing begins.
     */
    void setAlias(const std::string& alias);

    /**
     * @brief Closes the current file and begins writing to a new one.
     *
     * Thread-safe: may be called from any thread while the writer is running.
     * The actual rotation happens at the start of the next batch, so there is
     * no data loss — all records written before the call land in the old file.
     */
    void restart();

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
    [[nodiscard]] std::string createSuffixedFilename(const std::string& originalName) const;
    void rotateFile();

    std::string m_baseFilename;
    std::string m_filename;
    bool        m_addTimestampSuffix = true;
    std::string m_alias = "Default";
    CbfFile     m_cbfFile;
    size_t      m_maxRecords  = 0;
    size_t      m_recordCount = 0;
    std::atomic<bool> m_restartRequested{false};
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_CBFWRITER_H
