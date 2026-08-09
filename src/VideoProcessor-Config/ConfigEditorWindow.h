#pragma once

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>

class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QToolButton;

namespace ConfigEditorCore { struct ConfigDocument; }

class ConfigEditorWindow final : public QMainWindow
{
public:
    explicit ConfigEditorWindow(QString configPath, quintptr ownerHandle = 0,
        bool testMode = false);
    ~ConfigEditorWindow() override;
    void selectPage(int index);
    void reveal();
    void refreshMonitorDiscovery();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QWidget* createShell();
    QWidget* createStartupPage();
    QWidget* createQueuePage();
    QWidget* createRendererPage();
    QWidget* createDirectShowPage();
    QWidget* createViewportPage();
    QWidget* createLldvPage();
    QWidget* createShadersPage();
    QWidget* createActionsPage();
    QWidget* createShortcutsPage();
    QWidget* createLogsPage();
    QWidget* createProfilePage(const QString& title, const QString& description,
        const QString& sectionPrefix);
    QWidget* createCard(const QString& title, const QString& description, QWidget* content);
    QWidget* createPage(const QString& title, const QString& description, QWidget* body);
    QPushButton* addNavigationButton(const QString& text, int pageIndex);
    QString value(const QString& section, const QString& key, const QString& fallback = {}) const;
    QStringList profileSections(const QString& root) const;
    QLineEdit* bindTextField(const QString& section, const QString& key, const QString& fallback = {});
    QComboBox* bindChoiceField(const QString& section, const QString& key,
        const QStringList& values, const QStringList& labels = {}, bool editable = false);
    QCheckBox* bindCheckField(const QString& label, const QString& section, const QString& key, bool defaultValue = false);
    void markDirty();
    void keepAboveVideo();
    void applyChanges();
    void saveChanges();
    bool notifyVideoProcessor();
    void loadConfiguration();
    void loadDiscoveryCache();
    void setupTray();
    void exitApplication();
    void setStatus(const QString& message, bool error = false);

    QString configPath_;
    quintptr ownerHandle_ = 0;
    bool ownerApplied_ = false;
    bool exitRequested_ = false;
    bool configurationLoaded_ = false;
    bool dirty_ = false;
    bool hasPendingMigrations_ = false;
    bool testMode_ = false;
    std::unique_ptr<ConfigEditorCore::ConfigDocument> document_;
    QStringList captureDevices_;
    QMap<QString, QStringList> captureConnections_;
    QStringList filteredRenderers_;
    QStringList allRenderers_;
    QStringList monitors_;
    QStackedWidget* pages_ = nullptr;
    QWidget* navigation_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* monitorChoice_ = nullptr;
    QComboBox* actionRendererTarget_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
};
