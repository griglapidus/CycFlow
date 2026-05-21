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

// kChartVPad, kGridTargetDivisions, kGridStep2x/5x, kGridLabelFontPt,
// kGridLabelMinGap are defined in ChartDefs.h — single source of truth.

/// Horizontal margin between the viewport edge and a grid label (pixels).
constexpr int kGridLabelMarginX = 3;

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

    // Use the explicit view bounds if set, otherwise the running data bounds.
    const auto [loD, hiD] = effectiveViewBounds(s);
    if (loD >= hiD || !std::isfinite(loD) || !std::isfinite(hiD)) return;

    const int    chartH   = r.height() - kChartVPad;
    if (chartH <= 0) return;
    const double chartTop = r.top() + kChartVPad * 0.5; // top of the usable chart area
    const double span     = hiD - loD;

    // The visible value range is exactly [loD, hiD] — no yScale/yOffset math needed.
    const double visLo = loD;
    const double visHi = hiD;

    QFont lf("Consolas", kGridLabelFontPt);
    p->setFont(lf);
    const QFontMetrics fm(lf);
    const int labelH = fm.height();
    int       prevLy = INT_MIN;

    // Pass 1: finest "nice" step for ~kGridTargetDivisions divisions (snap down).
    const double rawStep = span / kGridTargetDivisions;
    if (rawStep <= 0 || !std::isfinite(rawStep)) return;
    const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
    double step = mag;
    if      (rawStep / mag >= kGridStep5x) step = kGridStep5x * mag;
    else if (rawStep / mag >= kGridStep2x) step = kGridStep2x * mag;

    // Pass 2: if adjacent labels would crowd at this step, snap UP to the next
    // nice step that guarantees enough vertical spacing.  Without this, the
    // crowding filter skips alternate labels and flips phase (even ↔ odd
    // grid lines) each time the range shifts by one step in live/auto-fit mode.
    const double minStepVal = span * (labelH + kGridLabelMinGap) / chartH;
    if (step < minStepVal) {
        const double mag2 = std::pow(10.0, std::floor(std::log10(minStepVal)));
        if      (minStepVal <= 1.0 * mag2) step = 1.0 * mag2;
        else if (minStepVal <= 2.0 * mag2) step = 2.0 * mag2;
        else if (minStepVal <= 5.0 * mag2) step = 5.0 * mag2;
        else                               step = 10.0  * mag2;
    }

    const double firstGrid = std::ceil(visLo / step) * step;
    for (int k = 0; firstGrid + k * step <= visHi + step * 0.5; ++k) {
        const double v     = firstGrid + k * step;
        // ratio=0 → bottom of chart (loD), ratio=1 → top (hiD)
        const double ratio = (v - loD) / span;
        const double yLine = chartTop + (1.0 - ratio) * chartH;
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

        const QString label = formatGridLabel(v, step);

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

    // Resolve the Y axis range. viewLo/viewHi are used when explicitly set;
    // otherwise fall back to the running min/max. This is the key fix:
    // new samples change minVal/maxVal but NOT viewLo/viewHi, so the chart
    // does not rescale or jump when data with growing range arrives.
    // Note: split into two named variables (not a structured binding) so that
    // the C++17 lambdas below can capture them without a C++20 extension.
    const auto   viewBounds = effectiveViewBounds(s);
    const double loD        = viewBounds.first;
    const double hiD        = viewBounds.second;
    if (loD >= hiD || !std::isfinite(loD) || !std::isfinite(hiD)) return;

    const int    chartH   = cell.height() - kChartVPad;
    if (chartH <= 0) return;
    const double chartTop = cell.top() + kChartVPad * 0.5;
    const double span     = hiD - loD;

    // Clamp the horizontal clip to the cell bounds.
    const int cLeft  = qMax(clipXLeft,  cell.left());
    const int cRight = qMin(clipXRight, cell.right());
    if (cLeft > cRight) return;

    // Rendering origin: sample `origin` is drawn at content x = 0.
    // All sample-pixel conversions below subtract / add it.
    const int origin = m_model->displayOriginSample();

    // Derive the sample range visible inside the clip region.
    const float dataLeft  = static_cast<float>(cLeft  - cell.left());
    const float dataRight = static_cast<float>(cRight - cell.left());
    const int first = qMax(origin, origin + static_cast<int>(dataLeft / pps));
    const int last  = qMin(dataSize - 1, origin + static_cast<int>((dataRight + pps) / pps));
    if (first > last) return;

    // Converts sample index → scene point using the value-space Y bounds.
    // Values outside [loD, hiD] produce y coordinates outside the cell rect
    // (graph goes off the edge when zoomed in), which is desired behaviour.
    auto toPoint = [&](int i) -> QPointF {
        const double v     = sampleAt(s.data, i);
        const double ratio = (span > 0.0) ? (v - loD) / span : 0.5;
        return { cell.left() + (i - origin) * static_cast<double>(pps),
                chartTop    + (1.0 - ratio) * chartH };
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
        //
        // Per-pixel emission writes four points (enter, pMax, pMin, exit) at
        // the same x coordinate.  drawPolyline connects them as:
        //   enter → pMax  (vertical excursion up)
        //   pMax  → pMin  (vertical span = min/max bar)
        //   pMin  → exit  (vertical return)
        //
        // Envelope continuity: when the current pixel's [pMin, pMax] is
        // disjoint from the previous pixel's range, we stretch the closer
        // boundary of the current bar so that consecutive bars always touch.
        // This eliminates visible gaps in the envelope and makes the trace
        // read as one continuous shape even when the signal slope between
        // two adjacent samples is much larger than the local oscillation.
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

        PixelData cur;
        QPolygonF poly;
        poly.reserve(visPixels * 4);

        // Previous emitted pixel's [pMin, pMax] — for the continuity step.
        bool   hasPrev  = false;
        double prevPMin = 0.0;
        double prevPMax = 0.0;

        // Converts a normalised ratio to a Y scene coordinate.
        auto ratioToY = [&](double ratio) {
            return chartTop + (1.0 - ratio) * chartH;
        };

        auto emitPixel = [&](PixelData pd) {
            if (!pd.used) return;

            // If the new bar lies entirely above or below the previous one,
            // pull its near boundary to touch the previous range so the
            // envelope stays visually connected.
            if (hasPrev) {
                if (pd.pMin > prevPMax)      pd.pMin = prevPMax;
                else if (pd.pMax < prevPMin) pd.pMax = prevPMin;
            }

            const double x = cLeft + pd.px + 0.5;
            poly.append(QPointF(x, ratioToY(pd.enterRatio)));
            poly.append(QPointF(x, ratioToY(pd.pMax)));
            poly.append(QPointF(x, ratioToY(pd.pMin)));
            poly.append(QPointF(x, ratioToY(pd.exitRatio)));

            prevPMin = pd.pMin;
            prevPMax = pd.pMax;
            hasPrev  = true;
        };

        for (int i = first; i <= last; ++i) {
            const int    px    = qBound(0, static_cast<int>((i - origin) * pps) - iDataLeft, visPixels);
            const double v     = sampleAt(s.data, i);
            const double ratio = (span > 0.0) ? (v - loD) / span : 0.5;

            if (px != cur.px) {
                if (cur.used) emitPixel(cur);
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
        if (cur.used) emitPixel(cur);

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
    // Cursor is an absolute sample index; apply the same origin offset.
    if (cursor >= origin && cursor < dataSize) {
        const double cx = cell.left() + (cursor - origin) * static_cast<double>(pps);
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
