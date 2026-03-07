// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "ChartHeaderView.h"

#include <QPainter>
#include <QDateTime>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QTimeZone>
#include <cmath>

ChartHeaderView::ChartHeaderView(ChartModel *model, QWidget *parent)
    : QHeaderView(Qt::Vertical, parent), m_model(model)
{
    setSectionResizeMode(QHeaderView::Fixed);
    setHighlightSections(false);
    setMouseTracking(true);
    setFixedWidth(model->headerWidth());

    connect(model, &ChartModel::cursorMoved,
            this,  &ChartHeaderView::onCursorMoved);
    connect(model, &ChartModel::layoutChanged,
            this,  &ChartHeaderView::onLayoutChanged);
    connect(model, &ChartModel::rowsInserted,
            this,  [this]() { viewport()->update(); });
    connect(model, &ChartModel::seriesDisplayChanged,
            this,  [this](const QString &, int) { viewport()->update(); });
}

void ChartHeaderView::onCursorMoved(int) { viewport()->update(); }

void ChartHeaderView::onLayoutChanged()
{
    setFixedWidth(m_model->headerWidth());
    viewport()->update();
}

// =============================================================================
//  Geometry helpers
// =============================================================================

int ChartHeaderView::rowAtResizeHandle(int y) const
{
    for (int r = 0; r < m_model->rowCount(); ++r) {
        const ChartSeries *s = m_model->series(r);
        // Fixed-height rows (digital signals) are not resizable.
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
    return logicalIndexAt(y);
}

// =============================================================================
//  Mouse events
// =============================================================================

void ChartHeaderView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {

        // Right-edge grip — start a header-width resize.
        if (e->pos().x() >= viewport()->width() - kHeaderResizeZone) {
            m_headerResizing     = true;
            m_headerResizePressX = e->pos().x();
            m_headerResizeStartW = width();
            setCursor(Qt::SizeHorCursor);
            e->accept();
            return;
        }

        // Row bottom-edge grip — start a row-height resize.
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

        // Normal click — update row selection.
        const int row = rowAt(e->pos().y());
        if (row >= 0) {
            if (e->modifiers() & Qt::ControlModifier) {
                // Ctrl+click toggles the clicked row.
                if (m_selectedRows.contains(row))
                    m_selectedRows.remove(row);
                else
                    m_selectedRows.insert(row);
            } else {
                // Plain click selects the row, or clears if already the sole selection.
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
    e->accept();
}

void ChartHeaderView::mouseMoveEvent(QMouseEvent *e)
{
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
        const int delta      = e->pos().y() - m_resizePressY;
        const ChartSeries *s = m_model->series(m_resizeRow);
        if (s) m_model->setSeriesRowHeight(s->name, m_resizeStartH + delta);
        e->accept();
        return;
    }

    // Update the cursor shape to indicate what drag action is available.
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

// =============================================================================
//  Context menu
//  Menu style is not set manually — the current theme's native style is used.
// =============================================================================

void ChartHeaderView::contextMenuEvent(QContextMenuEvent *e)
{
    const int sourceRow = rowAt(e->pos().y());

    // If the right-clicked row is not already selected, select it exclusively.
    if (sourceRow >= 0 && !m_selectedRows.contains(sourceRow)) {
        m_selectedRows.clear();
        m_selectedRows.insert(sourceRow);
        viewport()->update();
    }

    const ChartSeries *src = (sourceRow >= 0) ? m_model->series(sourceRow) : nullptr;

    QMenu menu(this);

    auto *actAutoFit = menu.addAction("Auto-fit Y to visible range");
    actAutoFit->setCheckable(true);
    actAutoFit->setChecked(m_autoFitY);
    connect(actAutoFit, &QAction::triggered, this, [this]() {
        emit autoFitYToggleRequested();
    });

    auto *actOneFit = menu.addAction("Fit Y to visible range");
    connect(actOneFit, &QAction::triggered, this, [this]() {
        emit fitYToVisibleRequested();
    });

    menu.addSeparator();

    if (src) {
        const QString srcName = src->name;
        const bool    multi   = m_selectedRows.size() > 1;

        if (multi) {
            auto *actSync = menu.addAction(
                QString("Sync scale to '%1'").arg(srcName));
            connect(actSync, &QAction::triggered, this, [this, sourceRow]() {
                emit syncScaleRequested(sourceRow, m_selectedRows);
            });

            auto *actOverlay = menu.addAction(
                QString("Overlay onto '%1'").arg(srcName));
            connect(actOverlay, &QAction::triggered, this, [this, sourceRow]() {
                emit overlayRequested(sourceRow, m_selectedRows);
            });

            menu.addSeparator();

            auto *actReset = menu.addAction("Reset selected rows");
            connect(actReset, &QAction::triggered, this, [this]() {
                emit resetSelectedRequested(m_selectedRows);
            });
        } else {
            auto *actReset = menu.addAction(
                QString("Reset '%1'").arg(srcName));
            connect(actReset, &QAction::triggered, this, [this, sourceRow]() {
                emit resetSelectedRequested({ sourceRow });
            });
        }

        menu.addSeparator();
    }

    auto *actResetAll = menu.addAction("Reset all");
    connect(actResetAll, &QAction::triggered, this, [this]() {
        m_model->resetAllDisplayParams();
    });

    menu.exec(e->globalPos());
}

// =============================================================================
//  Timestamp formatting
// =============================================================================

QString ChartHeaderView::formatTimestamp(double epochSec)
{
    if (epochSec <= 0 || !std::isfinite(epochSec)) return QStringLiteral("—");

    const qint64 wholeSec = static_cast<qint64>(epochSec);
    const int    ms       = static_cast<int>((epochSec - wholeSec) * 1000.0 + 0.5);

    const QTimeZone localTz  = QTimeZone::systemTimeZone();
    const QDateTime dt       = QDateTime::fromSecsSinceEpoch(wholeSec, localTz);
    const int       offsetSec = localTz.offsetFromUtc(dt);
    const int       offsetH   = std::abs(offsetSec) / 3600;
    const int       offsetM   = (std::abs(offsetSec) % 3600) / 60;
    const QString   tzStr     = QString("%1%2:%3")
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

// =============================================================================
//  Paint
// =============================================================================

void ChartHeaderView::paintEvent(QPaintEvent *)
{
    const QPalette &pal = palette();

    QPainter p(viewport());
    // The header panel uses Window (sidebar colour), distinct from Base (plot area).
    p.fillRect(viewport()->rect(), pal.color(QPalette::Window));

    if (!model()) return;

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

        paintRow(&p, pal, QRect(0, y, w, rh), *s, cursor, m_selectedRows.contains(r));

        // Row-height resize grip at the section bottom (not shown for fixed rows).
        if (s->minRowHeight != s->maxRowHeight || s->minRowHeight == 0) {
            QColor grip = pal.color(QPalette::Mid);
            grip.setAlpha(180);
            p.fillRect(QRect(0, y + rh - 3, w, 3), grip);
        }
    }

    // Header-width resize grip at the right edge of the panel.
    {
        QColor grip = pal.color(QPalette::Mid);
        grip.setAlpha(200);
        p.fillRect(QRect(w - 3, 0, 3, vh), grip);
    }
}

// =============================================================================
//  paintRow
// =============================================================================

void ChartHeaderView::paintRow(QPainter *p, const QPalette &pal, const QRect &r,
                               const ChartSeries &s, int cursor, bool selected) const
{
    const QColor bgColor   = pal.color(QPalette::Window);
    const QColor divColor  = pal.color(QPalette::Mid);
    const QColor nameColor = pal.color(QPalette::WindowText);
    const QColor dimColor  = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor hlColor   = pal.color(QPalette::Highlight);

    QColor selBg = hlColor;
    selBg.setAlpha(70);

    p->fillRect(r, selected ? selBg : bgColor);
    // Colour bar — identifies the series visually.
    p->fillRect(QRect(r.left(), r.top(), 4, r.height()), s.color);

    if (selected) {
        p->setPen(QPen(hlColor, 1));
        p->drawRect(r.adjusted(0, 0, -1, -1));
    }

    p->setPen(QPen(divColor, 1));
    p->drawLine(r.right(), r.top(),    r.right(), r.bottom());
    p->drawLine(r.left(),  r.bottom(), r.right(), r.bottom());

    // --- Compact layout: digital (bit) rows ----------------------------------
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
            p->setPen(bitVal ? s.color.lighter(140) : dimColor);
            QFont fv("Consolas", 11, QFont::Bold);
            p->setFont(fv);
            p->drawText(tr, Qt::AlignVCenter | Qt::AlignRight,
                        bitVal ? QStringLiteral("1") : QStringLiteral("0"));
        } else {
            p->setPen(dimColor);
            p->drawText(tr, Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("—"));
        }
        return;
    }

    // --- Standard layout: analogue rows --------------------------------------
    const QRect tr = r.adjusted(10, 6, -6, -6);

    QFont fn("Consolas", 10, QFont::Bold);
    p->setFont(fn);
    p->setPen(nameColor);
    const QString nameStr = QString("%1  [%2]")
                                .arg(s.name, QString::fromLatin1(sampleTypeName(s.data)));
    p->drawText(tr, Qt::AlignTop | Qt::AlignLeft,
                p->fontMetrics().elidedText(nameStr, Qt::ElideRight, tr.width()));

    // View-range annotation -- shown when the user has explicitly set Y bounds
    // (zoom/pan/fit). Hidden in auto-mode (NaN) to avoid visual clutter.
    // Uses Link colour (accent, readable in both themes).
    if (!qIsNaN(s.viewLo) && !qIsNaN(s.viewHi) && s.viewHi > s.viewLo) {
        auto fmtBound = [](double v) -> QString {
            if (std::abs(v) >= 1e6 || (std::abs(v) < 1e-3 && v != 0.0))
                return QString::number(v, 'e', 2);
            return QString::number(v, 'g', 4);
        };
        QFont fs("Consolas", 9);
        p->setFont(fs);
        p->setPen(pal.color(QPalette::Link));
        p->drawText(tr, Qt::AlignTop | Qt::AlignRight,
                    QString("[%1…%2]").arg(fmtBound(s.viewLo), fmtBound(s.viewHi)));
    }

    const int nameBottom = tr.top() + QFontMetrics(fn).height() + 3;
    const QRect valRect(tr.left(), nameBottom, tr.width(), tr.bottom() - nameBottom);

    const int   n           = sampleCount(s.data);
    const bool  isTimestamp = (s.name == QLatin1String("TimeStamp"));

    if (cursor >= 0 && cursor < n) {
        p->setPen(s.color.lighter(160));
        const std::size_t bufIdx = s.data.index();
        QString valStr;
        if      (bufIdx == 6) valStr = QString::number(sampleAtI64(s.data, cursor));
        else if (bufIdx == 7) valStr = QString::number(sampleAtU64(s.data, cursor));
        else if (bufIdx <= 5) valStr = QString::number(static_cast<long long>(sampleAt(s.data, cursor)));
        else                  valStr = QString::number(sampleAt(s.data, cursor), 'g', 6);

        if (isTimestamp) {
            // Show the raw epoch number at the top and the formatted date/time below.
            QFont fNum("Consolas", 10);
            p->setFont(fNum);
            p->drawText(valRect, Qt::AlignTop | Qt::AlignLeft, valStr);

            QFont fDt("Consolas", 10);
            p->setFont(fDt);
            const double  epochSec = sampleAt(s.data, cursor);
            const QString dtStr    = formatTimestamp(epochSec);
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
        // No cursor or cursor out of range — show the "no data" placeholder.
        p->setPen(dimColor);
        QFont fv("Consolas", 11);
        p->setFont(fv);
        p->drawText(valRect, Qt::AlignBottom | Qt::AlignLeft, QStringLiteral("—"));
    }
}
