#pragma once
#include "document.h"
#include "painting.h"
#include "retouch.h"
#include <QWidget>
#include <optional>

class QPlainTextEdit;
class Canvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Move, Brush, Eraser, Rectangle, Ellipse, Lasso, Polygon, Wand, Eyedropper, Gradient, Clone, Heal, Blur, Smudge, Liquify, ShapeRectangle, ShapeEllipse, ShapeLine, Text, Crop };
    explicit Canvas(QWidget *parent = nullptr);
    void setTool(Tool tool);
    void beginTextEditing(int index);
    bool finishTextEditing(bool accept = true);
    void setSelectedLayers(QVector<QUuid> ids) { selectedIDs=std::move(ids); update(); }
    void setBrush(const Arc::Brush &brush);
    void setMaskTarget(bool enabled);
    void setGuidesVisible(bool enabled);
    void setGuidesLocked(bool enabled);
    void setSnapping(bool enabled);
    void setRulersVisible(bool enabled) { rulersVisible=enabled; update(); }
    void setPixelGridVisible(bool enabled) { pixelGridVisible=enabled; update(); }
    void clearSelection();
    void setSelection(const QPainterPath &path);
    void setSelectionCoverage(const QImage &coverage);
    QImage selectedCoverage() const;
    void featherSelection(int radius);
    void invertSelection();
    std::optional<QPainterPath> selectedPath() const { return selection; }
    void setGradientOptions(Arc::GradientOptions value) { cancelGesture(); gradientOptions=value; }
    void setHealingMode(int mode) { cancelGesture(); healingMode=mode; }
    void setWandContiguous(bool value) { wandContiguous=value; }
    void setBackgroundColor(QColor color) { backgroundColor=color; }
    void setCloneOptions(bool aligned,bool allLayers) { cloneAligned=aligned; cloneAll=allLayers; }
    void setWandTolerance(int value) { wandTolerance = value; }
    std::optional<QRectF> selectionBounds() const { return selection ? std::optional<QRectF>(selection->boundingRect()) : std::nullopt; }
    void setDocument(const Arc::Document &document);
    void fit();
    void zoomBy(double factor);
    QPointF canvasToWidget(QPointF point) const;
signals:
    void selected(int index, bool extend = false);
    void layersTransformed(Arc::Document document);
    void moved(int index, QPointF origin);
    void maskMoved(int index, QJsonObject placement);
    void selectionPixelsMoved(QImage coverage,QPointF offset,bool duplicate);
    void guideMoved(int index, double position);
    void guideAdded(Arc::Guide guide);
    void cropRequested(QRect bounds);
    void filesDropped(QStringList paths);
    void zoomChanged(double zoom);
    void painted(int index, Arc::Layer layer);
    void maskPainted(int index, QImage mask);
    void colorPicked(QColor color);
    void errorOccurred(QString message);
    void shapeCreated(QString kind, QPointF start, QPointF end, QColor color);
    void textRequested(QRectF bounds, QColor color);
    void textEdited(int index,Arc::Layer layer);
protected:
    bool eventFilter(QObject *, QEvent *) override;
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
    QPlainTextEdit *textEditor=nullptr;
    int textIndex=-1;
    Arc::Document textOriginal;
    Arc::Document document;
    QImage composite,downsampled;
    qint64 downsampledKey=0;
    QPointF snapPoint(QPointF point) const;
    double zoom = 1;
    QPointF offset = {40, 40}, pressPoint, originalOrigin, originalOffset;
    QVector<QUuid> selectedIDs,transformIDs;
    Arc::Layer boxOriginal;
    int transformHandle=-1;
    bool distortActive=false, transformMask=false;
    QPolygonF transformQuad;
    void updateTransform(QPointF point,Qt::KeyboardModifiers modifiers);
    Arc::Layer activeTransformBox() const;
    bool drafting = false;
    QPointF draftAnchor, draftStart, draftEnd;
    void updateDraft(QPointF point, Qt::KeyboardModifiers modifiers);
    bool draggingMask = false;
    bool space = false, panning = false, dragging = false;
    Arc::Document dragOriginal;
    int dragLayer = -1;
    bool guidesVisible = true, guidesLocked = false, snapping = false;
    bool rulersVisible=false,pixelGridVisible=false,newGuide=false;
    int dragGuide = -1;
    double originalGuidePosition = 0;
    Tool tool = Tool::Move;
    Arc::Brush brush;
    QColor backgroundColor=Qt::white;
    Arc::GradientOptions gradientOptions;
    int healingMode=0;
    bool wandContiguous=true;
    std::optional<QPointF> cloneAnchor,cloneOffset,lastBrushPoint;
    bool cloneAligned=true,cloneAll=false;
    QImage cloneSample;
    Arc::Layer strokeOriginal;
    QPainterPath strokePath;
    bool painting = false, selecting = false, strokePixelsChanged = false;
    QImage selectionMask, previousSelectionMask;
    bool movingSelection=false, movingPixels=false, duplicatePixels=false;
    QPointF selectionOffset;
    void moveSelection(QPointF offset);
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
