// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "TcpDataSender.h"
#include <iostream>

namespace cyc {

TcpDataSender::TcpDataSender(std::shared_ptr<RecBuffer> buffer, asio::ip::tcp::socket socket)
    : RecordConsumer(buffer)
    , m_socket(std::move(socket))
{
}

TcpDataSender::~TcpDataSender() {
    stop();
    asio::error_code ec;
    if (m_socket.is_open()) {
        m_socket.close(ec);
    }
}

void TcpDataSender::workerLoop() {
    onConsumeStart();

    asio::error_code ec;

    while (isRunning() && m_socket.is_open()) {

        TcpHeader reqHeader;
        asio::read(m_socket, asio::buffer(&reqHeader, sizeof(TcpHeader)), ec);

        if (ec) {
            if (ec != asio::error::eof && m_socket.is_open()) {
                std::cerr << "TcpDataSender: Read request error: " << ec.message() << std::endl;
            }
            break;
        }

        if (reqHeader.signature != 0x43594300 || reqHeader.type != MessageType::RequestDataBatch) {
            std::cerr << "TcpDataSender: Invalid header or type: " << (int)reqHeader.type << std::endl;
            break;
        }

        uint32_t maxBytesRequested = reqHeader.payloadSize;
        size_t recordSize = m_reader->getRule().getRecSize();
        size_t maxRecordsToRead = (recordSize > 0) ? (maxBytesRequested / recordSize) : 0;

        if (maxRecordsToRead == 0) {
            TcpHeader resp;
            resp.type = MessageType::ResponseDataBatch;
            resp.payloadSize = 0;
            asio::write(m_socket, asio::buffer(&resp, sizeof(TcpHeader)), ec);
            continue;
        }

        // ВАЖНО: wait = false
        // Если данных нет, batch.count будет 0, мы отправим пустой ответ,
        // и TcpDataReceiver уйдет в sleep.
        auto batch = m_reader->nextBatch(maxRecordsToRead, false);

        size_t bytesToSend = batch.count * batch.recordSize;

        TcpHeader respHeader;
        respHeader.type = MessageType::ResponseDataBatch;
        respHeader.payloadSize = static_cast<uint32_t>(bytesToSend);

        std::vector<asio::const_buffer> buffers;
        buffers.push_back(asio::buffer(&respHeader, sizeof(TcpHeader)));

        if (bytesToSend > 0) {
            buffers.push_back(asio::buffer(batch.data, bytesToSend));
        }

        asio::write(m_socket, buffers, ec);

        if (ec) {
            if (m_socket.is_open()) {
                std::cerr << "TcpDataSender: Write response error: " << ec.message() << std::endl;
            }
            break;
        }
    }

    onConsumeStop();
}

} // namespace cyc
