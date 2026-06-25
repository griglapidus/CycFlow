// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CHARTMODEL_H
#define CHARTMODEL_H

#include "ChartDefs.h"
#include "ChartTheme.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QThread>

/**
 * @brief Central data model for the Chart widget system.
 *
 * ChartModel stores all series data, cursor state and display parameters
 * and exposes them through the QAbstractTableModel interface.  It is
 * designed for thread-safe appending from a background I/O thread while
 * the UI thread reads data for rendering.
 *
 * ### Roles
 * | Role                | Type            | Description                         |
 * |---------------------|-----------------|-------------------------------------|
 * | SeriesPointerRole   | const void *    | Read-only pointer to a ChartSeries  |
 * | CursorSampleRole    | int             | Current cursor sample index         |
 * | PpsRole             | float           | Current pixels-per-sample value     |
 *
 * ### Color management
 * When @p color is omitted (or an invalid QColor is passed) in addSeries(),
 * the model auto-assigns the next available slot from the ChartTheme series
 * color palette and stores the index in ChartSeries::colorIndex.
 * Calling reapplySeriesColors() after a theme switch re-resolves all
 * auto-assigned colors to the new variant without touching manually set ones.
 *
 * ### Thread safety — UI-only contract
 * All mutating methods (addSeries, appendBatch, appendData, clearSeries,
 * clearAll, setSeriesViewRange, setSeriesRowHeight, setCursorSample,
 * setRowHeight, setPixelsPerSample, setHeaderWidth, resetSeriesView,
 * resetAllDisplayParams, reapplySeriesColors) MUST be called on the UI
 * (model's) thread.  Calling them from a worker thread is a contract
 * violation: Debug builds will assert, Release builds invoke UB.
 *
 * To feed data from a worker thread, connect the worker's signal to a
 * model slot with Qt::QueuedConnection — Qt's event loop will marshal
 * the call onto the UI thread automatically.  This is how CbfReader
 * (via moveToThread) and CycBufReceiver::ChartConsumer feed the model.
 *
 * Const accessors (rowCount, columnCount, data, series, seriesByName,
 * rowOf, chartPixelWidth, maxSampleCount, cursorSample, pixelsPerSample,
 * rowHeight, headerWidth) are likewise UI-thread-only — they read the
 * same state without locks.
 */
class ChartModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /** @brief Row display category used to compute min/max row heights. */
    enum class RowKind { Regular, Timestamp, Digital };

    /** @brief Fallback digital row height — actual value comes from computeLimits(). */
    static constexpr int kDefaultDigitalRowHeight = 24;

    // -------------------------------------------------------------------------
    //  Pixels-per-sample limits and default
    // -------------------------------------------------------------------------
    static constexpr float kMinPps     = 0.01f;  ///< Minimum allowed pps (extreme zoom-out)
    static constexpr float kMaxPps     = 200.0f; ///< Maximum allowed pps (extreme zoom-in)
    static constexpr float kDefaultPps = 2.0f;   ///< Initial pps on model construction

    // -------------------------------------------------------------------------
    //  Row-height limits and default
    // -------------------------------------------------------------------------
    static constexpr int kMinRowHeight     = 30; ///< Hard lower bound for analogue rows
    static constexpr int kDefaultRowHeight = 90; ///< Initial row height for analogue rows

    // -------------------------------------------------------------------------
    //  Header-panel width limits and default
    // -------------------------------------------------------------------------
    static constexpr int kMinHeaderWidth     = 80;  ///< Hard lower bound for the header panel
    static constexpr int kDefaultHeaderWidth = 170; ///< Initial header panel width

    // -------------------------------------------------------------------------
    //  Per-series display-parameter limits
    // -------------------------------------------------------------------------
    // (No per-series scale limits needed: view bounds are in value space.)

    // -------------------------------------------------------------------------
    //  Zoom step factors (multiplicative)
    // -------------------------------------------------------------------------
    static constexpr float kZoomXStep = 1.5f; ///< Factor applied by zoomXIn() / zoomXOut()
    static constexpr float kZoomYStep = 1.3f; ///< Factor applied by zoomYIn() / zoomYOut()

    enum Roles {
        SeriesPointerRole = Qt::UserRole + 1, ///< const ChartSeries *
        CursorSampleRole,                      ///< int — current cursor sample
        PpsRole,                               ///< float — pixels per sample
    };

    explicit ChartModel(QObject *parent = nullptr);

    /**
     * @brief Adds a new analogue series.
     *
     * If a series with the same name already exists the call is a no-op.
     *
     * @param name        Unique series identifier.
     * @param color       Plot colour.  Pass an invalid QColor (the default) to
     *                    have the model auto-assign a theme-aware color from the
     *                    ChartTheme palette.  Pass an explicit QColor to pin the
     *                    series to that color regardless of the active theme.
     * @param sampleType  Native storage type for samples.
     * @return The series name (can be used as a key in subsequent calls).
     */
    QString addSeries(const QString &name,
                      const QColor  &color      = QColor{},
                      SampleType     sampleType = SampleType::Float32);

    /**
     * @brief Adds a digital (bit) series with a fixed row height.
     *
     * Data values should be 0 or 1; rendered as a standard polyline at
     * a compact, non-resizable row height.
     *
     * @param bitName  Unique series identifier.
     * @param color    Plot colour.  An invalid QColor triggers auto-assignment.
     * @return The series name.
     */
    QString addDigitalSeries(const QString &bitName, const QColor &color = QColor{});

    /**
     * @brief Appends typed samples to a single series.
     *
     * @warning Must be called on the model's (UI) thread — see the
     * "Thread safety" section above. The method takes no lock; calling it
     * from a worker thread is a contract violation (Debug: assert,
     * Release: UB / data race on the series buffers).  Being a template,
     * it cannot be a queued slot; to feed data from a worker thread, build
     * a SeriesBatch and deliver it to appendBatch() via a queued connection.
     *
     * @tparam T  Must match the type used when the series was created.
     */
    template<typename T>
    void appendData(const QString &name, const QVector<T> &samples);

    /**
     * @brief Appends samples to multiple series in a single call.
     *
     * Preferred over repeated appendData() calls when updating several
     * series at once.
     *
     * @warning Must be called on the model's (UI) thread (see the
     * "Thread safety" section above); it takes no lock.  This is the
     * designated cross-thread entry point: connect a worker signal carrying
     * QList<SeriesBatch> to this slot with Qt::QueuedConnection so Qt's
     * event loop marshals the call onto the UI thread.
     */
    void appendBatch(const QList<SeriesBatch> &batch);

    /** @brief Clears all samples for the named series, preserving its metadata. */
    void clearSeries(const QString &name);

    /** @brief Removes all series and resets the model. */
    void clearAll();

    /** @brief Returns a read-only pointer to the named series, or nullptr. */
    const ChartSeries *seriesByName(const QString &name) const;

    /** @brief Returns a read-only pointer to the series at @p row, or nullptr. */
    const ChartSeries *series(int row) const;

    /** @brief Returns the row index of the named series, or -1. */
    int rowOf(const QString &name) const;

    /** @brief Sets the horizontal zoom level in pixels per sample. Clamped to [kMinPps, kMaxPps]. */
    void  setPixelsPerSample(float pps);

    /** @brief Sets pps without emitting layoutChanged (use during X-zoom-scroll). */
    void  setPpsQuiet(float pps);

    /** @brief Current pixels-per-sample value. */
    float pixelsPerSample() const { return m_pps; }

    /** @brief Sets the default row height for analogue series. Clamped to [kMinRowHeight, ∞). */
    void setRowHeight(int px);

    /** @brief Current default row height. */
    int rowHeight() const { return m_defaultRowHeight; }

    /** @brief Sets the width of the ChartHeaderView panel in pixels. */
    void setHeaderWidth(int px);

    /** @brief Current header panel width. */
    int headerWidth() const { return m_headerWidth; }

    /**
     * @brief Returns the total content width in pixels for the currently
     *        visible sample window.
     *
     * Equals @c (maxSampleCount() − displayOriginSample()) × pixelsPerSample(),
     * rounded to int.  Because the window is always advanced (see
     * setDisplayOriginSample()) to keep its pixel width below QHeaderView's
     * internal section-size limit, the result always fits in @c int.
     */
    int chartPixelWidth() const;

    /** @brief Returns the maximum sample count across all series. */
    int maxSampleCount() const;

    /**
     * @brief Sample index that is rendered at content x = 0.
     *
     * Used as the rendering origin: sample @p i is drawn at content x
     * @c (i − displayOriginSample()) × pixelsPerSample().  Combined with a
     * tail-aligned sliding window in ChartView, this lets Live mode follow
     * arbitrarily long records without hitting QHeaderView's section-size
     * cap, which would otherwise stop the background from being drawn.
     */
    int displayOriginSample() const { return m_displayOriginSample; }

    /**
     * @brief Sets the rendering origin sample index.
     *
     * Triggers a model-wide repaint (via dataChanged with all rows) because
     * every visible point's content-x coordinate depends on it.
     */
    void setDisplayOriginSample(int sampleIndex);

    /** @brief Overrides the row height for a single series. */
    void setSeriesRowHeight(const QString &name, int px);

    /**
     * @brief Sets the explicit Y-axis view bounds for a single series.
     *
     * After this call, rendering uses [lo, hi] directly and is unaffected
     * by new data arriving (minVal/maxVal continue to update but do not
     * move the view).  Pass lo >= hi to reset to auto mode.
     *
     * @param lo  Lower Y bound (value at the bottom of the chart area).
     * @param hi  Upper Y bound (value at the top of the chart area).
     */
    void setSeriesViewRange(const QString &name, double lo, double hi);

    /**
     * @brief Resets viewLo/viewHi to NaN (auto mode) for the named series.
     *
     * In auto mode the view always spans the full running data range
     * [minVal, maxVal], which updates as new samples arrive.
     */
    void resetSeriesView(const QString &name);

    /** @brief Resets viewLo/viewHi to NaN for all series (auto mode). */
    void resetAllDisplayParams();

    /** @brief Moves the cursor to @p sampleIndex (-1 = no cursor). */
    void setCursorSample(int sampleIndex);

    /** @brief Returns the current cursor sample index (-1 = none). */
    int cursorSample() const { return m_cursor; }

    /**
     * @brief Re-resolves theme-managed series colors to the given variant.
     *
     * Should be called whenever the application theme changes (e.g. from
     * ChartWidget::applyCurrentTheme()).  Only series whose colorIndex !=
     * ChartSeries::kManualColor are updated; manually set colors are untouched.
     *
     * @param v  The newly active theme variant.
     */
    void reapplySeriesColors(ChartTheme::Variant v);

    // --- QAbstractTableModel -------------------------------------------------
    int      rowCount   (const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;

public slots:
    /// Increases X zoom by ×kZoomXStep.
    void zoomXIn()  { setPixelsPerSample(m_pps * kZoomXStep); }
    /// Decreases X zoom by ×kZoomXStep.
    void zoomXOut() { setPixelsPerSample(m_pps / kZoomXStep); }
    /// Increases row height by ×kZoomYStep.
    void zoomYIn()  { setRowHeight(static_cast<int>(m_defaultRowHeight * kZoomYStep)); }
    /// Decreases row height by ×kZoomYStep.
    void zoomYOut() { setRowHeight(static_cast<int>(m_defaultRowHeight / kZoomYStep)); }

signals:
    /** @brief Emitted when the cursor moves to a new sample index. */
    void cursorMoved(int sampleIndex);

    /**
     * @brief Emitted after samples are appended to a series.
     * @param name           Series name.
     * @param row            Row index of the series.
     * @param total          New total sample count.
     */
    void dataAppended(const QString &name, int row, int total);

    /**
     * @brief Emitted when the view range (viewLo/viewHi) of a series changes.
     * @param name  Series name.
     * @param row   Row index.
     */
    void seriesDisplayChanged(const QString &name, int row);

    /**
     * @brief Emitted when a single series row height changes via setSeriesRowHeight().
     * @param name  Series name.
     * @param row   Row index.
     * @param px    New height in pixels.
     */
    void seriesRowHeightChanged(const QString &name, int row, int px);

private:
    /**
     * @brief Appends samples from @p src to @p s and updates running bounds.
     * @return New total sample count, or 0 on type mismatch.
     */
    int appendToSeries(ChartSeries &s, const SampleBuffer &src);

    QHash<QString, ChartSeries> m_data;
    QVector<QString>            m_order;
    QHash<QString, int>         m_rowIndex;

    float m_pps                  = kDefaultPps;
    int   m_defaultRowHeight     = kDefaultRowHeight;
    int   m_headerWidth          = kDefaultHeaderWidth;
    int   m_displayOriginSample  = 0;  ///< sample drawn at content x = 0

    /// Counter incremented each time a series receives an auto-assigned color.
    int m_nextColorIndex = 0;

    int m_cursor = -1;
};

// =============================================================================
//  appendData — template implementation
// =============================================================================

template<typename T>
void ChartModel::appendData(const QString &name, const QVector<T> &samples)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (samples.isEmpty()) return;

    auto it = m_data.find(name);
    if (it == m_data.end()) return;
    ChartSeries &s = it.value();

    if (!std::holds_alternative<QVector<T>>(s.data)) {
        if (sampleIsEmpty(s.data)) { s.data = QVector<T>{}; }
        else {
            qWarning("ChartModel::appendData: type mismatch for '%s' — skipped", qPrintable(name));
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
    const int newTotal = vec.size();
    const int row      = m_rowIndex.value(name, -1);

    if (row >= 0) emit dataAppended(name, row, newTotal);
}

#endif // CHARTMODEL_H
