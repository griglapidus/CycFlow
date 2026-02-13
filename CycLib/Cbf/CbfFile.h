// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFFILE_H
#define CYC_CBFFILE_H

#include "Core/CycLib_global.h"
#include "Core/RecRule.h"
#include "Core/Record.h"
#include "CbfDefs.h"

#include <string>
#include <fstream>

namespace cyc {

enum class CbfMode {
    Read,
    Write
};

class CYCLIB_EXPORT CbfFile {
public:
    CbfFile();
    ~CbfFile();

    bool open(const std::string& filename, CbfMode mode);
    void close();

    bool isOpen() const;
    bool isGood() const;

    // --- API для записи (Write) ---
    /**
     * @brief Устанавливает псевдоним (alias) буфера.
     * Это имя будет записываться в поле header.name каждого заголовка секции.
     * @note Максимальная длина — 10 символов (обрезается, если длиннее).
     */
    void setAlias(const std::string& alias);

    /**
     * @brief Записывает секцию заголовка со схемой данных.
     */
    bool writeHeader(const RecRule& rule);

    /**
     * @brief Начинает секцию данных.
     * Записывает заголовок секции с нулевой длиной (обновляется в endDataSection).
     */
    bool beginDataSection();

    /**
     * @brief Записывает одну запись.
     * @note Требует наличия методов getSize() и data() у Record.
     */
    bool writeRecord(const Record& rec);

    /**
     * @brief Записывает сырые байты (альтернатива writeRecord).
     */
    bool writeBytes(const void* data, size_t size);

    /**
     * @brief Завершает секцию данных, обновляя поле длины в заголовке файла.
     */
    bool endDataSection();

    // --- API для чтения (Read) ---

    /**
     * @brief Читает заголовок следующей секции.
     */
    bool readSectionHeader(CbfSectionHeader& header);

    /**
     * @brief Читает тело секции Header и парсит RecRule.
     */
    bool readRule(const CbfSectionHeader& header, RecRule& outRule);

    /**
     * @brief Читает одну запись.
     * Record должен быть инициализирован и указывать на выделенную память.
     */
    bool readRecord(Record& rec);

    /**
     * @brief Пропускает тело текущей секции.
     */
    bool skipSection(const CbfSectionHeader& header);

private:
    std::string m_filename;
    std::fstream m_file;
    CbfMode m_mode;
    std::string m_alias;

    std::streampos m_currentSectionStart; // Позиция начала текущей секции (для перезаписи длины)
    int64_t m_writtenBytesInSection;      // Счетчик байт в текущей секции
};

} // namespace cyc

#endif // CYC_CBFFILE_H
