// MessageUtils.h
// SPDX-License-Identifier: MIT

#ifndef CYC_MESSAGEUTILS_H
#define CYC_MESSAGEUTILS_H

#include "Core/CycLib_global.h"
#include <asio.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include "TcpDefs.h"

namespace cyc {

class CYCLIB_EXPORT MessageUtils {
public:
    static void sendMessage(asio::ip::tcp::socket& socket, MessageType type, const std::string& payload, asio::error_code& ec) {
        TcpHeader header;
        header.type = type;
        header.payloadSize = static_cast<uint32_t>(payload.size());

        std::vector<asio::const_buffer> buffers;
        buffers.push_back(asio::buffer(&header, sizeof(TcpHeader)));

        if (header.payloadSize > 0) {
            buffers.push_back(asio::buffer(payload));
        }

        asio::write(socket, buffers, ec);
    }

    static bool receiveMessage(asio::ip::tcp::socket& socket, TcpHeader& header, std::vector<uint8_t>& payload, asio::error_code& ec) {
        asio::read(socket, asio::buffer(&header, sizeof(TcpHeader)), ec);
        if (ec) {
            return false;
        }

        if (header.signature != 0x43594300) {
            ec = asio::error::make_error_code(asio::error::operation_aborted);
            return false;
        }

        if (header.payloadSize > 0) {
            payload.resize(header.payloadSize);
            asio::read(socket, asio::buffer(payload.data(), header.payloadSize), ec);
            if (ec) {
                return false;
            }
        } else {
            payload.clear();
        }

        return true;
    }
};

} // namespace cyc

#endif // CYC_MESSAGEUTILS_H
