#include "spatial_filters.h"
#include "masks.h"
#include "styles.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Arc {
namespace {
QImage surface(QSize size) {
    if(size.width()<1 || size.height()<1 || size.width()>30000 || size.height()>30000 || qint64(size.width())*size.height()>MaxPixels)
        throw std::runtime_error("Blur exceeds supported raster dimensions.");
    QImage result(size,QImage::Format_ARGB32_Premultiplied); if(result.isNull()) throw std::runtime_error("Insufficient memory for blur.");
    result.fill(Qt::transparent); return result;
}
}
QImage motionBlur(const QImage &source,double distance,double angle) {
    if(!std::isfinite(distance) || distance<1 || distance>2000 || !std::isfinite(angle) || angle<-90 || angle>90)
        throw std::runtime_error("Invalid motion blur settings.");
    if(distance<=1) return source;
    QTransform rotate; rotate.rotate(angle);
    auto bounds=rotate.mapRect(QRectF(source.rect())).toAlignedRect();
    auto rotated=surface(bounds.size());
    { QPainter p(&rotated); p.translate(-bounds.topLeft()); p.setTransform(rotate,true); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(QPoint(),source); }
    auto streak=surface(rotated.size()); int radius=std::max(1,qRound((distance-1)/2)),count=2*radius+1;
    for(int y=0;y<rotated.height();++y) {
        const auto *row=reinterpret_cast<const QRgb *>(rotated.constScanLine(y)); auto *out=reinterpret_cast<QRgb *>(streak.scanLine(y));
        auto get=[&](int x) { return row[std::clamp(x,0,rotated.width()-1)]; };
        qint64 r=0,g=0,b=0,a=0;
        auto add=[&](QRgb p,int sign) { r+=sign*qRed(p); g+=sign*qGreen(p); b+=sign*qBlue(p); a+=sign*qAlpha(p); };
        for(int i=-radius;i<=radius;++i) add(get(i),1);
        for(int x=0;x<rotated.width();++x) { out[x]=qRgba(r/count,g/count,b/count,a/count); add(get(x-radius),-1); add(get(x+radius+1),1); }
    }
    auto result=surface(source.size());
    { QPainter p(&result); p.setTransform(rotate.inverted()); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(bounds.topLeft(),streak); }
    return source.format()==QImage::Format_Grayscale8 ? result.convertToFormat(source.format()) : result;
}
Layer expandedBlur(const Layer &layer,const FilterSettings &settings) {
    if(layer.image.isNull() || (settings.kind!="Gaussian Blur" && settings.kind!="Motion Blur")) throw std::runtime_error("Select a raster layer to blur.");
    double amount=settings.values[settings.kind=="Gaussian Blur" ? "radius" : "distance"].toDouble();
    if(!std::isfinite(amount) || amount<0 || amount>2000) throw std::runtime_error("Invalid blur extent.");
    if((settings.kind=="Gaussian Blur" && amount==0) || (settings.kind=="Motion Blur" && amount==1)) return layer;
    int pad=int(std::ceil(settings.kind=="Gaussian Blur" ? amount*3 : amount/2))+2;
    auto padded=surface(layer.image.size()+QSize(pad*2,pad*2));
    { QPainter p(&padded); p.drawImage(QPoint(pad,pad),layer.image); }
    auto result=layer; result.image=applyFilter(padded,settings); rasterize(result);
    double sx=layer.size.width()/layer.image.width(),sy=layer.size.height()/layer.image.height();
    result.origin-=QPointF(pad*sx,pad*sy); result.size=QSizeF(result.image.width()*sx,result.image.height()*sy);
    // Keep a mask's placement fixed while the raster's bounds grow around it.
    if(!layer.mask.isNull()) result.maskPlacement=placementOf(maskTargetLayer(layer));
    return result;
}
}
