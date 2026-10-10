#pragma once
#include <QWidget>
#include <QEvent>
#include <QFrame>
#include <QFormLayout>
#include <QShowEvent>
#include <QHideEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QProcess>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QSignalBlocker>

// Native Config UI; the helper owns verification and outlives Config during setup.
class UpdatePanel final : public QWidget
{
public:
    explicit UpdatePanel(const QString& root, QWidget* parent = nullptr, bool connectBackend = true)
        : QWidget(parent), root_(root), helper_(QDir(root).filePath("VideoProcessorUpdate.exe")), connected_(connectBackend)
    {
        setObjectName("config.updates");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
        const auto card = [this, layout](const QString& title) {
            auto* frame = new QFrame(this); frame->setProperty("card", true);
            auto* contents = new QVBoxLayout(frame); contents->setContentsMargins(12, 12, 12, 12); contents->setSpacing(10);
            auto* heading = new QLabel(title, frame); heading->setProperty("cardTitle", true); contents->addWidget(heading);
            layout->addWidget(frame); return contents;
        };
        auto* settings = card("Update preferences");
        auto* form = new QFormLayout; form->setSpacing(10);
        mode_ = new QComboBox(this); mode_->setObjectName("updates.mode");
        mode_->addItem("Manual checks only", "manual");
        mode_->addItem("Notify in tray", "notify");
        mode_->addItem("Install automatically", "auto");
        form->addRow("When updates are available", mode_);
        channel_ = new QComboBox(this); channel_->addItem("Stable", "stable"); channel_->addItem("Beta", "beta");
        form->addRow("Release channel", channel_); settings->addLayout(form);
        restart_ = new QCheckBox("Allow automatic updates to stop playback and restart VP", this); restart_->hide(); settings->addWidget(restart_);
        auto* explanation = new QLabel("Preferences save immediately. Automatic installs wait for VP to close unless allowed above. Config closes without saving pending edits and reopens after installation.", this);
        explanation->setWordWrap(true); explanation->setProperty("help", true); settings->addWidget(explanation);
        auto* available = card("This installation");
        identity_ = new QLabel("Reading local installation…", this); identity_->setObjectName("updates.identity");
        identity_->setWordWrap(true); available->addWidget(identity_);
        status_ = new QLabel("Connecting to the update helper…", this); status_->setObjectName("updates.status"); status_->setWordWrap(true); available->addWidget(status_);
        notes_ = new QPlainTextEdit(this); notes_->setReadOnly(true); notes_->setMaximumHeight(110); notes_->hide(); available->addWidget(notes_);
        progress_ = new QProgressBar(this); progress_->setObjectName("updatesProgress"); progress_->setRange(0, 100); progress_->hide(); available->addWidget(progress_);
        auto* actions = new QHBoxLayout;
        check_ = new QPushButton("Check now", this); download_ = new QPushButton("Download update", this);
        install_ = new QPushButton("Install and restart", this); skip_ = new QPushButton("Skip release", this);
        install_->setProperty("primary", true);
        for (auto* button : {check_, download_, install_, skip_}) actions->addWidget(button);
        actions->addStretch(); available->addLayout(actions); setAvailable(false);
        connect(check_, &QPushButton::clicked, this, [this] { request("check"); });
        connect(download_, &QPushButton::clicked, this, [this] { request("download"); });
        connect(skip_, &QPushButton::clicked, this, [this] { request("skip"); });
        connect(install_, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, "Install update", "Install on this computer? Playback will stop if VP is running. Config will close and discard unsaved edits. Running local applications will restart afterward.", QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Ok) request("install");
        });
        connect(mode_, &QComboBox::currentIndexChanged, this, [this] { saveSettings(); });
        connect(channel_, &QComboBox::currentIndexChanged, this, [this] { saveSettings(); });
        connect(restart_, &QCheckBox::toggled, this, [this] { saveSettings(); });
        process_ = new QProcess(this);
        connect(process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            timeout_.stop();
            const auto reply = QJsonDocument::fromJson(process_->readAllStandardOutput().trimmed());
            if (!reply.isObject()) { status_->setText("Update helper unavailable. Retry opening Updates after setup finishes."); setAvailable(false); return; }
            applyState(reply.object());
            if (!pending_.isEmpty())
            {
                const auto command = pending_; pending_ = {};
                QTimer::singleShot(0, this, [this, command] { request(command.value("command").toString(), command); });
            }
        });
        connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { status_->setText("The update helper could not start. Install a current full or Config-only package."); setAvailable(false); });
        timeout_.setSingleShot(true); timeout_.setInterval(8000);
        connect(&timeout_, &QTimer::timeout, this, [this] { process_->kill(); });
        poll_.setInterval(1500); connect(&poll_, &QTimer::timeout, this, [this] { request("state"); });

    }
    ~UpdatePanel() override
    {
        // Only the short-lived control client belongs to this panel. Never stop
        // the detached helper: it must finish installing after Config exits.
        if (process_->state() != QProcess::NotRunning) { process_->kill(); process_->waitForFinished(1000); }
        if (serviceActive_)
            QProcess::startDetached(helper_, {"--root", root_, "--control", "{\"command\":\"detach\"}"}, root_);
    }
    void applyState(const QJsonObject& state)
    {
        if (state.contains("error")) { status_->setText(state.value("error").toString()); return; }
        QSignalBlocker modeBlock(mode_), channelBlock(channel_), restartBlock(restart_);
        mode_->setCurrentIndex(mode_->findData(state.value("mode").toString()));
        channel_->setCurrentIndex(channel_->findData(state.value("channel").toString()));
        restart_->setChecked(state.value("allowPlaybackRestart").toBool()); restart_->setVisible(mode_->currentData() == "auto");
        identity_->setText(QString("%1 · %2\n%3").arg(state.value("flavor").toString() == "config" ? "VideoProcessor Config only" : "VideoProcessor and Config", state.value("version").toString(), root_));
        status_->setText(state.value("status").toString());
        const auto text = state.value("notes").toString(); if (notes_->toPlainText() != text) notes_->setPlainText(text);
        notes_->setVisible(!text.isEmpty());
        progress_->setValue(state.value("progress").toInt());
        progress_->setVisible(state.value("busy").toBool() && progress_->value() > 0);
        setAvailable(!state.value("busy").toBool());
        download_->setEnabled(state.value("canDownload").toBool()); install_->setEnabled(state.value("canInstall").toBool()); skip_->setEnabled(state.value("canSkip").toBool());
    }
protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        if (!connected_) return;
        if (!QFileInfo::exists(helper_)) { status_->setText("This copy does not include the updater. Install a current full or Config-only package."); return; }
        QProcess::startDetached(helper_, {"--root", root_, "--service"}, root_);
        serviceActive_ = true; poll_.start();
        QTimer::singleShot(500, this, [this] { if (isVisible()) request("state"); });
    }
    void hideEvent(QHideEvent* event) override
    {
        poll_.stop();
        if (serviceActive_) QProcess::startDetached(helper_, {"--root", root_, "--control", "{\"command\":\"detach\"}"}, root_);
        serviceActive_ = false; QWidget::hideEvent(event);
    }

private:
    void setAvailable(bool available) { for (auto* button : {check_, download_, install_, skip_}) button->setEnabled(available); mode_->setEnabled(available); channel_->setEnabled(available); restart_->setEnabled(available); }
    void request(const QString& command, QJsonObject object = {})
    {
        if (!connected_) return;
        object["command"] = command;
        if (process_->state() != QProcess::NotRunning)
        {
            if (command != "state") { pending_ = object; setAvailable(false); }
            return;
        }
        if (command != "state") setAvailable(false);
        process_->start(helper_, {"--root", root_, "--control", QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))}); timeout_.start();
    }
    void saveSettings() { request("settings", {{"mode", mode_->currentData().toString()}, {"channel", channel_->currentData().toString()}, {"allowPlaybackRestart", restart_->isChecked()}}); }
    QString root_, helper_;
    bool connected_;
    bool serviceActive_ = false;
    QJsonObject pending_;
    QLabel *identity_, *status_;
    QComboBox *mode_, *channel_;
    QCheckBox* restart_;
    QPlainTextEdit* notes_;
    QProgressBar* progress_;
    QPushButton *check_, *download_, *install_, *skip_;
    QProcess* process_;
    QTimer poll_, timeout_;
};
