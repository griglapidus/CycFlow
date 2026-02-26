#include "ChartDelegate.h"

#include <QPainter>
#include <QFontMetrics>
#include <cmath>

static const QColor kBg          = QColor(10,  13,  20);
static const QColor kDivider     = QColor(38,  46,  64);
static const QColor kGridMajor   = QColor(38,  50,  72);
static const QColor kGridLabel   = QColor(70,  90, 120);
static const QColor kCursorColor = QColor(255, 220, 80, 200);

ChartDelegate::ChartDelegate(ChartModel *model, QObject *parent)
    : QStyledItemDelegate(parent), m_model(model) {}

QSize ChartDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {m_model->chartPixelWidth(), m_model->rowHeight()};
}

// ─── paint(): только фон + сетка ─────────────────────────────────────────────

void ChartDelegate::paint(QPainter *p,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const
{
    const auto pv = index.data(ChartModel::SeriesPointerRole);
    if (!pv.isValid()) return;
    const auto *s = static_cast<const ChartSeries *>(pv.value<const void *>());

    p->save();
    // Расширяем клип по X до полной ширины устройства — фон и сетка рисуются
    // за пределами данных. По Y оставляем option.rect чтобы не лезть в соседние строки.
    {
        const QRect fullR(0, option.rect.top(),
                          p->device()->width(), option.rect.height());
        p->setClipRect(fullR, Qt::ReplaceClip);
    }
    paintBackground(p, option.rect, *s);
    p->restore();
}

// ─── Первый проход: фон, сетка, подписи ──────────────────────────────────────

void ChartDelegate::paintBackground(QPainter *p, const QRect &r,
                                    const ChartSeries &s) const
{
    // clipR — реально перерисовываемая часть ячейки по X (dirty region)
    const QRect clipR = p->clipBoundingRect().toAlignedRect().intersected(r);
    if (clipR.isEmpty()) return;

    // Фон и линии рисуем на всю ширину viewport, а не только до конца данных.
    // Это важно когда chartPixelWidth() < ширины окна.
    const int fullRight = p->device()->width() - 1;
    const QRect fullClip(clipR.left(), r.top(), fullRight - clipR.left() + 1, r.height());
    p->fillRect(fullClip, kBg);
    p->setPen(QPen(kDivider, 1));
    p->drawLine(clipR.left(), r.bottom(), fullRight, r.bottom());

    const double loD = boundsToDouble(s.minVal);
    const double hiD = boundsToDouble(s.maxVal);
    if (loD >= hiD || !std::isfinite(loD) || !std::isfinite(hiD)) return;

    const int    chartH  = r.height() - 8;
    if (chartH <= 0) return;
    const double centerY = r.top() + r.height() * 0.5;
    const double denom   = chartH * static_cast<double>(s.yScale);
    const double span    = hiD - loD;

    // Видимый диапазон значений по краям ячейки
    auto valAtY = [&](double y) {
        return loD + (0.5 - (y - centerY - s.yOffset) / denom) * span;
    };
    const double visHi = valAtY(r.top());
    const double visLo = valAtY(r.bottom());
    if (visHi <= visLo) return;

    const double rawStep = (visHi - visLo) / 5.0;
    if (rawStep <= 0 || !std::isfinite(rawStep)) return;
    const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
    double step = mag;
    if      (rawStep / mag >= 5.0) step = 5.0 * mag;
    else if (rawStep / mag >= 2.0) step = 2.0 * mag;

    QFont lf("Consolas", 9);
    p->setFont(lf);
    const QFontMetrics fm(lf);
    const int labelH = fm.height();
    int       prevLy = INT_MIN;

    for (double v = std::ceil(visLo / step) * step; v <= visHi + step * 0.5; v += step) {
        const double yLine = centerY + (0.5 - (v - loD) / span) * denom + s.yOffset;
        if (yLine < r.top() - 0.5 || yLine > r.bottom() + 0.5) continue;

        p->setPen(QPen(kGridMajor, 1));
        p->drawLine(QPointF(clipR.left(), yLine), QPointF(fullRight, yLine));

        // Подпись у левого края ВИДИМОЙ области — видна при любом scroll
        const int yI = static_cast<int>(yLine);
        int ly = yI - 3;
        if (ly - fm.ascent() < r.top() + 2) ly = yI + fm.ascent() + 2;
        if (ly - fm.ascent() > r.bottom()) continue;
        if (prevLy != INT_MIN && std::abs(ly - prevLy) < labelH + 2) continue;
        prevLy = ly;

        QString label;
        if (std::abs(v) >= 1e6 || (std::abs(v) < 1e-3 && v != 0.0))
            label = QString::number(v, 'e', 2);
        else
            label = QString::number(v, 'g', 4);

        p->setPen(kGridLabel);
        p->drawText(3, ly, label);
        // Дублируем у правого края viewport.
        // p->device()->width() — ширина viewport в тех же координатах что x=3.
        const int labelW = fm.horizontalAdvance(label);
        const int rLabelX = p->device()->width() - labelW - 3;
        if (rLabelX > labelW + 10)   // не рисуем если перекрывает левый лейбл
            p->drawText(rLabelX, ly, label);
    }
}

// ─── Второй проход: линии данных, точки, курсор ───────────────────────────────
//
// Вызывается из ChartView::paintEvent (новый QPainter на viewport) после того
// как все ячейки нарисовали фон. Painter уже переведён в content-координаты.
// clipXLeft/clipXRight — видимый X в content-координатах.
// Y не ограничен — линии могут выходить за пределы своей строки.

void ChartDelegate::paintData(QPainter *p, const QRect &cell,
                              const ChartSeries &s, int cursor, float pps,
                              int clipXLeft, int clipXRight) const
{
    const int dataSize = sampleCount(s.data);
    if (dataSize == 0) return;

    const double loD = boundsToDouble(s.minVal);
    const double hiD = boundsToDouble(s.maxVal);
    if (loD > hiD) return;

    const int    chartH  = cell.height() - 8;
    if (chartH <= 0) return;
    const double centerY = cell.top() + cell.height() * 0.5;
    const double denom   = chartH * static_cast<double>(s.yScale);

    // Пересечение с ячейкой по X (не рисуем левее начала данных)
    const int cLeft  = qMax(clipXLeft,  cell.left());
    const int cRight = qMin(clipXRight, cell.right());
    if (cLeft > cRight) return;

    // Индексы семплов по видимому X
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
    QPen lp(s.color, 1.0f); lp.setCosmetic(true);
    p->setPen(lp);

    if (pps >= 1.0f) {
        QPolygonF poly;
        poly.reserve(last - first + 1);
        for (int i = first; i <= last; ++i) poly.append(toPoint(i));
        p->drawPolyline(poly);
    } else {
        struct PixelData {
            int    px        = -1;
            double enterRatio = 0.0;
            double exitRatio  = 0.0;
            double pMin       = 1.0;
            double pMax       = 0.0;
            bool   used       = false;
        };

        const int visPixels = cRight - cLeft + 2;
        const int iDataLeft = static_cast<int>(dataLeft);

        PixelData prev, cur;

        QPolygonF poly;
        poly.reserve(visPixels * 2);

        auto emitPixel = [&](const PixelData &pd) {
            if (!pd.used) return;
            const double x  = cLeft + pd.px + 0.5;
            const double yE = centerY + (0.5 - pd.enterRatio) * denom + s.yOffset;
            const double yX = centerY + (0.5 - pd.exitRatio)  * denom + s.yOffset;
            const double yN = centerY + (0.5 - pd.pMin)       * denom + s.yOffset;
            const double yP = centerY + (0.5 - pd.pMax)       * denom + s.yOffset;
            poly.append(QPointF(x, yP));
            poly.append(QPointF(x, yN));
            poly.append(QPointF(x, yX));
            Q_UNUSED(yE)
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
        // Последний пиксель
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

    if (pps >= 4.0f) {
        const float dr = qMin(pps * 0.25f, 4.0f);
        QColor dc = s.color.lighter(130); dc.setAlpha(210);
        p->setPen(Qt::NoPen); p->setBrush(dc);
        for (int i = first; i <= last; ++i) p->drawEllipse(toPoint(i), dr, dr);
    }

    if (cursor >= 0 && cursor < dataSize) {
        const double cx = cell.left() + cursor * static_cast<double>(pps);
        if (cx >= cLeft && cx <= cRight) {
            p->setPen(QPen(kCursorColor, 1));
            p->drawLine(QPointF(cx, cell.top()), QPointF(cx, cell.bottom()));
            const float dr  = (pps >= 4.0f) ? qMin(pps * 0.25f, 4.0f) : 0.f;
            const float cdr = qMax(dr + 1.f, 3.5f);
            p->setPen(QPen(QColor(20, 24, 35), 1.5f));
            p->setBrush(kCursorColor);
            p->drawEllipse(toPoint(cursor), cdr, cdr);
        }
    }
}
