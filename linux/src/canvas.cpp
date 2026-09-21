#include "canvas.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <algorithm>
#include <cmath>

Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    setObjectName("canvas"); setFocusPolicy(Qt::StrongFocus); setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setAccessibleName("Image canvas");
    setToolTip("V: Move. B: Brush. E: Eraser. M: Rectangle selection. Wheel to zoom. Space-drag or middle-drag to pan. Escape cancels the current gesture.");
}
void Canvas::setDocument(const Arc::Document &d) {
    cancelGesture();
    if (document.id != d.id || document.size != d.size) selection.reset();
    document = d; refreshImage();
}
void Canvas::setTool(Tool value) {
    cancelGesture(); tool = value;
    setCursor(tool == Tool::Move ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}
void Canvas::setBrush(const Arc::Brush &value) { cancelGesture(); brush = value; update(); }
void Canvas::setMaskTarget(bool enabled) { cancelGesture(); maskTarget = enabled; }
void Canvas::clearSelection() { cancelGesture(); selection.reset(); update(); }
void Canvas::updateSelection(QPointF point) {
    const QRectF bounds(QPointF(), document.size);
    // Snap to pixel edges so selection boundaries have predictable coverage.
    QPointF end(std::round(point.x()), std::round(point.y()));
    selection = QRectF(selectionStart, end).normalized().intersected(bounds);
    update();
}
void Canvas::updateStroke(QPointF point) {
    try {
        if (strokePath.elementCount() == 0) strokePath.moveTo(point);
        else if (strokePath.currentPosition() != point) strokePath.lineTo(point);
        Arc::Brush settings = brush;
        settings.eraser = tool == Tool::Eraser;
        QRectF clip(QPointF(), document.size);
        if (selection) clip = clip.intersected(*selection);
        if (maskTarget) document.layers[dragLayer].mask = Arc::paintMaskStroke(strokeOriginal, strokePath, settings, clip);
        else document.layers[dragLayer].image = Arc::paintStroke(strokeOriginal, strokePath, settings, clip);
        refreshImage();
    } catch (const std::exception &error) {
        cancelGesture();
        emit errorOccurred(QString::fromUtf8(error.what()));
    }
}
void Canvas::refreshImage() {
    composite = Arc::render(document); update();
}
QPointF Canvas::documentPoint(QPointF point) const { return (point - offset) / zoom; }
QPointF Canvas::canvasToWidget(QPointF point) const { return point * zoom + offset; }
void Canvas::fit() {
    cancelGesture();
    zoom = std::clamp(std::min((width()-64.0)/document.size.width(), (height()-64.0)/document.size.height()), 0.01, 32.0);
    offset = QPointF((width()-document.size.width()*zoom)/2, (height()-document.size.height()*zoom)/2);
    emit zoomChanged(zoom); update();
}
void Canvas::zoomBy(double factor) {
    cancelGesture();
    QPointF center(width()/2.0, height()/2.0), anchor = documentPoint(center);
    zoom = std::clamp(zoom * factor, 0.01, 32.0); offset = center - anchor*zoom;
    emit zoomChanged(zoom); update();
}
void Canvas::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(), palette().color(QPalette::Dark));
    p.translate(offset); p.scale(zoom, zoom);
    QRectF bounds(QPointF(), document.size);
    p.save(); p.setClipRect(bounds);
    // Checkerboard is a display backdrop, never part of blend calculations or exports.
    QPixmap tile(24, 24); tile.fill(QColor(205, 205, 205));
    { QPainter checker(&tile); checker.fillRect(0,0,12,12,QColor(240,240,240)); checker.fillRect(12,12,12,12,QColor(240,240,240)); }
    p.fillRect(bounds, QBrush(tile));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 1);
    p.drawImage(QPoint(), composite); p.restore();
    QPen border(palette().color(QPalette::Mid)); border.setCosmetic(true); p.setPen(border); p.drawRect(bounds);
    if (document.active >= 0 && tool == Tool::Move) {
        const auto &l = document.layers[document.active];
        if (l.visible) {
            p.save();
            p.setTransform(l.transform(), true);
            QPen outline(QColor(83,168,255), 1, Qt::DashLine); outline.setCosmetic(true);
            p.setPen(outline); p.setBrush(Qt::NoBrush); p.drawRect(QRectF(QPointF(), l.size));
            p.restore();
        }
    }
    if (selection && !selection->isEmpty()) {
        QPen white(Qt::white, 1); white.setCosmetic(true);
        p.setPen(white); p.setBrush(Qt::NoBrush); p.drawRect(*selection);
        QPen black(Qt::black, 1, Qt::DashLine); black.setCosmetic(true);
        p.setPen(black); p.drawRect(*selection);
    }
    if (underMouse() && !panning && (tool == Tool::Brush || tool == Tool::Eraser)) {
        QPointF center = documentPoint(pointerPosition);
        QPen white(Qt::white, 2); white.setCosmetic(true);
        p.setBrush(Qt::NoBrush); p.setPen(white); p.drawEllipse(center, brush.diameter/2, brush.diameter/2);
        QPen black(Qt::black, 1); black.setCosmetic(true);
        p.setPen(black); p.drawEllipse(center, brush.diameter/2, brush.diameter/2);
    }
}
void Canvas::mousePressEvent(QMouseEvent *event) {
    pointerPosition = event->position();
    setFocus(); pressPoint = event->position(); originalOffset = offset;
    if (event->button() == Qt::MiddleButton || (space && event->button() == Qt::LeftButton)) {
        panning = true; setCursor(Qt::ClosedHandCursor); return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (tool == Tool::Rectangle) {
        previousSelection = selection;
        auto point = documentPoint(event->position());
        selectionStart = {std::clamp(std::round(point.x()), 0.0, double(document.size.width())),
                          std::clamp(std::round(point.y()), 0.0, double(document.size.height()))};
        selecting = true;
        updateSelection(point);
        return;
    }
    if (tool == Tool::Brush || tool == Tool::Eraser) {
        int index = document.active;
        if (index < 0 || document.layers[index].image.isNull() || !document.layers[index].visible) {
            emit errorOccurred("Select a visible image layer, or add a paint layer, before painting.");
            return;
        }
        if (maskTarget && (document.layers[index].mask.isNull() || !document.layers[index].maskEnabled)) {
            emit errorOccurred("Add and enable the layer mask before painting it.");
            return;
        }
        dragLayer = index;
        strokeOriginal = document.layers[index];
        strokePath = QPainterPath();
        painting = true;
        updateStroke(documentPoint(event->position()));
        return;
    }
    int hit = -1;
    auto point = documentPoint(event->position());
    for (int i = document.layers.size()-1; i >= 0; --i) {
        const auto &l = document.layers[i];
        if (l.visible && !l.image.isNull() && QRectF(QPointF(), l.size).contains(l.transform().inverted().map(point))) { hit = i; break; }
    }
    emit selected(hit);
    // Selection may synchronously replace the displayed document.
    if (hit >= 0) {
        dragLayer = hit; originalOrigin = document.layers[hit].origin; dragging = true;
    }
}
void Canvas::mouseMoveEvent(QMouseEvent *event) {
    pointerPosition = event->position();
    update();
    if (panning) { offset = originalOffset + event->position() - pressPoint; update(); }
    else if (painting) updateStroke(documentPoint(event->position()));
    else if (selecting) updateSelection(documentPoint(event->position()));
    else if (dragging) {
        QPointF delta = (event->position() - pressPoint) / zoom;
        if (event->modifiers() & Qt::ShiftModifier) {
            if (std::abs(delta.x()) > std::abs(delta.y())) delta.setY(0); else delta.setX(0);
        }
        auto point = originalOrigin + delta;
        document.layers[dragLayer].origin = {std::clamp(point.x(), -1000000.0, 1000000.0), std::clamp(point.y(), -1000000.0, 1000000.0)};
        refreshImage();
    }
}
void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if (painting && event->button() == Qt::LeftButton) {
        updateStroke(documentPoint(event->position()));
        if (!painting) return;
        int index = dragLayer;
        QImage result = maskTarget ? document.layers[index].mask : document.layers[index].image;
        bool changed = result != (maskTarget ? strokeOriginal.mask : strokeOriginal.image);
        painting = false; dragLayer = -1;
        strokeOriginal = Arc::Layer(); strokePath = QPainterPath();
        if (changed) {
            if (maskTarget) emit maskPainted(index, result);
            else emit painted(index, result);
        }
        return;
    }
    if (selecting && event->button() == Qt::LeftButton) {
        updateSelection(documentPoint(event->position()));
        selecting = false;
        if (selection->isEmpty()) selection.reset();
        previousSelection.reset();
        update();
        return;
    }
    if (panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning = false; unsetCursor();
    }
    if (dragging && event->button() == Qt::LeftButton) {
        auto index = dragLayer; auto point = document.layers[index].origin;
        dragging = false; dragLayer = -1;
        if (point != originalOrigin) emit moved(index, point);
    }
}
void Canvas::wheelEvent(QWheelEvent *event) {
    cancelGesture();
    QPointF anchor = documentPoint(event->position());
    zoom = std::clamp(zoom * std::pow(1.0015, event->angleDelta().y()), 0.01, 32.0);
    offset = event->position() - anchor*zoom; emit zoomChanged(zoom); update(); event->accept();
}
void Canvas::cancelGesture() {
    if (painting) {
        document.layers[dragLayer] = strokeOriginal;
        painting = false; dragLayer = -1;
        strokeOriginal = Arc::Layer(); strokePath = QPainterPath();
        refreshImage();
    }
    if (selecting) { selection = previousSelection; selecting = false; previousSelection.reset(); update(); }
    if (dragging) { document.layers[dragLayer].origin = originalOrigin; dragging = false; dragLayer = -1; refreshImage(); }
    panning = false; unsetCursor();
}
void Canvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space) { space = true; setCursor(Qt::OpenHandCursor); event->accept(); }
    else if (event->key() == Qt::Key_Escape) { cancelGesture(); event->accept(); }
    else QWidget::keyPressEvent(event);
}
void Canvas::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) { space = false; unsetCursor(); event->accept(); }
    else QWidget::keyReleaseEvent(event);
}
void Canvas::focusOutEvent(QFocusEvent *event) { space = false; cancelGesture(); QWidget::focusOutEvent(event); }
void Canvas::leaveEvent(QEvent *event) { update(); QWidget::leaveEvent(event); }
void Canvas::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void Canvas::dropEvent(QDropEvent *event) {
    QStringList paths;
    for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) paths.append(url.toLocalFile());
    if (!paths.isEmpty()) { emit filesDropped(paths); event->acceptProposedAction(); }
}
