// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFREADER_H
#define CYC_CBFREADER_H

#include "RecordProducer.h" // Здесь теперь BatchRecordProducer
#include "CbfDefs.h"
#include <string>
#include <fstream>
#include <vector>

namespace cyc {

/**
 * @brief Читает CBF файл и генерирует поток записей.
 * Использует BatchRecordProducer для прямой записи с диска в буфер обмена.
 */
class CYCLIB_EXPORT CbfReader : public BatchRecordProducer {
public:
    /**
     * @param filename Имя файла.
     * @param bufferCapacity Размер кольцевого буфера.
     * @param autoStart Запустить чтение сразу.
     * @param readBatchSize (Устарело/Игнорируется в Batch режиме) - размер блока чтения определятся Writer'ом.
     * @param writerBatchSize Размер блока записи (и чтения с диска).
     */
    CbfReader(const std::string& filename,
              size_t bufferCapacity = 100000,
              bool autoStart = true,
              size_t writerBatchSize = 1000); // readBatchSize убран, т.к. зависит от writerBatchSize

    ~CbfReader() override;

    bool isValid() const;

protected:
    RecRule defineRule() override;

    /**
     * @brief Читает блок данных из файла напрямую в буфер writer'а.
     */
    size_t produceBatch(const RecordWriter::RecordBatch& batch) override;

    void onProduceStop() override;

private:
    /**
     * @brief Читает заголовок секции CBF.
     */
    bool readSectionHeader(CbfSectionHeader& header);

private:
    std::string m_filename;
    std::ifstream m_file;

    // State
    size_t m_recordSize;
    bool m_valid;
    int64_t m_dataBytesRemaining; // Сколько байт осталось в текущей Data секции (-1 если бесконечно)
};

} // namespace cyc

#endif // CYC_CBFREADER_H
