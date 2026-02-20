// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecRule.h"
#include <cstring>
#include <numeric>
#include <algorithm>
#include <sstream>

namespace cyc {

RecRule::RecRule(const std::vector<PAttr> &inputAttrs) {
    init(inputAttrs);
}

void RecRule::init(const std::vector<PAttr> &inputAttrs) {
    m_attrs.clear();
    size_t tempSize = 0;

    buildHeader();

    for (auto attr : m_headerAttrs) {
        attr.offset = tempSize;
        tempSize += attr.getSize();
        m_attrs.push_back(attr);
    }

    for (auto attr : inputAttrs) {
        attr.offset = tempSize;
        tempSize += attr.getSize();
        m_attrs.push_back(attr);
    }

    int maxId = 0;
    for (const auto& attr : m_attrs) {
        if (attr.id > maxId) maxId = attr.id;
    }

    m_offsetCache.assign(maxId + 1, static_cast<size_t>(-1));
    m_typeCache.assign(maxId + 1, DataType::dtUndefine);

    for (const auto& attr : m_attrs) {
        m_offsetCache[attr.id] = attr.offset;
        m_typeCache[attr.id] = attr.type;
    }
}

void RecRule::buildHeader() {
    m_headerAttrs.clear();
    PAttr timestamp("TimeStamp", DataType::dtDouble, 1);
    m_headerAttrs.push_back(timestamp);
}

size_t RecRule::getRecSize() const {
    return std::accumulate(m_attrs.begin(), m_attrs.end(), size_t(0),
                           [](size_t sum, const PAttr& attr) { return sum + attr.getSize(); });
}

size_t RecRule::getOffsetByIndex(size_t index) const {
    if (index >= m_attrs.size()) return 0;
    return m_attrs[index].offset;
}

size_t RecRule::getOffsetById(int id) const {
    if (id >= 0 && id < static_cast<int>(m_offsetCache.size())) {
        size_t offset = m_offsetCache[id];
        if (offset != static_cast<size_t>(-1)) {
            return offset;
        }
    }
    return 0;
}

DataType RecRule::getType(int id) const {
    if (id >= 0 && id < static_cast<int>(m_typeCache.size())) {
        return m_typeCache[id];
    }
    return DataType::dtVoid;
}

const std::vector<PAttr>& RecRule::getAttributes() const {
    return m_attrs;
}

std::string RecRule::toText() const {
    std::stringstream ss;
    for (const auto& attr : m_attrs) {
        ss << attr.name << ";"
           << dataTypeToString(attr.type) << ";"
           << attr.count << ";"
           << attr.offset << "\n";
    }
    return ss.str();
}

RecRule RecRule::fromText(const std::string& text) {
    RecRule defaultRule;
    defaultRule.buildHeader();
    const auto& systemHeaders = defaultRule.m_headerAttrs;

    std::vector<PAttr> userAttrs;
    std::stringstream ss(text);
    std::string line;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();

        std::stringstream ls(line);
        std::string segment;
        std::vector<std::string> parts;

        while(std::getline(ls, segment, ';')) {
            parts.push_back(segment);
        }

        if (parts.size() >= 3) {
            std::string name = parts[0];
            bool isSystemHeader = false;

            for (const auto& header : systemHeaders) {
                if (std::strncmp(header.name, name.c_str(), sizeof(header.name) - 1) == 0) {
                    isSystemHeader = true;
                    break;
                }
            }

            if (isSystemHeader) continue;

            DataType type = dataTypeFromString(parts[1].c_str());
            size_t count = std::stoul(parts[2]);

            userAttrs.emplace_back(name.c_str(), type, count);
        }
    }

    return RecRule(userAttrs);
}

} // namespace cyc
