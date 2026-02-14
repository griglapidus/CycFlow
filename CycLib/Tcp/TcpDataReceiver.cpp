// TcpDataReceiver.cpp
// SPDX-License-Identifier: MIT

#include "TcpDataReceiver.h"
#include "MessageUtils.h"
#include "TcpDefs.h"
#include <iostream>

namespace cyc {

TcpDataReceiver::TcpDataReceiver(size_t bufferCapacity)
    : m_socket(m_ioContext)
    , m_capacity(bufferCapacity)
    , m_running(false)
{
}

TcpDataReceiver::~TcpDataReceiver() {
    stop();
}

bool TcpDataReceiver::connect(const std::string& host, uint16_t port, const std::string& bufferName) {
    if (m_running) return false;

    asio::error_code ec;

    // 1. Resolve and Connect
    asio::ip::tcp::resolver resolver(m_ioContext);
    auto endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec) {
        std::cerr << "TcpDataReceiver: Resolve failed: " << ec.message() << std::endl;
        return false;
    }

    asio::connect(m_socket, endpoints, ec);
    if (ec) {
        std::cerr << "TcpDataReceiver: Connect failed: " << ec.message() << std::endl;
        return false;
    }

    // 2. Handshake: Request the buffer
    MessageUtils::sendMessage(m_socket, MessageType::RequestDataStream, bufferName, ec);


    // 3. Handshake: Wait for RecRule (Synchronous wait)
    TcpHeader header;
    std::vector<uint8_t> payload;

    if (!MessageUtils::receiveMessage(m_socket, header, payload, ec)) {
        std::cerr << "TcpDataReceiver: Failed to receive handshake response: " << ec.message() << std::endl;
        m_socket.close(ec);
        return false;
    }

    if (header.type == MessageType::ResponseError) {
        // Server rejected request (e.g. buffer not found)
        std::string err(payload.begin(), payload.end());
        std::cerr << "TcpDataReceiver: Server rejected request: " << err << std::endl;
        m_socket.close(ec);
        return false;
    }

    if (header.type != MessageType::ResponseRecRule) {
        std::cerr << "TcpDataReceiver: Unexpected handshake response type: " << (int)header.type << std::endl;
        m_socket.close(ec);
        return false;
    }

    // 4. Initialize Buffer
    try {
        std::string ruleText(payload.begin(), payload.end());
        RecRule rule = RecRule::fromText(ruleText);
        m_buffer = std::make_shared<RecBuffer>(rule, m_capacity);
    } catch (const std::exception& e) {
        std::cerr << "TcpDataReceiver: Failed to parse rule: " << e.what() << std::endl;
        m_socket.close(ec);
        return false;
    }

    // 5. Start Background Thread for Data
    m_running = true;
    m_worker = std::thread(&TcpDataReceiver::receiveLoop, this);

    return true;
}

void TcpDataReceiver::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;

    asio::error_code ec;
    m_socket.close(ec); // Force blocking read to exit

    if (m_worker.joinable()) {
        m_worker.join();
    }
}

std::shared_ptr<RecBuffer> TcpDataReceiver::getBuffer() const {
    return m_buffer;
}

bool TcpDataReceiver::isConnected() const {
    return m_running;
}

void TcpDataReceiver::setOnFinishedCallback(std::function<void()> cb) {
    m_onFinished = cb;
}

void TcpDataReceiver::receiveLoop() {
    asio::error_code ec;
    TcpHeader header;
    std::vector<uint8_t> payload;

    // Loop handles DataStreamPayload (or late errors)
    while (m_running) {
        if (!MessageUtils::receiveMessage(m_socket, header, payload, ec)) {
            // Connection lost
            break;
        }

        if (header.type == MessageType::DataStreamPayload) {
            if (!m_buffer) continue; // Should not happen after connect() logic

            size_t recSize = m_buffer->getRecSize();
            size_t bytes = payload.size();

            // Minimal validation
            if (bytes == 0 || (bytes % recSize != 0)) {
                continue;
            }

            size_t count = bytes / recSize;
            m_buffer->push(payload.data(), count);

        } else if (header.type == MessageType::ResponseError) {
            std::string err(payload.begin(), payload.end());
            std::cerr << "TcpDataReceiver: Server sent error: " << err << std::endl;
            m_running = false;
        }
        // Ignore other types (like duplicate Rules)
    }

    m_running = false;
    if (m_onFinished) {
        m_onFinished();
    }
}

} // namespace cyc
