// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CbfReader.h"
#include <iostream>
#include <cstring>
#include <algorithm> // для std::min

namespace cyc {

CbfReader::CbfReader(const std::string& filename,
                     size_t bufferCapacity,
                     bool autoStart,
                     size_t writerBatchSize)
    : BatchRecordProducer(bufferCapacity, writerBatchSize)
    , m_filename(filename)
    , m_recordSize(0)
    , m_valid(false)
    , m_dataBytesRemaining(0)
{
    if (autoStart) {
        start();
    }
}

CbfReader::~CbfReader() {
    stop();
    if (m_file.is_open()) {
        m_file.close();
    }
}

bool CbfReader::isValid() const {
    return m_valid;
}

RecRule CbfReader::defineRule() {
    m_valid = false;
    // Открываем файл
    m_file.open(m_filename, std::ios::binary | std::ios::in);
    if (!m_file.is_open()) {
        std::cerr << "CbfReader warning: File not found: " << m_filename << std::endl;
        return RecRule();
    }

    CbfSectionHeader secHeader;
    if (!readSectionHeader(secHeader)) {
        std::cerr << "CbfReader error: Failed to read file header: " << m_filename << std::endl;
        m_file.close();
        return RecRule();
    }

    if (secHeader.type != static_cast<uint8_t>(CbfSectionType::Header)) {
        std::cerr << "CbfReader error: Invalid first section (expected Header)" << std::endl;
        m_file.close();
        return RecRule();
    }

    if (secHeader.bodyLength <= 0) {
        std::cerr << "CbfReader error: Empty schema body" << std::endl;
        m_file.close();
        return RecRule();
    }

    std::string schemaText;
    try {
        schemaText.resize(secHeader.bodyLength);
        m_file.read(&schemaText[0], secHeader.bodyLength);

        if (m_file.gcount() != secHeader.bodyLength) {
            std::cerr << "CbfReader error: Unexpected EOF reading schema" << std::endl;
            m_file.close();
            return RecRule();
        }
    } catch (...) {
        m_file.close();
        return RecRule();
    }

    RecRule rule = RecRule::fromText(schemaText);
    m_recordSize = rule.getRecSize();

    // Ищем секцию данных
    while (m_file.good()) {
        if (!readSectionHeader(secHeader)) break;

        if (secHeader.type == static_cast<uint8_t>(CbfSectionType::Data)) {
            m_dataBytesRemaining = secHeader.bodyLength;
            m_valid = true;
            return rule;
        } else {
            // Пропускаем неизвестные секции
            m_file.seekg(secHeader.bodyLength, std::ios::cur);
        }
    }

    std::cerr << "CbfReader warning: No data section found in " << m_filename << std::endl;
    m_file.close();
    return rule;
}

bool CbfReader::readSectionHeader(CbfSectionHeader& header) {
    m_file.read(reinterpret_cast<char*>(&header), sizeof(CbfSectionHeader));
    if (m_file.gcount() != sizeof(CbfSectionHeader)) return false;
    if (header.marker != CBF_SECTION_MARKER) return false;
    return true;
}

size_t CbfReader::produceBatch(const RecordWriter::RecordBatch& batch) {
    if (!m_valid || !m_file.good()) return 0;
    if (m_dataBytesRemaining == 0) return 0; // Секция закончилась

    // 1. Рассчитываем, сколько записей можем прочитать
    size_t countToRead = batch.capacity;

    // Если размер секции известен и ограничен (не -1)
    if (m_dataBytesRemaining != -1) {
        size_t recordsRemaining = static_cast<size_t>(m_dataBytesRemaining) / m_recordSize;
        if (countToRead > recordsRemaining) {
            countToRead = recordsRemaining;
        }
    }

    if (countToRead == 0) return 0;

    // 2. Рассчитываем размер в байтах
    size_t bytesToRead = countToRead * m_recordSize;

    // 3. Читаем прямо в целевой буфер
    // batch.data - это указатель uint8_t*, ifstream хочет char*
    m_file.read(reinterpret_cast<char*>(batch.data), bytesToRead);

    size_t bytesRead = m_file.gcount();

    // 4. Обновляем счетчики
    size_t recordsRead = bytesRead / m_recordSize;

    if (m_dataBytesRemaining != -1) {
        m_dataBytesRemaining -= (recordsRead * m_recordSize);
    }

    return recordsRead;
}

void CbfReader::onProduceStop() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

} // namespace cyc
