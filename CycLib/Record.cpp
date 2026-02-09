// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "Record.h"
#include <cstring>

namespace cyc {

Record::Record(const RecRule& r, void* ptr) 
    : rule(r), data(static_cast<uint8_t*>(ptr)) 
{}

bool Record::isValid() const { return data != nullptr; }
void Record::setData(void* ptr) { data = static_cast<uint8_t*>(ptr); }

void Record::clear()
{
    if (isValid()) {
        std::memset(data, 0, rule.getRecSize());
    }
}
void Record::copyFrom(const void* src) {
    if (isValid() && src) {
        std::memcpy(data, src, rule.getRecSize());
    }
}
void* Record::getVoid(int id) const {
    if (!isValid()) {
        return nullptr;
    }
    return data + rule.getOffsetById(id);
}

double Record::getValue(int id) const {
    DataType type = rule.getType(id);
    void* ptr = getVoid(id);
    if (!ptr) return 0.0;

    switch (type) {
        case DataType::dtBool:   return static_cast<double>(*static_cast<bool*>(ptr));
        case DataType::dtChar:   return static_cast<double>(*static_cast<char*>(ptr));
        case DataType::dtInt8:   return static_cast<double>(*static_cast<int8_t*>(ptr));
        case DataType::dtUInt8:  return static_cast<double>(*static_cast<uint8_t*>(ptr));
        case DataType::dtInt16:  return static_cast<double>(*static_cast<int16_t*>(ptr));
        case DataType::dtUInt16: return static_cast<double>(*static_cast<uint16_t*>(ptr));
        case DataType::dtInt32:  return static_cast<double>(*static_cast<int32_t*>(ptr));
        case DataType::dtUInt32: return static_cast<double>(*static_cast<uint32_t*>(ptr));
        case DataType::dtInt64:  return static_cast<double>(*static_cast<int64_t*>(ptr));
        case DataType::dtUInt64: return static_cast<double>(*static_cast<uint64_t*>(ptr));
        case DataType::dtFloat:  return static_cast<double>(*static_cast<float*>(ptr));
        case DataType::dtDouble: return *static_cast<double*>(ptr);
        case DataType::dtPtr:    return static_cast<double>(reinterpret_cast<uintptr_t>(*static_cast<void**>(ptr)));
        default: return 0.0;
    }
}

void Record::setValue(int id, double val) {
    DataType type = rule.getType(id);
    void* ptr = getVoid(id);
    if (!ptr) return;

    switch (type) {
        case DataType::dtBool:   *static_cast<bool*>(ptr) = (val != 0.0); break;
        case DataType::dtChar:   *static_cast<char*>(ptr) = static_cast<char>(val); break;
        case DataType::dtInt8:   *static_cast<int8_t*>(ptr) = static_cast<int8_t>(val); break;
        case DataType::dtUInt8:  *static_cast<uint8_t*>(ptr) = static_cast<uint8_t>(val); break;
        case DataType::dtInt16:  *static_cast<int16_t*>(ptr) = static_cast<int16_t>(val); break;
        case DataType::dtUInt16: *static_cast<uint16_t*>(ptr) = static_cast<uint16_t>(val); break;
        case DataType::dtInt32:  *static_cast<int32_t*>(ptr) = static_cast<int32_t>(val); break;
        case DataType::dtUInt32: *static_cast<uint32_t*>(ptr) = static_cast<uint32_t>(val); break;
        case DataType::dtInt64:  *static_cast<int64_t*>(ptr) = static_cast<int64_t>(val); break;
        case DataType::dtUInt64: *static_cast<uint64_t*>(ptr) = static_cast<uint64_t>(val); break;
        case DataType::dtFloat:  *static_cast<float*>(ptr) = static_cast<float>(val); break;
        case DataType::dtDouble: *static_cast<double*>(ptr) = val; break;
        case DataType::dtPtr:    *static_cast<void**>(ptr) = reinterpret_cast<void*>(static_cast<uintptr_t>(val)); break;
        default: break;
    }
}

} // namespace cyc
