#pragma once
#include "canvas.h"
#include <QMainWindow>
#include <QUndoStack>
#include <functional>
class LayerTree;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QCheckBox;
class QPushButton;
class Window : public QMainWindow {
    Q_OBJECT
public:
    Window();
    void importImages(const QStringList &paths);
    bool openProject(const QString &path);
    void initializeDocument(const Arc::Document &document);
    void setTabbed(bool enabled) { tabbed=enabled; }
    QString documentTitle() const;
    QString projectFile() const { return projectPath; }
    bool canCloseDocument() { return mayDiscard(); }
    QVector<Arc::Layer> selectedLayersForTransfer() const;
    void receiveLayers(const QVector<Arc::Layer> &layers);
signals:
    void documentStatusChanged();
    void newDocumentRequested(Arc::Document document);
    void openRequested(QString path);
    void closeRequested();
    void quitRequested();
    void copyLayersRequested();
protected:
    void closeEvent(QCloseEvent *) override;
private:
    Arc::Document document;
    QString projectPath;
    QUndoStack history;
    Canvas *canvas;
    LayerTree *layers;
    QWidget *inspector;
    QDoubleSpinBox *x, *y, *w, *h, *angle, *opacity;
    QComboBox *blend, *paintTarget;
    QCheckBox *maskEnabled, *maskLinked;
    QPushButton *addMask, *removeMask;
    QLabel *maskPreview;
    QLabel *zoomLabel;
    bool refreshing = false, tabbed = false;
    bool sizeFirstImport = true;
    void refresh(bool rebuildLayers = true);
    void edit(const QString &name, const std::function<void(Arc::Document &)> &operation);
    void editGeometry(const QString &name, const std::function<void(Arc::Layer &)> &operation);
    void editLayer(const QString &name, const std::function<void(Arc::Layer &)> &operation);
    void report(const std::function<void()> &operation);
    bool save(bool choosePath = false);
    bool mayDiscard();
    void newProject();
    void resizeDialog(bool resample);
    void filterDialog(const QString &kind);
    void shapeDialog();
    void effectsDialog();
    void adjustmentDialog(QString kind = {}, bool destructive = false);
    void guidesDialog();
    void textDialog(QRectF bounds = {}, QColor color = Qt::black, bool editActive = false);
    void exportDialog();
    void chooseProject();
};
