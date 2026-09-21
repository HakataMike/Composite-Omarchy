#pragma once
#include "document.h"
#include <QJsonObject>
namespace Arc {
QJsonObject shapeStyle(QString kind, QColor color);
QJsonObject textStyle(QColor color);
void validateStyles(const Layer &layer);
QImage shapeImage(const QJsonObject &style, QSizeF size);
QImage textImage(const QJsonObject &style);
void rasterize(Layer &layer);
}
