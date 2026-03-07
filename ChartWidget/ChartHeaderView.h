// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CHARTHEADERVIEW_H
#define CHARTHEADERVIEW_H

#include "ChartModel.h"

#include <QHeaderView>
#include <QPalette>
#include <QString>
#include <QSet>

/**
 * @brief Vertical header panel displayed to the left of ChartView.
 *
 * Each section corresponds to one ChartSeries row and shows:
 *  - a colour bar identifying the series,
 *  - the series name and sample type,
 *  - the current value at the cursor sample,
 *  - the view range annotation when Y bounds are explicitly set.
 *
 * Interactive features:
 *  - Row height resize (drag the bottom grip of any section).
 *  - Header width resize (drag the right edge grip).
 *  - Row selection via left-click / Ctrl+click.
 *  - Context menu with Y-fit, scale-sync, overlay and reset actions.
 */
class ChartHeaderView : public QHeaderView
{
    Q_OBJECT
public:
    /**
     * @brief Constructs the header view.
     * @param model  Shared data model (must outlive this object).
     * @param parent Optional parent widget.
     */
    explicit ChartHeaderView(ChartModel *model, QWidget *parent = nullptr);

    /** @brief Returns the set of currently selected row indices. */
    QSet<int> selectedRows() const { return m_selectedRows; }

    /**
     * @brief Formats a Unix epoch timestamp as a two-line string.
     *
     * Line 1: date (yyyy-MM-dd)
     * Line 2: time with milliseconds and UTC offset (HH:mm:ss.mmm ±hh:mm)
     *
     * @param epochSec  Seconds since the Unix epoch (1970-01-01T00:00:00Z).
     * @return Formatted string, or "—" for invalid/non-finite input.
     */
    static QString formatTimestamp(double epochSec);

    /**
     * @brief Same as formatTimestamp() but with the newline replaced by a space.
     *
     * Suitable for single-line labels such as the toolbar timestamp widget.
     */
    static QString formatTimestampLine(double epochSec);

    /**
     * @brief Keeps the "Auto Y" button state in sync with ChartView.
     * @param on  @c true when auto-fit is active.
     */
    void setAutoFitY(bool on);

signals:
    /**
     * @brief Emitted whenever the set of selected rows changes.
     * @param rows  The new selection set (may be empty).
     */
    void selectionChanged(const QSet<int> &rows);

    /** @brief Emitted when the user requests scale synchronisation. */
    void syncScaleRequested(int sourceRow, QSet<int> rows);

    /** @brief Emitted when the user requests overlaying multiple series. */
    void overlayRequested(int sourceRow, QSet<int> rows);

    /** @brief Emitted when the user requests resetting selected row parameters. */
    void resetSelectedRequested(QSet<int> rows);

    /** @brief Emitted when the user clicks "Fit Y to visible". */
    void fitYToVisibleRequested();

    /** @brief Emitted when the user toggles "Auto Y". */
    void autoFitYToggleRequested();

protected:
    void paintEvent       (QPaintEvent       *e) override;
    void mouseMoveEvent   (QMouseEvent       *e) override;
    void mousePressEvent  (QMouseEvent       *e) override;
    void mouseReleaseEvent(QMouseEvent       *e) override;
    void contextMenuEvent (QContextMenuEvent *e) override;
    void leaveEvent       (QEvent            *e) override;

private slots:
    void onCursorMoved  (int sample);
    void onLayoutChanged();

private:
    /**
     * @brief Paints a single header row.
     *
     * The QPalette is passed explicitly to avoid calling palette() on
     * every row during a repaint.
     *
     * @param p        Active painter.
     * @param pal      Current application palette.
     * @param r        Row rectangle in viewport coordinates.
     * @param s        Series data and display parameters.
     * @param cursor   Current cursor sample index, or -1.
     * @param selected Whether this row is currently selected.
     */
    void paintRow(QPainter *p, const QPalette &pal, const QRect &r,
                  const ChartSeries &s, int cursor, bool selected) const;

    static constexpr int kResizeZone       = 5;   ///< Row-height resize hit zone (px)
    static constexpr int kHeaderResizeZone = 6;   ///< Header-width resize hit zone (px)
    static constexpr int kHeaderMinWidth   = 80;  ///< Minimum header width (px)
    static constexpr int kHeaderMaxWidth   = 500; ///< Maximum header width (px)

    /** @brief Returns the row index whose bottom edge is within kResizeZone of @p y. */
    int rowAtResizeHandle(int y) const;

    /** @brief Returns the row index at viewport y-coordinate @p y. */
    int rowAt(int y) const;

    ChartModel *m_model; ///< Non-owning pointer to the shared data model.

    // --- Row resize state ----------------------------------------------------
    bool m_resizing     = false;
    int  m_resizeRow    = -1;
    int  m_resizePressY = 0;
    int  m_resizeStartH = 0;

    // --- Header-width resize state -------------------------------------------
    bool m_headerResizing     = false;
    int  m_headerResizePressX = 0;
    int  m_headerResizeStartW = 0;

    // --- Selection state -----------------------------------------------------
    QSet<int> m_selectedRows;
    int       m_lastClickedRow = -1;

    bool m_autoFitY = false; ///< Mirrors the Auto Y state of ChartView.
};

#endif // CHARTHEADERVIEW_H
