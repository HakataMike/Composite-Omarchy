#include "selection.h"
#include <memory>
#include <stdexcept>
extern "C" {
#include "kernels/WandPixels.h"
}
namespace Arc {
namespace {
QPainterPath trace(const QByteArray &mask, int width, int height) {
    int32_t *points=nullptr,*loops=nullptr; size_t pointCount=0,loopCount=0;
    int result=wand_trace(reinterpret_cast<const uint8_t *>(mask.constData()),width,height,&points,&pointCount,&loops,&loopCount);
    std::unique_ptr<int32_t,decltype(&std::free)> pointOwner(points,&std::free),loopOwner(loops,&std::free);
    if(result!=0) throw std::runtime_error(result==-2 ? "Selection is too detailed to trace. Increase tolerance or select a smaller area." : "Insufficient memory for selection.");
    QPainterPath path; path.setFillRule(Qt::WindingFill); size_t offset=0;
    for(size_t i=0;i<loopCount;++i) {
        if(loops[i]<1) continue;
        path.moveTo(points[offset*2],points[offset*2+1]);
        for(int32_t j=1;j<loops[i];++j) path.lineTo(points[(offset+j)*2],points[(offset+j)*2+1]);
        path.closeSubpath(); offset+=loops[i];
    }
    return path;
}
}
QPainterPath wandSelection(const QImage &image, QPoint seed, int tolerance, bool contiguous) {
    if(!image.rect().contains(seed)) return {};
    if(tolerance<0 || tolerance>255) throw std::runtime_error("Wand tolerance must be between 0 and 255.");
    auto rgba=image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QByteArray mask(qsizetype(image.width())*image.height(),0);
    if(rgba.isNull()) throw std::runtime_error("Insufficient memory for magic wand.");
    if(wand_mask(rgba.constBits(),rgba.width(),rgba.height(),rgba.bytesPerLine(),seed.x(),seed.y(),0,tolerance,contiguous,
                 reinterpret_cast<uint8_t *>(mask.data()))<0) throw std::runtime_error("Insufficient memory for magic wand.");
    return trace(mask,image.width(),image.height());
}
QPainterPath alphaSelection(const QImage &image) {
    QByteArray mask(qsizetype(image.width())*image.height(),0);
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
        mask[y*image.width()+x]=image.pixelColor(x,y).alpha() ? char(255) : 0;
    return trace(mask,image.width(),image.height());
}
}

#include "styles.h"
#include "clipping.h"
#include <algorithm>
namespace Arc {
QImage pathCoverage(const QPainterPath &path,QSize size) {
    QImage result(size,QImage::Format_Grayscale8); if(result.isNull()) throw std::runtime_error("Insufficient memory for selection.");
    result.fill(Qt::black); QPainter p(&result); p.setRenderHint(QPainter::Antialiasing); p.fillPath(path,Qt::white); return result;
}
QImage alphaCoverage(const QImage &image) {
    QImage result(image.size(),QImage::Format_Grayscale8); if(result.isNull()) throw std::runtime_error("Insufficient memory for selection.");
    for(int y=0;y<result.height();++y) for(int x=0;x<result.width();++x) result.scanLine(y)[x]=qAlpha(image.pixel(x,y));
    return result;
}
QPainterPath coverageOutline(const QImage &coverage) {
    if(coverage.isNull() || coverage.format()!=QImage::Format_Grayscale8) throw std::runtime_error("Invalid selection coverage.");
    QByteArray mask(qsizetype(coverage.width())*coverage.height(),0);
    for(int y=0;y<coverage.height();++y) for(int x=0;x<coverage.width();++x) mask[y*coverage.width()+x]=coverage.constScanLine(y)[x] ? char(255) : 0;
    return trace(mask,coverage.width(),coverage.height());
}
QImage selectionInLayer(const Layer &layer,QSize pixels,const QImage &coverage) {
    if(coverage.isNull() || coverage.format()!=QImage::Format_Grayscale8) throw std::runtime_error("Invalid selection coverage.");
    QImage result(pixels,QImage::Format_Grayscale8); if(result.isNull()) throw std::runtime_error("Insufficient memory for selection mapping.");
    result.fill(Qt::black); auto transform=layer.transform(); transform.scale(layer.size.width()/pixels.width(),layer.size.height()/pixels.height());
    QPainter p(&result); p.setTransform(transform.inverted()); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(QPoint(),coverage); return result;
}
QImage limitToSelection(const QImage &original,const QImage &changed,const Layer &target,const QImage &coverage) {
    if(coverage.isNull()) return changed;
    bool gray=original.format()==QImage::Format_Grayscale8;
    auto before=original.size()==changed.size() ? original : original.scaled(changed.size());
    before=before.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    auto result=changed.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    auto mask=selectionInLayer(target,result.size(),coverage);
    if(before.isNull() || result.isNull()) throw std::runtime_error("Insufficient memory for selected pixels.");
    for(int y=0;y<result.height();++y) {
        auto *row=reinterpret_cast<QRgb *>(result.scanLine(y)); const auto *old=reinterpret_cast<const QRgb *>(before.constScanLine(y)); const auto *m=mask.constScanLine(y);
        for(int x=0;x<result.width();++x) {
            auto mix=[&](int a,int b) { return (a*(255-m[x])+b*m[x]+127)/255; };
            row[x]=qRgba(mix(qRed(old[x]),qRed(row[x])),mix(qGreen(old[x]),qGreen(row[x])),mix(qBlue(old[x]),qBlue(row[x])),mix(qAlpha(old[x]),qAlpha(row[x])));
        }
    }
    return gray ? result.convertToFormat(QImage::Format_Grayscale8) : result;
}
QImage selectedLayerPixels(const Document &d,const QImage &coverage) {
    if(d.active<0 || d.layers[d.active].image.isNull()) throw std::runtime_error("Select an image layer first.");
    auto single=d; if(!single.layers[single.active].maskSourceID.isNull()) bakeClippingMask(single,single.active);
    auto layer=single.layers[single.active]; layer.parentID={}; layer.visible=true; layer.opacity=1; layer.blend="Normal";
    single.layers={layer}; single.active=0; auto pixels=render(single);
    QImage empty(pixels.size(),pixels.format()); empty.fill(Qt::transparent); Layer target; target.size=d.size;
    return limitToSelection(empty,pixels,target,coverage);
}
void cutSelectedPixels(Document &d,const QImage &coverage) {
    if(d.active<0 || d.layers[d.active].image.isNull()) throw std::runtime_error("Select an image layer first.");
    auto &layer=d.layers[d.active]; QImage empty(layer.image.size(),layer.image.format()); empty.fill(Qt::transparent);
    layer.image=limitToSelection(layer.image,empty,layer,coverage); rasterize(layer);
}
void floatSelectedPixels(Document &d,const QImage &coverage,QPointF offset,bool duplicate) {
    auto pixels=selectedLayerPixels(d,coverage); auto bounds=coverageOutline(coverage).boundingRect().toAlignedRect().intersected(pixels.rect());
    if(bounds.isEmpty()) return;
    Layer floating; floating.name=d.layers[d.active].name+" selection"; floating.parentID=d.layers[d.active].parentID;
    floating.image=pixels.copy(bounds); floating.size=bounds.size(); floating.origin=bounds.topLeft()+offset;
    if(!duplicate) cutSelectedPixels(d,coverage);
    d.layers.insert(d.active+1,floating); ++d.active; validate(d);
}
}
