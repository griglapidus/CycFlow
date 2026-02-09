// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "Common.h"
#include <chrono>

size_t cyc::getTypeSize(DataType t) {
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

double cyc::get_current_epoch_time() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::duration<double>>(duration).count();
}
