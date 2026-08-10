#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include "ConfigEditorWindow.h"
#include "Resource.h"
#include "VpTheme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QTimer>
#include <QWinEventNotifier>

namespace
{
QString defaultConfigPath()
{
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("VideoProcessor.cfg"));
    if (QFileInfo::exists(beside)) return QFileInfo(beside).absoluteFilePath();

    // A development build lives at src/VideoProcessor-Config/x64/<config>.
    // Walk upward instead of depending on that exact depth so direct launches
    // from Visual Studio and future output-layout changes still find the
    // checkout's configuration.
    QDir candidate(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth)
    {
        const QString config = candidate.filePath(QStringLiteral("VideoProcessor.cfg"));
        const QString project = candidate.filePath(
            QStringLiteral("src/VideoProcessor-Config/VideoProcessor-Config.vcxproj"));
        if (QFileInfo::exists(config) && QFileInfo::exists(project))
            return QFileInfo(config).absoluteFilePath();
        if (!candidate.cdUp()) break;
    }
    return beside;
}

quintptr parseOwner(const QString& value)
{
    bool ok = false;
    const quintptr parsed = static_cast<quintptr>(value.toULongLong(&ok, 0));
    return ok ? parsed : 0;
}

constexpr wchar_t ActivationEventName[] =
    L"Local\\VideoProcessorConfigEditor.Activate.v1";
constexpr wchar_t DiscoveryRefreshEventName[] =
    L"Local\\VideoProcessorConfigEditor.RefreshDiscovery.v1";
constexpr wchar_t ActivationMessageName[] =
    L"VideoProcessor.ConfigEditor.Activate.v1";

void activateWindowFromCurrentForeground(HWND window)
{
    if (!window || !IsWindow(window)) return;
    ShowWindowAsync(window, SW_RESTORE);
    // Config is a VP-owned transient, not a global always-on-top window. VP
    // temporarily demotes its exclusive fullscreen surface while Config is
    // visible, and native ownership keeps the two applications ordered.
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window);
}

bool ownerBelongsToProcess(quintptr owner, DWORD expectedProcessId)
{
    const HWND ownerWindow = reinterpret_cast<HWND>(owner);
    if (!ownerWindow || !IsWindow(ownerWindow) || expectedProcessId == 0)
        return false;
    DWORD actualProcessId = 0;
    GetWindowThreadProcessId(ownerWindow, &actualProcessId);
    return actualProcessId == expectedProcessId;
}

bool activateExistingWindow(quintptr owner, DWORD ownerProcessId)
{
    if (HWND existing = FindWindowW(nullptr, L"VideoProcessor Configuration"))
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(existing, &processId);
        if (processId != 0) AllowSetForegroundWindow(processId);
        DWORD_PTR acknowledged = 0;
        const UINT activationMessage = RegisterWindowMessageW(
            ActivationMessageName);
        if (activationMessage && SendMessageTimeoutW(existing,
            activationMessage, static_cast<WPARAM>(ownerProcessId),
            static_cast<LPARAM>(owner), SMTO_ABORTIFHUNG | SMTO_BLOCK,
            1500, &acknowledged) && acknowledged == 1)
            return true;
        activateWindowFromCurrentForeground(existing);
        return false;
    }
    return false;
}

bool highContrastEnabled()
{
    HIGHCONTRASTW settings{};
    settings.cbSize = sizeof(settings);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(settings),
        &settings, 0) != FALSE && (settings.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = __argc;
    QApplication app(argc, __argv);
    QApplication::setApplicationName(QStringLiteral("VideoProcessor Configuration"));
    QApplication::setOrganizationName(QStringLiteral("VideoProcessor"));
    QApplication::setQuitOnLastWindowClosed(false);
    if (!highContrastEnabled())
    {
        QApplication::setStyle(VpTheme::CreateStyle());
        app.setStyleSheet(VpTheme::StyleSheet());
    }

    if (HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_VIDEOPROCESSOR_CONFIG), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
        QApplication::setWindowIcon(QIcon(QPixmap::fromImage(QImage::fromHICON(icon))));

    QString configPath;
    QString screenshotPath;
    int initialPage = 0;
    quintptr owner = 0;
    DWORD ownerProcessId = 0;
    bool startInTray = false;
    QStringList arguments;
    int nativeArgumentCount = 0;
    LPWSTR* nativeArguments = CommandLineToArgvW(GetCommandLineW(), &nativeArgumentCount);
    for (int index = 0; nativeArguments && index < nativeArgumentCount; ++index)
        arguments.push_back(QString::fromWCharArray(nativeArguments[index]));
    if (nativeArguments) LocalFree(nativeArguments);
    for (int index = 1; index < arguments.size(); ++index)
    {
        if (arguments[index] == QStringLiteral("--config") && index + 1 < arguments.size())
            configPath = arguments[++index];
        else if (arguments[index] == QStringLiteral("--owner") && index + 1 < arguments.size())
            owner = parseOwner(arguments[++index]);
        else if (arguments[index] == QStringLiteral("--owner-process") && index + 1 < arguments.size())
        {
            bool processOk = false;
            const qulonglong parsed = arguments[++index].toULongLong(&processOk, 0);
            ownerProcessId = processOk && parsed <= MAXDWORD ?
                static_cast<DWORD>(parsed) : 0;
        }
        else if (arguments[index] == QStringLiteral("--screenshot") && index + 1 < arguments.size())
            screenshotPath = arguments[++index];
        else if (arguments[index] == QStringLiteral("--background"))
            startInTray = true;
        else if (arguments[index] == QStringLiteral("--page") && index + 1 < arguments.size())
        {
            bool pageOk = false;
            const int parsedPage = arguments[++index].toInt(&pageOk);
            if (pageOk) initialPage = parsedPage;
        }
    }
    if (configPath.isEmpty()) configPath = defaultConfigPath();
    if (!ownerBelongsToProcess(owner, ownerProcessId)) owner = 0;

    HANDLE activationEvent = screenshotPath.isEmpty() ?
        CreateEventW(nullptr, FALSE, FALSE, ActivationEventName) : nullptr;
    const bool existingInstance = activationEvent &&
        GetLastError() == ERROR_ALREADY_EXISTS;
    HANDLE discoveryRefreshEvent = screenshotPath.isEmpty() ?
        CreateEventW(nullptr, FALSE, FALSE, DiscoveryRefreshEventName) : nullptr;
    if (existingInstance)
    {
        // VP starts a hidden Config process opportunistically.  If one is
        // already running, leave its current visible/hidden state alone: a
        // background warm-up must never pull focus from the user.
        if (!startInTray)
        {
            const bool acknowledged = activateExistingWindow(
                owner, ownerProcessId);
            if (!acknowledged) SetEvent(activationEvent);
        }
        // VP launches the tray-resident editor opportunistically. Even when it
        // stays hidden, refresh monitor discovery so the next reveal reflects
        // the current Windows topology without discarding the configured name.
        if (discoveryRefreshEvent) SetEvent(discoveryRefreshEvent);
        CloseHandle(activationEvent);
        if (discoveryRefreshEvent) CloseHandle(discoveryRefreshEvent);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }

    ConfigEditorWindow window(QFileInfo(configPath).absoluteFilePath(), owner);
    std::unique_ptr<QWinEventNotifier> activationNotifier;
    std::unique_ptr<QWinEventNotifier> discoveryRefreshNotifier;
    if (activationEvent)
    {
        activationNotifier = std::make_unique<QWinEventNotifier>(activationEvent, &window);
        QObject::connect(activationNotifier.get(), &QWinEventNotifier::activated,
            &window, [&window] { window.reveal(); });
    }
    if (discoveryRefreshEvent)
    {
        discoveryRefreshNotifier = std::make_unique<QWinEventNotifier>(
            discoveryRefreshEvent, &window);
        QObject::connect(discoveryRefreshNotifier.get(),
            &QWinEventNotifier::activated, &window,
            [&window] { window.refreshMonitorDiscovery(); });
    }
    window.selectPage(initialPage);
    if (!startInTray)
        window.show();
    if (!screenshotPath.isEmpty())
        QTimer::singleShot(400, &window, [&window, screenshotPath]
        {
            window.grab().save(screenshotPath);
            QCoreApplication::quit();
        });
    const int result = app.exec();
    discoveryRefreshNotifier.reset();
    activationNotifier.reset();
    if (discoveryRefreshEvent) CloseHandle(discoveryRefreshEvent);
    if (activationEvent) CloseHandle(activationEvent);
    if (SUCCEEDED(comResult)) CoUninitialize();
    return result;
}
