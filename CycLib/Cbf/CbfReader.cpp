// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CbfReader.h"
#include <iostream>
#include <algorithm>

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
    m_cbfFile.close();
}

bool CbfReader::isValid() const {
    return m_valid;
}

RecRule CbfReader::defineRule() {
    m_valid = false;

    if (!m_cbfFile.open(m_filename, CbfMode::Read)) {
        std::cerr << "CbfReader warning: File not found or unreadable: " << m_filename << std::endl;
        return RecRule();
    }

    CbfSectionHeader header;
    RecRule rule;

    while (m_cbfFile.readSectionHeader(header)) {
        if (header.type == static_cast<uint8_t>(CbfSectionType::Header)) {
            if (m_cbfFile.readRule(header, rule)) {
                m_recordSize = rule.getRecSize();
                m_valid = (m_recordSize > 0);
            }
        } else if (header.type == static_cast<uint8_t>(CbfSectionType::Data)) {
            m_dataBytesRemaining = header.bodyLength;
            break; // Data section found, pause parsing and let produceBatch take over
        } else {
            m_cbfFile.skipSection(header);
        }
    }

    return rule;
}

void CbfReader::onProduceStop() {
    m_cbfFile.close();
}

size_t CbfReader::produceBatch(const RecordWriter::RecordBatch& batch) {
    if (!m_valid || !m_cbfFile.isGood() || m_dataBytesRemaining <= 0) {
        m_running.store(false, std::memory_order_release);
        return 0;
    }

    size_t countToRead = batch.capacity;

    size_t recordsRemaining = static_cast<size_t>(m_dataBytesRemaining) / m_recordSize;
    if (countToRead > recordsRemaining) {
        countToRead = recordsRemaining;
    }

    if (countToRead == 0) return 0;

    size_t bytesToRead = countToRead * m_recordSize;

    // Zero-copy read directly into the batch buffer
    std::fstream& fs = m_cbfFile.getStream();
    fs.read(reinterpret_cast<char*>(batch.data), bytesToRead);

    if (fs.gcount() != static_cast<std::streamsize>(bytesToRead)) {
        m_valid = false;
        return 0; // Unexpected EOF or read error
    }

    m_dataBytesRemaining -= bytesToRead;

    // If section ends, try to find the next Data section seamlessly
    if (m_dataBytesRemaining == 0) {
        CbfSectionHeader header;
        while (m_cbfFile.readSectionHeader(header)) {
            if (header.type == static_cast<uint8_t>(CbfSectionType::Data)) {
                m_dataBytesRemaining = header.bodyLength;
                break;
            } else {
                m_cbfFile.skipSection(header);
            }
        }
    }

    return countToRead;
}

} // namespace cyc
