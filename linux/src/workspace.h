#pragma once
#include <QMainWindow>
#include "document.h"
class QTabWidget;
class Window;
class Workspace : public QMainWindow {
    Q_OBJECT
public:
    explicit Workspace(QWidget *parent = nullptr);
    Window *activeEditor() const;
    Window *addDocument(const Arc::Document &document = {});
    void openProject(const QString &path);
    void copyLayersTo(int targetIndex);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object,QEvent *event) override;
private:
    QTabWidget *tabs;
    Window *attach(Window *editor);
    void updateTitles();
    void closeTab(int index);
    void chooseLayerDestination();
};
