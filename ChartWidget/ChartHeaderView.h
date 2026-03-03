#ifndef CHARTHEADERVIEW_H
#define CHARTHEADERVIEW_H

#include "ChartModel.h"

#include <QHeaderView>
#include <QString>
#include <QSet>

class ChartHeaderView : public QHeaderView
{
    Q_OBJECT
public:
    explicit ChartHeaderView(ChartModel *model, QWidget *parent = nullptr);

    // Текущее выделение
    QSet<int> selectedRows() const { return m_selectedRows; }

    // Форматирование epoch-секунд в читаемую строку
    static QString formatTimestamp(double epochSec);
    // Однострочная версия для тулбара (без \n)
    static QString formatTimestampLine(double epochSec);

    // Вызывается из ChartWidget, чтобы header знал текущее состояние авто-подстройки
    void setAutoFitY(bool on);

signals:
    // Операции над выделением — ChartView подключается и выполняет
    void syncScaleRequested    (int sourceRow, QSet<int> rows);
    void overlayRequested      (int sourceRow, QSet<int> rows);
    void resetSelectedRequested(QSet<int> rows);

    // View-level операции
    void fitYToVisibleRequested();
    void autoFitYToggleRequested();

protected:
    void paintEvent        (QPaintEvent        *e) override;
    void mouseMoveEvent    (QMouseEvent        *e) override;
    void mousePressEvent   (QMouseEvent        *e) override;
    void mouseReleaseEvent (QMouseEvent        *e) override;
    void contextMenuEvent  (QContextMenuEvent  *e) override;
    void leaveEvent        (QEvent             *e) override;

private slots:
    void onCursorMoved  (int sample);
    void onLayoutChanged();

private:
    void paintRow(QPainter *p, const QRect &r,
                  const ChartSeries &s, int cursor, bool selected) const;

    static constexpr int kResizeZone       = 5;
    static constexpr int kHeaderResizeZone = 6;
    static constexpr int kHeaderMinWidth   = 80;
    static constexpr int kHeaderMaxWidth   = 500;

    int rowAtResizeHandle(int y) const; // строка под resize-ручкой нижнего края
    int rowAt(int y) const;             // строка под указателем (не resize-зона)

    ChartModel *m_model;

    // Resize строки по вертикали
    bool m_resizing     = false;
    int  m_resizeRow    = -1;
    int  m_resizePressY = 0;
    int  m_resizeStartH = 0;

    // Resize ширины заголовка (drag правого края)
    bool m_headerResizing     = false;
    int  m_headerResizePressX = 0;
    int  m_headerResizeStartW = 0;

    // Selection
    QSet<int> m_selectedRows;
    int       m_lastClickedRow = -1;

    // Зеркало состояния ChartView::m_autoFitY
    bool m_autoFitY = false;
};

#endif // CHARTHEADERVIEW_H
