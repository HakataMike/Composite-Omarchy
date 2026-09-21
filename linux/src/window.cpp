#include "window.h"
#include "operations.h"
#include "selection.h"
#include "filters.h"
#include "styles.h"
#include <QJsonArray>
#include <QTimer>
#include <QPlainTextEdit>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QColorSpace>
#include <QCheckBox>
#include <memory>
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
#include <QToolButton>
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
    auto *imageMenu = menuBar()->addMenu("&Image");
    imageMenu->addAction("Canvas size…",this,[this] { resizeDialog(false); });
    imageMenu->addAction("Image size…",this,[this] { resizeDialog(true); });
    imageMenu->addAction("Crop to selection",this,[this] {
        auto selection = canvas->selectionBounds();
        if (selection) edit("Crop canvas",[&](auto &d) { Arc::crop(d,selection->toAlignedRect()); });
        else statusBar()->showMessage("Make a rectangular selection to crop the canvas.",5000);
    });
    imageMenu->addAction("Flip canvas horizontally",this,[this] {
        edit("Flip canvas horizontally",[](auto &d) { for (auto &l : d.layers) {
            l.origin.setX(d.size.width()-l.origin.x()-l.size.width()); l.rotation=-l.rotation; l.flipX=!l.flipX;
        }});
    });
    imageMenu->addAction("Flip canvas vertically",this,[this] {
        edit("Flip canvas vertically",[](auto &d) { for (auto &l : d.layers) {
            l.origin.setY(d.size.height()-l.origin.y()-l.size.height()); l.rotation=-l.rotation; l.flipY=!l.flipY;
        }});
    });
    auto *filterMenu=menuBar()->addMenu("&Adjust / Filter");
    for(const auto &name:Arc::filterNames()) filterMenu->addAction(name+"…",this,[this,name] { filterDialog(name); });
    filterMenu->addSeparator();
    filterMenu->addAction("Content-aware fill",this,[this] {
        auto selection=canvas->selectedPath();
        if(!selection) { statusBar()->showMessage("Select a region to fill first.",5000); return; }
        editLayer("Content-aware fill",[&](auto &layer) { layer.image=Arc::contentAwareFill(layer,*selection); Arc::rasterize(layer); });
    });
    auto *layerMenu = menuBar()->addMenu("&Layer");
    layerMenu->addAction("Edit shape…",this,&Window::shapeDialog);
    layerMenu->addAction("Edit text…",this,[this] { textDialog({},Qt::black,true); });
    layerMenu->addAction("Merge down",QKeySequence("Ctrl+E"),this,[this] { edit("Merge down",Arc::mergeDown); });
    auto *maskMenu = layerMenu->addMenu("Mask");
    auto maskFill = [this](int value) {
        editLayer("Fill layer mask",[=](auto &l) { if (!l.mask.isNull()) l.mask.fill(QColor(value,value,value)); });
    };
    maskMenu->addAction("Reveal all",this,[=] { maskFill(255); });
    maskMenu->addAction("Hide all",this,[=] { maskFill(0); });
    maskMenu->addAction("Invert",this,[this] { editLayer("Invert layer mask",[](auto &l) { l.mask.invertPixels(); }); });
    maskMenu->addAction("Feather / blur…",this,[this] {
        bool ok=false; int radius=QInputDialog::getInt(this,"Feather mask","Radius (source pixels)",5,0,250,1,&ok);
        if (ok) editLayer("Feather layer mask",[=](auto &l) { l.mask=Arc::blurImage(l.mask,radius); });
    });
    auto *view = menuBar()->addMenu("&View");
    auto *fit = view->addAction("Fit canvas", QKeySequence("Ctrl+0"), canvas, &Canvas::fit);
    view->addAction("Zoom in", QKeySequence::ZoomIn, this, [this] { canvas->zoomBy(1.25); });
    view->addAction("Zoom out", QKeySequence::ZoomOut, this, [this] { canvas->zoomBy(0.8); });
    auto *toolbar = addToolBar("Document"); toolbar->setMovable(false);
    toolbar->addAction(import); toolbar->addSeparator(); toolbar->addAction(undo); toolbar->addAction(redo); toolbar->addSeparator(); toolbar->addAction(fit);
    auto *selectMenu = menuBar()->addMenu("&Select");
    selectMenu->addAction("Deselect", QKeySequence("Ctrl+D"), canvas, &Canvas::clearSelection);
    selectMenu->addAction("Select all",QKeySequence::SelectAll,this,[this] {
        QPainterPath path; path.addRect(QRectF(QPointF(),document.size)); canvas->setSelection(path);
    });
    selectMenu->addAction("Invert selection",QKeySequence("Ctrl+Shift+I"),this,[this] {
        QPainterPath path; path.addRect(QRectF(QPointF(),document.size));
        if(auto selected=canvas->selectedPath()) path=path.subtracted(*selected);
        canvas->setSelection(path);
    });
    selectMenu->addAction("Select layer pixels",this,[this] {
        if(document.active<0) return;
        report([&] { const auto &l=document.layers[document.active]; if(l.image.isNull()) return;
            QTransform map=l.transform(); map.scale(l.size.width()/l.image.width(),l.size.height()/l.image.height());
            canvas->setSelection(map.map(Arc::alphaSelection(l.image)));
        });
    });
    editMenu->addAction("Copy merged",QKeySequence("Ctrl+Shift+C"),this,[this] {
        report([&] { auto image=Arc::render(document); if(auto selected=canvas->selectedPath()) {
            QImage mask(image.size(),QImage::Format_ARGB32_Premultiplied); mask.fill(Qt::transparent);
            { QPainter p(&mask); p.fillPath(*selected,Qt::white); }
            { QPainter p(&image); p.setCompositionMode(QPainter::CompositionMode_DestinationIn); p.drawImage(QPoint(),mask); }
            image=image.copy(selected->boundingRect().toAlignedRect().intersected(image.rect()));
        } QApplication::clipboard()->setImage(image); });
    });
    editMenu->addAction("Paste image as layer",QKeySequence::Paste,this,[this] {
        auto image=QApplication::clipboard()->image(); if(image.isNull()) return;
        edit("Paste image",[&](auto &d) { Arc::Layer layer; layer.image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            layer.name="Pasted image"; layer.size=image.size(); d.layers.append(layer); d.active=d.layers.size()-1; });
    });
    addToolBarBreak();
    auto *tools = addToolBar("Painting"); tools->setMovable(false);
    auto *toolGroup = new QActionGroup(this);
    auto *toolMenu = new QMenu(this);
    auto *toolButton = new QToolButton;
    toolButton->setText("Move (V)"); toolButton->setMenu(toolMenu);
    toolButton->setPopupMode(QToolButton::InstantPopup);
    toolButton->setAccessibleName("Active tool"); tools->addWidget(toolButton);
    auto addTool = [&](QString label, QString shortcut, Canvas::Tool mode) {
        auto *action = toolMenu->addAction(label);
        action->setCheckable(true); toolGroup->addAction(action);
        action->setShortcut(QKeySequence(shortcut));
        action->setShortcutContext(Qt::WidgetShortcut); canvas->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode, toolButton, label] { toolButton->setText(label); canvas->setTool(mode); canvas->setFocus(); });
        return action;
    };
    addTool("Move (V)", "V", Canvas::Tool::Move)->setChecked(true);
    addTool("Brush (B)", "B", Canvas::Tool::Brush);
    addTool("Eraser (E)", "E", Canvas::Tool::Eraser);
    addTool("Select rectangle (M)", "M", Canvas::Tool::Rectangle);
    addTool("Ellipse", "", Canvas::Tool::Ellipse);
    addTool("Lasso (L)", "L", Canvas::Tool::Lasso);
    addTool("Polygon", "", Canvas::Tool::Polygon);
    addTool("Wand (W)", "W", Canvas::Tool::Wand);
    addTool("Eyedropper (I)", "I", Canvas::Tool::Eyedropper);
    addTool("Gradient (G)", "G", Canvas::Tool::Gradient);
    addTool("Clone stamp (S)", "S", Canvas::Tool::Clone);
    addTool("Healing (J)", "J", Canvas::Tool::Heal);
    addTool("Blur brush", "", Canvas::Tool::Blur);
    addTool("Rectangle shape (U)", "U", Canvas::Tool::ShapeRectangle);
    addTool("Ellipse shape", "", Canvas::Tool::ShapeEllipse);
    addTool("Line shape", "", Canvas::Tool::ShapeLine);
    addTool("Text (T)", "T", Canvas::Tool::Text);
    auto *optionsMenu = new QMenu(this);
    auto *aligned = optionsMenu->addAction("Aligned clone source");
    aligned->setCheckable(true); aligned->setChecked(true);
    auto *allLayers = optionsMenu->addAction("Clone from all visible layers");
    allLayers->setCheckable(true);
    auto cloneOptions = [this, aligned, allLayers] { canvas->setCloneOptions(aligned->isChecked(), allLayers->isChecked()); };
    connect(aligned, &QAction::toggled, this, cloneOptions);
    connect(allLayers, &QAction::toggled, this, cloneOptions);
    auto *options = new QToolButton; options->setText("Options"); options->setMenu(optionsMenu);
    options->setPopupMode(QToolButton::InstantPopup); tools->addWidget(options);
    auto *tolerance=new QSpinBox; tolerance->setRange(0,255); tolerance->setValue(32);
    tolerance->setPrefix("Tolerance "); tolerance->setAccessibleName("Magic wand tolerance"); tools->addWidget(tolerance);
    connect(tolerance,&QSpinBox::valueChanged,canvas,&Canvas::setWandTolerance);
    tools->addSeparator();
    auto *color = tools->addAction("Color…");
    color->setObjectName("brushColor");
    auto settings = std::make_shared<Arc::Brush>();
    auto updateColorIcon = [color, settings] {
        QPixmap swatch(20,20); swatch.fill(settings->color); color->setIcon(swatch);
    };
    updateColorIcon();
    connect(color, &QAction::triggered, this, [this, settings, updateColorIcon] {
        auto chosen = QColorDialog::getColor(settings->color, this, "Paint color");
        if (chosen.isValid()) { settings->color = chosen; updateColorIcon(); canvas->setBrush(*settings); }
    });
    connect(canvas, &Canvas::colorPicked, this, [this, settings, updateColorIcon](QColor picked) {
        settings->color = picked; updateColorIcon(); canvas->setBrush(*settings);
    });
    auto background = std::make_shared<QColor>(Qt::white);
    auto *backgroundAction = optionsMenu->addAction("Gradient end color…");
    connect(backgroundAction, &QAction::triggered, this, [this, background] {
        auto chosen = QColorDialog::getColor(*background, this, "Gradient end color");
        if(chosen.isValid()) { *background = chosen; canvas->setBackgroundColor(chosen); }
    });
    auto *black = tools->addAction("Black");
    auto *white = tools->addAction("White");
    connect(black, &QAction::triggered, this, [this, settings, updateColorIcon] {
        settings->color = Qt::black; updateColorIcon(); canvas->setBrush(*settings);
    });
    connect(white, &QAction::triggered, this, [this, settings, updateColorIcon] {
        settings->color = Qt::white; updateColorIcon(); canvas->setBrush(*settings);
    });
    tools->addWidget(new QLabel(" Size "));
    auto *size = new QSpinBox; size->setObjectName("brushSize"); size->setAccessibleName("Brush diameter");
    size->setRange(1,2000); size->setValue(24); size->setSuffix(" px"); size->setKeyboardTracking(false); tools->addWidget(size);
    connect(size, &QSpinBox::valueChanged, this, [this, settings](int value) { settings->diameter = value; canvas->setBrush(*settings); });
    tools->addWidget(new QLabel(" Opacity "));
    auto *strength = new QSpinBox; strength->setObjectName("brushOpacity"); strength->setAccessibleName("Brush opacity");
    strength->setRange(1,100); strength->setValue(100); strength->setSuffix(" %"); strength->setKeyboardTracking(false); tools->addWidget(strength);
    connect(strength, &QSpinBox::valueChanged, this, [this, settings](int value) { settings->opacity = value/100.0; canvas->setBrush(*settings); });
    tools->addWidget(new QLabel(" Hardness "));
    auto *hardness = new QSpinBox; hardness->setObjectName("brushHardness"); hardness->setAccessibleName("Brush hardness");
    hardness->setRange(0,100); hardness->setValue(100); hardness->setSuffix(" %"); hardness->setKeyboardTracking(false); tools->addWidget(hardness);
    connect(hardness, &QSpinBox::valueChanged, this, [this, settings](int value) { settings->hardness = value/100.0; canvas->setBrush(*settings); });
    auto *dock = new QDockWidget("Layers & transform", this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures); dock->setMinimumWidth(265);
    auto *panel = new QWidget; auto *layout = new QVBoxLayout(panel);
    auto *hint = new QLabel("Top layer appears first.\nDouble-click a name to rename."); layout->addWidget(hint);
    layers = new QListWidget; layers->setObjectName("layers"); layers->setAccessibleName("Layers"); layout->addWidget(layers, 1);
    auto *paintLayer = new QPushButton("Add paint layer"); paintLayer->setObjectName("addPaintLayer"); layout->addWidget(paintLayer);
    connect(paintLayer, &QPushButton::clicked, this, [this] {
        edit("Add paint layer", [](auto &d) {
            qint64 pixels = qint64(d.size.width()) * d.size.height();
            for (const auto &layer : d.layers) pixels += qint64(layer.image.width()) * layer.image.height();
            if (pixels > Arc::MaxPixels) throw std::runtime_error("A new paint layer would exceed the 100-megapixel source limit.");
            Arc::Layer layer; layer.name = "Paint layer"; layer.size = d.size;
            layer.image = QImage(d.size, QImage::Format_ARGB32_Premultiplied);
            if (layer.image.isNull()) throw std::runtime_error("Insufficient memory for paint layer.");
            layer.image.fill(Qt::transparent); layer.image.setColorSpace(QColorSpace::SRgb);
            int index = d.active < 0 ? int(d.layers.size()) : d.active+1;
            d.layers.insert(index, layer); d.active = index;
        });
    });
    auto *buttons = new QHBoxLayout;
    auto button = [&](const QString &label, auto callback) {
        auto *b = new QPushButton(label); buttons->addWidget(b); connect(b, &QPushButton::clicked, this, callback);
    };
    button("+ Copy", [this] { edit("Duplicate layer", [](auto &d) { if (d.active < 0) return; auto l = d.layers[d.active]; l.id = QUuid::createUuid(); l.name += " copy"; d.layers.insert(++d.active, l); }); });
    button("−", [this] { edit("Delete layer", [](auto &d) { if (d.active < 0) return; d.layers.removeAt(d.active); d.active = std::min(d.active, int(d.layers.size())-1); }); });
    button("↑", [this] { edit("Raise layer", [](auto &d) { if (d.active >= 0 && d.active+1 < d.layers.size()) { d.layers.swapItemsAt(d.active, d.active+1); ++d.active; } }); });
    button("↓", [this] { edit("Lower layer", [](auto &d) { if (d.active > 0) { d.layers.swapItemsAt(d.active, d.active-1); --d.active; } }); });
    layout->addLayout(buttons);
    auto *maskRow = new QHBoxLayout;
    addMask = new QPushButton("Add mask"); addMask->setObjectName("addMask");
    removeMask = new QPushButton("Remove mask"); removeMask->setObjectName("removeMask");
    maskRow->addWidget(addMask); maskRow->addWidget(removeMask); layout->addLayout(maskRow);
    connect(addMask, &QPushButton::clicked, this, [this] {
        editLayer("Add layer mask", [](auto &layer) {
            if (!layer.mask.isNull()) return;
            layer.mask = QImage(1,1,QImage::Format_Grayscale8); layer.mask.fill(Qt::white); layer.maskEnabled = true;
        });
        if (document.active >= 0 && !document.layers[document.active].mask.isNull()) paintTarget->setCurrentIndex(1);
    });
    connect(removeMask, &QPushButton::clicked, this, [this] {
        editLayer("Remove layer mask", [](auto &layer) { layer.mask = QImage(); layer.maskEnabled = true; });
    });
    maskEnabled = new QCheckBox("Enable mask"); maskEnabled->setObjectName("maskEnabled"); layout->addWidget(maskEnabled);
    connect(maskEnabled, &QCheckBox::toggled, this, [this](bool value) {
        if (!refreshing) editLayer("Toggle layer mask", [=](auto &layer) { layer.maskEnabled = value; });
    });
    paintTarget = new QComboBox; paintTarget->setObjectName("paintTarget"); paintTarget->addItems({"Paint image", "Paint mask"});
    layout->addWidget(paintTarget);
    connect(paintTarget, &QComboBox::currentIndexChanged, this, [this](int value) { canvas->setMaskTarget(value == 1); });
    maskPreview = new QLabel; maskPreview->setToolTip("Mask coverage: black hides, white reveals. The eraser restores white.");
    layout->addWidget(maskPreview);
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
    bind(w, [](auto &l, double v) { l.size.setWidth(v); if(!l.shape.isEmpty()) l.image=Arc::shapeImage(l.shape,l.size); }); bind(h, [](auto &l, double v) { l.size.setHeight(v); if(!l.shape.isEmpty()) l.image=Arc::shapeImage(l.shape,l.size); });
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
    connect(canvas, &Canvas::shapeCreated, this, [this](QString kind,QPointF start,QPointF end,QColor color) {
        edit("Add shape",[&](auto &d) {
            auto box=QRectF(start,end).normalized();
            if(kind=="Line") box.adjust(-2,-2,2,2);
            if(box.width()<1 || box.height()<1) return;
            Arc::Layer layer; layer.name=kind; layer.origin=box.topLeft(); layer.size=box.size();
            layer.shape=Arc::shapeStyle(kind,color);
            if(kind=="Line") {
                layer.shape["lineWidth"]=4;
                layer.shape["start"]=QJsonArray{(start.x()-box.x())/box.width(),(start.y()-box.y())/box.height()};
                layer.shape["end"]=QJsonArray{(end.x()-box.x())/box.width(),(end.y()-box.y())/box.height()};
            }
            layer.image=Arc::shapeImage(layer.shape,layer.size);
            int index=d.active+1; d.layers.insert(index,layer); d.active=index;
        });
    });
    connect(canvas, &Canvas::textRequested, this, [this](QRectF bounds,QColor color) { textDialog(bounds,color); });
    connect(canvas, &Canvas::selected, this, [this](int index) { document.active = index; refresh(); });
    connect(canvas, &Canvas::moved, this, [this](int index, QPointF point) { edit("Move layer", [=](auto &d) { d.layers[index].origin = point; }); });
    connect(canvas, &Canvas::painted, this, [this](int index, const QImage &image) {
        edit("Paint stroke", [&](auto &d) { d.layers[index].image = image; Arc::rasterize(d.layers[index]); });
    });
    connect(canvas, &Canvas::maskPainted, this, [this](int index, const QImage &mask) {
        edit("Paint layer mask", [&](auto &d) { d.layers[index].mask = mask; });
    });
    connect(canvas, &Canvas::errorOccurred, this, [this](const QString &message) { QMessageBox::warning(this, "Painting", message); });
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
    const bool hasLayer = document.active >= 0;
    const bool hasMask = hasLayer && !document.layers[document.active].mask.isNull();
    addMask->setEnabled(hasLayer && !hasMask && !document.layers[document.active].image.isNull());
    removeMask->setEnabled(hasMask); maskEnabled->setEnabled(hasMask);
    maskEnabled->setChecked(hasMask && document.layers[document.active].maskEnabled);
    paintTarget->setEnabled(hasMask);
    if (!hasMask) paintTarget->setCurrentIndex(0);
    if (hasMask) maskPreview->setPixmap(QPixmap::fromImage(document.layers[document.active].mask.scaled(64,48,Qt::IgnoreAspectRatio)));
    else maskPreview->clear();
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
void Window::resizeDialog(bool resample) {
    QDialog dialog(this); dialog.setWindowTitle(resample ? "Image size" : "Canvas size");
    QFormLayout form(&dialog);
    QSpinBox width,height; width.setRange(1,30000); height.setRange(1,30000);
    width.setValue(document.size.width()); height.setValue(document.size.height());
    QDoubleSpinBox resolution; resolution.setRange(1,9600); resolution.setValue(document.resolution);
    QCheckBox centered("Keep canvas contents centered"); centered.setChecked(true);
    form.addRow("Width (px)",&width); form.addRow("Height (px)",&height);
    if (resample) form.addRow("Resolution (pixels/inch)",&resolution); else form.addRow(&centered);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); form.addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if (dialog.exec()!=QDialog::Accepted) return;
    edit(resample ? "Resize image" : "Resize canvas",[&](auto &d) {
        if (resample) { Arc::resizeImage(d,{width.value(),height.value()}); d.resolution=resolution.value(); }
        else Arc::resizeCanvas(d,{width.value(),height.value()},centered.isChecked());
    });
    canvas->fit();
}
void Window::filterDialog(const QString &kind) {
    if(document.active<0) { statusBar()->showMessage("Select an image layer first.",5000); return; }
    const int index=document.active; const bool mask=paintTarget->currentIndex()==1;
    const auto original=document;
    const QImage source=mask ? original.layers[index].mask : original.layers[index].image;
    if(source.isNull()) return;
    const auto selection=canvas->selectedPath();
    Arc::FilterSettings settings=Arc::defaultFilter(kind);
    QDialog dialog(this); dialog.setWindowTitle(kind); dialog.setMinimumWidth(350);
    QFormLayout form(&dialog); QMap<QString,QDoubleSpinBox *> fields;
    for(const auto &parameter:Arc::filterParameters(kind)) {
        auto *field=new QDoubleSpinBox; field->setRange(parameter.minimum,parameter.maximum);
        field->setDecimals(3); field->setValue(parameter.initial); field->setKeyboardTracking(false);
        fields[parameter.key]=field; form.addRow(parameter.label,field);
    }
    QComboBox *channel=nullptr;
    if(kind=="Levels" || kind=="Curves") {
        channel=new QComboBox; channel->addItems({"RGB","Red","Green","Blue"}); form.addRow("Channel",channel);
    }
    QPlainTextEdit *curve=nullptr;
    if(kind=="Curves") {
        curve=new QPlainTextEdit("0, 0\n128, 128\n255, 255");
        form.addRow("Curve points (input, output)",curve);
    }
    QCheckBox *gaussian=nullptr,*monochromatic=nullptr;
    if(kind=="Noise") {
        gaussian=new QCheckBox("Gaussian distribution"); monochromatic=new QCheckBox("Monochromatic");
        form.addRow(gaussian); form.addRow(monochromatic);
    }
    QTimer timer; timer.setSingleShot(true); timer.setInterval(100);
    auto schedule=[&] { timer.start(); };
    if(kind=="Gradient Map") {
        auto *dark=new QPushButton("Choose shadow color"),*light=new QPushButton("Choose highlight color");
        form.addRow(dark,light);
        connect(dark,&QPushButton::clicked,&dialog,[&] { auto c=QColorDialog::getColor(settings.shadows,&dialog); if(c.isValid()) { settings.shadows=c; schedule(); } });
        connect(light,&QPushButton::clicked,&dialog,[&] { auto c=QColorDialog::getColor(settings.highlights,&dialog); if(c.isValid()) { settings.highlights=c; schedule(); } });
    }
    if(kind=="Levels") {
        auto *automatic=new QPushButton("Auto levels"); form.addRow(automatic);
        connect(automatic,&QPushButton::clicked,&dialog,[&] { report([&] { auto range=Arc::autoLevels(source); fields["black"]->setValue(range.first); fields["white"]->setValue(range.second); }); });
    }
    QCheckBox preview("Live preview"); preview.setChecked(true); form.addRow(&preview);
    QLabel error; error.setWordWrap(true); form.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); form.addRow(&buttons);
    QImage result; bool valid=false;
    auto update=[&] {
        try {
            for(auto i=fields.begin();i!=fields.end();++i) settings.values[i.key()]=i.value()->value();
            if(channel) settings.channel=channel->currentIndex();
            if(gaussian) { settings.values["gaussian"]=gaussian->isChecked(); settings.values["monochromatic"]=monochromatic->isChecked(); }
            if(curve) {
                settings.curve.clear();
                for(auto line:curve->toPlainText().split('\n',Qt::SkipEmptyParts)) {
                    auto values=line.split(','); bool xok=false,yok=false;
                    double x=values.value(0).trimmed().toDouble(&xok),y=values.value(1).trimmed().toDouble(&yok);
                    if(values.size()!=2 || !xok || !yok) throw std::runtime_error("Enter one input, output pair per line.");
                    settings.curve.append({x,y});
                }
            }
            result=Arc::filterLayer(original.layers[index],settings,selection,mask);
            auto shown=original;
            if(preview.isChecked()) { if(mask) shown.layers[index].mask=result; else shown.layers[index].image=result; }
            canvas->setDocument(shown); error.clear(); valid=true;
        } catch(const std::exception &e) { error.setText(QString::fromUtf8(e.what())); valid=false; canvas->setDocument(original); }
        buttons.button(QDialogButtonBox::Ok)->setEnabled(valid);
    };
    connect(&timer,&QTimer::timeout,&dialog,update);
    for(auto *field:fields) connect(field,&QDoubleSpinBox::valueChanged,&dialog,schedule);
    if(channel) connect(channel,&QComboBox::currentIndexChanged,&dialog,schedule);
    if(curve) connect(curve,&QPlainTextEdit::textChanged,&dialog,schedule);
    if(gaussian) { connect(gaussian,&QCheckBox::toggled,&dialog,schedule); connect(monochromatic,&QCheckBox::toggled,&dialog,schedule); }
    connect(&preview,&QCheckBox::toggled,&dialog,schedule);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    timer.start(0); int accepted=dialog.exec(); timer.stop();
    if(accepted==QDialog::Accepted) update();
    canvas->setDocument(original);
    if(accepted==QDialog::Accepted && valid) edit(kind,[&](auto &d) { if(mask) d.layers[index].mask=result; else { d.layers[index].image=result; Arc::rasterize(d.layers[index]); } });
}
void Window::exportDialog() {
    auto path = QFileDialog::getSaveFileName(this, "Export image (JPEG uses a white background)", "Untitled.png", "PNG (*.png);;JPEG (*.jpg *.jpeg)");
    if (!path.isEmpty()) report([&] { Arc::exportImage(document, path); statusBar()->showMessage("Exported " + path, 5000); });
}
void Window::closeEvent(QCloseEvent *event) { if (mayDiscard()) event->accept(); else event->ignore(); }
