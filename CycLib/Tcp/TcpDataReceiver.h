// TcpDataReceiver.h
// SPDX-License-Identifier: MIT

#ifndef CYC_TCPDATARECEIVER_H
#define CYC_TCPDATARECEIVER_H

#include "Core/CycLib_global.h"
#include "RecordProducer.h" // Наследуемся от базового RecordProducer
#include "TcpDefs.h"
#include <asio.hpp>
#include <string>
#include <atomic>

namespace cyc {

/**
 * @brief Принимает данные по TCP в режиме Request-Response и пишет их в RecBuffer.
 */
class CYCLIB_EXPORT TcpDataReceiver : public RecordProducer {
public:
    /**
     * @param bufferCapacity Емкость создаваемого буфера.
     * @param writerBatchSize Размер пакета записи (максимальный размер транзакции).
     */
    TcpDataReceiver(size_t bufferCapacity = 65536, size_t writerBatchSize = 1000);
    ~TcpDataReceiver() override;

    /**
     * @brief Подключается к серверу, выполняет handshake и запускает поток.
     */
    bool connect(const std::string& host, uint16_t port, const std::string& bufferName);

    /**
     * @brief Останавливает прием и разрывает соединение.
     */
    void stop();

    bool isConnected() const;

protected:
    /**
     * @brief Возвращает правило, согласованное при connect().
     */
    RecRule defineRule() override;

    /**
     * @brief Основной цикл работы: Запрос -> Ожидание -> Чтение -> Commit.
     */
    void workerLoop() override;

    /**
     * @brief Хук перед запуском цикла (опционально).
     */
    void onProduceStart() override {}

    /**
     * @brief Хук после остановки цикла.
     */
    void onProduceStop() override;

    bool produceStep(Record& rec) override final { return false; }
private:
    asio::io_context m_ioContext;
    asio::ip::tcp::socket m_socket;

    RecRule m_negotiatedRule;
    bool m_connected;
    size_t m_writerBatchSize; // Храним желаемый размер батча
};

} // namespace cyc

#endif // CYC_TCPDATARECEIVER_H
