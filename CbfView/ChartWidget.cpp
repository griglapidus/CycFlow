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

    // Операции из контекстного меню заголовков → слоты ChartView
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

    auto *tbX = new QToolBar("X", this);
    tbX->setMovable(false); tbX->setIconSize({14,14});
    tbX->setStyleSheet(tbStyle);

    auto *actXIn  = new QAction("X +", tbX);
    auto *actXOut = new QAction("X −", tbX);
    connect(actXIn,  &QAction::triggered, m_model, &ChartModel::zoomXIn);
    connect(actXOut, &QAction::triggered, m_model, &ChartModel::zoomXOut);
    tbX->addAction(actXIn);
    tbX->addAction(actXOut);
    tbX->addWidget(new QLabel("|"));

    auto *actYIn  = new QAction("Y +", tbX);
    auto *actYOut = new QAction("Y −", tbX);
    connect(actYIn,  &QAction::triggered, m_model, &ChartModel::zoomYIn);
    connect(actYOut, &QAction::triggered, m_model, &ChartModel::zoomYOut);
    tbX->addAction(actYIn);
    tbX->addAction(actYOut);
    tbX->addWidget(new QLabel("|"));

    auto *actFit  = new QAction("Fit Y", tbX);
    connect(actFit, &QAction::triggered, m_view, &ChartView::fitYToVisible);
    tbX->addAction(actFit);

    m_actAutoFit = new QAction("Auto", tbX);
    m_actAutoFit->setCheckable(true);
    m_actAutoFit->setChecked(false);
    m_actAutoFit->setToolTip("Авто-подстройка Y по видимому фрагменту");
    connect(m_actAutoFit, &QAction::triggered, m_view, &ChartView::setAutoFitY);
    tbX->addAction(m_actAutoFit);

    tbX->addWidget(new QLabel("|"));

    auto *actReset = new QAction("Reset", tbX);
    connect(actReset, &QAction::triggered, m_model, &ChartModel::resetAllDisplayParams);
    tbX->addAction(actReset);

    tbX->addWidget(new QLabel(
        u8"  Ctrl+Wheel: X  |  Shift+Wheel: Y серии  |  Ctrl+Shift+Wheel: Y все"
        u8"  |  ЛКМ drag: сдвиг  |  ПКМ: меню  |  F/A/Esc"));

    auto *statusBar = new QWidget(this);
    statusBar->setFixedHeight(22);
    statusBar->setStyleSheet("background:#080b11; border-top:1px solid #1a2030;");

    m_statusLabel = new QLabel(statusBar);
    m_statusLabel->setStyleSheet(
        "color:#4a5f88; font:8pt 'Consolas'; padding:0 10px; background:transparent;");
    m_statusLabel->setText(u8"visible: — pts");

    auto *sbLay = new QHBoxLayout(statusBar);
    sbLay->setContentsMargins(0, 0, 0, 0);
    sbLay->addStretch();
    sbLay->addWidget(m_statusLabel);

    connect(m_view, &ChartView::visibleSamplesChanged,
            this,   &ChartWidget::onVisibleSamplesChanged);

    auto *chartRow = new QHBoxLayout;
    chartRow->setContentsMargins(0,0,0,0);
    chartRow->setSpacing(0);
    chartRow->addWidget(m_headerView);
    chartRow->addWidget(m_view, 1);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->setSpacing(0);
    lay->addWidget(tbX);
    lay->addLayout(chartRow, 1);
    lay->addWidget(statusBar);
}

void ChartWidget::onVisibleSamplesChanged(int count, double pps)
{
    m_statusLabel->setText(
        count > 0
            ? QString(u8"visible: %1 pts  pps: %2").arg(count).arg(pps, 0, 'f', 3)
            : u8"visible: — pts  pps: —");
}
