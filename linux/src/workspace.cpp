#include "workspace.h"
#include "window.h"
#include <QCloseEvent>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>

Workspace::Workspace(QWidget *parent) : QMainWindow(parent),tabs(new QTabWidget) {
    resize(1320,900); tabs->setObjectName("documentTabs"); tabs->setTabsClosable(true); tabs->setMovable(true); setCentralWidget(tabs);
    connect(tabs,&QTabWidget::tabCloseRequested,this,&Workspace::closeTab);
    connect(tabs,&QTabWidget::currentChanged,this,[this] { updateTitles(); if(auto *editor=activeEditor()) editor->findChild<Canvas *>()->setFocus(); });
    auto *add=new QToolButton; add->setText("+"); add->setToolTip("New document"); tabs->setCornerWidget(add);
    connect(add,&QToolButton::clicked,this,[this] { addDocument(); }); attach(new Window);
}
Window *Workspace::activeEditor() const { return qobject_cast<Window *>(tabs->currentWidget()); }
Window *Workspace::attach(Window *editor) {
    editor->setTabbed(true); editor->setWindowFlags(Qt::Widget);
    connect(editor,&Window::documentStatusChanged,this,&Workspace::updateTitles);
    connect(editor,&Window::newDocumentRequested,this,[this](const auto &d) { addDocument(d); });
    connect(editor,&Window::openRequested,this,&Workspace::openProject);
    connect(editor,&Window::closeRequested,this,[this,editor] { closeTab(tabs->indexOf(editor)); });
    connect(editor,&Window::quitRequested,this,&QWidget::close);
    connect(editor,&Window::copyLayersRequested,this,&Workspace::chooseLayerDestination);
    tabs->addTab(editor,editor->documentTitle()); tabs->setCurrentWidget(editor); updateTitles();
    QTimer::singleShot(0,editor,[editor] { editor->findChild<Canvas *>()->fit(); }); return editor;
}
Window *Workspace::addDocument(const Arc::Document &d) {
    auto *editor=new Window; editor->initializeDocument(d); return attach(editor);
}
void Workspace::openProject(const QString &path) {
    const auto canonical=QFileInfo(path).canonicalFilePath();
    for(int i=0;i<tabs->count();++i) {
        auto *editor=qobject_cast<Window *>(tabs->widget(i));
        if(!canonical.isEmpty() && QFileInfo(editor->projectFile()).canonicalFilePath()==canonical) { tabs->setCurrentIndex(i); return; }
    }
    auto *editor=new Window;
    if(!editor->openProject(path)) { delete editor; return; }
    attach(editor);
}
void Workspace::updateTitles() {
    for(int i=0;i<tabs->count();++i) {
        auto *editor=qobject_cast<Window *>(tabs->widget(i));
        tabs->setTabText(i,editor->documentTitle()+(editor->isWindowModified() ? " *" : ""));
        tabs->setTabToolTip(i,editor->projectFile());
    }
    setWindowTitle(activeEditor() ? activeEditor()->documentTitle()+" — Compositor ARC" : "Compositor ARC");
}
void Workspace::closeTab(int index) {
    auto *editor=qobject_cast<Window *>(tabs->widget(index));
    if(!editor || !editor->canCloseDocument()) return;
    tabs->removeTab(index); editor->deleteLater();
    if(!tabs->count()) addDocument();
}
void Workspace::closeEvent(QCloseEvent *event) {
    for(int i=0;i<tabs->count();++i) {
        auto *editor=qobject_cast<Window *>(tabs->widget(i));
        if(editor->isWindowModified()) tabs->setCurrentIndex(i);
        if(!editor->canCloseDocument()) { event->ignore(); return; }
    }
    event->accept();
}
void Workspace::chooseLayerDestination() {
    if(tabs->count()<2) { QMessageBox::information(this,"Copy layers","Open or create a second document first."); return; }
    QStringList choices; QVector<int> targets;
    for(int i=0;i<tabs->count();++i) if(i!=tabs->currentIndex()) { choices.append(QString::number(i+1)+": "+tabs->tabText(i)); targets.append(i); }
    bool accepted=false; auto choice=QInputDialog::getItem(this,"Copy selected layers","Destination document",choices,0,false,&accepted);
    if(accepted) copyLayersTo(targets[choices.indexOf(choice)]);
}
void Workspace::copyLayersTo(int targetIndex) {
    auto *source=activeEditor(),*target=qobject_cast<Window *>(tabs->widget(targetIndex));
    if(!source || !target || source==target) return;
    try { target->receiveLayers(source->selectedLayersForTransfer()); }
    catch(const std::exception &e) { QMessageBox::warning(this,"Copy layers",QString::fromUtf8(e.what())); }
}
