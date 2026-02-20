// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "TcpDataSender.h"
#include "TcpDefs.h"
#include <iostream>

namespace cyc {

TcpDataSender::TcpDataSender(std::shared_ptr<RecBuffer> buffer, asio::ip::tcp::socket socket)
    : RecordConsumer(buffer)
    , m_socket(std::move(socket))
{
}

TcpDataSender::~TcpDataSender() {
    // CRITICAL FIX: Break connection before joining the thread
    asio::error_code ec;
    if (m_socket.is_open()) {
        m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close(ec);
    }
    stop();
}

void TcpDataSender::workerLoop() {
    onConsumeStart();
    asio::error_code ec;

    while (isRunning() && m_socket.is_open()) {
        TcpHeader reqHeader;
        asio::read(m_socket, asio::buffer(&reqHeader, sizeof(TcpHeader)), ec);

        if (ec) {
            if (ec != asio::error::eof && m_socket.is_open() && isRunning()) {
                std::cerr << "TcpDataSender: Read request error: " << ec.message() << "\n";
            }
            break;
        }

        if (reqHeader.signature != 0x43594300 || reqHeader.type != MessageType::RequestDataBatch) {
            std::cerr << "TcpDataSender: Invalid header or type\n";
            break;
        }

        uint32_t maxBytesRequested = reqHeader.payloadSize;
        size_t recordSize = m_reader->getRule().getRecSize();
        size_t maxRecordsToRead = (recordSize > 0) ? (maxBytesRequested / recordSize) : 0;

        // Send keep-alive if capacity is 0
        if (maxRecordsToRead == 0) {
            TcpHeader resp;
            resp.type = MessageType::ResponseDataBatch;
            resp.payloadSize = 0;
            asio::write(m_socket, asio::buffer(&resp, sizeof(TcpHeader)), ec);
            continue;
        }

        // Fetch data without blocking to keep the network responsive
        auto batch = m_reader->nextBatch(maxRecordsToRead, false);
        size_t bytesToSend = batch.count * batch.recordSize;

        TcpHeader respHeader;
        respHeader.type = MessageType::ResponseDataBatch;
        respHeader.payloadSize = static_cast<uint32_t>(bytesToSend);

        // Scatter-gather output to avoid extra allocations
        std::vector<asio::const_buffer> buffers;
        buffers.push_back(asio::buffer(&respHeader, sizeof(TcpHeader)));

        if (bytesToSend > 0) {
            buffers.push_back(asio::buffer(batch.data, bytesToSend));
        }

        asio::write(m_socket, buffers, ec);

        if (ec) {
            if (m_socket.is_open() && isRunning()) {
                std::cerr << "TcpDataSender: Write response error: " << ec.message() << "\n";
            }
            break;
        }
    }

    onConsumeStop();
}

} // namespace cyc
