// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "ChartDelegate.h"

#include <QApplication>
#include <QPainter>
#include <QFontMetrics>
#include <cmath>

// =============================================================================
//  ChartPalette — colour set derived from the application QPalette
//
//  All rendering colours are resolved at paint time from QPalette so that
//  the delegate automatically follows dark and light themes without any
//  code changes.
//
//  The cursor colour is intentionally kept as a hard-coded warm accent:
//  it carries functional meaning and must be visible on any background.
// =============================================================================

namespace {

// =============================================================================
//  Rendering constants
// =============================================================================

// kChartVPad is defined in ChartDefs.h (included via ChartDelegate.h → ChartModel.h).
// It is NOT redeclared here to keep a single source of truth.

/// Target number of Y-axis grid intervals (drives "nice step" calculation).
constexpr double kGridTargetDivisions = 5.0;

/// Grid step is snapped to 1×, 2× or 5× the leading power-of-ten magnitude.
constexpr double kGridStep2x = 2.0;
constexpr double kGridStep5x = 5.0;

/// Font size for Y-axis grid labels (Consolas), in points.
constexpr int kGridLabelFontPt = 9;

/// Horizontal margin between the viewport edge and a grid label (pixels).
constexpr int kGridLabelMarginX = 3;

/// Minimum pixel gap between consecutive Y-axis labels to avoid overlap.
constexpr int kGridLabelMinGap = 2;

/// Minimum horizontal gap between the left-side and right-side label columns.
/// Prevents the two columns from overlapping on narrow rows.
constexpr int kGridLabelMinSeparation = 10;

/// pps threshold above which individual sample dots are drawn.
constexpr float kDotThresholdPps = 4.0f;

/// Sample dot radius = pps × kDotRadiusFraction, clamped to kDotRadiusMax.
constexpr float kDotRadiusFraction = 0.25f;
constexpr float kDotRadiusMax      = 4.0f;

/// lighter() factor applied to the series colour when drawing sample dots.
constexpr int kDotLighterFactor = 130;

/// Alpha of the sample dot fill colour.
constexpr int kDotAlpha = 210;

/// Signal polyline pen width (cosmetic — 1 device pixel regardless of DPI).
constexpr float kLinePenWidth = 1.0f;

/// Cursor vertical line colour — warm accent, intentionally palette-independent.
/// Semantic indicators must remain visible regardless of the colour scheme.
/// NOTE: QColor is not constexpr before Qt 6.4; declared as a local static
///       in the functions that use it to avoid static-init-order issues.
constexpr int kCursorColorR = 255;
constexpr int kCursorColorG = 200;
constexpr int kCursorColorB = 64;
constexpr int kCursorColorA = 200;

/// Cursor vertical line pen width.
constexpr float kCursorLinePenWidth = 1.0f;

/// darker() factor applied to QPalette::Base for the cursor dot outline.
constexpr int kCursorDotOutlineDarker = 200;

/// Cursor dot outline pen width.
constexpr float kCursorDotOutlinePenWidth = 1.5f;

/// Minimum cursor dot radius (pixels); applied even when pps < kDotThresholdPps.
constexpr float kCursorDotMinRadius = 3.5f;

/// Extra radius added to the sample-dot radius to size the cursor dot.
constexpr float kCursorDotRadiusExtra = 1.0f;

// =============================================================================
//  ChartPalette
// =============================================================================

struct ChartPalette {
    QColor bg;        ///< Plot area background  (QPalette::Base)
    QColor divider;   ///< Horizontal row divider (QPalette::Mid)
    QColor grid;      ///< Y-axis grid lines      (QPalette::Mid, semi-transparent)
    QColor gridLabel; ///< Y-axis value labels    (QPalette::Disabled WindowText)

    static ChartPalette from(const QPalette &p)
    {
        ChartPalette c;
        c.bg      = p.color(QPalette::Base);
        c.divider = p.color(QPalette::Mid);

        // Grid lines reuse Mid at reduced opacity so they sit naturally on any Base.
        QColor g = p.color(QPalette::Mid);
        g.setAlpha(100);
        c.grid = g;

        // Disabled WindowText provides a subdued label colour in both themes.
        c.gridLabel = p.color(QPalette::Disabled, QPalette::WindowText);
        return c;
    }
};

} // anonymous namespace

// =============================================================================
//  ChartDelegate
// =============================================================================

ChartDelegate::ChartDelegate(ChartModel *model, QObject *parent)
    : QStyledItemDelegate(parent), m_model(model) {}

QSize ChartDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {m_model->chartPixelWidth(), m_model->rowHeight()};
}

// -----------------------------------------------------------------------------
//  paint() — Pass 1: background and Y grid
// -----------------------------------------------------------------------------

void ChartDelegate::paint(QPainter *p,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const
{
    const auto pv = index.data(ChartModel::SeriesPointerRole);
    if (!pv.isValid()) return;
    const auto *s = static_cast<const ChartSeries *>(pv.value<const void *>());

    p->save();
    {
        // Expand the clip to the full viewport width so that grid lines and
        // the background fill reach the right edge even when scrolled.
        const QRect fullR(0, option.rect.top(),
                          p->device()->width(), option.rect.height());
        p->setClipRect(fullR, Qt::ReplaceClip);
    }
    paintBackground(p, option.rect, *s);
    p->restore();
}

// -----------------------------------------------------------------------------
//  paintBackground() — background fill, horizontal divider, Y grid + labels
// -----------------------------------------------------------------------------

void ChartDelegate::paintBackground(QPainter *p, const QRect &r,
                                    const ChartSeries &s) const
{
    const ChartPalette cp = ChartPalette::from(QApplication::palette());

    // clipR is the actually dirty rectangle intersected with the cell.
    const QRect clipR = p->clipBoundingRect().toAlignedRect().intersected(r);
    if (clipR.isEmpty()) return;

    // Background fill and the divider line span the full viewport width.
    const int fullRight = p->device()->width() - 1;
    const QRect fullClip(clipR.left(), r.top(), fullRight - clipR.left() + 1, r.height());
    p->fillRect(fullClip, cp.bg);
    p->setPen(QPen(cp.divider, 1));
    p->drawLine(clipR.left(), r.bottom(), fullRight, r.bottom());

    const double loD = boundsToDouble(s.minVal);
    const double hiD = boundsToDouble(s.maxVal);
    if (loD >= hiD || !std::isfinite(loD) || !std::isfinite(hiD)) return;

    const int chartH = r.height() - kChartVPad;
    if (chartH <= 0) return;
    const double centerY = r.top() + r.height() * 0.5;
    const double denom   = chartH * static_cast<double>(s.yScale);
    const double span    = hiD - loD;

    // Compute the visible value range for this row.
    auto valAtY = [&](double y) {
        return loD + (0.5 - (y - centerY - s.yOffset) / denom) * span;
    };
    const double visHi = valAtY(r.top());
    const double visLo = valAtY(r.bottom());
    if (visHi <= visLo) return;

    // Choose a "nice" grid step based on ~kGridTargetDivisions visible intervals.
    const double rawStep = (visHi - visLo) / kGridTargetDivisions;
    if (rawStep <= 0 || !std::isfinite(rawStep)) return;
    const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
    double step = mag;
    if      (rawStep / mag >= kGridStep5x) step = kGridStep5x * mag;
    else if (rawStep / mag >= kGridStep2x) step = kGridStep2x * mag;

    QFont lf("Consolas", kGridLabelFontPt);
    p->setFont(lf);
    const QFontMetrics fm(lf);
    const int labelH = fm.height();
    int       prevLy = INT_MIN;

    for (double v = std::ceil(visLo / step) * step; v <= visHi + step * 0.5; v += step) {
        const double yLine = centerY + (0.5 - (v - loD) / span) * denom + s.yOffset;
        if (yLine < r.top() - 0.5 || yLine > r.bottom() + 0.5) continue;

        // Horizontal grid line across the full viewport width.
        p->setPen(QPen(cp.grid, 1));
        p->drawLine(QPointF(clipR.left(), yLine), QPointF(fullRight, yLine));

        // Place the label above the line; flip below if it would clip the top.
        const int yI = static_cast<int>(yLine);
        int ly = yI - 3;
        if (ly - fm.ascent() < r.top() + 2) ly = yI + fm.ascent() + 2;
        if (ly - fm.ascent() > r.bottom()) continue;
        if (prevLy != INT_MIN && std::abs(ly - prevLy) < labelH + kGridLabelMinGap) continue;
        prevLy = ly;

        // Format: scientific for very large/small values, compact otherwise.
        QString label;
        if (std::abs(v) >= 1e6 || (std::abs(v) < 1e-3 && v != 0.0))
            label = QString::number(v, 'e', 2);
        else
            label = QString::number(v, 'g', 4);

        // Draw the label at the left edge and mirror it at the right edge.
        p->setPen(cp.gridLabel);
        p->drawText(kGridLabelMarginX, ly, label);

        const int labelW  = fm.horizontalAdvance(label);
        const int rLabelX = p->device()->width() - labelW - kGridLabelMarginX;
        if (rLabelX > labelW + kGridLabelMinSeparation)
            p->drawText(rLabelX, ly, label);
    }
}

// -----------------------------------------------------------------------------
//  paintData() / paintDataImpl() — Pass 2: signal polyline, dots, cursor
// -----------------------------------------------------------------------------

void ChartDelegate::paintData(QPainter *p, const QRect &cell,
                              const ChartSeries &s, int cursor, float pps,
                              int clipXLeft, int clipXRight) const
{
    paintDataImpl(p, cell, s, cursor, pps, clipXLeft, clipXRight);
}

void ChartDelegate::paintDataImpl(QPainter *p, const QRect &cell,
                                  const ChartSeries &s, int cursor, float pps,
                                  int clipXLeft, int clipXRight) const
{
    const int dataSize = sampleCount(s.data);
    if (dataSize == 0) return;

    const double loD = boundsToDouble(s.minVal);
    const double hiD = boundsToDouble(s.maxVal);
    if (loD > hiD) return;

    const int chartH = cell.height() - kChartVPad;
    if (chartH <= 0) return;
    const double centerY = cell.top() + cell.height() * 0.5;
    const double denom   = chartH * static_cast<double>(s.yScale);

    // Clamp the horizontal clip to the cell bounds.
    const int cLeft  = qMax(clipXLeft,  cell.left());
    const int cRight = qMin(clipXRight, cell.right());
    if (cLeft > cRight) return;

    // Derive the sample range visible inside the clip region.
    const float dataLeft  = static_cast<float>(cLeft  - cell.left());
    const float dataRight = static_cast<float>(cRight - cell.left());
    const int first = qMax(0, static_cast<int>(dataLeft / pps));
    const int last  = qMin(dataSize - 1, static_cast<int>((dataRight + pps) / pps));
    if (first > last) return;

    auto toPoint = [&](int i) -> QPointF {
        const double ratio = sampleRatio(s.data, i, s.minVal, s.maxVal);
        return { cell.left() + i * static_cast<double>(pps),
                centerY + (0.5 - ratio) * denom + s.yOffset };
    };

    p->setRenderHint(QPainter::Antialiasing, true);
    QPen lp(s.color, kLinePenWidth); lp.setCosmetic(true);
    p->setPen(lp);

    if (pps >= 1.0f) {
        // One pixel or more per sample: draw a straight polyline.
        QPolygonF poly;
        poly.reserve(last - first + 1);
        for (int i = first; i <= last; ++i) poly.append(toPoint(i));
        p->drawPolyline(poly);
    } else {
        // Multiple samples per pixel: reduce to per-pixel min/max envelopes
        // so no data excursion is lost even at extreme zoom-out levels.
        struct PixelData {
            int    px         = -1;
            double enterRatio = 0.0;
            double exitRatio  = 0.0;
            double pMin       = 1.0;
            double pMax       = 0.0;
            bool   used       = false;
        };

        const int visPixels  = cRight - cLeft + 2;
        const int iDataLeft  = static_cast<int>(dataLeft);

        PixelData prev, cur;
        QPolygonF poly;
        poly.reserve(visPixels * 2);

        auto emitPixel = [&](const PixelData &pd) {
            if (!pd.used) return;
            const double x  = cLeft + pd.px + 0.5;
            const double yX = centerY + (0.5 - pd.exitRatio) * denom + s.yOffset;
            const double yN = centerY + (0.5 - pd.pMin)      * denom + s.yOffset;
            const double yP = centerY + (0.5 - pd.pMax)      * denom + s.yOffset;
            poly.append(QPointF(x, yP));
            poly.append(QPointF(x, yN));
            poly.append(QPointF(x, yX));
        };

        for (int i = first; i <= last; ++i) {
            const int    px    = qBound(0, static_cast<int>(i * pps) - iDataLeft, visPixels);
            const double ratio = sampleRatio(s.data, i, s.minVal, s.maxVal);

            if (px != cur.px) {
                if (cur.used) {
                    if (prev.used) {
                        const double xP = cLeft + prev.px + 0.5;
                        const double xC = cLeft + px + 0.5;
                        const double yP = centerY + (0.5 - prev.exitRatio) * denom + s.yOffset;
                        const double yC = centerY + (0.5 - ratio)          * denom + s.yOffset;
                        poly.append(QPointF(xP, yP));
                        poly.append(QPointF(xC, yC));
                    }
                    emitPixel(cur);
                }
                prev = cur;
                cur.px         = px;
                cur.enterRatio = ratio;
                cur.exitRatio  = ratio;
                cur.pMin       = ratio;
                cur.pMax       = ratio;
                cur.used       = true;
            } else {
                if (ratio < cur.pMin) cur.pMin = ratio;
                if (ratio > cur.pMax) cur.pMax = ratio;
                cur.exitRatio = ratio;
            }
        }
        if (cur.used) {
            if (prev.used) {
                const double xP = cLeft + prev.px + 0.5;
                const double xC = cLeft + cur.px  + 0.5;
                const double yP = centerY + (0.5 - prev.exitRatio) * denom + s.yOffset;
                const double yC = centerY + (0.5 - cur.enterRatio) * denom + s.yOffset;
                poly.append(QPointF(xP, yP));
                poly.append(QPointF(xC, yC));
            }
            emitPixel(cur);
        }
        if (!poly.isEmpty())
            p->drawPolyline(poly);
    }

    // Sample dots — drawn only when zoom is sufficient to distinguish them.
    if (pps >= kDotThresholdPps) {
        const float dr = qMin(pps * kDotRadiusFraction, kDotRadiusMax);
        QColor dc = s.color.lighter(kDotLighterFactor);
        dc.setAlpha(kDotAlpha);
        p->setPen(Qt::NoPen);
        p->setBrush(dc);
        for (int i = first; i <= last; ++i) p->drawEllipse(toPoint(i), dr, dr);
    }

    // Cursor marker — a warm accent colour that stands out on any background.
    // Intentionally not taken from QPalette: semantic indicators must not
    // change appearance when the colour scheme is switched.
    if (cursor >= 0 && cursor < dataSize) {
        const double cx = cell.left() + cursor * static_cast<double>(pps);
        if (cx >= cLeft && cx <= cRight) {
            static const QColor kCursorColor(kCursorColorR, kCursorColorG,
                                             kCursorColorB, kCursorColorA);
            p->setPen(QPen(kCursorColor, kCursorLinePenWidth));
            p->drawLine(QPointF(cx, cell.top()), QPointF(cx, cell.bottom()));

            const float dr  = (pps >= kDotThresholdPps)
                                 ? qMin(pps * kDotRadiusFraction, kDotRadiusMax) : 0.f;
            const float cdr = qMax(dr + kCursorDotRadiusExtra, kCursorDotMinRadius);
            p->setPen(QPen(QApplication::palette().color(QPalette::Base)
                               .darker(kCursorDotOutlineDarker), kCursorDotOutlinePenWidth));
            p->setBrush(kCursorColor);
            p->drawEllipse(toPoint(cursor), cdr, cdr);
        }
    }
}
