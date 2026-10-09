#pragma once

#include <QColorDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

// A compact inline editor with only the reusable upper color-picker controls.
// No dialog, custom-color store, or duplicate numeric/hex value bindings.
class InlineColorEditor final : public QWidget
{
    class Surface final : public QWidget
    {
    public:
        explicit Surface(bool brightness, QWidget* parent) : QWidget(parent), brightness_(brightness)
        {
            setFixedSize(brightness ? QSize(24,160) : QSize(190,160));
            setFocusPolicy(Qt::StrongFocus);
            setAccessibleName(brightness ? tr("Brightness") : tr("Hue and saturation"));
            if(!brightness_) {
                field_=QImage(width(),height(),QImage::Format_RGB32);
                for(int y=0;y<height();++y)
                    for(int x=0;x<width();++x)
                        field_.setPixelColor(x,y,QColor::fromHsv(x*359/(width()-1),255-y*255/(height()-1),255));
            }
        }
        std::function<void(int,int)> changed;
        void setHsv(int h,int s,int v) { hue_=h;saturation_=s;value_=v;update(); }
    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            if(brightness_) {
                QLinearGradient gradient(0,0,0,height()-1);
                gradient.setColorAt(0,QColor::fromHsv(hue_,saturation_,255));
                gradient.setColorAt(1,Qt::black);p.fillRect(rect(),gradient);
            } else p.drawImage(0,0,field_);
            p.setPen(QPen(palette().mid().color()));p.drawRect(rect().adjusted(0,0,-1,-1));
            const int x=hue_*(width()-1)/359;
            const int y=(255-(brightness_?value_:saturation_))*(height()-1)/255;
            p.setPen(QPen(Qt::black,3));
            if(brightness_) p.drawLine(0,y,width()-1,y);else p.drawEllipse(QPoint(x,y),4,4);
            p.setPen(QPen(Qt::white,1));
            if(brightness_) p.drawLine(0,y,width()-1,y);else p.drawEllipse(QPoint(x,y),4,4);
            if(hasFocus()) {p.setPen(QPen(palette().highlight().color(),2));p.drawRect(rect().adjusted(1,1,-2,-2));}
        }
        void mousePressEvent(QMouseEvent* e) override
        {
            if(e->button()==Qt::LeftButton) {setFocus(Qt::MouseFocusReason);pick(e->position().toPoint());e->accept();}
        }
        void mouseMoveEvent(QMouseEvent* e) override
        {
            if(e->buttons()&Qt::LeftButton) {pick(e->position().toPoint());e->accept();}
        }
        void keyPressEvent(QKeyEvent* e) override
        {
            int h=hue_, channel=brightness_?value_:saturation_;
            if(e->key()==Qt::Key_Up) ++channel;
            else if(e->key()==Qt::Key_Down) --channel;
            else if(!brightness_ && e->key()==Qt::Key_Left) --h;
            else if(!brightness_ && e->key()==Qt::Key_Right) ++h;
            else {QWidget::keyPressEvent(e);return;}
            if(changed) changed(std::clamp(h,0,359),std::clamp(channel,0,255));e->accept();
        }
        void focusInEvent(QFocusEvent* e) override {QWidget::focusInEvent(e);update();}
        void focusOutEvent(QFocusEvent* e) override {QWidget::focusOutEvent(e);update();}
    private:
        void pick(QPoint point)
        {
            const int x=std::clamp(point.x(),0,width()-1),y=std::clamp(point.y(),0,height()-1);
            if(changed)changed(brightness_?hue_:x*359/(width()-1),255-y*255/(height()-1));
        }
        bool brightness_;
        int hue_=0,saturation_=0,value_=0;
        QImage field_;
    };
public:
    explicit InlineColorEditor(QWidget* parent=nullptr) : QWidget(parent)
    {
        auto* layout=new QHBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto* basic=new QVBoxLayout;basic->addWidget(new QLabel(tr("Basic colors"),this));
        auto* swatches=new QGridLayout;swatches->setSpacing(3);
        for(int i=0;i<48;++i) {
            const QColor color=QColorDialog::standardColor(i);
            auto* button=new QPushButton(this);button->setFixedSize(21,19);button->setAutoDefault(false);
            button->setObjectName(QStringLiteral("basicColor.%1").arg(i));
            button->setAccessibleName(tr("Basic color %1").arg(color.name()));button->setToolTip(color.name());
            button->setStyleSheet(QStringLiteral("QPushButton {background:%1;border:1px solid #777;border-radius:2px;padding:0;min-width:19px;max-width:19px;min-height:17px;max-height:17px;} QPushButton:focus {border-color:white;}").arg(color.name()));
            connect(button,&QPushButton::clicked,this,[this,color]{setCurrentColor(color);});
            swatches->addWidget(button,i/8,i%8);
        }
        basic->addLayout(swatches);basic->addStretch();layout->addLayout(basic);
        auto* spectrum=new QVBoxLayout;spectrum->addWidget(new QLabel(tr("Hue and saturation"),this));
        hueSaturation_=new Surface(false,this);hueSaturation_->setObjectName(QStringLiteral("hueSaturation"));
        spectrum->addWidget(hueSaturation_);spectrum->addStretch();layout->addLayout(spectrum);
        auto* brightness=new QVBoxLayout;brightness->addWidget(new QLabel(tr("Value"),this));
        brightness_=new Surface(true,this);brightness_->setObjectName(QStringLiteral("brightness"));
        brightness->addWidget(brightness_);brightness->addStretch();layout->addLayout(brightness);
        hueSaturation_->changed=[this](int h,int s) {hue_=h;saturation_=s;publish();};
        brightness_->changed=[this](int,int v) {value_=v;publish();};
        syncSurfaces();
    }
    QColor currentColor() const {return color_;}
    void setCurrentColor(const QColor& color,bool notify=true)
    {
        if(!color.isValid() || color_==color)return;
        color_=color.toRgb();
        if(color_.hsvHue()>=0)hue_=color_.hsvHue();
        saturation_=color_.hsvSaturation();value_=color_.value();syncSurfaces();
        if(notify && currentColorChanged)currentColorChanged(color_);
    }
    std::function<void(const QColor&)> currentColorChanged;
private:
    void syncSurfaces() {hueSaturation_->setHsv(hue_,saturation_,value_);brightness_->setHsv(hue_,saturation_,value_);}
    void publish() {color_=QColor::fromHsv(hue_,saturation_,value_);syncSurfaces();if(currentColorChanged)currentColorChanged(color_);}
    QColor color_=Qt::black;
    int hue_=0,saturation_=0,value_=0;
    Surface* hueSaturation_=nullptr;
    Surface* brightness_=nullptr;
};

// The external six-digit RGB field remains the sole persistence binding.
class InlineColorPicker final : public QWidget
{
public:
    InlineColorPicker(QLineEdit* field,const QString& label,QWidget* parent=nullptr)
        : QWidget(parent),field_(field),label_(label)
    {
        setObjectName(field->objectName()+QStringLiteral(".picker"));
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto* row=new QHBoxLayout;preview_=new QLabel(this);preview_->setFixedSize(44,26);
        preview_->setObjectName(objectName()+QStringLiteral(".preview"));row->addWidget(preview_);
        toggle_=new QPushButton(tr("Choose color"),this);toggle_->setAutoDefault(false);
        toggle_->setObjectName(objectName()+QStringLiteral(".toggle"));
        toggle_->setAccessibleName(label+tr(" — choose color"));toggle_->setCheckable(true);
        row->addWidget(toggle_);row->addStretch();layout->addLayout(row);
        connect(toggle_,&QPushButton::toggled,this,[this,layout](bool open) {
            if(open && !editor_) {
                editor_=new InlineColorEditor(this);editor_->setObjectName(objectName()+QStringLiteral(".editor"));
                editor_->setAccessibleName(label_+tr(" color picker"));layout->addWidget(editor_,0,Qt::AlignLeft);
                sync(field_->text());
                editor_->currentColorChanged=[this](const QColor& color) {field_->setText(color.name(QColor::HexRgb).mid(1).toUpper());};
            }
            if(editor_)editor_->setVisible(open);toggle_->setText(open?tr("Hide color picker"):tr("Choose color"));updateGeometry();
        });
        connect(field,&QLineEdit::textChanged,this,[this](const QString& text){sync(text);});sync(field->text());
    }
private:
    void sync(const QString& text)
    {
        const QColor color(QStringLiteral("#")+text);
        if(text.size()!=6 || !color.isValid())return;
        preview_->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #888;").arg(color.name()));
        preview_->setAccessibleName(label_+QStringLiteral(" #")+text.toUpper());preview_->setToolTip(QStringLiteral("#")+text.toUpper());
        if(editor_)editor_->setCurrentColor(color,false);
    }
    QLineEdit* field_;
    QString label_;
    QLabel* preview_=nullptr;
    QPushButton* toggle_=nullptr;
    InlineColorEditor* editor_=nullptr;
};
