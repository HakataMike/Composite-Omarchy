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
    void dropEvent(QDropEvent *event) override {
        QTreeWidget::dropEvent(event);
        if(event->isAccepted()) emit orderChanged();
    }
};
