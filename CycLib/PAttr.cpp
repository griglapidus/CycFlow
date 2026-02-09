// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "PAttr.h"
#include "PReg.h"
#include <cstring>

namespace cyc {

PAttr::PAttr() :
    id(0),
    type(DataType::dtUndefine),
    count(1)
{
    name[0] = 0;
}

PAttr::PAttr(const char* _name, DataType _type, size_t _count) :
    id(0),
    type(_type),
    count(_count)
{
    strncpy(name, _name, 25);
    name[25] = '\0';
    id = PReg::getID(name);
}

size_t PAttr::getSize() const { return getTypeSize(type) * count; }

} // namespace cyc
