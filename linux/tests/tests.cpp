#include "../src/document.h"
#include "../src/operations.h"
#include "../src/selection.h"
#include "../src/filters.h"
#include "../src/canvas.h"
#include "../src/window.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QListWidget>
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
        auto m = original; m["version"] = 8; writeMetadata(path, m);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::loadProject(path));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, Arc::saveProject(sample(), path));
        QCOMPARE(metadata(path)["version"].toInt(), 8);
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
        auto *list = window.findChild<QListWidget *>("layers"); QCOMPARE(list->count(), 1);
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
        QVERIFY(undo); undo->trigger(); QCOMPARE(list->count(), 1);
        QVERIFY(window.isWindowModified()); // The new layer remains; one stroke was one edit.
        undo->trigger(); QCOMPARE(list->count(), 0); QVERIFY(!window.isWindowModified());
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
        auto *canvas=window.findChild<Canvas *>(); auto point=canvas->canvasToWidget({8,6}).toPoint();
        QAction *invert=nullptr,*undo=nullptr;
        for(auto *action:window.findChildren<QAction *>()) {
            if(action->text()=="Invert…") invert=action;
            if(action->shortcut()==QKeySequence::Undo) undo=action;
        }
        QVERIFY(invert); QVERIFY(undo); bool sawPreview=false;
        QTimer::singleShot(150,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if(dialog) { sawPreview=canvas->grab().toImage().pixelColor(point)==QColor(Qt::cyan); dialog->reject(); }
        });
        invert->trigger(); QVERIFY(sawPreview); QCOMPARE(canvas->grab().toImage().pixelColor(point),QColor(Qt::red));
        QTimer::singleShot(150,[&] { if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->accept(); });
        invert->trigger(); QCOMPARE(canvas->grab().toImage().pixelColor(point),QColor(Qt::cyan));
        undo->trigger(); QCOMPARE(canvas->grab().toImage().pixelColor(point),QColor(Qt::red));
        undo->trigger(); QVERIFY(!window.isWindowModified());
    }
    void windowUndoAndLayerControls() {
        QTemporaryDir temp; auto image = sample().layers[0].image; QVERIFY(image.save(temp.filePath("red.png")));
        Window window; window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.importImages({temp.filePath("red.png")});
        auto *list = window.findChild<QListWidget *>("layers"); QCOMPARE(list->count(), 1);
        auto *x = window.findChild<QDoubleSpinBox *>("layerX"); x->setValue(15); QCOMPARE(x->value(), 15.0);
        QAction *undo = nullptr, *redo = nullptr;
        for (auto *action : window.findChildren<QAction *>()) {
            if (action->shortcut() == QKeySequence::Undo) undo = action;
            if (action->shortcut() == QKeySequence("Ctrl+Shift+Z")) redo = action;
        }
        QVERIFY(undo); QVERIFY(redo); undo->trigger(); QCOMPARE(x->value(), 0.0); redo->trigger(); QCOMPARE(x->value(), 15.0);
        list->item(0)->setText("Renamed"); QCOMPARE(list->item(0)->text(), QString("Renamed"));
        undo->trigger(); QCOMPARE(list->item(0)->text(), QString("red"));
        undo->trigger(); undo->trigger(); QCOMPARE(list->count(), 0);
        QVERIFY(!window.isWindowModified());
    }
};
QTEST_MAIN(Tests)
#include "tests.moc"
