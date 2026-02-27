#ifndef CHARTDELEGATE_H
#define CHARTDELEGATE_H

#include "ChartModel.h"
#include <QStyledItemDelegate>

class ChartDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChartDelegate(ChartModel *model, QObject *parent = nullptr);

    void  paint   (QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    void paintData(QPainter *p, const QRect &cell,
                   const ChartSeries &s, int cursor, float pps,
                   int clipXLeft, int clipXRight) const;

private:
    void paintBackground (QPainter *p, const QRect &r, const ChartSeries &s) const;
    void paintDataImpl (QPainter *p, const QRect &cell, const ChartSeries &s,
                       int cursor, float pps, int clipXLeft, int clipXRight) const;

    ChartModel *m_model;
};

#endif // CHARTDELEGATE_H
