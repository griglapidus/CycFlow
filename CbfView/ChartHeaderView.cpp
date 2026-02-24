#include "ChartHeaderView.h"

#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>

ChartHeaderView::ChartHeaderView(ChartModel *model, QWidget *parent)
    : QWidget(parent), m_model(model)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFixedWidth(model->headerWidth());
    setMouseTracking(true);

    connect(model, &ChartModel::cursorMoved,
            this,  &ChartHeaderView::onCursorMoved);
    connect(model, &ChartModel::layoutChanged,
            this,  &ChartHeaderView::onLayoutChanged);
    connect(model, &ChartModel::rowsInserted,
            this,  [this]() { update(); });
}

void ChartHeaderView::syncVerticalScroll(QAbstractScrollArea *chartView)
{
    connect(chartView->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ChartHeaderView::onVerticalScroll);
}

void ChartHeaderView::onCursorMoved(int) { update(); }

void ChartHeaderView::onVerticalScroll(int value)
{
    m_scrollOffset = value;
    update();
}

void ChartHeaderView::onLayoutChanged()
{
    setFixedWidth(m_model->headerWidth());
    update();
}

int ChartHeaderView::rowTop(int row) const
{
    int y = 0;
    for (int i = 0; i < row; ++i) {
        const ChartSeries *s = m_model->series(i);
        y += s ? s->rowHeight : m_model->rowHeight();
    }
    return y;
}

int ChartHeaderView::rowAtResizeHandle(int y) const
{
    const int rows = m_model->rowCount();
    for (int r = 0; r < rows; ++r) {
        const ChartSeries *s = m_model->series(r);
        const int rh  = s ? s->rowHeight : m_model->rowHeight();
        const int bot = rowTop(r) - m_scrollOffset + rh;
        if (std::abs(y - bot) <= kResizeZone)
            return r;
    }
    return -1;
}

void ChartHeaderView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const int row = rowAtResizeHandle(e->pos().y());
        if (row >= 0) {
            const ChartSeries *s = m_model->series(row);
            m_resizing      = true;
            m_resizeRow     = row;
            m_resizePressY  = e->pos().y();
            m_resizeStartH  = s ? s->rowHeight : m_model->rowHeight();
            setCursor(Qt::SizeVerCursor);
            e->accept();
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

void ChartHeaderView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_resizing && (e->buttons() & Qt::LeftButton)) {
        const int delta  = e->pos().y() - m_resizePressY;
        const int newH   = qMax(30, m_resizeStartH + delta);
        const ChartSeries *s = m_model->series(m_resizeRow);
        if (s)
            m_model->setSeriesRowHeight(s->name, newH);
        e->accept();
        return;
    }

    if (rowAtResizeHandle(e->pos().y()) >= 0)
        setCursor(Qt::SizeVerCursor);
    else
        unsetCursor();

    QWidget::mouseMoveEvent(e);
}

void ChartHeaderView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_resizing) {
        m_resizing  = false;
        m_resizeRow = -1;
        unsetCursor();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void ChartHeaderView::leaveEvent(QEvent *e)
{
    if (!m_resizing) unsetCursor();
    QWidget::leaveEvent(e);
}

void ChartHeaderView::paintEvent(QPaintEvent *)
{
    static const QColor bgColor = QColor(15, 18, 26);
    QPainter p(this);
    p.fillRect(rect(), bgColor);

    const int rows   = m_model->rowCount();
    const int cursor = m_model->cursorSample();
    const int w      = width();

    for (int r = 0; r < rows; ++r) {
        const int y = rowTop(r) - m_scrollOffset;
        const ChartSeries *s = m_model->series(r);
        const int rh = s ? s->rowHeight : m_model->rowHeight();

        if (y + rh < 0) continue;
        if (y > height()) break;

        if (!s) continue;
        paintRow(&p, QRect(0, y, w, rh), *s, cursor);

        p.fillRect(QRect(0, y + rh - 3, w, 3), QColor(40, 55, 80));
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
    p->drawLine(r.right(), r.top(),   r.right(),  r.bottom());
    p->drawLine(r.left(),  r.bottom(), r.right(), r.bottom());

    const QRect tr = r.adjusted(10, 6, -6, -6);

    // Имя + тип
    QFont fn("Consolas", 9, QFont::Bold);
    p->setFont(fn);
    p->setPen(nameColor);
    const QString nameStr = QString("%1  [%2]")
                                .arg(s.name, QString::fromLatin1(sampleTypeName(s.data)));
    p->drawText(tr, Qt::AlignTop | Qt::AlignLeft,
                p->fontMetrics().elidedText(nameStr, Qt::ElideRight, tr.width()));

    // Индикатор yScale (если не 1.0)
    if (!qFuzzyCompare(s.yScale, 1.0f)) {
        QFont fs("Consolas", 8);
        p->setFont(fs);
        p->setPen(QColor(100, 130, 180));
        p->drawText(tr, Qt::AlignTop | Qt::AlignRight,
                    QString("×%1").arg(s.yScale, 0, 'f', 1));
    }

    // Значение под курсором
    QFont fv("Consolas", 11);
    p->setFont(fv);

    const int n = sampleCount(s.data);
    if (cursor >= 0 && cursor < n) {
        p->setPen(s.color.lighter(140));

        const std::size_t bufIdx = s.data.index();
        QString valStr;
        if      (bufIdx == 6) valStr = QString::number(sampleAtI64(s.data, cursor));
        else if (bufIdx == 7) valStr = QString::number(sampleAtU64(s.data, cursor));
        else if (bufIdx <= 5) valStr = QString::number(static_cast<long long>(sampleAt(s.data, cursor)));
        else                  valStr = QString::number(sampleAt(s.data, cursor), 'g', 6);

        p->drawText(tr, Qt::AlignBottom | Qt::AlignLeft, valStr);
    } else {
        p->setPen(noValColor);
        p->drawText(tr, Qt::AlignBottom | Qt::AlignLeft, QStringLiteral("—"));
    }
}
