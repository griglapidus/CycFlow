// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPSERVER_H
#define CYC_TCPSERVER_H

#include "Core/CycLib_global.h"
#include "TcpDataSender.h"
#include "Core/RecBuffer.h"
#include <asio.hpp>

namespace cyc {
CYCLIB_SUPPRESS_C4251

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
    void registerBuffer(const std::string& name, std::shared_ptr<RecBuffer> buffer, size_t batchSize);

    /**
     * @brief Unregisters a buffer and closes every connection serving it.
     *
     * Removes the buffer from the registry and synchronously destroys all
     * active TcpDataSender sessions tied to it, which shuts down their
     * sockets and joins their worker threads.
     *
     * @param name Buffer name passed to registerBuffer().
     */
    void unregisterBuffer(const std::string& name);

    void start();

private:
    void doAccept();
    void handleClient(asio::ip::tcp::socket socket);
    void cleanupDeadSenders();

private:
    asio::ip::tcp::acceptor m_acceptor;

    std::unordered_map<std::string, std::pair<std::shared_ptr<RecBuffer>, size_t>> m_buffers;
    std::shared_mutex m_buffersMtx;

    // Each entry pairs the sender with the buffer name it serves, so
    // unregisterBuffer() can close exactly the matching sessions.
    // Lock order: m_buffersMtx -> m_sendersMtx (never the reverse).
    std::vector<std::pair<std::string, std::shared_ptr<TcpDataSender>>> m_activeSenders;
    std::mutex m_sendersMtx;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_TCPSERVER_H
