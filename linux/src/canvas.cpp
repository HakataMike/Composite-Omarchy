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
    setMinimumSize(320, 240);
    setAccessibleName("Image canvas");
    setToolTip("Drag a layer to move it. Wheel to zoom. Space-drag or middle-drag to pan. Escape cancels a drag.");
}
void Canvas::setDocument(const Arc::Document &d) {
    cancelGesture(); document = d; refreshImage();
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
    if (document.active >= 0) {
        const auto &l = document.layers[document.active];
        if (l.visible) {
            p.setTransform(l.transform(), true);
            QPen outline(QColor(83,168,255), 1, Qt::DashLine); outline.setCosmetic(true);
            p.setPen(outline); p.setBrush(Qt::NoBrush); p.drawRect(QRectF(QPointF(), l.size));
        }
    }
}
void Canvas::mousePressEvent(QMouseEvent *event) {
    setFocus(); pressPoint = event->position(); originalOffset = offset;
    if (event->button() == Qt::MiddleButton || (space && event->button() == Qt::LeftButton)) {
        panning = true; setCursor(Qt::ClosedHandCursor); return;
    }
    if (event->button() != Qt::LeftButton) return;
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
    if (panning) { offset = originalOffset + event->position() - pressPoint; update(); }
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
void Canvas::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}
void Canvas::dropEvent(QDropEvent *event) {
    QStringList paths;
    for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) paths.append(url.toLocalFile());
    if (!paths.isEmpty()) { emit filesDropped(paths); event->acceptProposedAction(); }
}
