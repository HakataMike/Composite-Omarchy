#include "retouch.h"
#include "masks.h"
#include "operations.h"
#include <QLinearGradient>
#include <QRadialGradient>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>
extern "C" {
#include "../../Compositor/Rendering/HealPixels.h"
}
namespace Arc {
namespace {
QTransform mapping(const Layer &layer,QSize pixels) {
    auto t=layer.transform(); t.scale(layer.size.width()/pixels.width(),layer.size.height()/pixels.height()); return t.inverted();
}
QImage coverage(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection,QSize size) {
    auto target=layer; target.maskPlacement={}; target.mask=QImage(size,QImage::Format_Grayscale8);
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
QImage gradientStroke(const Layer &layer,QPointF start,QPointF end,const Brush &brush,QColor background,QRectF clip,const QPainterPath &selection,bool mask,const GradientOptions &options) {
    QImage image=mask ? layer.mask : layer.image;
    if(mask && image.size()==QSize(1,1)) image=image.scaled(maskEditingSize(layer));
    if(start==end) return image;
    if(image.isNull()) throw std::runtime_error("Select a raster layer or mask for the gradient.");
    QColor foreground=brush.color;
    if(mask) { int a=qGray(foreground.rgb()),b=qGray(background.rgb()); foreground=QColor(a,a,a); background=QColor(b,b,b); }
    if(options.transparent) { background=foreground; background.setAlpha(0); }
    if(options.reversed) std::swap(foreground,background);
    image=image.copy(); if(image.isNull()) throw std::runtime_error("Insufficient memory for gradient.");
    QPainter p(&image); p.setTransform(mapping(mask ? maskTargetLayer(layer) : layer,image.size())); p.setClipRect(clip);
    if(!selection.isEmpty()) p.setClipPath(selection,Qt::IntersectClip);
    QGradient gradient=options.radial ? QGradient(QRadialGradient(start,QLineF(start,end).length())) : QGradient(QLinearGradient(start,end));
    gradient.setColorAt(0,foreground); gradient.setColorAt(1,background);
    p.setOpacity(brush.opacity); p.fillRect(clip,gradient); return image;
}
QImage healStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection,int mode) {
    if(mode<0 || mode>2) throw std::runtime_error("Invalid healing mode.");
    auto pixels=layer.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if(pixels.isNull()) throw std::runtime_error("Select an image to heal.");
    auto mask=coverage(layer,path,brush,clip,selection,pixels.size());
    QByteArray tight(qsizetype(pixels.width())*pixels.height(),0);
    for(int y=0;y<pixels.height();++y) std::memcpy(tight.data()+y*pixels.width(),mask.constScanLine(y),pixels.width());
    if(spot_heal(pixels.bits(),reinterpret_cast<const uint8_t *>(tight.constData()),pixels.width(),pixels.height(),pixels.bytesPerLine(),1,mode,1)!=0)
        throw std::runtime_error("Insufficient memory for healing.");
    return pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
QImage blurStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QRectF clip,const QPainterPath &selection,bool mask) {
    QImage source=mask ? layer.mask : layer.image;
    if(source.isNull()) throw std::runtime_error("Select an image or mask to blur.");
    auto coverageImage=coverage(mask ? maskTargetLayer(layer) : layer,path,brush,clip,selection,source.size());
    return mix(source,blurImage(source,std::clamp(int(brush.diameter/10),1,50)),coverageImage);
}
QImage warpStroke(const Layer &layer,const QPainterPath &path,const Brush &brush,QSize canvas,const QPainterPath &selection,bool smudge) {
    if(layer.image.isNull()) throw std::runtime_error("Select an image to smudge or liquify.");
    if(path.elementCount()<2 || brush.opacity==0) return layer.image;
    if(canvas.width()<1 || canvas.height()<1 || qint64(canvas.width())*canvas.height()>MaxPixels
        || !std::isfinite(brush.diameter) || brush.diameter<1 || brush.diameter>2000
        || !std::isfinite(brush.hardness) || brush.hardness<0 || brush.hardness>1
        || !std::isfinite(brush.opacity) || brush.opacity<0 || brush.opacity>1)
        throw std::runtime_error("Invalid warp stroke dimensions.");
    // Work in document pixels so a round brush remains round on scaled/rotated
    // layers, then replace only the stroke footprint in the original source.
    QImage working(canvas,QImage::Format_RGBA8888_Premultiplied);
    if(working.isNull()) throw std::runtime_error("Insufficient memory for warp stroke.");
    working.fill(Qt::transparent);
    { QPainter p(&working); p.setTransform(layer.transform()); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(QRectF(QPointF(),layer.size),layer.image); }
    double diameter=std::max(2.0,brush.diameter),hardness=std::clamp(brush.hardness,0.0,0.98),strength=std::clamp(brush.opacity,0.01,1.0);
    int radius=int(std::ceil(diameter/2)),side=2*radius+1;
    auto weight=[&](int x,int y) { double u=std::hypot(x,y)/(diameter/2); if(u>=1) return 0.0; if(u<=hardness) return 1.0; double t=(1-u)/(1-hardness); return t*t*(3-2*t); };
    QPointF last(path.elementAt(0).x,path.elementAt(0).y); QVector<float> carried;
    if(smudge) {
        carried.fill(0,side*side*4); int cx=qRound(last.x()),cy=qRound(last.y());
        for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) if(working.rect().contains(cx+x,cy+y))
            for(int c=0;c<4;++c) carried[((y+radius)*side+x+radius)*4+c]=working.constScanLine(cy+y)[(cx+x)*4+c];
    }
    for(int i=1;i<path.elementCount();++i) {
        QPointF end(path.elementAt(i).x,path.elementAt(i).y); double distance=QLineF(last,end).length();
        double spacing=std::max(1.0,diameter*(smudge ? 0.08 : 0.025)); if(distance<spacing) continue;
        if(!std::isfinite(distance) || distance/spacing>100000) throw std::runtime_error("Warp stroke is too long.");
        int steps=int(std::ceil(distance/spacing)); QPointF previous=last;
        for(int step=1;step<=steps;++step) {
            auto next=last+(end-last)*(double(step)/steps),move=(next-previous)*strength;
            int cx=qRound(next.x()),cy=qRound(next.y());
            int margin=int(std::ceil(std::max(std::abs(move.x()),std::abs(move.y()))))+2;
            QRect patch=QRect(cx-radius-margin,cy-radius-margin,2*(radius+margin)+1,2*(radius+margin)+1).intersected(working.rect());
            QImage scratch; if(!smudge && !patch.isEmpty()) scratch=working.copy(patch);
            if(!smudge && !patch.isEmpty() && scratch.isNull()) throw std::runtime_error("Insufficient memory for liquify dab.");
            for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) {
                if(!working.rect().contains(cx+x,cy+y)) continue;
                double w=weight(x,y); if(w==0) continue; auto *pixel=working.scanLine(cy+y)+(cx+x)*4;
                if(smudge) {
                    int index=((y+radius)*side+x+radius)*4;
                    for(int c=0;c<4;++c) { double value=pixel[c]+(carried[index+c]-pixel[c])*w; pixel[c]=qRound(value); carried[index+c]=value+(carried[index+c]-value)*strength; }
                } else {
                    double sx=std::clamp(cx+x-patch.x()-move.x()*w,0.0,double(patch.width()-1));
                    double sy=std::clamp(cy+y-patch.y()-move.y()*w,0.0,double(patch.height()-1));
                    int ix=int(sx),iy=int(sy),jx=std::min(ix+1,patch.width()-1),jy=std::min(iy+1,patch.height()-1);
                    for(int c=0;c<4;++c) {
                        double top=scratch.constScanLine(iy)[ix*4+c]*(1-(sx-ix))+scratch.constScanLine(iy)[jx*4+c]*(sx-ix);
                        double bottom=scratch.constScanLine(jy)[ix*4+c]*(1-(sx-ix))+scratch.constScanLine(jy)[jx*4+c]*(sx-ix);
                        pixel[c]=qRound(top*(1-(sy-iy))+bottom*(sy-iy));
                    }
                }
            }
            previous=next;
        }
        last=end;
    }
    QImage changed(layer.image.size(),QImage::Format_ARGB32_Premultiplied);
    if(changed.isNull()) throw std::runtime_error("Insufficient memory for warp result.");
    changed.fill(Qt::transparent);
    { QPainter p(&changed); p.setTransform(mapping(layer,changed.size())); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(QPoint(),working); }
    Brush footprint=brush; footprint.diameter=std::min(2000.0,diameter+4); footprint.hardness=1; footprint.opacity=1;
    return mix(layer.image,changed,coverage(layer,path,footprint,QRectF(QPointF(),canvas),selection,layer.image.size()));
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
