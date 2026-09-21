#pragma once
#include "document.h"
#include "painting.h"
#include <QWidget>
#include <optional>

class Canvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Move, Brush, Eraser, Rectangle, Ellipse, Lasso, Polygon, Wand, Eyedropper, Gradient, Clone, Heal, Blur, ShapeRectangle, ShapeEllipse, ShapeLine, Text };
    explicit Canvas(QWidget *parent = nullptr);
    void setTool(Tool tool);
    void setBrush(const Arc::Brush &brush);
    void setMaskTarget(bool enabled);
    void setGuidesVisible(bool enabled);
    void setGuidesLocked(bool enabled);
    void setSnapping(bool enabled);
    void clearSelection();
    void setSelection(const QPainterPath &path);
    std::optional<QPainterPath> selectedPath() const { return selection; }
    void setBackgroundColor(QColor color) { backgroundColor=color; }
    void setCloneOptions(bool aligned,bool allLayers) { cloneAligned=aligned; cloneAll=allLayers; }
    void setWandTolerance(int value) { wandTolerance = value; }
    std::optional<QRectF> selectionBounds() const { return selection ? std::optional<QRectF>(selection->boundingRect()) : std::nullopt; }
    void setDocument(const Arc::Document &document);
    void fit();
    void zoomBy(double factor);
    QPointF canvasToWidget(QPointF point) const;
signals:
    void selected(int index);
    void moved(int index, QPointF origin);
    void guideMoved(int index, double position);
    void filesDropped(QStringList paths);
    void zoomChanged(double zoom);
    void painted(int index, QImage image);
    void maskPainted(int index, QImage mask);
    void colorPicked(QColor color);
    void errorOccurred(QString message);
    void shapeCreated(QString kind, QPointF start, QPointF end, QColor color);
    void textRequested(QRectF bounds, QColor color);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void leaveEvent(QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    Arc::Document document;
    QImage composite;
    double zoom = 1;
    QPointF offset = {40, 40}, pressPoint, originalOrigin, originalOffset;
    bool drafting = false;
    QPointF draftAnchor, draftStart, draftEnd;
    void updateDraft(QPointF point, Qt::KeyboardModifiers modifiers);
    bool space = false, panning = false, dragging = false;
    Arc::Document dragOriginal;
    int dragLayer = -1;
    bool guidesVisible = true, guidesLocked = false, snapping = false;
    int dragGuide = -1;
    double originalGuidePosition = 0;
    Tool tool = Tool::Move;
    Arc::Brush brush;
    QColor backgroundColor=Qt::white;
    std::optional<QPointF> cloneAnchor,cloneOffset,lastBrushPoint;
    bool cloneAligned=true,cloneAll=false;
    QImage cloneSample;
    Arc::Layer strokeOriginal;
    QPainterPath strokePath;
    bool painting = false, selecting = false;
    bool maskTarget = false;
    std::optional<QPainterPath> selection, previousSelection;
    QPainterPath selectionGesture;
    int selectionOperation = 0, wandTolerance = 32;
    void combineSelection(const QPainterPath &path);
    QPointF selectionStart;
    QPointF pointerPosition;
    QPointF documentPoint(QPointF point) const;
    void refreshImage();
    void cancelGesture();
    void updateStroke(QPointF point);
    void updateSelection(QPointF point);
};
