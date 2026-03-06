// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_PATTR_H
#define CYC_PATTR_H

#include "CycLib_global.h"
#include "Common.h"
#include <vector>
#include <string>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @struct PAttr
 * @brief Describes a single attribute (field) within a record schema.
 *
 * Supports both plain typed fields and integer fields with named bit fields.
 * Bit fields are defined by a list of names and skip counts, stored in `bitIds`:
 *   - positive ID  → named bit (registered in PReg)
 *   - 0            → reserved/skipped bit
 *
 * Construction example:
 * @code
 * PAttr flags("StatusReg", DataType::dtUInt8, {"flag1", "flag2", "4", "flag3"});
 * //  bit 0 → "flag1"
 * //  bit 1 → "flag2"
 * //  bits 2-5 → skipped (numeric string "4" means skip 4 bits)
 * //  bit 6 → "flag3"
 * //  bit 7 → skipped (implicit)
 * @endcode
 */
struct CYCLIB_EXPORT PAttr {
    int      id;        ///< Unique ID of the attribute (from PReg).
    char     name[26];  ///< Name of the attribute (max 25 chars + null terminator).
    DataType type;      ///< Data type of the attribute.
    size_t   count;     ///< Number of elements (for arrays). Default is 1.
    size_t   offset;    ///< Byte offset within the record structure.

    /**
     * @brief Bit field map: bitIds[bitPosition] = PReg ID of the named bit,
     *        or 0 if the bit is reserved/skipped.
     *        Empty vector means this attribute has no bit fields defined.
     */
    std::vector<int> bitIds;

    /**
     * @brief Default constructor.
     */
    PAttr();

    /**
     * @brief Constructs a plain typed attribute and automatically assigns it a unique ID via PReg.
     * @param _name  Name of the attribute.
     * @param _type  Data type.
     * @param _count Number of elements (default 1).
     */
    PAttr(const char* _name, DataType _type, size_t _count = 1);

    /**
     * @brief Constructs an integer attribute with named bit fields.
     *
     * @param _name    Name of the containing integer field (registered in PReg as usual).
     * @param _type    An integer DataType (dtUInt8, dtInt8, dtUInt16, dtInt16, dtUInt32,
     *                 dtInt32, dtUInt64, dtInt64).
     * @param bitDefs  Ordered list describing each bit from LSB (bit 0) upward:
     *                 - Non-numeric string → registers a named bit via PReg.
     *                 - Numeric string     → skips that many bits (e.g. "4" skips bits N..N+3).
     *                 The total number of described bits must not exceed the bit width of @p _type.
     */
    PAttr(const char* _name, DataType _type, const std::vector<std::string>& bitDefs);

    /**
     * @brief Returns true if this attribute has named bit fields defined.
     */
    [[nodiscard]] bool hasBitFields() const { return !bitIds.empty(); }

    /**
     * @brief Calculates the total size of this attribute in bytes.
     * @return Size in bytes (type size * count).
     */
    [[nodiscard]] size_t getSize() const;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_PATTR_H
