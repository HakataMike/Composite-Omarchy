#include "adjustments.h"
#include "blending.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include "kernels/LevelsPixels.h"
}
namespace Arc {
namespace {
const QStringList channels{"RGB","Red","Green","Blue"};
const QStringList ranges{"Master","Reds","Yellows","Greens","Cyans","Blues","Magentas"};
void check(bool ok) { if(!ok) throw std::runtime_error("Invalid or unsupported adjustment layer settings."); }
void keys(const QJsonObject &o, QStringList allowed) { for(auto i=o.begin();i!=o.end();++i) check(allowed.contains(i.key())); }
double number(const QJsonObject &o,QString key,double lo,double hi) {
    auto v=o[key]; check(v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=lo && v.toDouble()<=hi); return v.toDouble();
}
QJsonObject dictionary(QJsonValue value) {
    // Swift dictionaries keyed by Codable enums use alternating key/value arrays.
    if(value.isObject()) return value.toObject();
    check(value.isArray()); auto array=value.toArray(); check(array.size()%2==0);
    QJsonObject result;
    for(int i=0;i<array.size();i+=2) { check(array[i].isString() && !result.contains(array[i].toString())); result[array[i].toString()]=array[i+1]; }
    return result;
}
QJsonArray encodedDictionary(const QJsonObject &o) {
    QJsonArray result; for(auto i=o.begin();i!=o.end();++i) { result.append(i.key()); result.append(i.value()); } return result;
}
QJsonObject band(QString range) {
    double center=std::max(0,int(ranges.indexOf(range))-1)*60;
    auto wrap=[](double n) { return std::fmod(n+360,360); };
    return {{"falloffStart",wrap(center-45)},{"rangeStart",wrap(center-15)},{"rangeEnd",wrap(center+15)},{"falloffEnd",wrap(center+45)}};
}
QJsonObject hsv(const QJsonObject &a) {
    if(a.contains("hsvSettings") && !a["hsvSettings"].isNull()) return a["hsvSettings"].toObject();
    QJsonObject adjustment{{"hue",a["hue"]},{"saturation",a["saturation"]},{"lightness",a["lightness"]}};
    return {{"range","Master"},{"colorize",a["colorize"]},{"invertRange",false},
        {"adjustments",encodedDictionary({{"Master",adjustment}})},{"bands",QJsonArray()}};
}
QColor color(const QJsonObject &o) {
    keys(o,{"red","green","blue"});
    return QColor::fromRgbF(number(o,"red",0,1),number(o,"green",0,1),number(o,"blue",0,1));
}
QJsonObject encodedColor(QColor c) { return {{"red",c.redF()},{"green",c.greenF()},{"blue",c.blueF()}}; }
QVector<QPointF> points(QJsonValue value) {
    check(value.isArray()); auto array=value.toArray(); check(array.size()>=2 && array.size()<=32);
    QVector<QPointF> result; double previous=-1;
    for(auto p:array) {
        check(p.isObject()); auto o=p.toObject(); keys(o,{"x","y"});
        double x=number(o,"x",0,255),y=number(o,"y",0,255); check(x>previous); previous=x; result.append({x,y});
    }
    check(result.first().x()==0 && result.last().x()==255); return result;
}
}
bool hasAdjustments(const Document &d) { for(const auto &l:d.layers) if(!l.adjustment.isEmpty()) return true; return false; }
QStringList adjustmentKinds() { return {"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain"}; }
QJsonObject defaultAdjustment(const QString &kind) {
    check(adjustmentKinds().contains(kind));
    QJsonObject level=defaultFilter("Levels").values; QJsonArray levelRanges,curves;
    for(int i=0;i<4;++i) { levelRanges.append(level); curves.append(QJsonArray{QJsonObject{{"x",0},{"y",0}},QJsonObject{{"x",255},{"y",255}}}); }
    return {{"kind",kind},{"hue",0},{"saturation",0},{"lightness",0},{"colorize",false},
        {"levels",QJsonObject{{"channel","RGB"},{"ranges",levelRanges}}},
        {"curves",QJsonObject{{"channel","RGB"},{"channels",curves}}}};
}
void validateAdjustment(const QJsonObject &a) {
    keys(a,{"kind","hue","saturation","lightness","colorize","levels","curves","hsvSettings","exposureSettings","gradientMapSettings","grainSettings"});
    check(adjustmentKinds().contains(a["kind"].toString()));
    number(a,"hue",-360,360); number(a,"saturation",-100,100); number(a,"lightness",-100,100); check(a["colorize"].isBool());
    auto levels=a["levels"].toObject(); keys(levels,{"channel","ranges"});
    check(channels.contains(levels["channel"].toString()) && levels["ranges"].isArray() && levels["ranges"].toArray().size()==4);
    for(auto value:levels["ranges"].toArray()) {
        check(value.isObject()); auto r=value.toObject(); keys(r,{"black","white","gamma","outputBlack","outputWhite"});
        double black=number(r,"black",0,254); number(r,"white",black+1,255); number(r,"gamma",0.1,9.99);
        number(r,"outputBlack",0,255); number(r,"outputWhite",0,255);
    }
    auto curves=a["curves"].toObject(); keys(curves,{"channel","channels"});
    check(channels.contains(curves["channel"].toString()) && curves["channels"].isArray() && curves["channels"].toArray().size()==4);
    for(auto curve:curves["channels"].toArray()) points(curve);
    auto h=hsv(a); keys(h,{"range","colorize","invertRange","adjustments","bands"});
    check(ranges.contains(h["range"].toString()) && h["colorize"].isBool() && h["invertRange"].isBool());
    auto adjustments=dictionary(h["adjustments"]),bands=dictionary(h["bands"]);
    for(auto i=adjustments.begin();i!=adjustments.end();++i) {
        check(ranges.contains(i.key()) && i.value().isObject()); auto v=i.value().toObject(); keys(v,{"hue","saturation","lightness"});
        number(v,"hue",-360,360); number(v,"saturation",-100,100); number(v,"lightness",-100,100);
    }
    for(auto i=bands.begin();i!=bands.end();++i) {
        check(ranges.contains(i.key()) && i.value().isObject()); auto v=i.value().toObject();
        keys(v,{"falloffStart","rangeStart","rangeEnd","falloffEnd"});
        for(auto key:{"falloffStart","rangeStart","rangeEnd","falloffEnd"}) number(v,key,-1e12,1e12);
    }
    for(auto field:{"exposureSettings","grainSettings"}) if(a.contains(field) && !a[field].isNull()) {
        check(a[field].isObject()); auto object=a[field].toObject(); QString kind=QString(field)=="exposureSettings" ? "Exposure" : "Grain";
        QStringList allowed;
        for(auto p:filterParameters(kind)) { allowed.append(p.key); number(object,p.key,p.minimum,p.maximum); }
        if(kind=="Grain") { allowed.append("seed"); double seed=number(object,"seed",0,4294967295.0); check(seed==std::floor(seed)); }
        keys(object,allowed);
    }
    if(a.contains("gradientMapSettings") && !a["gradientMapSettings"].isNull()) {
        check(a["gradientMapSettings"].isObject()); auto g=a["gradientMapSettings"].toObject(); keys(g,{"shadows","highlights","reversed"});
        color(g["shadows"].toObject()); color(g["highlights"].toObject()); check(g["reversed"].isBool());
    }
}
FilterSettings filterFromAdjustment(const QJsonObject &a, int selected, QString range) {
    validateAdjustment(a); auto f=defaultFilter(a["kind"].toString());
    if(f.kind=="Levels" || f.kind=="Curves") {
        auto settings=a[f.kind=="Levels" ? "levels" : "curves"].toObject();
        f.channel=selected<0 ? channels.indexOf(settings["channel"].toString()) : selected;
        check(f.channel>=0 && f.channel<4);
        if(f.kind=="Levels") f.values=settings["ranges"].toArray()[f.channel].toObject();
        else f.curve=points(settings["channels"].toArray()[f.channel]);
    } else if(f.kind=="Hue/Saturation") {
        auto h=hsv(a); if(range.isEmpty()) range=h["range"].toString();
        auto values=dictionary(h["adjustments"]).value(range).toObject();
        for(auto key:{"hue","saturation","lightness"}) f.values[key]=values[key].toDouble();
        f.values["range"]=range; f.values["colorize"]=h["colorize"]; f.values["invertRange"]=h["invertRange"];
        auto b=dictionary(h["bands"]).value(range).toObject(); if(b.isEmpty()) b=band(range);
        for(auto key:{"falloffStart","rangeStart","rangeEnd","falloffEnd"}) f.values[key]=b[key];
    } else if(f.kind=="Exposure" || f.kind=="Grain") {
        auto settings=a[f.kind=="Exposure" ? "exposureSettings" : "grainSettings"].toObject();
        if(!settings.isEmpty()) { for(auto p:filterParameters(f.kind)) f.values[p.key]=settings[p.key]; f.seed=quint32(settings["seed"].toDouble()); }
    } else {
        auto g=a["gradientMapSettings"].toObject();
        if(!g.isEmpty()) { f.shadows=color(g["shadows"].toObject()); f.highlights=color(g["highlights"].toObject()); f.values["reversed"]=g["reversed"]; }
    }
    return f;
}
QJsonObject adjustmentFromFilter(const FilterSettings &f, const QJsonObject &original) {
    auto a=original.isEmpty() ? defaultAdjustment(f.kind) : original;
    validateAdjustment(a); check(a["kind"].toString()==f.kind && f.channel>=0 && f.channel<4);
    if(f.kind=="Levels" || f.kind=="Curves") {
        QString field=f.kind=="Levels" ? "levels" : "curves",list=f.kind=="Levels" ? "ranges" : "channels";
        auto settings=a[field].toObject(); auto values=settings[list].toArray();
        if(f.kind=="Levels") values[f.channel]=f.values;
        else { QJsonArray curve; for(auto point:f.curve) curve.append(QJsonObject{{"x",point.x()},{"y",point.y()}}); values[f.channel]=curve; }
        settings[list]=values; settings["channel"]=channels[f.channel]; a[field]=settings;
    } else if(f.kind=="Hue/Saturation") {
        auto h=hsv(a); QString range=f.values["range"].toString("Master");
        auto values=dictionary(h["adjustments"]),bands=dictionary(h["bands"]);
        values[range]=QJsonObject{{"hue",f.values["hue"]},{"saturation",f.values["saturation"]},{"lightness",f.values["lightness"]}};
        auto b=band(range); for(auto key:{"falloffStart","rangeStart","rangeEnd","falloffEnd"}) if(f.values.contains(key)) b[key]=f.values[key];
        bands[range]=b; h["range"]=range; h["colorize"]=f.values["colorize"].toBool(); h["invertRange"]=f.values["invertRange"].toBool();
        h["adjustments"]=encodedDictionary(values); h["bands"]=encodedDictionary(bands); a["hsvSettings"]=h;
    } else if(f.kind=="Exposure" || f.kind=="Grain") {
        auto settings=f.values; if(f.kind=="Grain") settings["seed"]=double(f.seed);
        a[f.kind=="Exposure" ? "exposureSettings" : "grainSettings"]=settings;
    } else a["gradientMapSettings"]=QJsonObject{{"shadows",encodedColor(f.shadows)},{"highlights",encodedColor(f.highlights)},{"reversed",f.values["reversed"].toBool()}};
    validateAdjustment(a); return a;
}
QImage adjustedImage(const QImage &image, const QJsonObject &a) {
    validateAdjustment(a); auto f=filterFromAdjustment(a);
    if(f.kind=="Levels" || f.kind=="Curves") {
        auto imageOut=image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        if(imageOut.isNull()) throw std::runtime_error("Insufficient memory for adjustment.");
        float table[768]; auto all=a[f.kind=="Levels" ? "levels" : "curves"].toObject();
        auto entries=all[f.kind=="Levels" ? "ranges" : "channels"].toArray();
        QVector<QVector<QPointF>> curves;
        if(f.kind=="Curves") for(auto entry:entries) curves.append(points(entry));
        auto level=[](QJsonObject r,double x) {
            return r["outputBlack"].toDouble()+std::pow(std::clamp((x-r["black"].toDouble())/(r["white"].toDouble()-r["black"].toDouble()),0.0,1.0),
                1/r["gamma"].toDouble())*(r["outputWhite"].toDouble()-r["outputBlack"].toDouble());
        };
        for(int c=0;c<3;++c) for(int i=0;i<256;++i) table[c*256+i]=(f.kind=="Levels"
            ? level(entries[0].toObject(),level(entries[c+1].toObject(),i))
            : evaluateCurve(curves[0],evaluateCurve(curves[c+1],i)))/255;
        levels_apply(imageOut.bits(),size_t(imageOut.width())*imageOut.height(),table);
        return imageOut.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if(f.kind=="Hue/Saturation") {
        auto h=hsv(a),values=dictionary(h["adjustments"]),bands=dictionary(h["bands"]);
        struct Response { double hue=0,saturation=0,lightness=0; }; QVector<Response> response(361);
        auto forward=[](double from,double to) { double v=std::fmod(to-from,360); return v<0 ? v+360 : v; };
        for(int degree=0;degree<=360;++degree) for(auto i=values.begin();i!=values.end();++i) {
            double weight=1;
            if(i.key()!="Master") {
                auto b=bands.value(i.key()).toObject(); if(b.isEmpty()) b=band(i.key());
                double start=b["falloffStart"].toDouble(),span=forward(start,b["falloffEnd"].toDouble()),pos=forward(start,degree);
                double ramp=forward(start,b["rangeStart"].toDouble()),plateau=forward(start,b["rangeEnd"].toDouble());
                if(span>0) weight=pos>span ? 0 : pos<ramp ? pos/ramp : pos<=plateau ? 1 : (span>plateau ? (span-pos)/(span-plateau) : 1);
                if(h["invertRange"].toBool() && i.key()==h["range"].toString()) weight=1-weight;
            }
            auto value=i.value().toObject(); response[degree].hue+=weight*value["hue"].toDouble();
            response[degree].saturation+=weight*value["saturation"].toDouble(); response[degree].lightness+=weight*value["lightness"].toDouble();
        }
        auto result=image.convertToFormat(QImage::Format_ARGB32); if(result.isNull()) throw std::runtime_error("Insufficient memory for hue adjustment.");
        auto selected=values[h["range"].toString()].toObject();
        for(int y=0;y<result.height();++y) {
            auto *row=reinterpret_cast<QRgb *>(result.scanLine(y));
            for(int x=0;x<result.width();++x) {
                QColor c=QColor::fromRgba(row[x]); if(!c.alpha()) continue;
                float hue,sat,light,alpha; c.getHslF(&hue,&sat,&light,&alpha);
                double degrees=std::max(0.0,double(hue))*360;
                auto delta=response[std::clamp(qRound(degrees),0,360)]; double amount;
                if(h["colorize"].toBool()) { degrees=selected["hue"].toDouble(); sat=std::clamp(selected["saturation"].toDouble()/100,0.0,1.0); amount=selected["lightness"].toDouble()/100; }
                else { degrees+=delta.hue; sat=std::clamp(sat*(1+delta.saturation/100),0.0,1.0); amount=delta.lightness/100; }
                amount=std::clamp(amount,-1.0,1.0); light=amount>=0 ? light+(1-light)*amount : light*(1+amount);
                degrees=forward(0,degrees); row[x]=QColor::fromHslF(degrees/360,sat,std::clamp(double(light),0.0,1.0),alpha).rgba();
            }
        }
        return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if(f.kind=="Gradient Map" && f.values["reversed"].toBool()) std::swap(f.shadows,f.highlights);
    return applyFilter(image,f);
}
void compositeAdjustment(QImage &image, const Layer &layer, const QImage &coverage) {
    auto changed=adjustedImage(image,layer.adjustment);
    if(layer.blend!="Normal") {
        auto base=image,top=changed;
        for(int y=0;y<image.height();++y) {
            auto *b=reinterpret_cast<QRgb *>(base.scanLine(y)),*t=reinterpret_cast<QRgb *>(top.scanLine(y));
            for(int x=0;x<image.width();++x) { b[x]=qUnpremultiply(b[x])|0xff000000; t[x]=qUnpremultiply(t[x])|0xff000000; }
        }
        compositeImages(base,top,layer.blend); changed=base;
    }
    for(int y=0;y<image.height();++y) {
        auto *row=reinterpret_cast<QRgb *>(image.scanLine(y)); const auto *next=reinterpret_cast<const QRgb *>(changed.constScanLine(y));
        const auto *mask=reinterpret_cast<const QRgb *>(coverage.constScanLine(y));
        for(int x=0;x<image.width();++x) {
            int alpha=qAlpha(row[x]),weight=qAlpha(mask[x]); QRgb target=next[x];
            if(layer.blend!="Normal") target=qPremultiply(qRgba(qRed(target),qGreen(target),qBlue(target),alpha));
            auto mix=[&](int a,int b) { return (a*(255-weight)+b*weight+127)/255; };
            row[x]=qRgba(mix(qRed(row[x]),qRed(target)),mix(qGreen(row[x]),qGreen(target)),mix(qBlue(row[x]),qBlue(target)),alpha);
        }
    }
}
}
