// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECRULE_H
#define CYC_RECRULE_H

#include "PAttr.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstddef>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @struct BitRef
 * @brief Describes the location of a named bit in terms of its *containing field*.
 *
 * The cache is indexed by the bit's own PReg ID and stores:
 *  - `fieldId`  — PReg ID of the integer PAttr that owns this bit.
 *                 Using this ID with `getOffsetById` / `getType` gives the full
 *                 field descriptor (offset, type, count) with the same O(1) cost.
 *  - `bitPos`   — zero-based bit index within that field (0 = LSB of element 0).
 *
 * Keeping `fieldId` rather than a raw byte offset makes the cache self-describing:
 * you can always reconstruct "who owns this bit" without a reverse lookup.
 */
struct BitRef {
    int fieldId;  ///< PReg ID of the containing integer field (0 = not a bit field).
    int bitPos;   ///< Bit index within the integer element (0 = LSB).
};

/**
 * @class RecRule
 * @brief Defines the memory layout and schema of a record.
 *
 * Manages a collection of attributes (`PAttr`), calculates their byte offsets,
 * and caches offsets, types, and bit-field locations in flat arrays for O(1)
 * runtime lookups.
 *
 * Bit fields are supported via `PAttr` instances constructed with a `bitDefs`
 * vector.  After schema construction `getOffsetById` / `getType` work for the
 * *containing integer* field, while `getBitRef` resolves individual named bits.
 */
class CYCLIB_EXPORT RecRule {
public:
    /**
     * @brief Default constructor. Creates an empty rule.
     */
    RecRule() = default;

    /**
     * @brief Constructs a rule from a provided list of attributes.
     *
     * @param inputAttrs User-defined attributes (plain or bit-field).
     * @param align      When @c true, enables aligned layout:
     *                    - user fields are stably sorted by decreasing element size
     *                      so larger (more strictly aligned) types are placed first;
     *                    - since every supported element size is a power of two,
     *                      this sort alone keeps every field naturally aligned and
     *                      no internal padding is required;
     *                    - trailing padding is appended so the total record size is
     *                      a multiple of the *largest* element size in the record,
     *                      which keeps the next record in a packed buffer starting
     *                      on the same boundary.
     *                   When @c false (default) the historical tightly-packed layout
     *                   is used — fields keep their input order and there is no
     *                   inter-field or trailing padding.
     */
    explicit RecRule(const std::vector<PAttr>& inputAttrs, bool align = false);

    /**
     * @brief Initialises the rule, builds headers and all O(1) caches.
     *
     * @param inputAttrs User-defined attributes.
     * @param align      See RecRule constructor for semantics.
     */
    void init(const std::vector<PAttr>& inputAttrs, bool align = false);

    /**
     * @brief Builds internal system header attributes (e.g. TimeStamp).
     */
    void buildHeader();

    /**
     * @brief Gets the total memory size of a single record defined by this schema.
     * @return Total size in bytes.
     *
     * Inlined: this is called on every field access path
     * (Record::clear, RecordWriter::nextRecord) and trivial-inline avoids a
     * cross-DLL call.
     */
    [[nodiscard]] size_t getRecSize() const { return m_recSize; }

    /**
     * @brief Gets the byte offset of an attribute by sequential index.
     * @param index Index in the internal attribute list.
     * @return Offset in bytes.
     */
    [[nodiscard]] size_t getOffsetByIndex(size_t index) const;

    /**
     * @brief Gets the byte offset of an attribute by its PReg ID.
     * @note O(1) flat-array lookup.
     * @param id PReg ID of the attribute.
     * @return Offset in bytes, or 0 if the ID is invalid.
     *
     * Inlined: this is the hottest function in the library — called for every
     * typed field access (Record::getVoid). Keeping it out-of-line in the DLL
     * turns every field access into a dllimport trampoline call and blocks the
     * compiler from folding adjacent getters in the user's hot loop. With the
     * body visible, a full-chain inline (test → Record::getInt32 → getVoid →
     * getOffsetById) collapses to a couple of loads.
     */
    [[nodiscard]] size_t getOffsetById(int id) const {
        if (static_cast<unsigned>(id) < static_cast<unsigned>(m_offsetCache.size())) {
            const size_t offset = m_offsetCache[id];
            if (offset != static_cast<size_t>(-1)) return offset;
        }
        return 0;
    }

    /**
     * @brief Gets the data type of an attribute by its PReg ID.
     * @note O(1) flat-array lookup.
     * @param id PReg ID of the attribute.
     * @return DataType, or dtVoid if invalid.
     *
     * Inlined for the same reason as getOffsetById — called on every generic
     * (double-based) accessor path.
     */
    [[nodiscard]] DataType getType(int id) const {
        if (static_cast<unsigned>(id) < static_cast<unsigned>(m_typeCache.size())) {
            const DataType t = m_typeCache[id];
            if (t != DataType::dtUndefine) return t;
        }
        return DataType::dtVoid;
    }

    /**
     * @brief Returns the BitRef for a named bit by its PReg ID.
     *
     * The returned struct contains:
     *  - `fieldId` — PReg ID of the containing integer field.
     *                Pass it to `getOffsetById` / `getType` for full field info.
     *  - `bitPos`  — zero-based bit index within that field (0 = LSB).
     *
     * @param id  PReg ID of the named bit (registered via PAttr's bitDefs).
     * @return    A valid BitRef on success (fieldId > 0).
     *            Returns {0, 0} if id does not refer to a known named bit.
     */
    [[nodiscard]] BitRef getBitRef(int id) const;

    /**
     * @brief Retrieves the complete attribute list, including system headers.
     * @return Const reference to the attribute vector.
     */
    [[nodiscard]] const std::vector<PAttr>& getAttributes() const;

    /**
     * @brief Serialises the schema to a delimited text string.
     *
     * Each line has the form:
     *   `name;Type;count;offset[;bitName0,skip,bitName1,...]\n`
     * Bit fields use the same notation as PAttr's bitDefs constructor.
     *
     * @return Multi-line string representation.
     */
    [[nodiscard]] std::string toText() const;

    /**
     * @brief Deserialises a schema previously produced by `toText`.
     * @param text Serialised schema string.
     * @return Reconstructed RecRule.
     */
    static RecRule fromText(const std::string& text);

private:
    std::vector<PAttr>     m_headerAttrs;
    std::vector<PAttr>     m_attrs;
    size_t                 m_recSize = 0;   ///< Total record size in bytes (including alignment padding).
    bool                   m_aligned = false; ///< True if the layout was built in aligned mode.

    // --- O(1) caches for plain fields (indexed by PReg ID via flat vector) ---
    std::vector<size_t>    m_offsetCache;   ///< Byte offset of the containing field.
    std::vector<DataType>  m_typeCache;     ///< DataType of the containing field.

    /// Bit-field cache: maps bit's PReg ID → BitRef{fieldId, bitPos}.
    /// Uses unordered_map to avoid sparse allocation when bit IDs are not
    /// contiguous with plain-field IDs (which is the common case).
    std::unordered_map<int, BitRef> m_bitCache;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECRULE_H
