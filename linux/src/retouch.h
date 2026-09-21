#pragma once
#include "painting.h"
namespace Arc {
struct GradientOptions { bool radial=false,transparent=false,reversed=false; };
QImage gradientStroke(const Layer &layer,QPointF start,QPointF end,const Brush &brush,QColor background,QRectF clip,const QPainterPath &selection,bool mask,const GradientOptions &options = {});
QImage healStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection,int mode = 0);
QImage warpStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QSize canvas,const QPainterPath &selection,bool smudge);
QImage blurStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection,bool mask);
QImage cloneStroke(const Layer &layer,const QImage &sample,QPointF offset,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection);
}
