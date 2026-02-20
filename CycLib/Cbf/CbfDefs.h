// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFDEFS_H
#define CYC_CBFDEFS_H

#include <cstdint>

namespace cyc {

/**
 * @brief Magic number to identify valid CBF sections. (0xA1B2C3D4)
 */
constexpr uint32_t CBF_SECTION_MARKER = 0xA1B2C3D4;

/**
 * @brief Enumeration of supported CBF section types.
 */
enum class CbfSectionType : uint8_t {
    Header = 0x01, ///< Contains schema definitions (RecRule).
    Data   = 0x02  ///< Contains raw binary records.
};

#pragma pack(push, 1)
/**
 * @struct CbfSectionHeader
 * @brief Represents the metadata at the beginning of every CBF section.
 * Pack directive ensures exactly 24 bytes size without padding.
 */
struct CbfSectionHeader {
    uint32_t marker;        ///< Magic marker for validation.
    uint8_t  type;          ///< Type of the section (Header or Data).
    char     name[11];      ///< Alias or name of the section (10 chars + \0).
    int64_t  bodyLength;    ///< Size of the section body in bytes.

    CbfSectionHeader() : marker(CBF_SECTION_MARKER), type(0), bodyLength(0) {
        for (int i = 0; i < 11; ++i) name[i] = 0;
    }
};
#pragma pack(pop)

} // namespace cyc

#endif // CYC_CBFDEFS_H
