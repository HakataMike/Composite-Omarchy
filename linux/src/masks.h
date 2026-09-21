#pragma once
#include "document.h"
namespace Arc {
QJsonObject placementOf(const Layer &layer);
Layer maskTargetLayer(const Layer &layer);
void validateMaskPlacement(const Layer &layer);
QImage rasterMask(const Layer &layer, QSize size);
void followMask(const Layer &before, Layer &after);
void offsetMask(Layer &layer, QPointF delta);
void flipMask(Layer &layer, QSize canvas, bool horizontal);
}
