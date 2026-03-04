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

// --- Y-scale clamping --------------------------------------------------------
//
//  kMinYScale is re-declared here for clarity; its canonical definition is
//  ChartModel::kMinYScale.  All scale clamping in this file must use the
//  same pair of bounds — previously fitYToVisible() and doAutoFitY() used
//  qMax(0.1f, …) which was inconsistent with every other call site.

/// Minimum allowed yScale — must equal ChartModel::kMinYScale.
constexpr float kMinYScale = ChartModel::kMinYScale;

/// Maximum allowed yScale (extreme zoom-in).  Shared by syncScale,
/// overlayOnto, wheelEvent and keyPressEvent.
constexpr float kMaxYScale = 1000.0f;

// --- Y-offset clamping -------------------------------------------------------

/// Absolute pixel limit for yOffset computed during fitYToVisible / doAutoFitY.
/// Prevents integer overflow when the visible range is far outside the series bounds.
constexpr double kMaxYOffset = 1.0e7;

// --- Wheel / key zoom step factors -------------------------------------------

/// Row-height step for Ctrl+Shift+Wheel.
constexpr float kWheelRowHeightStep = 1.15f;

/// X-zoom (pps) step for Ctrl+Wheel.
constexpr float kWheelXZoomStep = 1.15f;

/// Y-scale step for Shift+Wheel.
constexpr float kWheelYScaleStep = 1.2f;

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

/// Target number of Y-axis grid intervals — must match ChartDelegate.
constexpr double kGridTargetDivisions = 5.0;
constexpr double kGridStep2x          = 2.0;
constexpr double kGridStep5x          = 5.0;

/// Font used for grid label measurement — must match ChartDelegate.
constexpr int kGridLabelFontPt = 9;

// --- paintEvent clip ---------------------------------------------------------

/// Half-height of the large vertical clip rect used in paintEvent.
/// Large enough to accommodate any yOffset; must exceed the tallest possible row.
constexpr int kPaintClipHalfH = 32768;

// --- overlayOnto initial bounds sentinels ------------------------------------

/// Initial globalLo sentinel (larger than any realistic double value).
constexpr double kOverlayInitLo =  1.0e300;

/// Initial globalHi sentinel (smaller than any realistic double value).
constexpr double kOverlayInitHi = -1.0e300;

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

        const float newScale = qBound(kMinYScale,
                                      static_cast<float>(globalRange / visibleRange),
                                      kMaxYScale);

        const double midGlobal = (boundsToDouble(s->minVal) + boundsToDouble(s->maxVal)) * 0.5;
        const double midVis    = (vr.lo + vr.hi) * 0.5;
        const int    rowH      = s->rowHeight - kChartVPad;
        const double rawOffset = (midVis - midGlobal) / globalRange * rowH * newScale;
        const int    newOffset = static_cast<int>(qBound(-kMaxYOffset, rawOffset, kMaxYOffset));

        m_chartModel->setSeriesYScale(s->name, newScale);
        m_chartModel->setSeriesYOffset(s->name, newOffset);
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

void ChartView::doAutoFitY()
{
    if (!m_autoFitY || !m_chartModel) return;
    for (int r = 0; r < m_chartModel->rowCount(); ++r) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        const auto vr = visibleRangeForRow(r);
        if (!vr.valid) continue;
        const double globalRange  = boundsToDouble(s->maxVal) - boundsToDouble(s->minVal);
        const double visibleRange = vr.hi - vr.lo;
        if (globalRange <= 0 || visibleRange <= 0) continue;
        const float  newScale  = qBound(kMinYScale,
                                      static_cast<float>(globalRange / visibleRange),
                                      kMaxYScale);
        const double midGlobal = (boundsToDouble(s->minVal) + boundsToDouble(s->maxVal)) * 0.5;
        const double midVis    = (vr.lo + vr.hi) * 0.5;
        const int    rowH      = s->rowHeight - kChartVPad;
        const double rawOffset = (midVis - midGlobal) / globalRange * rowH * newScale;
        const int    newOffset = static_cast<int>(qBound(-kMaxYOffset, rawOffset, kMaxYOffset));
        m_chartModel->setSeriesYScale(s->name, newScale);
        m_chartModel->setSeriesYOffset(s->name, newOffset);
    }
}

int ChartView::viewXToSample(int viewX) const
{
    if (!m_chartModel) return -1;
    const int   dataX  = viewX + horizontalScrollBar()->value();
    if (dataX < 0) return -1;
    const float pps    = m_chartModel->pixelsPerSample();
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
    const int   stripW = qMax(kCursorStripMinWidth, static_cast<int>(pps) + kCursorStripPpsExtra);

    auto toViewX = [&](int s) { return qRound(s * pps) - scroll; };
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
            const double loD = boundsToDouble(s->minVal);
            const double hiD = boundsToDouble(s->maxVal);

            if (loD < hiD && std::isfinite(loD) && std::isfinite(hiD)) {
                const int    chartH  = rowH - kChartVPad;
                const double centerY = rowY + rowH * 0.5;
                const double denom   = chartH * static_cast<double>(s->yScale);
                const double span    = hiD - loD;

                auto valAtY = [&](double y) {
                    return loD + (0.5 - (y - centerY - s->yOffset) / denom) * span;
                };
                const double visHi = valAtY(static_cast<double>(rowY));
                const double visLo = valAtY(static_cast<double>(rowY + rowH));

                if (visHi > visLo) {
                    const double rawStep = (visHi - visLo) / kGridTargetDivisions;
                    if (rawStep > 0 && std::isfinite(rawStep)) {
                        const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
                        double step = mag;
                        if      (rawStep / mag >= kGridStep5x) step = kGridStep5x * mag;
                        else if (rawStep / mag >= kGridStep2x) step = kGridStep2x * mag;

                        for (double v = std::ceil(visLo / step) * step;
                             v <= visHi + step * 0.5; v += step) {
                            QString label;
                            if (std::abs(v) >= 1e6 || (std::abs(v) < 1e-3 && v != 0.0))
                                label = QString::number(v, 'e', 2);
                            else
                                label = QString::number(v, 'g', 4);
                            maxW = qMax(maxW, fm.horizontalAdvance(label));
                        }
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
    p.setClipRect(hscroll, -kPaintClipHalfH, vpW, kPaintClipHalfH * 2);

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

        if (s && rowY + rowH > visTop) {
            // Extend the vertical clip by yOffset so out-of-bounds polylines are visible.
            const int yOff   = s->yOffset;
            const int top    = rowY + qMin(0, yOff) - rowH;
            const int bottom = rowY + rowH + qMax(0, yOff) + rowH;
            if (bottom >= visTop && top <= visBottom) {
                const QRect cell(0, rowY, m_chartModel->chartPixelWidth(), rowH);
                p.save();
                m_delegate->paintData(&p, cell, *s, cursor, pps, clipXLeft, clipXRight);
                p.restore();
            }
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
        // Start horizontal pan.
        m_panning        = true;
        m_panStartX      = e->pos().x();
        m_panStartScroll = horizontalScrollBar()->value();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }

    if (e->button() == Qt::LeftButton) {
        // Start vertical Y-offset drag on the row under the cursor.
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

    if (m_panning && (e->buttons() & Qt::RightButton)) {
        const int delta = e->pos().x() - m_panStartX;
        horizontalScrollBar()->setValue(m_panStartScroll - delta);
        e->accept();
        return;
    }

    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        const int delta  = e->pos().y() - m_dragStartY;
        const int newOff = m_dragStartOffset + delta;
        m_chartModel->setSeriesYOffset(m_dragSeriesName, newOff);
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
    if (m_chartModel) m_chartModel->setCursorSample(-1);
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
        // Ctrl+Wheel: zoom X, keeping the pixel under the cursor stationary.
        const float factor    = (delta > 0) ? kWheelXZoomStep : (1.0f / kWheelXZoomStep);
        const int   mouseX    = e->position().toPoint().x();
        const int   oldScroll = horizontalScrollBar()->value();
        const float oldPps    = m_chartModel->pixelsPerSample();
        const float newPps    = qBound(ChartModel::kMinPps, oldPps * factor, ChartModel::kMaxPps);
        const int   dataX     = mouseX + oldScroll;
        const int   newScroll = qMax(0, qRound(dataX * (newPps / oldPps)) - mouseX);
        m_chartModel->setPpsQuiet(newPps);
        horizontalHeader()->resizeSection(
            0, qRound(m_chartModel->maxSampleCount() * static_cast<double>(newPps)));
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
        // Shift+Wheel: zoom the Y scale of the row under the cursor.
        const int row = viewYToRow(e->position().toPoint().y());
        if (row >= 0) {
            const ChartSeries *s = m_chartModel->series(row);
            if (s) {
                const float factor   = (delta > 0) ? kWheelYScaleStep : (1.0f / kWheelYScaleStep);
                const float newScale = qBound(kMinYScale, s->yScale * factor, kMaxYScale);
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
        // +/= or -: zoom X around the viewport centre.
        const float factor    = (e->key() == Qt::Key_Minus) ? (1.0f / kKeyXZoomStep) : kKeyXZoomStep;
        const int   oldScroll = horizontalScrollBar()->value();
        const float oldPps    = m_chartModel->pixelsPerSample();
        const float newPps    = qBound(ChartModel::kMinPps, oldPps * factor, ChartModel::kMaxPps);
        const int   centerX   = viewport()->width() / 2;
        const int   dataX     = centerX + oldScroll;
        const int   newScroll = qMax(0, qRound(dataX * (newPps / oldPps)) - centerX);
        m_chartModel->setPixelsPerSample(newPps);
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
    const int first  = static_cast<int>(scroll / pps);
    const int last   = qMin(total - 1, static_cast<int>((scroll + vpW) / pps));
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

    const int newColWidth = qRound(newSamples * static_cast<double>(pps));
    if (horizontalHeader()->sectionSize(0) != newColWidth)
        horizontalHeader()->resizeSection(0, newColWidth);

    // Repaint only the newly appended pixel columns.
    const int xStart   = qRound(oldSamples * static_cast<double>(pps)) - scroll;
    const int xEnd     = newColWidth - scroll;
    const int visLeft  = qMax(xStart, 0);
    const int visRight = qMin(xEnd + 1, vpW);
    if (visLeft < visRight)
        viewport()->update(QRect(visLeft, 0, visRight - visLeft, vpH));

    emitVisibleSamplesIfChanged();
    if (m_autoFitY) doAutoFitY();
}

// =============================================================================
//  Scale / overlay helpers
// =============================================================================

static int rowTopY(ChartModel *model, int row)
{
    int y = 0;
    for (int i = 0; i < row; ++i) {
        const ChartSeries *s = model->series(i);
        y += s ? s->rowHeight : model->rowHeight();
    }
    return y;
}

static double rowCenterY(ChartModel *model, int row)
{
    const int          top = rowTopY(model, row);
    const ChartSeries *s   = model->series(row);
    const int          rh  = s ? s->rowHeight : model->rowHeight();
    return top + rh * 0.5;
}

void ChartView::syncScale(int sourceRow, const QSet<int> &rows)
{
    if (!m_chartModel) return;
    const ChartSeries *src = m_chartModel->series(sourceRow);
    if (!src) return;

    const double srcRange  = boundsToDouble(src->maxVal) - boundsToDouble(src->minVal);
    const int    srcChartH = src->rowHeight - kChartVPad;
    if (srcRange <= 0 || srcChartH <= 0) return;

    const double unitsPerPixel = srcRange / (srcChartH * static_cast<double>(src->yScale));

    for (int r : rows) {
        if (r == sourceRow) continue;
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        const double range  = boundsToDouble(s->maxVal) - boundsToDouble(s->minVal);
        const int    chartH = s->rowHeight - kChartVPad;
        if (range <= 0 || chartH <= 0) continue;
        const float newScale = qBound(kMinYScale,
                                      static_cast<float>(range / (chartH * unitsPerPixel)),
                                      kMaxYScale);
        m_chartModel->setSeriesYScale(s->name, newScale);
    }
}

void ChartView::overlayOnto(int sourceRow, const QSet<int> &rows)
{
    if (!m_chartModel) return;

    double globalLo = kOverlayInitLo, globalHi = kOverlayInitHi;
    for (int r : rows) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        globalLo = qMin(globalLo, boundsToDouble(s->minVal));
        globalHi = qMax(globalHi, boundsToDouble(s->maxVal));
    }
    if (globalHi <= globalLo) return;

    const double combinedRange = globalHi - globalLo;
    const double globalMid     = (globalLo + globalHi) * 0.5;

    const ChartSeries *srcS = m_chartModel->series(sourceRow);
    if (!srcS) return;
    const int srcChartH = srcS->rowHeight - kChartVPad;
    if (srcChartH <= 0) return;

    constexpr double fillFactor = 0.85;
    const double K = srcChartH * fillFactor / combinedRange;

    const double srcCenterY = rowCenterY(m_chartModel, sourceRow);

    for (int r : rows) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        const double range  = boundsToDouble(s->maxVal) - boundsToDouble(s->minVal);
        const int    chartH = s->rowHeight - kChartVPad;
        if (range <= 0 || chartH <= 0) continue;
        const float  newScale  = qBound(kMinYScale,
                                      static_cast<float>(K * range / chartH),
                                      kMaxYScale);
        const double sCenterY  = rowCenterY(m_chartModel, r);
        const double mid       = (boundsToDouble(s->minVal) + boundsToDouble(s->maxVal)) * 0.5;
        const int    newOffset = static_cast<int>(K * (globalMid - mid) + (srcCenterY - sCenterY));
        m_chartModel->setSeriesYScale(s->name, newScale);
        m_chartModel->setSeriesYOffset(s->name, newOffset);
    }

    // Apply the same transform to the source row itself.
    {
        const double range  = boundsToDouble(srcS->maxVal) - boundsToDouble(srcS->minVal);
        const int    chartH = srcChartH;
        if (range > 0 && chartH > 0) {
            const float  newScale  = qBound(kMinYScale,
                                          static_cast<float>(K * range / chartH),
                                          kMaxYScale);
            const double mid       = (boundsToDouble(srcS->minVal) + boundsToDouble(srcS->maxVal)) * 0.5;
            const int    newOffset = static_cast<int>(K * (globalMid - mid));
            m_chartModel->setSeriesYScale(srcS->name, newScale);
            m_chartModel->setSeriesYOffset(srcS->name, newOffset);
        }
    }
}

void ChartView::resetSelected(const QSet<int> &rows)
{
    if (!m_chartModel) return;
    for (int r : rows) {
        const ChartSeries *s = m_chartModel->series(r);
        if (!s) continue;
        m_chartModel->setSeriesYScale(s->name, 1.0f);
        m_chartModel->setSeriesYOffset(s->name, 0);
    }
}
