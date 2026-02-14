// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFWRITER_H
#define CYC_CBFWRITER_H

#include "RecordConsumer.h" // Здесь теперь определен BatchRecordConsumer
#include "CbfFile.h"
#include <string>

namespace cyc {

/**
 * @brief Асинхронный писатель данных в формате CBF.
 * Использует пакетный режим (BatchRecordConsumer) для эффективной записи
 * больших блоков данных на диск.
 */
class CYCLIB_EXPORT CbfWriter : public BatchRecordConsumer {
public:
    /**
     * @param filename Путь к выходному файлу.
     * @param buffer Источник данных.
     * @param autoStart Запустить поток записи сразу после создания.
     * @param batchSize Размер внутреннего буфера чтения (в записях).
     */
    CbfWriter(const std::string& filename,
              std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true,
              size_t batchSize = 1000);

    ~CbfWriter() override;

    /**
     * @brief Устанавливает псевдоним, который будет записан в заголовки секций файла.
     * Должен быть вызван до начала записи (start).
     */
    void setAlias(const std::string& alias);

protected:
    // --- Методы жизненного цикла RecordConsumer ---

    /**
     * @brief Вызывается в рабочем потоке перед началом цикла.
     * Открывает файл, пишет схему и начинает секцию данных.
     */
    void onConsumeStart() override;

    /**
     * @brief Обрабатывает пакет записей.
     * Пишет весь блок памяти в файл за одну операцию.
     */
    void consumeBatch(const RecordReader::RecordBatch& batch) override;

    /**
     * @brief Вызывается после остановки цикла (или при ошибке).
     * Завершает секцию данных и закрывает файл.
     */
    void onConsumeStop() override;

private:
    std::string m_filename;
    std::string m_alias;
    CbfFile m_cbfFile;
};

} // namespace cyc

#endif // CYC_CBFWRITER_H
