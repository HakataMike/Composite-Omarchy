#pragma once
#include <QTreeWidget>
#include <QDropEvent>
#include <QHeaderView>
class LayerTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit LayerTree(QWidget *parent=nullptr) : QTreeWidget(parent) {
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setColumnCount(2); header()->setStretchLastSection(false);
        header()->setSectionResizeMode(0,QHeaderView::Stretch); header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
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
