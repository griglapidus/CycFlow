// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "Record.h"
#include <cstring>

namespace cyc {

Record::Record(const RecRule& r, void* ptr)
    : m_rule(r), m_data(static_cast<uint8_t*>(ptr))
{}


void Record::clear() {
    if (isValid()) {
        std::memset(m_data, 0, m_rule.getRecSize());
    }
}

// ---------------------------------------------------------------------------
// Generic double-based accessors
// ---------------------------------------------------------------------------

double Record::getValue(int id, size_t index) const {
    DataType type = m_rule.getType(id);
    void*    ptr  = getVoid(id);
    if (!ptr) return 0.0;

    switch (type) {
        case DataType::dtBool:   return static_cast<double>(static_cast<bool*>   (ptr)[index]);
        case DataType::dtChar:   return static_cast<double>(static_cast<char*>   (ptr)[index]);
        case DataType::dtInt8:   return static_cast<double>(static_cast<int8_t*> (ptr)[index]);
        case DataType::dtUInt8:  return static_cast<double>(static_cast<uint8_t*>(ptr)[index]);
        case DataType::dtInt16:  return static_cast<double>(static_cast<int16_t*>(ptr)[index]);
        case DataType::dtUInt16: return static_cast<double>(static_cast<uint16_t*>(ptr)[index]);
        case DataType::dtInt32:  return static_cast<double>(static_cast<int32_t*>(ptr)[index]);
        case DataType::dtUInt32: return static_cast<double>(static_cast<uint32_t*>(ptr)[index]);
        case DataType::dtInt64:  return static_cast<double>(static_cast<int64_t*>(ptr)[index]);
        case DataType::dtUInt64: return static_cast<double>(static_cast<uint64_t*>(ptr)[index]);
        case DataType::dtFloat:  return static_cast<double>(static_cast<float*>  (ptr)[index]);
        case DataType::dtDouble: return static_cast<double>(static_cast<double*> (ptr)[index]);
        case DataType::dtPtr:
            return static_cast<double>(
                reinterpret_cast<uintptr_t>(static_cast<void**>(ptr)[index]));
        default: return 0.0;
    }
}

void Record::setValue(int id, double val, size_t index) {
    DataType type = m_rule.getType(id);
    void*    ptr  = getVoid(id);
    if (!ptr) return;

    switch (type) {
        case DataType::dtBool:   static_cast<bool*>   (ptr)[index] = (val != 0.0); break;
        case DataType::dtChar:   static_cast<char*>   (ptr)[index] = static_cast<char>   (val); break;
        case DataType::dtInt8:   static_cast<int8_t*> (ptr)[index] = static_cast<int8_t> (val); break;
        case DataType::dtUInt8:  static_cast<uint8_t*>(ptr)[index] = static_cast<uint8_t>(val); break;
        case DataType::dtInt16:  static_cast<int16_t*>(ptr)[index] = static_cast<int16_t>(val); break;
        case DataType::dtUInt16: static_cast<uint16_t*>(ptr)[index]= static_cast<uint16_t>(val); break;
        case DataType::dtInt32:  static_cast<int32_t*>(ptr)[index] = static_cast<int32_t>(val); break;
        case DataType::dtUInt32: static_cast<uint32_t*>(ptr)[index]= static_cast<uint32_t>(val); break;
        case DataType::dtInt64:  static_cast<int64_t*>(ptr)[index] = static_cast<int64_t>(val); break;
        case DataType::dtUInt64: static_cast<uint64_t*>(ptr)[index]= static_cast<uint64_t>(val); break;
        case DataType::dtFloat:  static_cast<float*>  (ptr)[index] = static_cast<float>  (val); break;
        case DataType::dtDouble: static_cast<double*> (ptr)[index] = val; break;
        case DataType::dtPtr:
            static_cast<void**>(ptr)[index] =
                reinterpret_cast<void*>(static_cast<uintptr_t>(val));
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Bit-field accessors
// ---------------------------------------------------------------------------

bool Record::getBit(int id) const {
    if (!isValid()) return false;

    const BitRef ref = m_rule.getBitRef(id);
    if (ref.fieldId == 0) return false;

    // Two O(1) calls: BitRef gives the field ID, getOffsetById gives byte offset
    const size_t fieldOffset = m_rule.getOffsetById(ref.fieldId);
    const uint8_t byte = m_data[fieldOffset + static_cast<size_t>(ref.bitPos) / 8];
    return (byte >> (ref.bitPos % 8)) & 1u;
}

void Record::setBit(int id, bool val) {
    if (!isValid()) return;

    const BitRef ref = m_rule.getBitRef(id);
    if (ref.fieldId == 0) return;

    const size_t fieldOffset = m_rule.getOffsetById(ref.fieldId);
    uint8_t& byte   = m_data[fieldOffset + static_cast<size_t>(ref.bitPos) / 8];
    const uint8_t mask = static_cast<uint8_t>(1u << (ref.bitPos % 8));

    if (val)
        byte |= mask;
    else
        byte &= static_cast<uint8_t>(~mask);
}

double Record::getBitValue(int id) const {
    return getBit(id) ? 1.0 : 0.0;
}

void Record::setBitValue(int id, double val) {
    setBit(id, val != 0.0);
}

} // namespace cyc
