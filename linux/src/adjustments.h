#pragma once
#include "filters.h"
namespace Arc {
bool hasAdjustments(const Document &document);
QStringList adjustmentKinds();
QJsonObject defaultAdjustment(const QString &kind);
void validateAdjustment(const QJsonObject &adjustment);
FilterSettings filterFromAdjustment(const QJsonObject &adjustment, int channel = -1, QString range = {});
QJsonObject adjustmentFromFilter(const FilterSettings &settings, const QJsonObject &original);
QImage adjustedImage(const QImage &image, const QJsonObject &adjustment);
void compositeAdjustment(QImage &image, const Layer &layer, const QImage &coverage);
}
