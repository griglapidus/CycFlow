// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECRULE_H
#define CYC_RECRULE_H

#include "PAttr.h"
#include <string>
#include <vector>

namespace cyc {

/**
 * @class RecRule
 * @brief Defines the memory layout and schema of a record.
 *
 * This class manages a collection of attributes (`PAttr`), calculates their
 * byte offsets within a contiguous memory block, and caches these offsets
 * and types in flat arrays for O(1) runtime lookups. This ensures extremely
 * fast access to record fields during data processing.
 */
class CYCLIB_EXPORT RecRule {
public:
    /**
     * @brief Default constructor. Creates an empty rule.
     */
    RecRule() = default;

    /**
     * @brief Constructs a rule from a provided list of attributes.
     * @param inputAttrs A vector containing the user-defined attributes.
     */
    explicit RecRule(const std::vector<PAttr>& inputAttrs);

    /**
     * @brief Initializes the rule, builds headers, and constructs O(1) caches.
     * @param inputAttrs A vector containing the user-defined attributes.
     */
    void init(const std::vector<PAttr>& inputAttrs);

    /**
     * @brief Builds internal system header attributes (e.g., TimeStamp).
     */
    void buildHeader();

    /**
     * @brief Gets the total memory size of a single record defined by this schema.
     * @return Total size in bytes.
     */
    [[nodiscard]] size_t getRecSize() const;

    /**
     * @brief Gets the memory offset of an attribute by its sequential index.
     * @param index The index of the attribute in the internal list.
     * @return The offset in bytes from the beginning of the record.
     */
    [[nodiscard]] size_t getOffsetByIndex(size_t index) const;

    /**
     * @brief Gets the memory offset of an attribute by its unique registry ID.
     * @note Uses a flat array cache to guarantee O(1) performance.
     * @param id The unique ID of the attribute (obtained from PReg).
     * @return The offset in bytes, or 0 if the ID is invalid.
     */
    [[nodiscard]] size_t getOffsetById(int id) const;

    /**
     * @brief Gets the data type of an attribute by its unique registry ID.
     * @note Uses a flat array cache to guarantee O(1) performance.
     * @param id The unique ID of the attribute.
     * @return The DataType of the attribute. Returns dtVoid if invalid.
     */
    [[nodiscard]] DataType getType(int id) const;

    /**
     * @brief Retrieves the complete list of attributes, including system headers.
     * @return A constant reference to the vector of attributes.
     */
    [[nodiscard]] const std::vector<PAttr>& getAttributes() const;

    /**
     * @brief Serializes the schema to a delimited text string.
     * @return A string representation of the schema.
     */
    [[nodiscard]] std::string toText() const;

    /**
     * @brief Deserializes a schema from a text string.
     * @param text The string representation of the schema.
     * @return A constructed RecRule object.
     */
    static RecRule fromText(const std::string& text);

private:
    std::vector<PAttr> m_headerAttrs;
    std::vector<PAttr> m_attrs;

    std::vector<size_t> m_offsetCache;
    std::vector<DataType> m_typeCache;
};

} // namespace cyc

#endif // CYC_RECRULE_H
