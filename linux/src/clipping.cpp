#include "clipping.h"
#include "hierarchy.h"
#include "blending.h"
#include "styles.h"
#include "masks.h"
#include <QHash>
#include <QSet>
#include <functional>
#include <stdexcept>

namespace Arc {
bool hasClipping(const Document &d) {
    for(const auto &l:d.layers) if(!l.maskSourceID.isNull()) return true;
    return false;
}
void validateClipping(const Document &d) {
    QHash<QUuid,const Layer *> layers;
    for(const auto &l:d.layers) layers[l.id]=&l;
    for(const auto &l:d.layers) {
        QSet<QUuid> seen;
        auto id=l.id;
        while(!id.isNull()) {
            if(!layers.contains(id) || seen.contains(id) || seen.size()>=256)
                throw std::runtime_error("Missing or cyclic clipping source (maximum chain length 256).");
            seen.insert(id); const auto &current=*layers[id];
            if(!current.maskSourceID.isNull() && (current.isGroup || !layers.contains(current.maskSourceID) || layers[current.maskSourceID]->isGroup))
                throw std::runtime_error("Clipping masks require raster source and target layers.");
            id=current.maskSourceID;
        }
    }
}
namespace {
class ClippingRenderer {
public:
    Document flat, sources;
    QHash<QUuid,int> byId;
    QHash<QUuid,QUuid> parents;
    explicit ClippingRenderer(const Document &d) : flat(flattenGroups(d)) {
        auto unmasked=d;
        for(auto &l:unmasked.layers) if(l.isGroup) l.maskEnabled=false;
        sources=flattenGroups(unmasked);
        for(const auto &l:d.layers) parents[l.id]=l.parentID;
        for(int i=0;i<flat.layers.size();++i) byId[flat.layers[i].id]=i;
    }
    QImage own(int index, bool folderMasks=true) const {
        Document single=folderMasks ? flat : sources; auto layer=single.layers[index];
        layer.visible=true; layer.blend="Normal"; layer.maskSourceID={};
        single.layers={layer}; single.active=0; return render(single);
    }
    QImage alpha(QUuid id) const {
        const auto &layer=flat.layers[byId.value(id)];
        // Resolve the dependency first: memory use stays bounded by a few surfaces,
        // rather than keeping a full-canvas image at every recursion depth.
        QImage dependency;
        if(!layer.maskSourceID.isNull()) dependency=alpha(layer.maskSourceID);
        auto pixels=own(byId.value(id),false);
        QImage coverage(flat.size,QImage::Format_Grayscale8);
        if(coverage.isNull()) throw std::runtime_error("Insufficient memory for clipping mask.");
        for(int y=0;y<coverage.height();++y) {
            auto *row=coverage.scanLine(y); const auto *src=reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
            for(int x=0;x<coverage.width();++x)
                row[x]=dependency.isNull() ? qAlpha(src[x]) : (qAlpha(src[x])*dependency.constScanLine(y)[x]+127)/255;
        }
        return coverage;
    }
};
void applyAlpha(QImage &image, const QImage &alpha, bool replace) {
    for(int y=0;y<image.height();++y) {
        auto *row=reinterpret_cast<QRgb *>(image.scanLine(y)); const auto *mask=alpha.constScanLine(y);
        for(int x=0;x<image.width();++x) {
            auto v=replace ? qUnpremultiply(row[x]) : row[x]; int c=mask[x];
            row[x]=qRgba((qRed(v)*c+127)/255,(qGreen(v)*c+127)/255,(qBlue(v)*c+127)/255,
                replace ? c : (qAlpha(v)*c+127)/255);
        }
    }
}
}
QImage renderClipping(const Document &d) {
    ClippingRenderer renderer(d);
    QImage result(d.size,QImage::Format_ARGB32_Premultiplied);
    if(result.isNull()) throw std::runtime_error("Insufficient memory for clipping composite.");
    result.fill(Qt::transparent);
    QVector<int> visible;
    for(int i=0;i<renderer.flat.layers.size();++i) if(renderer.flat.layers[i].visible) visible.append(i);
    for(int row=0;row<visible.size();++row) {
        int index=visible[row]; const auto &base=renderer.flat.layers[index];
        auto pixels=renderer.own(index);
        int end=row+1;
        if(base.maskSourceID.isNull()) while(end<visible.size()) {
            const auto &child=renderer.flat.layers[visible[end]];
            if(child.maskSourceID!=base.id || renderer.parents[child.id]!=renderer.parents[base.id]) break;
            ++end;
        }
        if(end>row+1) {
            QImage coverage(d.size,QImage::Format_Grayscale8);
            if(coverage.isNull()) throw std::runtime_error("Insufficient memory for clipping coverage.");
            for(int y=0;y<pixels.height();++y) {
                auto *mask=coverage.scanLine(y); const auto *line=reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
                for(int x=0;x<pixels.width();++x) mask[x]=qAlpha(line[x]);
            }
            for(int y=0;y<pixels.height();++y) {
                auto *line=reinterpret_cast<QRgb *>(pixels.scanLine(y));
                for(int x=0;x<pixels.width();++x) line[x]=qUnpremultiply(line[x])|0xff000000;
            }
            for(int child=row+1;child<end;++child) {
                const auto &layer=renderer.flat.layers[visible[child]];
                compositeImages(pixels,renderer.own(visible[child],false),layer.blend);
            }
            applyAlpha(pixels,coverage,true); row=end-1;
        } else if(!base.maskSourceID.isNull()) applyAlpha(pixels,renderer.alpha(base.maskSourceID),false);
        compositeImages(result,pixels,base.blend);
    }
    return result;
}
void createClippingMask(Document &d) {
    if(d.active<0 || d.layers[d.active].isGroup) throw std::runtime_error("Select a raster layer to clip.");
    auto &layer=d.layers[d.active];
    for(int i=d.active-1;i>=0;--i) if(d.layers[i].parentID==layer.parentID) {
        if(d.layers[i].isGroup) break;
        layer.maskSourceID=d.layers[i].maskSourceID.isNull() ? d.layers[i].id : d.layers[i].maskSourceID;
        validate(d); return;
    }
    throw std::runtime_error("A clipping mask needs a raster sibling underneath.");
}
void releaseClippingMask(Document &d) {
    if(d.active<0) return;
    auto source=d.layers[d.active].maskSourceID, parent=d.layers[d.active].parentID;
    if(source.isNull()) return;
    for(int i=d.active;i<d.layers.size();++i) if(d.layers[i].parentID==parent) {
        if(d.layers[i].maskSourceID!=source) break;
        d.layers[i].maskSourceID={};
    }
}
void bakeClippingDependents(Document &d, const QSet<QUuid> &removed) {
    auto original=d;
    for(int i=0;i<d.layers.size();++i) if(!removed.contains(d.layers[i].id) && removed.contains(d.layers[i].maskSourceID)) {
        auto copy=original; bakeClippingMask(copy,i); d.layers[i]=copy.layers[i];
    }
}
void bakeClippingMask(Document &d, int index) {
    auto &layer=d.layers[index]; if(layer.maskSourceID.isNull()) return;
    auto extent=layer.transform().mapRect(QRectF(QPointF(),layer.size));
    if(extent.width()>30000 || extent.height()>30000)
        throw std::runtime_error("Clipping bake exceeds supported dimensions.");
    auto bounds=extent.toAlignedRect();
    auto region=d; region.size=bounds.size();
    for(auto &l:region.layers) { l.origin-=bounds.topLeft(); offsetMask(l,-bounds.topLeft()); }
    validate(region);
    ClippingRenderer renderer(region); auto alpha=renderer.alpha(layer.maskSourceID);
    QImage coverage(layer.image.size(),QImage::Format_Grayscale8);
    if(coverage.isNull()) throw std::runtime_error("Select a raster layer to bake.");
    coverage.fill(Qt::black);
    auto transform=layer.transform(); transform.scale(layer.size.width()/layer.image.width(),layer.size.height()/layer.image.height());
    { QPainter p(&coverage); p.setTransform(transform.inverted()); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(bounds.topLeft(),alpha); }
    layer.image=layer.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if(layer.image.isNull()) throw std::runtime_error("Insufficient memory to bake clipping mask.");
    applyAlpha(layer.image,coverage,false); layer.maskSourceID={}; rasterize(layer);
}
}
