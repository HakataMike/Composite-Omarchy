#pragma once
#include "document.h"
namespace Arc {
void crop(Document &document, QRect rectangle);
void resizeCanvas(Document &document, QSize size, bool centered);
void resizeImage(Document &document, QSize size);
QPointF snapLayerOrigin(const Document &document, int index, QPointF origin, double tolerance, bool guides = true);
void flipCanvas(Document &document, bool horizontal);
void mergeDown(Document &document);
void mergeSelected(Document &document,const QVector<QUuid> &selected);
QImage blurImage(const QImage &image, int radius);
}
