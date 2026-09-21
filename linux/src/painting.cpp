#include "painting.h"
#include <QPainterPathStroker>
#include <cmath>
#include <stdexcept>

namespace Arc {
QImage paintStroke(const Layer &original, const QPainterPath &path, const Brush &brush, QRectF clip) {
    if (original.image.isNull()) throw std::runtime_error("Add a paint layer before painting.");
    if (!std::isfinite(brush.diameter) || brush.diameter < 1 || brush.diameter > 2000
        || !std::isfinite(brush.opacity) || brush.opacity < 0 || brush.opacity > 1 || !brush.color.isValid())
        throw std::runtime_error("Invalid brush settings.");
    if (path.elementCount() == 0 || clip.isEmpty() || brush.opacity == 0) return original.image;

    QImage coverage(original.image.size(), QImage::Format_ARGB32_Premultiplied);
    if (coverage.isNull()) throw std::runtime_error("Insufficient memory for brush stroke.");
    coverage.fill(Qt::transparent);
    QPainterPath shape;
    if (path.elementCount() == 1) {
        shape.addEllipse(path.currentPosition(), brush.diameter/2, brush.diameter/2);
    } else {
        QPainterPathStroker stroker;
        stroker.setWidth(brush.diameter);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        shape = stroker.createStroke(path);
    }
    {
        QPainter p(&coverage);
        // Source pixel -> local layer bounds -> document, then invert for painting.
        QTransform sourceToDocument = original.transform();
        sourceToDocument.scale(original.size.width()/original.image.width(), original.size.height()/original.image.height());
        bool invertible = false;
        auto documentToSource = sourceToDocument.inverted(&invertible);
        if (!invertible) throw std::runtime_error("Cannot paint on this layer transform.");
        p.setTransform(documentToSource);
        p.setClipRect(clip);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillPath(shape, brush.eraser ? QColor(Qt::white) : brush.color);
    }
    QImage result = original.image.copy();
    if (result.isNull()) throw std::runtime_error("Insufficient memory for brush stroke.");
    {
        QPainter p(&result);
        p.setOpacity(brush.opacity);
        if (brush.eraser) p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.drawImage(QPoint(), coverage);
    }
    return result;
}
}
