#include "window.h"
#include <QBuffer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <stdexcept>
void Window::exportDialog() {
    if(!canvas->finishTextEditing()) return;
    auto path=QFileDialog::getSaveFileName(this,"Export image","Untitled.png","PNG (*.png);;JPEG (*.jpg *.jpeg)",nullptr,QFileDialog::DontUseNativeDialog);
    if(path.isEmpty()) return;
    report([&] {
        auto suffix=QFileInfo(path).suffix().toLower(); bool jpeg=suffix=="jpg" || suffix=="jpeg";
        if(!jpeg && suffix!="png") throw std::runtime_error("Use a .png, .jpg, or .jpeg filename.");
        auto source=Arc::render(document,jpeg); auto small=source.scaled(850,520,Qt::KeepAspectRatio,Qt::SmoothTransformation); source={};
        QDialog dialog(this); dialog.setWindowTitle("Export preview"); QVBoxLayout layout(&dialog);
        QLabel preview; preview.setObjectName("exportPreview"); preview.setAlignment(Qt::AlignCenter); preview.setMinimumSize(420,280); layout.addWidget(&preview);
        QLabel description(QString("%1 × %2 pixels • %3\nPreview scaled to fit; export uses the full resolution.")
            .arg(document.size.width()).arg(document.size.height()).arg(jpeg ? "JPEG, white background" : "PNG, transparency preserved")); layout.addWidget(&description);
        QSpinBox quality; quality.setObjectName("jpegQuality"); quality.setRange(1,100); quality.setValue(95); quality.setPrefix("JPEG quality: "); quality.setVisible(jpeg); layout.addWidget(&quality);
        QLabel error; layout.addWidget(&error);
        QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel); layout.addWidget(&buttons);
        QTimer timer; timer.setSingleShot(true); timer.setInterval(120);
        auto update=[&] {
            QImage image=small;
            if(jpeg) {
                QByteArray data; QBuffer buffer(&data); buffer.open(QIODevice::WriteOnly); QImageWriter writer(&buffer,"jpeg"); writer.setQuality(quality.value());
                if(!writer.write(small)) { error.setText(writer.errorString()); buttons.button(QDialogButtonBox::Save)->setEnabled(false); return; }
                image=QImage::fromData(data,"jpeg");
            }
            QImage checker(image.size(),QImage::Format_RGB32); checker.fill(QColor(235,235,235));
            { QPainter p(&checker); for(int y=0;y<checker.height();y+=12) for(int x=0;x<checker.width();x+=12) if((x/12+y/12)%2) p.fillRect(x,y,12,12,QColor(195,195,195)); p.drawImage(QPoint(),image); }
            preview.setPixmap(QPixmap::fromImage(checker)); error.clear(); buttons.button(QDialogButtonBox::Save)->setEnabled(true);
        };
        connect(&quality,&QSpinBox::valueChanged,&dialog,[&] { timer.start(); }); connect(&timer,&QTimer::timeout,&dialog,update);
        connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        update(); if(dialog.exec()==QDialog::Accepted) { Arc::exportImage(document,path,quality.value()); statusBar()->showMessage("Exported "+path,5000); }
    });
}
