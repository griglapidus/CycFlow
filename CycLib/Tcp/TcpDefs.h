// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPDEFS_H
#define CYC_TCPDEFS_H

#include "Core/CycLib_global.h"
#include <cstdint>

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

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_TCPDEFS_H
