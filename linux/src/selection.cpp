#include "selection.h"
#include <memory>
#include <stdexcept>
extern "C" {
#include "../../Compositor/Rendering/WandPixels.h"
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
