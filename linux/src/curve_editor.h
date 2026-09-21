#pragma once
#include <QWidget>
#include <QVector>
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget *parent=nullptr);
    void setPoints(const QVector<QPointF> &points);
    QVector<QPointF> points() const { return values; }
signals:
    void pointsChanged();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QVector<QPointF> values{{0,0},{255,255}};
    int selected=-1;
    bool dragging=false;
    QRectF graphRect() const;
    QPointF toWidget(QPointF value) const;
    QPointF toValue(QPointF pixel) const;
    void movePoint(QPointF point);
};
