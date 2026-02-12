// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CbfWriter.h"
#include <iostream>

namespace cyc {

CbfWriter::CbfWriter(const std::string& filename,
                     std::shared_ptr<RecBuffer> buffer,
                     bool autoStart,
                     size_t batchSize)
    : RecordConsumer(buffer, batchSize)
    , m_filename(filename)
    , m_alias("Default")
{
    if (autoStart) {
        start();
    }
}

CbfWriter::~CbfWriter() {
    // Останавливаем поток и ждем завершения перед разрушением объекта
    stop();
}

void CbfWriter::setAlias(const std::string& alias) {
    m_alias = alias;
}

void CbfWriter::onConsumeStart() {
    // 1. Открываем файл на запись (перезаписываем, если есть)
    if (!m_cbfFile.open(m_filename, CbfMode::Write)) {
        std::cerr << "CbfWriter: Failed to open file " << m_filename << std::endl;
        return;
    }

    // 2. Устанавливаем Alias
    m_cbfFile.setAlias(m_alias);

    // 3. Записываем заголовок файла (Схему данных)
    // Схему берем из Reader'а, который уже инициализирован буфером
    const RecRule& rule = getReader().getRule();
    if (!m_cbfFile.writeHeader(rule)) {
        std::cerr << "CbfWriter: Failed to write header" << std::endl;
        m_cbfFile.close();
        return;
    }

    // 4. Начинаем секцию данных
    if (!m_cbfFile.beginDataSection()) {
        std::cerr << "CbfWriter: Failed to begin data section" << std::endl;
        m_cbfFile.close();
        return;
    }
}

void CbfWriter::consumeRecord(const Record& rec) {
    // Пишем запись только если файл открыт и готов
    if (m_cbfFile.isOpen()) {
        m_cbfFile.writeRecord(rec);
    }
}

void CbfWriter::onConsumeStop() {
    if (m_cbfFile.isOpen()) {
        // Завершаем секцию данных (обновляем размер в заголовке)
        m_cbfFile.endDataSection();

        // Закрываем файл
        m_cbfFile.close();
    }
}

} // namespace cyc
