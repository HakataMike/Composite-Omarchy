#include "masks.h"
#include "geometry.h"
#include <QJsonArray>
#include <cmath>
#include <stdexcept>

namespace Arc {
namespace {
void require(bool ok) { if(!ok) throw std::runtime_error("Invalid mask placement."); }
Layer placement(const QJsonObject &value) {
    const QStringList keys{"origin","size","rotation","flipX","flipY","sampling"};
    for(auto it=value.begin();it!=value.end();++it) require(keys.contains(it.key()));
    auto pair=[&](QString key) {
        auto array=value[key].toArray(); require(value[key].isArray() && array.size()==2);
        require(array[0].isDouble() && array[1].isDouble()); return QPointF(array[0].toDouble(),array[1].toDouble());
    };
    Layer layer; layer.origin=pair("origin"); auto size=pair("size"); layer.size={size.x(),size.y()};
    require(value["rotation"].isDouble() && value["flipX"].isBool() && value["flipY"].isBool());
    layer.rotation=value["rotation"].toDouble(); layer.flipX=value["flipX"].toBool(); layer.flipY=value["flipY"].toBool();
    layer.sampling=value["sampling"].toString();
    require(std::isfinite(layer.origin.x()) && std::isfinite(layer.origin.y()) && std::abs(layer.origin.x())<=1000000
        && std::abs(layer.origin.y())<=1000000 && std::isfinite(layer.rotation)
        && std::isfinite(size.x()) && std::isfinite(size.y()) && size.x()>=1 && size.x()<=300000 && size.y()>=1 && size.y()<=300000
        && QStringList{"Nearest","Smooth","High quality"}.contains(layer.sampling));
    return layer;
}
}
QJsonObject placementOf(const Layer &l) {
    return {{"origin",QJsonArray{l.origin.x(),l.origin.y()}},{"size",QJsonArray{l.size.width(),l.size.height()}},
        {"rotation",l.rotation},{"flipX",l.flipX},{"flipY",l.flipY},{"sampling",l.sampling}};
}
Layer maskTargetLayer(const Layer &l) {
    auto target=l;
    if(!l.maskPlacement.isEmpty()) {
        auto p=placement(l.maskPlacement);
        target.origin=p.origin; target.size=p.size; target.rotation=p.rotation;
        target.flipX=p.flipX; target.flipY=p.flipY; target.sampling=p.sampling;
    }
    target.adjustment={}; target.effects={}; target.image=l.mask; target.shape={}; target.text={}; target.isGroup=false;
    return target;
}
void validateMaskPlacement(const Layer &l) { if(!l.maskPlacement.isEmpty()) placement(l.maskPlacement); }
QImage rasterMask(const Layer &l, QSize size) {
    if(l.mask.isNull() || l.maskPlacement.isEmpty() || l.mask.size()==QSize(1,1)) return l.mask;
    if(size.width()<1 || size.height()<1 || size.width()>30000 || size.height()>30000 || qint64(size.width())*size.height()>MaxPixels)
        throw std::runtime_error("Mask raster exceeds supported dimensions.");
    QImage result(size,QImage::Format_Grayscale8);
    if(result.isNull()) throw std::runtime_error("Insufficient memory to position mask.");
    auto small=l.mask.scaled(96,96,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    if(small.isNull()) throw std::runtime_error("Insufficient memory for mask preview.");
    qint64 total=0,count=0;
    for(int y=0;y<small.height();++y) for(int x=0;x<small.width();++x)
        if(x==0 || y==0 || x==small.width()-1 || y==small.height()-1) { total+=small.constScanLine(y)[x]; ++count; }
    result.fill(total*2>=count*255 ? Qt::white : Qt::black);
    auto target=placement(l.maskPlacement);
    auto sourceToDocument=l.transform(); sourceToDocument.scale(l.size.width()/size.width(),l.size.height()/size.height());
    QPainter painter(&result); painter.setTransform(target.transform()*sourceToDocument.inverted());
    painter.setRenderHint(QPainter::SmoothPixmapTransform,target.sampling!="Nearest");
    painter.drawImage(QRectF(QPointF(),target.size),l.mask);
    return result;
}
void followMask(const Layer &before, Layer &after) {
    if(before.mask.isNull() || before.mask.size()==QSize(1,1) || placementOf(before)==placementOf(after)) return;
    if(!after.maskLinked) {
        if(after.maskPlacement.isEmpty()) after.maskPlacement=placementOf(before);
        return;
    }
    if(before.maskPlacement.isEmpty()) return;
    auto target=placement(before.maskPlacement);
    QTransform scale; scale.scale(after.size.width()/before.size.width(),after.size.height()/before.size.height());
    auto mapping=target.transform()*before.transform().inverted()*scale*after.transform();
    target=placedLayer(target,mapping);
    after.maskPlacement=placementOf(target);
}
void offsetMask(Layer &l, QPointF delta) {
    if(l.maskPlacement.isEmpty()) return;
    auto target=placement(l.maskPlacement); target.origin+=delta; l.maskPlacement=placementOf(target);
}
void flipMask(Layer &l, QSize canvas, bool horizontal) {
    if(l.maskPlacement.isEmpty()) return;
    auto target=placement(l.maskPlacement);
    if(horizontal) { target.origin.setX(canvas.width()-target.origin.x()-target.size.width()); target.flipX=!target.flipX; }
    else { target.origin.setY(canvas.height()-target.origin.y()-target.size.height()); target.flipY=!target.flipY; }
    target.rotation=-target.rotation; l.maskPlacement=placementOf(target);
}
}
