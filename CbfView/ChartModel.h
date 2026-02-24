#ifndef CHARTMODEL_H
#define CHARTMODEL_H

#include "ChartDefs.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QReadWriteLock>
#include <atomic>

class ChartModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Roles {
        SeriesPointerRole = Qt::UserRole + 1,
        CursorSampleRole,
        PpsRole,
    };

    explicit ChartModel(QObject *parent = nullptr);

    QString addSeries(const QString &name,
                      const QColor  &color      = Qt::green,
                      SampleType     sampleType = SampleType::Float32);

    template<typename T>
    void appendData(const QString &name, const QVector<T> &samples);

    void appendBatch(const QList<SeriesBatch> &batch);
    void clearSeries(const QString &name);

    const ChartSeries *seriesByName(const QString &name) const;
    const ChartSeries *series(int row) const;
    int                rowOf(const QString &name) const;

    void  setPixelsPerSample(float pps);
    void  setPpsQuiet(float pps);
    float pixelsPerSample() const { return m_pps; }

    void setRowHeight(int px);
    int  rowHeight()   const { return m_defaultRowHeight; }

    void setHeaderWidth(int px);
    int  headerWidth() const { return m_headerWidth; }

    int  chartPixelWidth() const;
    int  maxSampleCount()  const;

    void setSeriesRowHeight(const QString &name, int px);
    void setSeriesYScale   (const QString &name, float scale);
    void setSeriesYOffset  (const QString &name, int   px);

    void resetAllDisplayParams();

    void setCursorSample(int sampleIndex);
    int  cursorSample() const { return m_cursor.load(std::memory_order_relaxed); }

    // ── QAbstractTableModel ──────────────────────────────────────────────────

    int      rowCount   (const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;

public slots:
    void zoomXIn()  { setPixelsPerSample(m_pps * 1.5f); }
    void zoomXOut() { setPixelsPerSample(m_pps / 1.5f); }
    void zoomYIn()  { setRowHeight(static_cast<int>(m_defaultRowHeight * 1.3f)); }
    void zoomYOut() { setRowHeight(static_cast<int>(m_defaultRowHeight / 1.3f)); }

signals:
    void cursorMoved(int sampleIndex);
    void dataAppended(const QString &name, int row, int total);
    void seriesDisplayChanged(const QString &name, int row);

private:
    int appendToSeries(ChartSeries &s, const SampleBuffer &src);

    mutable QReadWriteLock      m_lock;
    QHash<QString, ChartSeries> m_data;
    QVector<QString>            m_order;
    QHash<QString, int>         m_rowIndex;

    float m_pps              = 2.0f;
    int   m_defaultRowHeight = 90;
    int   m_headerWidth      = 170;

    std::atomic<int> m_cursor{-1};
};

// ─── template appendData ──────────────────────────────────────────────────────

template<typename T>
void ChartModel::appendData(const QString &name, const QVector<T> &samples)
{
    if (samples.isEmpty()) return;
    int row = -1, newTotal = 0;
    {
        QWriteLocker lk(&m_lock);
        auto it = m_data.find(name);
        if (it == m_data.end()) return;
        ChartSeries &s = it.value();

        if (!std::holds_alternative<QVector<T>>(s.data)) {
            if (sampleIsEmpty(s.data)) { s.data = QVector<T>{}; }
            else {
                qFatal("ChartModel::appendData: type mismatch for '%s'", qPrintable(name));
                return;
            }
        }

        auto &vec = std::get<QVector<T>>(s.data);
        if (vec.capacity() < vec.size() + samples.size())
            vec.reserve(qMax(vec.size() + samples.size(), vec.capacity() * 2));

        if constexpr (std::is_same_v<T, int64_t>) {
            auto &lo = std::get<int64_t>(s.minVal); auto &hi = std::get<int64_t>(s.maxVal);
            for (int64_t v : samples) { if (v < lo) lo = v; if (v > hi) hi = v; }
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            auto &lo = std::get<uint64_t>(s.minVal); auto &hi = std::get<uint64_t>(s.maxVal);
            for (uint64_t v : samples) { if (v < lo) lo = v; if (v > hi) hi = v; }
        } else {
            auto &lo = std::get<double>(s.minVal); auto &hi = std::get<double>(s.maxVal);
            for (const T &v : samples) {
                const double dv = static_cast<double>(v);
                if (dv < lo) lo = dv; if (dv > hi) hi = dv;
            }
        }
        vec.append(samples);
        newTotal = vec.size();
        row = m_rowIndex.value(name, -1);
    }
    if (row >= 0) emit dataAppended(name, row, newTotal);
}

#endif // CHARTMODEL_H
