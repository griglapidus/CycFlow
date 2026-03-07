#ifndef CHARTDEFS_H
#define CHARTDEFS_H

#include <QVector>
#include <QString>
#include <QColor>
#include <QMetaType>
#include <variant>
#include <cstdint>
#include <cfloat>

// =============================================================================
//  Shared rendering constants
// =============================================================================

/**
 * @brief Pixels subtracted from a row's pixel height to get the usable chart area.
 *
 * Used identically in ChartDelegate (painting) and ChartView (geometry
 * calculations).  Defined here so both files share a single source of truth.
 */
constexpr int kChartVPad = 8;

// =============================================================================
//  SampleBuffer — type-safe multi-format sample storage
// =============================================================================

/**
 * @brief Variant holding a typed sample vector.
 *
 * Index mapping:
 *   0 = int8,  1 = uint8,  2 = int16, 3 = uint16,
 *   4 = int32, 5 = uint32, 6 = int64, 7 = uint64,
 *   8 = float32, 9 = float64
 */
using SampleBuffer = std::variant<
    QVector<int8_t>,    ///< 0 – int8
    QVector<uint8_t>,   ///< 1 – uint8
    QVector<int16_t>,   ///< 2 – int16
    QVector<uint16_t>,  ///< 3 – uint16
    QVector<int32_t>,   ///< 4 – int32
    QVector<uint32_t>,  ///< 5 – uint32
    QVector<int64_t>,   ///< 6 – int64
    QVector<uint64_t>,  ///< 7 – uint64
    QVector<float>,     ///< 8 – float32
    QVector<double>     ///< 9 – float64
    >;

/** @brief Numeric tag that identifies the active type in a SampleBuffer. */
enum class SampleType : int {
    Int8=0, UInt8=1, Int16=2, UInt16=3,
    Int32=4, UInt32=5, Int64=6, UInt64=7,
    Float32=8, Float64=9
};

// =============================================================================
//  BoundsValue — type-preserving min/max storage
// =============================================================================

/**
 * @brief Stores the minimum or maximum of a series with native precision.
 *
 * Retaining int64/uint64 in their native types avoids the precision loss
 * that would occur if they were cast to double before comparison.
 */
using BoundsValue = std::variant<double, int64_t, uint64_t>;

/**
 * @brief Returns inverted initial bounds for the given sample type.
 *
 * The returned pair is {initialMin, initialMax} set so that the very first
 * real sample value immediately overwrites both bounds.
 */
inline std::pair<BoundsValue,BoundsValue> makeBounds(SampleType t)
{
    switch (t) {
    case SampleType::Int64:  return { int64_t(INT64_MAX),   int64_t(INT64_MIN)  };
    case SampleType::UInt64: return { uint64_t(UINT64_MAX), uint64_t(0)         };
    default:                 return { double(DBL_MAX),       double(-DBL_MAX)    };
    }
}

/** @brief Losslessly converts any BoundsValue to double. */
inline double boundsToDouble(const BoundsValue &b)
{
    return std::visit([](const auto &v){ return static_cast<double>(v); }, b);
}

// =============================================================================
//  SampleBuffer helpers
// =============================================================================

/** @brief Returns the number of samples stored in @p buf. */
inline int sampleCount(const SampleBuffer &buf)
{
    return std::visit([](const auto &v){ return v.size(); }, buf);
}

/** @brief Returns @c true if @p buf contains no samples. */
inline bool sampleIsEmpty(const SampleBuffer &buf)
{
    return std::visit([](const auto &v){ return v.isEmpty(); }, buf);
}

/**
 * @brief Returns sample at index @p i cast to double.
 * @warning May lose precision for large int64/uint64 values.
 */
inline double sampleAt(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> double {
        return static_cast<double>(v[i]);
    }, buf);
}

/** @brief Returns sample at index @p i cast to int64_t. */
inline int64_t sampleAtI64(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> int64_t {
        return static_cast<int64_t>(v[i]);
    }, buf);
}

/** @brief Returns sample at index @p i cast to uint64_t. */
inline uint64_t sampleAtU64(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> uint64_t {
        return static_cast<uint64_t>(v[i]);
    }, buf);
}

/**
 * @brief Computes the normalised position of sample @p i within [lo, hi].
 *
 * Uses native int64/uint64 arithmetic when the buffer holds those types
 * to avoid precision loss, falling back to double for all other types.
 *
 * @return Value in [0.0, 1.0]; returns 0.5 when @p hi == @p lo.
 */
inline double sampleRatio(const SampleBuffer &buf, int i,
                          const BoundsValue &lo, const BoundsValue &hi)
{
    const std::size_t idx = buf.index();
    if (idx == 6) {
        const int64_t v = sampleAtI64(buf, i);
        const int64_t l = std::get<int64_t>(lo), h = std::get<int64_t>(hi);
        if (h == l) return 0.5;
        return static_cast<double>(v - l) / static_cast<double>(h - l);
    }
    if (idx == 7) {
        const uint64_t v = sampleAtU64(buf, i);
        const uint64_t l = std::get<uint64_t>(lo), h = std::get<uint64_t>(hi);
        if (h == l) return 0.5;
        return static_cast<double>(v - l) / static_cast<double>(h - l);
    }
    const double v = sampleAt(buf, i);
    const double l = boundsToDouble(lo), h = boundsToDouble(hi);
    if (qFuzzyCompare(l, h)) return 0.5;
    return (v - l) / (h - l);
}

/**
 * @brief Returns the human-readable type name of the active variant.
 * @return e.g. "float32", "int16", "uint64".
 */
inline const char *sampleTypeName(const SampleBuffer &buf)
{
    static const char *names[] = {
        "int8","uint8","int16","uint16","int32","uint32",
        "int64","uint64","float32","float64"
    };
    const std::size_t idx = buf.index();
    return (idx < std::size(names)) ? names[idx] : "?";
}

/** @brief Creates an empty SampleBuffer of the requested type. */
inline SampleBuffer makeSampleBuffer(SampleType t)
{
    switch (t) {
    case SampleType::Int8:    return QVector<int8_t>{};
    case SampleType::UInt8:   return QVector<uint8_t>{};
    case SampleType::Int16:   return QVector<int16_t>{};
    case SampleType::UInt16:  return QVector<uint16_t>{};
    case SampleType::Int32:   return QVector<int32_t>{};
    case SampleType::UInt32:  return QVector<uint32_t>{};
    case SampleType::Int64:   return QVector<int64_t>{};
    case SampleType::UInt64:  return QVector<uint64_t>{};
    case SampleType::Float32: return QVector<float>{};
    case SampleType::Float64: return QVector<double>{};
    }
    return QVector<float>{};
}

// =============================================================================
//  SeriesBatch — lightweight DTO for bulk appends
// =============================================================================

/**
 * @brief Pairs a series name with a typed block of new samples.
 *
 * Used with ChartModel::appendBatch() for efficient cross-thread delivery
 * via Qt queued connections.
 */
struct SeriesBatch {
    QString      name;    ///< Target series name (must already exist in ChartModel)
    SampleBuffer samples; ///< New samples to append (type must match the series type)
};

// =============================================================================
//  ChartSeries — complete state of a single plotted channel
// =============================================================================

/**
 * @brief Describes one data channel displayed in ChartView.
 *
 * Owned exclusively by ChartModel; the rendering thread accesses an instance
 * read-only through ChartModel::SeriesPointerRole.
 */
/**
 * @brief Describes one data channel displayed in ChartView.
 *
 * Owned exclusively by ChartModel; the rendering thread accesses an instance
 * read-only through ChartModel::SeriesPointerRole.
 *
 * ### Color management
 * When @c colorIndex >= 0 the series participates in automatic theme-aware
 * coloring.  ChartModel::reapplySeriesColors() resolves the index against the
 * active ChartTheme palette and writes the result into @c color.
 *
 * When @c colorIndex == kManualColor the series was given an explicit color
 * by the caller and is never touched by the automatic recoloring pass.
 */
struct ChartSeries {
    /// Sentinel value for colorIndex meaning "user-supplied, do not recolor".
    static constexpr int kManualColor = -1;

    QString      name;
    QColor       color      = Qt::green;

    /**
     * @brief Index into the ChartTheme series-color palette, or kManualColor.
     *
     * Assigned automatically by ChartModel::addSeries() when no explicit color
     * is provided.  Used by ChartModel::reapplySeriesColors() to update @c color
     * when the application theme changes.
     */
    int          colorIndex = kManualColor;

    SampleBuffer data     = QVector<float>{};
    BoundsValue  minVal   = double( DBL_MAX);  ///< Running minimum, updated on each append
    BoundsValue  maxVal   = double(-DBL_MAX);  ///< Running maximum, updated on each append

    // --- Display parameters --------------------------------------------------
    int    rowHeight    = 90;       ///< Current row height in pixels
    int    minRowHeight = 0;        ///< Minimum resize limit (0 = unrestricted)
    int    maxRowHeight = INT_MAX;  ///< Maximum resize limit (INT_MAX = unrestricted)

    /**
     * @brief Explicit Y-axis lower bound used for rendering.
     *
     * When both viewLo and viewHi are valid (not NaN, viewHi > viewLo),
     * rendering uses these bounds and ignores the running minVal/maxVal.
     * This means incoming data cannot shift or rescale the chart.
     *
     * Set to qQNaN() (auto mode) to always show the full data range.
     * Auto mode is the default; it is restored by resetAllDisplayParams()
     * and by resetSeriesView().
     */
    double viewLo = qQNaN(); ///< Y axis lower bound; NaN = auto (show full data range)
    double viewHi = qQNaN(); ///< Y axis upper bound; NaN = auto
};

// =============================================================================
//  Qt metatype registration for cross-thread QueuedConnection
// =============================================================================

Q_DECLARE_METATYPE(SeriesBatch)
Q_DECLARE_METATYPE(QList<SeriesBatch>)

// =============================================================================
//  effectiveViewBounds — resolves the Y axis range used for rendering
// =============================================================================

/**
 * @brief Returns the {lo, hi} value-space bounds used for rendering @p s.
 *
 * If viewLo and viewHi are both finite and viewHi > viewLo, they are
 * returned as-is (the user has explicitly set the Y range).  Otherwise
 * the function falls back to the running minVal/maxVal bounds.
 *
 * Use this everywhere a Y axis range is needed — in the delegate (paint),
 * in ChartView (zoom, pan, fit) and in computeGridLabelWidth().
 */
inline std::pair<double,double> effectiveViewBounds(const ChartSeries &s)
{
    if (!qIsNaN(s.viewLo) && !qIsNaN(s.viewHi) && s.viewHi > s.viewLo)
        return {s.viewLo, s.viewHi};
    return {boundsToDouble(s.minVal), boundsToDouble(s.maxVal)};
}

#endif // CHARTDEFS_H
