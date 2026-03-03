#include "ChartHeaderView.h"

#include <QPainter>
#include <QDateTime>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QTimeZone>

ChartHeaderView::ChartHeaderView(ChartModel *model, QWidget *parent)
    : QHeaderView(Qt::Vertical, parent), m_model(model)
{
    // Запрещаем встроенный Qt-ресайз секций — обрабатываем сами
    setSectionResizeMode(QHeaderView::Fixed);
    // Убираем выделение секций Qt — используем собственную логику
    setHighlightSections(false);
    setMouseTracking(true);
    setFixedWidth(model->headerWidth());

    connect(model, &ChartModel::cursorMoved,
            this,  &ChartHeaderView::onCursorMoved);
    connect(model, &ChartModel::layoutChanged,
            this,  &ChartHeaderView::onLayoutChanged);
    connect(model, &ChartModel::rowsInserted,
            this,  [this]() { viewport()->update(); });
}

void ChartHeaderView::onCursorMoved(int) { viewport()->update(); }

void ChartHeaderView::onLayoutChanged()
{
    setFixedWidth(m_model->headerWidth());
    viewport()->update();
}

// ─── Geometry helpers ─────────────────────────────────────────────────────────
// sectionViewportPosition(r) возвращает y строки r в координатах viewport
// (уже учитывает scroll), что заменяет старый rowTop(r) - m_scrollOffset.

int ChartHeaderView::rowAtResizeHandle(int y) const
{
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const ChartSeries *s = m_model->series(r);
        // Фиксированные строки нельзя ресайзить
        if (s && s->minRowHeight > 0 && s->minRowHeight == s->maxRowHeight)
            continue;
        const int rh  = sectionSize(r);
        const int bot = sectionViewportPosition(r) + rh;
        if (std::abs(y - bot) <= kResizeZone)
            return r;
    }
    return -1;
}

int ChartHeaderView::rowAt(int y) const
{
    // logicalIndexAt учитывает scroll внутри QHeaderView
    return logicalIndexAt(y);
}

// ─── Mouse ────────────────────────────────────────────────────────────────────

void ChartHeaderView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {

        // ── Resize ширины заголовка (правый край) ─────────────────────────
        if (e->pos().x() >= viewport()->width() - kHeaderResizeZone) {
            m_headerResizing     = true;
            m_headerResizePressX = e->pos().x();
            m_headerResizeStartW = width();
            setCursor(Qt::SizeHorCursor);
            e->accept();
            return;
        }

        // Resize handle строки имеет приоритет над выделением
        const int resizeRow = rowAtResizeHandle(e->pos().y());
        if (resizeRow >= 0) {
            const ChartSeries *s = m_model->series(resizeRow);
            m_resizing     = true;
            m_resizeRow    = resizeRow;
            m_resizePressY = e->pos().y();
            m_resizeStartH = s ? s->rowHeight : m_model->rowHeight();
            setCursor(Qt::SizeVerCursor);
            e->accept();
            return;
        }

        // Выделение строки
        const int row = rowAt(e->pos().y());
        if (row >= 0) {
            if (e->modifiers() & Qt::ControlModifier) {
                if (m_selectedRows.contains(row))
                    m_selectedRows.remove(row);
                else
                    m_selectedRows.insert(row);
            } else {
                if (m_selectedRows.size() == 1 && m_selectedRows.contains(row))
                    m_selectedRows.clear();
                else {
                    m_selectedRows.clear();
                    m_selectedRows.insert(row);
                }
            }
            m_lastClickedRow = row;
            viewport()->update();
        }
    }
    // Намеренно НЕ вызываем QHeaderView::mousePressEvent:
    // это предотвращает нежелательную встроенную логику выделения секций.
    e->accept();
}

void ChartHeaderView::mouseMoveEvent(QMouseEvent *e)
{
    // ── Drag ширины заголовка ────────────────────────────────────────────
    if (m_headerResizing && (e->buttons() & Qt::LeftButton)) {
        const int delta = e->pos().x() - m_headerResizePressX;
        const int newW  = qBound(kHeaderMinWidth,
                                m_headerResizeStartW + delta,
                                kHeaderMaxWidth);
        m_model->setHeaderWidth(newW);
        e->accept();
        return;
    }

    if (m_resizing && (e->buttons() & Qt::LeftButton)) {
        const int delta = e->pos().y() - m_resizePressY;
        const ChartSeries *s = m_model->series(m_resizeRow);
        if (s) m_model->setSeriesRowHeight(s->name, m_resizeStartH + delta);
        e->accept();
        return;
    }

    if (rowAtResizeHandle(e->pos().y()) >= 0)
        setCursor(Qt::SizeVerCursor);
    else if (e->pos().x() >= viewport()->width() - kHeaderResizeZone)
        setCursor(Qt::SizeHorCursor);
    else
        unsetCursor();

    e->accept();
}

void ChartHeaderView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_headerResizing) {
        m_headerResizing = false;
        unsetCursor();
        e->accept();
        return;
    }
    if (m_resizing) {
        m_resizing  = false;
        m_resizeRow = -1;
        unsetCursor();
        e->accept();
        return;
    }
    e->accept();
}

void ChartHeaderView::leaveEvent(QEvent *e)
{
    if (!m_resizing && !m_headerResizing)
        unsetCursor();
    QHeaderView::leaveEvent(e);
}

void ChartHeaderView::setAutoFitY(bool on) { m_autoFitY = on; }

// ─── Context menu ─────────────────────────────────────────────────────────────

void ChartHeaderView::contextMenuEvent(QContextMenuEvent *e)
{
    const int sourceRow = rowAt(e->pos().y());

    if (sourceRow >= 0 && !m_selectedRows.contains(sourceRow)) {
        m_selectedRows.clear();
        m_selectedRows.insert(sourceRow);
        viewport()->update();
    }

    const ChartSeries *src = (sourceRow >= 0) ? m_model->series(sourceRow) : nullptr;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background:#0f1219; color:#aabbcc; border:1px solid #1e2538; font:9pt 'Consolas'; }"
        "QMenu::item:selected { background:#1a2540; }"
        "QMenu::item:disabled { color:#445566; }"
        "QMenu::separator { height:1px; background:#1e2538; margin:3px 8px; }");

    auto *actAutoFit = menu.addAction(u8"Авто-подстройка Y по видимому фрагменту");
    actAutoFit->setCheckable(true);
    actAutoFit->setChecked(m_autoFitY);
    connect(actAutoFit, &QAction::triggered, this, [this]() {
        emit autoFitYToggleRequested();
    });

    auto *actOneFit = menu.addAction(u8"Подогнать Y под видимый фрагмент");
    connect(actOneFit, &QAction::triggered, this, [this]() {
        emit fitYToVisibleRequested();
    });

    menu.addSeparator();

    if (src) {
        const QString srcName = src->name;
        const bool multi = m_selectedRows.size() > 1;

        if (multi) {
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
            auto *actReset = menu.addAction(
                QString(u8"Сбросить вид '%1'").arg(srcName));
            connect(actReset, &QAction::triggered, this, [this, sourceRow]() {
                emit resetSelectedRequested({ sourceRow });
            });
        }

        menu.addSeparator();
    }

    auto *actResetAll = menu.addAction(u8"Сбросить все");
    connect(actResetAll, &QAction::triggered, this, [this]() {
        m_model->resetAllDisplayParams();
    });

    menu.exec(e->globalPos());
}

// ─── Timestamp formatting ─────────────────────────────────────────────────────

QString ChartHeaderView::formatTimestamp(double epochSec)
{
    if (epochSec <= 0 || !std::isfinite(epochSec)) return QStringLiteral("—");

    const qint64 wholeSec = static_cast<qint64>(epochSec);
    const int    ms       = static_cast<int>((epochSec - wholeSec) * 1000.0 + 0.5);

    const QTimeZone localTz = QTimeZone::systemTimeZone();
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(wholeSec, localTz);
    const int offsetSec = localTz.offsetFromUtc(dt);
    const int offsetH   = std::abs(offsetSec) / 3600;
    const int offsetM   = (std::abs(offsetSec) % 3600) / 60;
    const QString tzStr = QString("%1%2:%3")
                              .arg(offsetSec >= 0 ? "+" : "-")
                              .arg(offsetH, 2, 10, QLatin1Char('0'))
                              .arg(offsetM, 2, 10, QLatin1Char('0'));

    return dt.toString(QStringLiteral("yyyy-MM-dd\nHH:mm:ss"))
           + QStringLiteral(".")
           + QString::number(ms).rightJustified(3, QLatin1Char('0'))
           + QStringLiteral(" ")
           + tzStr;
}

QString ChartHeaderView::formatTimestampLine(double epochSec)
{
    return formatTimestamp(epochSec).replace(QLatin1Char('\n'), QLatin1Char(' '));
}

// ─── Paint ────────────────────────────────────────────────────────────────────
//
// Полностью переопределяем paintEvent — рисуем на viewport(), используем
// sectionViewportPosition(r) вместо rowTop(r)-scrollOffset.
// QHeaderView::paintEvent() не вызываем намеренно.

void ChartHeaderView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), QColor(15, 18, 26));

    if (!model()) return;   // модель ещё не подключена QTableView

    const int rows   = m_model->rowCount();
    const int cursor = m_model->cursorSample();
    const int w      = viewport()->width();
    const int vh     = viewport()->height();

    for (int r = 0; r < rows; ++r) {
        const int y  = sectionViewportPosition(r);
        const int rh = sectionSize(r);
        if (y + rh < 0) continue;
        if (y > vh)     break;

        const ChartSeries *s = m_model->series(r);
        if (!s) continue;

        paintRow(&p, QRect(0, y, w, rh), *s, cursor, m_selectedRows.contains(r));

        // Resize handle hint внизу строки (не для фиксированных)
        if (s->minRowHeight != s->maxRowHeight || s->minRowHeight == 0)
            p.fillRect(QRect(0, y + rh - 3, w, 3), QColor(40, 55, 80));
    }

    // Вертикальный resize handle — правый край заголовка
    p.fillRect(QRect(w - 3, 0, 3, vh), QColor(40, 55, 80));
}

// ─── paintRow ─────────────────────────────────────────────────────────────────
// Без изменений по сравнению с оригиналом.

void ChartHeaderView::paintRow(QPainter *p, const QRect &r,
                               const ChartSeries &s, int cursor, bool selected) const
{
    static const QColor bgColor      = QColor(15, 18, 26);
    static const QColor selColor     = QColor(25, 40, 70);
    static const QColor dividerColor = QColor(38, 46, 64);
    static const QColor nameColor    = QColor(200, 210, 230);
    static const QColor noValColor   = QColor(80, 90, 110);

    p->fillRect(r, selected ? selColor : bgColor);
    p->fillRect(QRect(r.left(), r.top(), 4, r.height()), s.color);

    if (selected) {
        p->setPen(QPen(QColor(60, 110, 200), 1));
        p->drawRect(r.adjusted(0, 0, -1, -1));
    }

    p->setPen(QPen(dividerColor, 1));
    p->drawLine(r.right(), r.top(), r.right(), r.bottom());
    p->drawLine(r.left(),  r.bottom(), r.right(), r.bottom());

    // Фиксированная высота (бит) → компактный вид: имя слева, значение справа
    const bool isFixed = (s.minRowHeight > 0 && s.minRowHeight == s.maxRowHeight);
    if (isFixed) {
        const QRect tr = r.adjusted(10, 2, -6, -2);
        QFont fn("Consolas", 10);
        p->setFont(fn);
        p->setPen(nameColor);
        const QFontMetrics fm(fn);
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                    fm.elidedText(s.name, Qt::ElideRight, tr.width() * 2 / 3));

        const int n = sampleCount(s.data);
        if (cursor >= 0 && cursor < n) {
            const bool bitVal = (sampleAt(s.data, cursor) != 0.0);
            p->setPen(bitVal ? s.color.lighter(140) : QColor(100, 110, 130));
            QFont fv("Consolas", 11, QFont::Bold);
            p->setFont(fv);
            p->drawText(tr, Qt::AlignVCenter | Qt::AlignRight,
                        bitVal ? QStringLiteral("1") : QStringLiteral("0"));
        } else {
            p->setPen(noValColor);
            p->drawText(tr, Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("—"));
        }
        return;
    }

    // ── Обычный вид ───────────────────────────────────────────────────────
    const QRect tr = r.adjusted(10, 6, -6, -6);

    QFont fn("Consolas", 10, QFont::Bold);
    p->setFont(fn);
    p->setPen(nameColor);
    const QString nameStr = QString("%1  [%2]")
                                .arg(s.name, QString::fromLatin1(sampleTypeName(s.data)));
    p->drawText(tr, Qt::AlignTop | Qt::AlignLeft,
                p->fontMetrics().elidedText(nameStr, Qt::ElideRight, tr.width()));

    if (!qFuzzyCompare(s.yScale, 1.0f)) {
        QFont fs("Consolas", 10);
        p->setFont(fs);
        p->setPen(QColor(100, 130, 180));
        p->drawText(tr, Qt::AlignTop | Qt::AlignRight,
                    QString("×%1").arg(s.yScale, 0, 'f', 1));
    }

    const int nameBottom = tr.top() + QFontMetrics(fn).height() + 3;
    const QRect valRect(tr.left(), nameBottom, tr.width(), tr.bottom() - nameBottom);

    const int n = sampleCount(s.data);
    const bool isTimestamp = (s.name == QLatin1String("TimeStamp"));

    if (cursor >= 0 && cursor < n) {
        p->setPen(s.color.lighter(140));
        const std::size_t bufIdx = s.data.index();
        QString valStr;
        if      (bufIdx == 6) valStr = QString::number(sampleAtI64(s.data, cursor));
        else if (bufIdx == 7) valStr = QString::number(sampleAtU64(s.data, cursor));
        else if (bufIdx <= 5) valStr = QString::number(static_cast<long long>(sampleAt(s.data, cursor)));
        else                  valStr = QString::number(sampleAt(s.data, cursor), 'g', 6);

        if (isTimestamp) {
            QFont fNum("Consolas", 10);
            p->setFont(fNum);
            p->drawText(valRect, Qt::AlignTop | Qt::AlignLeft, valStr);

            QFont fDt("Consolas", 10);
            p->setFont(fDt);
            const double epochSec = sampleAt(s.data, cursor);
            const QString dtStr = formatTimestamp(epochSec);
            const QFontMetrics fmDt(fDt);
            const int blockH = fmDt.height() * (dtStr.count(QLatin1Char('\n')) + 1);
            const QRect dtRect(tr.left(), tr.bottom() - blockH, tr.width(), blockH);
            p->drawText(dtRect, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, dtStr);
        } else {
            QFont fv("Consolas", 11);
            p->setFont(fv);
            p->drawText(valRect, Qt::AlignBottom | Qt::AlignLeft, valStr);
        }
    } else {
        p->setPen(noValColor);
        QFont fv("Consolas", 11);
        p->setFont(fv);
        p->drawText(valRect, Qt::AlignBottom | Qt::AlignLeft, QStringLiteral("—"));
    }
}
