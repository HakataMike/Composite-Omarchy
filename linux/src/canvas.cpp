#include "canvas.h"
#include "selection.h"
#include "retouch.h"
#include "operations.h"
#include "hierarchy.h"
#include "masks.h"
#include "geometry.h"
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
    if (document.id != d.id || document.size != d.size) { selection.reset(); selectionMask={}; cloneAnchor.reset(); cloneOffset.reset(); lastBrushPoint.reset(); }
    document = d; refreshImage();
}
void Canvas::setTool(Tool value) {
    cancelGesture(); tool = value;
    setCursor(tool == Tool::Move ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}
void Canvas::setBrush(const Arc::Brush &value) { cancelGesture(); brush = value; update(); }
void Canvas::setMaskTarget(bool enabled) { cancelGesture(); maskTarget = enabled; }
void Canvas::setGuidesVisible(bool enabled) { cancelGesture(); guidesVisible=enabled; update(); }
void Canvas::setGuidesLocked(bool enabled) { cancelGesture(); guidesLocked=enabled; }
void Canvas::setSnapping(bool enabled) { cancelGesture(); snapping=enabled; }
void Canvas::clearSelection() { cancelGesture(); selection.reset(); selectionMask={}; update(); }
void Canvas::setSelection(const QPainterPath &path) { cancelGesture(); selection=path; selectionMask={}; update(); }
QImage Canvas::selectedCoverage() const {
    if(!selection) return {};
    return selectionMask.isNull() ? Arc::pathCoverage(*selection,document.size) : selectionMask;
}
void Canvas::setSelectionCoverage(const QImage &mask) {
    if(mask.size()!=document.size) throw std::runtime_error("Selection dimensions do not match the document.");
    auto gray=mask.convertToFormat(QImage::Format_Grayscale8);
    if(gray.isNull()) throw std::runtime_error("Insufficient memory for selection.");
    auto outline=Arc::coverageOutline(gray);
    cancelGesture(); selectionMask=gray; selection=outline; update();
}
void Canvas::featherSelection(int radius) {
    if(selection) setSelectionCoverage(Arc::blurImage(selectedCoverage(),radius));
}
void Canvas::invertSelection() {
    auto mask=selectedCoverage(); if(mask.isNull()) { mask=QImage(document.size,QImage::Format_Grayscale8); mask.fill(Qt::black); }
    mask.invertPixels(); setSelectionCoverage(mask);
}
void Canvas::moveSelection(QPointF delta) {
    selectionOffset=delta;
    QTransform t; t.translate(delta.x(),delta.y());
    selection=t.map(*previousSelection);
    if(!previousSelectionMask.isNull()) {
        selectionMask=QImage(document.size,QImage::Format_Grayscale8);
        if(selectionMask.isNull()) throw std::runtime_error("Insufficient memory to move selection.");
        selectionMask.fill(Qt::black);
        QPainter p(&selectionMask); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(delta,previousSelectionMask);
    }
    if(movingPixels) { document=dragOriginal; Arc::floatSelectedPixels(document,previousSelectionMask,delta,duplicatePixels); refreshImage(); }
    update();
}
void Canvas::combineSelection(const QPainterPath &path) {
    QPainterPath canvasBounds; canvasBounds.addRect(QRectF(QPointF(),document.size));
    auto combined=path;
    if(previousSelection && selectionOperation==1) combined=previousSelection->united(path);
    if(previousSelection && selectionOperation==2) combined=previousSelection->subtracted(path);
    combined=combined.intersected(canvasBounds); QImage coverage;
    if(previousSelection && !previousSelectionMask.isNull() && selectionOperation!=0) {
        coverage=Arc::pathCoverage(path,document.size);
        for(int y=0;y<coverage.height();++y) for(int x=0;x<coverage.width();++x) {
            int old=previousSelectionMask.constScanLine(y)[x],value=coverage.constScanLine(y)[x];
            coverage.scanLine(y)[x]=selectionOperation==1 ? std::max(old,value) : old*(255-value)/255;
        }
        combined=Arc::coverageOutline(coverage);
    }
    selection=combined; selectionMask=coverage;
    update();
}
void Canvas::updateSelection(QPointF point) {
    QPainterPath shape;
    if(tool==Tool::Lasso) {
        selectionGesture.lineTo(point); shape=selectionGesture; shape.closeSubpath();
    } else if(tool==Tool::Polygon) {
        shape=selectionGesture; shape.lineTo(point); shape.closeSubpath();
    } else {
        QPointF end(std::round(point.x()),std::round(point.y()));
        QRectF rectangle=QRectF(selectionStart,end).normalized();
        if(tool==Tool::Ellipse) shape.addEllipse(rectangle); else shape.addRect(rectangle);
    }
    try { combineSelection(shape); }
    catch(const std::exception &error) { cancelGesture(); emit errorOccurred(QString::fromUtf8(error.what())); }
}
void Canvas::updateStroke(QPointF point) {
    try {
        if (strokePath.elementCount() == 0) strokePath.moveTo(point);
        else if (strokePath.currentPosition() != point) strokePath.lineTo(point);
        Arc::Brush settings = brush;
        settings.eraser = tool == Tool::Eraser;
        QRectF clip(QPointF(), document.size);
        if (selection) clip = clip.intersected(selection->boundingRect());
        if(tool==Tool::Gradient) {
            QPointF start(strokePath.elementAt(0).x,strokePath.elementAt(0).y);
            auto result=Arc::gradientStroke(strokeOriginal,start,point,settings,backgroundColor,clip,(selectionMask.isNull() ? selection.value_or(QPainterPath()) : QPainterPath()),maskTarget);
            if(maskTarget) document.layers[dragLayer].mask=result; else document.layers[dragLayer].image=result;
        } else if(tool==Tool::Clone) document.layers[dragLayer].image=Arc::cloneStroke(strokeOriginal,cloneSample,*cloneOffset,strokePath,settings,clip,(selectionMask.isNull() ? selection.value_or(QPainterPath()) : QPainterPath()));
        else if(tool==Tool::Heal) document.layers[dragLayer].image=Arc::healStroke(strokeOriginal,strokePath,settings,clip,(selectionMask.isNull() ? selection.value_or(QPainterPath()) : QPainterPath()));
        else if(tool==Tool::Blur) {
            auto result=Arc::blurStroke(strokeOriginal,strokePath,settings,clip,(selectionMask.isNull() ? selection.value_or(QPainterPath()) : QPainterPath()),maskTarget);
            if(maskTarget) document.layers[dragLayer].mask=result; else document.layers[dragLayer].image=result;
        }
        else if (maskTarget) document.layers[dragLayer].mask = Arc::paintMaskStroke(strokeOriginal, strokePath, settings, clip, (selectionMask.isNull() ? selection.value_or(QPainterPath()) : QPainterPath()));
        else document.layers[dragLayer].image = Arc::paintStroke(strokeOriginal, strokePath, settings, clip, (selectionMask.isNull() ? selection.value_or(QPainterPath()) : QPainterPath()));
        if(!selectionMask.isNull()) {
            auto &result=maskTarget ? document.layers[dragLayer].mask : document.layers[dragLayer].image;
            result=Arc::limitToSelection(maskTarget ? strokeOriginal.mask : strokeOriginal.image,result,
                maskTarget ? Arc::maskTargetLayer(strokeOriginal) : strokeOriginal,selectionMask);
        }
        double radius = settings.diameter/2 + 3;
        QRect region = strokePath.boundingRect().adjusted(-radius,-radius,radius,radius).toAlignedRect()
            .intersected(QRect(QPoint(),document.size));
        if(tool==Tool::Gradient) region=clip.toAlignedRect().intersected(QRect(QPoint(),document.size));
        if (!region.isEmpty()) {
            Arc::repaintRegion(composite,document,region);
        }
        update();
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
        auto box=activeTransformBox(); auto corners=distortActive && transformHandle>=0 ? transformQuad : Arc::layerCorners(box);
        QPen outline(QColor(83,168,255),1,Qt::DashLine); outline.setCosmetic(true); p.setPen(outline); p.setBrush(Qt::NoBrush); p.drawPolygon(corners);
        p.setPen(QPen(QColor(83,168,255),0)); p.setBrush(Qt::white);
        for(int i=0;i<8;++i) {
            auto point=i%2 ? (corners[i/2]+corners[(i/2+1)%4])/2 : corners[i/2];
            p.drawRect(QRectF(point-QPointF(3/zoom,3/zoom),QSizeF(6/zoom,6/zoom)));
        }
        auto rotate=box.transform().map(QPointF(box.size.width()/2,-24/zoom));
        p.drawEllipse(rotate,4/zoom,4/zoom);
    }
    if(guidesVisible) {
        p.save(); p.setClipRect(bounds);
        QPen guidePen(QColor(0,220,240),1); guidePen.setCosmetic(true); p.setPen(guidePen);
        for(const auto &g : document.guides) {
            if(g.axis=="vertical") p.drawLine(QPointF(g.position,0),QPointF(g.position,document.size.height()));
            else p.drawLine(QPointF(0,g.position),QPointF(document.size.width(),g.position));
        }
        p.restore();
    }
    if(drafting) {
        QPen pen(brush.color,tool==Tool::ShapeLine ? 4 : 1); pen.setCosmetic(tool!=Tool::ShapeLine);
        p.setPen(pen); p.setBrush(tool==Tool::Text ? QBrush(Qt::NoBrush) : QBrush(brush.color));
        p.setRenderHint(QPainter::Antialiasing);
        auto box=QRectF(draftStart,draftEnd).normalized();
        if(tool==Tool::ShapeLine) p.drawLine(draftStart,draftEnd);
        else if(tool==Tool::ShapeEllipse) p.drawEllipse(box);
        else p.drawRect(box);
    }
    if (selection && !selection->isEmpty()) {
        QPen white(Qt::white, 1); white.setCosmetic(true);
        p.setPen(white); p.setBrush(Qt::NoBrush); p.drawPath(*selection);
        QPen black(Qt::black, 1, Qt::DashLine); black.setCosmetic(true);
        p.setPen(black); p.drawPath(*selection);
    }
    if (underMouse() && !panning && (tool == Tool::Brush || tool == Tool::Eraser || tool == Tool::Clone || tool == Tool::Heal || tool == Tool::Blur)) {
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
    if(tool==Tool::Move && document.active>=0) {
        auto box=activeTransformBox(); auto corners=Arc::layerCorners(box); auto point=documentPoint(event->position());
        int handle=-1;
        for(int i=0;i<8;++i) {
            auto position=i%2 ? (corners[i/2]+corners[(i/2+1)%4])/2 : corners[i/2];
            if(QLineF(point,position).length()<7/zoom) handle=i;
        }
        auto rotate=box.transform().map(QPointF(box.size.width()/2,-24/zoom));
        if(QLineF(point,rotate).length()<7/zoom) handle=8;
        if(handle<0 && selectedIDs.size()>1 && !selection && box.transform().mapRect(QRectF(QPointF(),box.size)).contains(point)) handle=9;
        if(handle>=0) {
            transformMask=maskTarget && !document.layers[document.active].maskLinked && !document.layers[document.active].mask.isNull();
            transformIDs=selectedIDs; if(transformIDs.isEmpty()) transformIDs={document.layers[document.active].id};
            if(transformMask) transformIDs={document.layers[document.active].id};
            boxOriginal=box; dragOriginal=document; transformHandle=handle; transformQuad=corners;
            distortActive=handle<8 && handle%2==0 && (event->modifiers() & Qt::ControlModifier) && !transformMask;
            return;
        }
    }
    if(tool==Tool::Move && guidesVisible && !guidesLocked && !(event->modifiers() & Qt::AltModifier)
        && QRectF(QPointF(),document.size).contains(documentPoint(event->position()))) {
        auto point=documentPoint(event->position());
        double distance=5/zoom;
        for(int i=0;i<document.guides.size();++i) {
            const auto &g=document.guides[i];
            double candidate=std::abs((g.axis=="vertical" ? point.x() : point.y())-g.position);
            if(candidate<distance) { dragGuide=i; distance=candidate; }
        }
        if(dragGuide>=0) { originalGuidePosition=document.guides[dragGuide].position; return; }
    }
    if(tool==Tool::ShapeRectangle || tool==Tool::ShapeEllipse || tool==Tool::ShapeLine || tool==Tool::Text) {
        drafting=true; draftAnchor=documentPoint(event->position());
        updateDraft(draftAnchor,event->modifiers()); return;
    }
    if(tool==Tool::Eyedropper) {
        auto sample=documentPoint(event->position());
        QPoint point(std::floor(sample.x()),std::floor(sample.y()));
        if(composite.rect().contains(point)) emit colorPicked(composite.pixelColor(point));
        return;
    }
    if(tool==Tool::Clone && (event->modifiers() & Qt::AltModifier)) {
        cloneAnchor=documentPoint(event->position()); cloneOffset.reset(); return;
    }
    if (tool == Tool::Wand) {
        previousSelection=selection; previousSelectionMask=selectionMask;
        selectionOperation=(event->modifiers() & Qt::AltModifier) ? 2 : (event->modifiers() & Qt::ShiftModifier) ? 1 : 0;
        try { combineSelection(Arc::wandSelection(composite,documentPoint(event->position()).toPoint(),wandTolerance,true)); }
        catch(const std::exception &error) { emit errorOccurred(QString::fromUtf8(error.what())); }
        previousSelection.reset(); return;
    }
    if(!selecting && selection && selection->contains(documentPoint(event->position()))
        && (((tool==Tool::Rectangle || tool==Tool::Ellipse || tool==Tool::Lasso) && event->modifiers()==Qt::NoModifier)
            || (tool==Tool::Move && !maskTarget && document.active>=0 && !document.layers[document.active].image.isNull()))) {
        movingSelection=true; movingPixels=tool==Tool::Move; duplicatePixels=event->modifiers() & Qt::AltModifier;
        previousSelection=selection; previousSelectionMask=movingPixels ? selectedCoverage() : selectionMask;
        selectionOffset={}; dragOriginal=document; return;
    }
    if (tool == Tool::Polygon && selecting) {
        selectionGesture.lineTo(documentPoint(event->position())); updateSelection(documentPoint(event->position())); return;
    }
    if (tool == Tool::Rectangle || tool == Tool::Ellipse || tool == Tool::Lasso || tool == Tool::Polygon) {
        previousSelection = selection; previousSelectionMask=selectionMask;
        selectionOperation=(event->modifiers() & Qt::AltModifier) ? 2 : (event->modifiers() & Qt::ShiftModifier) ? 1 : 0;
        auto point = documentPoint(event->position());
        selectionStart = {std::clamp(std::round(point.x()), 0.0, double(document.size.width())),
                          std::clamp(std::round(point.y()), 0.0, double(document.size.height()))};
        selecting = true;
        selectionGesture=QPainterPath(); selectionGesture.moveTo(point);
        updateSelection(point);
        return;
    }
    if (tool == Tool::Brush || tool == Tool::Eraser || tool==Tool::Gradient || tool==Tool::Clone || tool==Tool::Heal || tool==Tool::Blur) {
        int index = document.active;
        if (index < 0 || (document.layers[index].image.isNull() && !((document.layers[index].isGroup || !document.layers[index].adjustment.isEmpty()) && maskTarget)) || !Arc::effectiveVisible(document,index)) {
            emit errorOccurred("Select a visible image layer, or add a paint layer, before painting.");
            return;
        }
        if (maskTarget && (document.layers[index].mask.isNull() || !document.layers[index].maskEnabled)) {
            emit errorOccurred("Add and enable the layer mask before painting it.");
            return;
        }
        if(maskTarget && (tool==Tool::Clone || tool==Tool::Heal)) { emit errorOccurred("Clone and healing tools edit image pixels. Choose Paint image first."); return; }
        if(tool==Tool::Clone) {
            if(!cloneAnchor) { emit errorOccurred("Alt-click to choose the clone source first."); return; }
            if(!cloneAligned || !cloneOffset) cloneOffset=*cloneAnchor-documentPoint(event->position());
            try {
            if(cloneAll) cloneSample=composite;
            else { auto single=document; single.layers={document.layers[index]}; single.active=0; single.layers[0].parentID={}; single.layers[0].maskSourceID={}; single.layers[0].opacity=1; single.layers[0].blend="Normal"; single.layers[0].maskEnabled=false; cloneSample=Arc::render(single); }
            } catch(const std::exception &error) { cloneSample=QImage(); emit errorOccurred(QString::fromUtf8(error.what())); return; }
        }
        dragLayer = index;
        strokeOriginal = document.layers[index];
        strokePath = QPainterPath();
        if(lastBrushPoint && (event->modifiers() & Qt::ShiftModifier) && (tool==Tool::Brush || tool==Tool::Eraser)) strokePath.moveTo(*lastBrushPoint);
        painting = true;
        updateStroke(documentPoint(event->position()));
        return;
    }
    if(maskTarget && document.active>=0) {
        const auto &l=document.layers[document.active];
        if(!l.maskLinked && !l.mask.isNull() && Arc::effectiveVisible(document,document.active)) {
            auto target=Arc::maskTargetLayer(l);
            if(QRectF(QPointF(),target.size).contains(target.transform().inverted().map(documentPoint(event->position())))) {
                dragOriginal=document; dragLayer=document.active; originalOrigin=target.origin; dragging=true; draggingMask=true; return;
            }
        }
    }
    int hit = -1;
    auto point = documentPoint(event->position());
    for (int i : Arc::visibleLayerOrder(document,true)) {
        const auto &l = document.layers[i];
        if (!l.image.isNull() && QRectF(QPointF(), l.size).contains(l.transform().inverted().map(point))) { hit = i; break; }
    }
    if(document.active>=0 && document.layers[document.active].isGroup && Arc::effectiveVisible(document,document.active)) {
        const auto &group=document.layers[document.active];
        if(QRectF(QPointF(),group.size).contains(group.transform().inverted().map(point))) hit=document.active;
    }
    emit selected(hit,event->modifiers() & Qt::ShiftModifier);
    // Selection may synchronously replace the displayed document.
    if (hit >= 0) {
        draggingMask=false; dragOriginal=document; dragLayer = hit; originalOrigin = document.layers[hit].origin; dragging = true;
    }
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *event) {
    if(tool==Tool::Polygon && selecting && event->button()==Qt::LeftButton) {
        updateSelection(documentPoint(event->position())); selecting=false; previousSelection.reset(); update();
    } else QWidget::mouseDoubleClickEvent(event);
}
void Canvas::mouseMoveEvent(QMouseEvent *event) {
    pointerPosition = event->position();
    update();
    if (panning) { offset = originalOffset + event->position() - pressPoint; update(); }
    else if (dragGuide>=0) {
        auto &g=document.guides[dragGuide]; auto point=documentPoint(event->position());
        g.position=std::clamp(g.axis=="vertical" ? point.x() : point.y(),-1000000.0,1000000.0); update();
    }
    else if(transformHandle>=0) {
        try { updateTransform(documentPoint(event->position()),event->modifiers()); }
        catch(const std::exception &error) { cancelGesture(); emit errorOccurred(QString::fromUtf8(error.what())); }
    }
    else if(movingSelection) {
        auto delta=(event->position()-pressPoint)/zoom;
        if(event->modifiers() & Qt::ShiftModifier) { if(std::abs(delta.x())>std::abs(delta.y())) delta.setY(0); else delta.setX(0); }
        try { moveSelection(QPointF(std::round(delta.x()),std::round(delta.y()))); }
        catch(const std::exception &error) { cancelGesture(); emit errorOccurred(QString::fromUtf8(error.what())); }
    }
    else if (drafting) updateDraft(documentPoint(event->position()),event->modifiers());
    else if (painting) updateStroke(documentPoint(event->position()));
    else if (selecting) updateSelection(documentPoint(event->position()));
    else if (dragging) {
        QPointF delta = (event->position() - pressPoint) / zoom;
        if (event->modifiers() & Qt::ShiftModifier) {
            if (std::abs(delta.x()) > std::abs(delta.y())) delta.setY(0); else delta.setX(0);
        }
        auto point = originalOrigin + delta;
        if(snapping && !draggingMask && !(event->modifiers() & Qt::AltModifier)) {
            auto snapped=Arc::snapLayerOrigin(document,dragLayer,point,6/zoom,guidesVisible);
            if(event->modifiers() & Qt::ShiftModifier) {
                if(delta.y()==0) snapped.setY(originalOrigin.y()); else snapped.setX(originalOrigin.x());
            }
            point=snapped;
        }
        auto changed=draggingMask ? Arc::maskTargetLayer(dragOriginal.layers[dragLayer]) : dragOriginal.layers[dragLayer];
        changed.origin={std::clamp(point.x(),-1000000.0,1000000.0),std::clamp(point.y(),-1000000.0,1000000.0)};
        document=dragOriginal;
        if(draggingMask) document.layers[dragLayer].maskPlacement=Arc::placementOf(changed);
        else Arc::transformGroup(document,dragLayer,changed);
        refreshImage();
    }
}
void Canvas::updateDraft(QPointF point, Qt::KeyboardModifiers modifiers) {
    auto delta=point-draftAnchor;
    if(modifiers & Qt::ShiftModifier) {
        if(tool==Tool::ShapeLine) {
            double angle=std::round(std::atan2(delta.y(),delta.x())/(M_PI/4))*(M_PI/4);
            double length=std::hypot(delta.x(),delta.y()); delta={std::cos(angle)*length,std::sin(angle)*length};
        } else {
            double side=std::max(std::abs(delta.x()),std::abs(delta.y()));
            delta={std::copysign(side,delta.x()),std::copysign(side,delta.y())};
        }
    }
    draftStart=(modifiers & Qt::AltModifier) ? draftAnchor-delta : draftAnchor;
    draftEnd=draftAnchor+delta; update();
}
void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if(transformHandle>=0 && event->button()==Qt::LeftButton) {
        mouseMoveEvent(event); if(transformHandle<0) return;
        auto result=document; bool changed=!(document==dragOriginal);
        transformHandle=-1; distortActive=false; document=dragOriginal; refreshImage();
        if(changed) emit layersTransformed(result);
        return;
    }
    if(movingSelection && event->button()==Qt::LeftButton) {
        mouseMoveEvent(event); if(!movingSelection) return;
        bool pixels=movingPixels,duplicate=duplicatePixels; auto mask=previousSelectionMask; auto delta=selectionOffset;
        movingSelection=movingPixels=false; previousSelection.reset(); previousSelectionMask={};
        if(pixels) { document=dragOriginal; refreshImage(); if(!delta.isNull()) emit selectionPixelsMoved(mask,delta,duplicate); }
        return;
    }
    if(dragGuide>=0 && event->button()==Qt::LeftButton) {
        auto index=dragGuide; auto &g=document.guides[index]; auto point=documentPoint(event->position());
        double position=std::clamp(g.axis=="vertical" ? point.x() : point.y(),-1000000.0,1000000.0);
        g.position=position; dragGuide=-1; update();
        if(position!=originalGuidePosition) emit guideMoved(index,position);
        return;
    }
    if(drafting && event->button()==Qt::LeftButton) {
        updateDraft(documentPoint(event->position()),event->modifiers()); drafting=false; update();
        if(tool==Tool::Text) emit textRequested(QRectF(draftStart,draftEnd).normalized(),brush.color);
        else if(QLineF(draftStart,draftEnd).length()>=1) emit shapeCreated(
            tool==Tool::ShapeLine ? "Line" : tool==Tool::ShapeEllipse ? "Ellipse" : "Rectangle",draftStart,draftEnd,brush.color);
        return;
    }
    if (painting && event->button() == Qt::LeftButton) {
        updateStroke(documentPoint(event->position()));
        if (!painting) return;
        int index = dragLayer;
        QImage result = maskTarget ? document.layers[index].mask : document.layers[index].image;
        bool changed = result != (maskTarget ? strokeOriginal.mask : strokeOriginal.image);
        lastBrushPoint=documentPoint(event->position());
        painting = false; dragLayer = -1;
        cloneSample=QImage();
        strokeOriginal = Arc::Layer(); strokePath = QPainterPath();
        if (changed) {
            if (maskTarget) emit maskPainted(index, result);
            else emit painted(index, result);
        }
        return;
    }
    if (selecting && tool != Tool::Polygon && event->button() == Qt::LeftButton) {
        updateSelection(documentPoint(event->position()));
        if(!selecting) return;
        selecting = false;
        if (selection->isEmpty()) { selection.reset(); selectionMask={}; }
        previousSelection.reset();
        update();
        return;
    }
    if (panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning = false; unsetCursor();
    }
    if (dragging && event->button() == Qt::LeftButton) {
        mouseMoveEvent(event);
        auto index=dragLayer; auto point=draggingMask ? Arc::maskTargetLayer(document.layers[index]).origin : document.layers[index].origin;
        bool mask=draggingMask; dragging=false; draggingMask=false; dragLayer=-1;
        if(point!=originalOrigin) {
            if(mask) emit maskMoved(index,document.layers[index].maskPlacement);
            else emit moved(index,point);
        }
    }
}
void Canvas::wheelEvent(QWheelEvent *event) {
    cancelGesture();
    QPointF anchor = documentPoint(event->position());
    zoom = std::clamp(zoom * std::pow(1.0015, event->angleDelta().y()), 0.01, 32.0);
    offset = event->position() - anchor*zoom; emit zoomChanged(zoom); update(); event->accept();
}
void Canvas::cancelGesture() {
    if(transformHandle>=0) { document=dragOriginal; transformHandle=-1; distortActive=false; refreshImage(); }
    if(movingSelection) {
        selection=previousSelection; selectionMask=previousSelectionMask; document=dragOriginal;
        movingSelection=movingPixels=false; previousSelection.reset(); previousSelectionMask={}; refreshImage();
    }
    if(dragGuide>=0) { document.guides[dragGuide].position=originalGuidePosition; dragGuide=-1; update(); }
    if(drafting) { drafting=false; update(); }
    if (painting) {
        document.layers[dragLayer] = strokeOriginal;
        painting = false; dragLayer = -1;
        cloneSample=QImage();
        strokeOriginal = Arc::Layer(); strokePath = QPainterPath();
        refreshImage();
    }
    if (selecting) { selectionMask=previousSelectionMask; selection = previousSelection; selecting = false; previousSelection.reset(); update(); }
    if (dragging) { document=dragOriginal; draggingMask=false; dragging = false; dragLayer = -1; refreshImage(); }
    panning = false; unsetCursor();
}
void Canvas::keyPressEvent(QKeyEvent *event) {
    if ((event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) && tool==Tool::Polygon && selecting) {
        selectionGesture.closeSubpath();
        try { combineSelection(selectionGesture); selecting=false; previousSelection.reset(); }
        catch(const std::exception &error) { cancelGesture(); emit errorOccurred(QString::fromUtf8(error.what())); }
        event->accept();
    }
    else if (event->key() == Qt::Key_Space) { space = true; setCursor(Qt::OpenHandCursor); event->accept(); }
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

Arc::Layer Canvas::activeTransformBox() const {
    if(document.active<0) return {};
    const auto &active=document.layers[document.active];
    if(maskTarget && !active.maskLinked && !active.mask.isNull()) return Arc::maskTargetLayer(active);
    return Arc::transformBox(document,selectedIDs.isEmpty() ? QVector<QUuid>{active.id} : selectedIDs);
}
void Canvas::updateTransform(QPointF point,Qt::KeyboardModifiers modifiers) {
    document=dragOriginal; auto changed=boxOriginal;
    auto start=documentPoint(pressPoint),delta=point-start;
    if(modifiers & Qt::ShiftModifier && (distortActive || transformHandle==9)) {
        if(std::abs(delta.x())>std::abs(delta.y())) delta.setY(0); else delta.setX(0);
        point=start+delta;
    }
    if(distortActive) {
        transformQuad=Arc::layerCorners(boxOriginal); transformQuad[transformHandle/2]+=delta;
        Arc::distortLayers(document,transformIDs,Arc::layerCorners(boxOriginal),transformQuad);
    } else {
        if(transformHandle==9) {
            changed.origin+=delta;
            if(snapping && !(modifiers & Qt::AltModifier)) changed.origin=Arc::snapLayerOrigin(dragOriginal,dragOriginal.active,changed.origin,6/zoom,guidesVisible);
        } else if(transformHandle==8) {
            auto center=boxOriginal.origin+QPointF(boxOriginal.size.width()/2,boxOriginal.size.height()/2);
            auto a=start-center,b=point-center;
            changed.rotation+=std::atan2(a.x()*b.y()-a.y()*b.x(),QPointF::dotProduct(a,b))*180/M_PI;
            if(modifiers & Qt::ShiftModifier) changed.rotation=std::round(changed.rotation/15)*15;
        } else {
            auto corners=Arc::layerCorners(boxOriginal);
            auto handlePoint=transformHandle%2 ? (corners[transformHandle/2]+corners[(transformHandle/2+1)%4])/2 : corners[transformHandle/2];
            auto local=boxOriginal.transform().inverted().map(handlePoint+delta); double width=boxOriginal.size.width(),height=boxOriginal.size.height();
            double left=0,right=width,top=0,bottom=height;
            bool changesLeft=transformHandle==0 || transformHandle==6 || transformHandle==7;
            bool changesRight=transformHandle==2 || transformHandle==3 || transformHandle==4;
            bool changesTop=transformHandle<=2,changesBottom=transformHandle>=4 && transformHandle<=6;
            if(changesLeft) left=std::min(local.x(),right-1);
            if(changesRight) right=std::max(local.x(),left+1);
            if(changesTop) top=std::min(local.y(),bottom-1);
            if(changesBottom) bottom=std::max(local.y(),top+1);
            if(modifiers & Qt::ShiftModifier && transformHandle%2==0) {
                double scale=std::max((right-left)/width,(bottom-top)/height);
                if(changesLeft) left=right-width*scale; else right=left+width*scale;
                if(changesTop) top=bottom-height*scale; else bottom=top+height*scale;
            }
            if(modifiers & Qt::AltModifier) {
                if(changesLeft) right=width-left;
                if(changesRight) left=width-right;
                if(changesTop) bottom=height-top;
                if(changesBottom) top=height-bottom;
            }
            changed.size={std::max(1.0,right-left),std::max(1.0,bottom-top)};
            auto center=boxOriginal.transform().map(QPointF((left+right)/2,(top+bottom)/2));
            changed.origin=center-QPointF(changed.size.width()/2,changed.size.height()/2);
        }
        if(transformMask) document.layers[document.active].maskPlacement=Arc::placementOf(changed);
        else Arc::transformLayers(document,transformIDs,boxOriginal,changed);
    }
    Arc::validate(document); refreshImage();
}
