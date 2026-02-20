// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_PATTR_H
#define CYC_PATTR_H

#include "CycLib_global.h"
#include "Common.h"

namespace cyc {

/**
 * @struct PAttr
 * @brief Describes a single attribute (field) within a record schema.
 */
struct CYCLIB_EXPORT PAttr {
    int id;             ///< Unique ID of the attribute (from PReg).
    char name[26];      ///< Name of the attribute (max 25 chars + null terminator).
    DataType type;      ///< Data type of the attribute.
    size_t count;       ///< Number of elements (for arrays). Default is 1.
    size_t offset;      ///< Byte offset within the record structure.

    /**
     * @brief Default constructor.
     */
    PAttr();

    /**
     * @brief Constructs an attribute and automatically assigns it a unique ID via PReg.
     * @param _name Name of the attribute.
     * @param _type Data type.
     * @param _count Number of elements (default 1).
     */
    PAttr(const char* _name, DataType _type, size_t _count = 1);

    /**
     * @brief Calculates the total size of this attribute in bytes.
     * @return Size in bytes (type size * count).
     */
    [[nodiscard]] size_t getSize() const;
};

} // namespace cyc

#endif // CYC_PATTR_H
