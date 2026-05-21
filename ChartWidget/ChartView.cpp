// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "ChartView.h"

#include <QPainter>
#include <QHeaderView>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QScrollBar>
#include <QMenu>
#include <QCursor>
#include <QAction>
#include <cmath>

// =============================================================================
//  ChartView — local constants
// =============================================================================

namespace {

// --- Wheel / key zoom step factors -------------------------------------------

/// Row-height step for Ctrl+Shift+Wheel.
constexpr float kWheelRowHeightStep = 1.15f;

/// X-zoom (pps) step for Ctrl+Wheel.
constexpr float kWheelXZoomStep = 1.15f;

/// Y-zoom step for Shift+Wheel (narrows/widens the visible value range).
constexpr float kWheelYZoomStep = 1.2f;

/// Row-height step for Key_Up / Key_Down.
constexpr float kKeyRowHeightStep = 1.2f;

/// X-zoom (pps) step for Key_Plus / Key_Minus.
constexpr float kKeyXZoomStep = 1.25f;

// --- Cursor repaint strip ----------------------------------------------------

/// Minimum width of the strip invalidated around the cursor line (pixels).
constexpr int kCursorStripMinWidth = 4;

/// Extra pixels added to pps when computing the cursor strip width.
constexpr int kCursorStripPpsExtra = 2;

// --- computeGridLabelWidth ---------------------------------------------------

/// Fallback label width (pixels) when no series data is available.
constexpr int kGridLabelFallbackWidth = 60;

/// Right-margin added to the measured max label width (mirrors the delegate).
constexpr int kGridLabelWidthMargin = 6;

// kGridTargetDivisions, kGridStep2x/5x, kGridLabelFontPt, kGridLabelMinGap
// are defined in ChartDefs.h — single source of truth.

// --- paintEvent clip ---------------------------------------------------------

/// Upper bound on the pixel width of a QHeaderView section.
///
/// QHeaderView has a hard internal cap (`maxSizeSection = 1048575`,
/// hard-coded in qheaderview.cpp since Qt 4.2): any `resizeSection()` with
/// a larger value is silently rejected.  We bound the visible sample
/// window so the chart's section never exceeds this value; the rest of
/// the (potentially much longer) record is handled by the rendering
/// origin in ChartModel::displayOriginSample().
constexpr int kMaxSectionPx = 1048575;

} // anonymous namespace

// =============================================================================
//  ChartView
// =============================================================================

ChartView::ChartView(QWidget *parent) : QTableView(parent)
{
    setMouseTracking(true);
    setSelectionMode(NoSelection);
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollMode(ScrollPerPixel);
    setHorizontalScrollMode(ScrollPerPixel);
    setSortingEnabled(false);
    setShowGrid(false);

    // The top horizontal header is not needed — hide it.
    // The vertical header is replaced by ChartHeaderView from outside,
    // so we must NOT hide it here.
    horizontalHeader()->hide();
    setCornerButtonEnabled(false);
}

void ChartView::setChartModel(ChartModel *model)
{
    m_chartModel = model;

    if (m_delegate) delete m_delegate;
    m_delegate = new ChartDelegate(model, this);
    setItemDelegate(m_delegate);
    setModel(model);

    horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    // The vertical header is our ChartHeaderView (already installed before
    // setChartModel was called).  Set Fixed so section resizing is handled
    // by ChartHeaderView's own mouse events.
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
    connect(model, &ChartModel::seriesDisplayChanged,
            this, [this](const QString &, int) {
                m_gridLabelWidth = -1;  // Y range may have changed
                viewport()->update();
            });
    connect(model, &ChartModel::modelReset, this, [this]() {
        m_gridLabelWidth    = -1;
        m_pendingOldSamples = 0;
        m_pendingNewSamples = 0;
        m_lastVisibleCount  = -1;
        horizontalScrollBar()->setValue(0);
        syncColumnWidth();
        viewport()->update();
    });

    m_pendingOldSamples = model->maxSampleCount();
    m_pendingNewSamples = model->maxSampleCount();
}

void ChartView::syncColumnWidth()
{
    if (!m_chartModel) return;
    m_gridLabelWidth = -1;  // row heights may have changed, invalidate label cache
    updateDisplayOrigin();
    horizontalHeader()->resizeSection(0, m_chartModel->chartPixelWidth());
    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s = m_chartModel->series(r);
        const int h = s ? s->rowHeight : m_chartModel->rowHeight();
        verticalHeader()->resizeSection(r, h);
    }
}

void ChartView::setPixelsPerSample(float pps) { if (m_chartModel) m_chartModel->setPixelsPerSample(pps); }
void ChartView::setRowHeight(int px)          { if (m_chartModel) m_chartModel->setRowHeight(px); }

ChartView::VisibleRange ChartView::visibleRangeForRow(int row) const
{
    if (!m_chartModel) return {0, 0, false};
    const ChartSeries *s = m_chartModel->series(row);
    if (!s || sampleIsEmpty(s->data)) return {0, 0, false};

    const int   scroll = horizontalScrollBar()->value();
    const float pps    = m_chartModel->pixelsPerSample();
    if (pps <= 0) return {0, 0, false};
    const int origin = m_chartModel->displayOriginSample();
    const int n = sampleCount(s->data);
    const int f = qMax(origin, origin + static_cast<int>(scroll / pps));
    const int l = qMin(n - 1, origin + static_cast<int>((scroll + viewport()->width()) / pps));
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

        m_chartModel->setSeriesViewRange(s->name, vr.lo, vr.hi);
    }
}

void ChartView::toggleAutoFitY() { setAutoFitY(!m_autoFitY); }

void ChartView::setAutoFitY(bool on)
{
    if (m_autoFitY == on) return;
    m_autoFitY = on;
    emit autoFitYChanged(on);
    if (on) doAutoFitY();
}

void ChartView::toggleLiveMode() { setLiveMode(!m_liveMode); }

void ChartView::setLiveMode(bool on)
{
    if (m_liveMode == on) return;
    m_liveMode = on;
    emit liveModeChanged(on);
    if (on) scrollToEnd();
}

void ChartView::scrollToEnd()
{
    if (!m_chartModel) return;
    // chartPixelWidth() already reflects the virtualised window
    // (ChartModel applies displayOriginSample), so it is bounded by
    // kMaxSectionPx and fits in int without further clamping.
    const int   colW   = m_chartModel->chartPixelWidth();
    const int   vpW    = viewport()->width();
    const int   maxScr = qMax(0, colW - vpW);
    QScrollBar *sb     = horizontalScrollBar();
    // The scrollbar range may lag behind a recent column-width change
    // (QTableView updates it via queued layout).  Force-extend it so the
    // setValue call below actually reaches the new end.
    if (sb->maximum() < maxScr) sb->setRange(0, maxScr);
    sb->setValue(maxScr);
}

void ChartView::updateDisplayOrigin()
{
    if (!m_chartModel) return;
    const float pps = m_chartModel->pixelsPerSample();
    if (pps <= 0.f) return;
    // Largest number of samples whose pixel span fits in a QHeaderView
    // section.  Subtracted from the tail to obtain the new origin so the
    // window always covers the newest data.
    const int maxVisibleSamples = static_cast<int>(kMaxSectionPx / pps);
    const int total             = m_chartModel->maxSampleCount();
    const int newOrigin         = qMax(0, total - maxVisibleSamples);
    if (newOrigin != m_chartModel->displayOriginSample())
        m_chartModel->setDisplayOriginSample(newOrigin);
}

void ChartView::doAutoFitY()
{
    if (!m_autoFitY || !m_chartModel) return;
    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        const auto vr = visibleRangeForRow(r);
        if (!vr.valid) continue;
        m_chartModel->setSeriesViewRange(s->name, vr.lo, vr.hi);
    }
}

int ChartView::viewXToSample(int viewX) const
{
    if (!m_chartModel) return -1;
    const int   dataX = viewX + horizontalScrollBar()->value();
    if (dataX < 0) return -1;
    const float pps   = m_chartModel->pixelsPerSample();
    if (pps <= 0.f) return -1;
    // dataX is in window-local pixels; the origin maps the window back
    // to absolute sample indices.
    const int sample = m_chartModel->displayOriginSample()
                       + static_cast<int>(dataX / pps + 0.5f);
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
    const int   origin = m_chartModel->displayOriginSample();
    const int   vH     = viewport()->height();
    const int   stripW = qMax(kCursorStripMinWidth, static_cast<int>(pps) + kCursorStripPpsExtra);

    // (s − origin) × pps stays within the window's pixel range (bounded
    // by kMaxSectionPx), so int arithmetic is safe.
    auto toViewX = [&](int s) { return qRound((s - origin) * pps) - scroll; };
    if (oldSample >= 0) viewport()->update(QRect(toViewX(oldSample) - stripW/2, 0, stripW, vH));
    if (newSample >= 0) viewport()->update(QRect(toViewX(newSample) - stripW/2, 0, stripW, vH));
}

void ChartView::onCursorMoved(int sample)
{
    repaintCursorStrip(m_prevCursorSample, sample);
    m_prevCursorSample = sample;
}

// =============================================================================
//  computeGridLabelWidth
//
//  Mirrors the label formatting logic from ChartDelegate::paintBackground()
//  without performing any drawing.  Computed once and cached until the
//  Y range changes; remains valid across horizontal scrolls.
// =============================================================================

int ChartView::computeGridLabelWidth() const
{
    if (!m_chartModel) return kGridLabelFallbackWidth;

    const QFontMetrics fm(QFont("Consolas", kGridLabelFontPt));
    int maxW = 0;

    const int vscroll = verticalScrollBar()->value();
    const int vpH     = viewport()->height();

    int rowY = 0;
    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s    = m_chartModel->series(r);
        const int          rowH = s ? s->rowHeight : m_chartModel->rowHeight();

        if (rowY + rowH > vscroll && rowY < vscroll + vpH && s) {
            const auto [loD, hiD] = effectiveViewBounds(*s);

            if (loD < hiD && std::isfinite(loD) && std::isfinite(hiD)) {
                const double visLo = loD, visHi = hiD;
                const double span  = visHi - visLo;
                const int    chartH = qMax(1, rowH - kChartVPad);

                const double rawStep = span / kGridTargetDivisions;
                if (rawStep > 0 && std::isfinite(rawStep)) {
                    const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
                    double step = mag;
                    if      (rawStep / mag >= kGridStep5x) step = kGridStep5x * mag;
                    else if (rawStep / mag >= kGridStep2x) step = kGridStep2x * mag;

                    const double minStepVal = span * (fm.height() + kGridLabelMinGap) / chartH;
                    if (step < minStepVal) {
                        const double mag2 = std::pow(10.0, std::floor(std::log10(minStepVal)));
                        if      (minStepVal <= 1.0 * mag2) step = 1.0 * mag2;
                        else if (minStepVal <= 2.0 * mag2) step = 2.0 * mag2;
                        else if (minStepVal <= 5.0 * mag2) step = 5.0 * mag2;
                        else                               step = 10.0  * mag2;
                    }

                    for (double v = std::ceil(visLo / step) * step;
                         v <= visHi + step * 0.5; v += step) {
                        maxW = qMax(maxW, fm.horizontalAdvance(formatGridLabel(v, step)));
                    }
                }
            }
        }
        rowY += rowH;
    }

    // kGridLabelWidthMargin: the delegate draws labels at x=3 (left) and
    // x=vpW-labelW-3 (right), so we need 3px on each side = 6px total.
    return maxW + kGridLabelWidthMargin;
}

// =============================================================================
//  scrollContentsBy — extends dirty region to cover Y-axis label strips
// =============================================================================

void ChartView::scrollContentsBy(int dx, int dy)
{
    // The base class performs a blit-scroll: it shifts existing pixels by dx
    // and marks only the newly exposed strip as dirty.  Y-axis labels are
    // painted at fixed left/right edge positions by ChartDelegate, so after
    // the blit they appear at wrong positions and form a ghost trail.
    //
    // We fix this by appending update() calls for the strips that contain
    // labels.  The strip width is computed once (computeGridLabelWidth) and
    // cached; it does not change during a horizontal scroll.
    QTableView::scrollContentsBy(dx, dy);

    if (dx != 0) {
        if (m_gridLabelWidth < 0)
            m_gridLabelWidth = computeGridLabelWidth();

        const int lWidth = m_gridLabelWidth;
        const int vpW    = viewport()->width();
        const int vpH    = viewport()->height();

        // Expand the left/right dirty strips by |dx| in the scroll direction
        // so there is no gap between the blit edge and the repainted label area.
        int lPos = 0,            lLen = lWidth;
        int rPos = vpW - lWidth, rLen = lWidth;
        if (dx < 0) { rLen -= dx; rPos += dx; }
        else        { lLen += dx; }

        viewport()->update(QRect(lPos, 0, lLen, vpH));
        viewport()->update(QRect(rPos, 0, rLen, vpH));
    }
}

// =============================================================================
//  paintEvent — two-pass rendering
// =============================================================================

void ChartView::paintEvent(QPaintEvent *event)
{
    // Pass 1: QTableView draws cell backgrounds and Y-axis grids via the delegate.
    QTableView::paintEvent(event);

    if (!m_chartModel || !m_delegate) return;

    const float pps     = m_chartModel->pixelsPerSample();
    const int   hscroll = horizontalScrollBar()->value();
    const int   vscroll = verticalScrollBar()->value();
    const int   cursor  = m_chartModel->cursorSample();
    const int   vpW     = viewport()->width();
    const int   vpH     = viewport()->height();

    QPainter p(viewport());

    // Translate to content coordinates so row Y values are independent of scroll.
    p.translate(-hscroll, -vscroll);
    // Clip to the actual visible content rectangle. There is no yOffset any
    // more, so polylines never intentionally extend beyond row boundaries.
    p.setClipRect(hscroll, vscroll, vpW, vpH);

    const int clipXLeft  = hscroll;
    const int clipXRight = hscroll + vpW;

    int       rowY      = 0;
    const int visTop    = vscroll;
    const int visBottom = vscroll + vpH;

    // Pass 2: draw signal data for each visible row.
    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s    = m_chartModel->series(r);
        const int          rowH = s ? s->rowHeight : m_chartModel->rowHeight();

        if (rowY > visBottom + rowH) break;

        if (s && rowY + rowH > visTop && rowY < visBottom) {
            const QRect cell(0, rowY, m_chartModel->chartPixelWidth(), rowH);
            p.save();
            m_delegate->paintData(&p, cell, *s, cursor, pps, clipXLeft, clipXRight);
            p.restore();
        }
        rowY += rowH;
    }
}

// =============================================================================
//  Mouse events
// =============================================================================

void ChartView::mousePressEvent(QMouseEvent *e)
{
    if (!m_chartModel) { e->accept(); return; }

    if (e->button() == Qt::RightButton) {
        // Start horizontal pan.  Manual scrolling and Live mode are
        // mutually exclusive: as soon as the user grabs the chart to
        // look back, stop dragging the view to the newest sample.
        if (m_liveMode) setLiveMode(false);
        m_panning        = true;
        m_panStartX      = e->pos().x();
        m_panStartScroll = horizontalScrollBar()->value();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }

    if (e->button() == Qt::LeftButton) {
        // Start vertical Y-pan drag on the row under the cursor.
        // We capture the current effective view bounds in value space so that
        // drag-move can shift them by the correct value delta.
        const int row = viewYToRow(e->pos().y());
        if (row >= 0) {
            const ChartSeries *s = m_chartModel->series(row);
            if (s) {
                const auto [lo, hi] = effectiveViewBounds(*s);
                m_dragging          = true;
                m_dragStartY        = e->pos().y();
                m_dragStartViewLo   = lo;
                m_dragStartViewHi   = hi;
                m_dragRowHeight     = s->rowHeight;
                m_dragSeriesName    = s->name;
                setCursor(Qt::SizeVerCursor);
            }
        }
    }
    e->accept();
}

void ChartView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_chartModel) { e->accept(); return; }

    if (m_panning && (e->buttons() & Qt::RightButton)) {
        const int delta = e->pos().x() - m_panStartX;
        horizontalScrollBar()->setValue(m_panStartScroll - delta);
        e->accept();
        return;
    }

    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        // Convert pixel delta → value delta and shift both view bounds.
        // drag DOWN (positive delta): the graph shifts down visually,
        // meaning the window now shows higher values → both bounds increase.
        const int    delta   = e->pos().y() - m_dragStartY;
        const double chartH  = m_dragRowHeight - kChartVPad;
        if (chartH > 0) {
            const double span          = m_dragStartViewHi - m_dragStartViewLo;
            const double valuePerPixel = span / chartH;
            const double valueDelta    = delta * valuePerPixel; // positive drag → higher values
            m_chartModel->setSeriesViewRange(m_dragSeriesName,
                                             m_dragStartViewLo + valueDelta,
                                             m_dragStartViewHi + valueDelta);
        }
        e->accept();
        return;
    }

    // Update the cursor sample for the hover position.
    const int newSample = viewXToSample(e->pos().x());
    if (newSample != m_chartModel->cursorSample())
        m_chartModel->setCursorSample(newSample);

    e->accept();
}

void ChartView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning && e->button() == Qt::RightButton) {
        m_panning = false;
        unsetCursor();
        e->accept();
        return;
    }
    if (m_dragging) {
        m_dragging = false;
        unsetCursor();
    }
    e->accept();
}

void ChartView::leaveEvent(QEvent *e)
{
    if (m_chartModel) {
        // In live mode, snap cursor to the newest sample so the header
        // keeps showing current values even when the mouse is away.
        if (m_liveMode) {
            const int n = m_chartModel->maxSampleCount();
            m_chartModel->setCursorSample(n > 0 ? n - 1 : -1);
        } else {
            m_chartModel->setCursorSample(-1);
        }
    }
    QTableView::leaveEvent(e);
}

// =============================================================================
//  Wheel event — Ctrl: zoom X  |  Shift: zoom Y  |  Ctrl+Shift: row height
// =============================================================================

void ChartView::wheelEvent(QWheelEvent *e)
{
    if (!m_chartModel) { QTableView::wheelEvent(e); return; }

    const bool ctrlHeld  = e->modifiers() & Qt::ControlModifier;
    const bool shiftHeld = e->modifiers() & Qt::ShiftModifier;
    const int  delta     = e->angleDelta().y();

    if (ctrlHeld && shiftHeld) {
        // Ctrl+Shift+Wheel: change row height.
        const float factor = (delta > 0) ? kWheelRowHeightStep : (1.0f / kWheelRowHeightStep);
        m_chartModel->setRowHeight(static_cast<int>(m_chartModel->rowHeight() * factor));
        e->accept();
        if (m_autoFitY) doAutoFitY();
        return;
    }

    if (ctrlHeld) {
        // Ctrl+Wheel: zoom X, keeping the sample under the cursor stationary.
        const float  factor    = (delta > 0) ? kWheelXZoomStep : (1.0f / kWheelXZoomStep);
        const int    mouseX    = e->position().toPoint().x();
        const int    oldScroll = horizontalScrollBar()->value();
        const float  oldPps    = m_chartModel->pixelsPerSample();
        const float  newPps    = qBound(ChartModel::kMinPps, oldPps * factor, ChartModel::kMaxPps);

        // Convert to absolute sample index (independent of origin/scroll/pps)
        // so we can re-anchor it after the pps + origin change.
        const int    oldOrigin        = m_chartModel->displayOriginSample();
        const double sampleUnderMouse = oldOrigin
                                      + (static_cast<double>(oldScroll) + mouseX) / oldPps;

        m_chartModel->setPpsQuiet(newPps);
        updateDisplayOrigin();   // window/origin depend on pps; recompute before geometry
        const int newOrigin    = m_chartModel->displayOriginSample();
        const int newColWidth  = m_chartModel->chartPixelWidth();
        const int newScroll    = qMax(0, static_cast<int>(std::round(
                                         (sampleUnderMouse - newOrigin) * newPps)) - mouseX);

        horizontalHeader()->resizeSection(0, newColWidth);
        horizontalScrollBar()->setRange(0, qMax(0, newColWidth - viewport()->width()));
        horizontalScrollBar()->setValue(newScroll);
        m_pendingOldSamples = m_chartModel->maxSampleCount();
        m_pendingNewSamples = m_chartModel->maxSampleCount();
        viewport()->update();
        emitVisibleSamplesIfChanged();
        m_chartModel->setCursorSample(
            viewXToSample(viewport()->mapFromGlobal(QCursor::pos()).x()));
        if (m_autoFitY) doAutoFitY();
        e->accept();
        return;
    }

    if (shiftHeld) {
        // Shift+Wheel: zoom the Y range of the row under the cursor.
        // The value under the mouse pointer stays fixed; we shrink/widen
        // [viewLo, viewHi] symmetrically around it.
        const int row = viewYToRow(e->position().toPoint().y());
        if (row >= 0) {
            const ChartSeries *s = m_chartModel->series(row);
            if (s) {
                const auto [lo, hi] = effectiveViewBounds(*s);
                if (hi > lo) {
                    const float  factor = (delta > 0) ? (1.0f / kWheelYZoomStep)
                                                       : kWheelYZoomStep;

                    // Find the value at the mouse Y position so we can keep it
                    // pinned (zoom around the pointer, not the centre).
                    const int    rowTop    = verticalScrollBar()->value();
                    const QModelIndex idx  = indexAt(QPoint(0, e->position().toPoint().y()));
                    const int    rowY      = idx.isValid()
                                                ? rowViewportPosition(idx.row()) : 0;
                    const double chartH    = s->rowHeight - kChartVPad;
                    const double chartTopY = rowY + kChartVPad * 0.5;
                    const double mouseY    = e->position().toPoint().y();
                    const double relY      = mouseY - chartTopY;
                    // ratio: 0 at top (hi), 1 at bottom (lo) — flipped; clamped so
                    // a mouse position outside the chart cell stays at the edge.
                    const double pivotRatio = qBound(0.0, (chartH > 0) ? (relY / chartH) : 0.5, 1.0);
                    const double pivot      = hi - pivotRatio * (hi - lo);

                    const double newHalfLo = (pivot - lo) * factor;
                    const double newHalfHi = (hi - pivot) * factor;
                    m_chartModel->setSeriesViewRange(s->name,
                                                     pivot - newHalfLo,
                                                     pivot + newHalfHi);
                    if (m_autoFitY) setAutoFitY(false);
                }
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
    if (m_autoFitY) doAutoFitY();
}

// =============================================================================
//  Key bindings
// =============================================================================

void ChartView::keyPressEvent(QKeyEvent *e)
{
    if (!m_chartModel) { QTableView::keyPressEvent(e); return; }

    switch (e->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_Minus: {
        // +/= or -: zoom X around the sample under the viewport centre.
        const float  factor    = (e->key() == Qt::Key_Minus) ? (1.0f / kKeyXZoomStep) : kKeyXZoomStep;
        const int    oldScroll = horizontalScrollBar()->value();
        const float  oldPps    = m_chartModel->pixelsPerSample();
        const float  newPps    = qBound(ChartModel::kMinPps, oldPps * factor, ChartModel::kMaxPps);
        const int    centerX   = viewport()->width() / 2;
        const int    oldOrigin = m_chartModel->displayOriginSample();
        const double sampleUnderCenter =
            oldOrigin + (static_cast<double>(oldScroll) + centerX) / oldPps;
        m_chartModel->setPixelsPerSample(newPps);   // syncColumnWidth refreshes origin via layoutChanged
        const int    newOrigin = m_chartModel->displayOriginSample();
        const int    newScroll = qMax(0, static_cast<int>(std::round(
                                          (sampleUnderCenter - newOrigin) * newPps)) - centerX);
        horizontalScrollBar()->setValue(newScroll);
        emitVisibleSamplesIfChanged();
        break;
    }
    case Qt::Key_Up:
        m_chartModel->setRowHeight(static_cast<int>(m_chartModel->rowHeight() * kKeyRowHeightStep));
        break;
    case Qt::Key_Down:
        m_chartModel->setRowHeight(static_cast<int>(m_chartModel->rowHeight() / kKeyRowHeightStep));
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
    if (m_autoFitY) doAutoFitY();
}

int ChartView::visibleSampleCount() const
{
    if (!m_chartModel) return 0;
    const float pps  = m_chartModel->pixelsPerSample();
    if (pps <= 0.f)  return 0;
    const int total  = m_chartModel->maxSampleCount();
    if (total == 0)  return 0;
    const int scroll = horizontalScrollBar()->value();
    const int vpW    = viewport()->width();
    const int origin = m_chartModel->displayOriginSample();
    const int first  = origin + static_cast<int>(scroll / pps);
    const int last   = qMin(total - 1, origin + static_cast<int>((scroll + vpW) / pps));
    return qMax(0, last - first + 1);
}

void ChartView::emitVisibleSamplesIfChanged()
{
    const int count = visibleSampleCount();
    if (count != m_lastVisibleCount) {
        m_lastVisibleCount = count;
        emit visibleSamplesChanged(count,
                                   m_chartModel ? m_chartModel->pixelsPerSample() : 0.0);
    }
}

void ChartView::onDataAppended(const QString & /*name*/, int /*row*/, int newTotalSamples)
{
    m_pendingNewSamples = qMax(m_pendingNewSamples, newTotalSamples);
    if (!m_appendFlushPending) {
        m_appendFlushPending = true;
        QMetaObject::invokeMethod(this, &ChartView::flushPendingAppend,
                                  Qt::QueuedConnection);
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

    // Advance the rendering origin so the (visible-sample × pps) section
    // width never crosses QHeaderView's hard cap (see updateDisplayOrigin).
    // If the origin moves, every prior pixel of the section maps to a
    // different sample, so we must force a full repaint (Pass 1's blit
    // optimisation would otherwise leave stale background pixels behind).
    const int prevOrigin = m_chartModel->displayOriginSample();
    updateDisplayOrigin();
    const int origin = m_chartModel->displayOriginSample();
    const bool originShifted = origin != prevOrigin;

    const int newColWidth = m_chartModel->chartPixelWidth();
    if (horizontalHeader()->sectionSize(0) != newColWidth)
        horizontalHeader()->resizeSection(0, newColWidth);

    if (originShifted) {
        // The whole content shifted; nothing the bitmap currently shows
        // is correct any more.
        viewport()->update();
    } else {
        // Repaint only the newly appended pixel columns.
        const int xStart   = qRound((oldSamples - origin) * static_cast<double>(pps)) - scroll;
        const int xEnd     = newColWidth - scroll;
        const int visLeft  = qMax(xStart, 0);
        const int visRight = qMin(xEnd + 1, vpW);
        if (visLeft < visRight)
            viewport()->update(QRect(visLeft, 0, visRight - visLeft, vpH));
    }

    if (m_liveMode) {
        scrollToEnd();
        // If the mouse is outside the viewport, keep the cursor pinned to
        // the newest sample so the header always reflects the latest data.
        if (!viewport()->underMouse()) {
            const int n = m_chartModel->maxSampleCount();
            m_chartModel->setCursorSample(n > 0 ? n - 1 : -1);
        }
    }

    emitVisibleSamplesIfChanged();
    if (m_autoFitY) doAutoFitY();
}

// =============================================================================
//  Scale / overlay helpers
// =============================================================================

void ChartView::syncScale(int sourceRow, const QSet<int> &rows)
{
    if (!m_chartModel) return;
    const ChartSeries *src = m_chartModel->series(sourceRow);
    if (!src) return;

    const auto [srcLo, srcHi] = effectiveViewBounds(*src);
    const int srcChartH = src->rowHeight - kChartVPad;
    if (srcHi <= srcLo || srcChartH <= 0) return;

    // Units per pixel of the source row.
    const double unitsPerPixel = (srcHi - srcLo) / srcChartH;

    for (int r : rows) {
        if (r == sourceRow) continue;
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        const auto [lo, hi] = effectiveViewBounds(*s);
        const int chartH = s->rowHeight - kChartVPad;
        if (chartH <= 0) continue;
        // Preserve the current centre, apply the source's units-per-pixel.
        const double mid      = (lo + hi) * 0.5;
        const double newRange = unitsPerPixel * chartH;
        m_chartModel->setSeriesViewRange(s->name,
                                         mid - newRange * 0.5,
                                         mid + newRange * 0.5);
    }
}

void ChartView::overlayOnto(int sourceRow, const QSet<int> &rows)
{
    if (!m_chartModel) return;

    // Compute the combined value range of all selected rows.
    double globalLo =  1.0e300, globalHi = -1.0e300;
    for (int r : rows) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        const auto [lo, hi] = effectiveViewBounds(*s);
        if (hi > lo) {
            globalLo = qMin(globalLo, lo);
            globalHi = qMax(globalHi, hi);
        }
    }
    if (globalHi <= globalLo) return;

    for (int r : rows) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        m_chartModel->setSeriesViewRange(s->name, globalLo, globalHi);
    }
}

void ChartView::resetSelected(const QSet<int> &rows)
{
    if (!m_chartModel) return;
    for (int r : rows) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        m_chartModel->resetSeriesView(s->name);
    }
}

// =============================================================================
//  Y-scale zoom for selected rows
// =============================================================================

/**
 * @brief Shared implementation for zoomYScaleIn / zoomYScaleOut.
 *
 * @p factor > 1 expands (zoom out), 0 < factor < 1 shrinks (zoom in).
 * The range is scaled around the current midpoint so the centre of the
 * chart stays fixed.
 */
static void applyYScaleZoom(ChartModel *model, const QSet<int> &rows, double factor)
{
    if (!model || rows.isEmpty()) return;
    for (int r : rows) {
        const ChartSeries *s = model->series(r);
        if (!s) continue;
        const auto [lo, hi] = effectiveViewBounds(*s);
        if (qFuzzyCompare(lo, hi)) continue; // degenerate range – skip
        const double mid      = (lo + hi) * 0.5;
        const double newRange = (hi - lo) * factor;
        model->setSeriesViewRange(s->name,
                                  mid - newRange * 0.5,
                                  mid + newRange * 0.5);
    }
}

void ChartView::zoomYScaleIn(const QSet<int> &rows)
{
    if (m_autoFitY) setAutoFitY(false);
    applyYScaleZoom(m_chartModel, rows, 1.0 / ChartModel::kZoomYStep);
}

void ChartView::zoomYScaleOut(const QSet<int> &rows)
{
    if (m_autoFitY) setAutoFitY(false);
    applyYScaleZoom(m_chartModel, rows, ChartModel::kZoomYStep);
}
