#include "window.h"
#include "workspace.h"
#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QTimer>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Compositor ARC");
    QApplication::setOrganizationName("Compositor ARC");
    QImageReader::setAllocationLimit(512);
    Workspace window; window.show();
    QTimer::singleShot(0, &window, [&] {
        auto args = app.arguments().mid(1);
        if (args.size() == 1 && QFileInfo(args[0]).isDir()) window.openProject(args[0]);
        else if (!args.isEmpty()) window.activeEditor()->importImages(args);
        else window.activeEditor()->findChild<Canvas *>()->fit();
    });
    return app.exec();
}
