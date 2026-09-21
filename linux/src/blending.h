#pragma once
#include <QImage>
#include <QString>
namespace Arc {
bool isNonseparableBlend(const QString &mode);
void blendNonseparable(QImage &backdrop, const QImage &source, const QString &mode);
}
