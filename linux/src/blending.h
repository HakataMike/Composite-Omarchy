#pragma once
#include <QImage>
#include <QString>
#include <QPainter>
namespace Arc {
QPainter::CompositionMode painterBlendMode(const QString &mode);
void compositeImages(QImage &backdrop, const QImage &source, const QString &mode);
bool isNonseparableBlend(const QString &mode);
void blendNonseparable(QImage &backdrop, const QImage &source, const QString &mode);
}
