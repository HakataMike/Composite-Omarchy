#include "geometry.h"
#include "hierarchy.h"
#include "masks.h"
#include "styles.h"
#include <QSet>
#include <QHash>
#include <QPainterPath>
#include <cmath>
#include <stdexcept>
namespace Arc {
QPolygonF layerCorners(const Layer &l) {
    return l.transform().map(QPolygonF{QPointF(0,0),QPointF(l.size.width(),0),QPointF(l.size.width(),l.size.height()),QPointF(0,l.size.height())});
}
Layer placedLayer(const Layer &original,const QTransform &map) {
    auto result=original; auto zero=map.map(QPointF());
    auto x=map.map(QPointF(original.size.width(),0))-zero,y=map.map(QPointF(0,original.size.height()))-zero;
    double sign=original.flipX ? -1 : 1,angle=std::atan2(x.y()*sign,x.x()*sign);
    // The native format stores rotated rectangles. As in LayerTransform.placing,
    // project a sheared vertical edge onto the perpendicular instead of resampling.
    double along=-y.x()*std::sin(angle)+y.y()*std::cos(angle);
    double width=std::hypot(x.x(),x.y()),height=std::abs(along);
    if(width<1 || height<1 || !std::isfinite(width) || !std::isfinite(height)) throw std::runtime_error("Transform would collapse the layer.");
    auto center=map.map(QPointF(original.size.width()/2,original.size.height()/2));
    result.size={width,height}; result.origin=center-QPointF(width/2,height/2);
    double degrees=angle*180/M_PI; result.rotation=degrees+std::round((original.rotation-degrees)/360)*360;
    result.flipY=along<0; return result;
}
QVector<QUuid> selectedRoots(const Document &d,const QVector<QUuid> &selected) {
    QSet<QUuid> chosen(selected.begin(),selected.end()); QHash<QUuid,QUuid> parents;
    for(const auto &l:d.layers) parents[l.id]=l.parentID;
    QVector<QUuid> roots;
    for(const auto &l:d.layers) if(chosen.contains(l.id)) {
        bool nested=false; for(auto parent=l.parentID; !parent.isNull(); parent=parents.value(parent)) if(chosen.contains(parent)) { nested=true; break; }
        if(!nested) roots.append(l.id);
    }
    return roots;
}
Layer transformBox(const Document &d,const QVector<QUuid> &selected) {
    auto roots=selectedRoots(d,selected); if(roots.isEmpty()) return {};
    QRectF bounds; Layer only;
    for(const auto &l:d.layers) if(roots.contains(l.id)) { only=l; bounds=bounds.united(layerCorners(l).boundingRect()); }
    if(roots.size()==1) return only;
    Layer box; box.origin=bounds.topLeft(); box.size=bounds.size(); return box;
}
void transformLayers(Document &d,const QVector<QUuid> &selected,const Layer &before,const Layer &after) {
    auto roots=selectedRoots(d,selected);
    QTransform scale; scale.scale(after.size.width()/before.size.width(),after.size.height()/before.size.height());
    auto mapping=before.transform().inverted()*scale*after.transform();
    for(int i=0;i<d.layers.size();++i) if(roots.contains(d.layers[i].id)) {
        auto changed=placedLayer(d.layers[i],d.layers[i].transform()*mapping);
        if(!changed.shape.isEmpty()) changed.image=shapeImage(changed.shape,changed.size);
        transformGroup(d,i,changed);
    }
    validate(d);
}
namespace {
double area(QPointF a,QPointF b,QPointF c) {
    auto u=b-a,v=c-a; return u.x()*v.y()-u.y()*v.x();
}
void checkQuad(const QPolygonF &quad) {
    if(quad.size()!=4) throw std::runtime_error("Distortion needs four corners.");
    for(auto p:quad) if(!std::isfinite(p.x()) || !std::isfinite(p.y()) || std::abs(p.x())>1000000 || std::abs(p.y())>1000000)
        throw std::runtime_error("Distortion exceeds supported coordinates.");
    if(std::abs(area(quad[0],quad[1],quad[2]))<=0.01 || std::abs(area(quad[0],quad[2],quad[3]))<=0.01)
        throw std::runtime_error("Distortion would collapse a triangle.");
}
bool convex(const QPolygonF &quad) {
    double sign=area(quad[0],quad[1],quad[2]);
    for(int i=1;i<4;++i) if(area(quad[i],quad[(i+1)%4],quad[(i+2)%4])*sign<=0) return false;
    return true;
}
QRect warpBounds(const QPolygonF &corners) {
    checkQuad(corners); auto extent=corners.boundingRect();
    if(extent.width()<1 || extent.height()<1 || extent.width()>30000 || extent.height()>30000)
        throw std::runtime_error("Distortion exceeds supported bounds.");
    auto bounds=extent.toAlignedRect();
    if(qint64(bounds.width())*bounds.height()>MaxPixels) throw std::runtime_error("Distortion exceeds 100 megapixels.");
    return bounds;
}
QPolygonF mappedCorners(const Layer &layer,const QTransform &mapping) {
    QPolygonF result;
    // Qt clamps negative projective denominators for drawing. A folded quad
    // deliberately crosses that plane; its corner coordinates still need the
    // signed homogeneous division before the two affine halves are drawn.
    for(auto p:layerCorners(layer)) {
        double w=mapping.m13()*p.x()+mapping.m23()*p.y()+mapping.m33();
        if(std::abs(w)<1e-12) throw std::runtime_error("Distortion crosses an infinite corner.");
        result.append(QPointF((mapping.m11()*p.x()+mapping.m21()*p.y()+mapping.m31())/w,
                             (mapping.m12()*p.x()+mapping.m22()*p.y()+mapping.m32())/w));
    }
    return result;
}
QTransform triangleBasis(QPointF a,QPointF b,QPointF c) {
    auto u=b-a,v=c-a; return QTransform(u.x(),u.y(),v.x(),v.y(),a.x(),a.y());
}
QImage warped(const QImage &source,const Layer &placement,const QPolygonF &corners,QRect &bounds,bool mask) {
    bounds=warpBounds(corners);
    if(mask && source.size()==QSize(1,1)) return source;
    QImage result(bounds.size(),mask ? QImage::Format_Grayscale8 : QImage::Format_ARGB32_Premultiplied);
    if(result.isNull()) throw std::runtime_error("Insufficient memory for distortion.");
    result.fill(mask ? Qt::black : Qt::transparent);
    QPainter p(&result); p.translate(-bounds.topLeft());
    p.setRenderHint(QPainter::SmoothPixmapTransform,placement.sampling!="Nearest");
    QPolygonF rectangle{QPointF(0,0),QPointF(placement.size.width(),0),QPointF(placement.size.width(),placement.size.height()),QPointF(0,placement.size.height())};
    if(convex(corners)) {
        QTransform transform;
        if(!QTransform::quadToQuad(rectangle,corners,transform)) throw std::runtime_error("Cannot map distortion corners.");
        p.setTransform(transform,true); p.drawImage(QRectF(QPointF(),placement.size),source);
    } else {
        // Like the native editor, draw the two halves in order with a hard shared
        // diagonal. Perspective alone cannot represent a folded quadrilateral.
        for(int half=0;half<2;++half) {
            int b=half==0 ? 1 : 2,c=half==0 ? 2 : 3;
            auto transform=triangleBasis(rectangle[0],rectangle[b],rectangle[c]).inverted()*triangleBasis(corners[0],corners[b],corners[c]);
            QPainterPath clip; clip.addPolygon(QPolygonF{corners[0],corners[b],corners[c]}); clip.closeSubpath();
            p.save(); p.setClipPath(clip); p.setTransform(transform,true); p.drawImage(QRectF(QPointF(),placement.size),source); p.restore();
        }
    }
    return result;
}
}
void distortLayers(Document &d,const QVector<QUuid> &selected,const QPolygonF &before,const QPolygonF &after) {
    checkQuad(before); checkQuad(after); QTransform mapping;
    if(!QTransform::quadToQuad(before,after,mapping)) throw std::runtime_error("Cannot map distortion corners.");
    QSet<QUuid> chosen;
    for(auto id:selectedRoots(d,selected)) { chosen.insert(id); for(int i:descendants(d,id)) chosen.insert(d.layers[i].id); }
    auto changed=d;
    for(int i=0;i<d.layers.size();++i) if(chosen.contains(d.layers[i].id)) {
        auto &l=changed.layers[i]; const auto &old=d.layers[i]; QRect bounds;
        auto corners=mappedCorners(old,mapping);
        if(!l.image.isNull()) l.image=warped(l.image,old,corners,bounds,false);
        else bounds=warpBounds(corners);
        if(!l.mask.isNull()) {
            if(l.maskLinked) {
                auto placement=maskTargetLayer(old); QRect maskBounds;
                l.mask=warped(l.mask,placement,mappedCorners(placement,mapping),maskBounds,true);
                placement.origin=maskBounds.topLeft(); placement.size=maskBounds.size(); placement.rotation=0; placement.flipX=placement.flipY=false;
                l.maskPlacement=placementOf(placement);
            } else if(l.maskPlacement.isEmpty()) l.maskPlacement=placementOf(old);
        }
        l.origin=bounds.topLeft(); l.size=bounds.size(); l.rotation=0; l.flipX=l.flipY=false; rasterize(l);
    }
    validate(changed); d=changed;
}
}
