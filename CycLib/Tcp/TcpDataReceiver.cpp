// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "TcpDataReceiver.h"
#include "MessageUtils.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace cyc {

TcpDataReceiver::TcpDataReceiver(size_t bufferCapacity, size_t writerBatchSize)
    : RecordProducer(bufferCapacity, writerBatchSize)
    , m_socket(m_ioContext)
    , m_connected(false)
{
}

TcpDataReceiver::~TcpDataReceiver() {
    stop();
}

bool TcpDataReceiver::connect(const std::string& host, uint16_t port, const std::string& bufferName) {
    if (isRunning() || m_connected) return false;

    asio::error_code ec;
    asio::ip::tcp::resolver resolver(m_ioContext);
    auto endpoints = resolver.resolve(host, std::to_string(port), ec);

    if (ec) {
        std::cerr << "TcpDataReceiver: Resolve failed: " << ec.message() << "\n";
        return false;
    }

    asio::connect(m_socket, endpoints, ec);
    if (ec) {
        std::cerr << "TcpDataReceiver: Connect failed: " << ec.message() << "\n";
        return false;
    }

    // Handshake
    if (!MessageUtils::sendMessage(m_socket, MessageType::RequestDataStream, bufferName, ec)) {
        std::cerr << "TcpDataReceiver: Handshake send failed: " << ec.message() << "\n";
        m_socket.close(ec);
        return false;
    }

    TcpHeader header;
    std::vector<uint8_t> payload;

    if (!MessageUtils::receiveMessage(m_socket, header, payload, ec)) {
        std::cerr << "TcpDataReceiver: Handshake recv failed: " << ec.message() << "\n";
        m_socket.close(ec);
        return false;
    }

    if (header.type == MessageType::ResponseError) {
        std::string err(payload.begin(), payload.end());
        std::cerr << "TcpDataReceiver: Server rejected: " << err << "\n";
        m_socket.close(ec);
        return false;
    }

    if (header.type != MessageType::ResponseRecRule) {
        std::cerr << "TcpDataReceiver: Unexpected header type\n";
        m_socket.close(ec);
        return false;
    }

    try {
        std::string ruleText(payload.begin(), payload.end());
        m_negotiatedRule = RecRule::fromText(ruleText);
    } catch (const std::exception& e) {
        std::cerr << "TcpDataReceiver: Invalid rule: " << e.what() << "\n";
        m_socket.close(ec);
        return false;
    }

    m_connected = true;
    start();

    return true;
}

RecRule TcpDataReceiver::defineRule() {
    return m_negotiatedRule;
}

void TcpDataReceiver::stop() {
    m_connected = false;
    asio::error_code ec;

    // CRITICAL FIX: Close socket first to unblock asio::read in worker loop
    if (m_socket.is_open()) {
        m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close(ec);
    }

    // Now safe to join the thread
    RecordProducer::stop();
}

bool TcpDataReceiver::isConnected() const {
    return m_connected && isRunning();
}

void TcpDataReceiver::workerLoop() {
    onProduceStart();

    size_t recordSize = m_negotiatedRule.getRecSize();
    asio::error_code ec;

    while (isRunning() && m_socket.is_open()) {
        auto batch = m_writer->nextBatch(m_writerBatchSize);

        // Wait if target ring buffer is currently full
        if (!batch.isValid()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        uint32_t maxBytesToReceive = static_cast<uint32_t>(batch.capacity * recordSize);

        // 1. Request Data
        TcpHeader reqHeader;
        reqHeader.type = MessageType::RequestDataBatch;
        reqHeader.payloadSize = maxBytesToReceive;

        asio::write(m_socket, asio::buffer(&reqHeader, sizeof(TcpHeader)), ec);
        if (ec) break;

        // 2. Read Response Header
        TcpHeader respHeader;
        asio::read(m_socket, asio::buffer(&respHeader, sizeof(TcpHeader)), ec);

        if (ec) break;

        if (respHeader.signature != 0x43594300 || respHeader.type != MessageType::ResponseDataBatch) {
            std::cerr << "TcpDataReceiver: Invalid signature or type\n";
            break;
        }

        uint32_t incomingBytes = respHeader.payloadSize;

        // 3. Handle Keep-Alive (Empty response)
        if (incomingBytes == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (incomingBytes > maxBytesToReceive) {
            std::cerr << "TcpDataReceiver: Server sent too much data!\n";
            break;
        }

        // 4. Zero-Copy Read Payload
        asio::read(m_socket, asio::buffer(batch.data, incomingBytes), ec);
        if (ec) break;

        size_t recordsReceived = incomingBytes / recordSize;
        m_writer->commitBatch(recordsReceived);

        // Push immediately to minimize latency
        m_writer->flush();
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
