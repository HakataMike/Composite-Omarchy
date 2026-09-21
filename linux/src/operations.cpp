#include "operations.h"
#include "styles.h"
#include "clipping.h"
#include "masks.h"
#include "effects.h"
#include <QColorSpace>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace Arc {
namespace {
void checkSize(QSize size) {
    Document probe; probe.size = size; validate(probe);
}
QImage transformedImage(const QImage &source, QSizeF localSize, const QTransform &mapping, QRect bounds, bool mask) {
    QImage result(bounds.size(),QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) throw std::runtime_error("Insufficient memory to resize layer.");
    result.fill(mask ? Qt::white : Qt::transparent);
    {
        QPainter p(&result); p.translate(-bounds.topLeft()); p.setTransform(mapping,true);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(QPointF(),localSize),source);
    }
    if (mask) return result.convertToFormat(QImage::Format_Grayscale8);
    result.setColorSpace(QColorSpace::SRgb); return result;
}
}
void crop(Document &d, QRect rectangle) {
    if (rectangle.isEmpty()) throw std::runtime_error("Select a crop rectangle first.");
    checkSize(rectangle.size());
    for (auto &l : d.layers) { l.origin -= rectangle.topLeft(); offsetMask(l,-rectangle.topLeft()); }
    for (auto &g : d.guides) g.position -= g.axis == "vertical" ? rectangle.x() : rectangle.y();
    d.size = rectangle.size(); validate(d);
}
void resizeCanvas(Document &d, QSize size, bool centered) {
    checkSize(size);
    if (centered) {
        QPointF delta((size.width()-d.size.width())/2.0,(size.height()-d.size.height())/2.0);
        for (auto &l : d.layers) { l.origin += delta; offsetMask(l,delta); }
        for (auto &g : d.guides) g.position += g.axis == "vertical" ? delta.x() : delta.y();
    }
    d.size = size; validate(d);
}
void resizeImage(Document &d, QSize size) {
    checkSize(size);
    double sx = double(size.width())/d.size.width(), sy = double(size.height())/d.size.height();
    QTransform scale; scale.scale(sx,sy);
    qint64 pixels = 0;
    for (auto &l : d.layers) {
        QTransform mapping = l.transform() * scale;
        QRectF extent=mapping.mapRect(QRectF(QPointF(),l.size));
        if(extent.width()>30000 || extent.height()>30000 || std::abs(extent.x())>1000000 || std::abs(extent.y())>1000000)
            throw std::runtime_error("A resized layer exceeds supported dimensions or position.");
        QRect bounds = extent.toAlignedRect();
        checkSize(bounds.size());
        pixels += qint64(bounds.width())*bounds.height();
        if (pixels > MaxPixels) throw std::runtime_error("Resized layers exceed 100 megapixels.");
        auto mask=l.mask.isNull() ? l.mask : rasterMask(l,maskEditingSize(l));
        l.maskPlacement={};
        if (!l.image.isNull()) l.image = transformedImage(l.image,l.size,mapping,bounds,false);
        rasterize(l);
        if (!l.mask.isNull()) l.mask = transformedImage(mask,l.size,mapping,bounds,true);
        l.origin = bounds.topLeft(); l.size = bounds.size(); l.rotation = 0; l.flipX = l.flipY = false;
    }
    for (auto &g : d.guides) g.position *= g.axis == "vertical" ? sx : sy;
    d.size = size; validate(d);
}
void flipCanvas(Document &d, bool horizontal) {
    for (auto &l : d.layers) {
        flipMask(l,d.size,horizontal);
        if (horizontal) { l.origin.setX(d.size.width()-l.origin.x()-l.size.width()); l.flipX=!l.flipX; }
        else { l.origin.setY(d.size.height()-l.origin.y()-l.size.height()); l.flipY=!l.flipY; }
        l.rotation=-l.rotation;
    }
    for (auto &g : d.guides) if ((g.axis=="vertical")==horizontal)
        g.position=(horizontal ? d.size.width() : d.size.height())-g.position;
    validate(d);
}
QPointF snapLayerOrigin(const Document &d, int index, QPointF origin, double tolerance, bool guides) {
    if (index < 0 || index >= d.layers.size() || tolerance <= 0) return origin;
    auto moved=d.layers[index]; moved.origin=origin;
    auto bounds=moved.transform().mapRect(QRectF(QPointF(),moved.size));
    QVector<double> xs{0, d.size.width()/2.0, double(d.size.width())};
    QVector<double> ys{0, d.size.height()/2.0, double(d.size.height())};
    if (guides) for (const auto &g : d.guides) {
        if(g.axis=="vertical") xs.append(g.position); else ys.append(g.position);
    }
    for(int i=0;i<d.layers.size();++i) if(i!=index && d.layers[i].visible) {
        const auto &l=d.layers[i]; auto r=l.transform().mapRect(QRectF(QPointF(),l.size));
        xs << r.left() << r.center().x() << r.right();
        ys << r.top() << r.center().y() << r.bottom();
    }
    auto nearest=[&](const QVector<double> &targets, std::initializer_list<double> edges) {
        double distance=tolerance, correction=0;
        for(double edge:edges) for(double target:targets) if(std::abs(target-edge)<distance) {
            correction=target-edge; distance=std::abs(correction);
        }
        return correction;
    };
    return origin+QPointF(nearest(xs,{bounds.left(),bounds.center().x(),bounds.right()}),
        nearest(ys,{bounds.top(),bounds.center().y(),bounds.bottom()}));
}
void mergeDown(Document &d) {
    if (d.active <= 0) throw std::runtime_error("Select a layer with another layer beneath it.");
    int top = d.active;
    int bottom=top-1;
    while(bottom>=0 && d.layers[bottom].parentID!=d.layers[top].parentID) --bottom;
    if(bottom<0 || d.layers[top].isGroup || d.layers[bottom].isGroup)
        throw std::runtime_error("Merge Down needs two raster siblings. Use Merge Folder for a folder.");
    if (d.layers[top].blend != "Normal" || d.layers[bottom].blend != "Normal")
        throw std::runtime_error("Set both layers to Normal before merging. Other modes depend on the layers below them.");
    QRectF extent = visualBounds(d.layers[top])
        .united(visualBounds(d.layers[bottom]));
    QRect bounds = extent.toAlignedRect(); checkSize(bounds.size());
    auto working=d; QSet<QUuid> removed{d.layers[bottom].id,d.layers[top].id};
    for(int i:{bottom,top}) if(!working.layers[i].maskSourceID.isNull() && !removed.contains(working.layers[i].maskSourceID)) bakeClippingMask(working,i);
    Document pair = d; pair.layers = {working.layers[bottom],working.layers[top]}; pair.active = 1; pair.size = bounds.size();
    for (auto &layer : pair.layers) { layer.origin -= bounds.topLeft(); offsetMask(layer,-bounds.topLeft()); layer.parentID={}; }
    Layer merged; merged.parentID=d.layers[top].parentID; merged.name = d.layers[top].name; merged.image = render(pair); merged.size = bounds.size(); merged.origin = bounds.topLeft();
    bakeClippingDependents(d,removed);
    d.layers[bottom] = merged; d.layers.removeAt(top); d.active = bottom; validate(d);
}
QImage blurImage(const QImage &image, int radius) {
    if (radius < 0 || radius > 250) throw std::runtime_error("Blur radius must be between 0 and 250.");
    if (radius == 0 || image.isNull()) return image;
    bool gray = image.format() == QImage::Format_Grayscale8;
    QImage current = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage scratch(current.size(),current.format());
    if (scratch.isNull() || current.isNull()) throw std::runtime_error("Insufficient memory for blur.");
    // Three separable box passes approximate a Gaussian; running sums keep work
    // independent of radius. Premultiplied channels prevent transparent fringes.
    for (int pass=0;pass<3;++pass) for (int axis=0;axis<2;++axis) {
        int length = axis ? current.height() : current.width();
        int lines = axis ? current.width() : current.height();
        int r = std::max(1,int(std::round(radius*0.58)));
        for (int line=0;line<lines;++line) {
            auto get = [&](int position) {
                position = std::clamp(position,0,length-1);
                return axis ? current.pixel(line,position) : current.pixel(position,line);
            };
            qint64 red=0,green=0,blue=0,alpha=0;
            auto accumulate = [&](QRgb v,int sign) { red+=sign*qRed(v); green+=sign*qGreen(v); blue+=sign*qBlue(v); alpha+=sign*qAlpha(v); };
            for (int j=-r;j<=r;++j) accumulate(get(j),1);
            int count=2*r+1;
            for (int j=0;j<length;++j) {
                QRgb value=qRgba(red/count,green/count,blue/count,alpha/count);
                if (axis) reinterpret_cast<QRgb *>(scratch.scanLine(j))[line]=value;
                else reinterpret_cast<QRgb *>(scratch.scanLine(line))[j]=value;
                accumulate(get(j-r),-1); accumulate(get(j+r+1),1);
            }
        }
        current.swap(scratch);
    }
    current.setColorSpace(image.colorSpace());
    return gray ? current.convertToFormat(QImage::Format_Grayscale8) : current;
}
}
