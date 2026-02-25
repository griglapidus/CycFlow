#include "ChartHeaderView.h"

#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QMenu>
#include <QAction>

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
void ChartHeaderView::onVerticalScroll(int value) { m_scrollOffset = value; update(); }
void ChartHeaderView::onLayoutChanged() { setFixedWidth(m_model->headerWidth()); update(); }

// ─── Geometry helpers ─────────────────────────────────────────────────────────

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
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const ChartSeries *s = m_model->series(r);
        const int rh  = s ? s->rowHeight : m_model->rowHeight();
        const int bot = rowTop(r) - m_scrollOffset + rh;
        if (std::abs(y - bot) <= kResizeZone) return r;
    }
    return -1;
}

int ChartHeaderView::rowAt(int y) const
{
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const ChartSeries *s = m_model->series(r);
        const int rh  = s ? s->rowHeight : m_model->rowHeight();
        const int top = rowTop(r) - m_scrollOffset;
        if (y >= top && y < top + rh) return r;
    }
    return -1;
}

// ─── Mouse ────────────────────────────────────────────────────────────────────

void ChartHeaderView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        // Resize handle имеет приоритет
        const int resizeRow = rowAtResizeHandle(e->pos().y());
        if (resizeRow >= 0) {
            const ChartSeries *s = m_model->series(resizeRow);
            m_resizing      = true;
            m_resizeRow     = resizeRow;
            m_resizePressY  = e->pos().y();
            m_resizeStartH  = s ? s->rowHeight : m_model->rowHeight();
            setCursor(Qt::SizeVerCursor);
            e->accept(); return;
        }

        // Выделение строки
        const int row = rowAt(e->pos().y());
        if (row >= 0) {
            if (e->modifiers() & Qt::ControlModifier) {
                // Ctrl+клик — toggle строки
                if (m_selectedRows.contains(row))
                    m_selectedRows.remove(row);
                else
                    m_selectedRows.insert(row);
            } else {
                // Простой клик — выделяем только эту строку
                // (если строка уже единственная выделенная — снимаем)
                if (m_selectedRows.size() == 1 && m_selectedRows.contains(row))
                    m_selectedRows.clear();
                else {
                    m_selectedRows.clear();
                    m_selectedRows.insert(row);
                }
            }
            m_lastClickedRow = row;
            update();
        }
    }
    QWidget::mousePressEvent(e);
}

void ChartHeaderView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_resizing && (e->buttons() & Qt::LeftButton)) {
        const int delta = e->pos().y() - m_resizePressY;
        const int newH  = qMax(30, m_resizeStartH + delta);
        const ChartSeries *s = m_model->series(m_resizeRow);
        if (s) m_model->setSeriesRowHeight(s->name, newH);
        e->accept(); return;
    }
    if (rowAtResizeHandle(e->pos().y()) >= 0)
        setCursor(Qt::SizeVerCursor);
    else
        unsetCursor();
    QWidget::mouseMoveEvent(e);
}

void ChartHeaderView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_resizing) { m_resizing = false; m_resizeRow = -1; unsetCursor(); e->accept(); return; }
    QWidget::mouseReleaseEvent(e);
}

void ChartHeaderView::leaveEvent(QEvent *e)
{
    if (!m_resizing) unsetCursor();
    QWidget::leaveEvent(e);
}

// ─── Context menu ─────────────────────────────────────────────────────────────

void ChartHeaderView::contextMenuEvent(QContextMenuEvent *e)
{
    const int sourceRow = rowAt(e->pos().y());
    if (sourceRow < 0) return;

    // Убеждаемся что строка под курсором входит в выделение
    if (!m_selectedRows.contains(sourceRow)) {
        m_selectedRows.clear();
        m_selectedRows.insert(sourceRow);
        update();
    }

    const ChartSeries *src = m_model->series(sourceRow);
    if (!src) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background:#0f1219; color:#aabbcc; border:1px solid #1e2538; font:9pt 'Consolas'; }"
        "QMenu::item:selected { background:#1a2540; }"
        "QMenu::separator { height:1px; background:#1e2538; margin:3px 8px; }"
        );

    const bool multi = m_selectedRows.size() > 1;
    const QString srcName = src->name;

    if (multi) {
        // ── Операции над несколькими выделенными ────────────────────────────
        auto *actSync = menu.addAction(
            QString(u8"Синхронизировать масштаб по '%1'").arg(srcName));
        connect(actSync, &QAction::triggered, this, [this, sourceRow]() {
            emit syncScaleRequested(sourceRow, m_selectedRows);
        });

        auto *actOverlay = menu.addAction(
            QString(u8"Наложить на '%1'").arg(srcName));
        connect(actOverlay, &QAction::triggered, this, [this, sourceRow]() {
            emit overlayRequested(sourceRow, m_selectedRows);
        });

        menu.addSeparator();

        auto *actReset = menu.addAction(u8"Сбросить параметры выделенных");
        connect(actReset, &QAction::triggered, this, [this]() {
            emit resetSelectedRequested(m_selectedRows);
        });
    } else {
        // ── Одиночное выделение ─────────────────────────────────────────────
        auto *actReset = menu.addAction(
            QString(u8"Сбросить вид '%1'").arg(srcName));
        connect(actReset, &QAction::triggered, this, [this, sourceRow]() {
            emit resetSelectedRequested({ sourceRow });
        });
    }

    menu.addSeparator();
    auto *actResetAll = menu.addAction(u8"Сбросить все");
    connect(actResetAll, &QAction::triggered, this, [this]() {
        m_model->resetAllDisplayParams();
    });

    menu.exec(e->globalPos());
}

// ─── Paint ────────────────────────────────────────────────────────────────────

void ChartHeaderView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(15, 18, 26));

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
        paintRow(&p, QRect(0, y, w, rh), *s, cursor, m_selectedRows.contains(r));
        // Resize handle hint
        p.fillRect(QRect(0, y + rh - 3, w, 3), QColor(40, 55, 80));
    }
}

void ChartHeaderView::paintRow(QPainter *p, const QRect &r,
                               const ChartSeries &s, int cursor, bool selected) const
{
    static const QColor bgColor      = QColor(15, 18, 26);
    static const QColor selColor     = QColor(25, 40, 70);   // фон выделенной строки
    static const QColor dividerColor = QColor(38, 46, 64);
    static const QColor nameColor    = QColor(200, 210, 230);
    static const QColor noValColor   = QColor(80, 90, 110);

    // Фон — с подсветкой при выделении
    p->fillRect(r, selected ? selColor : bgColor);

    // Цветная полоска слева
    p->fillRect(QRect(r.left(), r.top(), 4, r.height()), s.color);

    // Рамка выделения
    if (selected) {
        p->setPen(QPen(QColor(60, 110, 200), 1));
        p->drawRect(r.adjusted(0, 0, -1, -1));
    }

    p->setPen(QPen(dividerColor, 1));
    p->drawLine(r.right(), r.top(),   r.right(),  r.bottom());
    p->drawLine(r.left(),  r.bottom(), r.right(), r.bottom());

    const QRect tr = r.adjusted(10, 6, -6, -6);

    QFont fn("Consolas", 9, QFont::Bold);
    p->setFont(fn);
    p->setPen(nameColor);
    const QString nameStr = QString("%1  [%2]")
                                .arg(s.name, QString::fromLatin1(sampleTypeName(s.data)));
    p->drawText(tr, Qt::AlignTop | Qt::AlignLeft,
                p->fontMetrics().elidedText(nameStr, Qt::ElideRight, tr.width()));

    if (!qFuzzyCompare(s.yScale, 1.0f)) {
        QFont fs("Consolas", 8);
        p->setFont(fs);
        p->setPen(QColor(100, 130, 180));
        p->drawText(tr, Qt::AlignTop | Qt::AlignRight,
                    QString("×%1").arg(s.yScale, 0, 'f', 1));
    }

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
