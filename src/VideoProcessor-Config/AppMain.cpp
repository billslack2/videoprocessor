#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <UpdateLauncher.h>

#include "ConfigEditorWindow.h"
#include "Resource.h"
#include "VpTheme.h"
#include <ConfigurationRpcProtocol.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QSet>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QWinEventNotifier>

#include <functional>
#include <cwctype>
#include <cstdint>
#include <utility>

namespace
{
struct FoundTarget
{
	QString instanceId;
	QString label;
	QString address;
	quint16 port = 41686;
	bool local = false;
	QString vpVersion;
};

class LanTargetWatcher final : public QObject
{
	public:
	explicit LanTargetWatcher(QObject* parent = nullptr) : QObject(parent)
	{
		settle_.setSingleShot(true);
		connect(&socket_, &QUdpSocket::readyRead, this,
			[this] { readReplies(); });
		connect(&settle_, &QTimer::timeout, this, [this]
		{
			scanning_ = false;
			const bool stillLooking = results_ && results_(found_);
			if (rescanRequested_)
			{
				rescanRequested_ = false;
				QTimer::singleShot(0, this, [this] { scan(); });
			}
			else periodic_.start(stillLooking ? 1500 : 15000);
		});
		periodic_.setSingleShot(true);
		connect(&periodic_, &QTimer::timeout, this, [this] { scan(); });
	}
	void setResultsHandler(std::function<bool(const QList<FoundTarget>&)> handler)
	{
		results_ = std::move(handler);
	}
	void scan()
	{
		using namespace ConfigurationRpcProtocol;
		if (scanning_)
		{
			rescanRequested_ = true;
			return;
		}
		periodic_.stop();
		if (socket_.state() != QAbstractSocket::BoundState &&
			!socket_.bind(QHostAddress::AnyIPv4, 0))
		{
			const bool stillLooking = results_ && results_({});
			periodic_.start(stillLooking ? 1500 : 15000);
			return;
		}
		Frame query;
		query.operation = static_cast<uint16_t>(Operation::DiscoveryQuery);
		std::vector<uint8_t> bytes;
		if (!Encode(query, bytes)) return;
		found_.clear();
		scanning_ = true;
		QSet<QHostAddress> broadcasts;
		broadcasts.insert(QHostAddress::Broadcast);
		broadcasts.insert(QHostAddress::LocalHost);
		for (const auto& adapter : QNetworkInterface::allInterfaces())
		{
			if (!(adapter.flags() & QNetworkInterface::IsUp) ||
				!(adapter.flags() & QNetworkInterface::CanBroadcast)) continue;
			for (const auto& address : adapter.addressEntries())
				if (address.ip().protocol() == QAbstractSocket::IPv4Protocol &&
					!address.broadcast().isNull())
					broadcasts.insert(address.broadcast());
		}
		for (const auto& broadcast : broadcasts)
			socket_.writeDatagram(reinterpret_cast<const char*>(bytes.data()),
				static_cast<qint64>(bytes.size()), broadcast, 41687);
		settle_.start(600);
	}
	private:
	void readReplies()
	{
		using namespace ConfigurationRpcProtocol;
		while (socket_.hasPendingDatagrams())
		{
			QByteArray datagram;
			datagram.resize(static_cast<qsizetype>(
				socket_.pendingDatagramSize()));
			QHostAddress sender;
			const qint64 received = socket_.readDatagram(datagram.data(),
				datagram.size(), &sender);
			Frame reply;
			DiscoveryAdvertisement identity;
			if (!scanning_ || received <= 0 || !Decode(
				reinterpret_cast<const uint8_t*>(datagram.constData()),
				static_cast<size_t>(received), reply) ||
				!ParseDiscoveryReply(reply, identity)) continue;
			const QString id = QString::fromUtf8(identity.instanceId);
			const bool local = sender.isLoopback();
			const QString version = QString::fromUtf8(identity.vpVersion);
			FoundTarget candidate{ id,
				QStringLiteral("%1 (%2) · %3").arg(
					local ? QStringLiteral("This computer") :
						QString::fromUtf8(identity.computerName),
					sender.toString(), version),
				sender.toString(), identity.rpcPort, local, version };
			bool duplicate = false;
			for (auto& target : found_)
			{
				if (target.instanceId != id) continue;
				if (local && !target.local) target = candidate;
				duplicate = true;
				break;
			}
			if (!duplicate && found_.size() < 10)
				found_.push_back(candidate);
			// Loopback identifies the VP on this computer. Offer it as soon as
			// it answers instead of waiting for the LAN collection interval.
			if (local && results_) results_(found_);
		}
	}
	QUdpSocket socket_;
	QTimer settle_;
	QTimer periodic_;
	QList<FoundTarget> found_;
	std::function<bool(const QList<FoundTarget>&)> results_;
	bool scanning_ = false;
	bool rescanRequested_ = false;
};

bool parseTargetAddress(const QString& value, QString& host, quint16& port)
{
	const QString trimmed = value.trimmed();
	const int colon = trimmed.lastIndexOf(u':');
	port = 41686;
	host = trimmed;
	if (colon > 0)
	{
		bool valid = false;
		const uint parsed = trimmed.mid(colon + 1).toUInt(&valid);
		if (!valid || parsed == 0 || parsed > 65535) return false;
		host = trimmed.left(colon);
		port = static_cast<quint16>(parsed);
	}
	return !host.isEmpty();
}

QString defaultConfigPath()
{
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("VideoProcessor.cfg"));
    if (QFileInfo::exists(beside)) return QFileInfo(beside).absoluteFilePath();

    // Release packages keep the Qt application and its runtime private under
    // config\ while the operator configuration remains beside
    // VideoProcessor.exe in the parent installation directory.
    QDir installation(QCoreApplication::applicationDirPath());
    if (installation.dirName().compare(QStringLiteral("config"),
        Qt::CaseInsensitive) == 0 && installation.cdUp())
    {
        const QString installed = installation.filePath(
            QStringLiteral("VideoProcessor.cfg"));
        if (QFileInfo::exists(installed) || QFileInfo::exists(
            installation.filePath(QStringLiteral("VideoProcessor.exe"))))
            return QFileInfo(installed).absoluteFilePath();
    }

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

QString localVpInstanceId(const QString& configPath)
{
	// Match VP's stable discovery identity so a saved loopback target from
	// earlier Config releases becomes the local file choice, even with VP off.
	wchar_t computerName[256] = {};
	DWORD length = ARRAYSIZE(computerName);
	if (!GetComputerNameW(computerName, &length)) return {};
	QString directory = QDir::toNativeSeparators(
		QFileInfo(configPath).absolutePath());
	if (!directory.endsWith(u'\\')) directory += u'\\';
	std::wstring source(computerName, length);
	source += L'|';
	source += directory.toStdWString();
	uint64_t hash = 1469598103934665603ull;
	for (wchar_t character : source)
	{
		hash ^= static_cast<uint16_t>(std::towlower(character));
		hash *= 1099511628211ull;
	}
	return QString::number(hash, 16);
}

quintptr parseOwner(const QString& value)
{
    bool ok = false;
    const quintptr parsed = static_cast<quintptr>(value.toULongLong(&ok, 0));
    return ok ? parsed : 0;
}

constexpr wchar_t ActivationMessageName[] =
    L"VideoProcessor.ConfigEditor.Activate.v1";

std::wstring installationScopedEventName(const wchar_t* baseName)
{
    const QString installation = QDir::toNativeSeparators(
        QCoreApplication::applicationDirPath()).toCaseFolded();
    const QByteArray identity = QCryptographicHash::hash(
        installation.toUtf8(), QCryptographicHash::Sha1).toHex();
    return std::wstring(baseName) + L"." +
        QString::fromLatin1(identity).toStdWString();
}

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
    if (UpdateLauncher::InstallationPending())
    {
        MessageBoxW(nullptr, L"An update is being installed. Please wait for setup to finish.", L"VideoProcessor updates", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
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
	QString remoteHost;
	QString remoteName;
	quint16 remotePort = 41686;
	bool offline = false;
	bool explicitConfig = false;
    QString screenshotPath;
    int initialPage = 0;
    quintptr owner = 0;
    DWORD ownerProcessId = 0;
    bool startInTray = false;
    bool showUpdates = false;
    QStringList arguments;
    int nativeArgumentCount = 0;
    LPWSTR* nativeArguments = CommandLineToArgvW(GetCommandLineW(), &nativeArgumentCount);
    for (int index = 0; nativeArguments && index < nativeArgumentCount; ++index)
        arguments.push_back(QString::fromWCharArray(nativeArguments[index]));
    if (nativeArguments) LocalFree(nativeArguments);
    for (int index = 1; index < arguments.size(); ++index)
    {
        if (arguments[index] == QStringLiteral("--config") && index + 1 < arguments.size())
		{
			configPath = arguments[++index];
			explicitConfig = true;
		}
		else if (arguments[index] == QStringLiteral("--connect") && index + 1 < arguments.size())
		{
			if (!parseTargetAddress(arguments[++index], remoteHost, remotePort))
			{
				QMessageBox::warning(nullptr, QStringLiteral("Invalid address"),
					QStringLiteral("Use --connect COMPUTER or --connect IPv4[:port]."));
				return 2;
			}
		}
		else if (arguments[index] == QStringLiteral("--offline"))
			offline = true;
		else if (arguments[index] == QStringLiteral("--discover"))
			continue; // Retained for existing VP launch arguments.
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
        else if (arguments[index] == QStringLiteral("--updates"))
            showUpdates = true;
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
	if (remoteHost == QStringLiteral("127.0.0.1"))
		remoteName = QStringLiteral("This computer");
	const bool localAvailable = explicitConfig || offline || QFileInfo::exists(
		QFileInfo(configPath).dir().filePath(QStringLiteral("VideoProcessor.exe")));
	QSettings preferences;
	QString rememberedTarget = preferences.value(
		QStringLiteral("configRpc/selectedInstanceId")).toString();
	QString rememberedLabel = preferences.value(
		QStringLiteral("configRpc/selectedLabel")).toString();
	if (localAvailable && rememberedTarget == localVpInstanceId(configPath))
	{
		rememberedTarget = QStringLiteral("local");
		rememberedLabel = QStringLiteral("This computer");
		preferences.setValue(QStringLiteral("configRpc/selectedInstanceId"),
			rememberedTarget);
		preferences.setValue(QStringLiteral("configRpc/selectedLabel"),
			rememberedLabel);
	}
	if (rememberedLabel.isEmpty() &&
		rememberedTarget.startsWith(QStringLiteral("manual:")))
		rememberedLabel = preferences.value(
			QStringLiteral("configRpc/manualHost")).toString();
	const bool rememberRemote = !rememberedTarget.isEmpty() &&
		rememberedTarget != QStringLiteral("local");
	const bool noTarget = remoteHost.isEmpty() && !offline &&
		(rememberRemote || !localAvailable);

    const std::wstring activationEventName = installationScopedEventName(
		L"Local\\VideoProcessorConfigEditor.Activate.v1");
    HANDLE activationEvent = screenshotPath.isEmpty() ?
        CreateEventW(nullptr, FALSE, FALSE, activationEventName.c_str()) : nullptr;
    const bool activationExists = activationEvent && GetLastError() == ERROR_ALREADY_EXISTS;
    const std::wstring updatesEventName = installationScopedEventName(L"Local\\VideoProcessorConfigEditor.Updates.v1");
    HANDLE updatesEvent = CreateEventW(nullptr, FALSE, FALSE, updatesEventName.c_str());
    const bool existingInstance = activationExists;
    if (existingInstance)
    {
        // VP starts a hidden Config process opportunistically.  If one is
        // already running, leave its current visible/hidden state alone: a
        // background warm-up must never pull focus from the user.
        if (!startInTray)
        {
			const bool acknowledged = remoteHost.isEmpty() &&
				activateExistingWindow(owner, ownerProcessId);
            if (!acknowledged) SetEvent(activationEvent);
        }
        if (showUpdates && updatesEvent) SetEvent(updatesEvent);
        if (updatesEvent) CloseHandle(updatesEvent);
        CloseHandle(activationEvent);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }

    ConfigEditorWindow window(QFileInfo(configPath).absoluteFilePath(), owner,
		false, {}, {}, remoteHost, remotePort, remoteName, noTarget,
		localAvailable, rememberedTarget, rememberedLabel);
	std::unique_ptr<LanTargetWatcher> watcher;
	if (screenshotPath.isEmpty() || !offline)
	{
		watcher = std::make_unique<LanTargetWatcher>();
		LanTargetWatcher* const watcherPtr = watcher.get();
		watcher->setResultsHandler([&window](const QList<FoundTarget>& found)
		{
			QList<ConfigEditorWindow::Target> choices;
			for (const auto& target : found)
				choices.push_back({ target.instanceId, target.label,
					target.address, target.port, target.vpVersion, target.local });
			window.setDiscoveredTargets(choices);
			return window.awaitingTarget();
		});
		window.setTargetRefresh([watcherPtr] { watcherPtr->scan(); });
		QTimer::singleShot(0, watcherPtr, [watcherPtr] { watcherPtr->scan(); });
	}
	if (noTarget)
		QTimer::singleShot(5000, &window,
			[&window] { window.finishInitialTargetSearch(); });
    std::unique_ptr<QWinEventNotifier> activationNotifier;
    if (activationEvent)
    {
        activationNotifier = std::make_unique<QWinEventNotifier>(activationEvent, &window);
        QObject::connect(activationNotifier.get(), &QWinEventNotifier::activated,
            &window, [&window] { window.reveal(); });
    }
    std::unique_ptr<QWinEventNotifier> updatesNotifier;
    if (updatesEvent)
    {
        updatesNotifier = std::make_unique<QWinEventNotifier>(updatesEvent, &window);
        QObject::connect(updatesNotifier.get(), &QWinEventNotifier::activated, &window, [&window] { window.showUpdates(); });
    }
    if (showUpdates) QTimer::singleShot(0, &window, [&window] { window.showUpdates(); });
    // The startup cache remains stable across ordinary reveal/hide cycles.
    // Refresh monitor names only when Qt reports a real topology change.
    QObject::connect(&app, &QGuiApplication::screenAdded, &window,
        [&window](QScreen*) { window.refreshMonitorDiscovery(); });
    QObject::connect(&app, &QGuiApplication::screenRemoved, &window,
        [&window](QScreen*) { window.refreshMonitorDiscovery(); });
    window.selectPage(initialPage);
    if (!startInTray)
        // Enter the Qt event loop before exposing the native window. This
        // lets Qt complete the first style/layout/paint pass rather than
        // briefly showing an empty Win32 frame during startup.
        QTimer::singleShot(0, &window, [&window] { window.reveal(); });
    if (!screenshotPath.isEmpty())
		QTimer::singleShot(watcher ? 2500 : 400, &window,
			[&window, screenshotPath]
        {
            window.grab().save(screenshotPath);
            QCoreApplication::quit();
        });
    const int result = app.exec();
    updatesNotifier.reset();
    if (updatesEvent) CloseHandle(updatesEvent);
    activationNotifier.reset();
    if (activationEvent) CloseHandle(activationEvent);
    if (SUCCEEDED(comResult)) CoUninitialize();
    return result;
}
