#include "ChartView.h"

#include <QPainter>
#include <QHeaderView>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QScrollBar>
#include <QTimer>

ChartView::ChartView(QWidget *parent) : QTableView(parent)
{
    setMouseTracking(true);
    setSelectionMode(NoSelection);
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollMode(ScrollPerPixel);
    setHorizontalScrollMode(ScrollPerPixel);
    setSortingEnabled(false);
    setShowGrid(false);

    horizontalHeader()->hide();
    verticalHeader()->hide();
    setCornerButtonEnabled(false);

    setStyleSheet(
        "QTableView { background: #0a0d14; border: none; }"
        "QScrollBar:horizontal { background: #0f1219; height: 10px; }"
        "QScrollBar::handle:horizontal { background: #2c3650; border-radius: 4px; min-width: 20px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar:vertical { background: #0f1219; width: 10px; }"
        "QScrollBar::handle:vertical { background: #2c3650; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        );

    m_zoomDebounceTimer = new QTimer(this);
    m_zoomDebounceTimer->setSingleShot(true);
    m_zoomDebounceTimer->setInterval(40);
    connect(m_zoomDebounceTimer, &QTimer::timeout, this, &ChartView::flushZoom);
}

void ChartView::setChartModel(ChartModel *model)
{
    m_chartModel = model;

    if (m_delegate) delete m_delegate;
    m_delegate = new ChartDelegate(model, this);
    setItemDelegate(m_delegate);
    setModel(model);

    horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    syncColumnWidth();

    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
            this, &ChartView::onHScrollChanged);
    connect(model, &ChartModel::layoutChanged,
            this, [this]() { syncColumnWidth(); });
    connect(model, &ChartModel::cursorMoved,
            this, &ChartView::onCursorMoved);
    connect(model, &ChartModel::dataAppended,
            this, &ChartView::onDataAppended);
    connect(model, &ChartModel::rowsInserted,
            this, [this]() { syncColumnWidth(); });

    m_pendingOldSamples = model->maxSampleCount();
    m_pendingNewSamples = model->maxSampleCount();
}

void ChartView::syncColumnWidth()
{
    if (!m_chartModel) return;
    // Единственная колонка — данные
    horizontalHeader()->resizeSection(0, m_chartModel->chartPixelWidth());
    const int rh = m_chartModel->rowHeight();
    for (int r = 0; r < m_chartModel->rowCount(); ++r)
        verticalHeader()->resizeSection(r, rh);
}

void ChartView::setPixelsPerSample(float pps) { if (m_chartModel) m_chartModel->setPixelsPerSample(pps); }
void ChartView::setRowHeight(int px)          { if (m_chartModel) m_chartModel->setRowHeight(px); }

void ChartView::fitHeightToVisible()
{
    if (!m_chartModel) return;
    const int   scroll  = horizontalScrollBar()->value();
    const float pps     = m_chartModel->pixelsPerSample();
    const int   viewW   = viewport()->width();
    if (pps <= 0 || viewW <= 0) return;

    const int first = static_cast<int>(scroll / pps);
    const int last  = static_cast<int>((scroll + viewW) / pps);

    double maxRange = 0;
    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s || sampleIsEmpty(s->data)) continue;
        const int n = sampleCount(s->data);
        const int f = qMax(0, first);
        const int l = qMin(n - 1, last);
        if (f > l) continue;

        double rangeRow = 0.0;
        const std::size_t bufIdx = s->data.index();
        if (bufIdx == 6) {  // int64: native arithmetic
            int64_t lo = sampleAtI64(s->data, f), hi = lo;
            for (int i = f + 1; i <= l; ++i) {
                const int64_t v = sampleAtI64(s->data, i);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            rangeRow = static_cast<double>(hi - lo);
        } else if (bufIdx == 7) {  // uint64: native arithmetic
            uint64_t lo = sampleAtU64(s->data, f), hi = lo;
            for (int i = f + 1; i <= l; ++i) {
                const uint64_t v = sampleAtU64(s->data, i);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            rangeRow = static_cast<double>(hi - lo);
        } else {
            double lo = sampleAt(s->data, f), hi = lo;
            for (int i = f + 1; i <= l; ++i) {
                const double v = sampleAt(s->data, i);
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            rangeRow = hi - lo;
        }
        maxRange = qMax(maxRange, rangeRow);
    }
    if (maxRange <= 0) return;
    m_chartModel->setRowHeight(qBound(40, static_cast<int>(200.f / maxRange), 400));
}

// ─── Курсор ──────────────────────────────────────────────────────────────────

int ChartView::viewXToSample(int viewX) const
{
    if (!m_chartModel) return -1;
    // Единственная колонка: contentX = viewX + scroll
    // dataX = contentX = viewX + scroll  (col 0 начинается в content с позиции 0)
    const int dataX = viewX + horizontalScrollBar()->value();
    if (dataX < 0) return -1;
    const float pps = m_chartModel->pixelsPerSample();
    if (pps <= 0.f) return -1;
    const int sample = static_cast<int>(dataX / pps);
    if (sample >= m_chartModel->maxSampleCount()) return -1;
    return sample;
}

void ChartView::repaintCursorStrip(int oldSample, int newSample)
{
    if (!m_chartModel) return;
    const float pps    = m_chartModel->pixelsPerSample();
    const int   scroll = horizontalScrollBar()->value();
    const int   vH     = viewport()->height();
    const int   stripW = qMax(4, static_cast<int>(pps) + 2);

    // viewX = dataX - scroll = sample*pps - scroll
    auto toViewX = [&](int s) { return qRound(s * pps) - scroll; };

    if (oldSample >= 0) viewport()->update(QRect(toViewX(oldSample) - stripW/2, 0, stripW, vH));
    if (newSample >= 0) viewport()->update(QRect(toViewX(newSample) - stripW/2, 0, stripW, vH));
}

void ChartView::onCursorMoved(int sample)
{
    repaintCursorStrip(m_prevCursorSample, sample);
    m_prevCursorSample = sample;
}

// ─── Events ──────────────────────────────────────────────────────────────────

void ChartView::mouseMoveEvent(QMouseEvent *e)
{
    const int newSample = viewXToSample(e->pos().x());
    if (newSample != m_chartModel->cursorSample())
        m_chartModel->setCursorSample(newSample);
    // Не вызываем QTableView::mouseMoveEvent — он меняет выделение и скроллит к ячейке
    e->accept();
}

void ChartView::mousePressEvent(QMouseEvent *e)
{
    // Блокируем QTableView: он вызывает scrollTo(currentIndex()),
    // что при единственной широкой колонке сбрасывает горизонтальный скролл в 0.
    e->accept();
}

void ChartView::mouseReleaseEvent(QMouseEvent *e)
{
    e->accept();
}

void ChartView::leaveEvent(QEvent *e)
{
    if (m_chartModel) m_chartModel->setCursorSample(-1);
    QTableView::leaveEvent(e);
}

void ChartView::wheelEvent(QWheelEvent *e)
{
    if (!m_chartModel) { QTableView::wheelEvent(e); return; }

    const bool ctrlHeld  = e->modifiers() & Qt::ControlModifier;
    const bool shiftHeld = e->modifiers() & Qt::ShiftModifier;
    const int  delta     = e->angleDelta().y();

    if (ctrlHeld && shiftHeld) {
        const float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
        m_chartModel->setRowHeight(static_cast<int>(m_chartModel->rowHeight() * factor));
        e->accept();
        return;
    }

    if (ctrlHeld) {
        const float factor    = (delta > 0) ? 1.15f : (1.0f / 1.15f);
        const int   mouseX    = e->position().toPoint().x();
        const int   oldScroll = horizontalScrollBar()->value();
        const float oldPps    = m_chartModel->pixelsPerSample();
        const float newPps    = qBound(0.01f, oldPps * factor, 200.f);

        m_chartModel->setPpsQuiet(newPps);

        // Инвариант: семпл под мышью не двигается.
        // dataX = viewX + scroll  →  sample = dataX / oldPps
        // newScroll = sample * newPps - viewX = dataX * (newPps/oldPps) - viewX
        const int dataX     = mouseX + oldScroll;
        const int newScroll = qMax(0, qRound(dataX * (newPps / oldPps)) - mouseX);

        m_pendingPps    = newPps;
        m_pendingScroll = newScroll;
        m_zoomDebounceTimer->start();

        viewport()->update();
        emitVisibleSamplesIfChanged();
        e->accept();
        return;
    }

    QTableView::wheelEvent(e);
}

void ChartView::resizeEvent(QResizeEvent *e)
{
    QTableView::resizeEvent(e);
    emitVisibleSamplesIfChanged();
}

void ChartView::keyPressEvent(QKeyEvent *e)
{
    if (!m_chartModel) { QTableView::keyPressEvent(e); return; }

    switch (e->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_Minus: {
        const float factor    = (e->key() == Qt::Key_Minus) ? (1.0f / 1.25f) : 1.25f;
        const int   oldScroll = horizontalScrollBar()->value();
        const float oldPps    = m_chartModel->pixelsPerSample();
        const float newPps    = qBound(0.01f, oldPps * factor, 200.f);
        // Якорь — центр видимой области
        const int   centerX   = viewport()->width() / 2;
        const int   dataX     = centerX + oldScroll;
        const int   newScroll = qMax(0, qRound(dataX * (newPps / oldPps)) - centerX);
        m_chartModel->setPixelsPerSample(newPps);
        horizontalScrollBar()->setValue(newScroll);
        emitVisibleSamplesIfChanged();
        break;
    }
    case Qt::Key_Up:
        m_chartModel->setRowHeight(static_cast<int>(m_chartModel->rowHeight() * 1.2f));
        break;
    case Qt::Key_Down:
        m_chartModel->setRowHeight(static_cast<int>(m_chartModel->rowHeight() / 1.2f));
        break;
    case Qt::Key_F:
        fitHeightToVisible();
        break;
    default:
        QTableView::keyPressEvent(e);
    }
}

void ChartView::onHScrollChanged(int /*value*/)
{
    emitVisibleSamplesIfChanged();
}

int ChartView::visibleSampleCount() const
{
    if (!m_chartModel) return 0;
    const float pps   = m_chartModel->pixelsPerSample();
    if (pps <= 0.f)   return 0;
    const int total   = m_chartModel->maxSampleCount();
    if (total == 0)   return 0;

    const int scroll  = horizontalScrollBar()->value();
    const int vpW     = viewport()->width();

    const int first = static_cast<int>(scroll / pps);
    const int last  = qMin(total - 1, static_cast<int>((scroll + vpW) / pps));
    return qMax(0, last - first + 1);
}

void ChartView::emitVisibleSamplesIfChanged()
{
    const int count = visibleSampleCount();
    if (count != m_lastVisibleCount) {
        m_lastVisibleCount = count;
        emit visibleSamplesChanged(count, m_chartModel ? m_chartModel->pixelsPerSample() : 0.0);
    }
}

void ChartView::onDataAppended(int /*row*/, int newTotalSamples)
{
    m_pendingNewSamples = qMax(m_pendingNewSamples, newTotalSamples);
    if (!m_appendFlushPending) {
        m_appendFlushPending = true;
        QMetaObject::invokeMethod(this, &ChartView::flushPendingAppend, Qt::QueuedConnection);
    }
}

void ChartView::flushPendingAppend()
{
    m_appendFlushPending = false;
    if (!m_chartModel) return;

    const float pps        = m_chartModel->pixelsPerSample();
    const int   scroll     = horizontalScrollBar()->value();
    const int   vpW        = viewport()->width();
    const int   vpH        = viewport()->height();
    const int   oldSamples = m_pendingOldSamples;
    const int   newSamples = m_pendingNewSamples;

    m_pendingOldSamples = newSamples;

    const int newColWidth = qRound(newSamples * static_cast<double>(pps));
    if (horizontalHeader()->sectionSize(0) != newColWidth)
        horizontalHeader()->resizeSection(0, newColWidth);

    // viewX = dataX - scroll
    const int xStart = qRound(oldSamples * static_cast<double>(pps)) - scroll;
    const int xEnd   = newColWidth - scroll;

    const int visLeft  = qMax(xStart, 0);
    const int visRight = qMin(xEnd + 1, vpW);
    if (visLeft < visRight)
        viewport()->update(QRect(visLeft, 0, visRight - visLeft, vpH));

    emitVisibleSamplesIfChanged();
}

void ChartView::flushZoom()
{
    if (!m_chartModel || m_pendingPps <= 0.f) return;

    const int newColWidth = qRound(m_chartModel->maxSampleCount()
                                   * static_cast<double>(m_pendingPps));
    horizontalHeader()->resizeSection(0, newColWidth);

    if (m_pendingScroll >= 0) {
        horizontalScrollBar()->setValue(m_pendingScroll);
        m_pendingScroll = -1;
    }

    m_pendingOldSamples = m_chartModel->maxSampleCount();
    m_pendingNewSamples = m_chartModel->maxSampleCount();
    m_pendingPps        = 0.f;

    viewport()->update();
    emitVisibleSamplesIfChanged();
}
