#pragma once
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QUuid>
#include <QVector>

namespace Arc {
constexpr qint64 MaxPixels = 100000000;
struct Layer {
    QUuid id = QUuid::createUuid();
    QString name;
    QUuid parentID, maskSourceID;
    bool isGroup = false;
    QImage image;
    QJsonObject adjustment, effects;
    QJsonObject shape, text; // Editable source; image remains the portable raster fallback.
    QImage mask; // Grayscale8 coverage; optional placement gives independent document coordinates.
    bool maskEnabled = true, maskLinked = true;
    QJsonObject maskPlacement;
    QPointF origin;
    QSizeF size;
    double rotation = 0;
    bool flipX = false, flipY = false, visible = true;
    double opacity = 1;
    QString blend = "Normal", sampling = "High quality";
    QTransform transform() const;
    bool operator==(const Layer &other) const;
};
struct Guide {
    QUuid id = QUuid::createUuid();
    QString axis = "vertical";
    double position = 0;
    bool operator==(const Guide &o) const { return id == o.id && axis == o.axis && position == o.position; }
};
struct Document {
    QUuid id = QUuid::createUuid();
    QSize size = {1280, 720};
    double resolution = 72;
    QVector<Layer> layers; // Bottom to top, matching the macOS format.
    QVector<Guide> guides;
    int active = -1;
    bool operator==(const Document &other) const;
};
QSize maskEditingSize(const Layer &layer);
QStringList blendModes();
void validate(const Document &document);
QImage readImage(const QString &path);
void paint(QPainter &painter, const Document &document, QRect region = {});
void repaintRegion(QImage &image, const Document &document, QRect region);
QImage render(const Document &document, bool whiteBackground = false);
Document loadProject(const QString &path);
void saveProject(const Document &document, const QString &path);
void exportImage(const Document &document, const QString &path);
} // namespace Arc
