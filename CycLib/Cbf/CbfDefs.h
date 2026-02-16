// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFDEFS_H
#define CYC_CBFDEFS_H

#include <cstdint>

namespace cyc {

// Magic number: 0xA1B2C3D4
constexpr uint32_t CBF_SECTION_MARKER = 0xA1B2C3D4;

enum class CbfSectionType : uint8_t {
    Header = 0x01,
    Data   = 0x02
};

#pragma pack(push, 1)
struct CbfSectionHeader {
    uint32_t marker;        // 4 bytes
    uint8_t  type;          // 1 byte
    char     name[11];      // 11 bytes (10 chars + \0)
    int64_t  bodyLength;    // 8 bytes

    CbfSectionHeader() : marker(CBF_SECTION_MARKER), type(0), bodyLength(0) {
        for (int i = 0; i < 11; ++i) name[i] = 0;
    }
};
#pragma pack(pop)

} // namespace cyc

#endif // CYC_CBFDEFS_H
