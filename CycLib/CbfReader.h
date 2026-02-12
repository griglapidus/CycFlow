// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFREADER_H
#define CYC_CBFREADER_H

#include "RecordProducer.h"
#include "CbfDefs.h"
#include <string>
#include <fstream>
#include <vector>

namespace cyc {

class CYCLIB_EXPORT CbfReader : public RecordProducer {
public:
    CbfReader(const std::string& filename,
              size_t bufferCapacity = 100000,
              bool autoStart = true,
              size_t readBatchSize = 1000,
              size_t writerBatchSize = 100);

    ~CbfReader() override;

    bool isValid() const;

protected:
    RecRule defineRule() override;

    /**
     * @brief Копирует одну запись из внутреннего буфера в rec.
     * Если внутренний буфер пуст, подгружает новую порцию с диска.
     */
    bool produceStep(Record& rec) override;

    void onProduceStop() override;

private:
    bool readSectionHeader(CbfSectionHeader& header);
    bool refillBuffer();

private:
    std::string m_filename;
    std::ifstream m_file;

    // Config
    size_t m_readBatchSize;

    // State
    size_t m_recordSize;
    std::vector<char> m_readBuffer; // Промежуточный буфер (Disk -> RAM)

    bool m_valid;
    int64_t m_dataBytesRemaining;

    // Управление позицией внутри m_readBuffer
    size_t m_bufferedRecordsCount; // Сколько полных записей сейчас в буфере
    size_t m_currentRecordIdx;     // Индекс следующей записи для выдачи
};

} // namespace cyc

#endif // CYC_CBFREADER_H
