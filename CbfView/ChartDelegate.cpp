#include "ChartDelegate.h"

#include <QPainter>

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
    static const QColor gridColor   = QColor(28, 34, 50);
    static const QColor cursorColor = QColor(255, 220, 80, 200);

    const QRect clipR = p->clipBoundingRect().toAlignedRect().intersected(r);
    if (clipR.isEmpty()) return;

    p->fillRect(clipR, bgColor);

    p->setPen(QPen(gridColor, 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const int y = r.top() + r.height() * i / 4;
        p->drawLine(clipR.left(), y, clipR.right(), y);
    }

    p->setPen(QPen(QColor(38, 46, 64), 1));
    p->drawLine(clipR.left(), r.bottom(), clipR.right(), r.bottom());

    const int dataSize = sampleCount(s.data);
    if (dataSize == 0) return;

    // Проверяем валидность bounds
    const double loD = boundsToDouble(s.minVal);
    const double hiD = boundsToDouble(s.maxVal);
    if (loD > hiD) return;   // данные ещё не пришли

    const int pad    = 4;
    const int chartH = r.height() - pad * 2;

    const float dataLeft  = static_cast<float>(clipR.left()  - r.left());
    const float dataRight = static_cast<float>(clipR.right() - r.left());

    const int firstSample = qMax(0, static_cast<int>(dataLeft / pps));
    const int lastSample  = qMin(dataSize - 1, static_cast<int>((dataRight + pps) / pps));

    if (firstSample > lastSample) return;

    // sampleRatio использует нативную арифметику для int64/uint64:
    // (v - lo) вычисляется в int64_t/uint64_t до конвертации в double.
    auto sampleToPoint = [&](int i) -> QPointF {
        const double ratio = sampleRatio(s.data, i, s.minVal, s.maxVal);
        return {
            r.left() + i * static_cast<double>(pps),
            r.bottom() - pad - ratio * chartH
        };
    };

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
        // min/max прореживание — также через sampleRatio
        const int visPixels = clipR.width() + 2;
        QVector<QLineF> lines;
        lines.reserve(visPixels);

        int    prevPx  = -1;
        double pixMin  = 1.0, pixMax = 0.0;
        bool   pixUsed = false;
        const int iDataLeft = static_cast<int>(dataLeft);

        auto flush = [&](int px) {
            if (!pixUsed) return;
            const float xV   = clipR.left() + px + 0.5f;
            const float yMax = r.bottom() - pad - pixMax * chartH;
            const float yMin = r.bottom() - pad - pixMin * chartH;
            lines.append(QLineF(xV, yMax, xV, yMin));
        };

        for (int i = firstSample; i <= lastSample; ++i) {
            const int    px    = qBound(0, static_cast<int>(i * pps) - iDataLeft, visPixels);
            const double ratio = sampleRatio(s.data, i, s.minVal, s.maxVal);
            if (px != prevPx) {
                flush(prevPx);
                prevPx = px; pixMin = pixMax = ratio; pixUsed = true;
            } else {
                if (ratio < pixMin) pixMin = ratio;
                if (ratio > pixMax) pixMax = ratio;
            }
        }
        flush(prevPx);
        p->drawLines(lines);
    }

    // Точки семплов (pps >= 4)
    if (pps >= 4.0f) {
        const float dotR = qMin(pps * 0.25f, 4.0f);
        QColor dotColor  = s.color.lighter(130);
        dotColor.setAlpha(210);
        p->setPen(Qt::NoPen);
        p->setBrush(dotColor);
        for (int i = firstSample; i <= lastSample; ++i)
            p->drawEllipse(sampleToPoint(i), dotR, dotR);
    }

    // Курсор
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
