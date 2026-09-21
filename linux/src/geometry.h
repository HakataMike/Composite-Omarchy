#pragma once
#include "document.h"
#include <QPolygonF>
namespace Arc {
Layer placedLayer(const Layer &original,const QTransform &localToDocument);
QVector<QUuid> selectedRoots(const Document &document,const QVector<QUuid> &selected);
Layer transformBox(const Document &document,const QVector<QUuid> &selected);
void transformLayers(Document &document,const QVector<QUuid> &selected,const Layer &before,const Layer &after);
void distortLayers(Document &document,const QVector<QUuid> &selected,const QPolygonF &before,const QPolygonF &after);
QPolygonF layerCorners(const Layer &layer);
}
