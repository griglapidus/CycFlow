// TcpServiceClient.cpp
// SPDX-License-Identifier: MIT

#include "TcpServiceClient.h"
#include "MessageUtils.h"
#include "TcpDefs.h"
#include <asio.hpp>
#include <sstream>

namespace cyc {

std::vector<std::string> TcpServiceClient::requestBufferList(const std::string& host, uint16_t port) {
    asio::io_context io_context;
    asio::ip::tcp::socket socket(io_context);
    asio::error_code ec;

    // Resolve the host address and connect
    asio::ip::tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec) return {};

    asio::connect(socket, endpoints, ec);
    if (ec) return {};

    // Send the request
    MessageUtils::sendMessage(socket, MessageType::RequestBufferList, "", ec);
    if (ec) return {};

    // Receive the response
    TcpHeader header;
    std::vector<uint8_t> payload;
    if (!MessageUtils::receiveMessage(socket, header, payload, ec)) {
        return {};
    }

    std::vector<std::string> result;
    if (header.type == MessageType::ResponseBufferList) {
        std::string listStr(payload.begin(), payload.end());
        std::stringstream ss(listStr);
        std::string item;

        // Split the string by newline
        while (std::getline(ss, item, '\n')) {
            if (!item.empty()) {
                result.push_back(item);
            }
        }
    }

    // Socket goes out of scope and disconnects automatically
    return result;
}

std::string TcpServiceClient::requestRecRule(const std::string& host, uint16_t port, const std::string& bufferName) {
    asio::io_context io_context;
    asio::ip::tcp::socket socket(io_context);
    asio::error_code ec;

    // Resolve the host address and connect
    asio::ip::tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec) return "";

    asio::connect(socket, endpoints, ec);
    if (ec) return "";

    // Send the request with the buffer name as payload
    MessageUtils::sendMessage(socket, MessageType::RequestRecRule, bufferName, ec);
    if (ec) return "";

    // Receive the response
    TcpHeader header;
    std::vector<uint8_t> payload;
    if (!MessageUtils::receiveMessage(socket, header, payload, ec)) {
        return "";
    }

    if (header.type == MessageType::ResponseRecRule) {
        return std::string(payload.begin(), payload.end());
    }

    // In case of MessageType::ResponseError or parsing failure
    return "";
}

} // namespace cyc
