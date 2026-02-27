// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "PAttr.h"
#include "PReg.h"
#include <cstring>
#include <stdexcept>
#include <cctype>

namespace cyc {

// ---------------------------------------------------------------------------
// Helper: true if every character in s is a decimal digit (non-empty)
// ---------------------------------------------------------------------------
static bool isUnsignedInteger(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helper: true if DataType can serve as the base of a bit field
// ---------------------------------------------------------------------------
static bool isIntegerType(DataType t) {
    switch (t) {
    case DataType::dtBool:
    case DataType::dtInt8:
    case DataType::dtUInt8:
    case DataType::dtInt16:
    case DataType::dtUInt16:
    case DataType::dtInt32:
    case DataType::dtUInt32:
    case DataType::dtInt64:
    case DataType::dtUInt64:
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// PAttr – default constructor
// ---------------------------------------------------------------------------
PAttr::PAttr() :
    id(0),
    type(DataType::dtUndefine),
    count(1),
    offset(0)
{
    std::memset(name, 0, sizeof(name));
}

// ---------------------------------------------------------------------------
// PAttr – plain typed field
// ---------------------------------------------------------------------------
PAttr::PAttr(const char* _name, DataType _type, size_t _count) :
    id(0),
    type(_type),
    count(_count),
    offset(0)
{
    std::strncpy(name, _name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    id = PReg::getID(name);
}

// ---------------------------------------------------------------------------
// PAttr – integer field with named bit fields
// ---------------------------------------------------------------------------
PAttr::PAttr(const char* _name, DataType _type, const std::vector<std::string>& bitDefs) :
    id(0),
    type(_type),
    count(1),   // bit-field attributes always cover exactly one integer element
    offset(0)
{
    // --- Type validation ---------------------------------------------------
    if (!isIntegerType(_type)) {
        throw std::invalid_argument(
            std::string("PAttr '") + _name +
            "': bit fields require an integer base type "
            "(Bool/Int8/UInt8/.../UInt64), got '" +
            dataTypeToString(_type) + "'.");
    }

    std::strncpy(name, _name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    id = PReg::getID(name);

    const size_t totalBits = getTypeSize(_type) * 8;
    bitIds.resize(totalBits, 0);    // 0 = reserved/skipped bit

    size_t bitPos = 0;
    for (const auto& def : bitDefs) {
        if (bitPos >= totalBits) {
            throw std::out_of_range(
                std::string("PAttr '") + _name +
                "': bitDefs overflow the base type (" +
                std::to_string(totalBits) + " bits available).");
        }

        if (isUnsignedInteger(def)) {
            // Numeric string -> skip that many bits
            const size_t skip = std::stoul(def);
            if (skip == 0) continue;                // "0" is a no-op

            if (bitPos + skip > totalBits) {
                throw std::out_of_range(
                    std::string("PAttr '") + _name +
                    "': skip value " + def +
                    " at bit position " + std::to_string(bitPos) +
                    " would exceed the base type width (" +
                    std::to_string(totalBits) + " bits).");
            }
            bitPos += skip;

        } else if (!def.empty()) {
            // Named bit -> register with PReg and record its ID at this position
            bitIds[bitPos] = PReg::getID(def);
            ++bitPos;

        } else {
            // Empty string -> skip 1 bit (produced by toText/fromText round-trip)
            ++bitPos;
        }
    }

    // Trim trailing reserved (zero) entries for a leaner storage footprint
    while (!bitIds.empty() && bitIds.back() == 0) {
        bitIds.pop_back();
    }
}

// ---------------------------------------------------------------------------
// PAttr::getSize
// ---------------------------------------------------------------------------
size_t PAttr::getSize() const {
    return getTypeSize(type) * count;
}

} // namespace cyc
