#include "window.h"
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <stdexcept>

namespace {
class EditCommand : public QUndoCommand {
public:
    EditCommand(QString name, Arc::Document before, Arc::Document after, std::function<void(const Arc::Document &)> apply)
        : QUndoCommand(name), before(std::move(before)), after(std::move(after)), apply(std::move(apply)) {}
    void undo() override { apply(before); }
    void redo() override { apply(after); }
private:
    Arc::Document before, after;
    std::function<void(const Arc::Document &)> apply;
};
}
Window::Window() {
    resize(1280, 840); setMinimumSize(800, 520);
    history.setUndoLimit(100);
    canvas = new Canvas(this); setCentralWidget(canvas);
    auto *file = menuBar()->addMenu("&File");
    file->addAction("&New canvas…", QKeySequence::New, this, &Window::newProject);
    file->addAction("&Open project…", QKeySequence::Open, this, &Window::chooseProject);
    auto *import = file->addAction("&Import images…", QKeySequence("Ctrl+I"), this, [this] {
        importImages(QFileDialog::getOpenFileNames(this, "Import images", {}, "Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.webp);;All files (*)"));
    });
    file->addSeparator();
    file->addAction("&Save project", QKeySequence::Save, this, [this] { save(); });
    file->addAction("Save project &as…", QKeySequence::SaveAs, this, [this] { save(true); });
    file->addAction("&Export image…", QKeySequence("Ctrl+Shift+E"), this, &Window::exportDialog);
    file->addSeparator(); file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);
    auto *editMenu = menuBar()->addMenu("&Edit");
    auto *undo = history.createUndoAction(this, "Undo"); undo->setShortcut(QKeySequence::Undo); editMenu->addAction(undo);
    auto *redo = history.createRedoAction(this, "Redo"); redo->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")}); editMenu->addAction(redo);
    auto *view = menuBar()->addMenu("&View");
    auto *fit = view->addAction("Fit canvas", QKeySequence("Ctrl+0"), canvas, &Canvas::fit);
    view->addAction("Zoom in", QKeySequence::ZoomIn, this, [this] { canvas->zoomBy(1.25); });
    view->addAction("Zoom out", QKeySequence::ZoomOut, this, [this] { canvas->zoomBy(0.8); });
    auto *toolbar = addToolBar("Document"); toolbar->setMovable(false);
    toolbar->addAction(import); toolbar->addSeparator(); toolbar->addAction(undo); toolbar->addAction(redo); toolbar->addSeparator(); toolbar->addAction(fit);
    auto *dock = new QDockWidget("Layers & transform", this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures); dock->setMinimumWidth(265);
    auto *panel = new QWidget; auto *layout = new QVBoxLayout(panel);
    auto *hint = new QLabel("Top layer appears first.\nDouble-click a name to rename."); layout->addWidget(hint);
    layers = new QListWidget; layers->setObjectName("layers"); layers->setAccessibleName("Layers"); layout->addWidget(layers, 1);
    auto *buttons = new QHBoxLayout;
    auto button = [&](const QString &label, auto callback) {
        auto *b = new QPushButton(label); buttons->addWidget(b); connect(b, &QPushButton::clicked, this, callback);
    };
    button("+ Copy", [this] { edit("Duplicate layer", [](auto &d) { if (d.active < 0) return; auto l = d.layers[d.active]; l.id = QUuid::createUuid(); l.name += " copy"; d.layers.insert(++d.active, l); }); });
    button("−", [this] { edit("Delete layer", [](auto &d) { if (d.active < 0) return; d.layers.removeAt(d.active); d.active = std::min(d.active, int(d.layers.size())-1); }); });
    button("↑", [this] { edit("Raise layer", [](auto &d) { if (d.active >= 0 && d.active+1 < d.layers.size()) { d.layers.swapItemsAt(d.active, d.active+1); ++d.active; } }); });
    button("↓", [this] { edit("Lower layer", [](auto &d) { if (d.active > 0) { d.layers.swapItemsAt(d.active, d.active-1); --d.active; } }); });
    layout->addLayout(buttons);
    inspector = new QWidget; auto *form = new QFormLayout(inspector);
    auto spin = [&](QString label, double low, double high, QString objectName) {
        auto *s = new QDoubleSpinBox; s->setRange(low, high); s->setDecimals(2); s->setKeyboardTracking(false); s->setObjectName(objectName);
        form->addRow(label, s); return s;
    };
    x = spin("X", -1000000, 1000000, "layerX"); y = spin("Y", -1000000, 1000000, "layerY");
    w = spin("Width", 1, 300000, "layerWidth"); h = spin("Height", 1, 300000, "layerHeight");
    angle = spin("Rotation °", -360000, 360000, "layerRotation"); opacity = spin("Opacity %", 0, 100, "layerOpacity");
    blend = new QComboBox; blend->addItems(Arc::blendModes()); form->addRow("Blend", blend);
    auto bind = [this](QDoubleSpinBox *s, auto change) {
        connect(s, &QDoubleSpinBox::valueChanged, this, [this, change](double value) { if (!refreshing) editLayer("Layer transform / appearance", [=](auto &l) { change(l, value); }); });
    };
    bind(x, [](auto &l, double v) { l.origin.setX(v); }); bind(y, [](auto &l, double v) { l.origin.setY(v); });
    bind(w, [](auto &l, double v) { l.size.setWidth(v); }); bind(h, [](auto &l, double v) { l.size.setHeight(v); });
    bind(angle, [](auto &l, double v) { l.rotation = v; }); bind(opacity, [](auto &l, double v) { l.opacity = v/100; });
    connect(blend, &QComboBox::currentTextChanged, this, [this](const QString &value) { if (!refreshing) editLayer("Blend mode", [&](auto &l) { l.blend = value; }); });
    auto *flipX = new QPushButton("Flip horizontal"), *flipY = new QPushButton("Flip vertical");
    form->addRow(flipX, flipY);
    connect(flipX, &QPushButton::clicked, this, [this] { editLayer("Flip horizontal", [](auto &l) { l.flipX = !l.flipX; }); });
    connect(flipY, &QPushButton::clicked, this, [this] { editLayer("Flip vertical", [](auto &l) { l.flipY = !l.flipY; }); });
    layout->addWidget(inspector); dock->setWidget(panel); addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(layers, &QListWidget::currentRowChanged, this, [this](int row) {
        if (refreshing) return;
        document.active = row < 0 ? -1 : document.layers.size()-1-row;
        refresh();
    });
    connect(layers, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (refreshing) return;
        int index = document.layers.size()-1-layers->row(item);
        QString name = item->text(); bool visible = item->checkState() == Qt::Checked;
        edit("Layer properties", [=](auto &d) { d.layers[index].name = name; d.layers[index].visible = visible; });
    });
    connect(canvas, &Canvas::selected, this, [this](int index) { document.active = index; refresh(); });
    connect(canvas, &Canvas::moved, this, [this](int index, QPointF point) { edit("Move layer", [=](auto &d) { d.layers[index].origin = point; }); });
    connect(canvas, &Canvas::filesDropped, this, &Window::importImages);
    zoomLabel = new QLabel; statusBar()->addPermanentWidget(zoomLabel);
    connect(canvas, &Canvas::zoomChanged, this, [this](double value) { zoomLabel->setText(QString::number(value*100, 'f', 0) + "%"); });
    connect(&history, &QUndoStack::cleanChanged, this, [this] { setWindowModified(!history.isClean()); });
    statusBar()->showMessage("Drag to move • Wheel to zoom • Space-drag to pan • Ctrl+I to import");
    refresh();
}
void Window::refresh() {
    refreshing = true;
    layers->clear();
    for (int i = document.layers.size()-1; i >= 0; --i) {
        const auto &l = document.layers[i];
        auto *item = new QListWidgetItem(l.name, layers);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        item->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        item->setIcon(QPixmap::fromImage(l.image.scaled(48,48,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
    }
    layers->setCurrentRow(document.active < 0 ? -1 : document.layers.size()-1-document.active);
    inspector->setEnabled(document.active >= 0);
    if (document.active >= 0) {
        const auto &l = document.layers[document.active];
        x->setValue(l.origin.x()); y->setValue(l.origin.y()); w->setValue(l.size.width()); h->setValue(l.size.height());
        angle->setValue(l.rotation); opacity->setValue(l.opacity*100); blend->setCurrentText(l.blend);
    }
    canvas->setDocument(document);
    setWindowTitle((projectPath.isEmpty() ? "Untitled" : QFileInfo(projectPath).fileName()) + "[*] — Compositor ARC");
    setWindowModified(!history.isClean()); refreshing = false;
}
void Window::report(const std::function<void()> &operation) {
    try { operation(); } catch (const std::exception &e) { QMessageBox::warning(this, "Compositor ARC", QString::fromUtf8(e.what())); }
}
void Window::edit(const QString &name, const std::function<void(Arc::Document &)> &operation) {
    report([&] {
        auto next = document; operation(next); Arc::validate(next);
        if (next == document) return;
        history.push(new EditCommand(name, document, next, [this](const auto &d) { document = d; refresh(); }));
    });
}
void Window::editLayer(const QString &name, const std::function<void(Arc::Layer &)> &operation) {
    if (document.active >= 0) edit(name, [&](auto &d) { operation(d.layers[d.active]); });
}
void Window::importImages(const QStringList &paths) {
    if (paths.isEmpty()) return;
    edit("Import images", [&](auto &d) {
        qint64 pixels = 0; for (const auto &l : d.layers) pixels += qint64(l.image.width())*l.image.height();
        for (const auto &path : paths) {
            Arc::Layer l; l.image = Arc::readImage(path);
            pixels += qint64(l.image.width())*l.image.height();
            if (pixels > Arc::MaxPixels) throw std::runtime_error("Combined source images exceed 100 megapixels.");
            l.name = QFileInfo(path).completeBaseName(); l.size = l.image.size();
            if (sizeFirstImport && d.layers.isEmpty()) d.size = l.image.size();
            l.origin = {(d.size.width()-l.size.width())/2, (d.size.height()-l.size.height())/2};
            d.layers.append(l); d.active = d.layers.size()-1;
        }
    });
    if (!document.layers.isEmpty()) sizeFirstImport = false;
    canvas->fit();
}
bool Window::mayDiscard() {
    if (history.isClean()) return true;
    auto answer = QMessageBox::warning(this, "Unsaved changes", "Save your project before continuing?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save());
}
bool Window::save(bool choosePath) {
    QString path = projectPath;
    if (choosePath || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, "Save project folder", path.isEmpty() ? "Untitled.comp" : path, "Compositor project (*.comp)", nullptr, QFileDialog::DontUseNativeDialog);
        if (path.isEmpty()) return false;
        if (!path.endsWith(".comp", Qt::CaseInsensitive)) path += ".comp";
    }
    bool saved = false;
    report([&] { Arc::saveProject(document, path); projectPath = path; history.setClean(); refresh(); saved = true; });
    return saved;
}
void Window::openProject(const QString &path) {
    report([&] {
        auto next = Arc::loadProject(path);
        if (!mayDiscard()) return;
        document = next; projectPath = path; sizeFirstImport = false; history.clear(); refresh(); canvas->fit();
    });
}
void Window::chooseProject() {
    auto path = QFileDialog::getExistingDirectory(this, "Select a .comp project folder");
    if (!path.isEmpty()) openProject(path);
}
void Window::newProject() {
    QDialog dialog(this); dialog.setWindowTitle("New canvas"); QFormLayout form(&dialog);
    QSpinBox width, height; width.setRange(1,30000); height.setRange(1,30000); width.setValue(1280); height.setValue(720);
    form.addRow("Width (px)", &width); form.addRow("Height (px)", &height);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    report([&] {
        Arc::Document next; next.size = {width.value(), height.value()}; Arc::validate(next);
        if (!mayDiscard()) return;
        document = next; projectPath.clear(); sizeFirstImport = false; history.clear(); refresh(); canvas->fit();
    });
}
void Window::exportDialog() {
    auto path = QFileDialog::getSaveFileName(this, "Export image (JPEG uses a white background)", "Untitled.png", "PNG (*.png);;JPEG (*.jpg *.jpeg)");
    if (!path.isEmpty()) report([&] { Arc::exportImage(document, path); statusBar()->showMessage("Exported " + path, 5000); });
}
void Window::closeEvent(QCloseEvent *event) { if (mayDiscard()) event->accept(); else event->ignore(); }
