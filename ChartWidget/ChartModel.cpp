// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "ChartModel.h"
#include <QFontMetrics>

ChartModel::ChartModel(QObject *parent) : QAbstractTableModel(parent) {}

// =============================================================================
//  Row height limits
// =============================================================================

namespace {

struct RowHeightLimits { int min; int max; };

static RowHeightLimits computeLimits(ChartModel::RowKind kind)
{
    constexpr int kTopPad = 6;
    constexpr int kBotPad = 6;
    constexpr int kGap    = 3;

    const int nameH  = QFontMetrics(QFont("Consolas",  9, QFont::Bold)).height();
    const int valH   = QFontMetrics(QFont("Consolas", 11)).height();
    const int smallH = QFontMetrics(QFont("Consolas",  9)).height();

    switch (kind) {
    case ChartModel::RowKind::Timestamp: {
        const int h = kTopPad + nameH + kGap + smallH + kGap + smallH + smallH + kBotPad;
        return { h, INT_MAX };
    }
    case ChartModel::RowKind::Digital: {
        const int h = kTopPad + qMax(nameH, valH) + kBotPad;
        return { h, h };
    }
    default: {
        const int h = kTopPad + nameH + kGap + valH + kBotPad;
        return { h, INT_MAX };
    }
    }
}

} // namespace

// =============================================================================
//  Series management
// =============================================================================

QString ChartModel::addSeries(const QString &name, const QColor &color, SampleType sampleType)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_data.contains(name)) return name;

    const RowKind kind = (name == QLatin1String("TimeStamp"))
                             ? RowKind::Timestamp : RowKind::Regular;
    const auto [minH, maxH] = computeLimits(kind);

    ChartSeries s;
    s.name      = name;
    s.data      = makeSampleBuffer(sampleType);
    s.rowHeight    = qBound(minH, m_defaultRowHeight, maxH);
    s.minRowHeight = minH;
    s.maxRowHeight = maxH;
    auto [lo, hi] = makeBounds(sampleType);
    s.minVal = lo; s.maxVal = hi;

    // Color assignment:
    //   - invalid QColor  → auto-assign the next theme palette slot
    //   - valid   QColor  → pin to the caller's color, never recolor
    if (color.isValid()) {
        s.color      = color;
        s.colorIndex = ChartSeries::kManualColor;
    } else {
        s.colorIndex = m_nextColorIndex++;
        s.color      = ChartTheme::seriesColor(s.colorIndex, ChartTheme::systemVariant());
    }

    const int row = m_order.size();

    beginInsertRows({}, row, row);
    m_rowIndex.insert(name, m_order.size());
    m_data.insert(name, std::move(s));
    m_order.append(name);
    endInsertRows();
    return name;
}

QString ChartModel::addDigitalSeries(const QString &bitName, const QColor &color)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const QString name = addSeries(bitName, color, SampleType::UInt8);
    const auto [minH, maxH] = computeLimits(RowKind::Digital);
    auto it = m_data.find(name);
    if (it != m_data.end()) {
        it->rowHeight    = minH;   // minH == maxH for digital rows
        it->minRowHeight = minH;
        it->maxRowHeight = maxH;
        it->minVal       = double(0.0);
        it->maxVal       = double(1.0);
    }
    emit layoutChanged();
    return name;
}

void ChartModel::reapplySeriesColors(ChartTheme::Variant v)
{
    Q_ASSERT(thread() == QThread::currentThread());
    for (auto &s : m_data) {
        if (s.colorIndex == ChartSeries::kManualColor) continue;
        s.color = ChartTheme::seriesColor(s.colorIndex, v);
    }
    if (!m_order.isEmpty())
        emit dataChanged(index(0, 0), index(m_order.size() - 1, 0));
}

void ChartModel::clearSeries(const QString &name)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto it = m_data.find(name);
    if (it == m_data.end()) return;
    ChartSeries &s  = it.value();
    const SampleType t = static_cast<SampleType>(s.data.index());
    s.data = makeSampleBuffer(t);
    auto [lo, hi] = makeBounds(t); s.minVal = lo; s.maxVal = hi;
    const int row = m_rowIndex.value(name, -1);
    if (row >= 0) emit dataChanged(index(row, 0), index(row, 0));
}

void ChartModel::clearAll()
{
    Q_ASSERT(thread() == QThread::currentThread());
    beginResetModel();
    m_data.clear();
    m_order.clear();
    m_rowIndex.clear();
    m_nextColorIndex = 0;
    m_cursor = -1;
    m_displayOriginSample = 0;
    endResetModel();
}

const ChartSeries *ChartModel::seriesByName(const QString &name) const
{
    auto it = m_data.constFind(name);
    return (it != m_data.constEnd()) ? &it.value() : nullptr;
}

const ChartSeries *ChartModel::series(int row) const
{
    if (row < 0 || row >= m_order.size()) return nullptr;
    auto it = m_data.constFind(m_order[row]);
    return (it != m_data.constEnd()) ? &it.value() : nullptr;
}

int ChartModel::rowOf(const QString &name) const
{
    return m_rowIndex.value(name, -1);
}

struct AppendResult { QString name; int row; int total; };

void ChartModel::appendBatch(const QList<SeriesBatch> &batch)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (batch.isEmpty()) return;
    QList<AppendResult> results;
    results.reserve(batch.size());
    for (const SeriesBatch &item : batch) {
        auto it = m_data.find(item.name);
        if (it == m_data.end()) continue;
        const int newTotal = appendToSeries(it.value(), item.samples);
        if (newTotal > 0) {
            const int row = m_rowIndex.value(item.name, -1);
            if (row >= 0) results.append({ item.name, row, newTotal });
        }
    }
    for (const auto &r : results) emit dataAppended(r.name, r.row, r.total);
}

int ChartModel::appendToSeries(ChartSeries &s, const SampleBuffer &src)
{
    if (sampleIsEmpty(src)) return 0;
    if (src.index() != s.data.index()) {
        if (sampleIsEmpty(s.data)) {
            const SampleType t = static_cast<SampleType>(src.index());
            s.data = makeSampleBuffer(t);
            auto [lo, hi] = makeBounds(t); s.minVal = lo; s.maxVal = hi;
        } else {
            qWarning("ChartModel::appendToSeries: type mismatch for '%s' — skipped",
                     qPrintable(s.name));
            return 0;
        }
    }
    return std::visit([&s](const auto &srcVec) -> int {
        using T = std::decay_t<decltype(srcVec[0])>;
        auto &dstVec = std::get<QVector<T>>(s.data);
        if (dstVec.capacity() < dstVec.size() + srcVec.size())
            dstVec.reserve(qMax(dstVec.size() + srcVec.size(), dstVec.capacity() * 2));
        if constexpr (std::is_same_v<T, int64_t>) {
            auto &lo = std::get<int64_t>(s.minVal); auto &hi = std::get<int64_t>(s.maxVal);
            for (int64_t v : srcVec) { if (v < lo) lo = v; if (v > hi) hi = v; }
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            auto &lo = std::get<uint64_t>(s.minVal); auto &hi = std::get<uint64_t>(s.maxVal);
            for (uint64_t v : srcVec) { if (v < lo) lo = v; if (v > hi) hi = v; }
        } else {
            auto &lo = std::get<double>(s.minVal); auto &hi = std::get<double>(s.maxVal);
            for (const T &v : srcVec) {
                const double dv = static_cast<double>(v);
                if (dv < lo) lo = dv; if (dv > hi) hi = dv;
            }
        }
        dstVec.append(srcVec);
        return dstVec.size();
    }, src);
}

// =============================================================================
//  Display parameter setters
// =============================================================================

void ChartModel::setPixelsPerSample(float pps)
{
    Q_ASSERT(thread() == QThread::currentThread());
    pps = qBound(kMinPps, pps, kMaxPps);
    if (qFuzzyCompare(pps, m_pps)) return;
    m_pps = pps;
    emit layoutChanged();
}

void ChartModel::setPpsQuiet(float pps)
{
    Q_ASSERT(thread() == QThread::currentThread());
    m_pps = qBound(kMinPps, pps, kMaxPps);
}

void ChartModel::setRowHeight(int px)
{
    Q_ASSERT(thread() == QThread::currentThread());
    px = qMax(kMinRowHeight, px);
    if (px == m_defaultRowHeight) return;
    m_defaultRowHeight = px;
    for (auto &s : m_data) {
        if (s.minRowHeight > 0 && s.minRowHeight == s.maxRowHeight) continue;
        s.rowHeight = qBound(
            s.minRowHeight > 0 ? s.minRowHeight : px,
            px,
            s.maxRowHeight > s.minRowHeight ? s.maxRowHeight : s.minRowHeight + 1);
    }
    emit layoutChanged();
}

void ChartModel::setHeaderWidth(int px)
{
    Q_ASSERT(thread() == QThread::currentThread());
    px = qMax(kMinHeaderWidth, px);
    if (px == m_headerWidth) return;
    m_headerWidth = px;
    emit layoutChanged();
}

void ChartModel::setSeriesRowHeight(const QString &name, int px)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto it = m_data.find(name);
    if (it == m_data.end()) return;
    const int lo      = it->minRowHeight > 0 ? it->minRowHeight : 1;
    const int hi      = it->maxRowHeight < INT_MAX ? it->maxRowHeight : INT_MAX;
    const int clamped = qBound(lo, px, hi);
    if (it->rowHeight == clamped) return;
    it->rowHeight = clamped;
    const int row = m_rowIndex.value(name, -1);
    if (row >= 0) emit seriesRowHeightChanged(name, row, clamped);
}

void ChartModel::setSeriesViewRange(const QString &name, double lo, double hi)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto it = m_data.find(name);
    if (it == m_data.end()) return;
    // Reject inverted or degenerate finite ranges — fall back to auto mode.
    if (!qIsNaN(lo) && !qIsNaN(hi) && hi <= lo) {
        qWarning("ChartModel::setSeriesViewRange(\"%s\"): hi (%g) <= lo (%g) — resetting to auto",
                 qPrintable(name), hi, lo);
        lo = qQNaN();
        hi = qQNaN();
    }
    // Guard against no-op (both NaN → both NaN is also a no-op).
    const bool sameNaN = qIsNaN(it->viewLo) && qIsNaN(lo) &&
                         qIsNaN(it->viewHi) && qIsNaN(hi);
    const bool sameVal = it->viewLo == lo && it->viewHi == hi;
    if (sameNaN || sameVal) return;
    it->viewLo = lo;
    it->viewHi = hi;
    const int row = m_rowIndex.value(name, -1);
    if (row >= 0) {
        emit dataChanged(index(row, 0), index(row, 0));
        emit seriesDisplayChanged(name, row);
    }
}

void ChartModel::resetSeriesView(const QString &name)
{
    Q_ASSERT(thread() == QThread::currentThread());
    setSeriesViewRange(name, qQNaN(), qQNaN());
}

void ChartModel::resetAllDisplayParams()
{
    Q_ASSERT(thread() == QThread::currentThread());
    for (auto &s : m_data) {
        s.viewLo = qQNaN();
        s.viewHi = qQNaN();
    }
    if (!m_order.isEmpty())
        emit dataChanged(index(0, 0), index(m_order.size()-1, 0));
}

int ChartModel::chartPixelWidth() const
{
    const int visibleSamples = qMax(0, maxSampleCount() - m_displayOriginSample);
    return qRound(visibleSamples * static_cast<double>(m_pps));
}

void ChartModel::setDisplayOriginSample(int sampleIndex)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const int clamped = qMax(0, sampleIndex);
    if (m_displayOriginSample == clamped) return;
    m_displayOriginSample = clamped;
    if (!m_order.isEmpty())
        emit dataChanged(index(0, 0), index(m_order.size() - 1, 0));
}

int ChartModel::maxSampleCount() const
{
    int mx = 0;
    for (const auto &s : m_data) mx = qMax(mx, sampleCount(s.data));
    return mx;
}

void ChartModel::setCursorSample(int sampleIndex)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_cursor == sampleIndex) return;
    m_cursor = sampleIndex;
    emit cursorMoved(sampleIndex);
}

int  ChartModel::rowCount   (const QModelIndex &) const { return m_order.size(); }
int  ChartModel::columnCount(const QModelIndex &) const { return 1; }

QVariant ChartModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid()) return {};
    if (idx.row() >= m_order.size()) return {};
    auto it = m_data.constFind(m_order[idx.row()]);
    const ChartSeries *s = (it != m_data.constEnd()) ? &it.value() : nullptr;
    switch (role) {
    case SeriesPointerRole: return s ? QVariant::fromValue(static_cast<const void*>(s)) : QVariant{};
    case CursorSampleRole:  return m_cursor;
    case PpsRole:           return m_pps;
    default:                return {};
    }
}
