#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

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
#include <QPushButton>
#include <QInputDialog>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QSet>
#include <QSettings>
#include <QScreen>
#include <QTimer>
#include <QWinEventNotifier>

namespace
{
struct FoundTarget
{
	QString instanceId;
	QString label;
	QString address;
	quint16 port = 41686;
	bool local = false;
};

QList<FoundTarget> discoverTargets()
{
	using namespace ConfigurationRpcProtocol;
	QUdpSocket socket;
	if (!socket.bind(QHostAddress::AnyIPv4, 0)) return {};
	Frame query;
	query.operation = static_cast<uint16_t>(Operation::DiscoveryQuery);
	std::vector<uint8_t> bytes;
	if (!Encode(query, bytes)) return {};
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
		socket.writeDatagram(reinterpret_cast<const char*>(bytes.data()),
			static_cast<qint64>(bytes.size()), broadcast, 41687);
	QList<FoundTarget> found;
	QSet<QString> identities;
	QElapsedTimer elapsed;
	elapsed.start();
	while (elapsed.elapsed() < 1800)
	{
		if (!socket.waitForReadyRead(static_cast<int>(1800 - elapsed.elapsed())))
			break;
		while (socket.hasPendingDatagrams())
		{
			QByteArray datagram;
			datagram.resize(static_cast<qsizetype>(socket.pendingDatagramSize()));
			QHostAddress sender;
			const qint64 received = socket.readDatagram(datagram.data(),
				datagram.size(), &sender);
			Frame reply;
			DiscoveryAdvertisement identity;
			if (received <= 0 || !Decode(
				reinterpret_cast<const uint8_t*>(datagram.constData()),
				static_cast<size_t>(received), reply) ||
				!ParseDiscoveryReply(reply, identity)) continue;
			const QString id = QString::fromUtf8(identity.instanceId);
			if (identities.contains(id)) continue;
			identities.insert(id);
			const bool local = sender.isLoopback();
			found.push_back({ id,
				QStringLiteral("%1 (%2) · %3").arg(
					local ? QStringLiteral("LOCAL") :
						QString::fromUtf8(identity.computerName),
					sender.toString(), QString::fromUtf8(identity.vpVersion)),
				sender.toString(), identity.rpcPort, local });
			if (found.size() == 10) return found;
		}
	}
	return found;
}

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

bool chooseTarget(QString& host, quint16& port, QString& name, bool& offline,
	bool forceChoice = false)
{
	QSettings preferences;
	const QString remembered = preferences.value(
		QStringLiteral("configRpc/selectedInstanceId")).toString();
	if (!forceChoice && remembered.startsWith(QStringLiteral("manual:")))
	{
		host = preferences.value(QStringLiteral("configRpc/manualHost")).toString();
		port = static_cast<quint16>(preferences.value(
			QStringLiteral("configRpc/manualPort"), 41686).toUInt());
		name = host;
		if (!host.isEmpty() && port != 0) return true;
	}
	for (;;)
	{
		const auto targets = discoverTargets();
		if (!forceChoice && !remembered.isEmpty())
		{
			for (const auto& target : targets)
				if (target.instanceId == remembered)
				{
					host = target.address;
					port = target.port;
					name = target.local ? QStringLiteral("LOCAL") :
						target.label.section(QStringLiteral(" ("), 0, 0);
					return true;
				}
			QMessageBox::warning(nullptr,
				QStringLiteral("Saved VideoProcessor unavailable"),
				QStringLiteral("The previously selected VideoProcessor is not answering. Choose another target explicitly; no configuration will be sent to a different computer automatically."));
		}
		if (targets.size() == 1 && remembered.isEmpty() && !forceChoice)
		{
			host = targets.front().address;
			port = targets.front().port;
			name = targets.front().local ? QStringLiteral("LOCAL") :
				targets.front().label.section(QStringLiteral(" ("), 0, 0);
			return true;
		}
		if (!targets.isEmpty())
		{
			QStringList labels;
			for (const auto& target : targets) labels.push_back(target.label);
			bool chosen = false;
			const QString selected = QInputDialog::getItem(nullptr,
				QStringLiteral("Choose VideoProcessor"),
				QStringLiteral("VideoProcessor instances found on this LAN:"),
				labels, 0, false, &chosen);
			if (!chosen) return false;
			const int index = labels.indexOf(selected);
			if (index < 0) return false;
			host = targets[index].address;
			port = targets[index].port;
			name = targets[index].local ? QStringLiteral("LOCAL") :
				targets[index].label.section(QStringLiteral(" ("), 0, 0);
			preferences.setValue(QStringLiteral("configRpc/selectedInstanceId"),
				targets[index].instanceId);
			return true;
		}
		QMessageBox prompt;
		prompt.setWindowTitle(QStringLiteral("Find VideoProcessor"));
		prompt.setText(QStringLiteral(
			"No running VideoProcessor answered on this LAN."));
		prompt.setInformativeText(QStringLiteral(
			"Check that VP is running and Windows Firewall allows it, then retry or enter its address."));
		auto* retry = prompt.addButton(QStringLiteral("Retry"), QMessageBox::AcceptRole);
		auto* manual = prompt.addButton(QStringLiteral("Enter address"), QMessageBox::ActionRole);
		auto* local = forceChoice ? nullptr :
			prompt.addButton(QStringLiteral("Work offline"), QMessageBox::ActionRole);
		prompt.addButton(QMessageBox::Cancel);
		prompt.exec();
		if (prompt.clickedButton() == retry) continue;
		if (prompt.clickedButton() == local)
		{
			offline = true;
			preferences.remove(QStringLiteral("configRpc/selectedInstanceId"));
			return true;
		}
		if (prompt.clickedButton() != manual) return false;
		bool entered = false;
		const QString address = QInputDialog::getText(nullptr,
			QStringLiteral("Connect to VideoProcessor"),
			QStringLiteral("Computer name or IPv4 address:"),
			QLineEdit::Normal, {}, &entered);
		if (!entered) return false;
		if (parseTargetAddress(address, host, port))
		{
			name = host;
			preferences.setValue(QStringLiteral("configRpc/selectedInstanceId"),
				QStringLiteral("manual:%1:%2").arg(host).arg(port));
			preferences.setValue(QStringLiteral("configRpc/manualHost"), host);
			preferences.setValue(QStringLiteral("configRpc/manualPort"), port);
			return true;
		}
		QMessageBox::warning(nullptr, QStringLiteral("Invalid address"),
			QStringLiteral("Enter a computer name or IPv4 address, optionally followed by :port."));
	}
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
        if (QFileInfo::exists(installed))
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
	if (owner && remoteHost.isEmpty() && !offline)
	{
		remoteHost = QStringLiteral("127.0.0.1");
		remoteName = QStringLiteral("LOCAL");
	}
	else if (remoteHost.isEmpty() && !offline && !explicitConfig &&
		screenshotPath.isEmpty() &&
		!chooseTarget(remoteHost, remotePort, remoteName, offline))
	{
		if (SUCCEEDED(comResult)) CoUninitialize();
		return 0;
	}

    const std::wstring activationEventName = installationScopedEventName(
		L"Local\\VideoProcessorConfigEditor.Activate.v1") +
		(remoteHost.isEmpty() ? L"" : L"." + remoteHost.toStdWString());
    HANDLE activationEvent = screenshotPath.isEmpty() ?
        CreateEventW(nullptr, FALSE, FALSE, activationEventName.c_str()) : nullptr;
    const bool existingInstance = activationEvent &&
        GetLastError() == ERROR_ALREADY_EXISTS;
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
        CloseHandle(activationEvent);
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 0;
    }

    ConfigEditorWindow window(QFileInfo(configPath).absoluteFilePath(), owner,
		false, {}, {}, remoteHost, remotePort, remoteName);
	window.setTargetSelector([](QString& host, quint16& port, QString& name)
	{
		bool offline = false;
		return chooseTarget(host, port, name, offline, true) && !offline;
	});
    std::unique_ptr<QWinEventNotifier> activationNotifier;
    if (activationEvent)
    {
        activationNotifier = std::make_unique<QWinEventNotifier>(activationEvent, &window);
        QObject::connect(activationNotifier.get(), &QWinEventNotifier::activated,
            &window, [&window] { window.reveal(); });
    }
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
        QTimer::singleShot(400, &window, [&window, screenshotPath]
        {
            window.grab().save(screenshotPath);
            QCoreApplication::quit();
        });
    const int result = app.exec();
    activationNotifier.reset();
    if (activationEvent) CloseHandle(activationEvent);
    if (SUCCEEDED(comResult)) CoUninitialize();
    return result;
}
