#include "effects.h"
#include "masks.h"
#include "operations.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Arc {
namespace {
void check(bool ok) { if(!ok) throw std::runtime_error("Invalid layer effect settings."); }
bool enabled(const QJsonObject &effect) { return !effect.isEmpty() && effect["enabled"].toBool(true); }
double number(const QJsonObject &o,QString key,double low,double high) {
    auto v=o[key]; check(v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=low && v.toDouble()<=high); return v.toDouble();
}
int margin(const QJsonObject &effects) {
    double result=0; auto stroke=effects["stroke"].toObject(),shadow=effects["shadow"].toObject();
    if(enabled(stroke) && !stroke["inside"].toBool()) result=stroke["size"].toDouble();
    if(enabled(shadow)) result=std::max(result,shadow["distance"].toDouble()+shadow["blur"].toDouble()*3);
    return int(std::ceil(result))+2;
}
QImage extreme(const QImage &source,int radius,bool smallest) {
    QImage current=source,scratch(source.size(),source.format());
    if(scratch.isNull()) throw std::runtime_error("Insufficient memory for stroke.");
    QVector<int> queue(std::max(source.width(),source.height()));
    for(int axis=0;axis<2;++axis) {
        int lines=axis ? source.width() : source.height(),length=axis ? source.height() : source.width();
        for(int line=0;line<lines;++line) {
            auto value=[&](int j) { return axis ? current.constScanLine(j)[line] : current.constScanLine(line)[j]; };
            int head=0,tail=0,next=0;
            for(int center=0;center<length;++center) {
                while(next<=std::min(length-1,center+radius)) {
                    while(tail>head && (smallest ? value(queue[tail-1])>=value(next) : value(queue[tail-1])<=value(next))) --tail;
                    queue[tail++]=next++;
                }
                while(head<tail && queue[head]<center-radius) ++head;
                uchar v=smallest && (center<radius || center+radius>=length) ? 0 : value(queue[head]);
                if(axis) scratch.scanLine(center)[line]=v; else scratch.scanLine(line)[center]=v;
            }
        }
        current.swap(scratch);
    }
    return current;
}
QImage shifted(const QImage &shape,const QJsonObject &settings) {
    double radians=settings["angle"].toDouble()*3.14159265358979323846/180,distance=settings["distance"].toDouble();
    QImage result(shape.size(),QImage::Format_Grayscale8); if(result.isNull()) throw std::runtime_error("Insufficient memory for shadow.");
    result.fill(Qt::black);
    { QPainter p(&result); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(QPointF(-std::cos(radians)*distance,std::sin(radians)*distance),shape); }
    return blurImage(result,qRound(settings["blur"].toDouble()/2));
}
void fill(QImage &image,const QImage &coverage,const QJsonObject &effect) {
    if(!enabled(effect)) return;
    QColor color=QColor::fromRgbF(effect["red"].toDouble(),effect["green"].toDouble(),effect["blue"].toDouble());
    QImage top(image.size(),image.format()); if(top.isNull()) throw std::runtime_error("Insufficient memory for effect.");
    for(int y=0;y<top.height();++y) {
        auto *row=reinterpret_cast<QRgb *>(top.scanLine(y)); const auto *mask=coverage.constScanLine(y);
        for(int x=0;x<top.width();++x) row[x]=qPremultiply(qRgba(color.red(),color.green(),color.blue(),qRound(mask[x]*effect["opacity"].toDouble())));
    }
    QPainter p(&image); p.drawImage(QPoint(),top);
}
}
QStringList effectKinds() { return {"stroke","shadow","colorOverlay","innerShadow"}; }
QJsonObject defaultEffect(const QString &kind) {
    check(effectKinds().contains(kind)); QJsonObject value{{"enabled",true},{"red",0},{"green",0},{"blue",0},{"opacity",1}};
    if(kind=="stroke") { value["size"]=4; value["inside"]=false; }
    if(kind=="shadow" || kind=="innerShadow") { value["angle"]=90; value["distance"]=kind=="shadow" ? 20 : 10; value["blur"]=value["distance"]; value["opacity"]=0.5; }
    return value;
}
void validateEffects(const QJsonObject &effects) {
    for(auto i=effects.begin();i!=effects.end();++i) {
        check(effectKinds().contains(i.key())); if(i.value().isNull()) continue;
        check(i.value().isObject()); auto v=i.value().toObject();
        QStringList allowed{"enabled","red","green","blue","opacity"};
        for(auto key:{"red","green","blue","opacity"}) number(v,key,0,1);
        if(v.contains("enabled") && !v["enabled"].isNull()) check(v["enabled"].isBool());
        if(i.key()=="stroke") { allowed << "size" << "inside"; number(v,"size",0,500); check(v["inside"].isBool()); }
        if(i.key()=="shadow" || i.key()=="innerShadow") {
            allowed << "angle" << "distance" << "blur"; number(v,"angle",-360,360); number(v,"distance",0,5000); number(v,"blur",0,500);
        }
        for(auto k=v.begin();k!=v.end();++k) check(allowed.contains(k.key()));
    }
}
bool hasEffects(const Document &d) { for(const auto &l:d.layers) if(!l.effects.isEmpty()) return true; return false; }
QRectF visualBounds(const Layer &l) {
    if(l.effects.isEmpty() || l.image.isNull()) return l.transform().mapRect(QRectF(QPointF(),l.size));
    int pad=margin(l.effects); double x=pad*l.size.width()/l.image.width(),y=pad*l.size.height()/l.image.height();
    return l.transform().mapRect(QRectF(-x,-y,l.size.width()+2*x,l.size.height()+2*y));
}
Layer renderedEffects(const Layer &l) {
    if(l.effects.isEmpty()) return l;
    validateEffects(l.effects);
    bool visible=false; for(auto kind:effectKinds()) visible |= enabled(l.effects[kind].toObject());
    if(!visible) { auto result=l; result.effects={}; return result; }
    if(l.image.isNull()) throw std::runtime_error("Effects require source image pixels.");
    int pad=margin(l.effects); QSize size=l.image.size()+QSize(2*pad,2*pad);
    if(size.width()>30000 || size.height()>30000 || qint64(size.width())*size.height()>MaxPixels) throw std::runtime_error("Layer effects exceed supported raster dimensions.");
    auto own=l; own.effects={}; own.parentID={}; own.maskSourceID={}; own.adjustment={}; own.shape={}; own.text={};
    if(!own.mask.isNull()) own.mask=rasterMask(l,l.image.size());
    own.maskPlacement={}; own.origin=QPointF(pad,pad); own.size=l.image.size(); own.rotation=0; own.flipX=own.flipY=false; own.opacity=1; own.visible=true; own.blend="Normal";
    Document local; local.size=size; local.layers={own}; local.active=0;
    QImage shown=render(local),shape(size,QImage::Format_Grayscale8),result(size,QImage::Format_ARGB32_Premultiplied);
    if(shape.isNull() || result.isNull()) throw std::runtime_error("Insufficient memory for layer effects.");
    result.fill(Qt::transparent);
    for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) shape.scanLine(y)[x]=qAlpha(shown.pixel(x,y));
    auto stroke=l.effects["stroke"].toObject(),shadow=l.effects["shadow"].toObject(),overlay=l.effects["colorOverlay"].toObject(),inner=l.effects["innerShadow"].toObject();
    if(enabled(shadow)) fill(result,shifted(shape,shadow),shadow);
    QImage ring;
    if(enabled(stroke) && stroke["size"].toDouble()>0) {
        ring=extreme(shape,std::max(1,qRound(stroke["size"].toDouble())),stroke["inside"].toBool());
        for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) {
            int source=shape.constScanLine(y)[x],moved=ring.constScanLine(y)[x];
            ring.scanLine(y)[x]=std::max(0,stroke["inside"].toBool() ? source-moved : moved-source);
        }
        if(!stroke["inside"].toBool()) fill(result,ring,stroke);
    }
    { QPainter p(&result); p.drawImage(QPoint(),shown); }
    fill(result,shape,overlay);
    if(enabled(inner)) {
        auto coverage=shifted(shape,inner);
        for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) coverage.scanLine(y)[x]=(shape.constScanLine(y)[x]*(255-coverage.constScanLine(y)[x])+127)/255;
        fill(result,coverage,inner);
    }
    if(!ring.isNull() && stroke["inside"].toBool()) fill(result,ring,stroke);
    auto output=l; output.effects={}; output.shape={}; output.text={}; output.mask={}; output.maskPlacement={}; output.maskLinked=true;
    output.image=result;
    double sx=l.size.width()/l.image.width(),sy=l.size.height()/l.image.height();
    output.origin-=QPointF(pad*sx,pad*sy); output.size=QSizeF(size.width()*sx,size.height()*sy);
    return output;
}
Document renderEffects(const Document &d) {
    auto result=d; for(auto &l:result.layers) if(!l.effects.isEmpty()) l=renderedEffects(l); return result;
}
}
