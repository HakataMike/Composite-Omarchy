#include "document.h"
#include "styles.h"
#include "blending.h"
#include "hierarchy.h"
#include <algorithm>
#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <cmath>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace Arc {
namespace {
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
void require(bool condition, const QString &message) { if (!condition) fail(message); }
bool validSize(QSize size) {
    return size.width() > 0 && size.height() > 0 && size.width() <= 30000 && size.height() <= 30000
        && qint64(size.width()) * size.height() <= MaxPixels;
}
const QVector<QPainter::CompositionMode> modes = {
    QPainter::CompositionMode_SourceOver, QPainter::CompositionMode_Multiply,
    QPainter::CompositionMode_Screen, QPainter::CompositionMode_Overlay,
    QPainter::CompositionMode_Darken, QPainter::CompositionMode_Lighten,
    QPainter::CompositionMode_Difference, QPainter::CompositionMode_ColorDodge,
    QPainter::CompositionMode_ColorBurn, QPainter::CompositionMode_SoftLight
};
void keys(const QJsonObject &object, const QStringList &allowed) {
    for (auto i = object.begin(); i != object.end(); ++i)
        require(allowed.contains(i.key()), "Unsupported project field: " + i.key() + ". Open this project in the macOS app.");
}
double number(const QJsonValue &value) {
    require(value.isDouble() && std::isfinite(value.toDouble()), "Invalid numeric project field.");
    return value.toDouble();
}
int integer(const QJsonValue &value) {
    double v = number(value);
    require(v >= 0 && v <= 1000000 && std::floor(v) == v, "Invalid integer project field.");
    return int(v);
}
bool boolean(const QJsonValue &value) {
    require(value.isBool(), "Invalid boolean project field."); return value.toBool();
}
QJsonArray pair(const QJsonValue &value) {
    require(value.isArray() && value.toArray().size() == 2, "Invalid transform coordinates.");
    return value.toArray();
}
QUuid uuid(const QJsonValue &value) {
    require(value.isString() && !QUuid(value.toString()).isNull(), "Invalid project UUID.");
    return QUuid(value.toString());
}
bool hasNonseparableBlend(const Document &d) {
    return std::any_of(d.layers.begin(),d.layers.end(),[](const auto &l) { return l.visible && !l.image.isNull() && isNonseparableBlend(l.blend); });
}
QString idString(QUuid id) { return id.toString(QUuid::WithoutBraces).toUpper(); }
void safeFile(const QString &path, const QString &root, qint64 limit) {
    QFileInfo info(path);
    require(info.isFile() && !info.isSymLink() && info.size() <= limit
        && info.canonicalFilePath().startsWith(root + '/'), "Missing, unsafe, or oversized project asset: " + info.fileName());
}
}
QSize maskEditingSize(const Layer &layer) {
    if(!layer.image.isNull()) return layer.image.size();
    QSize result=layer.size.toSize();
    require(validSize(result), "Mask exceeds supported dimensions.");
    return result;
}
QStringList blendModes() {
    return {"Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Color Dodge", "Color Burn", "Soft Light", "Hue", "Saturation", "Color", "Luminosity"};
}
bool Layer::operator==(const Layer &o) const {
    return id == o.id && parentID == o.parentID && isGroup == o.isGroup && name == o.name && image == o.image && origin == o.origin && size == o.size
        && rotation == o.rotation && flipX == o.flipX && flipY == o.flipY && visible == o.visible
        && shape == o.shape && text == o.text && mask == o.mask && maskEnabled == o.maskEnabled
        && opacity == o.opacity && blend == o.blend && sampling == o.sampling;
}
bool Document::operator==(const Document &o) const {
    return id == o.id && size == o.size && resolution == o.resolution && layers == o.layers && active == o.active && guides == o.guides;
}
QTransform Layer::transform() const {
    QTransform t;
    t.translate(origin.x() + size.width()/2, origin.y() + size.height()/2);
    t.rotate(rotation);
    t.scale(flipX ? -1 : 1, flipY ? -1 : 1);
    t.translate(-size.width()/2, -size.height()/2);
    return t;
}
void validate(const Document &d) {
    require(!d.id.isNull() && validSize(d.size), "Canvas exceeds the 30,000-side or 100-megapixel limit.");
    require(std::isfinite(d.resolution) && d.resolution >= 1 && d.resolution <= 9600, "Invalid resolution.");
    require(d.layers.size() <= 10000 && d.active >= -1 && d.active < d.layers.size(), "Invalid layer list.");
    require(d.guides.size() <= 1000, "Too many guides (maximum 1,000).");
    QSet<QUuid> guideIds;
    for (const auto &guide : d.guides) {
        require(!guide.id.isNull() && !guideIds.contains(guide.id)
            && (guide.axis == "horizontal" || guide.axis == "vertical")
            && std::isfinite(guide.position) && std::abs(guide.position) <= 1000000, "Invalid guide.");
        guideIds.insert(guide.id);
    }
    validateHierarchy(d);
    QSet<QUuid> ids;
    qint64 pixels = 0, maskPixels = 0;
    for (const auto &l : d.layers) {
        validateStyles(l);
        require(!l.id.isNull() && !ids.contains(l.id), "Duplicate or invalid layer ID."); ids.insert(l.id);
        require(std::isfinite(l.origin.x()) && std::isfinite(l.origin.y()) && std::abs(l.origin.x()) <= 1000000
            && std::abs(l.origin.y()) <= 1000000 && std::isfinite(l.rotation)
            && std::isfinite(l.size.width()) && std::isfinite(l.size.height())
            && l.size.width() >= 1 && l.size.height() >= 1 && l.size.width() <= 300000 && l.size.height() <= 300000,
            "Invalid layer transform.");
        require(std::isfinite(l.opacity) && l.opacity >= 0 && l.opacity <= 1 && blendModes().contains(l.blend)
            && QStringList{"Nearest", "Smooth", "High quality"}.contains(l.sampling), "Unsupported layer appearance.");
        if (!l.image.isNull()) {
            require(validSize(l.image.size()), "Image exceeds supported dimensions.");
            pixels += qint64(l.image.width()) * l.image.height();
        }
        if (!l.mask.isNull()) {
            require(validSize(l.mask.size()) && l.mask.format() == QImage::Format_Grayscale8, "Invalid grayscale mask.");
            maskPixels += qint64(l.mask.width()) * l.mask.height();
            require(maskPixels <= MaxPixels, "Combined masks exceed 100 megapixels.");
        }
        require(pixels <= MaxPixels, "Combined source images exceed 100 megapixels.");
    }
}
QImage readImage(const QString &path) {
    require(QFileInfo(path).size() <= 512LL * 1024 * 1024, "Image exceeds 512 MiB.");
    QImageReader reader(path);
    reader.setAutoTransform(true);
    require(validSize(reader.size()), "Unsupported image or dimensions (maximum 100 megapixels).");
    QImage image = reader.read();
    require(!image.isNull(), "Cannot read image: " + reader.errorString());
    if (image.colorSpace().isValid()) image.convertToColorSpace(QColorSpace::SRgb);
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    require(!image.isNull(), "Insufficient memory for image.");
    image.setColorSpace(QColorSpace::SRgb);
    return image;
}
void paint(QPainter &p, const Document &d, QRect region) {
    if (region.isNull()) region = QRect(QPoint(), d.size);
    p.save();
    p.setClipRect(region.intersected(QRect(QPoint(), d.size)), Qt::IntersectClip);
    if(std::any_of(d.layers.begin(),d.layers.end(),[](const auto &l) { return l.isGroup; })) { paint(p,flattenGroups(d),region); p.restore(); return; }
    if(hasNonseparableBlend(d)) {
        p.drawImage(QPoint(),render(d)); p.restore(); return;
    }
    for (const auto &l : d.layers) {
        if (!l.visible || l.image.isNull()) continue;
        p.save();
        p.setTransform(l.transform(), true);
        p.setOpacity(l.opacity);
        p.setCompositionMode(modes.at(blendModes().indexOf(l.blend)));
        p.setRenderHint(QPainter::SmoothPixmapTransform, l.sampling != "Nearest");
        if (l.mask.isNull() || !l.maskEnabled) {
            p.drawImage(QRectF(QPointF(), l.size), l.image);
        } else {
            QTransform sourceToDocument = l.transform();
            sourceToDocument.scale(l.size.width()/l.image.width(), l.size.height()/l.image.height());
            // Keep neighboring samples for pixel-aligned crops. Transformed layers
            // retain the full source domain to match Qt's export interpolation.
            const bool transformed = sourceToDocument.type() > QTransform::TxTranslate
                || l.origin.x() != std::floor(l.origin.x()) || l.origin.y() != std::floor(l.origin.y());
            QRect sourceRect = transformed ? l.image.rect() : sourceToDocument.inverted().mapRect(QRectF(region))
                .intersected(QRectF(l.image.rect())).toAlignedRect().adjusted(-2,-2,2,2).intersected(l.image.rect());
            if (!sourceRect.isEmpty()) {
                QImage pixels = l.image.copy(sourceRect).convertToFormat(QImage::Format_ARGB32_Premultiplied);
                require(!pixels.isNull(), "Insufficient memory for masked image.");
                QImage scaledMask;
                const bool uniform = l.mask.size() == QSize(1,1);
                const bool sameSize = l.mask.size() == l.image.size();
                if (!uniform && !sameSize) {
                    scaledMask = QImage(sourceRect.size(), QImage::Format_RGB32);
                    require(!scaledMask.isNull(), "Insufficient memory for mask rendering.");
                    QPainter maskPainter(&scaledMask);
                    maskPainter.translate(-sourceRect.topLeft());
                    maskPainter.setRenderHint(QPainter::SmoothPixmapTransform);
                    maskPainter.drawImage(QRect(QPoint(),l.image.size()),l.mask);
                }
                for (int y = 0; y < pixels.height(); ++y) {
                    auto *row = reinterpret_cast<QRgb *>(pixels.scanLine(y));
                    for (int x = 0; x < pixels.width(); ++x) {
                        unsigned c = uniform ? l.mask.constScanLine(0)[0]
                            : sameSize ? l.mask.constScanLine(y+sourceRect.y())[x+sourceRect.x()]
                            : qRed(reinterpret_cast<const QRgb *>(scaledMask.constScanLine(y))[x]);
                        const QRgb v = row[x];
                        row[x] = qRgba((qRed(v)*c+127)/255,(qGreen(v)*c+127)/255,
                                       (qBlue(v)*c+127)/255,(qAlpha(v)*c+127)/255);
                    }
                }
                double sx = l.size.width()/l.image.width(), sy = l.size.height()/l.image.height();
                p.scale(sx,sy);
                p.drawImage(sourceRect.topLeft(),pixels);
            }
        }
        p.restore();
    }
    p.restore();
}
void repaintRegion(QImage &image, const Document &d, QRect region) {
    validate(d);
    require(image.size() == d.size && image.format() == QImage::Format_ARGB32_Premultiplied,
            "Invalid canvas preview cache.");
    region = region.intersected(QRect(QPoint(),d.size));
    if (region.isEmpty()) return;
    if(hasNonseparableBlend(d) || std::any_of(d.layers.begin(),d.layers.end(),[](const auto &l) { return l.isGroup; })) { image=render(d); return; }
    for (const auto &layer : d.layers) {
        if (!layer.visible || layer.image.isNull()) continue;
        if (layer.rotation != 0 || layer.flipX || layer.flipY || layer.size != QSizeF(layer.image.size())
            || layer.origin.x() != std::floor(layer.origin.x()) || layer.origin.y() != std::floor(layer.origin.y())) {
            // Qt's clipped transformed sampling can differ by one channel level.
            // Preserve exact preview/export agreement until a shared tile sampler exists.
            image = render(d);
            return;
        }
    }
    // Keep the global raster origin: translating a rotated layer onto a temporary
    // regional image can change Qt's fixed-point interpolation by one level.
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(region, Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    paint(painter,d,region);
}
QImage render(const Document &d, bool whiteBackground) {
    validate(d);
    if(std::any_of(d.layers.begin(),d.layers.end(),[](const auto &l) { return l.isGroup; })) return render(flattenGroups(d),whiteBackground);
    QImage result(d.size, QImage::Format_ARGB32_Premultiplied);
    require(!result.isNull(), "Insufficient memory to render canvas.");
    result.fill(Qt::transparent);
    if(hasNonseparableBlend(d)) {
        // Only modes absent from QPainter need an intermediate source surface.
        // Keep ordinary layers on the existing painter path for identical sampling.
        Document single=d; single.layers.clear(); single.active=0;
        for(const auto &layer:d.layers) {
            if(!layer.visible || layer.image.isNull()) continue;
            single.layers={layer};
            if(isNonseparableBlend(layer.blend)) {
                single.layers[0].blend="Normal";
                auto source=render(single);
                blendNonseparable(result,source,layer.blend);
            } else { QPainter p(&result); paint(p,single); }
        }
    } else { QPainter p(&result); paint(p, d); }
    if (whiteBackground) {
        QPainter p(&result);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
        p.fillRect(result.rect(), Qt::white);
    }
    result.setColorSpace(QColorSpace::SRgb);
    result.setDotsPerMeterX(qRound(d.resolution / 0.0254));
    result.setDotsPerMeterY(qRound(d.resolution / 0.0254));
    return result;
}
Document loadProject(const QString &path) {
    const QString root = QFileInfo(path).canonicalFilePath();
    require(QFileInfo(path).isDir() && !QFileInfo(path).isSymLink(), "Choose a .comp project folder.");
    safeFile(path + "/manifest.json", root, 4 * 1024 * 1024);
    QFile file(path + "/manifest.json");
    require(file.open(QIODevice::ReadOnly), "Cannot open project manifest.");
    QJsonParseError error;
    auto json = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && json.isObject(), "Invalid project JSON.");
    auto m = json.object();
    require(m["format"] == "com.compositor.project", "Not a Compositor project.");
    int version = integer(m["version"]);
    require(version >= 1 && version <= 8, "Unsupported project version (expected v1–8).");
    keys(m, {"format", "version", "colorSpace", "resolution", "documentID", "width", "height", "activeLayerID", "layers", "guides"});
    require(m["colorSpace"] == "sRGB" && m["layers"].isArray(), "Invalid project metadata.");
    Document d;
    d.id = uuid(m["documentID"]);
    if (m.contains("guides") && !m["guides"].isNull()) {
        require(m["guides"].isArray() && m["guides"].toArray().size() <= 1000, "Invalid guide list.");
        auto guides = m["guides"].toArray();
        require(version >= 8 || guides.isEmpty(), "Guides require project version 8.");
        for (const auto &value : guides) {
            require(value.isObject(), "Invalid guide record.");
            auto record = value.toObject(); keys(record, {"id", "axis", "position"});
            Guide guide; guide.id = uuid(record["id"]); guide.axis = record["axis"].toString();
            guide.position = number(record["position"]); d.guides.append(guide);
        }
    }
    d.size = QSize(integer(m["width"]), integer(m["height"]));
    if (m.contains("resolution")) d.resolution = number(m["resolution"]);
    validate(d);
    require(m["layers"].toArray().size() <= 10000, "Too many layers.");
    QVector<QString> files, maskFiles;
    for (const auto value : m["layers"].toArray()) {
        require(value.isObject(), "Invalid layer record.");
        const auto r = value.toObject();
        keys(r, {"id", "name", "isVisible", "transform", "imageFile", "opacity", "blendMode", "parentID", "isGroup", "maskFile", "maskEnabled", "shape", "text"});
        Layer l;
        if(r.contains("parentID") && !r["parentID"].isNull()) l.parentID=uuid(r["parentID"]);
        if(r.contains("isGroup")) l.isGroup=boolean(r["isGroup"]);
        require(version>=2 || (!l.isGroup && l.parentID.isNull()), "Folders require project version 2.");
        l.id = uuid(r["id"]);
        require(r["name"].isString() && r["transform"].isObject(), "Invalid layer metadata.");
        l.name = r["name"].toString(); l.visible = boolean(r["isVisible"]);
        auto t = r["transform"].toObject();
        keys(t, {"origin", "size", "rotation", "flipX", "flipY", "sampling"});
        auto xy = pair(t["origin"]), wh = pair(t["size"]);
        l.origin = {number(xy[0]), number(xy[1])}; l.size = {number(wh[0]), number(wh[1])};
        l.rotation = number(t["rotation"]); l.flipX = boolean(t["flipX"]); l.flipY = boolean(t["flipY"]);
        l.sampling = t["sampling"].toString();
        if (r.contains("opacity")) l.opacity = number(r["opacity"]);
        if (r.contains("blendMode")) l.blend = r["blendMode"].toString();
        require(version >= 3 || (l.opacity == 1 && l.blend == "Normal"), "Appearance is invalid for this project version.");
        QString asset;
        if (r.contains("imageFile") && !r["imageFile"].isNull()) {
            require(!l.isGroup, "Folders cannot have source images.");
            asset = r["imageFile"].toString();
            require(asset == r["id"].toString() + ".png", "Unsafe image filename.");
        }
        for (const auto &field : {QString("shape"), QString("text")}) {
            if(r.contains(field) && !r[field].isNull()) {
                require(r[field].isObject() && !r[field].toObject().isEmpty() && !asset.isEmpty(), "Invalid editable layer metadata.");
                if(field=="shape") l.shape=r[field].toObject(); else l.text=r[field].toObject();
            }
        }
        QString maskFile;
        if (r.contains("maskFile")) {
            require(version >= (l.isGroup ? 6 : 4) && r["maskFile"].isString(), "Invalid mask metadata for this project version.");
            maskFile = r["maskFile"].toString();
            require(maskFile == r["id"].toString() + ".mask.png", "Unsafe mask filename.");
        }
        if (r.contains("maskEnabled")) {
            require(!maskFile.isEmpty(), "Mask enabled flag without a mask.");
            l.maskEnabled = boolean(r["maskEnabled"]);
        }
        d.layers.append(l); files.append(asset); maskFiles.append(maskFile);
    }
    if (m.contains("activeLayerID") && !m["activeLayerID"].isNull()) {
        auto active = uuid(m["activeLayerID"]);
        for (int i = 0; i < d.layers.size(); ++i) if (d.layers[i].id == active) d.active = i;
        require(d.active >= 0, "Active layer does not exist.");
    }
    validate(d);
    qint64 pixels = 0;
    for (int i = 0; i < files.size(); ++i) {
        if (files[i].isEmpty()) continue;
        QString asset = path + "/images/" + files[i];
        safeFile(asset, root, 512LL * 1024 * 1024);
        QImageReader reader(asset);
        require(reader.format() == "png" && validSize(reader.size()), "Invalid PNG asset.");
        pixels += qint64(reader.size().width()) * reader.size().height();
        require(pixels <= MaxPixels, "Combined source images exceed 100 megapixels.");
        d.layers[i].image = readImage(asset);
    }
    qint64 maskPixels = 0;
    for (int i = 0; i < maskFiles.size(); ++i) {
        if (maskFiles[i].isEmpty()) continue;
        QString asset = path + "/images/" + maskFiles[i];
        safeFile(asset, root, 512LL * 1024 * 1024);
        QImageReader reader(asset);
        require(reader.format() == "png" && validSize(reader.size()), "Invalid mask PNG.");
        maskPixels += qint64(reader.size().width()) * reader.size().height();
        require(maskPixels <= MaxPixels, "Combined masks exceed 100 megapixels.");
        auto mask = reader.read();
        require(!mask.isNull() && mask.format() == QImage::Format_Grayscale8,
                "Masks must be 8-bit grayscale PNGs without alpha.");
        d.layers[i].mask = mask;
    }
    validate(d);
    return d;
}
void saveProject(const Document &d, const QString &path) {
    validate(d);
    for(const auto &layer : d.layers) require((layer.shape.isEmpty() && layer.text.isEmpty()) || !layer.image.isNull(), "Editable layers require a cached image.");
    QFileInfo destination(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    require(destination.fileName().endsWith(".comp", Qt::CaseInsensitive) && !destination.isSymLink(), "Project name must end with .comp and cannot be a symbolic link.");
    // Only replace projects we can fully understand, never an arbitrary directory.
    if (destination.exists()) {
        const auto previous = loadProject(destination.absoluteFilePath());
        const auto entries = QDir(destination.absoluteFilePath()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const auto &entry : entries)
            require(entry == "manifest.json" || entry == "images", "Project contains extra files. Save to a new .comp folder to preserve them.");
        QSet<QString> assets;
        for (const auto &layer : previous.layers) {
            if (!layer.image.isNull()) assets.insert(idString(layer.id).toLower() + ".png");
            if (!layer.mask.isNull()) assets.insert(idString(layer.id).toLower() + ".mask.png");
        }
        QDir images(destination.absoluteFilePath() + "/images");
        require(!QFileInfo(images.path()).isSymLink(), "Cannot replace a project with a linked images folder.");
        for (const auto &entry : images.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
            require(entry.isFile() && !entry.isSymLink() && assets.contains(entry.fileName().toLower()),
                "Project contains extra assets. Save to a new .comp folder to preserve them.");
    }
    QTemporaryDir staging(destination.absolutePath() + "/.compositor-save-XXXXXX");
    require(staging.isValid() && QDir(staging.path()).mkdir("images"), "Cannot create temporary project beside destination.");
    QJsonArray layers;
    int version = 3;
    for (const auto &l : d.layers) {
        QJsonObject t{{"origin", QJsonArray{l.origin.x(), l.origin.y()}}, {"size", QJsonArray{l.size.width(), l.size.height()}},
            {"rotation", l.rotation}, {"flipX", l.flipX}, {"flipY", l.flipY}, {"sampling", l.sampling}};
        QJsonObject r{{"id", idString(l.id)}, {"name", l.name}, {"isVisible", l.visible}, {"transform", t},
            {"opacity", l.opacity}, {"blendMode", l.blend}};
        if (!l.image.isNull()) {
            QString name = idString(l.id) + ".png";
            require(l.image.save(staging.path() + "/images/" + name, "PNG"), "Could not write project image.");
            r["imageFile"] = name;
        }
        if (!l.mask.isNull()) {
            QString name = idString(l.id) + ".mask.png";
            require(l.mask.save(staging.path() + "/images/" + name, "PNG"), "Could not write layer mask.");
            r["maskFile"] = name; r["maskEnabled"] = l.maskEnabled; version = std::max(version,l.isGroup ? 6 : 4);
        }
        if(l.isGroup) r["isGroup"]=true;
        if(!l.parentID.isNull()) r["parentID"]=idString(l.parentID);
        if(!l.shape.isEmpty()) { r["shape"]=l.shape; version=8; }
        if(!l.text.isEmpty()) { r["text"]=l.text; version=8; }
        layers.append(r);
    }
    QJsonObject manifest{{"format", "com.compositor.project"}, {"version", version}, {"colorSpace", "sRGB"},
        {"documentID", idString(d.id)}, {"width", d.size.width()}, {"height", d.size.height()},
        {"resolution", d.resolution}, {"layers", layers}};
    if (!d.guides.isEmpty()) {
        QJsonArray guides;
        for (const auto &g : d.guides) guides.append(QJsonObject{{"id", idString(g.id)}, {"axis", g.axis}, {"position", g.position}});
        manifest["guides"] = guides; manifest["version"] = 8;
    }
    if (d.active >= 0) manifest["activeLayerID"] = idString(d.layers[d.active].id);
    QByteArray bytes = QJsonDocument(manifest).toJson();
    require(bytes.size() <= 4 * 1024 * 1024, "Project manifest exceeds 4 MiB.");
    QFile file(staging.path() + "/manifest.json");
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush(), "Could not write project manifest.");
    file.close();
    // Exchange whole directories in one operation; old data stays intact on failure.
    auto from = QFile::encodeName(staging.path()), to = QFile::encodeName(destination.absoluteFilePath());
    unsigned flags = destination.exists() ? RENAME_EXCHANGE : RENAME_NOREPLACE;
    if (syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), flags) != 0)
        fail("Could not atomically save project: " + QString::fromLocal8Bit(std::strerror(errno)));
}
void exportImage(const Document &d, const QString &path) {
    auto format = QFileInfo(path).suffix().toLower().toLatin1();
    require(format == "png" || format == "jpg" || format == "jpeg", "Export filename must end with .png, .jpg, or .jpeg.");
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), file.errorString());
    QImageWriter writer(&file, format == "png" ? "png" : "jpeg"); writer.setQuality(95);
    require(writer.write(render(d, format != "png")), writer.errorString());
    require(file.commit(), file.errorString());
}
} // namespace Arc
