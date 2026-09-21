#include "window.h"
#include "adjustments.h"
#include "operations.h"
#include "selection.h"
#include "filters.h"
#include "spatial_filters.h"
#include "styles.h"
#include "hierarchy.h"
#include "clipping.h"
#include "masks.h"
#include "geometry.h"
#include "layer_tree.h"
#include <QStyle>
#include <QJsonArray>
#include <QTimer>
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
#include <QTreeWidgetItemIterator>
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
        importImages(QFileDialog::getOpenFileNames(this, "Import images", {}, "Images (*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.webp *.heic *.heif *.avif);;All files (*)"));
    });
    file->addSeparator();
    file->addAction("&Save project", QKeySequence::Save, this, [this] { save(); });
    file->addAction("Save project &as…", QKeySequence::SaveAs, this, [this] { save(true); });
    file->addAction("&Export image…", QKeySequence("Ctrl+Shift+E"), this, &Window::exportDialog);
    file->addAction("Close document",QKeySequence::Close,this,[this] { if(tabbed) emit closeRequested(); else close(); });
    file->addSeparator(); file->addAction("&Quit", QKeySequence::Quit, this,[this] { if(tabbed) emit quitRequested(); else close(); });
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
    imageMenu->addAction("Flip canvas horizontally",this,[this] { edit("Flip canvas horizontally",[](auto &d) { Arc::flipCanvas(d,true); }); });
    imageMenu->addAction("Flip canvas vertically",this,[this] { edit("Flip canvas vertically",[](auto &d) { Arc::flipCanvas(d,false); }); });
    auto *filterMenu=menuBar()->addMenu("&Adjust / Filter");
    for(const auto &name:Arc::filterNames()) filterMenu->addAction(name+"…",this,[this,name] { filterDialog(name); });
    filterMenu->addSeparator();
    filterMenu->addAction("Content-aware fill",this,[this] {
        auto selection=canvas->selectedPath();
        if(!selection) { statusBar()->showMessage("Select a region to fill first.",5000); return; }
        auto coverage=canvas->selectedCoverage();
        editLayer("Content-aware fill",[&](auto &layer) { auto changed=Arc::contentAwareFill(layer,*selection); layer.image=Arc::limitToSelection(layer.image,changed,layer,coverage); Arc::rasterize(layer); });
    });
    auto *layerMenu = menuBar()->addMenu("&Layer");
    layerMenu->addAction("Group selected layers",QKeySequence("Ctrl+G"),this,[this] {
        QVector<QUuid> ids;
        for(auto *item:layers->selectedItems()) ids.append(item->data(0,Qt::UserRole+1).toUuid());
        edit("Group layers",[&](auto &d) { Arc::groupLayers(d,ids); });
    });
    layerMenu->addAction("Add folder",this,[this] {
        edit("Add folder",[](auto &d) {
            Arc::Layer group; group.isGroup=true; group.name="Folder"; group.size=d.size;
            if(d.active>=0) group.parentID=d.layers[d.active].isGroup ? d.layers[d.active].id : d.layers[d.active].parentID;
            d.layers.append(group); d.active=d.layers.size()-1;
        });
    });
    layerMenu->addAction("Merge folder",this,[this] { edit("Merge folder",Arc::mergeFolder); });
    layerMenu->addAction("Scale layer / folder…",this,[this] {
        bool ok=false; double percent=QInputDialog::getDouble(this,"Scale layer / folder","Scale (%)",100,0.01,10000,2,&ok);
        if(ok) editLayer("Scale layer / folder",[=](auto &l) {
            auto center=l.origin+QPointF(l.size.width()/2,l.size.height()/2);
            l.size*=percent/100; l.origin=center-QPointF(l.size.width()/2,l.size.height()/2);
            if(!l.shape.isEmpty()) l.image=Arc::shapeImage(l.shape,l.size);
        });
    });
    layerMenu->addAction("Move out of folder",this,[this] {
        edit("Move out of folder",[](auto &d) {
            if(d.active<0 || d.layers[d.active].parentID.isNull()) return;
            auto parent=d.layers[d.active].parentID;
            for(const auto &layer:d.layers) if(layer.id==parent) { d.layers[d.active].parentID=layer.parentID; break; }
        });
    });
    layerMenu->addAction("Copy selected layers to document…",this,[this] { emit copyLayersRequested(); });
    auto *adjustments=layerMenu->addMenu("New adjustment layer");
    for(const auto &kind:Arc::adjustmentKinds()) adjustments->addAction(kind+"…",this,[this,kind] { adjustmentDialog(kind); });
    layerMenu->addAction("Edit adjustment…",this,[this] { adjustmentDialog(); });
    layerMenu->addAction("Layer effects…",this,&Window::effectsDialog);
    layerMenu->addAction("Edit shape…",this,&Window::shapeDialog);
    layerMenu->addAction("Edit text…",this,[this] { textDialog({},Qt::black,true); });
    layerMenu->addAction("Create clipping mask",QKeySequence("Ctrl+Alt+G"),this,[this] { edit("Create clipping mask",Arc::createClippingMask); });
    layerMenu->addAction("Release clipping mask",this,[this] { edit("Release clipping mask",Arc::releaseClippingMask); });
    layerMenu->addAction("Bake clipping mask",this,[this] { edit("Bake clipping mask",[](auto &d) { if(d.active>=0) Arc::bakeClippingMask(d,d.active); }); });
    layerMenu->addAction("Merge layers / down",QKeySequence("Ctrl+E"),this,[this] {
        QVector<QUuid> ids; for(auto *item:layers->selectedItems()) ids.append(item->data(0,Qt::UserRole+1).toUuid());
        edit("Merge layers",[&](auto &d) {
            if(ids.size()>1) Arc::mergeSelected(d,ids);
            else if(d.active>=0 && d.layers[d.active].isGroup) Arc::mergeFolder(d);
            else Arc::mergeDown(d);
        });
    });
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
    view->addAction("Manage guides…",this,&Window::guidesDialog);
    auto *showGuides=view->addAction("Show guides"); showGuides->setCheckable(true); showGuides->setChecked(true);
    connect(showGuides,&QAction::toggled,canvas,&Canvas::setGuidesVisible);
    auto *lockGuides=view->addAction("Lock guides"); lockGuides->setCheckable(true);
    connect(lockGuides,&QAction::toggled,canvas,&Canvas::setGuidesLocked);
    auto *snap=view->addAction("Snap to guides, canvas and layers"); snap->setCheckable(true);
    connect(snap,&QAction::toggled,canvas,&Canvas::setSnapping);
    auto *rulers=view->addAction("Show rulers"); rulers->setCheckable(true); connect(rulers,&QAction::toggled,canvas,&Canvas::setRulersVisible);
    auto *grid=view->addAction("Show pixel grid"); grid->setCheckable(true); connect(grid,&QAction::toggled,canvas,&Canvas::setPixelGridVisible);
    view->addSeparator();
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
        report([&] { canvas->invertSelection(); });
    });
    selectMenu->addAction("Feather selection…",this,[this] {
        bool accepted=false; int radius=QInputDialog::getInt(this,"Feather selection","Radius (document pixels)",5,0,250,1,&accepted);
        if(accepted) report([&] { canvas->featherSelection(radius); });
    });
    auto loadSelection=[this](bool mask) {
        if(document.active<0) return;
        report([&] {
            auto layer=mask ? Arc::maskTargetLayer(document.layers[document.active]) : document.layers[document.active];
            if(layer.image.isNull()) return;
            auto source=mask ? layer.image.convertToFormat(QImage::Format_Grayscale8) : Arc::alphaCoverage(layer.image);
            QImage coverage(document.size,QImage::Format_Grayscale8); coverage.fill(Qt::black);
            { QPainter p(&coverage); p.setTransform(layer.transform()); p.setRenderHint(QPainter::SmoothPixmapTransform); p.drawImage(QRectF(QPointF(),layer.size),source); }
            canvas->setSelectionCoverage(coverage);
        });
    };
    selectMenu->addAction("Select layer pixels",this,[=] { loadSelection(false); });
    selectMenu->addAction("Select layer mask",this,[=] { loadSelection(true); });
    auto copyPixels=[this](bool merged) {
        auto coverage=canvas->selectedCoverage();
        QImage image;
        if(merged) {
            image=Arc::render(document);
            if(!coverage.isNull()) { QImage empty(image.size(),image.format()); empty.fill(Qt::transparent); Arc::Layer target; target.size=document.size; image=Arc::limitToSelection(empty,image,target,coverage); }
        } else image=Arc::selectedLayerPixels(document,coverage);
        if(auto selected=canvas->selectedPath()) image=image.copy(selected->boundingRect().toAlignedRect().intersected(image.rect()));
        if(!image.isNull()) QApplication::clipboard()->setImage(image);
    };
    editMenu->addAction("Copy merged",QKeySequence("Ctrl+Shift+C"),this,[this,copyPixels] { report([&] { copyPixels(true); }); });
    editMenu->addAction("Copy selected pixels",QKeySequence::Copy,this,[this,copyPixels] { report([&] { copyPixels(false); }); });
    editMenu->addAction("Cut selected pixels",QKeySequence::Cut,this,[this,copyPixels] {
        report([&] { copyPixels(false); auto coverage=canvas->selectedCoverage(); edit("Cut selected pixels",[&](auto &d) { Arc::cutSelectedPixels(d,coverage); }); });
    });
    editMenu->addAction("Duplicate selected pixels",QKeySequence("Ctrl+J"),this,[this] {
        auto coverage=canvas->selectedCoverage(); if(coverage.isNull()) { edit("Duplicate layer",Arc::duplicateLayer); return; }
        edit("Duplicate selected pixels",[&](auto &d) { Arc::floatSelectedPixels(d,coverage,{},true); });
    });
    editMenu->addAction("Paste image as layer",QKeySequence::Paste,this,[this] {
        auto image=QApplication::clipboard()->image(); if(image.isNull()) return;
        edit("Paste image",[&](auto &d) { Arc::Layer layer; layer.image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if(d.active>=0) layer.parentID=d.layers[d.active].isGroup ? d.layers[d.active].id : d.layers[d.active].parentID;
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
    addTool("Crop (C)", "C", Canvas::Tool::Crop);
    auto *optionsMenu = new QMenu(this);
    auto *aligned = optionsMenu->addAction("Aligned clone source");
    aligned->setCheckable(true); aligned->setChecked(true);
    auto *allLayers = optionsMenu->addAction("Clone from all visible layers");
    allLayers->setCheckable(true);
    auto cloneOptions = [this, aligned, allLayers] { canvas->setCloneOptions(aligned->isChecked(), allLayers->isChecked()); };
    connect(aligned, &QAction::toggled, this, cloneOptions);
    connect(allLayers, &QAction::toggled, this, cloneOptions);
    auto *gradientMenu=optionsMenu->addMenu("Gradient");
    auto *radial=gradientMenu->addAction("Radial"); radial->setCheckable(true);
    auto *transparent=gradientMenu->addAction("Foreground to transparent"); transparent->setCheckable(true);
    auto *reverse=gradientMenu->addAction("Reverse"); reverse->setCheckable(true);
    auto gradientOptions=[this,radial,transparent,reverse] { canvas->setGradientOptions({radial->isChecked(),transparent->isChecked(),reverse->isChecked()}); };
    for(auto *action:{radial,transparent,reverse}) connect(action,&QAction::toggled,this,gradientOptions);
    auto *healingMenu=optionsMenu->addMenu("Healing mode"); auto *healingGroup=new QActionGroup(this);
    QStringList healingNames{"Content-Aware","Create Texture","Proximity Match"};
    for(int mode=0;mode<healingNames.size();++mode) {
        auto *action=healingMenu->addAction(healingNames[mode]); action->setCheckable(true); action->setChecked(mode==0); healingGroup->addAction(action);
        connect(action,&QAction::triggered,this,[this,mode] { canvas->setHealingMode(mode); });
    }
    auto *contiguous=optionsMenu->addAction("Contiguous magic wand"); contiguous->setCheckable(true); contiguous->setChecked(true);
    connect(contiguous,&QAction::toggled,canvas,&Canvas::setWandContiguous);
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
    layers = new LayerTree;
    layers->transferLayers=[this] { return selectedLayersForTransfer(); }; layers->setObjectName("layers"); layers->setAccessibleName("Layers"); layout->addWidget(layers, 1);
    auto *paintLayer = new QPushButton("Add paint layer"); paintLayer->setObjectName("addPaintLayer"); layout->addWidget(paintLayer);
    connect(paintLayer, &QPushButton::clicked, this, [this] {
        edit("Add paint layer", [](auto &d) {
            qint64 pixels = qint64(d.size.width()) * d.size.height();
            for (const auto &layer : d.layers) pixels += qint64(layer.image.width()) * layer.image.height();
            if (pixels > Arc::MaxPixels) throw std::runtime_error("A new paint layer would exceed the 100-megapixel source limit.");
            Arc::Layer layer; if(d.active>=0) layer.parentID=d.layers[d.active].isGroup ? d.layers[d.active].id : d.layers[d.active].parentID;
            layer.name = "Paint layer"; layer.size = d.size;
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
    button("+ Copy", [this] { edit("Duplicate layer", Arc::duplicateLayer); });
    button("−", [this] { edit("Delete layer", Arc::deleteLayer); });
    button("↑", [this] { edit("Raise layer", [](auto &d) { Arc::reorderLayer(d,true); }); });
    button("↓", [this] { edit("Lower layer", [](auto &d) { Arc::reorderLayer(d,false); }); });
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
        editLayer("Remove layer mask", [](auto &layer) { layer.mask = QImage(); layer.maskEnabled = true; layer.maskLinked=true; layer.maskPlacement={}; });
    });
    maskEnabled = new QCheckBox("Enable mask"); maskEnabled->setObjectName("maskEnabled"); layout->addWidget(maskEnabled);
    connect(maskEnabled, &QCheckBox::toggled, this, [this](bool value) {
        if (!refreshing) editLayer("Toggle layer mask", [=](auto &layer) { layer.maskEnabled = value; });
    });
    maskLinked=new QCheckBox("Link mask to layer"); maskLinked->setObjectName("maskLinked"); layout->addWidget(maskLinked);
    connect(maskLinked,&QCheckBox::toggled,this,[this](bool linked) {
        if(refreshing) return;
        editLayer("Link / unlink mask",[=](auto &l) {
            if(!linked && l.maskPlacement.isEmpty()) l.maskPlacement=Arc::placementOf(l);
            l.maskLinked=linked;
        });
    });
    paintTarget = new QComboBox; paintTarget->setObjectName("paintTarget"); paintTarget->addItems({"Paint image", "Paint mask"});
    layout->addWidget(paintTarget);
    connect(paintTarget, &QComboBox::currentIndexChanged, this, [this](int value) { canvas->setMaskTarget(value == 1); if(!refreshing) refresh(); });
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
    auto bind = [this](QDoubleSpinBox *s, auto change, bool geometry=true) {
        connect(s,&QDoubleSpinBox::valueChanged,this,[this,change,geometry](double value) {
            if(refreshing) return;
            auto operation=[=](Arc::Layer &l) { change(l,value); };
            if(geometry) editGeometry("Transform layer / mask",operation);
            else editLayer("Layer appearance",operation);
        });
    };
    bind(x, [](auto &l, double v) { l.origin.setX(v); }); bind(y, [](auto &l, double v) { l.origin.setY(v); });
    bind(w, [](auto &l, double v) { l.size.setWidth(v); if(!l.shape.isEmpty()) l.image=Arc::shapeImage(l.shape,l.size); }); bind(h, [](auto &l, double v) { l.size.setHeight(v); if(!l.shape.isEmpty()) l.image=Arc::shapeImage(l.shape,l.size); });
    bind(angle, [](auto &l, double v) { l.rotation = v; }); bind(opacity, [](auto &l, double v) { l.opacity = v/100; },false);
    connect(blend, &QComboBox::currentTextChanged, this, [this](const QString &value) { if (!refreshing) editLayer("Blend mode", [&](auto &l) { l.blend = value; }); });
    auto *flipX = new QPushButton("Flip horizontal"), *flipY = new QPushButton("Flip vertical");
    form->addRow(flipX, flipY);
    connect(flipX, &QPushButton::clicked, this, [this] { editGeometry("Flip horizontal", [](auto &l) { l.flipX = !l.flipX; }); });
    connect(flipY, &QPushButton::clicked, this, [this] { editGeometry("Flip vertical", [](auto &l) { l.flipY = !l.flipY; }); });
    layout->addWidget(inspector); dock->setWidget(panel); addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(layers, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if(refreshing) return;
        document.active=item ? item->data(0,Qt::UserRole).toInt() : -1;
        refresh();
    });
    connect(layers, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if(refreshing || column!=0) return;
        int index=item->data(0,Qt::UserRole).toInt();
        QString name=item->text(0); bool visible=item->checkState(0)==Qt::Checked;
        edit("Layer properties",[=](auto &d) { d.layers[index].name=name; d.layers[index].visible=visible; });
    });
    connect(layers,&LayerTree::orderChanged,this,[this] {
        QVector<Arc::Layer> ordered;
        QUuid active=document.active>=0 ? document.layers[document.active].id : QUuid();
        std::function<void(QTreeWidgetItem *,QUuid)> collect=[&](QTreeWidgetItem *parent,QUuid parentID) {
            for(int row=parent->childCount()-1;row>=0;--row) {
                auto *item=parent->child(row);
                auto layer=document.layers[item->data(0,Qt::UserRole).toInt()]; layer.parentID=parentID;
                ordered.append(layer); collect(item,layer.id);
            }
        };
        collect(layers->invisibleRootItem(),{});
        edit("Reorder layers",[&](auto &d) { d.layers=ordered; for(int i=0;i<d.layers.size();++i) if(d.layers[i].id==active) d.active=i; });
        refresh();
    });
    connect(canvas, &Canvas::shapeCreated, this, [this](QString kind,QPointF start,QPointF end,QColor color) {
        edit("Add shape",[&](auto &d) {
            auto box=QRectF(start,end).normalized();
            if(kind=="Line") box.adjust(-2,-2,2,2);
            if(box.width()<1 || box.height()<1) return;
            Arc::Layer layer; if(d.active>=0) layer.parentID=d.layers[d.active].isGroup ? d.layers[d.active].id : d.layers[d.active].parentID;
            layer.name=kind; layer.origin=box.topLeft(); layer.size=box.size();
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
    connect(canvas, &Canvas::textRequested, this, [this](QRectF bounds,QColor color) {
        if(bounds.width()<2 && bounds.height()<2) for(int i:Arc::visibleLayerOrder(document,true)) {
            const auto &layer=document.layers[i];
            if(!layer.text.isEmpty() && QRectF(QPointF(),layer.size).contains(layer.transform().inverted().map(bounds.topLeft()))) {
                document.active=i; refresh(); canvas->beginTextEditing(i); return;
            }
        }
        textDialog(bounds,color);
    });
    connect(canvas,&Canvas::textEdited,this,[this](int index,const Arc::Layer &layer) {
        edit("Edit text",[&](auto &d) { d.layers[index]=layer; });
    });
    connect(layers,&QTreeWidget::itemSelectionChanged,this,[this] {
        if(refreshing) return;
        QVector<QUuid> ids; for(auto *item:layers->selectedItems()) ids.append(item->data(0,Qt::UserRole+1).toUuid());
        canvas->setSelectedLayers(ids);
    });
    connect(canvas,&Canvas::layersTransformed,this,[this](const Arc::Document &result) { edit("Transform layers",[&](auto &d) { d=result; }); });
    connect(canvas, &Canvas::selected, this, [this](int index,bool extend) {
        if(!extend || index<0) { QSignalBlocker block(layers); layers->clearSelection(); }
        document.active=index; refresh();
    });
    connect(canvas,&Canvas::guideAdded,this,[this](Arc::Guide guide) { edit("Add guide",[&](auto &d) { d.guides.append(guide); }); });
    connect(canvas,&Canvas::cropRequested,this,[this](QRect bounds) { edit("Crop canvas",[&](auto &d) { Arc::crop(d,bounds); }); });
    connect(canvas, &Canvas::guideMoved, this, [this](int index,double position) {
        edit("Move guide",[=](auto &d) { d.guides[index].position=position; });
    });
    connect(canvas,&Canvas::maskMoved,this,[this](int index,QJsonObject placement) {
        edit("Move mask",[&](auto &d) { d.layers[index].maskPlacement=placement; });
    });
    connect(canvas, &Canvas::moved, this, [this](int index, QPointF point) { edit("Move layer", [=](auto &d) { auto changed=d.layers[index]; changed.origin=point; Arc::transformGroup(d,index,changed); }); });
    connect(canvas, &Canvas::painted, this, [this](int index, const QImage &image) {
        edit("Paint stroke", [&](auto &d) { d.layers[index].image = image; Arc::rasterize(d.layers[index]); });
    });
    connect(canvas, &Canvas::maskPainted, this, [this](int index, const QImage &mask) {
        edit("Paint layer mask", [&](auto &d) { d.layers[index].mask = mask; });
    });
    connect(canvas,&Canvas::selectionPixelsMoved,this,[this](QImage coverage,QPointF offset,bool duplicate) {
        edit(duplicate ? "Duplicate selected pixels" : "Move selected pixels",[&](auto &d) { Arc::floatSelectedPixels(d,coverage,offset,duplicate); });
    });
    connect(canvas, &Canvas::errorOccurred, this, [this](const QString &message) { QMessageBox::warning(this, "Painting", message); });
    connect(canvas, &Canvas::filesDropped, this, &Window::importImages);
    zoomLabel = new QLabel; statusBar()->addPermanentWidget(zoomLabel);
    connect(canvas, &Canvas::zoomChanged, this, [this](double value) { zoomLabel->setText(QString::number(value*100, 'f', 0) + "%"); });
    connect(&history, &QUndoStack::cleanChanged, this, [this] { setWindowModified(!history.isClean()); emit documentStatusChanged(); });
    statusBar()->showMessage("Drag to move • Wheel to zoom • Space-drag to pan • Ctrl+I to import");
    refresh();
}
void Window::refresh() {
    refreshing = true;
    QSet<QUuid> collapsed, selected;
    for(QTreeWidgetItemIterator it(layers); *it; ++it) {
        auto id=(*it)->data(0,Qt::UserRole+1).toUuid();
        if(!(*it)->isExpanded()) collapsed.insert(id);
        if((*it)->isSelected()) selected.insert(id);
    }
    layers->clear();
    QHash<QUuid,QTreeWidgetItem *> items;
    for(int i:Arc::layerOrder(document,true)) {
        const auto &l=document.layers[i];
        auto *item=l.parentID.isNull() ? new QTreeWidgetItem(layers) : new QTreeWidgetItem(items[l.parentID]);
        item->setText(0,l.name); item->setText(1,l.maskSourceID.isNull() ? "" : "↳");
        if(!l.maskSourceID.isNull()) for(const auto &source:document.layers) if(source.id==l.maskSourceID)
            item->setToolTip(1,"Clipped to "+source.name);
        item->setData(0,Qt::UserRole,i); item->setData(0,Qt::UserRole+1,l.id);
        auto flags=item->flags()|Qt::ItemIsEditable|Qt::ItemIsUserCheckable;
        if(!l.isGroup) flags &= ~Qt::ItemIsDropEnabled;
        item->setFlags(flags); item->setCheckState(0,l.visible ? Qt::Checked : Qt::Unchecked);
        item->setIcon(0,l.isGroup ? style()->standardIcon(QStyle::SP_DirIcon)
            : !l.adjustment.isEmpty() ? style()->standardIcon(QStyle::SP_FileDialogDetailedView)
            : l.image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(l.image.scaled(48,48,Qt::KeepAspectRatio,Qt::SmoothTransformation))));
        item->setExpanded(!collapsed.contains(l.id)); item->setSelected(selected.contains(l.id) || i==document.active); items[l.id]=item;
        if(i==document.active) layers->setCurrentItem(item,0,QItemSelectionModel::NoUpdate);
    }
    inspector->setEnabled(document.active >= 0);
    if (document.active >= 0) {
        const auto &active = document.layers[document.active];
        auto l=paintTarget->currentIndex()==1 && !active.maskLinked ? Arc::maskTargetLayer(active) : active;
        QVector<QUuid> ids; for(auto *item:layers->selectedItems()) ids.append(item->data(0,Qt::UserRole+1).toUuid());
        if(ids.size()>1 && paintTarget->currentIndex()==0) l=Arc::transformBox(document,ids);
        x->setValue(l.origin.x()); y->setValue(l.origin.y()); w->setValue(l.size.width()); h->setValue(l.size.height());
        angle->setValue(l.rotation); opacity->setValue(active.opacity*100); blend->setCurrentText(active.blend); blend->setEnabled(!active.isGroup);
    }
    const bool hasLayer = document.active >= 0;
    const bool hasMask = hasLayer && !document.layers[document.active].mask.isNull();
    addMask->setEnabled(hasLayer && !hasMask && (document.layers[document.active].isGroup || !document.layers[document.active].adjustment.isEmpty() || !document.layers[document.active].image.isNull()));
    removeMask->setEnabled(hasMask); maskEnabled->setEnabled(hasMask);
    maskEnabled->setChecked(hasMask && document.layers[document.active].maskEnabled);
    maskLinked->setEnabled(hasMask); maskLinked->setChecked(hasMask && document.layers[document.active].maskLinked);
    paintTarget->setEnabled(hasMask);
    if (!hasMask) paintTarget->setCurrentIndex(0);
    if (hasMask) maskPreview->setPixmap(QPixmap::fromImage(document.layers[document.active].mask.scaled(64,48,Qt::IgnoreAspectRatio)));
    else maskPreview->clear();
    canvas->setDocument(document);
    QVector<QUuid> selectedIDs; for(auto *item:layers->selectedItems()) selectedIDs.append(item->data(0,Qt::UserRole+1).toUuid());
    canvas->setSelectedLayers(selectedIDs);
    setWindowTitle((projectPath.isEmpty() ? "Untitled" : QFileInfo(projectPath).fileName()) + "[*] — Compositor ARC");
    setWindowModified(!history.isClean()); refreshing = false; emit documentStatusChanged();
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
void Window::editGeometry(const QString &name, const std::function<void(Arc::Layer &)> &operation) {
    if(document.active<0) return;
    QVector<QUuid> ids; for(auto *item:layers->selectedItems()) ids.append(item->data(0,Qt::UserRole+1).toUuid());
    if(ids.size()>1 && paintTarget->currentIndex()==0) {
        edit(name,[&](auto &d) { auto before=Arc::transformBox(d,ids),after=before; operation(after); Arc::transformLayers(d,ids,before,after); }); return;
    }
    const auto &active=document.layers[document.active];
    if(paintTarget->currentIndex()!=1 || active.maskLinked || active.mask.isNull()) { editLayer(name,operation); return; }
    edit(name,[&](auto &d) {
        auto target=Arc::maskTargetLayer(d.layers[d.active]); operation(target);
        d.layers[d.active].maskPlacement=Arc::placementOf(target);
    });
}
void Window::editLayer(const QString &name, const std::function<void(Arc::Layer &)> &operation) {
    if (document.active >= 0) edit(name, [&](auto &d) { auto changed=d.layers[d.active]; operation(changed); Arc::transformGroup(d,d.active,changed); });
}
void Window::importImages(const QStringList &paths) {
    if (paths.isEmpty()) return;
    edit("Import images", [&](auto &d) {
        qint64 pixels = 0; for (const auto &l : d.layers) pixels += qint64(l.image.width())*l.image.height();
        for (const auto &path : paths) {
            Arc::Layer l; if(d.active>=0) l.parentID=d.layers[d.active].isGroup ? d.layers[d.active].id : d.layers[d.active].parentID;
            l.image = Arc::readImage(path);
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
    if(!canvas->finishTextEditing()) return false;
    if (history.isClean()) return true;
    auto answer = QMessageBox::warning(this, "Unsaved changes", "Save your project before continuing?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save());
}
bool Window::save(bool choosePath) {
    if(!canvas->finishTextEditing()) return false;
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
bool Window::openProject(const QString &path) {
    bool opened=false;
    report([&] {
        auto next = Arc::loadProject(path);
        if (!mayDiscard()) return;
        document = next; projectPath = path; sizeFirstImport = false; history.clear(); refresh(); canvas->fit(); opened=true;
    });
    return opened;
}
void Window::chooseProject() {
    auto path = QFileDialog::getExistingDirectory(this, "Select a .comp project folder");
    if (!path.isEmpty()) { if(tabbed) emit openRequested(path); else openProject(path); }
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
        if(tabbed) { emit newDocumentRequested(next); return; }
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
    if(Arc::adjustmentKinds().contains(kind)) { adjustmentDialog(kind,true); return; }
    if(document.active<0) { statusBar()->showMessage("Select an image layer first.",5000); return; }
    const int index=document.active; const bool mask=paintTarget->currentIndex()==1;
    const auto original=document;
    const QImage source=mask ? original.layers[index].mask : original.layers[index].image;
    if(source.isNull()) return;
    const auto selection=canvas->selectedPath();
    const auto selectionCoverage=canvas->selectedCoverage();
    Arc::FilterSettings settings=Arc::defaultFilter(kind);
    QDialog dialog(this); dialog.setWindowTitle(kind); dialog.setMinimumWidth(350);
    QFormLayout form(&dialog); QMap<QString,QDoubleSpinBox *> fields;
    for(const auto &parameter:Arc::filterParameters(kind)) {
        auto *field=new QDoubleSpinBox; field->setRange(parameter.minimum,parameter.maximum);
        field->setDecimals(3); field->setValue(parameter.initial); field->setKeyboardTracking(false);
        fields[parameter.key]=field; form.addRow(parameter.label,field);
    }
    QCheckBox *gaussian=nullptr,*monochromatic=nullptr;
    if(kind=="Noise") {
        gaussian=new QCheckBox("Gaussian distribution"); monochromatic=new QCheckBox("Monochromatic");
        form.addRow(gaussian); form.addRow(monochromatic);
    }
    QTimer timer; timer.setSingleShot(true); timer.setInterval(100);
    auto schedule=[&] { timer.start(); };
    QCheckBox preview("Live preview"); preview.setChecked(true); form.addRow(&preview);
    QLabel error; error.setWordWrap(true); form.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); form.addRow(&buttons);
    QImage result; Arc::Layer expanded; bool valid=false;
    const bool spreads=!mask && !selection && (kind=="Gaussian Blur" || kind=="Motion Blur");
    auto update=[&] {
        try {
            for(auto i=fields.begin();i!=fields.end();++i) settings.values[i.key()]=i.value()->value();
            if(gaussian) { settings.values["gaussian"]=gaussian->isChecked(); settings.values["monochromatic"]=monochromatic->isChecked(); }
            if(spreads) { expanded=Arc::expandedBlur(original.layers[index],settings); result=expanded.image; }
            else result=Arc::filterLayer(original.layers[index],settings,selectionCoverage.isNull() ? selection : std::nullopt,mask);
            if(!selectionCoverage.isNull()) result=Arc::limitToSelection(source,result,mask ? Arc::maskTargetLayer(original.layers[index]) : original.layers[index],selectionCoverage);
            auto shown=original;
            if(preview.isChecked()) { if(spreads) shown.layers[index]=expanded; else if(mask) shown.layers[index].mask=result; else shown.layers[index].image=result; }
            canvas->setDocument(shown); error.clear(); valid=true;
        } catch(const std::exception &e) { error.setText(QString::fromUtf8(e.what())); valid=false; canvas->setDocument(original); }
        buttons.button(QDialogButtonBox::Ok)->setEnabled(valid);
    };
    connect(&timer,&QTimer::timeout,&dialog,update);
    for(auto *field:fields) connect(field,&QDoubleSpinBox::valueChanged,&dialog,schedule);
    if(gaussian) { connect(gaussian,&QCheckBox::toggled,&dialog,schedule); connect(monochromatic,&QCheckBox::toggled,&dialog,schedule); }
    connect(&preview,&QCheckBox::toggled,&dialog,schedule);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    timer.start(0); int accepted=dialog.exec(); timer.stop();
    if(accepted==QDialog::Accepted) update();
    canvas->setDocument(original);
    if(accepted==QDialog::Accepted && valid) edit(kind,[&](auto &d) { if(spreads) d.layers[index]=expanded; else if(mask) d.layers[index].mask=result; else { d.layers[index].image=result; Arc::rasterize(d.layers[index]); } });
}
void Window::closeEvent(QCloseEvent *event) { if (mayDiscard()) event->accept(); else event->ignore(); }

QString Window::documentTitle() const { return projectPath.isEmpty() ? "Untitled" : QFileInfo(projectPath).fileName(); }
void Window::initializeDocument(const Arc::Document &d) {
    Arc::validate(d); document=d; projectPath.clear(); history.clear(); sizeFirstImport=false; refresh();
}
QVector<Arc::Layer> Window::selectedLayersForTransfer() const {
    QSet<QUuid> chosen;
    for(auto *item:layers->selectedItems()) {
        auto id=item->data(0,Qt::UserRole+1).toUuid(); chosen.insert(id);
        for(int i:Arc::descendants(document,id)) chosen.insert(document.layers[i].id);
    }
    auto source=document;
    for(int i=0;i<source.layers.size();++i) if(chosen.contains(source.layers[i].id)
        && !source.layers[i].maskSourceID.isNull() && !chosen.contains(source.layers[i].maskSourceID)) Arc::bakeClippingMask(source,i);
    QHash<QUuid,QUuid> ids; for(auto id:chosen) ids[id]=QUuid::createUuid();
    QVector<Arc::Layer> result;
    for(auto layer:source.layers) if(chosen.contains(layer.id)) {
        layer.id=ids[layer.id]; layer.parentID=ids.value(layer.parentID); layer.maskSourceID=ids.value(layer.maskSourceID); result.append(layer);
    }
    return result;
}
void Window::receiveLayers(const QVector<Arc::Layer> &incoming) {
    if(incoming.isEmpty()) return;
    edit("Copy layers from document",[&](auto &d) {
        QUuid parent; if(d.active>=0) parent=d.layers[d.active].isGroup ? d.layers[d.active].id : d.layers[d.active].parentID;
        for(auto layer:incoming) { if(layer.parentID.isNull()) layer.parentID=parent; d.layers.append(layer); }
        d.active=d.layers.size()-1;
    });
}
