// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CbfWriter.h"
#include <iostream>

namespace cyc {

CbfWriter::CbfWriter(const std::string& filename,
                     std::shared_ptr<RecBuffer> buffer,
                     bool autoStart,
                     size_t batchSize)
    : BatchRecordConsumer(buffer, batchSize) // Инициализируем BatchConsumer
    , m_filename(filename)
    , m_alias("Default")
{
    if (autoStart) {
        start();
    }
}

CbfWriter::~CbfWriter() {
    stop();
}

void CbfWriter::setAlias(const std::string& alias) {
    m_alias = alias;
}

void CbfWriter::onConsumeStart() {
    // 1. Открываем файл
    if (!m_cbfFile.open(m_filename, CbfMode::Write)) {
        std::cerr << "CbfWriter: Failed to open file " << m_filename << std::endl;
        return;
    }

    m_cbfFile.setAlias(m_alias);

    // 2. Пишем заголовок (Схему)
    const RecRule& rule = getReader().getRule();
    if (!m_cbfFile.writeHeader(rule)) {
        std::cerr << "CbfWriter: Failed to write header" << std::endl;
        m_cbfFile.close();
        return;
    }

    // 3. Начинаем секцию данных
    if (!m_cbfFile.beginDataSection()) {
        std::cerr << "CbfWriter: Failed to begin data section" << std::endl;
        m_cbfFile.close();
        return;
    }
}

void CbfWriter::consumeBatch(const RecordReader::RecordBatch& batch) {
    if (!m_cbfFile.isOpen()) return;

    // Оптимизация: пишем весь блок памяти сразу
    // RecordBatch гарантирует, что данные лежат непрерывно
    size_t totalBytes = batch.count * batch.recordSize;

    if (totalBytes > 0) {
        m_cbfFile.writeBytes(batch.data, totalBytes);
    }
}

void CbfWriter::onConsumeStop() {
    if (m_cbfFile.isOpen()) {
        m_cbfFile.endDataSection();
        m_cbfFile.close();
    }
}

} // namespace cyc
