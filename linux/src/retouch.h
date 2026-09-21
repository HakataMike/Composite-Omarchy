#pragma once
#include "painting.h"
namespace Arc {
QImage gradientStroke(const Layer &layer,QPointF start,QPointF end,const Brush &brush,QColor background,QRectF clip,const QPainterPath &selection,bool mask);
QImage healStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection);
QImage blurStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection,bool mask);
QImage cloneStroke(const Layer &layer,const QImage &sample,QPointF offset,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection);
}
