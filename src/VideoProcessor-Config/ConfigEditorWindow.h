#pragma once

#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QLabel;
class QCheckBox;
class QComboBox;
class QHideEvent;
class QLineEdit;
class QListWidget;
class QPushButton;
class QProgressBar;
class QRect;
class QStackedWidget;
class QSystemTrayIcon;
class QThread;
class QTimer;
class QToolButton;
class QWindow;
class QWinEventNotifier;
class QFormLayout;

namespace ConfigEditorCore { struct ConfigDocument; }
namespace ConfigEditorPlacement
{
    QRect ClampFrameToWorkArea(const QRect& frame, const QRect& workArea);
}

class ConfigEditorWindow final : public QMainWindow
{
public:
    explicit ConfigEditorWindow(QString configPath, quintptr ownerHandle = 0,
        bool testMode = false, const QStringList& testFilteredRenderers = {},
        const QStringList& testAllRenderers = {});
    ~ConfigEditorWindow() override;
    void selectPage(int index);
    void reveal();
    void refreshMonitorDiscovery();
    void setActiveProfileStatusForTesting(const QString& queue,
        const QString& renderer, const QString& viewport,
        const QStringList& shaders, bool shaderAvailable = true);
    void setRendererDiscoveryForTesting(const QStringList& allRenderers,
        const QStringList& filteredRenderers);

protected:
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message,
        qintptr* result) override;

private:
    QWidget* createShell();
    QWidget* createStartupPage();
    QWidget* createQueuePage();
    QWidget* createRendererPage();
    QWidget* createOutputPage();
    QWidget* createDirectShowPage();
    QWidget* createInputProcessingPage(const QString& title, const QString& description,
        const QString& section);
    QWidget* createViewportPage();
    QWidget* createLldvPage();
    QWidget* createStandardShadersPage();
    QWidget* createNlsShadersPage();
    QWidget* createShadersSetupPage();
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
	void updateEffectSummary();
    void prepareRendererPopup();
    bool validateCandidate(std::wstring& error,
        bool allowActionDrafts = false) const;
    QStringList validationErrors(QStringList& fields,
        bool allowActionDrafts, bool includeCoreValidation = true) const;
    QString displayWarning() const;
    bool updateValidationState();
    void applyNativeOwner();
    void clearNativeOwner();
    void publishNativeAssociation();
    void positionForReveal();
    void applyScopedTopmost();
    void removeScopedTopmost();
    bool hasActiveOwnedPopup() const;
    bool nativeOwnerIsValid() const;
    void applyChanges();
	bool saveChanges();
	void rebuildConfigurationShell();
    void applyRendererVisibilityFilter(bool hideLegacyRenderers);
    bool notifyVideoProcessor();
    void requestShaderPreparation();
    void refreshShaderPreparationStatus();
    void setShaderPreparationBusy(bool busy, const QString& message = {});
    void loadConfiguration();
    void migrateSharedRefreshRate();
    void loadDiscoveryCache();
    void applyMonitorDiscovery(const QStringList& discovered);
    void setupTray();
    void exitApplication();
    void setStatus(const QString& message, bool error = false);
    void setWarningStatus(const QString& message);
    void refreshActiveProfileIndicators();
    void applyActiveProfileIndicators(bool available, const QString& queue,
        const QString& renderer, const QString& viewport,
        const QStringList& shaders, bool shaderAvailable);

    QString configPath_;
    quintptr ownerHandle_ = 0;
    quint32 ownerProcessId_ = 0;
    bool ownerApplied_ = false;
    quintptr presentationTargetHandle_ = 0;
    quint32 presentationTargetProcessId_ = 0;
    bool pendingTopmostReassert_ = false;
    bool scopedTopmost_ = false;
    bool explicitRevealIntent_ = false;
    bool scopedTopmostEligible_ = false;
    bool popupActivationTransition_ = false;
    bool exitRequested_ = false;
    bool configurationLoaded_ = false;
    bool dirty_ = false;
    bool validationValid_ = true;
    bool hasPendingMigrations_ = false;
    bool testMode_ = false;
    std::unique_ptr<ConfigEditorCore::ConfigDocument> document_;
	quintptr publishedWindowHandle_ = 0;
	std::map<std::string, std::map<std::string, std::string>> savedSnapshot_;
    QStringList captureDevices_;
    QMap<QString, QStringList> captureConnections_;
    QMap<QString, quint64> editOrder_;
    quint64 editSerial_ = 0;
    QStringList filteredRenderers_;
    QStringList allRenderers_;
    QStringList monitors_;
    QStackedWidget* pages_ = nullptr;
    QWidget* navigation_ = nullptr;
    QLabel* status_ = nullptr;
	QLabel* effectSummary_ = nullptr;
    QComboBox* monitorChoice_ = nullptr;
    QComboBox* rendererChoice_ = nullptr;
    QThread* monitorDiscoveryThread_ = nullptr;
    QTimer* activeProfileTimer_ = nullptr;
    QTimer* shaderStatusTimer_ = nullptr;
    QComboBox* actionRendererTarget_ = nullptr;
    QFormLayout* rendererShortcutForm_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QWidget* configurationHost_ = nullptr;
    QLabel* shaderCacheStatus_ = nullptr;
    QLabel* shaderPreparationStatus_ = nullptr;
    QProgressBar* shaderFooterBusy_ = nullptr;
    bool shaderPreparationBusy_ = false;
    QSystemTrayIcon* tray_ = nullptr;
	void* revealEvent_ = nullptr;
	QWinEventNotifier* revealEventNotifier_ = nullptr;
    struct ProfileListBinding { QListWidget* list; QString sectionPrefix; };
    std::vector<ProfileListBinding> activeProfileLists_;
};
