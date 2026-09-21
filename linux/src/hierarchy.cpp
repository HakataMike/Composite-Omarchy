#include "hierarchy.h"
#include "styles.h"
#include "clipping.h"
#include "masks.h"
#include "effects.h"
#include "geometry.h"
#include <QHash>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace Arc {
namespace {
QHash<QUuid,int> indices(const Document &d) {
    QHash<QUuid,int> result;
    for(int i=0;i<d.layers.size();++i) result.insert(d.layers[i].id,i);
    return result;
}
void multiplyMask(Layer &child, const Layer &group) {
    if(child.image.isNull() || group.mask.isNull() || !group.maskEnabled) return;
    QTransform sourceToDocument=child.transform();
    sourceToDocument.scale(child.size.width()/child.image.width(),child.size.height()/child.image.height());
    QImage coverage(child.image.size(),QImage::Format_RGB32);
    if(coverage.isNull()) throw std::runtime_error("Insufficient memory for folder mask.");
    coverage.fill(Qt::black);
    {
        QPainter p(&coverage); p.setTransform(group.transform()*sourceToDocument.inverted());
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(QPointF(),group.size),rasterMask(group,maskEditingSize(group)));
    }
    child.image=child.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if(child.image.isNull()) throw std::runtime_error("Insufficient memory for folder mask.");
    for(int y=0;y<child.image.height();++y) {
        auto *row=reinterpret_cast<QRgb *>(child.image.scanLine(y));
        const auto *mask=reinterpret_cast<const QRgb *>(coverage.constScanLine(y));
        for(int x=0;x<child.image.width();++x) {
            auto c=qRed(mask[x]); auto v=row[x];
            row[x]=qRgba((qRed(v)*c+127)/255,(qGreen(v)*c+127)/255,(qBlue(v)*c+127)/255,(qAlpha(v)*c+127)/255);
        }
    }
}
}
void validateHierarchy(const Document &d) {
    auto byId=indices(d);
    for(const auto &l:d.layers) {
        if(l.isGroup && (!l.image.isNull() || !l.adjustment.isEmpty() || !l.shape.isEmpty() || !l.text.isEmpty() || l.blend!="Normal"))
            throw std::runtime_error("Folders cannot contain source pixels or a layer blend mode.");
        QSet<QUuid> seen{l.id};
        auto parent=l.parentID;
        while(!parent.isNull()) {
            if(seen.contains(parent) || seen.size()>64 || !byId.contains(parent) || !d.layers[byId[parent]].isGroup)
                throw std::runtime_error("Invalid or cyclic layer hierarchy (maximum depth 64).");
            seen.insert(parent); parent=d.layers[byId[parent]].parentID;
        }
    }
}
QVector<int> layerOrder(const Document &d, bool topFirst) {
    QHash<QUuid,QVector<int>> children;
    for(int i=0;i<d.layers.size();++i) children[d.layers[i].parentID].append(i);
    QVector<int> result;
    std::function<void(QUuid)> visit=[&](QUuid parent) {
        auto siblings=children.value(parent);
        if(topFirst) std::reverse(siblings.begin(),siblings.end());
        for(int i:siblings) { result.append(i); if(d.layers[i].isGroup) visit(d.layers[i].id); }
    };
    visit({}); return result;
}
QVector<int> descendants(const Document &d, QUuid parent) {
    QHash<QUuid,QVector<int>> children;
    for(int i=0;i<d.layers.size();++i) children[d.layers[i].parentID].append(i);
    QVector<int> result; QVector<QUuid> pending{parent};
    while(!pending.isEmpty()) for(int i:children.value(pending.takeLast())) { result.append(i); pending.append(d.layers[i].id); }
    std::sort(result.begin(),result.end()); return result;
}
QVector<int> visibleLayerOrder(const Document &d, bool topFirst) {
    QVector<int> result; QHash<QUuid,bool> visibility;
    for(int i:layerOrder(d,topFirst)) {
        const auto &l=d.layers[i];
        bool visible=l.visible && (l.parentID.isNull() || visibility.value(l.parentID));
        visibility[l.id]=visible;
        if(visible) result.append(i);
    }
    return result;
}
void groupLayers(Document &d, const QVector<QUuid> &selected) {
    auto byId=indices(d); QSet<QUuid> chosen(selected.begin(),selected.end()); QVector<int> roots;
    for(int i:layerOrder(d)) if(chosen.contains(d.layers[i].id)) {
        bool ancestorSelected=false;
        for(auto parent=d.layers[i].parentID; !parent.isNull(); parent=d.layers[byId[parent]].parentID)
            ancestorSelected |= chosen.contains(parent);
        if(!ancestorSelected) roots.append(i);
    }
    if(roots.isEmpty()) return;
    auto parent=d.layers[roots[0]].parentID;
    auto within=[&](int i,QUuid ancestor) {
        if(ancestor.isNull()) return true;
        for(auto p=d.layers[i].parentID;!p.isNull();p=d.layers[byId[p]].parentID) if(p==ancestor) return true;
        return false;
    };
    while(!std::all_of(roots.begin(),roots.end(),[&](int i) { return within(i,parent); })) parent=d.layers[byId[parent]].parentID;
    int insertion=0;
    for(int i:roots) {
        auto branch=i;
        while(d.layers[branch].parentID!=parent) branch=byId[d.layers[branch].parentID];
        insertion=std::max(insertion,branch+1);
    }
    Layer folder; folder.isGroup=true; folder.name="Folder"; folder.size=d.size; folder.parentID=parent;
    for(int i:roots) d.layers[i].parentID=folder.id;
    d.layers.insert(insertion,folder); d.active=insertion; validate(d);
}
bool effectiveVisible(const Document &d, int index) {
    auto byId=indices(d);
    for(int i=index;i>=0;) {
        if(!d.layers[i].visible) return false;
        auto parent=d.layers[i].parentID; i=parent.isNull() ? -1 : byId.value(parent,-1);
    }
    return true;
}
Document flattenGroups(const Document &d) {
    Document result=d; result.layers.clear(); result.active=-1;
    auto byId=indices(d);
    for(int index:layerOrder(d)) {
        auto layer=d.layers[index]; if(layer.isGroup) continue;
        for(auto parent=layer.parentID; !parent.isNull();) {
            const auto &group=d.layers[byId[parent]];
            layer.visible=layer.visible && group.visible; layer.opacity*=group.opacity;
            multiplyMask(layer,group); parent=group.parentID;
        }
        layer.parentID={}; result.layers.append(layer);
    }
    return result;
}
void mergeFolder(Document &d) {
    if(d.active<0 || !d.layers[d.active].isGroup) throw std::runtime_error("Select a folder to merge.");
    auto folder=d.layers[d.active]; auto children=descendants(d,folder.id);
    QRectF extent;
    for(int i:children) if(!d.layers[i].isGroup) extent=extent.united(visualBounds(d.layers[i]));
    if(extent.isEmpty() || extent.width()>30000 || extent.height()>30000 || std::abs(extent.x())>1000000 || std::abs(extent.y())>1000000)
        throw std::runtime_error("Folder is empty or exceeds the supported raster bounds.");
    auto bounds=extent.toAlignedRect();
    QSet<QUuid> members{folder.id}; for(int i:children) members.insert(d.layers[i].id);
    auto working=d;
    for(int i:children) if(!working.layers[i].maskSourceID.isNull() && !members.contains(working.layers[i].maskSourceID)) bakeClippingMask(working,i);
    Document subtree=d; subtree.layers.clear(); subtree.size=bounds.size(); subtree.active=0;
    auto root=folder; root.parentID={}; root.visible=true; subtree.layers.append(root);
    for(int i:children) subtree.layers.append(working.layers[i]);
    for(auto &l:subtree.layers) { l.origin-=bounds.topLeft(); offsetMask(l,-bounds.topLeft()); }
    Layer merged; merged.visible=folder.visible; merged.id=folder.id; merged.name=folder.name; merged.parentID=folder.parentID;
    merged.image=render(subtree); merged.size=bounds.size(); merged.origin=bounds.topLeft();
    bakeClippingDependents(d,members);
    d.layers[d.active]=merged;
    std::sort(children.begin(),children.end(),std::greater<int>());
    for(int i:children) d.layers.removeAt(i);
    for(int i=0;i<d.layers.size();++i) if(d.layers[i].id==merged.id) d.active=i;
    validate(d);
}
void duplicateLayer(Document &d) {
    if(d.active<0) return;
    auto selected=descendants(d,d.layers[d.active].id); selected.prepend(d.active);
    QHash<QUuid,QUuid> replacements;
    for(int i:selected) replacements[d.layers[i].id]=QUuid::createUuid();
    QVector<Layer> copies;
    for(int i:selected) {
        auto copy=d.layers[i]; copy.id=replacements[copy.id];
        if(replacements.contains(copy.maskSourceID)) copy.maskSourceID=replacements[copy.maskSourceID];
        if(replacements.contains(copy.parentID)) copy.parentID=replacements[copy.parentID];
        if(i==d.active) copy.name+=" copy";
        copies.append(copy);
    }
    int insertion=d.active+1;
    for(int i=0;i<copies.size();++i) d.layers.insert(insertion+i,copies[i]);
    d.active=insertion; validate(d);
}
void deleteLayer(Document &d) {
    if(d.active<0) return;
    auto remove=descendants(d,d.layers[d.active].id); remove.append(d.active);
    QSet<QUuid> removed; for(int i:remove) removed.insert(d.layers[i].id);
    bakeClippingDependents(d,removed);
    std::sort(remove.begin(),remove.end(),std::greater<int>());
    for(int i:remove) d.layers.removeAt(i);
    d.active=std::min(d.active,int(d.layers.size())-1); validate(d);
}
void reorderLayer(Document &d, bool raise) {
    if(d.active<0) return;
    auto parent=d.layers[d.active].parentID;
    int step=raise ? 1 : -1;
    for(int i=d.active+step;i>=0 && i<d.layers.size();i+=step) if(d.layers[i].parentID==parent) {
        d.layers.swapItemsAt(d.active,i); d.active=i; break;
    }
}
void transformGroup(Document &d, int index, const Layer &replacement) {
    const auto original=d.layers[index];
    auto adjusted=replacement; followMask(original,adjusted);
    if(!original.isGroup) { d.layers[index]=adjusted; return; }
    if(original.origin==replacement.origin && original.size==replacement.size && original.rotation==replacement.rotation
        && original.flipX==replacement.flipX && original.flipY==replacement.flipY) { d.layers[index]=adjusted; return; }
    QTransform scale; scale.scale(replacement.size.width()/original.size.width(),replacement.size.height()/original.size.height());
    auto mapping=original.transform().inverted()*scale*replacement.transform();
    for(int i:descendants(d,original.id)) {
        auto &child=d.layers[i]; auto before=child;
        auto transformed=child.transform()*mapping;
        child=placedLayer(child,transformed);
        followMask(before,child);
        if(!child.shape.isEmpty()) child.image=shapeImage(child.shape,child.size);
    }
    d.layers[index]=adjusted;
}
}
