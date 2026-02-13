// TcpServer.cpp
// SPDX-License-Identifier: MIT

#include "TcpServer.h"
#include "MessageUtils.h"
#include "TcpDefs.h"
#include <thread>
#include <vector>

namespace cyc {

TcpServer::TcpServer(asio::io_context& io_context, uint16_t port)
    : m_acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
{
}

void TcpServer::registerBuffer(const std::string& name, std::shared_ptr<RecBuffer> buffer) {
    std::unique_lock<std::shared_mutex> lock(m_buffersMtx);
    m_buffers[name] = buffer;
}

void TcpServer::start() {
    doAccept();
}

void TcpServer::doAccept() {
    m_acceptor.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec) {
                std::thread(&TcpServer::handleClient, this, std::move(socket)).detach();
            }
            doAccept();
        });
}

void TcpServer::handleClient(asio::ip::tcp::socket socket) {
    TcpHeader header;
    std::vector<uint8_t> payload;
    asio::error_code ec;

    if (!MessageUtils::receiveMessage(socket, header, payload, ec)) {
        return;
    }

    switch (header.type) {
        case MessageType::RequestBufferList: {
            std::string listStr;
            {
                std::shared_lock<std::shared_mutex> lock(m_buffersMtx);
                for (const auto& pair : m_buffers) {
                    listStr += pair.first + "\n";
                }
            }
            MessageUtils::sendMessage(socket, MessageType::ResponseBufferList, listStr, ec);
            break;
        }

        case MessageType::RequestRecRule: {
            std::string bufferName(payload.begin(), payload.end());
            std::shared_ptr<RecBuffer> targetBuffer;
            {
                std::shared_lock<std::shared_mutex> lock(m_buffersMtx);
                auto it = m_buffers.find(bufferName);
                if (it != m_buffers.end()) {
                    targetBuffer = it->second;
                }
            }

            if (targetBuffer) {
                std::string ruleText = targetBuffer->getRule().toText();
                MessageUtils::sendMessage(socket, MessageType::ResponseRecRule, ruleText, ec);
            } else {
                MessageUtils::sendMessage(socket, MessageType::ResponseError, "Buffer not found", ec);
            }
            break;
        }

        case MessageType::RequestDataStream: {
            std::string bufferName(payload.begin(), payload.end());
            std::shared_ptr<RecBuffer> targetBuffer;
            {
                std::shared_lock<std::shared_mutex> lock(m_buffersMtx);
                auto it = m_buffers.find(bufferName);
                if (it != m_buffers.end()) {
                    targetBuffer = it->second;
                }
            }

            if (targetBuffer) {
                // TODO: Step 3 - Create DataSender and transfer socket ownership
                MessageUtils::sendMessage(socket, MessageType::ResponseError, "DataSender not implemented", ec);
            } else {
                MessageUtils::sendMessage(socket, MessageType::ResponseError, "Buffer not found", ec);
            }
            break;
        }

        default: {
            MessageUtils::sendMessage(socket, MessageType::ResponseError, "Unknown request type", ec);
            break;
        }
    }
}

} // namespace cyc
