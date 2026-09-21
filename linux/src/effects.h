#pragma once
#include "document.h"
namespace Arc {
QStringList effectKinds();
QJsonObject defaultEffect(const QString &kind);
void validateEffects(const QJsonObject &effects);
bool hasEffects(const Document &document);
Layer renderedEffects(const Layer &layer);
Document renderEffects(const Document &document);
QRectF visualBounds(const Layer &layer);
}
