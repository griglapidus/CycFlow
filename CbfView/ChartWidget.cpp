#include "ChartWidget.h"

#include <QLayout>
#include <QLabel>
#include <QTimer>
#include <QToolBar>

ChartWidget::ChartWidget(QWidget *parent) : QWidget(parent)
{
    m_model      = new ChartModel(this);
    m_view       = new ChartView(this);
    m_headerView = new ChartHeaderView(m_model, this);

    m_view->setChartModel(m_model);
    m_headerView->syncVerticalScroll(m_view);

    // ── тулбар ──────────────────────────────────────────────────────────────
    auto *toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setIconSize({16, 16});
    toolbar->setStyleSheet(
        "QToolBar { background: #0c1018; border-bottom: 1px solid #1e2538; spacing: 4px; padding: 2px 6px; }"
        "QToolButton { color: #8899bb; background: transparent; border: 1px solid #1e2538;"
        "  border-radius: 3px; padding: 2px 8px; font: 9pt 'Consolas'; }"
        "QToolButton:hover  { color: #dde8ff; background: #1a2035; }"
        "QToolButton:pressed{ background: #242d45; }"
        "QLabel { color: #445577; font: 9pt 'Consolas'; padding: 0 6px; }"
        );

    auto addBtn = [&](const QString &label, auto slot) {
        auto *act = new QAction(label, toolbar);
        connect(act, &QAction::triggered, this, slot);
        toolbar->addAction(act);
    };

    addBtn(u8"X+",  [this]{ m_model->setPixelsPerSample(m_model->pixelsPerSample() * 1.5f); });
    addBtn(u8"X−",  [this]{ m_model->setPixelsPerSample(m_model->pixelsPerSample() / 1.5f); });
    toolbar->addSeparator();
    addBtn(u8"Y+",  [this]{ m_model->setRowHeight(static_cast<int>(m_model->rowHeight() * 1.3f)); });
    addBtn(u8"Y−",  [this]{ m_model->setRowHeight(static_cast<int>(m_model->rowHeight() / 1.3f)); });
    toolbar->addSeparator();
    addBtn(u8"Fit", [this]{ m_view->fitHeightToVisible(); });

    toolbar->addWidget(new QLabel(
        u8"  Ctrl+Wheel: X-zoom  |  Ctrl+Shift+Wheel: Y-zoom  |  +/−: X-zoom  |  ↑↓: Y-zoom  |  F: fit",
        toolbar));

    // ── строка статуса ──────────────────────────────────────────────────────
    auto *statusBar = new QWidget(this);
    statusBar->setFixedHeight(22);
    statusBar->setStyleSheet("background: #080b11; border-top: 1px solid #1a2030;");

    m_statusLabel = new QLabel(statusBar);
    m_statusLabel->setStyleSheet(
        "color: #4a5f88; font: 8pt 'Consolas'; padding: 0 10px; background: transparent;");
    m_statusLabel->setText(u8"visible: — pts");

    auto *sbLay = new QHBoxLayout(statusBar);
    sbLay->setContentsMargins(0, 0, 0, 0);
    sbLay->addStretch();
    sbLay->addWidget(m_statusLabel);

    connect(m_view, &ChartView::visibleSamplesChanged,
            this,   &ChartWidget::onVisibleSamplesChanged);

    // ── layout ──────────────────────────────────────────────────────────────
    // header | chart (растягивается)
    auto *chartRow = new QHBoxLayout;
    chartRow->setContentsMargins(0, 0, 0, 0);
    chartRow->setSpacing(0);
    chartRow->addWidget(m_headerView);
    chartRow->addWidget(m_view, 1);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(toolbar);
    lay->addLayout(chartRow, 1);
    lay->addWidget(statusBar);
}

void ChartWidget::onVisibleSamplesChanged(int count, double pps)
{
    m_statusLabel->setText(
        count > 0
            ? QString(u8"visible: %1 pts, pps: %2").arg(count).arg(pps)
            : u8"visible: — pts, pps: -");
}
