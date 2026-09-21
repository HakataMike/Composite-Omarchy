#include "../src/document.h"
#include "../src/operations.h"
#include "../src/selection.h"
#include "../src/filters.h"
#include "../src/retouch.h"
#include "../src/styles.h"
#include "../src/blending.h"
#include <QPlainTextEdit>
#include "../src/canvas.h"
#include "../src/window.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QListWidget>
#include <QTreeWidget>
#include "../src/hierarchy.h"
#include "../src/clipping.h"
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
