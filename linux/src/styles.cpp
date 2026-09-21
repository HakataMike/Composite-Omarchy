#include "styles.h"
#include <QJsonArray>
#include <QColorSpace>
#include <QTextLayout>
#include <QFont>
#include <cmath>
#include <stdexcept>

namespace Arc {
namespace {
void check(bool condition) {
    if (!condition) throw std::runtime_error("Invalid or unsupported shape/text properties.");
}
void keys(const QJsonObject &o, QStringList allowed) {
    for (auto it=o.begin(); it!=o.end(); ++it) check(allowed.contains(it.key()));
}
double number(const QJsonObject &o, QString key, double lo, double hi) {
    auto v=o[key]; check(v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=lo && v.toDouble()<=hi);
    return v.toDouble();
}
QPointF pair(QJsonValue value, double lo, double hi) {
    check(value.isArray() && value.toArray().size()==2);
    auto a=value.toArray(); QJsonObject o{{"x",a[0]},{"y",a[1]}};
    return {number(o,"x",lo,hi),number(o,"y",lo,hi)};
}
QColor color(const QJsonObject &o) {
    return QColor::fromRgbF(number(o,"red",0,1),number(o,"green",0,1),number(o,"blue",0,1));
}
QImage image(QSizeF size) {
    check(std::isfinite(size.width()) && std::isfinite(size.height()) && size.width()>=1 && size.height()>=1
        && size.width()<=30000 && size.height()<=30000);
    QSize pixels(std::ceil(size.width()),std::ceil(size.height()));
    check(qint64(pixels.width())*pixels.height()<=MaxPixels);
    QImage result(pixels,QImage::Format_ARGB32_Premultiplied);
    if(result.isNull()) throw std::runtime_error("Insufficient memory to render shape or text.");
    result.fill(Qt::transparent); result.setColorSpace(QColorSpace::SRgb); return result;
}
}
QJsonObject shapeStyle(QString kind, QColor c) {
    return {{"kind",kind},{"red",c.redF()},{"green",c.greenF()},{"blue",c.blueF()},{"cornerRadius",0}};
}
QJsonObject textStyle(QColor c) {
    return {{"content","Text"},{"fontName","Sans Serif"},{"fontSize",72},{"red",c.redF()},{"green",c.greenF()},
        {"blue",c.blueF()},{"alignment","Left"},{"tracking",0},{"leading",0}};
}
void validateStyles(const Layer &layer) {
    check(layer.shape.isEmpty() || layer.text.isEmpty());
    if(!layer.shape.isEmpty()) {
        const auto &s=layer.shape;
        keys(s,{"kind","red","green","blue","cornerRadius","lineWidth","start","end"});
        check(QStringList{"Rectangle","Ellipse","Line"}.contains(s["kind"].toString()));
        color(s); number(s,"cornerRadius",0,300000);
        if(s.contains("lineWidth")) number(s,"lineWidth",0.1,30000);
        if(s.contains("start")) pair(s["start"],0,1);
        if(s.contains("end")) pair(s["end"],0,1);
    }
    if(!layer.text.isEmpty()) {
        const auto &s=layer.text;
        keys(s,{"content","fontName","fontSize","red","green","blue","alignment","tracking","leading","boxSize"});
        check(s["content"].isString() && s["content"].toString().size()<=100000 && s["fontName"].isString()
            && !s["fontName"].toString().isEmpty() && s["fontName"].toString().size()<=1024);
        color(s); number(s,"fontSize",1,2000); number(s,"tracking",-100,1000); number(s,"leading",0,5000);
        check(QStringList{"Left","Center","Right"}.contains(s["alignment"].toString()));
        if(s.contains("boxSize")) { auto box=pair(s["boxSize"],16,30000); check(box.x()*box.y()<=MaxPixels); }
    }
}
QImage shapeImage(const QJsonObject &s, QSizeF size) {
    Layer probe; probe.shape=s; validateStyles(probe);
    auto result=image(size); QPainter p(&result); p.setRenderHint(QPainter::Antialiasing);
    p.scale(result.width()/size.width(),result.height()/size.height());
    QRectF bounds(QPointF(),size); p.setPen(Qt::NoPen); p.setBrush(color(s));
    if(s["kind"]=="Ellipse") p.drawEllipse(bounds);
    else if(s["kind"]=="Line") {
        auto a=s.contains("start") ? pair(s["start"],0,1) : QPointF(0,0);
        auto b=s.contains("end") ? pair(s["end"],0,1) : QPointF(1,1);
        p.setPen(QPen(color(s),s["lineWidth"].toDouble(1),Qt::SolidLine,Qt::RoundCap));
        p.drawLine(QPointF(a.x()*size.width(),a.y()*size.height()),QPointF(b.x()*size.width(),b.y()*size.height()));
    } else {
        double r=std::min({s["cornerRadius"].toDouble(),size.width()/2,size.height()/2});
        p.drawRoundedRect(bounds,r,r);
    }
    return result;
}
QImage textImage(const QJsonObject &s) {
    Layer probe; probe.text=s; validateStyles(probe);
    QFont font(s["fontName"].toString());
    double fontSize=s["fontSize"].toDouble();
    // Pixel sizes avoid display-DPI dependent project rendering.
    font.setPixelSize(std::max(1,int(std::round(fontSize))));
    font.setLetterSpacing(QFont::AbsoluteSpacing,s["tracking"].toDouble());
    QString text=s["content"].toString(); text.replace('\n',QChar::LineSeparator);
    QTextLayout layout(text,font);
    QTextOption options;
    bool fixed=s.contains("boxSize");
    options.setWrapMode(fixed ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    auto alignment=s["alignment"].toString();
    options.setAlignment(alignment=="Center" ? Qt::AlignHCenter : alignment=="Right" ? Qt::AlignRight : Qt::AlignLeft);
    layout.setTextOption(options);
    QPointF box=fixed ? pair(s["boxSize"],16,30000) : QPointF();
    double width=fixed ? std::max(1.0,box.x()-24) : 30000;
    double leading=s["leading"].toDouble(); if(leading==0) leading=fontSize*1.2;
    double height=0, naturalWidth=0;
    layout.beginLayout();
    while(true) {
        auto line=layout.createLine(); if(!line.isValid()) break;
        line.setLineWidth(width); line.setPosition({0,height}); height+=leading;
        naturalWidth=std::max(naturalWidth,line.naturalTextWidth());
        if(!fixed && (height>29976 || naturalWidth>29976)) { layout.endLayout(); throw std::runtime_error("Text exceeds supported image dimensions."); }
        if(fixed && height>box.y()) break;
    }
    if(!fixed) for(int i=0;i<layout.lineCount();++i) layout.lineAt(i).setLineWidth(std::max(1.0,naturalWidth));
    layout.endLayout();
    QSizeF size=fixed ? QSizeF(box.x(),box.y()) : QSizeF(std::max(25.0,naturalWidth+24),std::max(fontSize+24,height+24));
    auto result=image(size); QPainter p(&result); p.setPen(color(s)); layout.draw(&p,{12,12}); return result;
}
void rasterize(Layer &layer) { layer.shape={}; layer.text={}; }
}
