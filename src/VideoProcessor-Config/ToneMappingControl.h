#pragma once
#include <ToneMappingTuning.h>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

// Keeps the existing profile text binding as the single persistence path.
class ToneMappingControl : public QWidget
{
public:
    ToneMappingControl(const ToneMappingTuning::Spec& spec, QLineEdit* edit)
        : spec_(spec), edit_(edit), default_(spec.defaultValue), minimum_(spec.minimum), maximum_(spec.maximum), sliderLow_(spec.minimum), sliderHigh_(spec.maximum)
    {
        setObjectName(edit->objectName() + ".control");
        edit->setProperty("toneMappingControl", QVariant::fromValue<QObject*>(this));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 4, 0, 9);
        layout->setSpacing(4);
        auto* label = new QLabel(QString::fromUtf8(spec.label));
        label->setBuddy(edit);
        layout->addWidget(label);
        auto* line = new QHBoxLayout;
        slider_ = new QSlider(Qt::Horizontal);
        slider_->setObjectName(edit->objectName() + ".slider");
        slider_->setAccessibleName(label->text());
        slider_->setRange(0, 1000000);
        slider_->setSingleStep(1000);
        slider_->setPageStep(10000);
        slider_->setMinimumWidth(120);
        edit->setFixedWidth(96);
        edit->setAlignment(Qt::AlignRight);
        edit->setPlaceholderText(QStringLiteral("Inherit"));
        reset_ = new QToolButton;
        reset_->setObjectName(edit->objectName() + ".reset");
        reset_->setProperty("toneReset", true);
        reset_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        reset_->setFixedSize(26, 26);
        line->addWidget(slider_, 1);
        line->addWidget(edit);
        line->addWidget(reset_);
        layout->addLayout(line);
        range_ = new QLabel;
        range_->setObjectName(edit->objectName() + ".range");
        range_->setProperty("toneHint", true);
        layout->addWidget(range_);
        auto* help = new QLabel(QString::fromUtf8(spec.help));
        help->setProperty("toneHint", true);
        help->setWordWrap(true);
        layout->addWidget(help);
        error_ = new QLabel;
        error_->setObjectName(edit->objectName() + ".error");
        error_->setStyleSheet("color: #f0a39e;");
        error_->setWordWrap(true);
        error_->hide();
        layout->addWidget(error_);
        if (std::string(spec.key) == "percentile") {
            fine_ = new QCheckBox("Fine slider range: 99-100%");
            fine_->setObjectName(edit->objectName() + ".fine");
            fine_->setChecked(true);
            layout->addWidget(fine_);
            connect(fine_, &QCheckBox::toggled, this, [this] { refresh(); });
        }
        connect(reset_, &QToolButton::clicked, this, [this] { restoreDefault(); });
        connect(edit_, &QLineEdit::textChanged, this, [this](const QString& text) {
            automatic_ = text.trimmed().compare("auto", Qt::CaseInsensitive) == 0;
            if (automatic_) displayDefault();
            refresh();
        });
        connect(slider_, &QSlider::valueChanged, this, [this](int position) {
            const double low = sliderMinimum();
            const int start = zeroDetent_ ? 1 : 0;
            double value = zeroDetent_ && position == 0 ? 0. :
                low + (position - start) * (sliderHigh_ - low) / (1000000. - start);
            const double step = sliderStep();
            value = std::round(value / step) * step;
            if (!(zeroDetent_ && position == 0)) value = std::clamp(value, low, sliderHigh_);
            const QString text = number(value);
            if (slider_->isSliderDown()) {
                // Mouse motion must not enter the document/validation path.
                // Keep the number responsive, then publish one edit on release.
                if (edit_->text() != text) {
                    const QSignalBlocker blocker(edit_);
                    edit_->setText(text);
                    automatic_ = false;
                    pendingDrag_ = true;
                }
            } else edit_->setText(text);
        });
        connect(slider_, &QSlider::sliderReleased, this, [this] {
            if (!pendingDrag_) return;
            pendingDrag_ = false;
            edit_->textChanged(edit_->text());
        });
        setDefault(default_);
    }
    static ToneMappingControl* For(QWidget* edit) {
        return dynamic_cast<ToneMappingControl*>(edit->property("toneMappingControl").value<QObject*>());
    }
    void loadValue(const QString& value) {
        automatic_ = value.isEmpty() || value.compare("auto", Qt::CaseInsensitive) == 0;
        const QSignalBlocker blocker(edit_);
        edit_->setText(automatic_ ? number(default_) : value);
        refresh();
    }
    QString configuredValue() const { return automatic_ ? QStringLiteral("AUTO") : edit_->text(); }
    double value() const { return edit_->text().toDouble(); }
    QLineEdit* editor() const { return edit_; }
    void restoreDefault() { edit_->setText(QStringLiteral("AUTO")); }
    void setDefault(double value) {
        default_ = value;
        reset_->setToolTip(QStringLiteral("Reset %1 to %2 (default)").arg(QString::fromUtf8(spec_.label), number(value)));
        reset_->setAccessibleName(reset_->toolTip());
        if (automatic_) displayDefault();
        refresh();
    }
    void setBounds(double minimum, double maximum) {
        minimum_ = sliderLow_ = minimum; maximum_ = sliderHigh_ = maximum;
        refresh();
    }
    void setSliderBounds(double minimum, double maximum, bool zeroDetent = false) {
        if (minimum > maximum) return;
        sliderLow_ = minimum; sliderHigh_ = maximum; zeroDetent_ = zeroDetent;
        refresh();
    }
    void setError(const QString& message) {
        error_->setText(message);
        error_->setVisible(!message.isEmpty());
    }
    void refresh() {
        bool ok = false;
        const double value = edit_->text().toDouble(&ok);
        ok = ok && std::isfinite(value) && value >= minimum_ && value <= maximum_;
        if (fine_ && value < 99 && fine_->isChecked()) {
            const QSignalBlocker blocker(fine_); fine_->setChecked(false);
        }
        QString range = QStringLiteral("%1 - %2 %3").arg(number(minimum_), number(maximum_), QString::fromUtf8(spec_.unit));
        if (fine_ && fine_->isChecked()) range = QStringLiteral("Slider: 99-100% | Entry: 0-100%");
        else if (sliderLow_ != minimum_ || sliderHigh_ != maximum_) {
            range = QStringLiteral("Slider: %1%2 - %3 %4 | Entry: %5 - %6")
                .arg(zeroDetent_ ? QStringLiteral("0 (off), ") : QString(), number(sliderLow_), number(sliderHigh_),
                    QString::fromUtf8(spec_.unit), number(minimum_), number(maximum_));
        }
        range_->setText(range);
        slider_->setToolTip(range_->text());
        const QSignalBlocker blocker(slider_);
        const double span = sliderHigh_ - sliderMinimum();
        slider_->setSingleStep(span > 0 ? static_cast<int>(std::clamp(std::round(sliderStep() / span * 1000000.), 1., 1000000.)) : 1);
        slider_->setPageStep(std::max(1, slider_->singleStep() * 10));
        if (ok) {
            const int start = zeroDetent_ ? 1 : 0;
            slider_->setValue(zeroDetent_ && value == 0 ? 0 : start + static_cast<int>(
                (span > 0 ? std::clamp((value-sliderMinimum())/span, 0., 1.) : 0.) * (1000000 - start)));
        }
        setError(ok || edit_->text().trimmed().isEmpty() ? QString() : QStringLiteral("Enter a number from %1 to %2.").arg(number(minimum_), number(maximum_)));
    }
private:
    static QString number(double value) {
        QString text = QString::number(value, 'f', 6);
        while (text.endsWith('0')) text.chop(1);
        if (text.endsWith('.')) text.chop(1);
        return text;
    }
    double sliderStep() const {
        const std::string key(spec_.key);
        if (key == "smoothing_period") return 1.;
        if (key == "percentile") return .001;
        return .01;
    }
    double sliderMinimum() const { return fine_ && fine_->isChecked() ? 99. : sliderLow_; }
    void displayDefault() { const QSignalBlocker blocker(edit_); edit_->setText(number(default_)); }
    ToneMappingTuning::Spec spec_;
    QLineEdit* edit_;
    QSlider* slider_ = nullptr;
    QToolButton* reset_ = nullptr;
    QLabel* range_ = nullptr;
    QLabel* error_ = nullptr;
    QCheckBox* fine_ = nullptr;
    bool automatic_ = true;
    bool pendingDrag_ = false;
    bool zeroDetent_ = false;
    double default_, minimum_, maximum_, sliderLow_, sliderHigh_;
};
