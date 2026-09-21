#pragma once
#include "document.h"
#include <QWidget>

class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(QWidget *parent = nullptr);
    void setDocument(const Arc::Document &document);
    void fit();
    void zoomBy(double factor);
    QPointF canvasToWidget(QPointF point) const;
signals:
    void selected(int index);
    void moved(int index, QPointF origin);
    void filesDropped(QStringList paths);
    void zoomChanged(double zoom);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    Arc::Document document;
    QImage composite;
    double zoom = 1;
    QPointF offset = {40, 40}, pressPoint, originalOrigin, originalOffset;
    bool space = false, panning = false, dragging = false;
    int dragLayer = -1;
    QPointF documentPoint(QPointF point) const;
    void refreshImage();
    void cancelGesture();
};
