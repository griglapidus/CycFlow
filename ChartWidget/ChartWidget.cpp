// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "ChartWidget.h"

#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#if defined(Q_OS_WIN)
#  include <windows.h>
#endif
#include <QLayout>
#include <QToolBar>
#include <QAction>
#include <QShowEvent>
#include <QFontMetrics>
#include <QToolButton>
#include <optional>
#include <QScrollBar>

namespace {

/// The vertical scroll bar single-step is set to totalContentHeight / kScrollStepDivisor.
/// A larger value produces a coarser step; a smaller value produces a finer one.
constexpr int kScrollStepDivisor = 30;

/// Toolbar action icon size (square, pixels).
constexpr int kToolbarIconSize = 40;

/// Toolbar font size (Consolas), in points.
constexpr int kToolbarFontPt = 9;

/// Toolbar button border radius (pixels).
constexpr int kToolbarBorderRadius = 3;

/// Extra horizontal padding added to the minimum timestamp label width (pixels).
constexpr int kTsLabelMinWidthPad = 20;

} // namespace

ChartWidget::ChartWidget(QWidget *parent) : QWidget(parent)
{
    m_model      = new ChartModel(this);
    m_view       = new ChartView(this);

    // ChartHeaderView is constructed without a parent — QTableView takes
    // ownership when setVerticalHeader() is called.
    m_headerView = new ChartHeaderView(m_model, nullptr);

    // Install the header before setChartModel() so that when setModel() runs
    // internally, Qt can correctly bind the header to the model and scroll bar.
    m_view->setVerticalHeader(m_headerView);
    m_view->setChartModel(m_model);

    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Adjust the vertical scroll step proportionally to the total content height.
    QObject::connect(m_view->verticalScrollBar(), &QScrollBar::rangeChanged,
                     m_view, [tableView = m_view]() {
                         int totalH = tableView->verticalHeader()->length();
                         if (totalH > 0) {
                             int step = qMax(1, totalH / kScrollStepDivisor);
                             tableView->verticalScrollBar()->setSingleStep(step);
                         }
                     });

    // --- Header → View signal forwarding ------------------------------------
    connect(m_headerView, &ChartHeaderView::syncScaleRequested,
            m_view,       &ChartView::syncScale);
    connect(m_headerView, &ChartHeaderView::overlayRequested,
            m_view,       &ChartView::overlayOnto);
    connect(m_headerView, &ChartHeaderView::resetSelectedRequested,
            m_view,       &ChartView::resetSelected);

    connect(m_headerView, &ChartHeaderView::fitYToVisibleRequested,
            m_view,       &ChartView::fitYToVisible);
    connect(m_headerView, &ChartHeaderView::autoFitYToggleRequested,
            m_view,       &ChartView::toggleAutoFitY);
    connect(m_view,       &ChartView::autoFitYChanged,
            m_headerView, &ChartHeaderView::setAutoFitY);

    // Apply the theme immediately.
    applyCurrentTheme();

    // On Windows, QEvent::ApplicationPaletteChange is not reliably delivered
    // when the user switches between light and dark mode in System Settings.
    // Qt 6.5+ exposes a dedicated signal for this; connect to it here so that
    // the theme is updated regardless of which delivery path fires first.
    // For Qt < 6.5 on Windows the nativeEvent() override handles
    // WM_SETTINGCHANGE / WM_THEMECHANGED as a fallback.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) {
                if (!m_themeOverride.has_value()) {
                    applyCurrentTheme();
                    if (m_view) m_view->viewport()->update();
                    update();
                }
            });
#endif

    // Toolbar style: structural only (spacing, font, border shape).
    // Colours are inherited from QPalette via palette() references in QSS.
    const QString tbStyle = QString(
                                "QToolBar { spacing: 2px; padding: 2px 6px; }"
                                "QToolButton {"
                                "    border: 1px solid palette(mid);"
                                "    border-radius: %1px;"
                                "    font: %2pt 'Consolas';"
                                "}"
                                "QToolButton:hover    { border-color: palette(highlight); }"
                                "QToolButton:pressed  { background: palette(dark);"
                                "                       border-color: palette(shadow);"
                                "                       padding-top: 1px; padding-left: 1px; }"
                                "QToolButton:checked  { background: palette(highlight); }"
                                "QToolButton:disabled { color: palette(mid); }")
                                .arg(kToolbarBorderRadius)
                                .arg(kToolbarFontPt);

    m_tb = new QToolBar(this);
    m_tb->setMovable(false);
    m_tb->setFloatable(false);
    m_tb->setIconSize({kToolbarIconSize, kToolbarIconSize});
    m_tb->setStyleSheet(tbStyle);

    auto *actXIn  = new QAction("X +", m_tb);
    actXIn->setProperty("iconName", "X_Out.svg");
    auto *actXOut = new QAction("X \xe2\x88\x92", m_tb);
    actXOut->setProperty("iconName", "X_In.svg");
    connect(actXIn,  &QAction::triggered, m_model, &ChartModel::zoomXIn);
    connect(actXOut, &QAction::triggered, m_model, &ChartModel::zoomXOut);
    m_tb->addAction(actXIn);
    m_tb->addAction(actXOut);
    m_tb->addSeparator();

    auto *actYIn  = new QAction("Y +", m_tb);
    actYIn->setProperty("iconName", "Y_Out.svg");
    auto *actYOut = new QAction("Y \xe2\x88\x92", m_tb);
    actYOut->setProperty("iconName", "Y_In.svg");
    connect(actYIn,  &QAction::triggered, m_model, &ChartModel::zoomYIn);
    connect(actYOut, &QAction::triggered, m_model, &ChartModel::zoomYOut);
    m_tb->addAction(actYIn);
    m_tb->addAction(actYOut);
    m_tb->addSeparator();

    // Y-scale zoom for selected rows (viewLo/viewHi in value space).
    m_actYsIn  = new QAction(QStringLiteral("Y\xe2\x86\x91"), m_tb);   // "Y↑"
    m_actYsIn->setProperty("iconName", "Y_Zoom_Out.svg");
    m_actYsIn->setToolTip(tr("Zoom in Y scale of selected rows"));
    m_actYsIn->setEnabled(false);
    m_actYsOut = new QAction(QStringLiteral("Y\xe2\x86\x93"), m_tb);   // "Y↓"
    m_actYsOut->setProperty("iconName", "Y_Zoom_In.svg");
    m_actYsOut->setToolTip(tr("Zoom out Y scale of selected rows"));
    m_actYsOut->setEnabled(false);
    connect(m_actYsIn,  &QAction::triggered, this, [this]() {
        m_view->zoomYScaleIn(m_headerView->selectedRows());
    });
    connect(m_actYsOut, &QAction::triggered, this, [this]() {
        m_view->zoomYScaleOut(m_headerView->selectedRows());
    });
    connect(m_headerView, &ChartHeaderView::selectionChanged,
            this, [this](const QSet<int> &rows) {
                const bool hasSelection = !rows.isEmpty();
                m_actYsIn ->setEnabled(hasSelection);
                m_actYsOut->setEnabled(hasSelection);
            });
    m_tb->addAction(m_actYsIn);
    m_tb->addAction(m_actYsOut);
    m_tb->addSeparator();

    auto *actAutoFit = new QAction("Auto Y", m_tb);
    actAutoFit->setProperty("iconName", "Auto.svg");
    actAutoFit->setCheckable(true);
    actAutoFit->setChecked(false);
    actAutoFit->setToolTip("Auto-fit Y to the visible X range");
    connect(actAutoFit, &QAction::triggered, m_view, &ChartView::setAutoFitY);
    connect(m_view, &ChartView::autoFitYChanged, actAutoFit, &QAction::setChecked);
    m_tb->addAction(actAutoFit);
    m_tb->addSeparator();

    auto *actReset = new QAction("Reset", m_tb);
    actReset->setProperty("iconName", "Reset.svg");
    actReset->setToolTip("Reset all display parameters");
    connect(actReset, &QAction::triggered, m_model, &ChartModel::resetAllDisplayParams);
    m_tb->addAction(actReset);

    // Expanding spacer pushes the labels to the right.
    m_tbSpacer = new QWidget(m_tb);
    m_tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_tb->addWidget(m_tbSpacer);

    // Cursor timestamp label — uses Link colour (accent, readable in both themes).
    m_tsLabel = new QLabel(m_tb);
    m_tsLabel->setStyleSheet(QString(
                                 "QLabel { font: %1pt 'Consolas'; color: palette(link);"
                                 "         padding: 0 8px 0 4px; border-left: 1px solid palette(mid); }")
                                 .arg(kToolbarFontPt));
    m_tsLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    m_tsLabel->setTextFormat(Qt::PlainText);
    m_tb->addWidget(m_tsLabel);

    {
        const QFontMetrics fm(QFont("Consolas", kToolbarFontPt));
        const int tsMinWidth =
            fm.horizontalAdvance(QStringLiteral("2000-01-01 00:00:00.000 +00:00"))
            + kTsLabelMinWidthPad;
        m_tsLabel->setMinimumWidth(tsMinWidth);
    }
    m_tb->addSeparator();
    // Keyboard shortcut hint label — uses Mid colour (subdued, informational).
    m_hintsLabel = new QLabel(
        "Ctrl+Wheel: X  |  Shift+Wheel: Y  |  LMB: pan Y  |  RMB: pan X  |  A / F",
        m_tb);
    m_hintsLabel->setStyleSheet(QString(
                                    "QLabel { font: %1pt 'Consolas'; color: palette(HighlightedText);"
                                    "         padding: 0 6px; border-left: 1px solid palette(mid); }")
                                    .arg(kToolbarFontPt));
    m_hintsLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_tb->addWidget(m_hintsLabel);

    connect(m_model, &ChartModel::cursorMoved, this, &ChartWidget::onCursorMoved);

    // ChartHeaderView is now embedded in ChartView as the vertical header —
    // no separate HBoxLayout is needed.
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_tb);
    lay->addWidget(m_view, 1);
}

void ChartWidget::onCursorMoved(int sample)
{
    if (!m_tsLabel) return;

    if (sample < 0) {
        m_tsLabel->clear();
        updateToolbarLayout();
        return;
    }

    const ChartSeries *ts = m_model->seriesByName(QStringLiteral("TimeStamp"));
    if (!ts || sampleCount(ts->data) == 0 || sample >= sampleCount(ts->data)) {
        m_tsLabel->clear();
        updateToolbarLayout();
        return;
    }

    const double epochSec = sampleAt(ts->data, sample);
    m_tsLabel->setText(ChartHeaderView::formatTimestampLine(epochSec));
    updateToolbarLayout();
}

void ChartWidget::updateToolbarLayout()
{
    if (!m_tb || !m_tbSpacer || !m_tsLabel || !m_hintsLabel) return;

    int rightOccupied = 0;
    if (m_tsLabel->isVisible())    rightOccupied += m_tsLabel->width();
    if (m_hintsLabel->isVisible()) rightOccupied += m_hintsLabel->width();

    const int available = m_tbSpacer->width() + rightOccupied;

    const bool hasTs  = !m_tsLabel->text().isEmpty();
    const int  tsW    = hasTs
                        ? qMax(m_tsLabel->minimumWidth(), m_tsLabel->sizeHint().width())
                        : 0;
    const int  hintsW = m_hintsLabel->sizeHint().width();

    const bool showTs    = hasTs && (available >= tsW);
    const bool showHints = available >= tsW + hintsW;

    m_tsLabel->setVisible(showTs);
    m_hintsLabel->setVisible(showHints);
}

void ChartWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updateToolbarLayout();
}

void ChartWidget::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    updateToolbarLayout();
}

void ChartWidget::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::ApplicationPaletteChange ||
        e->type() == QEvent::PaletteChange)
    {
        // Guard against re-entrancy: applyCurrentTheme() calls
        // QApplication::setPalette() which itself emits
        // ApplicationPaletteChange, triggering changeEvent() again.
        if (m_applyingTheme) return;
        m_applyingTheme = true;
        applyCurrentTheme();
        m_applyingTheme = false;
        if (m_view) m_view->viewport()->update();
        update();
    }
}

#if defined(Q_OS_WIN)
bool ChartWidget::nativeEvent(const QByteArray &eventType,
                              void *message, qintptr *result)
{
    // WM_SETTINGCHANGE fires when the user changes system settings, including
    // the light/dark mode toggle (lParam = L"ImmersiveColorSet").
    // WM_THEMECHANGED fires when the Windows visual theme changes.
    // Both can arrive before Qt has updated its own palette, so applyCurrentTheme()
    // is scheduled via a queued call to let Qt finish processing first.
    // Only used on Qt < 6.5; on Qt 6.5+ colorSchemeChanged handles this.
#  if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    if (eventType == "windows_generic_MSG" ||
        eventType == "windows_dispatcher_MSG")
    {
        const MSG *msg = static_cast<const MSG *>(message);
        if ((msg->message == WM_SETTINGCHANGE ||
             msg->message == WM_THEMECHANGED) && !m_themeOverride.has_value())
        {
            QMetaObject::invokeMethod(this, [this]() {
                if (!m_applyingTheme) {
                    applyCurrentTheme();
                    if (m_view) m_view->viewport()->update();
                    update();
                }
            }, Qt::QueuedConnection);
        }
    }
#  else
    Q_UNUSED(eventType) Q_UNUSED(message)
#  endif
    Q_UNUSED(result)
    return false;
}
#endif // Q_OS_WIN

void ChartWidget::setTheme(ChartTheme::Variant v)
{
    m_themeOverride = v;
    applyCurrentTheme();
}

void ChartWidget::applyCurrentTheme()
{
    const ChartTheme::Variant v = m_themeOverride.value_or(ChartTheme::systemVariant());
    // Apply QPalette + scroll bar stylesheet to the whole application so that
    // every widget (menus, dialogs, scroll bars, etc.) looks consistent.
    ChartTheme::applyToApplication(v);
    // Re-resolve theme-managed series colors to the new variant.
    // Manually pinned colors (colorIndex == kManualColor) are left untouched.
    if (m_model) m_model->reapplySeriesColors(v);

    // --- Update toolbar icons dynamically ---
    if (m_tb) {
        // Determine if the current theme is dark based on window background lightness.
        // Alternatively, if your ChartTheme::Variant provides a direct check
        // (e.g., v == ChartTheme::Variant::Dark), you can use that instead.
        const bool isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
        const QString themeDir = isDark ? QStringLiteral("H_Dark") : QStringLiteral("H_Light");

        for (QAction *act : m_tb->actions()) {
            const QString iconName = act->property("iconName").toString();
            if (!iconName.isEmpty()) {
                const QString iconPath = QStringLiteral(":/toolbar/%1/%2").arg(themeDir, iconName);
                act->setIcon(QIcon(iconPath));
            }
        }
    }
}
