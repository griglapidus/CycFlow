#include "ChartHeaderView.h"

#include <QPainter>
#include <QScrollBar>

ChartHeaderView::ChartHeaderView(ChartModel *model, QWidget *parent)
    : QWidget(parent), m_model(model)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFixedWidth(model->headerWidth());

    connect(model, &ChartModel::cursorMoved,
            this,  &ChartHeaderView::onCursorMoved);
    connect(model, &ChartModel::layoutChanged,
            this,  [this]() { setFixedWidth(m_model->headerWidth()); update(); });
    connect(model, &ChartModel::rowsInserted,
            this,  [this]() { update(); });
}

void ChartHeaderView::syncVerticalScroll(QAbstractScrollArea *chartView)
{
    connect(chartView->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ChartHeaderView::onVerticalScroll);
}

void ChartHeaderView::onCursorMoved(int) { update(); }
void ChartHeaderView::onVerticalScroll(int value) { m_scrollOffset = value; update(); }

void ChartHeaderView::paintEvent(QPaintEvent *)
{
    static const QColor bgColor = QColor(15, 18, 26);
    QPainter p(this);
    p.fillRect(rect(), bgColor);

    const int rowH   = m_model->rowHeight();
    const int rows   = m_model->rowCount();
    const int cursor = m_model->cursorSample();
    const int w      = width();

    for (int r = 0; r < rows; ++r) {
        const int y = r * rowH - m_scrollOffset;
        if (y + rowH < 0) continue;
        if (y > height())  break;

        const ChartSeries *s = m_model->series(r);
        if (!s) continue;
        paintRow(&p, QRect(0, y, w, rowH), *s, cursor);
    }
}

void ChartHeaderView::paintRow(QPainter *p, const QRect &r,
                               const ChartSeries &s, int cursor) const
{
    static const QColor bgColor      = QColor(15, 18, 26);
    static const QColor dividerColor = QColor(38, 46, 64);
    static const QColor nameColor    = QColor(200, 210, 230);
    static const QColor noValColor   = QColor(80, 90, 110);

    p->fillRect(r, bgColor);
    p->fillRect(QRect(r.left(), r.top(), 4, r.height()), s.color);

    p->setPen(QPen(dividerColor, 1));
    p->drawLine(r.right(), r.top(),    r.right(),  r.bottom());
    p->drawLine(r.left(),  r.bottom(), r.right(), r.bottom());

    const QRect tr = r.adjusted(10, 6, -6, -6);

    QFont fn("Consolas", 9, QFont::Bold);
    p->setFont(fn);
    p->setPen(nameColor);
    const QString nameStr = QString("%1  [%2]")
                                .arg(s.name, QString::fromLatin1(sampleTypeName(s.data)));
    p->drawText(tr, Qt::AlignTop | Qt::AlignLeft,
                p->fontMetrics().elidedText(nameStr, Qt::ElideRight, tr.width()));

    QFont fv("Consolas", 11);
    p->setFont(fv);

    const int n = sampleCount(s.data);
    if (cursor >= 0 && cursor < n) {
        p->setPen(s.color.lighter(140));

        // Форматируем значение с максимальной точностью для каждого типа
        const std::size_t bufIdx = s.data.index();
        QString valStr;

        if (bufIdx == 6) {
            // int64: используем sampleAtI64 — точный, без потери разрядов
            valStr = QString::number(sampleAtI64(s.data, cursor));
        } else if (bufIdx == 7) {
            // uint64: используем sampleAtU64
            valStr = QString::number(sampleAtU64(s.data, cursor));
        } else if (bufIdx <= 5) {
            // int8..uint32: целые числа, double точен
            valStr = QString::number(static_cast<long long>(sampleAt(s.data, cursor)));
        } else {
            // float32, float64
            valStr = QString::number(sampleAt(s.data, cursor), 'g', 6);
        }

        p->drawText(tr, Qt::AlignBottom | Qt::AlignLeft, valStr);
    } else {
        p->setPen(noValColor);
        p->drawText(tr, Qt::AlignBottom | Qt::AlignLeft, QStringLiteral("—"));
    }
}
