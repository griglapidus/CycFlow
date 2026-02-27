#ifndef CHARTDEFS_H
#define CHARTDEFS_H

#include <QVector>
#include <QString>
#include <QColor>
#include <QMetaType>
#include <variant>
#include <cstdint>
#include <cfloat>

// ─── SampleBuffer ─────────────────────────────────────────────────────────────

using SampleBuffer = std::variant<
    QVector<int8_t>,    // 0
    QVector<uint8_t>,   // 1
    QVector<int16_t>,   // 2
    QVector<uint16_t>,  // 3
    QVector<int32_t>,   // 4
    QVector<uint32_t>,  // 5
    QVector<int64_t>,   // 6
    QVector<uint64_t>,  // 7
    QVector<float>,     // 8
    QVector<double>     // 9
    >;

enum class SampleType : int {
    Int8=0, UInt8=1, Int16=2, UInt16=3,
    Int32=4, UInt32=5, Int64=6, UInt64=7,
    Float32=8, Float64=9
};

// ─── BoundsValue ─────────────────────────────────────────────────────────────

using BoundsValue = std::variant<double, int64_t, uint64_t>;

inline std::pair<BoundsValue,BoundsValue> makeBounds(SampleType t)
{
    switch (t) {
    case SampleType::Int64:  return { int64_t(INT64_MAX),  int64_t(INT64_MIN)  };
    case SampleType::UInt64: return { uint64_t(UINT64_MAX), uint64_t(0)         };
    default:                 return { double(DBL_MAX),       double(-DBL_MAX)   };
    }
}

inline double boundsToDouble(const BoundsValue &b)
{
    return std::visit([](const auto &v){ return static_cast<double>(v); }, b);
}

// ─── SampleBuffer helpers ─────────────────────────────────────────────────────

inline int sampleCount(const SampleBuffer &buf)
{
    return std::visit([](const auto &v){ return v.size(); }, buf);
}

inline bool sampleIsEmpty(const SampleBuffer &buf)
{
    return std::visit([](const auto &v){ return v.isEmpty(); }, buf);
}

inline double sampleAt(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> double {
        return static_cast<double>(v[i]);
    }, buf);
}

inline int64_t sampleAtI64(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> int64_t {
        return static_cast<int64_t>(v[i]);
    }, buf);
}

inline uint64_t sampleAtU64(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> uint64_t {
        return static_cast<uint64_t>(v[i]);
    }, buf);
}

// sampleRatio: (v - lo) / (hi - lo) с нативной точностью для int64/uint64
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

inline const char *sampleTypeName(const SampleBuffer &buf)
{
    static const char *names[] = {
        "int8","uint8","int16","uint16","int32","uint32",
        "int64","uint64","float32","float64"
    };
    const std::size_t idx = buf.index();
    return (idx < std::size(names)) ? names[idx] : "?";
}

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

// ─── SeriesBatch ─────────────────────────────────────────────────────────────

struct SeriesBatch {
    QString      name;
    SampleBuffer samples;
};

// ─── ChartSeries ─────────────────────────────────────────────────────────────

struct ChartSeries {
    QString      name;
    QColor       color    = Qt::green;
    SampleBuffer data     = QVector<float>{};
    BoundsValue  minVal   = double( DBL_MAX);
    BoundsValue  maxVal   = double(-DBL_MAX);

    // ── Параметры отображения ─────────────────────────────────────────────
    int   rowHeight    = 90;
    int   minRowHeight = 0;          ///< Нижний порог resize (0 = без ограничения)
    int   maxRowHeight = INT_MAX;    ///< Верхний порог resize (INT_MAX = без ограничения)
    float yScale    = 1.0f;
    int   yOffset   = 0;
};

// ─── Qt metatype для кросс-поточного QueuedConnection ────────────────────────

Q_DECLARE_METATYPE(SeriesBatch)
Q_DECLARE_METATYPE(QList<SeriesBatch>)

#endif // CHARTDEFS_H
