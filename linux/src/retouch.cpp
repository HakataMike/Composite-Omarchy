#include "retouch.h"
#include "operations.h"
#include <QLinearGradient>
#include <stdexcept>
#include <cstring>
#include <algorithm>
extern "C" {
#include "../../Compositor/Rendering/HealPixels.h"
}
namespace Arc {
namespace {
QTransform mapping(const Layer &layer,QSize pixels) {
    auto t=layer.transform(); t.scale(layer.size.width()/pixels.width(),layer.size.height()/pixels.height()); return t.inverted();
}
QImage coverage(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection,QSize size) {
    auto target=layer; target.mask=QImage(size,QImage::Format_Grayscale8);
    if(target.mask.isNull()) throw std::runtime_error("Insufficient memory for retouching coverage.");
    target.mask.fill(Qt::black); target.image=QImage();
    Brush white=brush; white.color=Qt::white; white.eraser=false;
    return paintMaskStroke(target,path,white,clip,selection);
}
QImage mix(const QImage &source,const QImage &changed,const QImage &coverage) {
    bool gray=source.format()==QImage::Format_Grayscale8;
    auto original=source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    auto result=changed.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if(original.isNull() || result.isNull()) throw std::runtime_error("Insufficient memory for retouching.");
    for(int y=0;y<result.height();++y) {
        auto *row=reinterpret_cast<QRgb *>(result.scanLine(y));
        const auto *before=reinterpret_cast<const QRgb *>(original.constScanLine(y));
        const auto *mask=coverage.constScanLine(y);
        for(int x=0;x<result.width();++x) {
            int c=mask[x],n=255-c; QRgb a=before[x],b=row[x];
            row[x]=qRgba((qRed(a)*n+qRed(b)*c+127)/255,(qGreen(a)*n+qGreen(b)*c+127)/255,
                (qBlue(a)*n+qBlue(b)*c+127)/255,(qAlpha(a)*n+qAlpha(b)*c+127)/255);
        }
    }
    return gray ? result.convertToFormat(QImage::Format_Grayscale8) : result;
}
}
QImage gradientStroke(const Layer &layer,QPointF start,QPointF end,const Brush &brush,QColor background,QRectF clip,const QPainterPath &selection,bool mask) {
    QImage image=mask ? layer.mask : layer.image;
    if(mask && image.size()==QSize(1,1)) image=image.scaled(layer.image.size());
    if(start==end) return image;
    if(image.isNull()) throw std::runtime_error("Select a raster layer or mask for the gradient.");
    QColor foreground=brush.color;
    if(mask) { int a=qGray(foreground.rgb()),b=qGray(background.rgb()); foreground=QColor(a,a,a); background=QColor(b,b,b); }
    image=image.copy(); if(image.isNull()) throw std::runtime_error("Insufficient memory for gradient.");
    QPainter p(&image); p.setTransform(mapping(layer,image.size())); p.setClipRect(clip);
    if(!selection.isEmpty()) p.setClipPath(selection,Qt::IntersectClip);
    QLinearGradient gradient(start,end); gradient.setColorAt(0,foreground); gradient.setColorAt(1,background);
    p.setOpacity(brush.opacity); p.fillRect(clip,gradient); return image;
}
QImage healStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection) {
    auto pixels=layer.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if(pixels.isNull()) throw std::runtime_error("Select an image to heal.");
    auto mask=coverage(layer,path,brush,clip,selection,pixels.size());
    QByteArray tight(qsizetype(pixels.width())*pixels.height(),0);
    for(int y=0;y<pixels.height();++y) std::memcpy(tight.data()+y*pixels.width(),mask.constScanLine(y),pixels.width());
    if(spot_heal(pixels.bits(),reinterpret_cast<const uint8_t *>(tight.constData()),pixels.width(),pixels.height(),pixels.bytesPerLine(),1,0,1)!=0)
        throw std::runtime_error("Insufficient memory for healing.");
    return pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
QImage blurStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection,bool mask) {
    QImage source=mask ? layer.mask : layer.image;
    if(source.isNull()) throw std::runtime_error("Select an image or mask to blur.");
    auto coverageImage=coverage(layer,path,brush,clip,selection,source.size());
    return mix(source,blurImage(source,std::clamp(int(brush.diameter/10),1,50)),coverageImage);
}
QImage cloneStroke(const Layer &layer,const QImage &sample,QPointF offset,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection) {
    if(layer.image.isNull() || sample.isNull()) throw std::runtime_error("Choose a clone source with Alt-click first.");
    auto mask=coverage(layer,path,brush,clip,selection,layer.image.size());
    QImage cloned(layer.image.size(),QImage::Format_ARGB32_Premultiplied);
    if(cloned.isNull()) throw std::runtime_error("Insufficient memory for clone stamp.");
    cloned.fill(Qt::transparent);
    { QPainter p(&cloned); p.setTransform(mapping(layer,cloned.size())); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(-offset,sample); }
    for(int y=0;y<cloned.height();++y) {
        auto *row=reinterpret_cast<QRgb *>(cloned.scanLine(y)); const auto *c=mask.constScanLine(y);
        for(int x=0;x<cloned.width();++x) { auto v=row[x]; row[x]=qRgba((qRed(v)*c[x]+127)/255,(qGreen(v)*c[x]+127)/255,(qBlue(v)*c[x]+127)/255,(qAlpha(v)*c[x]+127)/255); }
    }
    auto result=layer.image.copy(); if(result.isNull()) throw std::runtime_error("Insufficient memory for clone stamp.");
    { QPainter p(&result); p.drawImage(QPoint(),cloned); }
    return result;
}
}
