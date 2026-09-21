#pragma once
#include "canvas.h"
#include <QMainWindow>
#include <QUndoStack>
#include <functional>
class QListWidget;
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
    void openProject(const QString &path);
protected:
    void closeEvent(QCloseEvent *) override;
private:
    Arc::Document document;
    QString projectPath;
    QUndoStack history;
    Canvas *canvas;
    QListWidget *layers;
    QWidget *inspector;
    QDoubleSpinBox *x, *y, *w, *h, *angle, *opacity;
    QComboBox *blend, *paintTarget;
    QCheckBox *maskEnabled;
    QPushButton *addMask, *removeMask;
    QLabel *maskPreview;
    QLabel *zoomLabel;
    bool refreshing = false;
    bool sizeFirstImport = true;
    void refresh();
    void edit(const QString &name, const std::function<void(Arc::Document &)> &operation);
    void editLayer(const QString &name, const std::function<void(Arc::Layer &)> &operation);
    void report(const std::function<void()> &operation);
    bool save(bool choosePath = false);
    bool mayDiscard();
    void newProject();
    void exportDialog();
    void chooseProject();
};
