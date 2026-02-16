// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "TcpServer.h"
#include "MessageUtils.h"
#include "TcpDefs.h"
#include <thread>
#include <vector>
#include <iostream> // Добавлено для логов ошибок, если нужно

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
            // Проверка возврата, хотя после break мы все равно выходим
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
                if (m_buffers.count(bufferName)) {
                    targetBuffer = m_buffers[bufferName];
                }
            }

            if (targetBuffer) {
                std::string ruleText = targetBuffer->getRule().toText();

                // ИСПОЛЬЗУЕМ ВОЗВРАТ BOOL
                if (!MessageUtils::sendMessage(socket, MessageType::ResponseRecRule, ruleText, ec)) {
                    // Если не удалось отправить подтверждение, разрываем связь
                    return;
                }

                cleanupDeadSenders();

                // Создаем sender
                auto sender = std::make_shared<TcpDataSender>(
                    targetBuffer,
                    std::move(socket)
                );

                {
                    std::lock_guard<std::mutex> lock(m_sendersMtx);
                    m_activeSenders.push_back(sender);
                }

                sender->start();
                return;

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

void TcpServer::cleanupDeadSenders()
{
    std::lock_guard<std::mutex> lock(m_sendersMtx);
    auto it = std::remove_if(m_activeSenders.begin(), m_activeSenders.end(),
                             [](const std::shared_ptr<TcpDataSender>& sender) {
                                 return !sender->isRunning();
                             });

    m_activeSenders.erase(it, m_activeSenders.end());
}

} // namespace cyc
