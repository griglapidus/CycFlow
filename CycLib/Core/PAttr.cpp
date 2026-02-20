// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "PAttr.h"
#include "PReg.h"
#include <cstring>

namespace cyc {

PAttr::PAttr() :
    id(0),
    type(DataType::dtUndefine),
    count(1),
    offset(0)
{
    std::memset(name, 0, sizeof(name));
}

PAttr::PAttr(const char* _name, DataType _type, size_t _count) :
    id(0),
    type(_type),
    count(_count),
    offset(0)
{
    std::strncpy(name, _name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    id = PReg::getID(name); // Automatically register and get ID
}

size_t PAttr::getSize() const {
    return getTypeSize(type) * count;
}

} // namespace cyc
