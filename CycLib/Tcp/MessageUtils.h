// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_MESSAGEUTILS_H
#define CYC_MESSAGEUTILS_H

#include "Core/CycLib_global.h"
#include "TcpDefs.h"
#include <asio.hpp>

namespace cyc {

/**
 * @class MessageUtils
 * @brief Utility class for standardizing network message serialization.
 */
class CYCLIB_EXPORT MessageUtils {
public:
    /**
     * @brief Sends a standard TCP message with an optional string payload.
     * Uses scatter-gather I/O to avoid unnecessary memory allocations.
     * @param socket Connected TCP socket.
     * @param type Message type.
     * @param payload Payload string (can be empty).
     * @param ec Error code populated on failure.
     * @return True if sent successfully.
     */
    static bool sendMessage(asio::ip::tcp::socket& socket, MessageType type, const std::string& payload, asio::error_code& ec) {
        TcpHeader header;
        header.type = type;
        header.payloadSize = static_cast<uint32_t>(payload.size());

        std::vector<asio::const_buffer> buffers;
        buffers.push_back(asio::buffer(&header, sizeof(TcpHeader)));

        if (header.payloadSize > 0) {
            buffers.push_back(asio::buffer(payload));
        }

        asio::write(socket, buffers, ec);
        return !ec;
    }

    /**
     * @brief Receives a standard TCP message and reads its string payload.
     * @param socket Connected TCP socket.
     * @param header Header structure populated by the read.
     * @param payload Vector populated with incoming bytes.
     * @param ec Error code populated on failure.
     * @return True if received successfully and signature matches.
     */
    static bool receiveMessage(asio::ip::tcp::socket& socket, TcpHeader& header, std::vector<uint8_t>& payload, asio::error_code& ec) {
        asio::read(socket, asio::buffer(&header, sizeof(TcpHeader)), ec);
        if (ec) return false;

        if (header.signature != 0x43594300) {
            ec = asio::error::make_error_code(asio::error::operation_aborted);
            return false;
        }

        if (header.payloadSize > 0) {
            payload.resize(header.payloadSize);
            asio::read(socket, asio::buffer(payload), ec);
            if (ec) return false;
        } else {
            payload.clear();
        }

        return true;
    }
};

} // namespace cyc

#endif // CYC_MESSAGEUTILS_H
