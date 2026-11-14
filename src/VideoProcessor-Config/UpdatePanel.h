#pragma once
#include <QWidget>
#include <QEvent>
#include <QElapsedTimer>
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
        actions->addStretch(); available->addLayout(actions); setActionAvailable(false); setSettingsAvailable(connected_);
        connect(check_, &QPushButton::clicked, this, [this] { if (connectionLost_) { startHelper(); request("state"); } else request("check"); });
        connect(download_, &QPushButton::clicked, this, [this] { request("download"); });
        connect(skip_, &QPushButton::clicked, this, [this] { request("skip"); });
        connect(install_, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, "Install update", "Install on this computer? Playback will stop if VP is running. Config will close and discard unsaved edits. Running local applications will restart afterward.", QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Ok) request("install");
        });
        connect(mode_, &QComboBox::currentIndexChanged, this, [this] { saveSettings(); });
        connect(channel_, &QComboBox::currentIndexChanged, this, [this] { saveSettings(); });
        connect(restart_, &QCheckBox::toggled, this, [this] { saveSettings(); });
        process_ = new QProcess(this);
        process_->setObjectName("updates.bridgeProcess");
        connect(process_, &QProcess::started, this, [this] {
            sessionReady_ = true;
            if (!pending_.isEmpty()) { const auto command = pending_; pending_ = {}; sendRequest(command); }
        });
        connect(process_, &QProcess::readyReadStandardOutput, this, [this] { consumeOutput(); });
        connect(process_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            consumeOutput();
            const QString bridgeStderr = QString::fromUtf8(process_->readAllStandardError());
            status_->setProperty("updaterBridgeExit", QString("code=%1 status=%2 stderr=%3")
                .arg(process_->exitCode())
                .arg(process_->exitStatus() == QProcess::NormalExit ? "normal" : "crash")
                .arg(bridgeStderr));
            const bool preserveFinalError = finalServerError_;
            const bool partialFinalFrame = !responseBuffer_.isEmpty();
            const bool wasLost = connectionLost_;
            timeout_.stop(); sessionReady_ = false; awaiting_ = false; responseBuffer_.clear();
            bool wasClosing = closingSession_;
            closingSession_ = false;
            if (wasClosing) serviceActive_ = false;
            else if (preserveFinalError)
            {
                connectionLost_ = true; setAvailable(false); check_->setText("Reconnect");
                if (isVisible())
                {
                    if (!wasLost) startHelper();
                    if (!reconnect_.isActive()) reconnect_.start();
                }
            }
            else
            {
                const QString exitReason = QString("Updater bridge exited (code %1, status %2): %3")
                    .arg(process_->exitCode())
                    .arg(process_->exitStatus() == QProcess::NormalExit ? "normal" : "crash")
                    .arg(process_->errorString());
                connectionFailed(exitReason);
                if (partialFinalFrame) status_->setText("The updater exited before completing its response. Reconnecting…");
            }
            finalServerError_ = false;
            if (restartSessionAfterExit_ && isVisible())
            {
                restartSessionAfterExit_ = false;
                QTimer::singleShot(250, this, [this] { startControlSession(); });
            }
        });
        connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) connectionFailed("Updater bridge failed to start: " + process_->errorString());
        });
        timeout_.setSingleShot(true); timeout_.setInterval(8000);
        connect(&timeout_, &QTimer::timeout, this, [this] {
            // The request may already have reached the worker. Drop it and never replay it.
            const QString timeoutState = QString("generation=%1 awaiting=%2 pending=%3 bytesToWrite=%4")
                .arg(sessionGeneration_).arg(awaiting_).arg(pending_.value("command").toString()).arg(process_->bytesToWrite());
            status_->setProperty("updaterTimeoutState", timeoutState);
            pending_ = {}; awaiting_ = false; connectionFailed("Updater request timed out.");
            if (process_->state() != QProcess::NotRunning) process_->kill();
        });
        reconnect_.setSingleShot(true); reconnect_.setInterval(3000);
        connect(&reconnect_, &QTimer::timeout, this, [this] {
            if (isVisible() && connectionLost_)
            {
                restartClock_.invalidate(); startHelper(); reconnect_.start();
            }
        });
        poll_.setInterval(1500); connect(&poll_, &QTimer::timeout, this, [this] { request("state"); });

    }
    ~UpdatePanel() override
    {
        // Closing the bridge stdin sends an explicit detach request to the worker.
        poll_.stop(); reconnect_.stop();
        if (process_->state() != QProcess::NotRunning)
        {
            closingSession_ = true; process_->closeWriteChannel();
            if (!process_->waitForFinished(6500)) { process_->kill(); process_->waitForFinished(1000); }
        }
    }
    void applyState(const QJsonObject& state)
    {
        if (state.contains("error")) {
            if (state.value("connectionError").toBool()) connectionFailed();
            else { settingsPending_ = false; settingsExpected_ = {}; status_->setText(state.value("error").toString()); }
            return;
        }
        connectionLost_ = false; check_->setText("Check now");
        const bool preferencesSaved = state.value("status").toString().contains("preferences saved", Qt::CaseInsensitive);
        const bool matchesPendingSettings = preferencesSaved &&
            state.value("mode").toString() == settingsExpected_.value("mode").toString() &&
            state.value("channel").toString() == settingsExpected_.value("channel").toString() &&
            state.value("allowPlaybackRestart").toBool() == settingsExpected_.value("allowPlaybackRestart").toBool();
        if (settingsPending_ && matchesPendingSettings) { settingsPending_ = false; settingsExpected_ = {}; }
        if (!settingsPending_)
        {
            QSignalBlocker modeBlock(mode_), channelBlock(channel_), restartBlock(restart_);
            mode_->setCurrentIndex(mode_->findData(state.value("mode").toString()));
            channel_->setCurrentIndex(channel_->findData(state.value("channel").toString()));
            restart_->setChecked(state.value("allowPlaybackRestart").toBool()); restart_->setVisible(mode_->currentData() == "auto");
        }
        identity_->setText(QString("%1 · %2\n%3").arg(state.value("flavor").toString() == "config" ? "VideoProcessor Config only" : "VideoProcessor and Config", state.value("version").toString(), root_));
        status_->setText(settingsPending_ && preferencesSaved ? "Saving update preferences…" : state.value("status").toString());
        const auto text = state.value("notes").toString(); if (notes_->toPlainText() != text) notes_->setPlainText(text);
        notes_->setVisible(!text.isEmpty());
        progress_->setValue(state.value("progress").toInt());
        progress_->setVisible(state.value("busy").toBool() && progress_->value() > 0);
        setActionAvailable(!state.value("busy").toBool() && !settingsPending_);
        setSettingsAvailable(!state.value("busy").toBool());
        download_->setEnabled(state.value("canDownload").toBool()); install_->setEnabled(state.value("canInstall").toBool()); skip_->setEnabled(state.value("canSkip").toBool());
    }
protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        if (!connected_) return;
        if (!QFileInfo::exists(helper_)) { setAvailable(false); status_->setText("This copy does not include the updater. Install a current full or Config-only package."); return; }
        reconnect_.stop(); startHelper(); startControlSession(); poll_.start();
        QTimer::singleShot(500, this, [this] { if (isVisible()) request("state"); });
    }
    void hideEvent(QHideEvent* event) override
    {
        poll_.stop(); reconnect_.stop(); restartClock_.invalidate();
        closeSessionInput();
        QWidget::hideEvent(event);
    }

private:
    void startHelper()
    {
        if (!connected_ || !QFileInfo::exists(helper_)) return;
        if (restartClock_.isValid() && restartClock_.elapsed() < 1000) return;
        restartClock_.start();
        serviceActive_ = QProcess::startDetached(helper_, {"--root", root_, "--service"}, root_);
    }
    void startControlSession()
    {
        if (!connected_ || !QFileInfo::exists(helper_)) return;
        if (process_->state() != QProcess::NotRunning)
        {
            if (closingSession_) restartSessionAfterExit_ = true;
            return;
        }
        closingSession_ = false; sessionReady_ = false; awaiting_ = false;
        responseBuffer_.clear(); finalServerError_ = false; ++sessionGeneration_;
        process_->start(helper_, {"--root", root_, "--control-session"});
    }
    void closeSessionInput()
    {
        if (process_->state() == QProcess::NotRunning || closingSession_) return;
        closingSession_ = true;
        const int generation = sessionGeneration_;
        process_->closeWriteChannel();
        QTimer::singleShot(8000, this, [this, generation] {
            if (closingSession_ && generation == sessionGeneration_ && process_->state() != QProcess::NotRunning) process_->kill();
        });
    }
    void connectionFailed(const QString& reason = {})
    {
        const bool wasLost = connectionLost_;
        status_->setProperty("updaterLastFailure", reason);
        connectionLost_ = true; pending_ = {}; // Never replay a possibly completed install or preference change.
        settingsPending_ = false; settingsExpected_ = {};
        status_->setText(reason.isEmpty() ? "Connection to the updater was interrupted. Reconnecting…" :
            "Connection to the updater was interrupted. " + reason + " Reconnecting…");
        setAvailable(false); check_->setText("Reconnect"); check_->setEnabled(true);
        if (isVisible())
        {
            if (!wasLost) startHelper();
            if (!reconnect_.isActive()) reconnect_.start();
        }
    }
    void consumeOutput()
    {
        responseBuffer_ += process_->readAllStandardOutput();
        int newline = -1;
        while ((newline = responseBuffer_.indexOf('\n')) >= 0)
        {
            const auto line = responseBuffer_.left(newline).trimmed();
            responseBuffer_.remove(0, newline + 1);
            status_->setProperty("updaterLastFrame", QString::fromUtf8(line));
            if (line.size() > 1024 * 1024) { connectionFailed(); process_->kill(); return; }
            const auto reply = QJsonDocument::fromJson(line);
            if (!reply.isObject()) { connectionFailed(); continue; }
            const auto object = reply.object();
            finalServerError_ = object.contains("error") && !object.value("connectionError").toBool();
            timeout_.stop(); awaiting_ = false;
            if (object.value("connectionError").toBool())
            {
                connectionFailed(object.value("error").toString());
                continue;
            }
            applyState(object);
            if (!pending_.isEmpty()) { const auto command = pending_; pending_ = {}; sendRequest(command); }
        }
        if (responseBuffer_.size() > 1024 * 1024) { connectionFailed(); process_->kill(); }
    }
    void setAvailable(bool available) { for (auto* button : {check_, download_, install_, skip_}) button->setEnabled(available); mode_->setEnabled(available); channel_->setEnabled(available); restart_->setEnabled(available); }
    void setActionAvailable(bool available) { for (auto* button : {check_, download_, install_, skip_}) button->setEnabled(available); }
    void setSettingsAvailable(bool available) { mode_->setEnabled(available); channel_->setEnabled(available); restart_->setEnabled(available); }
    void request(const QString& command, QJsonObject object = {})
    {
        if (!connected_) return;
        object["command"] = command;
        const bool settingChange = command == "settings";
        if (closingSession_)
        {
            if (command != "state") { pending_ = object; if (settingChange) setActionAvailable(false); else setAvailable(false); }
            restartSessionAfterExit_ = true;
            return;
        }
        if (process_->state() == QProcess::NotRunning)
        {
            pending_ = object;
            if (settingChange) status_->setProperty("updaterLastPendingSettings", QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
            if (settingChange) setActionAvailable(false); else if (command != "state") setAvailable(false);
            startControlSession();
            return;
        }
        if (!sessionReady_)
        {
            if (command != "state") { pending_ = object; if (settingChange) status_->setProperty("updaterLastPendingSettings", QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))); if (settingChange) setActionAvailable(false); else setAvailable(false); }
            return;
        }
        if (awaiting_)
        {
            if (command != "state") { pending_ = object; if (settingChange) status_->setProperty("updaterLastPendingSettings", QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))); if (settingChange) setActionAvailable(false); else setAvailable(false); }
            return;
        }
        sendRequest(object);
    }
    void sendRequest(const QJsonObject& object)
    {
        if (!sessionReady_ || process_->state() == QProcess::NotRunning) return;
        QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (bytes.size() > 8192) { status_->setText("Update request is too large."); return; }
        status_->setProperty("updaterLastSent", QString::fromUtf8(bytes));
        bytes.append('\n');
        awaiting_ = true;
        if (object.value("command").toString() == "settings") setActionAvailable(false);
        else if (object.value("command").toString() != "state") setAvailable(false);
        if (process_->write(bytes) != bytes.size())
        {
            awaiting_ = false; pending_ = {}; connectionFailed(); return;
        }
        status_->setProperty("updaterBytesToWriteAfterSend", process_->bytesToWrite());
        timeout_.start();
    }
    void saveSettings()
    {
        settingsExpected_ = {{"mode", mode_->currentData().toString()}, {"channel", channel_->currentData().toString()},
            {"allowPlaybackRestart", restart_->isChecked()}};
        status_->setProperty("updaterLastSettingsRequested", QString::fromUtf8(QJsonDocument(settingsExpected_).toJson(QJsonDocument::Compact)));
        settingsPending_ = true;
        status_->setText("Saving update preferences…");
        request("settings", settingsExpected_);
    }
    QString root_, helper_;
    bool connected_;
    bool serviceActive_ = false;
    bool connectionLost_ = false;
    bool sessionReady_ = false;
    bool awaiting_ = false;
    bool closingSession_ = false;
    bool restartSessionAfterExit_ = false;
    bool finalServerError_ = false;
    bool settingsPending_ = false;
    int sessionGeneration_ = 0;
    QByteArray responseBuffer_;
    QElapsedTimer restartClock_;
    QJsonObject pending_;
    QJsonObject settingsExpected_;
    QLabel *identity_, *status_;
    QComboBox *mode_, *channel_;
    QCheckBox* restart_;
    QPlainTextEdit* notes_;
    QProgressBar* progress_;
    QPushButton *check_, *download_, *install_, *skip_;
    QProcess* process_;
    QTimer poll_, timeout_, reconnect_;
};
