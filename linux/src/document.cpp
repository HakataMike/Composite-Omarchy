#include "document.h"
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
QString idString(QUuid id) { return id.toString(QUuid::WithoutBraces).toUpper(); }
void safeFile(const QString &path, const QString &root, qint64 limit) {
    QFileInfo info(path);
    require(info.isFile() && !info.isSymLink() && info.size() <= limit
        && info.canonicalFilePath().startsWith(root + '/'), "Missing, unsafe, or oversized project asset: " + info.fileName());
}
}
QStringList blendModes() {
    return {"Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Difference", "Color Dodge", "Color Burn", "Soft Light"};
}
bool Layer::operator==(const Layer &o) const {
    return id == o.id && name == o.name && image == o.image && origin == o.origin && size == o.size
        && rotation == o.rotation && flipX == o.flipX && flipY == o.flipY && visible == o.visible
        && opacity == o.opacity && blend == o.blend && sampling == o.sampling;
}
bool Document::operator==(const Document &o) const {
    return id == o.id && size == o.size && resolution == o.resolution && layers == o.layers && active == o.active;
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
    QSet<QUuid> ids;
    qint64 pixels = 0;
    for (const auto &l : d.layers) {
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
void paint(QPainter &p, const Document &d) {
    p.save();
    p.setClipRect(QRect(QPoint(), d.size), Qt::IntersectClip);
    for (const auto &l : d.layers) {
        if (!l.visible || l.image.isNull()) continue;
        p.save();
        p.setTransform(l.transform(), true);
        p.setOpacity(l.opacity);
        p.setCompositionMode(modes.at(blendModes().indexOf(l.blend)));
        p.setRenderHint(QPainter::SmoothPixmapTransform, l.sampling != "Nearest");
        p.drawImage(QRectF(QPointF(), l.size), l.image);
        p.restore();
    }
    p.restore();
}
QImage render(const Document &d, bool whiteBackground) {
    validate(d);
    QImage result(d.size, QImage::Format_ARGB32_Premultiplied);
    require(!result.isNull(), "Insufficient memory to render canvas.");
    result.fill(Qt::transparent);
    { QPainter p(&result); paint(p, d); }
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
    require(version >= 1 && version <= 3, "This Linux preview supports flat raster projects v1–3. Newer projects require features not yet ported.");
    keys(m, {"format", "version", "colorSpace", "resolution", "documentID", "width", "height", "activeLayerID", "layers"});
    require(m["colorSpace"] == "sRGB" && m["layers"].isArray(), "Invalid project metadata.");
    Document d;
    d.id = uuid(m["documentID"]);
    d.size = QSize(integer(m["width"]), integer(m["height"]));
    if (m.contains("resolution")) d.resolution = number(m["resolution"]);
    validate(d);
    require(m["layers"].toArray().size() <= 10000, "Too many layers.");
    QVector<QString> files;
    for (const auto value : m["layers"].toArray()) {
        require(value.isObject(), "Invalid layer record.");
        const auto r = value.toObject();
        keys(r, {"id", "name", "isVisible", "transform", "imageFile", "opacity", "blendMode", "parentID", "isGroup"});
        require((!r.contains("parentID") || r["parentID"].isNull())
            && (!r.contains("isGroup") || (r["isGroup"].isBool() && !r["isGroup"].toBool())), "Groups are not supported in this preview.");
        Layer l;
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
            asset = r["imageFile"].toString();
            require(asset == r["id"].toString() + ".png", "Unsafe image filename.");
        }
        d.layers.append(l); files.append(asset);
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
    validate(d);
    return d;
}
void saveProject(const Document &d, const QString &path) {
    validate(d);
    QFileInfo destination(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    require(destination.fileName().endsWith(".comp", Qt::CaseInsensitive) && !destination.isSymLink(), "Project name must end with .comp and cannot be a symbolic link.");
    // Only replace projects we can fully understand, never an arbitrary directory.
    if (destination.exists()) {
        const auto previous = loadProject(destination.absoluteFilePath());
        const auto entries = QDir(destination.absoluteFilePath()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const auto &entry : entries)
            require(entry == "manifest.json" || entry == "images", "Project contains extra files. Save to a new .comp folder to preserve them.");
        QSet<QString> assets;
        for (const auto &layer : previous.layers)
            if (!layer.image.isNull()) assets.insert(idString(layer.id).toLower() + ".png");
        QDir images(destination.absoluteFilePath() + "/images");
        require(!QFileInfo(images.path()).isSymLink(), "Cannot replace a project with a linked images folder.");
        for (const auto &entry : images.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
            require(entry.isFile() && !entry.isSymLink() && assets.contains(entry.fileName().toLower()),
                "Project contains extra assets. Save to a new .comp folder to preserve them.");
    }
    QTemporaryDir staging(destination.absolutePath() + "/.compositor-save-XXXXXX");
    require(staging.isValid() && QDir(staging.path()).mkdir("images"), "Cannot create temporary project beside destination.");
    QJsonArray layers;
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
        layers.append(r);
    }
    QJsonObject manifest{{"format", "com.compositor.project"}, {"version", 3}, {"colorSpace", "sRGB"},
        {"documentID", idString(d.id)}, {"width", d.size.width()}, {"height", d.size.height()},
        {"resolution", d.resolution}, {"layers", layers}};
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
