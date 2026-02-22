#ifndef CHARTMODEL_H
#define CHARTMODEL_H

#include "ChartDefs.h"

#include <QAbstractTableModel>
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

    int  addSeries(const QString &name,
                  const QColor &color     = Qt::green,
                  SampleType    sampleType = SampleType::Float32);

    template<typename T>
    void appendData(int idx, const QVector<T> &samples);

    void clearSeries(int idx);
    const ChartSeries *series(int row) const;

    void  setPixelsPerSample(float pps);
    void  setPpsQuiet(float pps);
    float pixelsPerSample() const { return m_pps; }

    void setRowHeight(int px);
    int  rowHeight()        const { return m_rowHeight; }

    void setHeaderWidth(int px);
    int  headerWidth()      const { return m_headerWidth; }

    int  chartPixelWidth()  const;
    int  maxSampleCount()   const;

    void setCursorSample(int sampleIndex);
    int  cursorSample() const { return m_cursor.load(std::memory_order_relaxed); }

    int      rowCount   (const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;

signals:
    void cursorMoved(int sampleIndex);
    void dataAppended(int row, int newTotalSamples);

private:
    mutable QReadWriteLock m_lock;
    QVector<ChartSeries>   m_series;

    float m_pps         = 2.0f;
    int   m_rowHeight   = 90;
    int   m_headerWidth = 170;

    std::atomic<int> m_cursor{-1};
};

// ─── template appendData ──────────────────────────────────────────────────────

template<typename T>
void ChartModel::appendData(int idx, const QVector<T> &samples)
{
    if (samples.isEmpty()) return;
    int newTotal = 0;
    {
        QWriteLocker lk(&m_lock);
        if (idx < 0 || idx >= m_series.size()) return;
        ChartSeries &s = m_series[idx];

        if (!std::holds_alternative<QVector<T>>(s.data)) {
            if (sampleIsEmpty(s.data)) {
                s.data = QVector<T>{};
            } else {
                qFatal("ChartModel::appendData: type mismatch for series %d "
                       "(buffer holds '%s', appending '%s')",
                       idx, sampleTypeName(s.data), typeid(T).name());
                return;
            }
        }

        auto &vec = std::get<QVector<T>>(s.data);
        if (vec.capacity() < vec.size() + samples.size())
            vec.reserve(qMax(vec.size() + samples.size(), vec.capacity() * 2));

        // Обновляем bounds в нативном типе:
        //   int64_t  → BoundsValue хранит int64_t  (нет потери точности)
        //   uint64_t → BoundsValue хранит uint64_t (нет потери точности)
        //   все остальные → double (53 бит мантиссы достаточно для int8..int32, float)
        if constexpr (std::is_same_v<T, int64_t>) {
            auto &lo = std::get<int64_t>(s.minVal);
            auto &hi = std::get<int64_t>(s.maxVal);
            for (int64_t v : samples) {
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            auto &lo = std::get<uint64_t>(s.minVal);
            auto &hi = std::get<uint64_t>(s.maxVal);
            for (uint64_t v : samples) {
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        } else {
            auto &lo = std::get<double>(s.minVal);
            auto &hi = std::get<double>(s.maxVal);
            for (const T &v : samples) {
                const double dv = static_cast<double>(v);
                if (dv < lo) lo = dv;
                if (dv > hi) hi = dv;
            }
        }

        vec.append(samples);
        newTotal = vec.size();
    }
    emit dataAppended(idx, newTotal);
}

#endif // CHARTMODEL_H
