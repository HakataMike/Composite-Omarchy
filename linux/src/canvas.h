#pragma once
#include "document.h"
#include "painting.h"
#include <QWidget>
#include <optional>

class Canvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Move, Brush, Eraser, Rectangle };
    explicit Canvas(QWidget *parent = nullptr);
    void setTool(Tool tool);
    void setBrush(const Arc::Brush &brush);
    void clearSelection();
    std::optional<QRectF> selectionBounds() const { return selection; }
    void setDocument(const Arc::Document &document);
    void fit();
    void zoomBy(double factor);
    QPointF canvasToWidget(QPointF point) const;
signals:
    void selected(int index);
    void moved(int index, QPointF origin);
    void filesDropped(QStringList paths);
    void zoomChanged(double zoom);
    void painted(int index, QImage image);
    void errorOccurred(QString message);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
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
    bool space = false, panning = false, dragging = false;
    int dragLayer = -1;
    Tool tool = Tool::Move;
    Arc::Brush brush;
    Arc::Layer strokeOriginal;
    QPainterPath strokePath;
    bool painting = false, selecting = false;
    std::optional<QRectF> selection, previousSelection;
    QPointF selectionStart;
    QPointF pointerPosition;
    QPointF documentPoint(QPointF point) const;
    void refreshImage();
    void cancelGesture();
    void updateStroke(QPointF point);
    void updateSelection(QPointF point);
};
