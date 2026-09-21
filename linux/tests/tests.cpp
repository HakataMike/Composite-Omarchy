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
