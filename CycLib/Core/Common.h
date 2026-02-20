// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_COMMON_H
#define CYC_COMMON_H

#include <cstdint>
#include "CycLib_global.h"

namespace cyc {

/**
 * @brief Enumeration representing supported data types within records.
 */
enum class DataType : uint8_t {
    dtUndefine, ///< Undefined type
    dtBool,     ///< Boolean
    dtChar,     ///< Character
    dtVoid,     ///< Void type
    dtInt8,     ///< 8-bit signed integer
    dtUInt8,    ///< 8-bit unsigned integer
    dtInt16,    ///< 16-bit signed integer
    dtUInt16,   ///< 16-bit unsigned integer
    dtInt32,    ///< 32-bit signed integer
    dtUInt32,   ///< 32-bit unsigned integer
    dtInt64,    ///< 64-bit signed integer
    dtUInt64,   ///< 64-bit unsigned integer
    dtFloat,    ///< Float
    dtDouble,   ///< Double
    dtPtr       ///< Pointer
};

/**
 * @brief Gets the size in bytes of a specific data type.
 * @param t The data type to check.
 * @return Size of the type in bytes.
 */
size_t getTypeSize(DataType t);

/**
 * @brief Gets the current epoch time.
 * @return Current time as a double.
 */
CYCLIB_EXPORT double get_current_epoch_time();

/**
 * @brief Converts DataType enum to string representation.
 * @param t Data type.
 * @return String representation of the type.
 */
const char* dataTypeToString(DataType t);

DataType dataTypeFromString(const char* str);

} // namespace cyc

#endif // CYC_COMMON_H
