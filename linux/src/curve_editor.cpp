#include "curve_editor.h"
#include "filters.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <algorithm>
CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent) {
    setMinimumSize(256,200); setFocusPolicy(Qt::StrongFocus); setAccessibleName("Curves graph");
    setToolTip("Click to add a point; drag to edit. Right-click removes an interior point. Arrow keys move the selected point.");
}
QRectF CurveEditor::graphRect() const { return QRectF(rect()).adjusted(10,10,-10,-10); }
QPointF CurveEditor::toWidget(QPointF v) const { auto r=graphRect(); return {r.left()+v.x()/255*r.width(),r.bottom()-v.y()/255*r.height()}; }
QPointF CurveEditor::toValue(QPointF p) const { auto r=graphRect(); return {std::clamp((p.x()-r.left())/r.width()*255,0.0,255.0),std::clamp((r.bottom()-p.y())/r.height()*255,0.0,255.0)}; }
void CurveEditor::setPoints(const QVector<QPointF> &points) { values=points; selected=-1; update(); }
void CurveEditor::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(),palette().base()); auto r=graphRect(); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(palette().mid().color());
    for(int i=0;i<=4;++i) { auto x=r.left()+r.width()*i/4,y=r.top()+r.height()*i/4; p.drawLine(QPointF(x,r.top()),QPointF(x,r.bottom())); p.drawLine(QPointF(r.left(),y),QPointF(r.right(),y)); }
    p.setPen(QPen(palette().mid().color(),1,Qt::DashLine)); p.drawLine(r.bottomLeft(),r.topRight());
    QPainterPath curve; for(int x=0;x<=255;++x) { auto point=toWidget({double(x),Arc::evaluateCurve(values,x)}); if(!x) curve.moveTo(point); else curve.lineTo(point); }
    p.setPen(QPen(palette().highlight().color(),2)); p.drawPath(curve);
    for(int i=0;i<values.size();++i) { p.setBrush(i==selected ? palette().highlight() : palette().base()); p.drawEllipse(toWidget(values[i]),4,4); }
}
void CurveEditor::movePoint(QPointF point) {
    if(selected<0) return;
    if(selected==0) point.setX(0); else if(selected==values.size()-1) point.setX(255);
    else point.setX(std::clamp(point.x(),values[selected-1].x()+0.01,values[selected+1].x()-0.01));
    point.setY(std::clamp(point.y(),0.0,255.0)); values[selected]=point; update(); emit pointsChanged();
}
void CurveEditor::mousePressEvent(QMouseEvent *event) {
    setFocus(); selected=-1; double nearest=10;
    for(int i=0;i<values.size();++i) { double distance=QLineF(event->position(),toWidget(values[i])).length(); if(distance<nearest) { selected=i; nearest=distance; } }
    if(event->button()==Qt::RightButton) {
        if(selected>0 && selected<values.size()-1) { values.removeAt(selected); selected=-1; emit pointsChanged(); } update(); return;
    }
    if(event->button()!=Qt::LeftButton) return;
    if(selected<0 && values.size()<32) {
        auto point=toValue(event->position()); if(point.x()<=0.01 || point.x()>=254.99) return;
        int index=1; while(index<values.size() && values[index].x()<point.x()) ++index;
        if(point.x()-values[index-1].x()<0.02 || values[index].x()-point.x()<0.02) return;
        values.insert(index,point); selected=index; emit pointsChanged();
    }
    dragging=selected>=0; update();
}
void CurveEditor::mouseMoveEvent(QMouseEvent *event) { if(dragging) movePoint(toValue(event->position())); }
void CurveEditor::mouseReleaseEvent(QMouseEvent *event) { if(dragging) movePoint(toValue(event->position())); dragging=false; }
void CurveEditor::keyPressEvent(QKeyEvent *event) {
    if(selected<0) { QWidget::keyPressEvent(event); return; }
    auto value=values[selected]; int step=event->modifiers() & Qt::ShiftModifier ? 10 : 1;
    if(event->key()==Qt::Key_Left) value.rx()-=step;
    else if(event->key()==Qt::Key_Right) value.rx()+=step;
    else if(event->key()==Qt::Key_Up) value.ry()+=step;
    else if(event->key()==Qt::Key_Down) value.ry()-=step;
    else { QWidget::keyPressEvent(event); return; }
    movePoint(value); event->accept();
}
