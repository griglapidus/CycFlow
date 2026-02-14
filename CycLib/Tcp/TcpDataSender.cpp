// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "TcpDataSender.h"
#include <iostream>

namespace cyc {

TcpDataSender::TcpDataSender(std::shared_ptr<RecBuffer> buffer, asio::ip::tcp::socket socket, size_t batchSize)
    : BatchRecordConsumer(buffer, batchSize) // Pass batch size to base reader
    , m_socket(std::move(socket))
{
    // No internal buffer allocation needed anymore.
}

TcpDataSender::~TcpDataSender() {
    // Stop the thread first to ensure no write operations are attempted on a closed socket
    stop();

    asio::error_code ec;
    if (m_socket.is_open()) {
        m_socket.close(ec);
    }
}

void TcpDataSender::consumeBatch(const RecordReader::RecordBatch& batch) {
    if (!batch.isValid() || batch.count == 0) return;

    asio::error_code ec;

    // 1. Calculate payload size
    size_t payloadSize = batch.count * batch.recordSize;

    // 2. Prepare Header
    TcpHeader header;
    header.type = MessageType::DataStreamPayload;
    header.payloadSize = static_cast<uint32_t>(payloadSize);

    // 3. Prepare Scatter-Gather buffers
    // We send Header + Data in one go.
    std::vector<asio::const_buffer> buffers;
    buffers.push_back(asio::buffer(&header, sizeof(TcpHeader)));
    buffers.push_back(asio::buffer(batch.data, payloadSize));

    // 4. Write to socket
    asio::write(m_socket, buffers, ec);

    if (ec) {
        // If write fails (e.g. client disconnected), stop the consumer loop
        std::cerr << "TcpDataSender: Write error: " << ec.message() << std::endl;
        stop();
    }
}

} // namespace cyc
