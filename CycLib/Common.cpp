// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "Common.h"
#include <chrono>
#include <cstring>

namespace cyc {

size_t getTypeSize(DataType t) {
    switch(t) {
    case DataType::dtBool:   return sizeof(bool);
    case DataType::dtChar:   return sizeof(char);
    case DataType::dtVoid:   return 1;
    case DataType::dtInt8:   return sizeof(int8_t);
    case DataType::dtUInt8:  return sizeof(uint8_t);
    case DataType::dtInt16:  return sizeof(int16_t);
    case DataType::dtUInt16: return sizeof(uint16_t);
    case DataType::dtInt32:  return sizeof(int32_t);
    case DataType::dtUInt32: return sizeof(uint32_t);
    case DataType::dtInt64:  return sizeof(int64_t);
    case DataType::dtUInt64: return sizeof(uint64_t);
    case DataType::dtFloat:  return sizeof(float);
    case DataType::dtDouble: return sizeof(double);
    case DataType::dtPtr:    return sizeof(void*);
    default: return 0;
    }
}

double get_current_epoch_time() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::duration<double>>(duration).count();
}

const char* dataTypeToString(DataType t) {
    switch(t) {
    case DataType::dtUndefine: return "Undefine";
    case DataType::dtBool:     return "Bool";
    case DataType::dtChar:     return "Char";
    case DataType::dtVoid:     return "Void";
    case DataType::dtInt8:     return "Int8";
    case DataType::dtUInt8:    return "UInt8";
    case DataType::dtInt16:    return "Int16";
    case DataType::dtUInt16:   return "UInt16";
    case DataType::dtInt32:    return "Int32";
    case DataType::dtUInt32:   return "UInt32";
    case DataType::dtInt64:    return "Int64";
    case DataType::dtUInt64:   return "UInt64";
    case DataType::dtFloat:    return "Float";
    case DataType::dtDouble:   return "Double";
    case DataType::dtPtr:      return "Ptr";
    default:                   return "Unknown";
    }
}

DataType dataTypeFromString(const char* str) {
    if (strcmp(str, "Bool") == 0) return DataType::dtBool;
    if (strcmp(str, "Char") == 0) return DataType::dtChar;
    if (strcmp(str, "Int8") == 0) return DataType::dtInt8;
    if (strcmp(str, "UInt8") == 0) return DataType::dtUInt8;
    if (strcmp(str, "Int16") == 0) return DataType::dtInt16;
    if (strcmp(str, "UInt16") == 0) return DataType::dtUInt16;
    if (strcmp(str, "Int32") == 0) return DataType::dtInt32;
    if (strcmp(str, "UInt32") == 0) return DataType::dtUInt32;
    if (strcmp(str, "Int64") == 0) return DataType::dtInt64;
    if (strcmp(str, "UInt64") == 0) return DataType::dtUInt64;
    if (strcmp(str, "Float") == 0) return DataType::dtFloat;
    if (strcmp(str, "Double") == 0) return DataType::dtDouble;
    if (strcmp(str, "Ptr") == 0) return DataType::dtPtr;
    return DataType::dtUndefine;
}

}
