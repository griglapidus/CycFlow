// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecRule.h"
#include <numeric>
#include <algorithm>
#include <iterator>

namespace cyc {

cyc::RecRule::RecRule(const std::vector<PAttr> &inputAttrs) {
    init(inputAttrs);
}

void RecRule::init(const std::vector<PAttr> &inputAttrs)
{
    m_attrs.clear();
    size_t tempSize = 0;
    buildHeader();

    for(auto attr : m_headerAttrs) {
        attr.offset = tempSize;
        tempSize = attr.offset + attr.getSize();
        m_attrs.push_back(attr);
    }

    for(auto attr : inputAttrs) {
        attr.offset = tempSize;
        tempSize = attr.offset + attr.getSize();
        m_attrs.push_back(attr);
    }
}

void RecRule::buildHeader()
{
    m_headerAttrs.clear();
    PAttr timestamp("TimeStamp",DataType::dtDouble,1);
    m_headerAttrs.push_back(timestamp);
}

size_t RecRule::getRecSize() const {
    return std::accumulate(m_attrs.begin(), m_attrs.end(), size_t(0),
        [](size_t sum, const PAttr& attr) { return sum + attr.getSize(); });
}
size_t RecRule::getOffsetByIndex(size_t index) const {
    if(index >= m_attrs.size()) return 0;
    return m_attrs[index].offset;
}
size_t RecRule::getOffsetById(int id) const {
    auto it = std::find_if(m_attrs.begin(), m_attrs.end(),
        [id](const PAttr& attr) { return attr.id == id; });
    if (it != m_attrs.end()) return getOffsetByIndex(std::distance(m_attrs.begin(), it));
    return 0;
}
DataType RecRule::getType(int id) const {
    auto it = std::find_if(m_attrs.begin(), m_attrs.end(),
        [id](const PAttr& attr) { return attr.id == id; });
    if (it != m_attrs.end()) return it->type;
    return DataType::dtVoid; 
}
const std::vector<PAttr>& RecRule::getAttributes() const { return m_attrs; }

} // namespace cyc
