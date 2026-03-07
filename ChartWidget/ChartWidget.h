// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CHARTWIDGET_H
#define CHARTWIDGET_H

#include "ChartHeaderView.h"
#include "ChartModel.h"
#include "ChartTheme.h"
#include "ChartView.h"

#include <QWidget>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <optional>

class QAction;
class QToolBar;

/**
 * @brief Top-level composite widget that assembles all Chart sub-components.
 *
 * ChartWidget owns and wires together:
 *  - ChartModel   — the shared data model
 *  - ChartView    — the scrollable plot area (QTableView)
 *  - ChartHeaderView — the vertical row-label panel
 *  - A QToolBar   — X/Y zoom buttons, Auto Y toggle, Reset, timestamp label
 *
 * ### Theme management
 * On construction ChartWidget applies a palette to the entire application
 * via ChartTheme::applyToApplication().  The palette is re-applied
 * automatically when the OS colour scheme changes (QEvent::ApplicationPaletteChange).
 * Call setTheme() to override the automatic selection.
 *
 * ### Usage
 * @code
 *   auto *chart = new ChartWidget(parent);
 *   chart->model()->addSeries("Signal", Qt::cyan, SampleType::Float32);
 *   chart->model()->appendData("Signal", samples);
 * @endcode
 */
class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChartWidget(QWidget *parent = nullptr);

    /** @brief Returns the shared data model. */
    ChartModel      *model()      { return m_model;      }

    /** @brief Returns the plot view. */
    ChartView       *view()       { return m_view;       }

    /** @brief Returns the row-label header view. */
    ChartHeaderView *headerView() { return m_headerView; }

    /**
     * @brief Overrides automatic OS theme detection.
     *
     * Applies the selected palette to the entire application immediately.
     * @param v  Desired colour scheme variant.
     */
    void setTheme(ChartTheme::Variant v);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent  (QShowEvent   *e) override;

    /**
     * @brief Re-applies the current theme when the OS colour scheme changes.
     *
     * Reacts to QEvent::ApplicationPaletteChange and QEvent::PaletteChange.
     * On Windows these events are unreliable; theme detection is also done
     * via QStyleHints::colorSchemeChanged (Qt >= 6.5) and nativeEvent
     * (WM_SETTINGCHANGE / WM_THEMECHANGED) as fallbacks.
     */
    void changeEvent(QEvent *e) override;

#if defined(Q_OS_WIN)
    /**
     * @brief Catches WM_SETTINGCHANGE and WM_THEMECHANGED on Windows.
     *
     * Fallback for Qt < 6.5 where QStyleHints::colorSchemeChanged is not
     * available and ApplicationPaletteChange is not reliably delivered.
     */
    bool nativeEvent(const QByteArray &eventType,
                     void *message, qintptr *result) override;
#endif

private slots:
    /** @brief Updates the timestamp label in the toolbar when the cursor moves. */
    void onCursorMoved(int sample);

private:
    /** @brief Shows or hides the timestamp and hint labels based on available width. */
    void updateToolbarLayout();

    /**
     * @brief Applies the palette for the current (or overridden) theme variant.
     *
     * Calls ChartTheme::applyToApplication(), which sets both
     * QApplication::setPalette() and the global scroll bar style sheet.
     */
    void applyCurrentTheme();

    ChartModel      *m_model      = nullptr;
    ChartView       *m_view       = nullptr;
    ChartHeaderView *m_headerView = nullptr;

    QToolBar *m_tb         = nullptr;
    QWidget  *m_tbSpacer   = nullptr;
    QLabel   *m_tsLabel    = nullptr;
    QLabel   *m_hintsLabel = nullptr;

    QAction  *m_actYsIn  = nullptr; ///< "Zoom Y scale in" for selected rows
    QAction  *m_actYsOut = nullptr; ///< "Zoom Y scale out" for selected rows

    /** @brief Explicit theme override; std::nullopt = auto-detect from OS. */
    std::optional<ChartTheme::Variant> m_themeOverride;

    /** @brief Guards against re-entrant applyCurrentTheme() calls.
     *
     *  QApplication::setPalette() emits ApplicationPaletteChange, which
     *  delivers changeEvent() again while we are still inside applyCurrentTheme().
     *  This flag breaks the cycle.
     */
    bool m_applyingTheme = false;
};

#endif // CHARTWIDGET_H
