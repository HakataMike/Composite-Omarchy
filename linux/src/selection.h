#pragma once
#include <QImage>
#include <QPainterPath>
namespace Arc {
QPainterPath wandSelection(const QImage &image, QPoint seed, int tolerance, bool contiguous);
QPainterPath alphaSelection(const QImage &image);
}

#include "document.h"
namespace Arc {
QImage pathCoverage(const QPainterPath &path,QSize size);
QImage alphaCoverage(const QImage &image);
QPainterPath coverageOutline(const QImage &coverage);
QImage selectionInLayer(const Layer &layer,QSize pixels,const QImage &coverage);
QImage limitToSelection(const QImage &original,const QImage &changed,const Layer &target,const QImage &coverage);
QImage selectedLayerPixels(const Document &document,const QImage &coverage);
void cutSelectedPixels(Document &document,const QImage &coverage);
void floatSelectedPixels(Document &document,const QImage &coverage,QPointF offset,bool duplicate);
}
