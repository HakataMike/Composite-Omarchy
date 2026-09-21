#pragma once
#include "document.h"
#include <QPainterPath>

namespace Arc {
struct Brush {
    QColor color = Qt::black;
    double diameter = 24;
    double opacity = 1;
    double hardness = 1;
    bool eraser = false;
};
// Path and clip are in document coordinates; pixels remain in layer coordinates.
// Every preview starts from the original image so opacity applies once per stroke.
QImage paintStroke(const Layer &original, const QPainterPath &path, const Brush &brush, QRectF clip);
QImage paintMaskStroke(const Layer &original, const QPainterPath &path, const Brush &brush, QRectF clip);
}
