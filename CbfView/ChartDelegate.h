#ifndef CHARTDELEGATE_H
#define CHARTDELEGATE_H

#include "ChartModel.h"

#include <QStyledItemDelegate>
#include <cfloat>

class ChartDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChartDelegate(ChartModel *model, QObject *parent = nullptr);

    void  paint   (QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    void paintChart(QPainter *p, const QRect &r,
                    const ChartSeries &s, int cursor, float pps) const;

    ChartModel *m_model;
};

#endif // CHARTDELEGATE_H
