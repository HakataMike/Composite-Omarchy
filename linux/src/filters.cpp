#include "filters.h"
#include "masks.h"
#include "operations.h"
#include <QColorSpace>
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include "../../Compositor/Rendering/AdjustPixels.h"
#include "../../Compositor/Rendering/LevelsPixels.h"
#include "../../Compositor/Rendering/NoisePixels.h"
#include "../../Compositor/Rendering/LensPixels.h"
#include "../../Compositor/Rendering/ContentFill.h"
}
namespace Arc {
QStringList filterNames() { return {"Levels","Curves","Hue/Saturation","Exposure","Gradient Map","Grain","Invert","Gaussian Blur","Noise","Lens Correction"}; }
QVector<FilterParameter> filterParameters(const QString &kind) {
    if(kind=="Levels") return {{"black","Input black",0,254,0},{"gamma","Gamma",0.01,9.99,1},{"white","Input white",1,255,255},{"outputBlack","Output black",0,255,0},{"outputWhite","Output white",0,255,255}};
    if(kind=="Hue/Saturation") return {{"hue","Hue (degrees)",-180,180,0},{"saturation","Saturation (%)",-100,100,0},{"lightness","Lightness (%)",-100,100,0}};
    if(kind=="Exposure") return {{"exposure","Exposure (stops)",-20,20,0},{"offset","Offset",-0.5,0.5,0},{"gamma","Gamma",0.01,9.99,1}};
    if(kind=="Grain") return {{"amount","Amount",0,100,25},{"size","Size",0.5,20,1.5},{"roughness","Roughness",0,100,50}};
    if(kind=="Gaussian Blur") return {{"radius","Radius (source pixels)",0,250,5}};
    if(kind=="Noise") return {{"amount","Amount (%)",0,100,10}};
    if(kind=="Lens Correction") return {{"distortion","Distortion",-1,1,0}};
    return {};
}
FilterSettings defaultFilter(const QString &kind) {
    FilterSettings settings; settings.kind=kind;
    for(const auto &p:filterParameters(kind)) settings.values[p.key]=p.initial;
    return settings;
}
namespace {
double curveValue(const QVector<QPointF> &points,double x) {
    int i=0; while(i+2<points.size() && points[i+1].x()<=x) ++i;
    auto slopeBetween=[&](int j) { return (points[j+1].y()-points[j].y())/(points[j+1].x()-points[j].x()); };
    auto slope=[&](int j) {
        if(j==0) return slopeBetween(0);
        if(j==points.size()-1) return slopeBetween(j-1);
        double a=slopeBetween(j-1),b=slopeBetween(j);
        return a*b<=0 ? 0.0 : 2/(1/a+1/b);
    };
    double h=points[i+1].x()-points[i].x(),t=std::clamp((x-points[i].x())/h,0.0,1.0);
    return std::clamp((2*t*t*t-3*t*t+1)*points[i].y()+(t*t*t-2*t*t+t)*h*slope(i)
        +(-2*t*t*t+3*t*t)*points[i+1].y()+(t*t*t-t*t)*h*slope(i+1),0.0,255.0);
}
QTransform documentToPixels(const Layer &layer,QSize pixels) {
    auto t=layer.transform(); t.scale(layer.size.width()/pixels.width(),layer.size.height()/pixels.height());
    return t.inverted();
}
}
QImage applyFilter(const QImage &image,const FilterSettings &f) {
    if(image.isNull()) throw std::runtime_error("Select a raster image or mask first.");
    if(!filterNames().contains(f.kind) || f.channel<0 || f.channel>3) throw std::runtime_error("Unsupported filter settings.");
    for(const auto &p:filterParameters(f.kind)) {
        auto value=f.values.value(p.key);
        if(!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble()<p.minimum || value.toDouble()>p.maximum)
            throw std::runtime_error("Invalid filter parameter.");
    }
    auto value=[&](const char *key) { return f.values.value(key).toDouble(); };
    QImage result=image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if(result.isNull()) throw std::runtime_error("Insufficient memory for filter.");
    if(f.kind=="Levels" || f.kind=="Exposure" || f.kind=="Curves") {
        if(f.kind=="Levels" && value("white")<=value("black")) throw std::runtime_error("Input white must be greater than input black.");
        if(f.kind=="Curves") {
            if(f.curve.size()<2 || f.curve.size()>32 || f.curve.first().x()!=0 || f.curve.last().x()!=255)
                throw std::runtime_error("Curve needs 2–32 points, starting at input 0 and ending at 255.");
            double previous=-1;
            for(auto point:f.curve) {
                if(!std::isfinite(point.x()) || !std::isfinite(point.y()) || point.x()<=previous || point.x()>255 || point.y()<0 || point.y()>255)
                    throw std::runtime_error("Curve inputs must increase, with all values from 0 to 255.");
                previous=point.x();
            }
        }
        float tables[768];
        for(int c=0;c<3;++c) for(int i=0;i<256;++i) {
            double x=i/255.0,y=x;
            if(f.kind=="Exposure") {
                double linear=x<=0.04045 ? x/12.92 : std::pow((x+0.055)/1.055,2.4);
                linear=std::pow(std::max(0.0,linear*std::exp2(value("exposure"))+value("offset")),1/value("gamma"));
                y=linear<=0.0031308 ? linear*12.92 : 1.055*std::pow(linear,1/2.4)-0.055;
            } else if(f.channel==0 || f.channel==c+1) {
                if(f.kind=="Levels") y=(value("outputBlack")+(value("outputWhite")-value("outputBlack"))
                    *std::pow(std::clamp((i-value("black"))/(value("white")-value("black")),0.0,1.0),1/value("gamma")))/255;
                else y=curveValue(f.curve,i)/255;
            }
            tables[c*256+i]=std::clamp(y,0.0,1.0);
        }
        levels_apply(result.bits(),size_t(result.width())*result.height(),tables);
    } else if(f.kind=="Gradient Map") {
        if(!f.shadows.isValid() || !f.highlights.isValid()) throw std::runtime_error("Invalid gradient colors.");
        uint8_t table[768];
        for(int i=0;i<256;++i) { double t=i/255.0;
            table[i*3]=qRound(f.shadows.red()*(1-t)+f.highlights.red()*t);
            table[i*3+1]=qRound(f.shadows.green()*(1-t)+f.highlights.green()*t);
            table[i*3+2]=qRound(f.shadows.blue()*(1-t)+f.highlights.blue()*t);
        }
        adjust_gradient_map(result.bits(),result.width(),result.height(),result.bytesPerLine(),table);
    } else if(f.kind=="Grain") adjust_grain(result.bits(),result.width(),result.height(),result.bytesPerLine(),value("amount"),value("size"),value("roughness"),f.seed,0,0,1);
    else if(f.kind=="Noise") noise_add(result.bits(),result.width(),result.height(),result.bytesPerLine(),value("amount"),f.values["gaussian"].toBool(),f.values["monochromatic"].toBool(),f.seed);
    else if(f.kind=="Lens Correction") {
        QImage source=result; result=result.copy();
        if(result.isNull()) throw std::runtime_error("Insufficient memory for lens correction.");
        lens_distort(source.constBits(),result.bits(),result.width(),result.height(),result.bytesPerLine(),value("distortion"));
    } else if(f.kind=="Gaussian Blur") result=blurImage(result,qRound(value("radius")));
    else {
        auto straight=result.convertToFormat(QImage::Format_ARGB32);
        if(straight.isNull()) throw std::runtime_error("Insufficient memory for color adjustment.");
        for(int y=0;y<straight.height();++y) {
            auto *row=reinterpret_cast<QRgb *>(straight.scanLine(y));
            for(int x=0;x<straight.width();++x) {
                auto color=QColor::fromRgba(row[x]); if(!color.alpha()) continue;
                if(f.kind=="Invert") color=QColor(255-color.red(),255-color.green(),255-color.blue(),color.alpha());
                else {
                    float h,s,l,a; color.getHslF(&h,&s,&l,&a);
                    h=std::fmod(std::max(0.0,double(h))+value("hue")/360+1,1.0);
                    s=std::clamp(s+value("saturation")/100,0.0,1.0); l=std::clamp(l+value("lightness")/100,0.0,1.0);
                    color=QColor::fromHslF(h,s,l,a);
                }
                row[x]=color.rgba();
            }
        }
        result=straight;
    }
    result=result.convertToFormat(image.format()==QImage::Format_Grayscale8 ? QImage::Format_Grayscale8 : QImage::Format_ARGB32_Premultiplied);
    if(result.isNull()) throw std::runtime_error("Insufficient memory for filtered image.");
    result.setColorSpace(image.colorSpace()); return result;
}
QImage filterLayer(const Layer &layer,const FilterSettings &filter,const std::optional<QPainterPath> &selection,bool mask) {
    QImage original=mask ? layer.mask : layer.image;
    if(mask && selection && original.size()==QSize(1,1)) original=original.scaled(maskEditingSize(layer));
    auto changed=applyFilter(original,filter);
    if(!selection) return changed;
    if(selection->isEmpty()) return original;
    QImage result=original.copy();
    if(result.isNull()) throw std::runtime_error("Insufficient memory for filtered selection.");
    QPainter p(&result); p.setTransform(documentToPixels(mask ? maskTargetLayer(layer) : layer,result.size())); p.setClipPath(*selection);
    p.setTransform(QTransform()); p.setCompositionMode(QPainter::CompositionMode_Source); p.drawImage(QPoint(),changed);
    return result;
}
QPair<int,int> autoLevels(const QImage &image) {
    auto rgba=image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if(rgba.isNull()) throw std::runtime_error("Select an image first.");
    double bins[1024]={}; levels_histogram(rgba.constBits(),nullptr,size_t(rgba.width())*rgba.height(),bins);
    double total=0; for(int i=0;i<256;++i) total+=bins[i];
    if(total==0) return {0,255};
    double sum=0; int low=0,high=255;
    while(low<254 && (sum+=bins[low])<total*0.005) ++low;
    sum=0; while(high>low+1 && (sum+=bins[high])<total*0.005) --high;
    return {low,high};
}
QImage contentAwareFill(const Layer &layer,const QPainterPath &selection) {
    if(layer.image.isNull() || selection.isEmpty()) throw std::runtime_error("Select an image region to fill.");
    auto pixels=layer.image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if(pixels.isNull()) throw std::runtime_error("Insufficient memory for content-aware fill.");
    QImage mask(pixels.size(),QImage::Format_Grayscale8); if(mask.isNull()) throw std::runtime_error("Insufficient memory for fill.");
    mask.fill(Qt::black);
    { QPainter p(&mask); p.setTransform(documentToPixels(layer,pixels.size())); p.fillPath(selection,Qt::white); }
    int result=content_fill(pixels.bits(),pixels.bytesPerLine(),mask.constBits(),mask.bytesPerLine(),pixels.width(),pixels.height());
    if(result!=1) throw std::runtime_error(result==0 ? "No unselected opaque source pixels are available to fill this region." : "Insufficient memory for content-aware fill.");
    auto filled=pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if(filled.isNull()) throw std::runtime_error("Insufficient memory for filled image.");
    return filled;
}
}
