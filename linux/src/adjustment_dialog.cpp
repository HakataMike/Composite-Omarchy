#include "window.h"
#include "adjustments.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <stdexcept>

void Window::adjustmentDialog(QString kind) {
    const auto original=document; auto working=original;
    const bool creating=!kind.isEmpty();
    if(creating) {
        Arc::Layer layer; layer.name=kind; layer.size=document.size; layer.adjustment=Arc::defaultAdjustment(kind);
        if(document.active>=0) { const auto &active=document.layers[document.active]; layer.parentID=active.isGroup ? active.id : active.parentID; }
        if(auto selection=canvas->selectedPath()) {
            layer.mask=QImage(document.size,QImage::Format_Grayscale8); layer.mask.fill(Qt::black);
            QPainter painter(&layer.mask); painter.fillPath(*selection,Qt::white);
        }
        int position=document.active<0 ? working.layers.size() : document.active+1;
        working.layers.insert(position,layer); working.active=position;
    } else {
        if(working.active<0 || working.layers[working.active].adjustment.isEmpty()) {
            statusBar()->showMessage("Select an adjustment layer first.",5000); return;
        }
        kind=working.layers[working.active].adjustment["kind"].toString();
    }
    auto draft=working.layers[working.active].adjustment;
    auto settings=Arc::filterFromAdjustment(draft);
    QDialog dialog(this); dialog.setWindowTitle((creating ? "New " : "Edit ")+kind+" adjustment");
    dialog.setMinimumWidth(380); QFormLayout form(&dialog);
    QMap<QString,QDoubleSpinBox *> fields;
    auto field=[&](QString key,QString label,double minimum,double maximum) {
        auto *spin=new QDoubleSpinBox; spin->setObjectName(key); spin->setDecimals(3);
        spin->setRange(minimum,maximum); spin->setKeyboardTracking(false); fields[key]=spin; form.addRow(label,spin);
    };
    for(auto p:Arc::filterParameters(kind)) field(p.key,p.label,p.minimum,p.maximum);
    if(kind=="Levels") fields["gamma"]->setMinimum(0.1);
    QComboBox *channel=nullptr,*range=nullptr;
    QCheckBox *colorize=nullptr,*invert=nullptr,*reversed=nullptr;
    QDoubleSpinBox *seed=nullptr;
    QPlainTextEdit *curve=nullptr;
    if(kind=="Levels" || kind=="Curves") {
        channel=new QComboBox; channel->setObjectName("adjustmentChannel"); channel->addItems({"RGB","Red","Green","Blue"}); form.addRow("Channel",channel);
    }
    if(kind=="Curves") { curve=new QPlainTextEdit; form.addRow("Curve points (input, output)",curve); }
    if(kind=="Hue/Saturation") {
        fields["hue"]->setRange(-360,360);
        range=new QComboBox; range->addItems({"Master","Reds","Yellows","Greens","Cyans","Blues","Magentas"}); form.addRow("Color range",range);
        colorize=new QCheckBox("Colorize"); invert=new QCheckBox("Invert selected range"); form.addRow(colorize); form.addRow(invert);
        field("falloffStart","Range: falloff start (degrees)",0,360); field("rangeStart","Range: full strength start",0,360);
        field("rangeEnd","Range: full strength end",0,360); field("falloffEnd","Range: falloff end",0,360);
    }
    if(kind=="Gradient Map") { reversed=new QCheckBox("Reverse gradient"); form.addRow(reversed); }
    if(kind=="Grain") { seed=new QDoubleSpinBox; seed->setDecimals(0); seed->setRange(0,4294967295.0); form.addRow("Seed",seed); }
    QTimer timer; timer.setSingleShot(true); timer.setInterval(100);
    bool loading=false;
    auto schedule=[&] { if(!loading) timer.start(); };
    if(kind=="Gradient Map") {
        auto *dark=new QPushButton("Shadow color…"),*light=new QPushButton("Highlight color…"); form.addRow(dark,light);
        connect(dark,&QPushButton::clicked,&dialog,[&] { auto c=QColorDialog::getColor(settings.shadows,&dialog); if(c.isValid()) { settings.shadows=c; schedule(); } });
        connect(light,&QPushButton::clicked,&dialog,[&] { auto c=QColorDialog::getColor(settings.highlights,&dialog); if(c.isValid()) { settings.highlights=c; schedule(); } });
    }
    QCheckBox preview("Live preview"); preview.setChecked(true); form.addRow(&preview);
    QLabel error; error.setWordWrap(true); form.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); form.addRow(&buttons);
    auto load=[&] {
        loading=true;
        for(auto i=fields.begin();i!=fields.end();++i) i.value()->setValue(settings.values[i.key()].toDouble());
        if(channel) channel->setCurrentIndex(settings.channel);
        if(range) { range->setCurrentText(settings.values["range"].toString()); colorize->setChecked(settings.values["colorize"].toBool()); invert->setChecked(settings.values["invertRange"].toBool()); }
        if(reversed) reversed->setChecked(settings.values["reversed"].toBool());
        if(seed) seed->setValue(settings.seed);
        if(curve) { QStringList lines; for(auto p:settings.curve) lines.append(QString::number(p.x())+", "+QString::number(p.y())); curve->setPlainText(lines.join('\n')); }
        loading=false;
    };
    auto collect=[&] {
        auto next=settings;
        for(auto i=fields.begin();i!=fields.end();++i) next.values[i.key()]=i.value()->value();
        if(colorize) { next.values["colorize"]=colorize->isChecked(); next.values["invertRange"]=invert->isChecked(); }
        if(reversed) next.values["reversed"]=reversed->isChecked();
        if(seed) next.seed=quint32(seed->value());
        if(curve) {
            next.curve.clear();
            for(auto line:curve->toPlainText().split('\n',Qt::SkipEmptyParts)) {
                auto values=line.split(','); bool xok=false,yok=false;
                double x=values.value(0).trimmed().toDouble(&xok),y=values.value(1).trimmed().toDouble(&yok);
                if(values.size()!=2 || !xok || !yok) throw std::runtime_error("Enter one input, output pair per line.");
                next.curve.append({x,y});
            }
        }
        auto result=Arc::adjustmentFromFilter(next,draft); settings=next; draft=result;
    };
    bool valid=false;
    auto update=[&] {
        try {
            collect(); working.layers[working.active].adjustment=draft; Arc::validate(working);
            canvas->setDocument(preview.isChecked() ? working : original); error.clear(); valid=true;
        } catch(const std::exception &e) { error.setText(QString::fromUtf8(e.what())); valid=false; canvas->setDocument(original); }
        buttons.button(QDialogButtonBox::Ok)->setEnabled(valid);
    };
    connect(&timer,&QTimer::timeout,&dialog,update);
    for(auto *spin:fields) connect(spin,&QDoubleSpinBox::valueChanged,&dialog,schedule);
    if(curve) connect(curve,&QPlainTextEdit::textChanged,&dialog,schedule);
    for(auto *box:{colorize,invert,reversed,&preview}) if(box) connect(box,&QCheckBox::toggled,&dialog,schedule);
    if(seed) connect(seed,&QDoubleSpinBox::valueChanged,&dialog,schedule);
    auto switchSettings=[&] {
        if(loading) return;
        try {
            collect(); settings=Arc::filterFromAdjustment(draft,channel ? channel->currentIndex() : -1,range ? range->currentText() : QString());
            load(); schedule();
        } catch(const std::exception &e) {
            error.setText(QString::fromUtf8(e.what()));
            if(channel) { QSignalBlocker block(channel); channel->setCurrentIndex(settings.channel); }
            if(range) { QSignalBlocker block(range); range->setCurrentText(settings.values["range"].toString()); }
        }
    };
    if(channel) connect(channel,&QComboBox::currentIndexChanged,&dialog,switchSettings);
    if(range) connect(range,&QComboBox::currentIndexChanged,&dialog,switchSettings);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    load(); timer.start(0); int accepted=dialog.exec(); timer.stop();
    if(accepted==QDialog::Accepted) update();
    canvas->setDocument(original);
    if(accepted==QDialog::Accepted && valid) edit((creating ? "Add " : "Edit ")+kind+" adjustment",[&](auto &d) { d=working; });
}
