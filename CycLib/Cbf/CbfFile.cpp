// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CbfFile.h"
#include <cstring>

namespace cyc {

CbfFile::CbfFile()
    : m_mode(CbfMode::Read)
    , m_currentSectionStart(0)
    , m_writtenBytesInSection(0)
{
}

CbfFile::~CbfFile() {
    close();
}

bool CbfFile::open(const std::string& filename, CbfMode mode) {
    if (m_file.is_open()) {
        close();
    }

    m_filename = filename;
    m_mode = mode;

    std::ios_base::openmode openMode = std::ios::binary;
    if (mode == CbfMode::Read) {
        openMode |= std::ios::in;
    } else {
        openMode |= std::ios::out | std::ios::trunc;
    }

    m_file.open(filename, openMode);
    return m_file.is_open();
}

void CbfFile::close() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

bool CbfFile::isOpen() const {
    return m_file.is_open();
}

bool CbfFile::isGood() const {
    return m_file.good();
}

// --- Write Implementation ---

void CbfFile::setAlias(const std::string& alias) {
    m_alias = alias;
}

bool CbfFile::writeHeader(const RecRule& rule) {
    if (m_mode != CbfMode::Write || !m_file.is_open()) return false;

    std::string ruleText = rule.toText();

    CbfSectionHeader header;
    header.type = static_cast<uint8_t>(CbfSectionType::Header);
    header.bodyLength = ruleText.size();
    std::memset(header.name, 0, sizeof(header.name));
    std::strncpy(header.name, m_alias.c_str(), sizeof(header.name) - 1);

    m_file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    m_file.write(ruleText.c_str(), ruleText.size());

    return m_file.good();
}

bool CbfFile::beginDataSection() {
    if (m_mode != CbfMode::Write || !m_file.is_open()) return false;

    m_currentSectionStart = m_file.tellp();
    m_writtenBytesInSection = 0;

    CbfSectionHeader header;
    header.type = static_cast<uint8_t>(CbfSectionType::Data);
    header.bodyLength = -1;
    std::memset(header.name, 0, sizeof(header.name));
    std::strncpy(header.name, m_alias.c_str(), sizeof(header.name) - 1);

    m_file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    return m_file.good();
}

bool CbfFile::writeRecord(const Record& rec) {
    if (!rec.isValid()) return false;
    // Используем методы, которые нужно добавить в Record
    return writeBytes(rec.data(), rec.getSize());
}

bool CbfFile::writeBytes(const void* data, size_t size) {
    if (m_mode != CbfMode::Write || !m_file.is_open()) return false;

    m_file.write(static_cast<const char*>(data), size);
    m_writtenBytesInSection += size;

    return m_file.good();
}

bool CbfFile::endDataSection() {
    if (m_mode != CbfMode::Write || !m_file.is_open()) return false;

    std::streampos endPos = m_file.tellp();

    // Смещение до поля bodyLength внутри CbfSectionHeader:
    // marker(4) + type(1) + name(11) = 16 байт
    std::streampos lengthPos = m_currentSectionStart + static_cast<std::streampos>(16);

    m_file.seekp(lengthPos);
    m_file.write(reinterpret_cast<const char*>(&m_writtenBytesInSection), sizeof(m_writtenBytesInSection));

    // Возвращаемся в конец файла
    m_file.seekp(endPos);

    return m_file.good();
}

// --- Read Implementation ---

bool CbfFile::readSectionHeader(CbfSectionHeader& header) {
    if (m_mode != CbfMode::Read || !m_file.is_open()) return false;

    m_file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (m_file.gcount() != sizeof(header)) return false;

    if (header.marker != CBF_SECTION_MARKER) {
        return false;
    }

    return true;
}

bool CbfFile::readRule(const CbfSectionHeader& header, RecRule& outRule) {
    if (header.bodyLength <= 0) return false;

    std::string schemaText;
    schemaText.resize(header.bodyLength);

    m_file.read(&schemaText[0], header.bodyLength);
    if (m_file.gcount() != header.bodyLength) return false;

    try {
        outRule = RecRule::fromText(schemaText);
        return true;
    } catch (...) {
        return false;
    }
}

bool CbfFile::readRecord(Record& rec) {
    if (m_mode != CbfMode::Read || !m_file.is_open()) return false;
    if (!rec.isValid()) return false;

    size_t size = rec.getSize();
    m_file.read(static_cast<char*>(rec.data()), size);

    return (m_file.gcount() == static_cast<std::streamsize>(size));
}

bool CbfFile::skipSection(const CbfSectionHeader& header) {
    if (m_mode != CbfMode::Read) return false;
    m_file.seekg(header.bodyLength, std::ios::cur);
    return m_file.good();
}

} // namespace cyc
