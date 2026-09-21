#include "heif_import.h"
#include "document.h"
#include <libheif/heif.h>
#include <QColorSpace>
#include <QFile>
#include <memory>
#include <cstring>
#include <stdexcept>
namespace Arc {
namespace {
void check(heif_error error) {
    if(error.code!=heif_error_Ok) throw std::runtime_error(std::string("Cannot read HEIF image: ")+error.message);
}
void dimensions(int width,int height) {
    if(width<1 || height<1 || width>30000 || height>30000 || qint64(width)*height>MaxPixels)
        throw std::runtime_error("HEIF image exceeds supported dimensions (100 megapixels).");
}
}
QImage readHeif(const QString &path) {
    std::unique_ptr<heif_context,decltype(&heif_context_free)> context(heif_context_alloc(),heif_context_free);
    if(!context) throw std::runtime_error("Cannot allocate HEIF decoder.");
    auto limits=*heif_context_get_security_limits(context.get()); limits.max_image_size_pixels=MaxPixels;
    limits.max_color_profile_size=16*1024*1024; check(heif_context_set_security_limits(context.get(),&limits));
    heif_context_set_max_decoding_threads(context.get(),4);
    check(heif_context_read_from_file(context.get(),QFile::encodeName(path).constData(),nullptr));
    heif_image_handle *rawHandle=nullptr; check(heif_context_get_primary_image_handle(context.get(),&rawHandle));
    std::unique_ptr<heif_image_handle,decltype(&heif_image_handle_release)> handle(rawHandle,heif_image_handle_release);
    dimensions(heif_image_handle_get_width(handle.get()),heif_image_handle_get_height(handle.get()));
    heif_image *rawImage=nullptr;
    // libheif applies HEIF rotation/crop/mirror properties during decoding.
    check(heif_decode_image(handle.get(),&rawImage,heif_colorspace_RGB,heif_chroma_interleaved_RGBA,nullptr));
    std::unique_ptr<heif_image,decltype(&heif_image_release)> decoded(rawImage,heif_image_release);
    int width=heif_image_get_width(decoded.get(),heif_channel_interleaved),height=heif_image_get_height(decoded.get(),heif_channel_interleaved);
    dimensions(width,height); size_t stride=0;
    auto *pixels=heif_image_get_plane_readonly2(decoded.get(),heif_channel_interleaved,&stride);
    if(!pixels || stride<size_t(width)*4) throw std::runtime_error("Invalid HEIF pixel plane.");
    QImage result(width,height,heif_image_is_premultiplied_alpha(decoded.get()) ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGBA8888); if(result.isNull()) throw std::runtime_error("Insufficient memory for HEIF image.");
    for(int y=0;y<height;++y) std::memcpy(result.scanLine(y),pixels+size_t(y)*stride,size_t(width)*4);
    auto profileSize=heif_image_handle_get_raw_color_profile_size(handle.get());
    if(profileSize>0) {
        if(profileSize>16*1024*1024) throw std::runtime_error("HEIF color profile is too large.");
        QByteArray profile(qsizetype(profileSize),Qt::Uninitialized); check(heif_image_handle_get_raw_color_profile(handle.get(),profile.data()));
        result.setColorSpace(QColorSpace::fromIccProfile(profile));
        if(!result.colorSpace().isValid()) throw std::runtime_error("Unsupported HEIF color profile.");
    } else {
        heif_color_profile_nclx *rawProfile=nullptr;
        auto error=heif_image_handle_get_nclx_color_profile(handle.get(),&rawProfile);
        std::unique_ptr<heif_color_profile_nclx,decltype(&heif_nclx_color_profile_free)> profile(rawProfile,heif_nclx_color_profile_free);
        if(error.code==heif_error_Ok && profile) {
            using P=QColorSpace::Primaries; using T=QColorSpace::TransferFunction;
            P primaries=P::SRgb; T transfer=T::SRgb; float gamma=0;
            switch(profile->color_primaries) {
            case 1: case 2: break;
            case 9: primaries=P::Bt2020; break;
            case 12: primaries=P::DciP3D65; break;
            default: throw std::runtime_error("Unsupported HEIF color primaries. Convert this image to sRGB first.");
            }
            switch(profile->transfer_characteristics) {
            case 2: case 13: break;
            case 1: case 6: case 14: case 15: transfer=T::Bt2020; break;
            case 4: transfer=T::Gamma; gamma=2.2; break;
            case 5: transfer=T::Gamma; gamma=2.8; break;
            case 8: transfer=T::Linear; break;
            // The editor is 8-bit SDR; avoid silently misinterpreting HDR values.
            default: throw std::runtime_error("Unsupported HEIF transfer function. Convert HDR images to SDR first.");
            }
            result.setColorSpace(QColorSpace(primaries,transfer,gamma));
        } else if(error.code!=heif_error_Color_profile_does_not_exist) check(error);
    }
    return result;
}
}
