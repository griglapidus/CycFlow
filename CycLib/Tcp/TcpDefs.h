// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPDEFS_H
#define CYC_TCPDEFS_H

#include <cstdint>

namespace cyc {

enum class MessageType : uint8_t {
    RequestBufferList = 1,
    ResponseBufferList = 2,
    RequestRecRule = 3,
    ResponseRecRule = 4,
    RequestDataStream = 5,
    DataStreamPayload = 6,
    ResponseError = 7
};

#pragma pack(push, 1)
struct TcpHeader {
    uint32_t signature = 0x43594300; // "CYC\0"
    MessageType type;
    uint32_t payloadSize;
};
#pragma pack(pop)

} // namespace cyc

#endif // CYC_TCPDEFS_H
