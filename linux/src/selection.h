#pragma once
#include <QImage>
#include <QPainterPath>
namespace Arc {
QPainterPath wandSelection(const QImage &image, QPoint seed, int tolerance, bool contiguous);
QPainterPath alphaSelection(const QImage &image);
}
