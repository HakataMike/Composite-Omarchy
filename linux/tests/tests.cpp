#include "../src/document.h"
#include "../src/adjustments.h"
#include "../src/effects.h"
#include "../src/curve_editor.h"
#include "../src/spatial_filters.h"
#include "../src/geometry.h"
#include <QDialogButtonBox>
#include "../src/operations.h"
#include "../src/selection.h"
#include "../src/filters.h"
#include "../src/retouch.h"
#include "../src/styles.h"
#include "../src/blending.h"
#include <QPlainTextEdit>
#include "../src/canvas.h"
#include "../src/window.h"
#include "../src/workspace.h"
#include <QTabWidget>
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QListWidget>
#include <QTreeWidget>
#include "../src/hierarchy.h"
#include "../src/clipping.h"
#include "../src/masks.h"
#include <QDoubleSpinBox>
#include <QAction>
#include <QMessageBox>
#include <QColorSpace>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>

class Tests : public QObject {
    Q_OBJECT
    Arc::Document sample() {
        Arc::Document d; d.size = {32, 24};
        Arc::Layer bottom; bottom.name = "Red"; bottom.image = QImage(16,12,QImage::Format_ARGB32_Premultiplied);
        bottom.image.fill(Qt::red); bottom.image.setColorSpace(QColorSpace::SRgb); bottom.size = {16,12}; bottom.origin = {2,3};
        d.layers.append(bottom); d.active = 0; return d;
    }
    QJsonObject metadata(QString path) {
        QFile f(path + "/manifest.json"); if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }
    void writeMetadata(QString path, QJsonObject object) {
        QFile f(path + "/manifest.json"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QJsonDocument(object).toJson());
    }
private slots:
    void radialTransparentGradientsAndHealingModes() {
        auto d=sample(); auto layer=d.layers[0]; layer.origin={0,0}; layer.image.fill(Qt::transparent);
        Arc::Brush brush; brush.color=Qt::red;
        auto radial=Arc::gradientStroke(layer,{8,6},{12,6},brush,Qt::blue,QRectF(0,0,16,12),{},false,{true,false,false});
        QVERIFY(radial.pixelColor(8,6).red()>radial.pixelColor(12,6).red()); QVERIFY(radial.pixelColor(12,6).blue()>200);
        auto fading=Arc::gradientStroke(layer,{0,0},{8,0},brush,Qt::blue,QRectF(0,0,16,12),{},false,{false,true,false});
        QVERIFY(fading.pixelColor(0,0).alpha()>200); QCOMPARE(fading.pixelColor(10,0).alpha(),0);
        auto reversed=Arc::gradientStroke(layer,{0,0},{8,0},brush,Qt::blue,QRectF(0,0,16,12),{},false,{false,true,true});
        QVERIFY(reversed.pixelColor(10,0).alpha()>200); QVERIFY(reversed.pixelColor(0,0).alpha()<50);
        layer=d.layers[0]; QPainterPath path; path.moveTo(8,8); brush.diameter=3;
        for(int mode=0;mode<3;++mode) { auto healed=Arc::healStroke(layer,path,brush,QRectF(QPointF(),d.size),{},mode); QCOMPARE(healed.size(),layer.image.size()); }
    }
    void mergeSelectedGroupsAdjustmentsAndBlends() {
        auto d=sample(); auto top=d.layers[0]; top.id=QUuid::createUuid(); top.image.fill(Qt::green); top.blend="Multiply";
        d.layers.append(top); d.active=1; auto expected=Arc::render(d); Arc::mergeDown(d); QCOMPARE(d.layers.size(),1); QCOMPARE(Arc::render(d),expected);
        d=sample(); Arc::groupLayers(d,{d.layers[0].id}); auto groupID=d.layers[d.active].id;
        Arc::Layer adjustment; adjustment.size=d.size; adjustment.name="Hue"; auto f=Arc::defaultFilter("Hue/Saturation"); f.values["hue"]=120;
        adjustment.adjustment=Arc::adjustmentFromFilter(f,{}); d.layers.append(adjustment); d.active=d.layers.size()-1;
        expected=Arc::render(d); Arc::mergeSelected(d,{groupID,adjustment.id}); QCOMPARE(d.layers.size(),1); QCOMPARE(Arc::render(d),expected);
        QCOMPARE(d.layers[0].image.size(),QSize(16,12));
        QTemporaryDir temp; auto file=temp.filePath("merged.comp"); Arc::saveProject(d,file); QCOMPARE(Arc::render(Arc::loadProject(file)),expected);
    }
    void inlineTextApplyCancelSaveAndUndo() {
        Arc::Document d; d.size={400,200}; Arc::Layer text; text.name="Text"; text.text=Arc::textStyle(Qt::black);
        text.text["fontSize"]=20; text.text["boxSize"]=QJsonArray{200,100}; text.image=Arc::textImage(text.text); text.size=text.image.size(); text.origin={20,20}; d.layers={text}; d.active=0;
        QTemporaryDir temp; auto file=temp.filePath("inline.comp"); Arc::saveProject(d,file);
        Window window; window.show(); window.openProject(file); auto *canvas=window.findChild<Canvas *>(); canvas->fit(); canvas->setTool(Canvas::Tool::Text);
        QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,canvas->canvasToWidget({30,30}).toPoint());
        auto *editor=canvas->findChild<QPlainTextEdit *>("inlineTextEditor"); QVERIFY(editor && editor->isVisible());
        QTest::keyClicks(editor,"Inline edit"); QTest::keyClick(editor,Qt::Key_Return,Qt::ControlModifier);
        QAction *save=nullptr,*undo=nullptr; for(auto *a:window.findChildren<QAction *>()) { if(a->text()=="&Save project") save=a; if(a->shortcut()==QKeySequence::Undo) undo=a; }
        QVERIFY(save && undo); save->trigger(); QCOMPARE(Arc::loadProject(file).layers[0].text["content"].toString(),QString("Inline edit"));
        canvas->beginTextEditing(0); editor=nullptr; for(auto *item:canvas->findChildren<QPlainTextEdit *>()) if(item->isVisible()) editor=item;
        QVERIFY(editor); editor->setPlainText("Canceled"); QTest::keyClick(editor,Qt::Key_Escape); save->trigger();
        QCOMPARE(Arc::loadProject(file).layers[0].text["content"].toString(),QString("Inline edit"));
        undo->trigger(); save->trigger(); QCOMPARE(Arc::loadProject(file).layers,d.layers);
        canvas->beginTextEditing(0); editor=nullptr; for(auto *item:canvas->findChildren<QPlainTextEdit *>()) if(item->isVisible()) editor=item;
        QVERIFY(editor); editor->setPlainText("Saved while typing"); save->trigger();
        QCOMPARE(Arc::loadProject(file).layers[0].text["content"].toString(),QString("Saved while typing"));
    }
    void groupGeometryProjectionAndDistortion() {
        auto d=sample(); d.layers[0].rotation=30;
        auto source=d.layers[0].image; Arc::groupLayers(d,{d.layers[0].id}); auto folder=d.layers[d.active]; folder.size.setWidth(folder.size.width()*2);
        Arc::transformGroup(d,d.active,folder); Arc::validate(d); QCOMPARE(d.layers[0].image,source);
        QVERIFY(d.layers[0].size.width()>16); QVERIFY(d.layers[0].rotation<30);
        auto before=Arc::layerCorners(d.layers[0]),after=before; after[0]+=QPointF(3,2);
        auto id=d.layers[0].id; Arc::distortLayers(d,{id},before,after); Arc::validate(d);
        QCOMPARE(d.layers[0].rotation,0.0); QVERIFY(d.layers[0].shape.isEmpty()); QVERIFY(d.layers[0].image!=source);
        QTemporaryDir temp; auto file=temp.filePath("distorted.comp"); Arc::saveProject(d,file); QCOMPARE(Arc::render(Arc::loadProject(file)),Arc::render(d));
        auto invalid=before; invalid[0]=invalid[2]; QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::distortLayers(d,{id},before,invalid));
    }
    void transformHandleGesturesAndCancellation() {
        auto d=sample(); Canvas canvas; canvas.resize(640,480); canvas.show(); canvas.setDocument(d); canvas.fit(); canvas.setTool(Canvas::Tool::Move);
        QSignalSpy transformed(&canvas,&Canvas::layersTransformed); auto point=[&](QPointF p) { return canvas.canvasToWidget(p).toPoint(); };
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({18,15})); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({26,21}));
        QCOMPARE(transformed.count(),1); auto resized=qvariant_cast<Arc::Document>(transformed[0][0]);
        QVERIFY(std::abs(resized.layers[0].size.width()-24)<0.2); QCOMPARE(resized.layers[0].image,d.layers[0].image);
        canvas.setDocument(d); transformed.clear();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::ControlModifier,point({2,3})); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::ControlModifier,point({5,5}));
        QCOMPARE(transformed.count(),1); auto warped=qvariant_cast<Arc::Document>(transformed[0][0]); QVERIFY(warped.layers[0].image!=d.layers[0].image);
        canvas.setDocument(d); transformed.clear();
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({18,15})); QTest::mouseMove(&canvas,point({26,21})); QTest::keyClick(&canvas,Qt::Key_Escape);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({26,21})); QCOMPARE(transformed.count(),0);
        auto second=d.layers[0]; second.id=QUuid::createUuid(); second.origin={20,10}; d.layers.append(second); canvas.setDocument(d); canvas.setSelectedLayers({d.layers[0].id,second.id});
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({12,9})); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({17,9}));
        QCOMPARE(transformed.count(),1); auto moved=qvariant_cast<Arc::Document>(transformed[0][0]);
        QVERIFY(std::abs(moved.layers[0].origin.x()-7)<0.2); QVERIFY(std::abs(moved.layers[1].origin.x()-25)<0.2);
    }
    void expandedBlursAndHueMath() {
        auto d=sample(); auto source=d.layers[0]; auto gaussian=Arc::defaultFilter("Gaussian Blur"); gaussian.values["radius"]=3;
        auto blurred=Arc::expandedBlur(source,gaussian); QVERIFY(blurred.image.width()>source.image.width());
        QCOMPARE(blurred.origin.x()+blurred.size.width()/2,source.origin.x()+source.size.width()/2);
        QVERIFY(blurred.image.pixelColor((blurred.image.width()-source.image.width())/2-1,blurred.image.height()/2).alpha()>0);
        auto motion=Arc::defaultFilter("Motion Blur"); motion.values["distance"]=11; motion.values["angle"]=0;
        auto streak=Arc::expandedBlur(source,motion); QVERIFY(streak.image.width()>source.image.width());
        int pad=(streak.image.width()-source.image.width())/2;
        QVERIFY(streak.image.pixelColor(pad-2,streak.image.height()/2).alpha()>0); QCOMPARE(streak.image.pixelColor(streak.image.width()/2,pad-2).alpha(),0);
        motion.values["angle"]=90; streak=Arc::expandedBlur(source,motion); QVERIFY(streak.image.pixelColor(streak.image.width()/2,pad-2).alpha()>0);
        source.mask=QImage(1,1,QImage::Format_Grayscale8); source.mask.fill(Qt::white); source.rotation=30;
        blurred=Arc::expandedBlur(source,gaussian); QCOMPARE(Arc::maskTargetLayer(blurred).transform(),source.transform());
        auto hue=Arc::defaultFilter("Hue/Saturation"); hue.values["saturation"]=100;
        QImage neutral(1,1,QImage::Format_ARGB32_Premultiplied); neutral.fill(QColor(128,128,128)); QCOMPARE(Arc::applyFilter(neutral,hue),neutral);
        hue.values["lightness"]=50; auto light=Arc::applyFilter(d.layers[0].image,hue).pixelColor(0,0); QCOMPARE(light.red(),255); QVERIFY(std::abs(light.green()-128)<=1);
        gaussian.values["radius"]=0; QCOMPARE(Arc::expandedBlur(source,gaussian),source);
    }
    void curvesGraphAndDestructiveChannels() {
        CurveEditor graph; graph.resize(300,250); graph.show(); QSignalSpy changes(&graph,&CurveEditor::pointsChanged);
        QTest::mousePress(&graph,Qt::LeftButton,Qt::NoModifier,{150,125}); QTest::mouseRelease(&graph,Qt::LeftButton,Qt::NoModifier,{150,60});
        QCOMPARE(graph.points().size(),3); QVERIFY(graph.points()[1].y()>190); QVERIFY(changes.count()>0);
        QTest::mouseClick(&graph,Qt::RightButton,Qt::NoModifier,{150,60}); QCOMPARE(graph.points().size(),2);
        auto d=sample(); d.layers[0].image.fill(QColor(100,50,20)); QTemporaryDir temp; auto file=temp.filePath("channels.comp"); Arc::saveProject(d,file);
        Window window; window.show(); window.openProject(file); QAction *levels=nullptr,*save=nullptr;
        for(auto *a:window.findChildren<QAction *>()) { if(a->text()=="Levels…" && !levels) levels=a; if(a->text()=="&Save project") save=a; }
        QVERIFY(levels && save);
        QTimer::singleShot(50,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            auto *channel=dialog->findChild<QComboBox *>("adjustmentChannel"); QVERIFY(channel);
            channel->setCurrentIndex(1); dialog->findChild<QDoubleSpinBox *>("outputWhite")->setValue(0);
            channel->setCurrentIndex(3); dialog->findChild<QDoubleSpinBox *>("outputBlack")->setValue(255);
            dialog->accept();
        });
        levels->trigger(); save->trigger(); auto changed=Arc::loadProject(file); QCOMPARE(changed.layers.size(),1);
        QCOMPARE(changed.layers[0].image.pixelColor(0,0),QColor(0,50,255)); QVERIFY(changed.layers[0].adjustment.isEmpty());
        QCOMPARE(Arc::autoLevels(d.layers[0].image,1).first,100); QCOMPARE(Arc::autoLevels(d.layers[0].image,2).first,50);
    }
    void selectionCoverageFeatherAndTransformedEdits() {
        auto d=sample(); QImage coverage(d.size,QImage::Format_Grayscale8); coverage.fill(QColor(128,128,128));
        auto changed=d.layers[0].image; changed.fill(Qt::blue);
        auto mixed=Arc::limitToSelection(d.layers[0].image,changed,d.layers[0],coverage);
        QCOMPARE(mixed.pixelColor(5,5),QColor(127,0,128));
        auto translucent=d.layers[0].image; translucent.fill(QColor(0,255,0,64));
        QCOMPARE(Arc::alphaCoverage(translucent).constScanLine(0)[0],64);
        QPainterPath shape; shape.addRect(4,4,10,10); coverage=Arc::pathCoverage(shape,d.size);
        Canvas canvas; canvas.resize(640,480); canvas.setDocument(d); canvas.setSelectionCoverage(coverage); canvas.featherSelection(3);
        auto soft=canvas.selectedCoverage(); QVERIFY(soft.constScanLine(4)[4]>0 && soft.constScanLine(4)[4]<255); QVERIFY(soft.constScanLine(3)[5]>0);
        canvas.invertSelection(); QCOMPARE(canvas.selectedCoverage().constScanLine(4)[4],255-soft.constScanLine(4)[4]);
        auto layer=d.layers[0]; layer.origin={0,0}; layer.size={32,24};
        auto mapped=Arc::selectionInLayer(layer,layer.image.size(),coverage); QCOMPARE(mapped.size(),layer.image.size());
        QVERIFY(mapped.constScanLine(3)[3]>0); QCOMPARE(mapped.constScanLine(10)[12],0);
        auto pixels=Arc::selectedLayerPixels(d,coverage); QCOMPARE(pixels.pixelColor(5,5),QColor(Qt::red)); QCOMPARE(pixels.pixelColor(3,3).alpha(),0);
        auto original=d; Arc::floatSelectedPixels(d,coverage,{10,0},false); QCOMPARE(d.layers.size(),2);
        QCOMPARE(d.layers[0].image.pixelColor(3,2).alpha(),0); QCOMPARE(Arc::render(d).pixelColor(15,5),QColor(Qt::red));
        QCOMPARE(Arc::render(d).pixelColor(5,5).alpha(),0);
        d=original; Arc::floatSelectedPixels(d,coverage,{10,0},true); QCOMPARE(d.layers[0].image,original.layers[0].image);
    }
    void softSelectionPaintingAndCancel() {
        auto d=sample(); Canvas canvas; canvas.resize(640,480); canvas.show(); canvas.setDocument(d); canvas.fit();
        QImage coverage(d.size,QImage::Format_Grayscale8); coverage.fill(QColor(128,128,128)); canvas.setSelectionCoverage(coverage);
        Arc::Brush brush; brush.color=Qt::blue; brush.diameter=6; canvas.setBrush(brush); canvas.setTool(Canvas::Tool::Brush);
        QSignalSpy painted(&canvas,&Canvas::painted); auto point=[&](QPointF p) { return canvas.canvasToWidget(p).toPoint(); };
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({6.5,6.5})); QCOMPARE(painted.count(),1);
        auto image=qvariant_cast<QImage>(painted[0][1]); QCOMPARE(image.pixelColor(4,3),QColor(127,0,128));
        canvas.setDocument(d); canvas.setTool(Canvas::Tool::Rectangle);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({6,6})); QTest::mouseMove(&canvas,point({10,6}));
        QTest::keyClick(&canvas,Qt::Key_Escape); QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({10,6}));
        QCOMPARE(canvas.selectedCoverage(),coverage);
    }
    void selectedPixelDragUndoAndOutlineMove() {
        auto d=sample(); QTemporaryDir temp; auto file=temp.filePath("selection-ui.comp"); Arc::saveProject(d,file);
        Window window; window.show(); window.openProject(file); auto *canvas=window.findChild<Canvas *>(); canvas->fit();
        QPainterPath selection; selection.addRect(4,4,5,5); canvas->setSelection(selection); canvas->setTool(Canvas::Tool::Rectangle);
        auto point=[&](QPointF p) { return canvas->canvasToWidget(p).toPoint(); };
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point({6,6})); QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point({8,6}));
        QCOMPARE(canvas->selectionBounds()->left(),6.0);
        canvas->setSelection(selection); canvas->setTool(Canvas::Tool::Move);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point({6,6})); QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point({14,6}));
        QAction *save=nullptr,*undo=nullptr;
        for(auto *a:window.findChildren<QAction *>()) { if(a->text()=="&Save project") save=a; if(a->shortcut()==QKeySequence::Undo) undo=a; }
        QVERIFY(save && undo); save->trigger(); auto changed=Arc::loadProject(file); QCOMPARE(changed.layers.size(),2);
        QCOMPARE(Arc::render(changed).pixelColor(5,5).alpha(),0); QCOMPARE(Arc::render(changed).pixelColor(13,5),QColor(Qt::red));
        undo->trigger(); save->trigger(); QCOMPARE(Arc::loadProject(file).layers,d.layers);
    }
    void documentTabsTransferAndIndependentHistory() {
        QTemporaryDir temp; auto first=temp.filePath("first.comp"),second=temp.filePath("second.comp");
        auto d=sample(); Arc::Layer folder; folder.isGroup=true; folder.name="Group"; folder.size=d.size;
        d.layers[0].parentID=folder.id; d.layers.append(folder); d.active=1; Arc::saveProject(d,first);
        auto destination=sample(); destination.layers[0].image.fill(Qt::blue); Arc::saveProject(destination,second);
        Workspace workspace; workspace.show(); QVERIFY(QTest::qWaitForWindowActive(&workspace)); workspace.openProject(first); auto *source=workspace.activeEditor();
        workspace.openProject(second); auto *target=workspace.activeEditor(); QVERIFY(source!=target);
        auto *tabs=workspace.findChild<QTabWidget *>("documentTabs"); QCOMPARE(tabs->count(),3);
        workspace.openProject(first); QCOMPARE(tabs->count(),3); QCOMPARE(workspace.activeEditor(),source);
        workspace.copyLayersTo(2); tabs->setCurrentIndex(2); QTest::qWait(10);
        QAction *save=nullptr; for(auto *a:target->findChildren<QAction *>()) if(a->text()=="&Save project") save=a;
        QVERIFY(save); save->trigger(); auto copied=Arc::loadProject(second); QCOMPARE(copied.layers.size(),3);
        QVERIFY(copied.layers[1].id!=d.layers[0].id); QCOMPARE(copied.layers[1].parentID,copied.layers[2].id);
        QCOMPARE(Arc::loadProject(first).layers,d.layers);
        QTest::keyClick(target->findChild<Canvas *>(),Qt::Key_Z,Qt::ControlModifier); save->trigger();
        QCOMPARE(Arc::loadProject(second).layers,destination.layers);
        tabs->setCurrentIndex(1); auto transferred=source->selectedLayersForTransfer(); target->receiveLayers(transferred);
        bool prompted=false;
        QTimer::singleShot(50,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); prompted=true; box->button(QMessageBox::Cancel)->click(); });
        QVERIFY(!workspace.close()); QVERIFY(prompted); QCOMPARE(tabs->count(),3);
        // Close cancellation leaves every editor and its undo history alive.
        QVERIFY(workspace.activeEditor()==target); save->trigger();
        tabs->tabCloseRequested(2); QCOMPARE(tabs->count(),2);
    }
    void effectsRenderingMasksAndPersistence() {
        auto d=sample(); auto source=d.layers[0].image; auto stroke=Arc::defaultEffect("stroke");
        stroke["size"]=2; stroke["blue"]=1; d.layers[0].effects={{"stroke",stroke}};
        auto rendered=Arc::render(d); QCOMPARE(rendered.pixelColor(1,5),QColor(Qt::blue)); QCOMPARE(rendered.pixelColor(5,5),QColor(Qt::red));
        QCOMPARE(d.layers[0].image,source);
        auto bounds=Arc::visualBounds(d.layers[0]); QVERIFY(bounds.left()<d.layers[0].origin.x());
        d.layers[0].mask=QImage(1,1,QImage::Format_Grayscale8); d.layers[0].mask.fill(Qt::black);
        QCOMPARE(Arc::render(d).pixelColor(1,5).alpha(),0); d.layers[0].mask={};
        auto overlay=Arc::defaultEffect("colorOverlay"); overlay["green"]=1; d.layers[0].effects["colorOverlay"]=overlay;
        QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(Qt::green));
        auto shadow=Arc::defaultEffect("shadow"); shadow["distance"]=5; shadow["blur"]=0; shadow["opacity"]=1;
        d.layers[0].effects={{"shadow",shadow}}; QCOMPARE(Arc::render(d).pixelColor(5,18),QColor(Qt::black));
        auto inner=Arc::defaultEffect("innerShadow"); inner["distance"]=3; inner["blur"]=0; inner["opacity"]=1;
        d.layers[0].effects={{"innerShadow",inner}}; QCOMPARE(Arc::render(d).pixelColor(5,3),QColor(Qt::black)); QCOMPARE(Arc::render(d).pixelColor(5,8),QColor(Qt::red));
        d.layers[0].rotation=30; d.layers[0].flipX=true; d.layers[0].effects["stroke"]=stroke;
        auto baked=Arc::renderedEffects(d.layers[0]);
        auto center=d.layers[0].transform().map(QPointF(d.layers[0].size.width()/2,d.layers[0].size.height()/2));
        QCOMPARE(baked.transform().map(QPointF(baked.size.width()/2,baked.size.height()/2)),center);
        QTemporaryDir temp; auto file=temp.filePath("effects.comp"); Arc::saveProject(d,file);
        auto loaded=Arc::loadProject(file); QCOMPARE(loaded.layers,d.layers); QCOMPARE(Arc::render(loaded),Arc::render(d));
        auto invalid=stroke; invalid["size"]=-1; QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::validateEffects({{"stroke",invalid}}));
        Arc::Layer folder; folder.isGroup=true; folder.size=d.size; folder.mask=QImage(1,1,QImage::Format_Grayscale8); folder.mask.fill(Qt::black);
        d.layers[0].parentID=folder.id; d.layers.append(folder); QCOMPARE(Arc::render(d).pixelColor(5,5).alpha(),0);
    }
    void effectsMergingPreservesExtent() {
        auto d=sample(); d.size={64,64}; auto top=d.layers[0]; top.id=QUuid::createUuid(); top.origin={30,30}; top.image.fill(Qt::green);
        auto stroke=Arc::defaultEffect("stroke"); stroke["size"]=4; top.effects={{"stroke",stroke}};
        d.layers.append(top); d.active=1; auto before=Arc::render(d); Arc::mergeDown(d); QCOMPARE(Arc::render(d),before);
        auto groupDoc=sample(); groupDoc.size={64,64}; top.parentID={}; groupDoc.layers.append(top);
        Arc::groupLayers(groupDoc,{groupDoc.layers[0].id,top.id}); before=Arc::render(groupDoc); Arc::mergeFolder(groupDoc); QCOMPARE(Arc::render(groupDoc),before);
    }
    void effectsDialogCancelUndo() {
        auto d=sample(); QTemporaryDir temp; auto file=temp.filePath("effect-ui.comp"); Arc::saveProject(d,file);
        Window window; window.show(); window.openProject(file); QAction *effects=nullptr,*save=nullptr,*undo=nullptr;
        for(auto *a:window.findChildren<QAction *>()) {
            if(a->text()=="Layer effects…") effects=a;
            if(a->text()=="&Save project") save=a;
            if(a->shortcut()==QKeySequence::Undo) undo=a;
        }
        QVERIFY(effects && save && undo);
        QTimer::singleShot(50,[&] { auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->findChild<QCheckBox *>("strokeIncluded")->setChecked(true); dialog->findChild<QDoubleSpinBox *>("strokesize")->setValue(2); dialog->accept(); });
        effects->trigger(); save->trigger(); auto changed=Arc::loadProject(file); QVERIFY(!changed.layers[0].effects.isEmpty());
        QTimer::singleShot(50,[&] { auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->findChild<QCheckBox *>("strokeIncluded")->setChecked(false); dialog->reject(); });
        effects->trigger(); save->trigger(); QCOMPARE(Arc::loadProject(file).layers,changed.layers);
        undo->trigger(); save->trigger(); QCOMPARE(Arc::loadProject(file).layers,d.layers);
    }
    void adjustmentRenderingAndPersistence() {
        auto d=sample(); auto source=d.layers[0].image;
        Arc::Layer adjustment; adjustment.name="Hue"; adjustment.size=d.size;
        auto settings=Arc::defaultFilter("Hue/Saturation"); settings.values["hue"]=120;
        adjustment.adjustment=Arc::adjustmentFromFilter(settings,{}); d.layers.append(adjustment); d.active=1;
        QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(Qt::green));
        QCOMPARE(d.layers[0].image,source); QCOMPARE(Arc::render(d).pixelColor(0,0).alpha(),0);
        d.layers[1].opacity=0.5; auto c=Arc::render(d).pixelColor(5,5);
        QVERIFY(std::abs(c.red()-128)<=1 && std::abs(c.green()-127)<=1);
        d.layers[1].opacity=1; d.layers[1].mask=QImage(d.size,QImage::Format_Grayscale8); d.layers[1].mask.fill(Qt::white);
        d.layers[1].mask.setPixelColor(5,5,Qt::black); QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(Qt::red));
        QCOMPARE(Arc::render(d).pixelColor(6,5),QColor(Qt::green));
        QTemporaryDir temp; auto file=temp.filePath("adjustment.comp"); Arc::saveProject(d,file);
        auto loaded=Arc::loadProject(file); QCOMPARE(loaded.layers,d.layers); QCOMPARE(Arc::render(loaded),Arc::render(d));
        QCOMPARE(metadata(file)["version"].toInt(),7);
        auto invalid=d; invalid.layers[1].image=source; QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::validate(invalid));
        invalid=d; invalid.layers[0].maskSourceID=d.layers[1].id; QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::validate(invalid));
        for(auto kind:Arc::adjustmentKinds()) {
            auto a=Arc::defaultAdjustment(kind); Arc::validateAdjustment(a);
            auto result=Arc::adjustedImage(source,a); QCOMPARE(result.size(),source.size()); QCOMPARE(result.pixelColor(0,0).alpha(),255);
        }
    }
    void adjustmentChannelsFoldersAndClipping() {
        auto d=sample(); Arc::Layer a; a.size=d.size; auto f=Arc::defaultFilter("Hue/Saturation"); f.values["hue"]=120;
        a.adjustment=Arc::adjustmentFromFilter(f,{}); d.layers.append(a); d.active=1;
        Arc::createClippingMask(d); d.layers[0].opacity=0.5;
        QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(0,255,0,127));
        Arc::bakeClippingMask(d,1); QVERIFY(d.layers[1].maskSourceID.isNull());
        // Baking a clipping dependency produces an independent masked adjustment.
        QVERIFY(!d.layers[1].mask.isNull()); QVERIFY(Arc::render(d).pixelColor(5,5).green()>100);
        d.layers[0].opacity=1; d.layers[1].mask={}; d.layers[1].maskPlacement={};
        Arc::Layer group; group.isGroup=true; group.size=d.size; group.mask=QImage(d.size,QImage::Format_Grayscale8); group.mask.fill(Qt::white);
        group.mask.setPixelColor(5,5,Qt::black); d.layers[1].parentID=group.id; d.layers.append(group);
        QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(Qt::red)); QCOMPARE(Arc::render(d).pixelColor(6,5),QColor(Qt::green));
        auto levels=Arc::defaultAdjustment("Levels"); auto red=Arc::filterFromAdjustment(levels,1); red.values["outputWhite"]=0;
        levels=Arc::adjustmentFromFilter(red,levels); auto blue=Arc::filterFromAdjustment(levels,3); blue.values["outputBlack"]=255;
        levels=Arc::adjustmentFromFilter(blue,levels); QCOMPARE(Arc::adjustedImage(d.layers[0].image,levels).pixelColor(0,0),QColor(Qt::blue));
        auto translucent=d.layers[0].image; translucent.fill(QColor(255,0,0,128));
        QCOMPARE(Arc::adjustedImage(translucent,levels).pixelColor(0,0),QColor(0,0,255,128));
        auto curves=Arc::defaultAdjustment("Curves"); auto curve=Arc::filterFromAdjustment(curves,1); curve.curve={{0,0},{255,0}};
        curves=Arc::adjustmentFromFilter(curve,curves); QCOMPARE(Arc::adjustedImage(d.layers[0].image,curves).pixelColor(0,0),QColor(Qt::black));
    }
    void adjustmentDialogUndoAndCancel() {
        auto d=sample(); QTemporaryDir temp; auto file=temp.filePath("adjustment-ui.comp"); Arc::saveProject(d,file);
        Window window; window.show(); window.openProject(file);
        QAction *create=nullptr,*modify=nullptr,*save=nullptr,*undo=nullptr;
        for(auto *action:window.findChildren<QAction *>()) {
            if(action->text()=="Hue/Saturation…") create=action;
            if(action->text()=="Edit adjustment…") modify=action;
            if(action->text()=="&Save project") save=action;
            if(action->shortcut()==QKeySequence::Undo) undo=action;
        }
        QVERIFY(create && modify && save && undo);
        QTimer::singleShot(50,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            auto *hue=dialog->findChild<QDoubleSpinBox *>("hue"); QVERIFY(hue); hue->setValue(120); dialog->accept();
        });
        create->trigger(); save->trigger(); auto changed=Arc::loadProject(file); QCOMPARE(changed.layers.size(),2);
        QCOMPARE(Arc::render(changed).pixelColor(5,5),QColor(Qt::green));
        QTimer::singleShot(50,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->findChild<QDoubleSpinBox *>("hue")->setValue(240); dialog->reject();
        });
        modify->trigger(); save->trigger(); QCOMPARE(Arc::loadProject(file).layers,changed.layers);
        undo->trigger(); save->trigger(); QCOMPARE(Arc::loadProject(file).layers,d.layers);
    }
    void independentMaskGeometryAndPainting() {
        auto d=sample(); auto &l=d.layers[0];
        l.mask=QImage(l.image.size(),QImage::Format_Grayscale8); l.mask.fill(Qt::white);
        l.mask.setPixelColor(6,4,Qt::black); l.maskLinked=false; l.maskPlacement=Arc::placementOf(l);
        auto changed=l; changed.origin+={4,0}; Arc::transformGroup(d,0,changed);
        QCOMPARE(Arc::render(d).pixelColor(8,7).alpha(),0);
        QCOMPARE(Arc::render(d).pixelColor(12,7),QColor(Qt::red));
        QPainterPath path; path.moveTo(8.5,7.5); Arc::Brush brush; brush.color=Qt::white; brush.diameter=2;
        l.mask=Arc::paintMaskStroke(l,path,brush,QRectF(QPointF(),d.size));
        QCOMPARE(Arc::render(d).pixelColor(8,7),QColor(Qt::red));
        l.mask.setPixelColor(6,4,Qt::black);
        l.maskLinked=true; changed=l; changed.origin+={4,0}; Arc::transformGroup(d,0,changed);
        QCOMPARE(Arc::render(d).pixelColor(12,7).alpha(),0);
        auto before=Arc::render(d);
        Arc::flipCanvas(d,true);
        auto flipped=Arc::render(d),expectedFlip=before.flipped(Qt::Horizontal);
        QCOMPARE(flipped,expectedFlip);
        Arc::flipCanvas(d,true); Arc::crop(d,{4,0,28,24}); QCOMPARE(Arc::render(d),before.copy(4,0,28,24));
        QTemporaryDir temp; auto file=temp.filePath("mask.comp"); Arc::saveProject(d,file);
        auto loaded=Arc::loadProject(file); QCOMPARE(loaded.layers,d.layers); QCOMPARE(Arc::render(loaded),Arc::render(d));
    }
    void maskUnlinkControlsAndDrag() {
        auto d=sample(); auto &l=d.layers[0]; l.mask=QImage(l.image.size(),QImage::Format_Grayscale8); l.mask.fill(Qt::white);
        QTemporaryDir temp; auto file=temp.filePath("mask.comp"); Arc::saveProject(d,file);
        Window window; window.show(); window.openProject(file);
        auto *linked=window.findChild<QCheckBox *>("maskLinked"); QVERIFY(linked); QVERIFY(linked->isChecked());
        linked->setChecked(false); window.findChild<QComboBox *>("paintTarget")->setCurrentIndex(1);
        auto *canvas=window.findChild<Canvas *>(); canvas->fit(); canvas->setTool(Canvas::Tool::Move);
        QSignalSpy moved(canvas,&Canvas::maskMoved),layersMoved(canvas,&Canvas::moved);
        auto point=[&](QPointF p) { return canvas->canvasToWidget(p).toPoint(); };
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point({8,8}));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point({12,8}));
        QCOMPARE(moved.count(),1); QCOMPARE(layersMoved.count(),0);
        QAction *save=nullptr,*undo=nullptr;
        for(auto *a:window.findChildren<QAction *>()) {
            if(a->text()=="&Save project") save=a;
            if(a->shortcut()==QKeySequence::Undo) undo=a;
        }
        QVERIFY(save); QVERIFY(undo); save->trigger();
        auto loaded=Arc::loadProject(file); QCOMPARE(loaded.layers[0].origin,l.origin); QVERIFY(!loaded.layers[0].maskLinked);
        QVERIFY(std::abs(Arc::maskTargetLayer(loaded.layers[0]).origin.x()-6)<0.2);
        undo->trigger(); save->trigger(); QCOMPARE(Arc::maskTargetLayer(Arc::loadProject(file).layers[0]).origin,l.origin);
    }
    void clippingStacksAndDependencies() {
        auto d=sample(); d.layers[0].opacity=0.5;
        auto top=d.layers[0]; top.id=QUuid::createUuid(); top.opacity=1; top.image.fill(Qt::blue);
        d.layers.append(top); d.active=1; Arc::createClippingMask(d);
        QCOMPARE(d.layers[1].maskSourceID,d.layers[0].id);
        QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(0,0,255,127));
        d.layers[0].visible=false;
        QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(0,0,255,127));
        auto invalid=d; invalid.layers[0].maskSourceID=invalid.layers[1].id;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::validate(invalid));
        invalid=d; invalid.layers[1].maskSourceID=QUuid::createUuid();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::validate(invalid));
        d.layers[0].visible=true; d.layers[0].opacity=1;
        Arc::Layer folder; folder.isGroup=true; folder.name="Folder"; folder.size=d.size;
        folder.mask=QImage(1,1,QImage::Format_Grayscale8); folder.mask.fill(QColor(128,128,128));
        for(auto &l:d.layers) l.parentID=folder.id;
        d.layers.append(folder);
        QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(0,0,255,128));
        QTemporaryDir temp; auto file=temp.filePath("clip.comp"); Arc::saveProject(d,file);
        auto loaded=Arc::loadProject(file); QCOMPARE(loaded.layers,d.layers); QCOMPARE(Arc::render(loaded),Arc::render(d));
        auto before=Arc::render(d); d.active=2; Arc::mergeFolder(d); QCOMPARE(Arc::render(d),before);
    }
    void clippingDeletionAndBaking() {
        auto d=sample(); d.layers[0].opacity=0.5;
        auto top=d.layers[0]; top.id=QUuid::createUuid(); top.opacity=1; top.image.fill(Qt::blue);
        d.layers.append(top); d.active=1; Arc::createClippingMask(d);
        auto expected=Arc::render(d);
        d.active=0; Arc::deleteLayer(d);
        QCOMPARE(d.layers.size(),1); QVERIFY(d.layers[0].maskSourceID.isNull());
        QCOMPARE(Arc::render(d),expected);
        d=sample(); d.layers[0].origin={-10,-10}; d.layers[0].opacity=0.5;
        top=d.layers[0]; top.id=QUuid::createUuid(); top.opacity=1; top.image.fill(Qt::blue);
        d.layers.append(top); d.active=1; Arc::createClippingMask(d);
        Arc::bakeClippingMask(d,1); QCOMPARE(d.layers[1].image.pixelColor(0,0),QColor(0,0,255,127));
        d=sample(); top=d.layers[0]; top.id=QUuid::createUuid(); top.image.fill(Qt::green);
        d.layers.append(top); d.active=1; Arc::createClippingMask(d);
        expected=Arc::render(d); Arc::mergeDown(d); QCOMPARE(Arc::render(d),expected);
    }
    void folderRenderingAndRoundTrip() {
        auto d=sample();
        Arc::Layer folder; folder.isGroup=true; folder.name="Folder"; folder.size=d.size; folder.opacity=0.5;
        d.layers[0].parentID=folder.id; d.layers.append(folder); d.active=1;
        QCOMPARE(Arc::render(d).pixelColor(4,5),QColor(255,0,0,127));
        folder.visible=false; d.layers[1]=folder; QCOMPARE(Arc::render(d).pixelColor(4,5).alpha(),0);
        folder.visible=true; folder.opacity=1; folder.mask=QImage(1,1,QImage::Format_Grayscale8); folder.mask.fill(Qt::black);
        d.layers[1]=folder; QCOMPARE(Arc::render(d).pixelColor(4,5).alpha(),0);
        folder.mask.fill(Qt::white); d.layers[1]=folder; QCOMPARE(Arc::render(d).pixelColor(4,5),QColor(Qt::red));
        QPainterPath path; path.moveTo(4,5); Arc::Brush brush; brush.diameter=4;
        d.layers[1].mask=Arc::paintMaskStroke(folder,path,brush,QRectF(QPointF(),d.size));
        QCOMPARE(d.layers[1].mask.size(),d.size); QCOMPARE(Arc::render(d).pixelColor(4,5).alpha(),0);
        QTemporaryDir temp; auto file=temp.filePath("folder.comp"); Arc::saveProject(d,file);
        auto loaded=Arc::loadProject(file); QCOMPARE(loaded.layers,d.layers); QCOMPARE(Arc::render(loaded),Arc::render(d));
        loaded.layers[1].parentID=folder.id;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::validate(loaded));
        loaded=d; loaded.layers[0].parentID=QUuid::createUuid();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::validate(loaded));
    }
    void folderOperations() {
        auto d=sample(); auto second=d.layers[0]; second.id=QUuid::createUuid(); second.origin={10,10};
        d.layers.append(second); d.active=1; auto before=Arc::render(d);
        Arc::groupLayers(d,{d.layers[0].id,d.layers[1].id});
        QCOMPARE(d.layers.size(),3); QVERIFY(d.layers[d.active].isGroup); QCOMPARE(Arc::render(d),before);
        int index=d.active; auto moved=d.layers[index]; moved.origin+={4,5};
        Arc::transformGroup(d,index,moved); QCOMPARE(d.layers[0].origin,QPointF(6,8)); QCOMPARE(d.layers[1].origin,QPointF(14,15));
        Arc::duplicateLayer(d); QCOMPARE(d.layers.size(),6); QCOMPARE(Arc::descendants(d,d.layers[d.active].id).size(),2);
        Arc::deleteLayer(d); QCOMPARE(d.layers.size(),3);
        for(int i=0;i<d.layers.size();++i) if(d.layers[i].isGroup) d.active=i;
        before=Arc::render(d); Arc::mergeFolder(d); QCOMPARE(d.layers.size(),1); QCOMPARE(Arc::render(d),before);
    }
    void folderTreeAndUndo() {
        QTemporaryDir temp; auto file=temp.filePath("folder.comp"); Arc::saveProject(sample(),file);
        Window window; window.show(); window.openProject(file);
        QAction *group=nullptr,*undo=nullptr,*save=nullptr;
        for(auto *a:window.findChildren<QAction *>()) {
            if(a->text()=="Group selected layers") group=a;
            if(a->shortcut()==QKeySequence::Undo) undo=a;
            if(a->text()=="&Save project") save=a;
        }
        QVERIFY(group); QVERIFY(undo); QVERIFY(save);
        auto *tree=window.findChild<QTreeWidget *>("layers"); group->trigger();
        QCOMPARE(tree->topLevelItemCount(),1); QCOMPARE(tree->topLevelItem(0)->childCount(),1);
        save->trigger(); QVERIFY(Arc::loadProject(file).layers.last().isGroup);
        undo->trigger(); QCOMPARE(tree->topLevelItem(0)->childCount(),0); save->trigger();
    }
    void nonseparableBlendReferenceColors() {
        QImage backdrop(1,1,QImage::Format_ARGB32_Premultiplied),source(1,1,QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(32,128,192));
        const QStringList modes{"Hue","Saturation","Color","Luminosity"};
        const QList<QColor> expected{QColor(35,93,131),QColor(160,54,0),QColor(5,101,165),QColor(155,91,59)};
        for(int i=0;i<modes.size();++i) {
            backdrop.fill(QColor(128,64,32));
            Arc::blendNonseparable(backdrop,source,modes[i]); QCOMPARE(backdrop.pixelColor(0,0),expected[i]);
            backdrop.fill(Qt::transparent);
            Arc::blendNonseparable(backdrop,source,modes[i]); QCOMPARE(backdrop,source);
            QImage clear=source; clear.fill(Qt::transparent);
            Arc::blendNonseparable(backdrop,clear,modes[i]); QCOMPARE(backdrop,source);
        }
        backdrop.fill(QColor(128,128,128));
        Arc::blendNonseparable(backdrop,source,"Saturation"); QCOMPARE(backdrop.pixelColor(0,0),QColor(128,128,128));
        // Source-over alpha: red at alpha 128 over red at alpha 128.
        source.fill(QColor(255,0,0,128)); backdrop=source;
        Arc::blendNonseparable(backdrop,source,"Color");
        QCOMPARE(backdrop.pixelColor(0,0),QColor(255,0,0,192));
        // Extreme luminosities exercise gamut clipping without division by zero.
        for(const auto &mode:modes) for(const auto &color:{QColor(Qt::white),QColor(Qt::black)}) {
            backdrop.fill(color); source.fill(Qt::red); Arc::blendNonseparable(backdrop,source,mode);
            auto p=backdrop.pixel(0,0); QVERIFY(qRed(p)<=qAlpha(p)); QVERIFY(qGreen(p)<=qAlpha(p)); QVERIFY(qBlue(p)<=qAlpha(p));
        }
    }
    void nonseparableBlendRenderingAndPersistence() {
        auto d=sample(); d.layers[0].image.fill(QColor(128,64,32));
        auto top=d.layers[0]; top.id=QUuid::createUuid(); top.image.fill(QColor(32,128,192)); top.blend="Color";
        d.layers.append(top); d.active=1;
        auto full=Arc::render(d); QCOMPARE(full.pixelColor(5,5),QColor(5,101,165));
        top.opacity=0.5; d.layers[1]=top;
        auto half=Arc::render(d).pixelColor(5,5);
        QVERIFY(std::abs(half.red()-67)<=1); QVERIFY(std::abs(half.green()-83)<=1); QVERIFY(std::abs(half.blue()-99)<=1);
        d.layers[1].mask=QImage(1,1,QImage::Format_Grayscale8); d.layers[1].mask.fill(Qt::black);
        QCOMPARE(Arc::render(d).pixelColor(5,5),QColor(128,64,32));
        d.layers[1].mask.fill(Qt::white); d.layers[1].rotation=17;
        auto cache=Arc::render(d); d.layers[1].image.fill(Qt::green);
        Arc::repaintRegion(cache,d,{3,3,6,6}); QCOMPARE(cache,Arc::render(d));
        QTemporaryDir temp; auto path=temp.filePath("blend.comp"); Arc::saveProject(d,path);
        auto loaded=Arc::loadProject(path); QCOMPARE(loaded.layers[1].blend,QString("Color")); QCOMPARE(Arc::render(loaded),cache);
        Arc::exportImage(d,temp.filePath("blend.png")); QCOMPARE(QImage(temp.filePath("blend.png")).convertToFormat(cache.format()),cache);
    }
    void guidesPersistTransformAndStayOutOfExports() {
        auto d=sample(); d.size={1000,1000};
        Arc::Guide vertical; vertical.position=60;
        Arc::Guide horizontal; horizontal.axis="horizontal"; horizontal.position=70;
        auto withoutGuides=Arc::render(d);
        d.guides={vertical,horizontal};
        QCOMPARE(Arc::render(d),withoutGuides);
        QTemporaryDir temp; auto path=temp.filePath("guides.comp"); Arc::saveProject(d,path);
        QCOMPARE(metadata(path)["version"].toInt(),8);
        QCOMPARE(Arc::loadProject(path).guides,d.guides);
        auto changed=d; changed.guides[1].id=vertical.id;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::saveProject(changed,path));
        QCOMPARE(Arc::loadProject(path).guides,d.guides);
        auto manifest=metadata(path); manifest["version"]=7; writeMetadata(path,manifest);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::loadProject(path));
        Arc::crop(d,{10,20,900,900}); QCOMPARE(d.guides[0].position,50.0); QCOMPARE(d.guides[1].position,50.0);
        Arc::resizeCanvas(d,{1100,1100},true); QCOMPARE(d.guides[0].position,150.0);
        Arc::resizeImage(d,{550,550}); QCOMPARE(d.guides[0].position,75.0);
        Arc::flipCanvas(d,true); QCOMPARE(d.guides[0].position,475.0); QCOMPARE(d.guides[1].position,75.0);
        Arc::flipCanvas(d,false); QCOMPARE(d.guides[1].position,475.0);
    }
    void snappingAndGuideGestures() {
        auto d=sample(); d.size={1000,1000};
        Arc::Guide g; g.position=80; d.guides={g};
        QCOMPARE(Arc::snapLayerOrigin(d,0,{60,300},5),QPointF(64,300));
        QCOMPARE(Arc::snapLayerOrigin(d,0,{60,300},5,false),QPointF(60,300));
        QCOMPARE(Arc::snapLayerOrigin(d,0,{40,300},5),QPointF(40,300));
        d.size={100,100}; d.guides[0].position=25;
        Canvas canvas; canvas.resize(640,480); canvas.setDocument(d); canvas.show(); canvas.fit();
        auto point=[&](QPointF p) { return canvas.canvasToWidget(p).toPoint(); };
        QSignalSpy moved(&canvas,&Canvas::guideMoved);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({25,50}));
        QTest::mouseMove(&canvas,point({40,50})); QTest::keyClick(&canvas,Qt::Key_Escape);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({40,50})); QCOMPARE(moved.count(),0);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({25,50}));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({40,50}));
        QCOMPARE(moved.count(),1); QVERIFY(std::abs(moved[0][1].toDouble()-40)<0.2);
        canvas.setGuidesLocked(true);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({40,50}));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({60,50})); QCOMPARE(moved.count(),1);
    }
    void guidesDialogCancelApplyAndUndo() {
        QTemporaryDir temp; auto path=temp.filePath("guides.comp"); Arc::saveProject(sample(),path);
        Window window; window.show(); window.openProject(path);
        QAction *manage=nullptr,*save=nullptr,*undo=nullptr;
        for(auto *action:window.findChildren<QAction *>()) {
            if(action->text()=="Manage guides…") manage=action;
            if(action->text()=="&Save project") save=action;
            if(action->shortcut()==QKeySequence::Undo) undo=action;
        }
        QVERIFY(manage); QVERIFY(save); QVERIFY(undo);
        auto addGuide=[](bool accept) {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->findChild<QDoubleSpinBox *>("guidePosition")->setValue(18);
            dialog->findChild<QPushButton *>("addGuide")->click();
            if(accept) dialog->accept(); else dialog->reject();
        };
        QTimer::singleShot(0,[&] { addGuide(false); }); manage->trigger(); save->trigger();
        QVERIFY(Arc::loadProject(path).guides.isEmpty());
        QTimer::singleShot(0,[&] { addGuide(true); }); manage->trigger(); save->trigger();
        QCOMPARE(Arc::loadProject(path).guides.size(),1); QCOMPARE(Arc::loadProject(path).guides[0].position,18.0);
        undo->trigger(); save->trigger(); QVERIFY(Arc::loadProject(path).guides.isEmpty());
    }
    void editableStylesAndPersistence() {
        auto d=sample(); auto &l=d.layers[0];
        l.shape=Arc::shapeStyle("Ellipse",Qt::green); l.image=Arc::shapeImage(l.shape,l.size);
        QCOMPARE(l.image.pixelColor(8,6),QColor(Qt::green)); QCOMPARE(l.image.pixelColor(0,0).alpha(),0);
        l.mask=QImage(1,1,QImage::Format_Grayscale8); l.mask.fill(Qt::white);
        QTemporaryDir temp; auto path=temp.filePath("editable.comp"); Arc::saveProject(d,path);
        QCOMPARE(metadata(path)["version"].toInt(),8); auto loaded=Arc::loadProject(path);
        QCOMPARE(loaded.layers[0].shape,l.shape); QCOMPARE(loaded.layers[0].image,l.image);
        l.shape={}; l.text=Arc::textStyle(Qt::red); l.text["content"]="Hello Linux";
        l.text["fontSize"]=24; l.image=Arc::textImage(l.text); l.size=l.image.size();
        QVERIFY(l.image.width()>50); QVERIFY(l.image.height()>24);
        for(const auto &alignment:{"Center","Right"}) {
            l.text["alignment"]=alignment; auto aligned=Arc::textImage(l.text);
            bool visible=false;
            for(int y=0;y<aligned.height();++y) for(int x=0;x<aligned.width();++x) visible|=qAlpha(aligned.pixel(x,y))>0;
            QVERIFY(visible);
        }
        l.text["alignment"]="Left";
        l.text["boxSize"]=QJsonArray{100,120}; l.text["content"]="Hello Linux long paragraph";
        l.image=Arc::textImage(l.text); l.size=l.image.size(); QCOMPARE(l.image.size(),QSize(100,120));
        Arc::saveProject(d,path); loaded=Arc::loadProject(path);
        QCOMPARE(loaded.layers[0].text,l.text); QCOMPARE(loaded.layers[0].image,l.image);
        l.text["fontSize"]=-10;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::saveProject(d,path));
        QCOMPARE(Arc::loadProject(path).layers[0].text,loaded.layers[0].text);
        l.text=loaded.layers[0].text; l.text["unsupported"]=true;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::textImage(l.text));
        l.text=loaded.layers[0].text; l.text["boxSize"]=QJsonArray{30000,30000};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::textImage(l.text));
    }
    void shapeGestureAndRasterizationUndo() {
        QTemporaryDir temp; auto path=temp.filePath("shape.comp");
        Arc::Document d; d.size={200,200}; Arc::saveProject(d,path);
        Window window; window.show(); window.openProject(path); QTest::qWait(10);
        auto *canvas=window.findChild<Canvas *>(); canvas->fit();
        auto point=[&](QPointF p) { return canvas->canvasToWidget(p).toPoint(); };
        canvas->setTool(Canvas::Tool::ShapeEllipse);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point({30,30}));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::ShiftModifier,point({80,60}));
        auto *list=window.findChild<QTreeWidget *>("layers"); QCOMPARE(list->topLevelItemCount(),1);
        QAction *save=nullptr,*undo=nullptr;
        for(auto *action:window.findChildren<QAction *>()) {
            if(action->text()=="&Save project") save=action;
            if(action->shortcut()==QKeySequence::Undo) undo=action;
        }
        QVERIFY(save); QVERIFY(undo); save->trigger();
        auto saved=Arc::loadProject(path); QVERIFY(!saved.layers[0].shape.isEmpty());
        QCOMPARE(saved.layers[0].size.width(),saved.layers[0].size.height());
        Arc::Brush brush; brush.color=Qt::red; canvas->setBrush(brush); canvas->setTool(Canvas::Tool::Brush);
        QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,point({50,50}));
        save->trigger(); QVERIFY(Arc::loadProject(path).layers[0].shape.isEmpty());
        undo->trigger(); save->trigger(); QVERIFY(!Arc::loadProject(path).layers[0].shape.isEmpty());
    }
    void textDialogCancelAndApply() {
        QTemporaryDir temp; auto path=temp.filePath("text.comp");
        Arc::Document d; d.size={400,300}; Arc::saveProject(d,path);
        Window window; window.show(); window.openProject(path); QTest::qWait(10);
        auto *canvas=window.findChild<Canvas *>(); canvas->fit(); canvas->setTool(Canvas::Tool::Text);
        auto point=canvas->canvasToWidget({20,20}).toPoint();
        QTimer::singleShot(0,[] { auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->reject(); });
        QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(window.findChild<QTreeWidget *>("layers")->topLevelItemCount(),0);
        QTimer::singleShot(0,[] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            auto *content=dialog->findChild<QPlainTextEdit *>("textContent"); QVERIFY(content);
            content->setPlainText("Editable text");
            QTimer::singleShot(180,dialog,&QDialog::accept);
        });
        QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(window.findChild<QTreeWidget *>("layers")->topLevelItemCount(),1);
        for(auto *action:window.findChildren<QAction *>()) if(action->text()=="&Save project") action->trigger();
        QCOMPARE(Arc::loadProject(path).layers[0].text["content"].toString(),QString("Editable text"));
    }
    void retouchingPixels() {
        Arc::Layer layer; layer.size={64,64};
        layer.image=QImage(64,64,QImage::Format_ARGB32_Premultiplied); layer.image.fill(Qt::white);
        { QPainter painter(&layer.image); painter.fillRect(0,0,16,64,Qt::red); }
        auto original=layer.image;
        Arc::Brush brush; brush.color=Qt::black; brush.diameter=10;
        QPainterPath path; path.moveTo(40,32);
        QRectF bounds(0,0,64,64);
        auto cloned=Arc::cloneStroke(layer,layer.image,{-32,0},path,brush,bounds,{});
        QCOMPARE(cloned.pixelColor(40,32),QColor(Qt::red));
        QCOMPARE(cloned.pixelColor(50,32),QColor(Qt::white));
        QCOMPARE(layer.image,original);
        QPainterPath selection; selection.addRect(32,0,32,64);
        auto gradient=Arc::gradientStroke(layer,{32,0},{64,0},brush,Qt::white,bounds,selection,false);
        QCOMPARE(gradient.pixelColor(8,32),QColor(Qt::red));
        QVERIFY(gradient.pixelColor(33,32).red()<20);
        QVERIFY(gradient.pixelColor(62,32).red()>230);
        layer.origin={10,20}; layer.size={128,128};
        gradient=Arc::gradientStroke(layer,{10,20},{138,20},brush,Qt::white,QRectF(0,0,200,200),{},false);
        QVERIFY(gradient.pixelColor(0,32).red()<10);
        QVERIFY(gradient.pixelColor(63,32).red()>245);
        layer.origin={0,0}; layer.size={64,64};
        layer.image.fill(Qt::white); layer.image.setPixelColor(32,32,Qt::black);
        path=QPainterPath(); path.moveTo(32,32);
        auto healed=Arc::healStroke(layer,path,brush,bounds,{});
        QVERIFY(healed.pixelColor(32,32).red()>200);
        QCOMPARE(healed.pixelColor(0,0),QColor(Qt::white));
        auto blurred=Arc::blurStroke(layer,path,brush,bounds,{},false);
        QVERIFY(blurred.pixelColor(32,32).red()>0);
        QCOMPARE(blurred.pixelColor(0,0),QColor(Qt::white));
        QCOMPARE(layer.image.pixelColor(32,32),QColor(Qt::black));
    }
    void retouchingGestures() {
        auto d=sample(); Canvas canvas; canvas.resize(640,480); canvas.setDocument(d); canvas.show(); canvas.fit();
        auto point=[&](QPointF p) { return canvas.canvasToWidget(p).toPoint(); };
        QSignalSpy colors(&canvas,&Canvas::colorPicked), painted(&canvas,&Canvas::painted), errors(&canvas,&Canvas::errorOccurred);
        canvas.setTool(Canvas::Tool::Eyedropper);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({8,8}));
        QCOMPARE(colors.count(),1); QCOMPARE(colors[0][0].value<QColor>(),QColor(Qt::red));
        canvas.setTool(Canvas::Tool::Clone);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({8,8})); QCOMPARE(errors.count(),1);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::AltModifier,point({8,8}));
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({10,8})); QCOMPARE(errors.count(),1);
        canvas.setTool(Canvas::Tool::Gradient);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({2,6}));
        QTest::mouseMove(&canvas,point({18,6}));
        QTest::keyClick(&canvas,Qt::Key_Escape);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({18,6}));
        QCOMPARE(painted.count(),0);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({2,6}));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({18,6}));
        QCOMPARE(painted.count(),1);
        auto image=painted[0][1].value<QImage>();
        canvas.setDocument(d); canvas.setTool(Canvas::Tool::Brush);
        Arc::Brush brush; brush.diameter=2; canvas.setBrush(brush);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({4,8}));
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::ShiftModifier,point({14,8}));
        QCOMPARE(painted.count(),3);
        QCOMPARE(painted.last()[1].value<QImage>().pixelColor(7,5),QColor(Qt::black));
        QVERIFY(image.pixelColor(0,3).red()<20); QVERIFY(image.pixelColor(15,3).red()>230);
    }
    void compositeAndExport() {
        auto d = sample();
        auto image = Arc::render(d);
        QCOMPARE(image.pixelColor(2,3), QColor(Qt::red)); QCOMPARE(image.pixelColor(0,0).alpha(), 0);
        Arc::Layer top = d.layers[0]; top.id = QUuid::createUuid(); top.image.fill(Qt::blue); top.opacity = 0.5;
        d.layers.append(top);
        auto mixed = Arc::render(d).pixelColor(3,4);
        QVERIFY(std::abs(mixed.red()-128) <= 1); QVERIFY(std::abs(mixed.blue()-128) <= 1);
        d.layers[1].visible = false; QCOMPARE(Arc::render(d).pixelColor(3,4), QColor(Qt::red));
        QTemporaryDir temp;
        Arc::exportImage(d, temp.filePath("image.png"));
        QCOMPARE(QImage(temp.filePath("image.png")).pixelColor(0,0).alpha(), 0);
        Arc::exportImage(d, temp.filePath("image.jpg"));
        auto jpeg = QImage(temp.filePath("image.jpg")); QVERIFY(!jpeg.isNull());
        QVERIFY(jpeg.pixelColor(31,23).red() > 245);
        QFile existing(temp.filePath("keep.txt")); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("keep"); existing.close();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::exportImage(d, existing.fileName()));
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), QByteArray("keep"));
    }
    void transformsAndBlend() {
        auto d = sample(); auto &l = d.layers[0];
        l.size = {8,6}; l.origin = {10,10}; l.rotation = 90;
        QCOMPARE(l.transform().map(QPointF(0,0)), QPointF(17,9));
        QCOMPARE(l.transform().map(QPointF(4,3)), QPointF(14,13));
        l.flipX = true; QCOMPARE(l.transform().map(QPointF(0,0)), QPointF(17,17));
        auto rendered = Arc::render(d); QCOMPARE(rendered.pixelColor(14,13), QColor(Qt::red));
        auto top = l; top.id = QUuid::createUuid(); top.image.fill(Qt::blue); top.blend = "Multiply"; d.layers.append(top);
        QCOMPARE(Arc::render(d).pixelColor(14,13), QColor(Qt::black));
    }
    void roundTripAndReplace() {
        QTemporaryDir temp; auto path = temp.filePath("art.comp"); auto d = sample();
        d.layers[0].rotation = 33; d.layers[0].flipY = true; d.layers[0].opacity = 0.75;
        Arc::saveProject(d, path);
        auto loaded = Arc::loadProject(path);
        QCOMPARE(loaded.id, d.id); QCOMPARE(loaded.active, d.active); QCOMPARE(loaded.size, d.size);
        QCOMPARE(loaded.layers[0].image, d.layers[0].image);
        QCOMPARE(Arc::render(loaded), Arc::render(d));
        d.layers[0].image.fill(Qt::green); d.layers[0].name = "Changed";
        Arc::saveProject(d, path); loaded = Arc::loadProject(path);
        QCOMPARE(loaded.layers[0].name, QString("Changed")); QCOMPARE(loaded.layers[0].image.pixelColor(0,0), QColor(Qt::green));
        auto invalid = d; invalid.size = {30000,30000};
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::saveProject(invalid, path));
        QCOMPARE(Arc::loadProject(path).layers[0].name, QString("Changed"));
        QCOMPARE(QDir(temp.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), QStringList{"art.comp"});
    }
    void rejectUnsupportedOrUnsafe() {
        QTemporaryDir temp; auto path = temp.filePath("art.comp"); Arc::saveProject(sample(), path);
        const auto original = metadata(path);
        auto m = original; m["version"] = 9; writeMetadata(path, m);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::saveProject(sample(), path));
        QCOMPARE(metadata(path)["version"].toInt(), 9);
        m = original; auto records = m["layers"].toArray(); auto r = records[0].toObject();
        r["maskFile"] = "hidden.png"; records[0] = r; m["layers"] = records; writeMetadata(path,m);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        r.remove("maskFile"); r["imageFile"] = "../../outside.png"; records[0] = r; m["layers"] = records; writeMetadata(path,m);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        m = original; m["width"] = -1; writeMetadata(path,m);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        m = original; m["activeLayerID"] = QUuid::createUuid().toString(); writeMetadata(path,m);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        writeMetadata(path, original);
        QString asset = path + "/images/" + original["layers"].toArray()[0].toObject()["imageFile"].toString();
        QVERIFY(QFile::remove(asset));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        QVERIFY(QFile::link(temp.filePath("outside.png"), asset));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
    }
    void legacyFixture() {
        // Shape of Swift Codable CGPoint/CGSize records: two-number arrays.
        QTemporaryDir temp; QString path = temp.filePath("legacy.comp"); QVERIFY(QDir().mkpath(path));
        QByteArray json = R"({"format":"com.compositor.project","version":1,"colorSpace":"sRGB","documentID":"8BC1E57D-577A-46ED-A87E-D38934E3DE21","width":20,"height":30,"layers":[{"id":"8BC1E57D-577A-46ED-A87E-D38934E3DE22","name":"Blank","isVisible":true,"transform":{"origin":[0,0],"size":[20,30],"rotation":0,"flipX":false,"flipY":false,"sampling":"High quality"}}]})";
        writeMetadata(path, QJsonDocument::fromJson(json).object());
        auto d = Arc::loadProject(path); QCOMPARE(d.size, QSize(20,30)); QCOMPARE(d.layers.size(), 1); QVERIFY(d.layers[0].image.isNull());
    }
    void preserveUnrecognizedFilesOnSave() {
        QTemporaryDir temp; QString path = temp.filePath("art.comp");
        auto d = sample(); Arc::saveProject(d, path);
        QFile extra(path + "/notes.txt"); QVERIFY(extra.open(QIODevice::WriteOnly)); extra.write("important"); extra.close();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::saveProject(d, path));
        QVERIFY(extra.open(QIODevice::ReadOnly)); QCOMPARE(extra.readAll(), QByteArray("important")); extra.close();
        QVERIFY(extra.remove());
        extra.setFileName(path + "/images/unused.png"); QVERIFY(extra.open(QIODevice::WriteOnly)); extra.write("keep"); extra.close();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::saveProject(d, path));
        QCOMPARE(Arc::loadProject(path).layers[0].image, d.layers[0].image);
    }
    void rejectDuplicateLayersAndOversize() {
        QTemporaryDir temp; QString path = temp.filePath("art.comp"); Arc::saveProject(sample(), path);
        auto m = metadata(path); auto layers = m["layers"].toArray(); layers.append(layers[0]); m["layers"] = layers;
        writeMetadata(path, m); QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        auto d = sample(); d.layers[0].size.setWidth(0); QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::validate(d));
        d = sample(); d.size = {30000,30000}; QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::render(d));
    }
    void canvasGestures() {
        Canvas c; c.resize(640,480); auto d = sample(); c.setDocument(d); c.show(); c.fit();
        QVERIFY(QTest::qWaitForWindowExposed(&c));
        QSignalSpy moves(&c, &Canvas::moved), selections(&c, &Canvas::selected);
        QPoint start = c.canvasToWidget({8,8}).toPoint();
        QTest::mousePress(&c, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&c, start + QPoint(40,20));
        QTest::mouseRelease(&c, Qt::LeftButton, Qt::NoModifier, start + QPoint(40,20));
        QCOMPARE(selections.size(), 1); QCOMPARE(moves.size(), 1);
        QPointF moved = moves[0][1].toPointF(); QVERIFY(moved.x() > d.layers[0].origin.x());
        c.setDocument(d);
        QTest::mousePress(&c, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&c, start + QPoint(40,20)); QTest::keyClick(&c, Qt::Key_Escape);
        QTest::mouseRelease(&c, Qt::LeftButton, Qt::NoModifier, start + QPoint(40,20));
        QCOMPARE(moves.size(), 1);
        QPointF before = c.canvasToWidget({0,0});
        QTest::keyPress(&c, Qt::Key_Space); QTest::mousePress(&c, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&c, start + QPoint(20,20)); QTest::mouseRelease(&c, Qt::LeftButton, Qt::NoModifier, start + QPoint(20,20)); QTest::keyRelease(&c, Qt::Key_Space);
        QCOMPARE(c.canvasToWidget({0,0}), before + QPointF(20,20));
        QCOMPARE(moves.size(), 1);
    }
    void brushOpacityAndErase() {
        auto d = sample(); auto &layer = d.layers[0];
        layer.image.fill(Qt::transparent); layer.origin = {}; layer.size = layer.image.size();
        Arc::Brush brush; brush.color = Qt::red; brush.diameter = 4; brush.opacity = 0.5;
        QPainterPath path; path.moveTo(3,6); path.lineTo(12,6); path.lineTo(3,6);
        QRectF clip(0,0,16,12);
        auto painted = Arc::paintStroke(layer, path, brush, clip);
        QVERIFY(std::abs(painted.pixelColor(6,6).alpha()-128) <= 1);
        QCOMPARE(painted.pixelColor(6,6).red(), 255);
        QCOMPARE(painted.pixelColor(6,0).alpha(), 0);
        QCOMPARE(layer.image.pixelColor(6,6).alpha(), 0); // Original history snapshot is untouched.
        layer.image = painted; brush.eraser = true;
        auto erased = Arc::paintStroke(layer, path, brush, clip);
        QVERIFY(std::abs(erased.pixelColor(6,6).alpha()-64) <= 1);
        brush.opacity = 1;
        QCOMPARE(Arc::paintStroke(layer,path,brush,clip).pixelColor(6,6).alpha(), 0);
        QPainterPath dot; dot.moveTo(6,6); brush.eraser = false;
        QCOMPARE(Arc::paintStroke(layer,dot,brush,clip).pixelColor(6,6).alpha(), 255);
    }
    void transformedPaintingAndSelection() {
        auto d = sample(); auto &layer = d.layers[0];
        layer.image.fill(Qt::transparent); layer.origin = {20,20}; layer.size = {32,24};
        layer.rotation = 90; layer.flipX = true;
        QTransform toDocument = layer.transform(); toDocument.scale(2,2);
        QPointF center = toDocument.map(QPointF(8.5,6.5));
        QPainterPath dot; dot.moveTo(center);
        Arc::Brush brush; brush.color = Qt::green; brush.diameter = 8;
        auto result = Arc::paintStroke(layer,dot,brush,QRectF(0,0,100,100));
        QCOMPARE(result.pixelColor(8,6), QColor(Qt::green));
        QCOMPARE(result.pixelColor(0,0).alpha(), 0);
        layer.origin = {}; layer.size = layer.image.size(); layer.rotation = 0; layer.flipX = false;
        QPainterPath line; line.moveTo(0,6); line.lineTo(16,6); brush.diameter = 10;
        auto selected = Arc::paintStroke(layer,line,brush,QRectF(5,4,4,4));
        QCOMPARE(selected.pixelColor(6,5), QColor(Qt::green));
        QCOMPARE(selected.pixelColor(4,5).alpha(), 0); QCOMPARE(selected.pixelColor(9,5).alpha(), 0);
        QCOMPARE(selected.pixelColor(6,3).alpha(), 0); QCOMPARE(selected.pixelColor(6,8).alpha(), 0);
        QCOMPARE(Arc::paintStroke(layer,line,brush,QRectF()), layer.image);
    }
    void paintGesturesAndSelectionCancel() {
        Canvas canvas; canvas.resize(640,480); auto d = sample(); canvas.setDocument(d);
        canvas.show(); canvas.fit(); QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QSignalSpy painted(&canvas, &Canvas::painted);
        auto point = [&](QPointF p) { return canvas.canvasToWidget(p).toPoint(); };
        canvas.setTool(Canvas::Tool::Rectangle);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({5,5}));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({10,10}));
        QVERIFY(canvas.selectionBounds()); QCOMPARE(*canvas.selectionBounds(), QRectF(5,5,5,5));
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({1,1}));
        QTest::mouseMove(&canvas,point({3,3})); QTest::keyClick(&canvas,Qt::Key_Escape);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({3,3}));
        QCOMPARE(*canvas.selectionBounds(), QRectF(5,5,5,5));
        canvas.setTool(Canvas::Tool::Brush);
        Arc::Brush brush; brush.color = Qt::blue; brush.diameter = 20; canvas.setBrush(brush);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({7,7}));
        QCOMPARE(painted.size(), 1);
        auto image = painted[0][1].value<QImage>();
        QCOMPARE(image.pixelColor(5,4), QColor(Qt::blue)); // Image origin is (2,3).
        QCOMPARE(image.pixelColor(0,0), QColor(Qt::red));
        canvas.setDocument(d);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({7,7}));
        QTest::mouseMove(&canvas,point({9,7})); QTest::keyClick(&canvas,Qt::Key_Escape);
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({9,7}));
        QCOMPARE(painted.size(), 1);
        canvas.clearSelection(); QVERIFY(!canvas.selectionBounds());
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({7,7}));
        QCOMPARE(painted.size(), 2); // Cancellation leaves the next stroke usable.
    }
    void paintUndoAndProjectRoundTrip() {
        QTemporaryDir temp;
        Window window; window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *add = window.findChild<QPushButton *>("addPaintLayer"); QVERIFY(add); QTest::mouseClick(add,Qt::LeftButton);
        auto *canvas = window.findChild<Canvas *>(); canvas->fit(); canvas->setTool(Canvas::Tool::Brush);
        auto *list = window.findChild<QTreeWidget *>("layers"); QCOMPARE(list->topLevelItemCount(), 1);
        QSignalSpy painted(canvas, &Canvas::painted);
        QPoint start = canvas->canvasToWidget({100,100}).toPoint();
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(canvas,start+QPoint(40,0));
        QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,start+QPoint(40,0));
        QCOMPARE(painted.size(), 1);
        auto image = painted[0][1].value<QImage>(); QVERIFY(image.pixelColor(100,100).alpha() > 0);
        Arc::Document d; Arc::Layer layer; layer.image = image; layer.size = image.size(); layer.name = "Paint";
        d.layers.append(layer); d.active = 0;
        auto path = temp.filePath("paint.comp"); Arc::saveProject(d,path);
        QCOMPARE(Arc::loadProject(path).layers[0].image, image);
        QAction *undo = nullptr;
        for (auto *action : window.findChildren<QAction *>()) if (action->shortcut() == QKeySequence::Undo) undo = action;
        QVERIFY(undo); undo->trigger(); QCOMPARE(list->topLevelItemCount(), 1);
        QVERIFY(window.isWindowModified()); // The new layer remains; one stroke was one edit.
        undo->trigger(); QCOMPARE(list->topLevelItemCount(), 0); QVERIFY(!window.isWindowModified());
    }
    void maskRenderingAndPersistence() {
        auto d = sample(); auto source = d.layers[0].image;
        auto &l = d.layers[0]; l.mask = QImage(1,1,QImage::Format_Grayscale8); l.mask.fill(QColor(128,128,128));
        auto rendered = Arc::render(d);
        QCOMPARE(rendered.pixelColor(4,5).alpha(), 128);
        QCOMPARE(rendered.pixelColor(4,5).red(), 255);
        QCOMPARE(l.image, source);
        l.maskEnabled = false; QCOMPARE(Arc::render(d).pixelColor(4,5), QColor(Qt::red));
        QTemporaryDir temp; auto path = temp.filePath("mask.comp"); Arc::saveProject(d,path);
        QCOMPARE(metadata(path)["version"].toInt(), 4);
        auto loaded = Arc::loadProject(path);
        QVERIFY(!loaded.layers[0].maskEnabled); QCOMPARE(loaded.layers[0].mask.constScanLine(0)[0], uchar(128));
        l.maskEnabled = true; l.rotation = 90; l.flipX = true; Arc::saveProject(d,path);
        QCOMPARE(Arc::render(Arc::loadProject(path)), Arc::render(d));
        Arc::exportImage(d,temp.filePath("masked.png"));
        QCOMPARE(QImage(temp.filePath("masked.png")).pixelColor(10,9).alpha(), 128);
        auto manifest = metadata(path); auto layers = manifest["layers"].toArray(); auto layer = layers[0].toObject();
        QString asset = path+"/images/"+layer["maskFile"].toString();
        QImage invalid(1,1,QImage::Format_ARGB32); invalid.fill(Qt::transparent); QVERIFY(invalid.save(asset));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        layer["maskFile"] = "../escape.mask.png"; layers[0] = layer; manifest["layers"] = layers; writeMetadata(path,manifest);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
    }
    void maskBrushAndSoftEdges() {
        auto d = sample(); auto &l = d.layers[0];
        l.image = QImage(64,64,QImage::Format_ARGB32_Premultiplied); l.image.fill(Qt::red);
        l.size = {128,128}; l.origin = {10,20}; l.rotation = 90; l.flipX = true;
        l.mask = QImage(1,1,QImage::Format_Grayscale8); l.mask.fill(Qt::white);
        QCOMPARE(l.mask.constScanLine(0)[0], uchar(255));
        QTransform mapping = l.transform(); mapping.scale(2,2);
        QPainterPath dot; dot.moveTo(mapping.map(QPointF(32,32)));
        Arc::Brush brush; brush.diameter = 80; brush.hardness = 0; brush.color = Qt::black;
        auto mask = Arc::paintMaskStroke(l,dot,brush,QRectF(0,0,200,200));
        QCOMPARE(mask.size(), QSize(64,64));
        QVERIFY(mask.constScanLine(32)[32] < 35);
        QVERIFY(mask.constScanLine(32)[42] > mask.constScanLine(32)[32]);
        QVERIFY(mask.constScanLine(32)[42] < 230);
        QCOMPARE(mask.constScanLine(0)[0], uchar(255));
        QCOMPARE(l.mask.constScanLine(0)[0], uchar(255));
        l.mask = mask; brush.hardness = 1; brush.eraser = true;
        auto restored = Arc::paintMaskStroke(l,dot,brush,QRectF(0,0,200,200));
        QCOMPARE(restored.constScanLine(32)[32], uchar(255));
        QCOMPARE(l.image.pixelColor(32,32), QColor(Qt::red));
        brush.eraser = false; brush.hardness = 0; brush.opacity = 0.5;
        auto soft = Arc::paintStroke(l,dot,brush,QRectF(0,0,200,200));
        QVERIFY(soft.pixelColor(32,32).red() > 110); // Half-opacity paint cannot fully cover red.
        QVERIFY(soft.pixelColor(32,32).red() < 160);
    }
    void maskControlsUndoAndCancel() {
        QTemporaryDir temp; QVERIFY(sample().layers[0].image.save(temp.filePath("red.png")));
        Window window; window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.importImages({temp.filePath("red.png")});
        auto *add = window.findChild<QPushButton *>("addMask");
        auto *remove = window.findChild<QPushButton *>("removeMask");
        auto *target = window.findChild<QComboBox *>("paintTarget");
        auto *enabled = window.findChild<QCheckBox *>("maskEnabled");
        QVERIFY(add->isEnabled()); QTest::mouseClick(add,Qt::LeftButton);
        QVERIFY(enabled->isChecked()); QCOMPARE(target->currentIndex(),1);
        auto *canvas = window.findChild<Canvas *>(); canvas->setTool(Canvas::Tool::Brush); canvas->fit();
        auto point = canvas->canvasToWidget({8,6}).toPoint();
        QSignalSpy masks(canvas, &Canvas::maskPainted), images(canvas, &Canvas::painted);
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,point);
        QTest::keyClick(canvas,Qt::Key_Escape); QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(masks.size(),0);
        QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(masks.size(),1); QCOMPARE(images.size(),0);
        QCOMPARE(masks[0][1].value<QImage>().constScanLine(6)[8], uchar(0));
        QTest::mouseClick(enabled,Qt::LeftButton,Qt::NoModifier,QPoint(8,enabled->height()/2)); QVERIFY(!enabled->isChecked());
        QAction *undo = nullptr;
        for (auto *action : window.findChildren<QAction *>()) if (action->shortcut() == QKeySequence::Undo) undo = action;
        QVERIFY(undo); undo->trigger(); QVERIFY(enabled->isChecked());
        QTest::mouseClick(remove,Qt::LeftButton); QVERIFY(!enabled->isEnabled()); QCOMPARE(target->currentIndex(),0);
        undo->trigger(); QVERIFY(enabled->isEnabled());
        undo->trigger(); // Undo paint.
        undo->trigger(); // Undo add mask.
        QVERIFY(!enabled->isEnabled());
        undo->trigger(); QVERIFY(!window.isWindowModified());
    }
    void regionalRenderingMatchesFullCanvas() {
        auto d = sample(); d.size = {100,100};
        auto &l = d.layers[0]; l.origin = {20,30}; l.size = {48,36}; l.rotation = 28; l.flipX = true;
        l.mask = QImage(8,6,QImage::Format_Grayscale8);
        for (int y=0;y<6;++y) for (int x=0;x<8;++x) l.mask.scanLine(y)[x] = x*30;
        auto full = Arc::render(d);
        QRect region(30,35,22,19); QImage partial(d.size,QImage::Format_ARGB32_Premultiplied);
        partial.fill(Qt::transparent); Arc::repaintRegion(partial,d,region);
        for (int y=0;y<region.height();++y) for (int x=0;x<region.width();++x)
            QCOMPARE(partial.pixel(x+region.x(),y+region.y()),full.pixel(x+region.x(),y+region.y()));
    }
    void paintingPerformanceSample() {
        Arc::Document d; d.size = {3840,2160};
        for (int i = 0; i < 4; ++i) {
            Arc::Layer l; l.image = QImage(d.size,QImage::Format_ARGB32_Premultiplied); l.image.fill(QColor(50*i,80,150));
            l.size = d.size; l.opacity = 0.5; d.layers.append(l);
        }
        d.layers[3].mask = QImage(1,1,QImage::Format_Grayscale8); d.layers[3].mask.fill(Qt::white);
        QPainterPath path; path.moveTo(300,400); path.lineTo(1600,900);
        Arc::Brush brush; brush.hardness = 0.3; brush.diameter = 120;
        QElapsedTimer timer; timer.start();
        for (int i=0; i<3; ++i) {
            auto preview = d;
            preview.layers[3].mask = Arc::paintMaskStroke(d.layers[3],path,brush,QRectF(QPointF(),d.size));
            QVERIFY(!Arc::render(preview).isNull());
        }
        qInfo("4K / four layers / soft mask stroke + full render: %.1f ms per preview (3 samples)", timer.nsecsElapsed()/3000000.0);
        auto composite = Arc::render(d);
        timer.restart();
        for (int i=0; i<3; ++i) {
            auto preview = d;
            preview.layers[3].mask = Arc::paintMaskStroke(d.layers[3],path,brush,QRectF(QPointF(),d.size));
            auto region = path.boundingRect().adjusted(-63,-63,63,63).toAlignedRect();
            Arc::repaintRegion(composite,preview,region);
            QVERIFY(!composite.isNull());
        }
        qInfo("4K / four layers / soft mask stroke + regional render: %.1f ms per preview (3 samples)", timer.nsecsElapsed()/3000000.0);
    }
    void cropResizeAndMerge() {
        auto d = sample(); auto original = d.layers[0].image;
        Arc::crop(d,QRect(1,2,20,16)); QCOMPARE(d.size,QSize(20,16)); QCOMPARE(d.layers[0].origin,QPointF(1,1));
        QCOMPARE(d.layers[0].image,original);
        Arc::resizeCanvas(d,{24,20},true); QCOMPARE(d.layers[0].origin,QPointF(3,3));
        Arc::resizeImage(d,{48,40}); QCOMPARE(d.size,QSize(48,40)); QCOMPARE(d.layers[0].origin,QPointF(6,6));
        QCOMPARE(d.layers[0].image.size(),QSize(32,24));
        auto top=d.layers[0]; top.id=QUuid::createUuid(); top.image.fill(Qt::blue); top.origin += QPointF(3,4); top.opacity=0.5;
        d.layers.append(top); d.active=1; auto before=Arc::render(d);
        Arc::mergeDown(d); QCOMPARE(d.layers.size(),1); QCOMPARE(Arc::render(d),before);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::resizeCanvas(d,{30000,30000},true));
    }
    void maskFeatherPreservesCoverageAndSource() {
        QImage mask(31,31,QImage::Format_Grayscale8); mask.fill(Qt::black);
        for(int y=10;y<21;++y) for(int x=10;x<21;++x) mask.scanLine(y)[x]=255;
        auto blurred=Arc::blurImage(mask,4);
        QCOMPARE(blurred.format(),QImage::Format_Grayscale8);
        QVERIFY(blurred.constScanLine(15)[9]>0); QVERIFY(blurred.constScanLine(15)[10]<255);
        QCOMPARE(mask.constScanLine(15)[9],uchar(0)); QCOMPARE(mask.constScanLine(15)[10],uchar(255));
    }
    void wandAndVectorSelections() {
        QImage image(20,20,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
        { QPainter p(&image); p.fillRect(QRect(2,2,5,5),Qt::black); p.fillRect(QRect(12,12,5,5),Qt::black); }
        auto wand=Arc::wandSelection(image,{3,3},0,true);
        QVERIFY(wand.contains(QPointF(4,4))); QVERIFY(!wand.contains(QPointF(13,13)));
        auto global=Arc::wandSelection(image,{3,3},0,false); QVERIFY(global.contains(QPointF(13,13)));
        Canvas canvas; canvas.resize(640,480); canvas.setDocument(sample()); canvas.show(); canvas.fit();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        auto point=[&](QPointF p) { return canvas.canvasToWidget(p).toPoint(); };
        canvas.setTool(Canvas::Tool::Ellipse);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::NoModifier,point({2,2}));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::NoModifier,point({12,12}));
        QVERIFY(canvas.selectedPath()->contains(QPointF(7,7))); QVERIFY(!canvas.selectedPath()->contains(QPointF(2.1,2.1)));
        canvas.setTool(Canvas::Tool::Rectangle);
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::ShiftModifier,point({15,2}));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::ShiftModifier,point({20,7}));
        QVERIFY(canvas.selectedPath()->contains(QPointF(17,4))); QVERIFY(canvas.selectedPath()->contains(QPointF(7,7)));
        QTest::mousePress(&canvas,Qt::LeftButton,Qt::AltModifier,point({5,5}));
        QTest::mouseRelease(&canvas,Qt::LeftButton,Qt::AltModifier,point({9,9}));
        QVERIFY(!canvas.selectedPath()->contains(QPointF(7,7))); QVERIFY(canvas.selectedPath()->contains(QPointF(17,4)));
        canvas.setTool(Canvas::Tool::Polygon);
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({1,1}));
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({10,1}));
        QTest::mouseClick(&canvas,Qt::LeftButton,Qt::NoModifier,point({1,10})); QTest::keyClick(&canvas,Qt::Key_Return);
        QVERIFY(canvas.selectedPath()->contains(QPointF(3,3))); QVERIFY(!canvas.selectedPath()->contains(QPointF(9,9)));
    }
    void adjustmentKernelsAndSelection() {
        QImage image(2,1,QImage::Format_ARGB32_Premultiplied); image.setPixelColor(0,0,QColor(64,64,64)); image.setPixelColor(1,0,QColor(192,192,192));
        auto levels=Arc::defaultFilter("Levels"); levels.values["black"]=64; levels.values["white"]=192;
        auto output=Arc::applyFilter(image,levels); QCOMPARE(output.pixelColor(0,0),QColor(Qt::black)); QCOMPARE(output.pixelColor(1,0),QColor(Qt::white));
        auto exposure=Arc::defaultFilter("Exposure"); exposure.values["exposure"]=1;
        image.fill(QColor(128,128,128)); output=Arc::applyFilter(image,exposure); QVERIFY(std::abs(output.pixelColor(0,0).red()-176)<=1);
        auto curve=Arc::defaultFilter("Curves"); curve.curve={{0,255},{255,0}};
        QCOMPARE(Arc::applyFilter(image,curve).pixelColor(0,0).red(),127);
        curve.curve={{0,0},{0,255}}; QVERIFY_THROWS_EXCEPTION(std::runtime_error,Arc::applyFilter(image,curve));
        image.fill(Qt::red); auto hue=Arc::defaultFilter("Hue/Saturation"); hue.values["hue"]=120;
        QCOMPARE(Arc::applyFilter(image,hue).pixelColor(0,0),QColor(Qt::green));
        auto noise=Arc::defaultFilter("Noise"); QCOMPARE(Arc::applyFilter(image,noise),Arc::applyFilter(image,noise));
        auto lens=Arc::defaultFilter("Lens Correction"); QCOMPARE(Arc::applyFilter(image,lens),image);
        auto d=sample(); auto &layer=d.layers[0]; QPainterPath selected; selected.addRect(QRectF(2,3,5,5));
        auto invert=Arc::defaultFilter("Invert"); output=Arc::filterLayer(layer,invert,selected,false);
        QCOMPARE(output.pixelColor(1,1),QColor(Qt::cyan)); QCOMPARE(output.pixelColor(10,8),QColor(Qt::red));
        layer.mask=QImage(1,1,QImage::Format_Grayscale8); layer.mask.fill(Qt::white);
        output=Arc::filterLayer(layer,invert,selected,true);
        QCOMPARE(output.size(),layer.image.size()); QCOMPARE(output.constScanLine(1)[1],uchar(0)); QCOMPARE(output.constScanLine(8)[10],uchar(255));
        layer.image.fill(QColor(80,120,160)); output=Arc::contentAwareFill(layer,selected);
        QCOMPARE(output.pixelColor(1,1),QColor(80,120,160));
    }
    void filterPreviewCancelAndUndo() {
        QTemporaryDir temp; QVERIFY(sample().layers[0].image.save(temp.filePath("red.png")));
        Window window; window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); window.importImages({temp.filePath("red.png")});
        auto *canvas=window.findChild<Canvas *>();
        auto sampledColor=[&] {
            auto image=canvas->grab().toImage();
            auto point=canvas->canvasToWidget({8,6})*image.devicePixelRatio();
            return image.pixelColor(point.toPoint());
        };
        QAction *invert=nullptr,*undo=nullptr;
        for(auto *action:window.findChildren<QAction *>()) {
            if(action->text()=="Invert…") invert=action;
            if(action->shortcut()==QKeySequence::Undo) undo=action;
        }
        QVERIFY(invert); QVERIFY(undo); bool sawPreview=false;
        QTimer::singleShot(150,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if(dialog) { sawPreview=sampledColor()==QColor(Qt::cyan); dialog->reject(); }
        });
        invert->trigger(); QVERIFY(sawPreview); QCOMPARE(sampledColor(),QColor(Qt::red));
        QTimer::singleShot(150,[&] { if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->accept(); });
        invert->trigger(); QCOMPARE(sampledColor(),QColor(Qt::cyan));
        undo->trigger(); QCOMPARE(sampledColor(),QColor(Qt::red));
        undo->trigger(); QVERIFY(!window.isWindowModified());
    }
    void windowUndoAndLayerControls() {
        QTemporaryDir temp; auto image = sample().layers[0].image; QVERIFY(image.save(temp.filePath("red.png")));
        Window window; window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.importImages({temp.filePath("red.png")});
        auto *list = window.findChild<QTreeWidget *>("layers"); QCOMPARE(list->topLevelItemCount(), 1);
        auto *x = window.findChild<QDoubleSpinBox *>("layerX"); x->setValue(15); QCOMPARE(x->value(), 15.0);
        QAction *undo = nullptr, *redo = nullptr;
        for (auto *action : window.findChildren<QAction *>()) {
            if (action->shortcut() == QKeySequence::Undo) undo = action;
            if (action->shortcut() == QKeySequence("Ctrl+Shift+Z")) redo = action;
        }
        QVERIFY(undo); QVERIFY(redo); undo->trigger(); QCOMPARE(x->value(), 0.0); redo->trigger(); QCOMPARE(x->value(), 15.0);
        list->topLevelItem(0)->setText(0,"Renamed"); QCOMPARE(list->topLevelItem(0)->text(0), QString("Renamed"));
        undo->trigger(); QCOMPARE(list->topLevelItem(0)->text(0), QString("red"));
        undo->trigger(); undo->trigger(); QCOMPARE(list->topLevelItemCount(), 0);
        QVERIFY(!window.isWindowModified());
    }
};
QTEST_MAIN(Tests)
#include "tests.moc"
