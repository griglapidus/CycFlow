#include "ChartWidget.h"

#include <QLayout>
#include <QLabel>
#include <QToolBar>
#include <QAction>

ChartWidget::ChartWidget(QWidget *parent) : QWidget(parent)
{
    m_model      = new ChartModel(this);
    m_view       = new ChartView(this);
    m_headerView = new ChartHeaderView(m_model, this);

    m_view->setChartModel(m_model);
    m_headerView->syncVerticalScroll(m_view);

    connect(m_headerView, &ChartHeaderView::syncScaleRequested,
            m_view,       &ChartView::syncScale);
    connect(m_headerView, &ChartHeaderView::overlayRequested,
            m_view,       &ChartView::overlayOnto);
    connect(m_headerView, &ChartHeaderView::resetSelectedRequested,
            m_view,       &ChartView::resetSelected);

    const QString tbStyle =
        "QToolBar { background:#0c1018; border-bottom:1px solid #1e2538;"
        "           spacing:2px; padding:2px 6px; }"
        "QToolButton { color:#8899bb; background:transparent;"
        "              border:1px solid #1e2538; border-radius:3px;"
        "              padding:2px 7px; font:9pt 'Consolas'; }"
        "QToolButton:hover   { color:#dde8ff; background:#1a2035; }"
        "QToolButton:pressed { background:#242d45; }"
        "QToolButton:checked { color:#aaddff; background:#1a2a45;"
        "                      border-color:#2a4a80; }"
        "QLabel { color:#445577; font:9pt 'Consolas'; padding:0 4px; }";

    auto *tb = new QToolBar(this);
    tb->setMovable(false);
    tb->setIconSize({14, 14});
    tb->setStyleSheet(tbStyle);

    auto *actXIn  = new QAction("X +", tb);
    auto *actXOut = new QAction("X −", tb);
    connect(actXIn,  &QAction::triggered, m_model, &ChartModel::zoomXIn);
    connect(actXOut, &QAction::triggered, m_model, &ChartModel::zoomXOut);
    tb->addAction(actXIn);
    tb->addAction(actXOut);
    tb->addWidget(new QLabel("|"));

    auto *actYIn  = new QAction("Y +", tb);
    auto *actYOut = new QAction("Y −", tb);
    connect(actYIn,  &QAction::triggered, m_model, &ChartModel::zoomYIn);
    connect(actYOut, &QAction::triggered, m_model, &ChartModel::zoomYOut);
    tb->addAction(actYIn);
    tb->addAction(actYOut);
    tb->addWidget(new QLabel("|"));

    m_actAutoFit = new QAction("Auto Y", tb);
    m_actAutoFit->setCheckable(true);
    m_actAutoFit->setChecked(false);
    m_actAutoFit->setToolTip(u8"Авто-подстройка Y по видимому фрагменту");
    connect(m_actAutoFit, &QAction::triggered, m_view, &ChartView::setAutoFitY);
    connect(m_view, &ChartView::autoFitYChanged, m_actAutoFit, &QAction::setChecked);
    tb->addAction(m_actAutoFit);

    tb->addWidget(new QLabel("|"));

    auto *actReset = new QAction("Reset", tb);
    actReset->setToolTip(u8"Сбросить все параметры отображения");
    connect(actReset, &QAction::triggered, m_model, &ChartModel::resetAllDisplayParams);
    tb->addAction(actReset);

    tb->addWidget(new QLabel(
        u8"  Ctrl+Wheel: X  |  Shift+Wheel: Y серии  |  Ctrl+Shift+Wheel: Y все"
        u8"  |  ЛКМ drag: сдвиг  |  ПКМ: меню  |  A/F"));

    auto *chartRow = new QHBoxLayout;
    chartRow->setContentsMargins(0, 0, 0, 0);
    chartRow->setSpacing(0);
    chartRow->addWidget(m_headerView);
    chartRow->addWidget(m_view, 1);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(tb);
    lay->addLayout(chartRow, 1);
}
