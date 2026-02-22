#include "ChartModel.h"

ChartModel::ChartModel(QObject *parent) : QAbstractTableModel(parent) {}

int ChartModel::addSeries(const QString &name, const QColor &color, SampleType sampleType)
{
    ChartSeries s;
    s.name  = name;
    s.color = color;
    s.data  = makeSampleBuffer(sampleType);
    auto [lo, hi] = makeBounds(sampleType);
    s.minVal = lo;
    s.maxVal = hi;

    int row;
    { QReadLocker lk(&m_lock); row = m_series.size(); }

    beginInsertRows({}, row, row);
    { QWriteLocker lk(&m_lock); m_series.append(s); }
    endInsertRows();
    return row;
}

void ChartModel::clearSeries(int idx)
{
    {
        QWriteLocker lk(&m_lock);
        if (idx < 0 || idx >= m_series.size()) return;
        ChartSeries &s = m_series[idx];
        const SampleType t = static_cast<SampleType>(s.data.index());
        s.data = makeSampleBuffer(t);
        auto [lo, hi] = makeBounds(t);
        s.minVal = lo;
        s.maxVal = hi;
    }
    emit dataChanged(index(idx, 0), index(idx, 0));
}

const ChartSeries *ChartModel::series(int row) const
{
    return (row >= 0 && row < m_series.size()) ? &m_series[row] : nullptr;
}

void ChartModel::setPixelsPerSample(float pps)
{
    pps = qBound(0.01f, pps, 200.f);
    if (qFuzzyCompare(pps, m_pps)) return;
    m_pps = pps;
    emit layoutChanged();
}

void ChartModel::setPpsQuiet(float pps)
{
    m_pps = qBound(0.01f, pps, 200.f);
}

void ChartModel::setRowHeight(int px)
{
    px = qMax(30, px);
    if (px == m_rowHeight) return;
    m_rowHeight = px;
    emit layoutChanged();
}

void ChartModel::setHeaderWidth(int px)
{
    px = qMax(80, px);
    if (px == m_headerWidth) return;
    m_headerWidth = px;
    emit layoutChanged();
}

int ChartModel::chartPixelWidth() const
{
    return qRound(maxSampleCount() * static_cast<double>(m_pps));
}

int ChartModel::maxSampleCount() const
{
    QReadLocker lk(&m_lock);
    int mx = 0;
    for (const auto &s : m_series)
        mx = qMax(mx, sampleCount(s.data));
    return mx;
}

void ChartModel::setCursorSample(int sampleIndex)
{
    const int prev = m_cursor.exchange(sampleIndex, std::memory_order_relaxed);
    if (prev == sampleIndex) return;

    QReadLocker lk(&m_lock);
    if (!m_series.isEmpty())
        emit dataChanged(index(0, 0), index(m_series.size() - 1, 0), {CursorSampleRole});
    emit cursorMoved(sampleIndex);
}

int  ChartModel::rowCount   (const QModelIndex &) const { QReadLocker lk(&m_lock); return m_series.size(); }
int  ChartModel::columnCount(const QModelIndex &) const { return 1; }

QVariant ChartModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid()) return {};
    QReadLocker lk(&m_lock);
    if (idx.row() >= m_series.size()) return {};

    switch (role) {
    case SeriesPointerRole:
        return QVariant::fromValue(static_cast<const void *>(&m_series[idx.row()]));
    case CursorSampleRole:
        return m_cursor.load(std::memory_order_relaxed);
    case PpsRole:
        return m_pps;
    default:
        return {};
    }
}
