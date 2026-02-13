// TcpServer.h
// SPDX-License-Identifier: MIT

#ifndef CYC_TCPSERVER_H
#define CYC_TCPSERVER_H

#include "Core/CycLib_global.h"
#include <asio.hpp>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <memory>

#include "Core/RecBuffer.h"

namespace cyc {

class CYCLIB_EXPORT TcpServer {
public:
    TcpServer(asio::io_context& io_context, uint16_t port);

    void registerBuffer(const std::string& name, std::shared_ptr<RecBuffer> buffer);
    void start();

private:
    void doAccept();
    void handleClient(asio::ip::tcp::socket socket);

private:
    asio::ip::tcp::acceptor m_acceptor;

    std::unordered_map<std::string, std::shared_ptr<RecBuffer>> m_buffers;
    std::shared_mutex m_buffersMtx;
};

} // namespace cyc

#endif // CYC_TCPSERVER_H
