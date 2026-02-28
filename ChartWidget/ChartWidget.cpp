#include "ChartWidget.h"

#include <QLayout>
#include <QToolBar>
#include <QAction>
#include <QShowEvent>
#include <QFontMetrics>

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

    connect(m_headerView, &ChartHeaderView::fitYToVisibleRequested,
            m_view,       &ChartView::fitYToVisible);
    connect(m_headerView, &ChartHeaderView::autoFitYToggleRequested,
            m_view,       &ChartView::toggleAutoFitY);
    connect(m_view, &ChartView::autoFitYChanged,
            m_headerView, &ChartHeaderView::setAutoFitY);

    const QString tbStyle =
        "QToolBar { background:#0c1018; border-bottom:1px solid #1e2538;"
        "           spacing:2px; padding:2px 6px; }"
        "QToolButton { color:#8899bb; background:transparent;"
        "              border:1px solid #1e2538; border-radius:3px;"
        "              padding:2px 7px; font:9pt 'Consolas'; }"
        "QToolButton:hover   { color:#dde8ff; background:#1a2035; }"
        "QToolButton:pressed { background:#242d45; }"
        "QToolButton:checked { color:#aaddff; background:#1a2a45;"
        "                      border-color:#2a4a80; }";

    m_tb = new QToolBar(this);
    m_tb->setMovable(false);
    m_tb->setIconSize({14, 14});
    m_tb->setStyleSheet(tbStyle);


    auto *actXIn  = new QAction("X +", m_tb);
    auto *actXOut = new QAction("X −", m_tb);
    connect(actXIn,  &QAction::triggered, m_model, &ChartModel::zoomXIn);
    connect(actXOut, &QAction::triggered, m_model, &ChartModel::zoomXOut);
    m_tb->addAction(actXIn);
    m_tb->addAction(actXOut);
    m_tb->addSeparator();

    auto *actYIn  = new QAction("Y +", m_tb);
    auto *actYOut = new QAction("Y −", m_tb);
    connect(actYIn,  &QAction::triggered, m_model, &ChartModel::zoomYIn);
    connect(actYOut, &QAction::triggered, m_model, &ChartModel::zoomYOut);
    m_tb->addAction(actYIn);
    m_tb->addAction(actYOut);
    m_tb->addSeparator();

    m_actAutoFit = new QAction("Auto Y", m_tb);
    m_actAutoFit->setCheckable(true);
    m_actAutoFit->setChecked(false);
    m_actAutoFit->setToolTip(u8"Авто-подстройка Y по видимому фрагменту");
    connect(m_actAutoFit, &QAction::triggered, m_view, &ChartView::setAutoFitY);
    connect(m_view, &ChartView::autoFitYChanged, m_actAutoFit, &QAction::setChecked);
    m_tb->addAction(m_actAutoFit);
    m_tb->addSeparator();

    auto *actReset = new QAction("Reset", m_tb);
    actReset->setToolTip(u8"Сбросить все параметры отображения");
    connect(actReset, &QAction::triggered, m_model, &ChartModel::resetAllDisplayParams);
    m_tb->addAction(actReset);


    m_tbSpacer = new QWidget(m_tb);
    m_tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_tb->addWidget(m_tbSpacer);

    m_tsLabel = new QLabel(m_tb);
    m_tsLabel->setStyleSheet(
        "QLabel { color:#88ccaa; font:9pt 'Consolas'; padding:0 8px 0 4px;"
        "         border-left:1px solid #1e2538; }");
    m_tsLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    m_tsLabel->setTextFormat(Qt::PlainText);
    m_tb->addWidget(m_tsLabel);

    {
        const QFontMetrics fm(QFont("Consolas", 9));
        // "2000-01-01 00:00:00.000 +00:00" — 30 символов + поля
        m_tsMinWidth = fm.horizontalAdvance(QStringLiteral("2000-01-01 00:00:00.000 +00:00")) + 24;
    }
    m_tsLabel->setMinimumWidth(m_tsMinWidth);


    m_hintsLabel = new QLabel(
        u8"Ctrl+Wheel: X  |  Shift+Wheel: Y  |  ЛКМ: сдвиг Y  |  ПКМ: пан X  |  A / F",
        m_tb);
    m_hintsLabel->setStyleSheet(
        "QLabel { color:#334466; font:9pt 'Consolas'; padding:0 6px;"
        "         border-left:1px solid #1e2538; }");
    m_hintsLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_tb->addWidget(m_hintsLabel);


    connect(m_model, &ChartModel::cursorMoved, this, &ChartWidget::onCursorMoved);


    auto *chartRow = new QHBoxLayout;
    chartRow->setContentsMargins(0, 0, 0, 0);
    chartRow->setSpacing(0);
    chartRow->addWidget(m_headerView);
    chartRow->addWidget(m_view, 1);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_tb);
    lay->addLayout(chartRow, 1);
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
    const int  tsW    = hasTs    ? qMax(m_tsMinWidth, m_tsLabel->sizeHint().width()) : 0;
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
