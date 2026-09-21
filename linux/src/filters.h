#pragma once
#include "document.h"
#include <QJsonObject>
#include <QPainterPath>
#include <optional>
namespace Arc {
struct FilterParameter { QString key,label; double minimum,maximum,initial; };
struct FilterSettings {
    QString kind;
    QJsonObject values;
    QColor shadows=Qt::black, highlights=Qt::white;
    QVector<QPointF> curve={{0,0},{255,255}};
    int channel=0;
    quint32 seed=1;
};
double evaluateCurve(const QVector<QPointF> &points, double x);
QStringList filterNames();
QVector<FilterParameter> filterParameters(const QString &kind);
FilterSettings defaultFilter(const QString &kind);
QImage applyFilter(const QImage &image,const FilterSettings &filter);
QImage filterLayer(const Layer &layer,const FilterSettings &filter,const std::optional<QPainterPath> &selection,bool mask);
QPair<int,int> autoLevels(const QImage &image, int channel = 0);
Layer contentAwareFillExpanded(const Layer &layer,const QPainterPath &selection,const QImage &coverage);
QImage contentAwareFill(const Layer &layer,const QPainterPath &selection);
}
