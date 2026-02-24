#include "ChartDelegate.h"

#include <QPainter>
#include <QFontMetrics>
#include <cmath>

ChartDelegate::ChartDelegate(ChartModel *model, QObject *parent)
    : QStyledItemDelegate(parent), m_model(model) {}

QSize ChartDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {m_model->chartPixelWidth(), m_model->rowHeight()};
}

void ChartDelegate::paint(QPainter *painter,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const
{
    painter->save();
    painter->setClipRect(option.rect, Qt::IntersectClip);

    const auto pv = index.data(ChartModel::SeriesPointerRole);
    if (!pv.isValid()) { painter->restore(); return; }
    const auto *s  = static_cast<const ChartSeries *>(pv.value<const void *>());
    const int   cursor = index.data(ChartModel::CursorSampleRole).toInt();
    const float pps    = index.data(ChartModel::PpsRole).toFloat();

    paintChart(painter, option.rect, *s, cursor, pps);
    painter->restore();
}

void ChartDelegate::paintChart(QPainter *p, const QRect &r,
                               const ChartSeries &s, int cursor, float pps) const
{
    static const QColor bgColor     = QColor(10, 13, 20);
    static const QColor cursorColor = QColor(255, 220, 80, 200);
    static const QColor gridMajor   = QColor(38,  50,  72);
    static const QColor gridLabel   = QColor(60,  80, 110);

    const QRect clipR = p->clipBoundingRect().toAlignedRect().intersected(r);
    if (clipR.isEmpty()) return;

    p->fillRect(clipR, bgColor);

    p->setPen(QPen(QColor(38, 46, 64), 1));
    p->drawLine(clipR.left(), r.bottom(), clipR.right(), r.bottom());

    const int dataSize = sampleCount(s.data);

    const double loD = boundsToDouble(s.minVal);
    const double hiD = boundsToDouble(s.maxVal);

    const int    pad     = 4;
    const int    chartH  = r.height() - pad * 2;
    const double centerY = r.top() + r.height() * 0.5;
    const float  yScale  = s.yScale;
    const int    yOffset = s.yOffset;

    if (loD <= hiD && chartH > 0 && !qFuzzyCompare(loD, hiD)) {
        const double span   = hiD - loD;
        const double denom  = chartH * yScale;

        auto valAtY = [&](double y) -> double {
            const double ratio = 0.5 - (y - centerY - yOffset) / denom;
            return loD + ratio * span;
        };

        const double visHi  = valAtY(r.top());
        const double visLo  = valAtY(r.bottom());

        if (visHi > visLo) {
            const double visRange = visHi - visLo;

            const double rawStep = visRange / 5.0;
            const double mag     = std::pow(10.0, std::floor(std::log10(rawStep)));
            double step = mag;
            if      (rawStep / mag >= 5.0) step = 5.0 * mag;
            else if (rawStep / mag >= 2.0) step = 2.0 * mag;

            if (step > 0) {
                const double firstLine = std::ceil(visLo / step) * step;

                QFont labelFont("Consolas", 7);
                p->setFont(labelFont);
                const QFontMetrics fm(labelFont);

                bool labelDrawn = false;

                for (double v = firstLine; v <= visHi + step * 0.5; v += step) {
                    const double ratio = (span > 0) ? (v - loD) / span : 0.5;
                    const double yLine = centerY + (0.5 - ratio) * denom + yOffset;

                    if (yLine < r.top() - 0.5 || yLine > r.bottom() + 0.5) continue;
                    if (yLine < clipR.top() || yLine > clipR.bottom()) continue;

                    p->setPen(QPen(gridMajor, 1));
                    p->drawLine(QPointF(clipR.left(), yLine), QPointF(clipR.right(), yLine));

                    if (!labelDrawn || std::fmod(v, step * 5) < step * 0.5) {
                        QString label;
                        if (std::abs(v) >= 1e6 || (std::abs(v) < 1e-3 && v != 0))
                            label = QString::number(v, 'e', 2);
                        else
                            label = QString::number(v, 'g', 4);

                        const int lx = clipR.left() + 3;
                        const int ly = static_cast<int>(yLine) - 2;
                        if (ly - fm.ascent() >= r.top() && ly <= r.bottom()) {
                            p->setPen(gridLabel);
                            p->drawText(lx, ly, label);
                        }
                        labelDrawn = true;
                    }
                }
            }
        }
    }

    if (dataSize == 0) return;
    if (loD > hiD) return;


    const float dataLeft  = static_cast<float>(clipR.left()  - r.left());
    const float dataRight = static_cast<float>(clipR.right() - r.left());

    const int firstSample = qMax(0, static_cast<int>(dataLeft / pps));
    const int lastSample  = qMin(dataSize - 1, static_cast<int>((dataRight + pps) / pps));

    if (firstSample > lastSample) return;

    const double denom = static_cast<double>(chartH) * yScale;

    auto sampleToPoint = [&](int i) -> QPointF {
        const double ratio = sampleRatio(s.data, i, s.minVal, s.maxVal);
        return {
            r.left() + i * static_cast<double>(pps),
            centerY + (0.5 - ratio) * denom + yOffset
        };
    };

    p->setClipRect(r, Qt::IntersectClip);

    p->setRenderHint(QPainter::Antialiasing, true);
    QPen linePen(s.color, 1.0f);
    linePen.setCosmetic(true);
    p->setPen(linePen);

    if (pps >= 1.0f) {
        QPolygonF poly;
        poly.reserve(lastSample - firstSample + 1);
        for (int i = firstSample; i <= lastSample; ++i)
            poly.append(sampleToPoint(i));
        p->drawPolyline(poly);
    } else {
        const int visPixels = clipR.width() + 2;
        QVector<QLineF> lines;
        lines.reserve(visPixels);

        int    prevPx    = -1;
        double pixMin    = 1.0, pixMax = 0.0, lastRatio = 0.0;
        bool   pixUsed   = false;
        const int iDataLeft = static_cast<int>(dataLeft);

        auto flush = [&](int px) {
            if (!pixUsed) return;
            const double xV   = clipR.left() + px + 0.5;
            const double yHi  = centerY + (0.5 - pixMax) * denom + yOffset;
            const double yLo  = centerY + (0.5 - pixMin) * denom + yOffset;
            lines.append(QLineF(xV, yHi, xV, yLo));
        };

        for (int i = firstSample; i <= lastSample; ++i) {
            const int    px    = qBound(0, static_cast<int>(i * pps) - iDataLeft, visPixels);
            const double ratio = sampleRatio(s.data, i, s.minVal, s.maxVal);
            if (px != prevPx) {
                flush(prevPx);
                pixMin = pixMax = ratio;
                if (pixUsed) {
                    if (lastRatio < pixMin) pixMin = lastRatio;
                    if (lastRatio > pixMax) pixMax = lastRatio;
                }
                prevPx  = px;
                pixUsed = true;
            } else {
                if (ratio < pixMin) pixMin = ratio;
                if (ratio > pixMax) pixMax = ratio;
            }
            lastRatio = ratio;
        }
        flush(prevPx);
        p->drawLines(lines);
    }

    if (pps >= 4.0f) {
        const float dotR = qMin(pps * 0.25f, 4.0f);
        QColor dotColor  = s.color.lighter(130);
        dotColor.setAlpha(210);
        p->setPen(Qt::NoPen);
        p->setBrush(dotColor);
        for (int i = firstSample; i <= lastSample; ++i)
            p->drawEllipse(sampleToPoint(i), dotR, dotR);
    }

    if (cursor >= 0 && cursor < dataSize) {
        const float xView = r.left() + cursor * static_cast<float>(pps);
        if (xView >= clipR.left() && xView <= clipR.right()) {
            p->setPen(QPen(cursorColor, 1));
            p->drawLine(QPointF(xView, r.top()), QPointF(xView, r.bottom()));

            const float dotR       = (pps >= 4.0f) ? qMin(pps * 0.25f, 4.0f) : 0.f;
            const float cursorDotR = qMax(dotR + 1.f, 3.5f);
            p->setRenderHint(QPainter::Antialiasing, true);
            p->setPen(QPen(QColor(20, 24, 35), 1.5f));
            p->setBrush(cursorColor);
            p->drawEllipse(sampleToPoint(cursor), cursorDotR, cursorDotR);
        }
    }
}
