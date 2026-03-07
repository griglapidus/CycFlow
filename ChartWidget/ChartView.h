// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CHARTVIEW_H
#define CHARTVIEW_H

#include "ChartDelegate.h"
#include "ChartModel.h"

#include <QTableView>
#include <QFontMetrics>
#include <QSet>

/**
 * @brief Scrollable plot area that renders all ChartSeries rows.
 *
 * ChartView extends QTableView and uses a two-pass paint strategy:
 *  - Pass 1 (QTableView::paintEvent): draws cell backgrounds and Y-axis
 *    grids via ChartDelegate::paint().
 *  - Pass 2 (custom loop in paintEvent): draws signal polylines, sample
 *    dots and the cursor marker via ChartDelegate::paintData().
 *
 * Interaction:
 *  - Left-drag: pan the series vertically (shifts viewLo/viewHi in value space).
 *  - Right-drag: pan the chart horizontally.
 *  - Ctrl+Wheel: zoom X (pixels per sample).
 *  - Shift+Wheel: zoom Y scale of the row under the cursor.
 *  - Ctrl+Shift+Wheel: change row height.
 *  - Keys +/-, Up/Down, F, A, Escape: zoom and fit shortcuts.
 *
 * ChartHeaderView must be installed as the vertical header
 * (setVerticalHeader()) before setChartModel() is called.
 */
class ChartView : public QTableView
{
    Q_OBJECT
public:
    explicit ChartView(QWidget *parent = nullptr);

    /**
     * @brief Connects the view to a model and installs the delegate.
     *
     * Must be called after setVerticalHeader() to ensure correct layout.
     * @param model  Model to display (must outlive this view).
     */
    void setChartModel(ChartModel *model);

    /** @brief Forwards to ChartModel::setPixelsPerSample(). */
    void setPixelsPerSample(float pps);

    /** @brief Forwards to ChartModel::setRowHeight(). */
    void setRowHeight(int px);

    /** @brief Returns the number of samples currently visible in the viewport. */
    int visibleSampleCount() const;

signals:
    /**
     * @brief Emitted when the number of visible samples or the zoom level changes.
     * @param count  Number of visible samples.
     * @param pps    Current pixels-per-sample value.
     */
    void visibleSamplesChanged(int count, double pps);

    /**
     * @brief Emitted when the Auto Y state changes.
     * @param on  @c true when auto-fit is active.
     */
    void autoFitYChanged(bool on);

public slots:
    /** @brief Fits all series to the currently visible X range (disables Auto Y). */
    void fitYToVisible();

    /** @brief Toggles Auto Y on/off. */
    void toggleAutoFitY();

    /**
     * @brief Enables or disables continuous Y auto-fit.
     * @param on  @c true to enable.
     */
    void setAutoFitY(bool on);

    /** @brief Synchronises the Y scale of @p rows to match @p sourceRow. */
    void syncScale(int sourceRow, const QSet<int> &rows);

    /** @brief Overlays @p rows visually on top of @p sourceRow. */
    void overlayOnto(int sourceRow, const QSet<int> &rows);

    /** @brief Resets viewLo/viewHi to auto-mode for all rows in @p rows. */
    void resetSelected(const QSet<int> &rows);

protected:
    /// @brief Two-pass paint: base class draws backgrounds, then custom loop draws data.
    void paintEvent(QPaintEvent *e) override;

    /**
     * @brief Extends the dirty region after Qt's blit-scroll optimisation.
     *
     * On horizontal scroll Qt shifts the existing pixels and only marks
     * the newly exposed strip as dirty.  Y-axis labels are painted at
     * fixed viewport-edge positions by ChartDelegate, so after the blit
     * the old label pixels remain at their original (now wrong) positions.
     *
     * This override appends update() calls for the left and right label
     * strips, whose width is computed once and cached in m_gridLabelWidth.
     */
    void scrollContentsBy(int dx, int dy) override;

    void mouseMoveEvent   (QMouseEvent  *e) override;
    void mousePressEvent  (QMouseEvent  *e) override;
    void mouseReleaseEvent(QMouseEvent  *e) override;
    void leaveEvent       (QEvent       *e) override;
    void wheelEvent       (QWheelEvent  *e) override;
    void resizeEvent      (QResizeEvent *e) override;
    void keyPressEvent    (QKeyEvent    *e) override;

private slots:
    void onHScrollChanged(int value);
    void onCursorMoved(int sample);
    void onDataAppended(const QString &name, int row, int newTotalSamples);
    void flushPendingAppend();

private:
    void doAutoFitY();
    int  viewXToSample(int viewX) const;
    int  viewYToRow   (int viewY) const;
    void repaintCursorStrip(int oldSample, int newSample);
    void syncColumnWidth();
    void emitVisibleSamplesIfChanged();

    /**
     * @brief Computes the pixel width of the widest Y-axis grid label.
     *
     * Mirrors the label formatting logic of ChartDelegate::paintBackground()
     * without performing any actual painting.  Iterates only the vertically
     * visible rows, computes the same "nice" grid step, formats each label
     * string and measures it with QFontMetrics.
     *
     * Result is cached in m_gridLabelWidth and invalidated whenever the
     * Y range may have changed (layout change, series display change, reset).
     * The cache remains valid across horizontal scrolls.
     *
     * @return Max label width + 6 px edge margin.
     */
    int computeGridLabelWidth() const;

    /**
     * @brief Cached result of computeGridLabelWidth().
     * -1 means the cache is stale and must be recomputed on next use.
     */
    mutable int m_gridLabelWidth = -1;

    struct VisibleRange { double lo, hi; bool valid; };
    VisibleRange visibleRangeForRow(int row) const;

    ChartModel    *m_chartModel = nullptr;
    ChartDelegate *m_delegate   = nullptr;

    int m_prevCursorSample = -1;
    int m_lastVisibleCount = -1;

    // --- Deferred append flush -----------------------------------------------
    bool m_appendFlushPending = false;
    int  m_pendingOldSamples  = 0;
    int  m_pendingNewSamples  = 0;

    // --- Left-button Y-drag state --------------------------------------------
    bool    m_dragging          = false;
    int     m_dragStartY        = 0;
    double  m_dragStartViewLo   = 0.0; ///< viewLo at drag start (value space)
    double  m_dragStartViewHi   = 0.0; ///< viewHi at drag start (value space)
    int     m_dragRowHeight     = 0;   ///< rowHeight of the dragged series
    QString m_dragSeriesName;

    // --- Right-button horizontal pan state -----------------------------------
    bool m_panning        = false;
    int  m_panStartX      = 0;
    int  m_panStartScroll = 0;

    bool m_autoFitY = false;
};

#endif // CHARTVIEW_H
