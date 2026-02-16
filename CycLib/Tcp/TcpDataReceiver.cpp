// TcpDataReceiver.cpp
// SPDX-License-Identifier: MIT

#include "TcpDataReceiver.h"
#include "MessageUtils.h"
#include <iostream>
#include <thread> // Для sleep_for
#include <chrono> // Для milliseconds

namespace cyc {

TcpDataReceiver::TcpDataReceiver(size_t bufferCapacity, size_t writerBatchSize)
    : RecordProducer(bufferCapacity)
    , m_socket(m_ioContext)
    , m_connected(false)
    , m_writerBatchSize(writerBatchSize)
{
}

TcpDataReceiver::~TcpDataReceiver() {
    stop();
}

bool TcpDataReceiver::connect(const std::string& host, uint16_t port, const std::string& bufferName) {
    if (isRunning() || m_connected) return false;

    asio::error_code ec;

    // 1. Resolve
    asio::ip::tcp::resolver resolver(m_ioContext);
    auto endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec) {
        std::cerr << "TcpDataReceiver: Resolve failed: " << ec.message() << std::endl;
        return false;
    }

    // 2. Connect
    asio::connect(m_socket, endpoints, ec);
    if (ec) {
        std::cerr << "TcpDataReceiver: Connect failed: " << ec.message() << std::endl;
        return false;
    }

    // 3. Handshake Request
    if (!MessageUtils::sendMessage(m_socket, MessageType::RequestDataStream, bufferName, ec)) {
        std::cerr << "TcpDataReceiver: Handshake send failed: " << ec.message() << std::endl;
        m_socket.close(ec);
        return false;
    }

    // 4. Handshake Response
    TcpHeader header;
    std::vector<uint8_t> payload;

    if (!MessageUtils::receiveMessage(m_socket, header, payload, ec)) {
        std::cerr << "TcpDataReceiver: Handshake recv failed: " << ec.message() << std::endl;
        m_socket.close(ec);
        return false;
    }

    if (header.type == MessageType::ResponseError) {
        std::string err(payload.begin(), payload.end());
        std::cerr << "TcpDataReceiver: Server rejected: " << err << std::endl;
        m_socket.close(ec);
        return false;
    }

    if (header.type != MessageType::ResponseRecRule) {
        std::cerr << "TcpDataReceiver: Unexpected header type: " << (int)header.type << std::endl;
        m_socket.close(ec);
        return false;
    }

    // 5. Parse Rule
    try {
        std::string ruleText(payload.begin(), payload.end());
        m_negotiatedRule = RecRule::fromText(ruleText);
    } catch (const std::exception& e) {
        std::cerr << "TcpDataReceiver: Invalid rule: " << e.what() << std::endl;
        m_socket.close(ec);
        return false;
    }

    m_connected = true;
    start(); // Запуск RecordProducer

    return true;
}

RecRule TcpDataReceiver::defineRule() {
    return m_negotiatedRule;
}

void TcpDataReceiver::stop() {
    asio::error_code ec;

    // Сначала останавливаем логический цикл
    RecordProducer::stop();

    // Затем закрываем сокет, чтобы прервать блокирующие вызовы (read/write)
    // Это вызовет ошибку operation_aborted или WSACancelBlockingCall в потоке, это нормально.
    if (m_socket.is_open()) {
        m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close(ec);
    }
    m_connected = false;
}

bool TcpDataReceiver::isConnected() const {
    return m_connected && isRunning();
}

void TcpDataReceiver::workerLoop() {
    onProduceStart();

    size_t recordSize = m_negotiatedRule.getRecSize();
    asio::error_code ec;

    while (isRunning() && m_socket.is_open()) {
        auto batch = m_writer->nextBatch();

        // Если локальный буфер полон, ждем и пробуем снова
        if (!batch.isValid()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        uint32_t maxBytesToReceive = static_cast<uint32_t>(batch.capacity * recordSize);

        // --- 1. Отправка запроса ---
        TcpHeader reqHeader;
        reqHeader.type = MessageType::RequestDataBatch;
        reqHeader.payloadSize = maxBytesToReceive;

        asio::write(m_socket, asio::buffer(&reqHeader, sizeof(TcpHeader)), ec);
        if (ec) {
            if (isRunning()) std::cerr << "TcpDataReceiver: Write request failed: " << ec.message() << std::endl;
            break;
        }

        // --- 2. Чтение заголовка ответа ---
        TcpHeader respHeader;
        asio::read(m_socket, asio::buffer(&respHeader, sizeof(TcpHeader)), ec);

        if (ec) {
            // Если мы останавливаемся, ошибки отмены операций нормальны
            if (isRunning() && ec != asio::error::eof) {
                std::cerr << "TcpDataReceiver: Read response failed: " << ec.message() << std::endl;
            }
            break;
        }

        // Проверка сигнатуры и типа
        if (respHeader.signature != 0x43594300) {
            std::cerr << "TcpDataReceiver: Invalid signature received." << std::endl;
            break;
        }

        if (respHeader.type != MessageType::ResponseDataBatch) {
            std::cerr << "TcpDataReceiver: Unexpected response type: " << (int)respHeader.type << std::endl;
            break;
        }

        uint32_t incomingBytes = respHeader.payloadSize;

        // --- 3. Обработка пустого ответа (Keep-Alive) ---
        if (incomingBytes == 0) {
            // ВАЖНО: Если данных нет, делаем паузу, чтобы не спамить сервер запросами
            // и не создавать busy loop, который сложно прервать.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (incomingBytes > maxBytesToReceive) {
            std::cerr << "TcpDataReceiver: Server sent too much data!" << std::endl;
            break;
        }

        // --- 4. Чтение данных (Zero-Copy) ---
        asio::read(m_socket, asio::buffer(batch.data, incomingBytes), ec);
        if (ec) {
            if (isRunning()) std::cerr << "TcpDataReceiver: Read payload failed: " << ec.message() << std::endl;
            break;
        }

        // Фиксация полученных данных
        size_t recordsReceived = incomingBytes / recordSize;
        m_writer->commitBatch(recordsReceived);
    }

    onProduceStop();
}

void TcpDataReceiver::onProduceStop() {
    asio::error_code ec;
    if (m_socket.is_open()) {
        m_socket.close(ec);
    }
    m_connected = false;
}

} // namespace cyc
