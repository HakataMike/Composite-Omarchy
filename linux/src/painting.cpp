#include "painting.h"
#include <QPainterPathStroker>
#include <cmath>
#include <stdexcept>

namespace Arc {
QImage paintStroke(const Layer &original, const QPainterPath &path, const Brush &brush, QRectF clip, const QPainterPath &selection) {
    if (original.image.isNull()) throw std::runtime_error("Add a paint layer before painting.");
    if (!std::isfinite(brush.diameter) || brush.diameter < 1 || brush.diameter > 2000
        || !std::isfinite(brush.hardness) || brush.hardness < 0 || brush.hardness > 1
        || !std::isfinite(brush.opacity) || brush.opacity < 0 || brush.opacity > 1 || !brush.color.isValid())
        throw std::runtime_error("Invalid brush settings.");
    if (path.elementCount() == 0 || clip.isEmpty() || brush.opacity == 0) return original.image;

    QTransform sourceToDocument = original.transform();
    sourceToDocument.scale(original.size.width()/original.image.width(), original.size.height()/original.image.height());
    bool invertible = false;
    const auto documentToSource = sourceToDocument.inverted(&invertible);
    if (!invertible) throw std::runtime_error("Cannot paint on this layer transform.");
    double radius = brush.diameter/2 + 2;
    QRectF affected = path.boundingRect().adjusted(-radius,-radius,radius,radius).intersected(clip);
    QRect sourceRect = documentToSource.mapRect(affected).intersected(QRectF(original.image.rect())).toAlignedRect().adjusted(-2,-2,2,2).intersected(original.image.rect());
    if (affected.isEmpty() || sourceRect.isEmpty()) return original.image;
    QImage coverage(sourceRect.size(), QImage::Format_ARGB32_Premultiplied);
    if (coverage.isNull()) throw std::runtime_error("Insufficient memory for brush stroke.");
    coverage.fill(Qt::transparent);
    {
        QPainter p(&coverage);
        p.translate(-sourceRect.topLeft());
        p.setTransform(documentToSource,true);
        p.setClipRect(clip);
        if (!selection.isEmpty()) p.setClipPath(selection,Qt::IntersectClip);
        p.setRenderHint(QPainter::Antialiasing);
        // Nested strokes approximate a linear radial falloff without accumulating
        // opacity where a stroke crosses itself. Hard brushes need only one pass.
        const int rings = brush.hardness == 1 ? 1 : 24;
        double previous = 0;
        for (int i = 0; i < rings; ++i) {
            double fraction = rings == 1 ? 0 : double(i)/(rings-1);
            double diameter = std::max(0.01, brush.diameter * (1 - (1-brush.hardness)*fraction));
            QPainterPath shape;
            if (path.elementCount() == 1) shape.addEllipse(path.currentPosition(), diameter/2, diameter/2);
            else {
                QPainterPathStroker stroker;
                stroker.setWidth(diameter); stroker.setCapStyle(Qt::RoundCap); stroker.setJoinStyle(Qt::RoundJoin);
                shape = stroker.createStroke(path);
            }
            QColor color = brush.eraser ? QColor(Qt::white) : brush.color;
            double target = double(i+1)/rings * color.alphaF();
            color.setAlphaF((target-previous)/(1-previous));
            p.fillPath(shape, color);
            previous = target;
        }
    }
    QImage result = original.image.copy();
    if (result.isNull()) throw std::runtime_error("Insufficient memory for brush stroke.");
    {
        QPainter p(&result);
        p.setOpacity(brush.opacity);
        if (brush.eraser) p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(sourceRect.topLeft(), coverage);
    }
    return result;
}
QImage paintMaskStroke(const Layer &original, const QPainterPath &path, const Brush &brush, QRectF clip, const QPainterPath &selection) {
    if (original.mask.isNull()) throw std::runtime_error("Add a mask before painting mask coverage.");
    Layer target = original;
    // Expand compact uniform masks only when edited; preserve nonuniform resolution.
    QImage mask = original.mask;
    if (mask.size() == QSize(1,1) && !original.image.isNull())
        mask = mask.scaled(original.image.size());
    target.image = mask;
    if (target.image.isNull()) throw std::runtime_error("Insufficient memory for mask painting.");
    Brush settings = brush;
    int coverage = brush.eraser ? 255 : qGray(brush.color.rgb());
    settings.color = QColor(coverage, coverage, coverage); settings.eraser = false;
    auto result = paintStroke(target, path, settings, clip, selection);
    if (result.isNull()) throw std::runtime_error("Insufficient memory for painted mask.");
    return result;
}
}
