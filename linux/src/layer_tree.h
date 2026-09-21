#pragma once
#include <QTreeWidget>
#include <QDropEvent>
class LayerTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit LayerTree(QWidget *parent=nullptr) : QTreeWidget(parent) {
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setHeaderHidden(true); setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
    }
signals:
    void orderChanged();
protected:
    void dropEvent(QDropEvent *event) override {
        QTreeWidget::dropEvent(event);
        if(event->isAccepted()) emit orderChanged();
    }
};
