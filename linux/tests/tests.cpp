#include "../src/document.h"
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
