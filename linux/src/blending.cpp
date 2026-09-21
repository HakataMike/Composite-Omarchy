#include "blending.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Arc {
namespace {
using Color = std::array<double,3>;
double luminosity(Color c) { return 0.3*c[0]+0.59*c[1]+0.11*c[2]; }
double saturation(Color c) { return *std::max_element(c.begin(),c.end())-*std::min_element(c.begin(),c.end()); }
Color withLuminosity(Color c, double value) {
    double delta=value-luminosity(c);
    for(auto &channel:c) channel+=delta;
    double low=*std::min_element(c.begin(),c.end());
    double high=*std::max_element(c.begin(),c.end());
    if(low<0) for(auto &channel:c) channel=value+(channel-value)*value/(value-low);
    if(high>1) for(auto &channel:c) channel=value+(channel-value)*(1-value)/(high-value);
    return c;
}
Color withSaturation(Color c, double value) {
    double low=*std::min_element(c.begin(),c.end());
    double range=saturation(c);
    for(auto &channel:c) channel=range>0 ? (channel-low)*value/range : 0;
    return c;
}
int modeIndex(const QString &mode) {
    if(mode=="Hue") return 0;
    if(mode=="Saturation") return 1;
    if(mode=="Color") return 2;
    if(mode=="Luminosity") return 3;
    return -1;
}
}
bool isNonseparableBlend(const QString &mode) { return modeIndex(mode)>=0; }
void blendNonseparable(QImage &backdrop, const QImage &source, const QString &mode) {
    int kind=modeIndex(mode);
    if(kind<0 || backdrop.size()!=source.size() || backdrop.format()!=QImage::Format_ARGB32_Premultiplied
        || source.format()!=QImage::Format_ARGB32_Premultiplied)
        throw std::runtime_error("Invalid blend surface or mode.");
    // Nonseparable blend and source-over equations:
    // https://www.w3.org/TR/compositing-1/#blendingnonseparable
    // Work in straight sRGB for blending, then combine premultiplied contributions.
    backdrop.detach();
    if(backdrop.isNull()) throw std::runtime_error("Insufficient memory for blending.");
    for(int y=0;y<backdrop.height();++y) {
        auto *dst=reinterpret_cast<QRgb *>(backdrop.scanLine(y));
        const auto *src=reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for(int x=0;x<backdrop.width();++x) {
            QRgb s=src[x],b=dst[x]; int sa=qAlpha(s),ba=qAlpha(b);
            if(!sa) continue;
            if(!ba) { dst[x]=s; continue; }
            Color cs{double(qRed(s))/sa,double(qGreen(s))/sa,double(qBlue(s))/sa};
            Color cb{double(qRed(b))/ba,double(qGreen(b))/ba,double(qBlue(b))/ba};
            Color mixed;
            if(kind==0) mixed=withLuminosity(withSaturation(cs,saturation(cb)),luminosity(cb));
            else if(kind==1) mixed=withLuminosity(withSaturation(cb,saturation(cs)),luminosity(cb));
            else if(kind==2) mixed=withLuminosity(cs,luminosity(cb));
            else mixed=withLuminosity(cb,luminosity(cs));
            double as=sa/255.0,ab=ba/255.0;
            int alpha=std::clamp(int(std::round((as+ab-as*ab)*255)),0,255);
            std::array<int,3> out;
            for(int i=0;i<3;++i) out[i]=std::clamp(int(std::round(255*((1-as)*ab*cb[i]+(1-ab)*as*cs[i]+as*ab*mixed[i]))),0,alpha);
            dst[x]=qRgba(out[0],out[1],out[2],alpha);
        }
    }
}
}
