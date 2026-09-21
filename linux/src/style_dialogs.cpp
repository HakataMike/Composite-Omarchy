#include "window.h"
#include "styles.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox *numeric(QFormLayout *form, QString name, double value, double lo, double hi) {
    auto *field=new QDoubleSpinBox; field->setRange(lo,hi); field->setValue(value);
    field->setKeyboardTracking(false); field->setAccessibleName(name); form->addRow(name,field); return field;
}
QColor styleColor(const QJsonObject &s) {
    return QColor::fromRgbF(s["red"].toDouble(),s["green"].toDouble(),s["blue"].toDouble());
}
void setColor(QJsonObject &s,QColor color) { s["red"]=color.redF(); s["green"]=color.greenF(); s["blue"]=color.blueF(); }
}
void Window::shapeDialog() {
    if(document.active<0 || document.layers[document.active].shape.isEmpty()) return;
    int index=document.active; auto original=document; auto style=original.layers[index].shape;
    QDialog dialog(this); dialog.setWindowTitle("Edit shape"); dialog.setObjectName("shapeDialog");
    QVBoxLayout layout(&dialog); QFormLayout form; layout.addLayout(&form);
    auto *radius=numeric(&form,"Corner radius",style["cornerRadius"].toDouble(),0,30000);
    auto *thickness=numeric(&form,"Line width",style["lineWidth"].toDouble(4),0.1,30000);
    radius->setEnabled(style["kind"]=="Rectangle"); thickness->setEnabled(style["kind"]=="Line");
    QPushButton color("Fill color…"); form.addRow(&color);
    QLabel error; error.setWordWrap(true); layout.addWidget(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout.addWidget(&buttons);
    QImage result; bool valid=false;
    auto update=[&] {
        try {
            style["cornerRadius"]=radius->value();
            if(style["kind"]=="Line") style["lineWidth"]=thickness->value();
            result=Arc::shapeImage(style,original.layers[index].size);
            auto preview=original; preview.layers[index].shape=style; preview.layers[index].image=result;
            Arc::validate(preview); canvas->setDocument(preview);
            error.clear(); valid=true;
        } catch(const std::exception &e) { error.setText(QString::fromUtf8(e.what())); valid=false; }
        buttons.button(QDialogButtonBox::Ok)->setEnabled(valid);
    };
    connect(radius,&QDoubleSpinBox::valueChanged,&dialog,update);
    connect(thickness,&QDoubleSpinBox::valueChanged,&dialog,update);
    connect(&color,&QPushButton::clicked,&dialog,[&] {
        auto chosen=QColorDialog::getColor(styleColor(style),&dialog,"Shape color");
        if(chosen.isValid()) { setColor(style,chosen); update(); }
    });
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    update(); int accepted=dialog.exec();
    if(accepted==QDialog::Accepted) update();
    canvas->setDocument(original);
    if(accepted==QDialog::Accepted && valid) edit("Edit shape",[&](auto &d) { d.layers[index].shape=style; d.layers[index].image=result; });
}
void Window::textDialog(QRectF bounds, QColor color, bool editActive) {
    int index=-1;
    if(editActive) {
        if(document.active<0 || document.layers[document.active].text.isEmpty()) return;
        index=document.active;
    } else if(bounds.width()<2 && bounds.height()<2) {
        for(int i=document.layers.size()-1;i>=0;--i) {
            const auto &l=document.layers[i];
            if(l.visible && !l.text.isEmpty() && QRectF(QPointF(),l.size).contains(l.transform().inverted().map(bounds.topLeft()))) { index=i; break; }
        }
    }
    auto original=document;
    auto style=index>=0 ? document.layers[index].text : Arc::textStyle(color);
    if(index<0 && bounds.width()>=16 && bounds.height()>=16) style["boxSize"]=QJsonArray{bounds.width(),bounds.height()};
    QDialog dialog(this); dialog.setObjectName("textDialog"); dialog.setWindowTitle(index>=0 ? "Edit text" : "Add text");
    dialog.resize(530,650); QVBoxLayout layout(&dialog);
    QPlainTextEdit content(style["content"].toString()); content.setObjectName("textContent"); layout.addWidget(&content);
    QFormLayout form; layout.addLayout(&form);
    QFontComboBox family; family.setCurrentFont(QFont(style["fontName"].toString())); form.addRow("Font",&family);
    auto *size=numeric(&form,"Font size (pixels)",style["fontSize"].toDouble(),1,2000);
    auto *tracking=numeric(&form,"Tracking",style["tracking"].toDouble(),-100,1000);
    auto *leading=numeric(&form,"Leading (0 = auto)",style["leading"].toDouble(),0,5000);
    QComboBox align; align.addItems({"Left","Center","Right"}); align.setCurrentText(style["alignment"].toString()); form.addRow("Alignment",&align);
    QPushButton foreground("Text color…"); form.addRow(&foreground);
    QCheckBox fixed("Fixed paragraph box"); fixed.setChecked(style.contains("boxSize")); form.addRow(&fixed);
    auto box=style["boxSize"].toArray();
    auto *width=numeric(&form,"Box width",box.isEmpty() ? 600 : box[0].toDouble(),16,30000);
    auto *height=numeric(&form,"Box height",box.isEmpty() ? 300 : box[1].toDouble(),16,30000);
    QLabel error; error.setWordWrap(true); layout.addWidget(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout.addWidget(&buttons);
    QTimer timer; timer.setSingleShot(true); timer.setInterval(120);
    Arc::Layer result; bool valid=false;
    auto update=[&] {
        try {
            style["content"]=content.toPlainText(); style["fontSize"]=size->value();
            style["tracking"]=tracking->value(); style["leading"]=leading->value(); style["alignment"]=align.currentText();
            width->setEnabled(fixed.isChecked()); height->setEnabled(fixed.isChecked());
            if(fixed.isChecked()) style["boxSize"]=QJsonArray{width->value(),height->value()}; else style.remove("boxSize");
            result=index>=0 ? original.layers[index] : Arc::Layer();
            double sx=index>=0 ? result.size.width()/result.image.width() : 1;
            double sy=index>=0 ? result.size.height()/result.image.height() : 1;
            result.text=style; result.image=Arc::textImage(style);
            result.size={result.image.width()*sx,result.image.height()*sy};
            if(index<0) { result.name=content.toPlainText().left(40); result.origin=bounds.topLeft(); }
            auto preview=original;
            if(index>=0) preview.layers[index]=result;
            else { preview.layers.insert(original.active+1,result); preview.active=original.active+1; }
            Arc::validate(preview); canvas->setDocument(preview);
            valid=true; error.clear();
        } catch(const std::exception &e) { valid=false; error.setText(QString::fromUtf8(e.what())); }
        buttons.button(QDialogButtonBox::Ok)->setEnabled(valid);
    };
    auto schedule=[&] { valid=false; buttons.button(QDialogButtonBox::Ok)->setEnabled(false); timer.start(); };
    connect(&timer,&QTimer::timeout,&dialog,update);
    connect(&content,&QPlainTextEdit::textChanged,&dialog,schedule);
    connect(&family,&QFontComboBox::currentFontChanged,&dialog,[&](const QFont &font) { style["fontName"]=font.family(); schedule(); });
    for(auto *field:{size,tracking,leading,width,height}) connect(field,&QDoubleSpinBox::valueChanged,&dialog,schedule);
    connect(&align,&QComboBox::currentTextChanged,&dialog,schedule);
    connect(&fixed,&QCheckBox::toggled,&dialog,schedule);
    connect(&foreground,&QPushButton::clicked,&dialog,[&] {
        auto chosen=QColorDialog::getColor(styleColor(style),&dialog,"Text color");
        if(chosen.isValid()) { setColor(style,chosen); schedule(); }
    });
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    update(); content.selectAll(); content.setFocus();
    int accepted=dialog.exec(); timer.stop();
    if(accepted==QDialog::Accepted) update();
    canvas->setDocument(original);
    if(accepted==QDialog::Accepted && valid && (index>=0 || !style["content"].toString().trimmed().isEmpty())) {
        edit(index>=0 ? "Edit text" : "Add text",[&](auto &d) {
            if(index>=0) d.layers[index]=result;
            else { d.layers.insert(original.active+1,result); d.active=original.active+1; }
        });
    }
}

void Window::guidesDialog() {
    auto original=document;
    auto guides=document.guides;
    QDialog dialog(this); dialog.setWindowTitle("Manage guides"); dialog.setObjectName("guidesDialog");
    QVBoxLayout layout(&dialog);
    QLabel hint("Guides are saved with the project and do not appear in exports.\nUse Move to drag unlocked guides; Alt bypasses guides and snapping.");
    hint.setWordWrap(true); layout.addWidget(&hint);
    QListWidget list; list.setObjectName("guideList"); layout.addWidget(&list);
    QFormLayout form; layout.addLayout(&form);
    QComboBox axis; axis.addItems({"vertical","horizontal"}); form.addRow("Axis",&axis);
    auto *position=numeric(&form,"Position (pixels)",0,-1000000,1000000);
    position->setObjectName("guidePosition");
    QPushButton add("Add guide"), change("Update selected guide"), remove("Remove selected guide");
    add.setObjectName("addGuide"); layout.addWidget(&add); layout.addWidget(&change); layout.addWidget(&remove);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout.addWidget(&buttons);
    auto refresh=[&] {
        int selected=list.currentRow();
        list.clear();
        for(const auto &g:guides) list.addItem(g.axis+" — "+QString::number(g.position,'f',2)+" px");
        list.setCurrentRow(std::min(selected,int(guides.size())-1));
        add.setEnabled(guides.size()<1000);
        auto preview=original; preview.guides=guides; canvas->setDocument(preview);
    };
    connect(&list,&QListWidget::currentRowChanged,&dialog,[&](int row) {
        bool valid=row>=0 && row<guides.size(); change.setEnabled(valid); remove.setEnabled(valid);
        if(valid) { axis.setCurrentText(guides[row].axis); position->setValue(guides[row].position); }
    });
    connect(&add,&QPushButton::clicked,&dialog,[&] {
        if(guides.size()>=1000) return;
        Arc::Guide g; g.axis=axis.currentText(); g.position=position->value(); guides.append(g);
        refresh(); list.setCurrentRow(guides.size()-1);
    });
    connect(&change,&QPushButton::clicked,&dialog,[&] {
        int row=list.currentRow(); if(row<0) return;
        guides[row].axis=axis.currentText(); guides[row].position=position->value(); refresh();
    });
    connect(&remove,&QPushButton::clicked,&dialog,[&] { if(list.currentRow()>=0) { guides.removeAt(list.currentRow()); refresh(); } });
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    refresh(); change.setEnabled(false); remove.setEnabled(false);
    int accepted=dialog.exec(); canvas->setDocument(original);
    if(accepted==QDialog::Accepted) edit("Edit guides",[&](auto &d) { d.guides=guides; });
}
