// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPSERVER_H
#define CYC_TCPSERVER_H

#include "Core/CycLib_global.h"
#include "TcpDataSender.h"
#include "Core/RecBuffer.h"
#include <asio.hpp>

namespace cyc {

/**
 * @class TcpServer
 * @brief ASIO-based TCP server managing incoming client requests.
 *
 * Routes requests to specific RecBuffers and spawns TcpDataSender
 * sessions for continuous data streaming.
 */
class CYCLIB_EXPORT TcpServer {
public:
    TcpServer(asio::io_context& io_context, uint16_t port);

    /**
     * @brief Registers a buffer to be available for clients over the network.
     * @param name Unique name of the buffer.
     * @param buffer Shared pointer to the RecBuffer.
     */
    void registerBuffer(const std::string& name, std::shared_ptr<RecBuffer> buffer);
    void start();

private:
    void doAccept();
    void handleClient(asio::ip::tcp::socket socket);
    void cleanupDeadSenders();

private:
    asio::ip::tcp::acceptor m_acceptor;

    std::unordered_map<std::string, std::shared_ptr<RecBuffer>> m_buffers;
    std::shared_mutex m_buffersMtx;

    std::vector<std::shared_ptr<TcpDataSender>> m_activeSenders;
    std::mutex m_sendersMtx;
};

} // namespace cyc

#endif // CYC_TCPSERVER_H
