// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CbfReader.h"
#include <iostream>
#include <cstring>

namespace cyc {

CbfReader::CbfReader(const std::string& filename,
                     size_t bufferCapacity,
                     bool autoStart,
                     size_t readBatchSize,
                     size_t writerBatchSize)
    : RecordProducer(bufferCapacity, writerBatchSize)
    , m_filename(filename)
    , m_readBatchSize(readBatchSize)
    , m_recordSize(0)
    , m_valid(false)
    , m_dataBytesRemaining(0)
    , m_bufferedRecordsCount(0)
    , m_currentRecordIdx(0)
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

    // Выделяем память под буфер чтения
    m_readBuffer.resize(m_recordSize * m_readBatchSize);
    m_bufferedRecordsCount = 0;
    m_currentRecordIdx = 0;

    // 5. Ищем секцию данных
    while (m_file.good()) {
        if (!readSectionHeader(secHeader)) break;

        if (secHeader.type == static_cast<uint8_t>(CbfSectionType::Data)) {
            m_dataBytesRemaining = secHeader.bodyLength;
            m_valid = true; // УСПЕХ: Файл валиден и готов к чтению
            return rule;
        } else {
            m_file.seekg(secHeader.bodyLength, std::ios::cur);
        }
    }

    std::cerr << "CbfReader warning: No data section found in " << m_filename << std::endl;
    m_file.close();
    return rule; // Возвращаем правило, но m_valid остался false
}

bool CbfReader::readSectionHeader(CbfSectionHeader& header) {
    m_file.read(reinterpret_cast<char*>(&header), sizeof(CbfSectionHeader));
    if (m_file.gcount() != sizeof(CbfSectionHeader)) return false;
    if (header.marker != CBF_SECTION_MARKER) return false;
    return true;
}

bool CbfReader::refillBuffer() {
    if (!m_valid || !m_file.good()) return false;
    if (m_dataBytesRemaining == 0) return false;

    size_t bytesToRead = m_readBatchSize * m_recordSize;
    if (m_dataBytesRemaining != -1) {
        if (static_cast<int64_t>(bytesToRead) > m_dataBytesRemaining) {
            bytesToRead = static_cast<size_t>(m_dataBytesRemaining);
        }
    }

    m_file.read(m_readBuffer.data(), bytesToRead);
    size_t bytesRead = m_file.gcount();

    if (bytesRead == 0) return false;

    m_bufferedRecordsCount = bytesRead / m_recordSize;
    m_currentRecordIdx = 0;

    if (m_dataBytesRemaining != -1) {
        m_dataBytesRemaining -= (m_bufferedRecordsCount * m_recordSize);
    }

    return m_bufferedRecordsCount > 0;
}

bool CbfReader::produceStep(Record& rec) {
    // Если файл не валиден (не найден или ошибка формата), сразу возвращаем false.
    // Это заставит RecordProducer остановить поток.
    if (!m_valid) return false;

    if (m_currentRecordIdx >= m_bufferedRecordsCount) {
        if (!refillBuffer()) {
            return false; // EOF
        }
    }

    const char* srcPtr = m_readBuffer.data() + (m_currentRecordIdx * m_recordSize);
    std::memcpy(rec.data(), srcPtr, m_recordSize);

    m_currentRecordIdx++;
    return true;
}

void CbfReader::onProduceStop() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

} // namespace cyc
