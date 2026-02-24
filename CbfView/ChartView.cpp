#include "ChartView.h"

#include <QPainter>
#include <QHeaderView>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QPaintEvent>
#include <QScrollBar>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <cmath>

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

    m_autoFitTimer = new QTimer(this);
    m_autoFitTimer->setSingleShot(true);
    m_autoFitTimer->setInterval(50);
    connect(m_autoFitTimer, &QTimer::timeout, this, &ChartView::doAutoFitY);
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
    // seriesDisplayChanged: обновляем конкретную строку
    connect(model, &ChartModel::seriesDisplayChanged,
            this, [this](const QString &, int row) {
                const QRect rowRect = visualRect(this->model()->index(row, 0));
                viewport()->update(rowRect);
            });

    m_pendingOldSamples = model->maxSampleCount();
    m_pendingNewSamples = model->maxSampleCount();
}

void ChartView::syncColumnWidth()
{
    if (!m_chartModel) return;
    horizontalHeader()->resizeSection(0, m_chartModel->chartPixelWidth());
    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s = m_chartModel->series(r);
        const int h = s ? s->rowHeight : m_chartModel->rowHeight();
        verticalHeader()->resizeSection(r, h);
    }
}

void ChartView::setPixelsPerSample(float pps) { if (m_chartModel) m_chartModel->setPixelsPerSample(pps); }
void ChartView::setRowHeight(int px)          { if (m_chartModel) m_chartModel->setRowHeight(px); }

// ─── Auto / manual Y fit ─────────────────────────────────────────────────────

ChartView::VisibleRange ChartView::visibleRangeForRow(int row) const
{
    if (!m_chartModel) return {0, 0, false};
    const ChartSeries *s = m_chartModel->series(row);
    if (!s || sampleIsEmpty(s->data)) return {0, 0, false};

    const int   scroll = horizontalScrollBar()->value();
    const float pps    = m_chartModel->pixelsPerSample();
    if (pps <= 0) return {0, 0, false};
    const int n = sampleCount(s->data);
    const int f = qMax(0, static_cast<int>(scroll / pps));
    const int l = qMin(n - 1, static_cast<int>((scroll + viewport()->width()) / pps));
    if (f > l) return {0, 0, false};

    const std::size_t bufIdx = s->data.index();
    if (bufIdx == 6) {
        int64_t lo = sampleAtI64(s->data, f), hi = lo;
        for (int i = f+1; i <= l; ++i) { int64_t v = sampleAtI64(s->data,i); if(v<lo) lo=v; if(v>hi) hi=v; }
        return {static_cast<double>(lo), static_cast<double>(hi), lo != hi};
    }
    if (bufIdx == 7) {
        uint64_t lo = sampleAtU64(s->data, f), hi = lo;
        for (int i = f+1; i <= l; ++i) { uint64_t v = sampleAtU64(s->data,i); if(v<lo) lo=v; if(v>hi) hi=v; }
        return {static_cast<double>(lo), static_cast<double>(hi), lo != hi};
    }
    double lo = sampleAt(s->data, f), hi = lo;
    for (int i = f+1; i <= l; ++i) { double v = sampleAt(s->data,i); if(v<lo) lo=v; if(v>hi) hi=v; }
    return {lo, hi, !qFuzzyCompare(lo, hi)};
}

void ChartView::fitYToVisible()
{
    setAutoFitY(false);
    if (!m_chartModel) return;

    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;

        const auto vr = visibleRangeForRow(r);
        if (!vr.valid) continue;

        const double globalRange  = boundsToDouble(s->maxVal) - boundsToDouble(s->minVal);
        const double visibleRange = vr.hi - vr.lo;
        if (globalRange <= 0 || visibleRange <= 0) continue;

        const float newScale = qBound(0.1f, static_cast<float>(globalRange / visibleRange * 0.8), 50.f);

        const double midGlobal = (boundsToDouble(s->minVal) + boundsToDouble(s->maxVal)) * 0.5;
        const double midVis    = (vr.lo + vr.hi) * 0.5;
        const int rowH  = s->rowHeight - 4*2;
        const int newOffset = static_cast<int>((midGlobal - midVis) / globalRange * rowH * newScale);

        m_chartModel->setSeriesYScale(s->name, newScale);
        m_chartModel->setSeriesYOffset(s->name, newOffset);
    }
}

void ChartView::toggleAutoFitY() { setAutoFitY(!m_autoFitY); }

void ChartView::setAutoFitY(bool on)
{
    m_autoFitY = on;
    if (on) doAutoFitY();
}

void ChartView::doAutoFitY()
{
    if (!m_autoFitY || !m_chartModel) return;
    if (!m_chartModel) return;
    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        const auto vr = visibleRangeForRow(r);
        if (!vr.valid) continue;
        const double globalRange  = boundsToDouble(s->maxVal) - boundsToDouble(s->minVal);
        const double visibleRange = vr.hi - vr.lo;
        if (globalRange <= 0 || visibleRange <= 0) continue;
        const float newScale = qBound(0.1f, static_cast<float>(globalRange / visibleRange * 0.8), 50.f);
        const double midGlobal = (boundsToDouble(s->minVal) + boundsToDouble(s->maxVal)) * 0.5;
        const double midVis    = (vr.lo + vr.hi) * 0.5;
        const int rowH  = s->rowHeight - 8;
        const int newOffset = static_cast<int>((midGlobal - midVis) / globalRange * rowH * newScale);
        m_chartModel->setSeriesYScale(s->name, newScale);
        m_chartModel->setSeriesYOffset(s->name, newOffset);
    }
}

int ChartView::viewXToSample(int viewX) const
{
    if (!m_chartModel) return -1;
    const int dataX = viewX + horizontalScrollBar()->value();
    if (dataX < 0) return -1;
    const float pps = m_chartModel->pixelsPerSample();
    if (pps <= 0.f) return -1;
    const int sample = static_cast<int>(dataX / pps);
    if (sample >= m_chartModel->maxSampleCount()) return -1;
    return sample;
}

int ChartView::viewYToRow(int viewY) const
{
    const QModelIndex idx = indexAt(QPoint(0, viewY));
    return idx.isValid() ? idx.row() : -1;
}

void ChartView::repaintCursorStrip(int oldSample, int newSample)
{
    if (!m_chartModel) return;
    const float pps    = m_chartModel->pixelsPerSample();
    const int   scroll = horizontalScrollBar()->value();
    const int   vH     = viewport()->height();
    const int   stripW = qMax(4, static_cast<int>(pps) + 2);
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

void ChartView::mousePressEvent(QMouseEvent *e)
{
    if (!m_chartModel) { e->accept(); return; }

    if (e->button() == Qt::LeftButton) {
        const int row = viewYToRow(e->pos().y());
        if (row >= 0) {
            const ChartSeries *s = m_chartModel->series(row);
            if (s) {
                m_dragging        = true;
                m_dragStartY      = e->pos().y();
                m_dragStartOffset = s->yOffset;
                m_dragSeriesName  = s->name;
                setCursor(Qt::SizeVerCursor);
            }
        }
    }
    e->accept();
}

void ChartView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_chartModel) { e->accept(); return; }

    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        const int delta  = e->pos().y() - m_dragStartY;
        const int newOff = m_dragStartOffset + delta;
        m_chartModel->setSeriesYOffset(m_dragSeriesName, newOff);
        e->accept();
        return;
    }

    // Курсор
    const int newSample = viewXToSample(e->pos().x());
    if (newSample != m_chartModel->cursorSample())
        m_chartModel->setCursorSample(newSample);

    e->accept();
}

void ChartView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging) {
        m_dragging = false;
        unsetCursor();
    }
    e->accept();
}

void ChartView::contextMenuEvent(QContextMenuEvent *e)
{
    if (!m_chartModel) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background:#0f1219; color:#aabbcc; border:1px solid #1e2538; font:9pt 'Consolas'; }"
        "QMenu::item:selected { background:#1a2540; }"
        "QMenu::separator { height:1px; background:#1e2538; margin:3px 8px; }"
        );

    QAction *actAutoFit = menu.addAction(u8"Авто-подстройка Y по видимому фрагменту");
    actAutoFit->setCheckable(true);
    actAutoFit->setChecked(m_autoFitY);
    connect(actAutoFit, &QAction::triggered, this, &ChartView::toggleAutoFitY);

    QAction *actOneFit = menu.addAction(u8"Подогнать Y под видимый фрагмент");
    connect(actOneFit, &QAction::triggered, this, &ChartView::fitYToVisible);

    menu.addSeparator();

    const int row = viewYToRow(e->pos().y());
    const ChartSeries *s = (row >= 0) ? m_chartModel->series(row) : nullptr;
    if (s) {
        QAction *actReset = menu.addAction(
            QString(u8"Сбросить вид '%1'").arg(s->name));
        const QString name = s->name;
        connect(actReset, &QAction::triggered, this, [this, name]() {
            m_chartModel->setSeriesYScale(name, 1.0f);
            m_chartModel->setSeriesYOffset(name, 0);
        });
    }

    QAction *actResetAll = menu.addAction(u8"Сбросить вид всех графиков");
    connect(actResetAll, &QAction::triggered, m_chartModel, &ChartModel::resetAllDisplayParams);

    menu.exec(e->globalPos());
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
        const int dataX    = mouseX + oldScroll;
        const int newScroll= qMax(0, qRound(dataX * (newPps / oldPps)) - mouseX);
        m_pendingPps    = newPps;
        m_pendingScroll = newScroll;
        m_zoomDebounceTimer->start();
        viewport()->update();
        emitVisibleSamplesIfChanged();
        e->accept();
        return;
    }

    if (shiftHeld) {
        const int row = viewYToRow(e->position().toPoint().y());
        if (row >= 0) {
            const ChartSeries *s = m_chartModel->series(row);
            if (s) {
                const float factor   = (delta > 0) ? 1.2f : (1.0f / 1.2f);
                const float newScale = qBound(0.05f, s->yScale * factor, 100.f);
                m_chartModel->setSeriesYScale(s->name, newScale);
                if (m_autoFitY) setAutoFitY(false);
            }
        }
        e->accept();
        return;
    }

    QTableView::wheelEvent(e);
}

void ChartView::resizeEvent(QResizeEvent *e)
{
    QTableView::resizeEvent(e);
    emitVisibleSamplesIfChanged();
    if (m_autoFitY) m_autoFitTimer->start();
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
        fitYToVisible();
        break;
    case Qt::Key_A:
        toggleAutoFitY();
        break;
    case Qt::Key_Escape:
        m_chartModel->resetAllDisplayParams();
        setAutoFitY(false);
        break;
    default:
        QTableView::keyPressEvent(e);
    }
}


void ChartView::onHScrollChanged(int /*value*/)
{
    emitVisibleSamplesIfChanged();
    if (m_autoFitY) m_autoFitTimer->start();
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
    const int first   = static_cast<int>(scroll / pps);
    const int last    = qMin(total - 1, static_cast<int>((scroll + vpW) / pps));
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

void ChartView::onDataAppended(const QString &/*name*/, int /*row*/, int newTotalSamples)
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
    m_pendingOldSamples    = newSamples;

    const int newColWidth = qRound(newSamples * static_cast<double>(pps));
    if (horizontalHeader()->sectionSize(0) != newColWidth)
        horizontalHeader()->resizeSection(0, newColWidth);

    const int xStart  = qRound(oldSamples * static_cast<double>(pps)) - scroll;
    const int xEnd    = newColWidth - scroll;
    const int visLeft = qMax(xStart, 0);
    const int visRight= qMin(xEnd + 1, vpW);
    if (visLeft < visRight)
        viewport()->update(QRect(visLeft, 0, visRight - visLeft, vpH));

    emitVisibleSamplesIfChanged();
    if (m_autoFitY) m_autoFitTimer->start();
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
