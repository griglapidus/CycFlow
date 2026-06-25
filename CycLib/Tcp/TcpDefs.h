// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPDEFS_H
#define CYC_TCPDEFS_H

#include "Core/CycLib_global.h"
#include <asio.hpp>
#include <chrono>
#include <cstdint>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/time.h>
#endif

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @brief TCP message types for the CycLib protocol.
 */
enum class MessageType : uint8_t {
    RequestBufferList  = 1,
    ResponseBufferList = 2,
    RequestRecRule     = 3,
    ResponseRecRule    = 4,
    RequestDataStream  = 5,
    RequestDataBatch   = 6,
    ResponseDataBatch  = 7,
    ResponseError      = 8
};

#pragma pack(push, 1)
/**
 * @struct TcpHeader
 * @brief Standard header for all CycLib TCP network packets.
 * Packed to exactly 9 bytes to ensure cross-platform compatibility.
 */
struct TcpHeader {
    uint32_t signature = 0x43594300; ///< Magic signature "CYC\0"
    MessageType type;                ///< Type of the message
    uint32_t payloadSize;            ///< Size of the following payload in bytes
};
#pragma pack(pop)

/// Maximum payload accepted by MessageUtils::receiveMessage() for control-plane
/// messages (buffer list, RecRule schema, request strings). Bounds the allocation
/// driven by the untrusted on-wire payloadSize field. Data-batch streaming uses its
/// own caller-supplied capacity check instead and is unaffected by this limit.
constexpr uint32_t kMaxControlPayloadSize = 16u * 1024u * 1024u;

/// Default timeout applied to socket connect/read/write operations across the
/// TCP stack. Bounds how long an unresponsive or stalled peer can keep a
/// thread and socket alive (control-plane handshakes as well as the
/// steady-state data request/response loops), mitigating both accidental
/// hangs and slow-loris-style connections that never send a complete message.
constexpr std::chrono::milliseconds kDefaultSocketTimeout{15000};

/// Applies kDefaultSocketTimeout (or @p timeout) as the OS-level receive/send
/// timeout for @p socket. Must be called once the socket is connected or
/// accepted; the setting persists for the socket's lifetime, so every later
/// synchronous asio::read/asio::write on it is automatically bounded without
/// any further per-call changes.
inline void applySocketTimeout(asio::ip::tcp::socket& socket,
                                std::chrono::milliseconds timeout = kDefaultSocketTimeout) {
#ifdef _WIN32
    DWORD timeoutMs = static_cast<DWORD>(timeout.count());
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    struct timeval tv;
    tv.tv_sec  = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/// Performs asio::connect() bounded by @p timeout, using @p ioContext's own
/// run_for() to enforce the deadline.
/// @p ioContext must be privately owned by the caller for the duration of this
/// call (e.g. a per-call or per-object io_context, not one shared/driven by
/// other connections), since run_for() executes whatever work is queued on it.
inline bool connectWithTimeout(asio::io_context& ioContext,
                                asio::ip::tcp::socket& socket,
                                const asio::ip::tcp::resolver::results_type& endpoints,
                                asio::error_code& ec,
                                std::chrono::milliseconds timeout = kDefaultSocketTimeout) {
    ec = asio::error::would_block;
    asio::async_connect(socket, endpoints,
        [&ec](const asio::error_code& e, const asio::ip::tcp::endpoint&) { ec = e; });

    ioContext.restart();
    ioContext.run_for(timeout);

    if (ec == asio::error::would_block) {
        // Timed out: cancel the pending connect and leave the socket closed so
        // the caller starts clean if it retries.
        asio::error_code ignored;
        socket.close(ignored);
        ec = asio::error::timed_out;
        return false;
    }
    return !ec;
}

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_TCPDEFS_H
