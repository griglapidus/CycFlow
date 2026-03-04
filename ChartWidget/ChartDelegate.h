// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CHARTDELEGATE_H
#define CHARTDELEGATE_H

#include "ChartModel.h"
#include <QStyledItemDelegate>

/**
 * @brief Delegate responsible for rendering chart cells inside ChartView.
 *
 * Rendering is split into two passes to avoid overdraw artefacts:
 *  - Pass 1 (paint()): fills the background and draws the Y-axis grid
 *    with value labels.  Called by QTableView's standard paint pipeline.
 *  - Pass 2 (paintData()): draws the signal polyline, sample dots and
 *    the cursor marker.  Called explicitly from ChartView::paintEvent()
 *    after the base class pass completes.
 *
 * All colours are derived from the current QPalette so the delegate
 * adapts automatically to both dark and light themes.
 */
class ChartDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @brief Constructs the delegate.
     * @param model  The data model shared with ChartView.
     * @param parent Optional parent object.
     */
    explicit ChartDelegate(ChartModel *model, QObject *parent = nullptr);

    /// @brief Pass 1 – background fill and Y grid (called by QTableView).
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    /// @brief Returns the preferred cell size for the given index.
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    /**
     * @brief Pass 2 – signal polyline, sample dots and cursor marker.
     *
     * @param p          Painter targeting the viewport.
     * @param cell       Cell rectangle in content (non-scrolled) coordinates.
     * @param s          Series data and display parameters.
     * @param cursor     Current cursor sample index, or -1 if none.
     * @param pps        Pixels per sample (horizontal zoom).
     * @param clipXLeft  Left edge of the visible clip region (content coords).
     * @param clipXRight Right edge of the visible clip region (content coords).
     */
    void paintData(QPainter *p, const QRect &cell,
                   const ChartSeries &s, int cursor, float pps,
                   int clipXLeft, int clipXRight) const;

private:
    /// @brief Fills background, draws divider line and Y-axis grid with labels.
    void paintBackground(QPainter *p, const QRect &r, const ChartSeries &s) const;

    /// @brief Core polyline and cursor rendering implementation.
    void paintDataImpl(QPainter *p, const QRect &cell, const ChartSeries &s,
                       int cursor, float pps, int clipXLeft, int clipXRight) const;

    ChartModel *m_model; ///< Non-owning pointer to the shared data model.
};

#endif // CHARTDELEGATE_H
