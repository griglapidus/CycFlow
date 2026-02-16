// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDPRODUCER_H
#define CYC_RECORDPRODUCER_H

#include "Core/CycLib_global.h"
#include "Core/RecBuffer.h"
#include "RecordWriter.h"
#include "Core/RecRule.h"
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>

namespace cyc {

/**
 * @brief Абстрактный базовый класс для генерации данных.
 *
 * Сам создает RecBuffer на основе правила, которое возвращает наследник через defineRule().
 * Использует отложенную инициализацию (буфер создается при первом использовании).
 */
class CYCLIB_EXPORT RecordProducer {
public:
    /**
     * @brief Конструктор.
     * @note RecBuffer НЕ создается здесь, так как нельзя вызывать виртуальные функции в конструкторе.
     * @param bufferCapacity Размер создаваемого буфера (количество записей).
     * @param writerBatchSize Размер внутреннего батча RecordWriter'а.
     */
    RecordProducer(size_t bufferCapacity = 10000,
                   size_t writerBatchSize = 100);

    virtual ~RecordProducer();

    /**
     * @brief Запускает рабочий поток генерации.
     * Автоматически вызывает initialize(), если это не было сделано ранее.
     */
    void start();

    /**
     * @brief Останавливает генерацию и сбрасывает остатки данных в буфер.
     */
    void stop();

    /**
     * @brief Ожидает завершения потока.
     */
    void join();

    bool isRunning() const;

    /**
     * @brief Возвращает указатель на буфер.
     * Если буфер еще не создан, вызывает initialize().
     */
    std::shared_ptr<RecBuffer> getBuffer();

protected:
    /**
     * @brief Чисто виртуальная функция, определяющая схему данных.
     * Наследник ДОЛЖЕН реализовать этот метод. Он будет вызван один раз при инициализации.
     */
    virtual RecRule defineRule() = 0;

    /**
     * @brief Вызывается в потоке перед началом цикла.
     */
    virtual void onProduceStart() {}

    /**
     * @brief Заполняет одну запись данными.
     * @param rec Ссылка на запись, память которой нужно заполнить.
     * @return true, если запись успешно заполнена и готова к фиксации.
     * @return false, если данных больше нет (EOF) или произошла ошибка.
     */
    virtual bool produceStep(Record& rec) = 0;

    /**
     * @brief Вызывается в потоке после остановки цикла.
     */
    virtual void onProduceStop() {}

    /**
     * @brief Предоставляет доступ к писателю данных.
     * Гарантирует инициализацию.
     */
    RecordWriter& getWriter();

    virtual void workerLoop();

private:
    /**
     * @brief Создает RecBuffer и RecordWriter, вызывая defineRule().
     */
    void initialize();

protected:
    // Config
    size_t m_bufferCapacity;
    size_t m_writerBatchSize;

    // State
    std::shared_ptr<RecBuffer> m_buffer;
    std::unique_ptr<RecordWriter> m_writer;

    std::atomic_bool m_running;
    std::thread m_worker;

    std::mutex m_initMtx; // Защита для lazy initialization
    bool m_isInitialized;
};

class CYCLIB_EXPORT BatchRecordProducer : public RecordProducer {
public:
    using RecordProducer::RecordProducer; // Наследуем конструктор

protected:
    /**
     * @brief Заглушка для поштучного метода.
     * Не должен вызываться в пакетном режиме.
     */
    bool produceStep(Record& rec) override final { return false; }

    /**
     * @brief Метод для генерации пакета данных.
     * @param batch Структура, содержащая указатель на память и доступную емкость.
     * @return Количество фактически записанных записей.
     * Если вернуть 0, генерация остановится.
     */
    virtual size_t produceBatch(const RecordWriter::RecordBatch& batch) = 0;

    /**
     * @brief Переопределенный цикл для пакетной работы.
     */
    void workerLoop() override;
};

} // namespace cyc

#endif // CYC_RECORDPRODUCER_H
