#pragma once
#include <QTreeWidget>
#include <QDropEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QToolTip>
#include "document.h"
#include <functional>
class LayerTree;
class LayerMimeData : public QMimeData {
public:
    const LayerTree *origin=nullptr;
    QVector<Arc::Layer> layers;
};
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
    std::function<QVector<Arc::Layer>()> transferLayers;
signals:
    void orderChanged();
    void copiesDropped(QVector<Arc::Layer> copies,QUuid parent,QUuid anchor,bool above);
protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override {
        auto *native=QTreeWidget::mimeData(items);
        if(!native || !transferLayers) return native;
        auto *data=new LayerMimeData; data->origin=this;
        try { data->layers=transferLayers(); }
        catch(const std::exception &e) { delete data; QToolTip::showText(QCursor::pos(),QString::fromUtf8(e.what())); return native; }
        for(const auto &format:native->formats()) data->setData(format,native->data(format));
        data->setData("application/x-compositor-layers","1"); delete native; return data;
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        QTreeWidget::dragMoveEvent(event);
        const auto *data=dynamic_cast<const LayerMimeData *>(event->mimeData());
        if(data && data->origin==this && (event->modifiers() & Qt::AltModifier)) { event->setDropAction(Qt::CopyAction); event->accept(); }
    }
    void dropEvent(QDropEvent *event) override {
        const auto *data=dynamic_cast<const LayerMimeData *>(event->mimeData());
        if(data && data->origin==this && (event->modifiers() & Qt::AltModifier)) {
            auto *item=itemAt(event->position().toPoint()); QUuid parent,anchor; bool above=false;
            if(item) {
                if(dropIndicatorPosition()==OnItem && (item->flags() & Qt::ItemIsDropEnabled)) parent=item->data(0,Qt::UserRole+1).toUuid();
                else { parent=item->parent() ? item->parent()->data(0,Qt::UserRole+1).toUuid() : QUuid(); anchor=item->data(0,Qt::UserRole+1).toUuid(); above=dropIndicatorPosition()==AboveItem; }
            }
            event->setDropAction(Qt::CopyAction); event->accept(); emit copiesDropped(data->layers,parent,anchor,above); return;
        }
        QTreeWidget::dropEvent(event);
        if(event->isAccepted()) emit orderChanged();
    }
};
