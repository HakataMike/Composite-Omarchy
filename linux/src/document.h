#pragma once
#include <QImage>
#include <QPainter>
#include <QUuid>
#include <QVector>

namespace Arc {
constexpr qint64 MaxPixels = 100000000;
struct Layer {
    QUuid id = QUuid::createUuid();
    QString name;
    QImage image;
    QImage mask; // Grayscale8 coverage, normalized to the layer bounds.
    bool maskEnabled = true;
    QPointF origin;
    QSizeF size;
    double rotation = 0;
    bool flipX = false, flipY = false, visible = true;
    double opacity = 1;
    QString blend = "Normal", sampling = "High quality";
    QTransform transform() const;
    bool operator==(const Layer &other) const;
};
struct Document {
    QUuid id = QUuid::createUuid();
    QSize size = {1280, 720};
    double resolution = 72;
    QVector<Layer> layers; // Bottom to top, matching the macOS format.
    int active = -1;
    bool operator==(const Document &other) const;
};
QStringList blendModes();
void validate(const Document &document);
QImage readImage(const QString &path);
void paint(QPainter &painter, const Document &document);
QImage render(const Document &document, bool whiteBackground = false);
Document loadProject(const QString &path);
void saveProject(const Document &document, const QString &path);
void exportImage(const Document &document, const QString &path);
} // namespace Arc
