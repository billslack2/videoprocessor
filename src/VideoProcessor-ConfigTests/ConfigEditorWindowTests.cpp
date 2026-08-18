#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ConfigEditorWindow.h"
#include "VpTheme.h"
#include <ActiveProfileStatus.h>
#include <ConfigurationLiveApply.h>

#include <QApplication>
#include <QAccessible>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QListView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
QString testNameFilter;
int selectedTestsRun = 0;

WNDPROC testOwnerOriginalProcedure = nullptr;
HWND testAdvertisedEditor = nullptr;
bool testActivateOnAssociation = false;
bool testAssociationActivationAcknowledged = false;

void answerInputDialog(const QString& text);

bool appearsAbove(HWND first, HWND second)
{
    for (HWND window = GetTopWindow(nullptr); window;
        window = GetWindow(window, GW_HWNDNEXT))
    {
        if (window == first) return true;
        if (window == second) return false;
    }
    return false;
}

struct ReverseCallbackFixture
{
    std::atomic<HWND> owner = nullptr;
    std::atomic<HWND> advertisedEditor = nullptr;
    std::atomic<bool> activationReturned = false;
    std::atomic<bool> activationAcknowledged = false;
    std::atomic<ULONGLONG> activationElapsedMs = 0;
};

struct ExternalZOrderFixture
{
    HWND owner = nullptr;
    HWND host = nullptr;
    bool hostBecameForeground = false;
};

LRESULT CALLBACK externalFixtureReceiverProcedure(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam)
{
    auto* fixture = reinterpret_cast<ExternalZOrderFixture*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        fixture = static_cast<ExternalZOrderFixture*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(fixture));
    }
    static const UINT readyMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigTests.ZOrderFixture.Ready.v1");
    static const UINT foregroundMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigTests.ZOrderFixture.Foreground.v1");
    if (fixture && message == readyMessage)
    {
        fixture->owner = reinterpret_cast<HWND>(wParam);
        fixture->host = reinterpret_cast<HWND>(lParam);
        return 1;
    }
    if (fixture && message == foregroundMessage)
    {
        fixture->hostBecameForeground = wParam != 0;
        return 1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

struct ExternalHostContext { HWND receiver = nullptr; };

BOOL CALLBACK collectMonitorRects(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info))
        reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcMonitor);
    return TRUE;
}

LRESULT CALLBACK externalHostProcedure(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam)
{
    auto* context = reinterpret_cast<ExternalHostContext*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        context = static_cast<ExternalHostContext*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(context));
    }
    static const UINT requestMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigTests.ZOrderFixture.RequestForeground.v1");
    if (context && message == requestMessage)
    {
        const bool foregrounded = SetForegroundWindow(window) != FALSE &&
            GetForegroundWindow() == window;
        static const UINT foregroundMessage = RegisterWindowMessageW(
            L"VideoProcessor.ConfigTests.ZOrderFixture.Foreground.v1");
        PostMessageW(context->receiver, foregroundMessage, foregrounded, 0);
        return 1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int runExternalZOrderFixture(HWND receiver)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = externalHostProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"VP.ConfigTests.ExternalHost.v1";
    RegisterClassW(&windowClass);
    ExternalHostContext context{ receiver };
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName,
        L"VP external owner", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200,
        nullptr, nullptr, instance, &context);
    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitorRects,
        reinterpret_cast<LPARAM>(&monitors));
    RECT bounds{};
    if (!monitors.empty()) bounds = monitors[monitors.size() > 1 ? 1 : 0];
    else
    {
        bounds.right = GetSystemMetrics(SM_CXSCREEN);
        bounds.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    HWND host = CreateWindowExW(WS_EX_TOPMOST, windowClass.lpszClassName,
        L"VP external fullscreen host", WS_POPUP,
        bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, nullptr, nullptr, instance, &context);
    if (!owner || !host) return 2;
    ShowWindow(owner, SW_SHOWNOACTIVATE);
    SetWindowPos(owner, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(host, HWND_TOPMOST, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    static const UINT readyMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigTests.ZOrderFixture.Ready.v1");
    PostMessageW(receiver, readyMessage, reinterpret_cast<WPARAM>(owner),
        reinterpret_cast<LPARAM>(host));
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyWindow(host);
    DestroyWindow(owner);
    return 0;
}

LRESULT CALLBACK reverseCallbackOwnerProcedure(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam)
{
    ReverseCallbackFixture* fixture = reinterpret_cast<ReverseCallbackFixture*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        fixture = static_cast<ReverseCallbackFixture*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(fixture));
    }
    static const UINT associationMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.Association.v1");
    if (fixture && message == associationMessage)
    {
        const HWND editor = reinterpret_cast<HWND>(lParam);
        fixture->advertisedEditor.store(editor);
        static const UINT activationMessage = RegisterWindowMessageW(
            L"VideoProcessor.ConfigEditor.Activate.v1");
        DWORD_PTR acknowledged = 0;
        const ULONGLONG start = GetTickCount64();
        const bool delivered = activationMessage && SendMessageTimeoutW(editor,
            activationMessage, wParam, reinterpret_cast<LPARAM>(window),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &acknowledged);
        fixture->activationElapsedMs.store(GetTickCount64() - start);
        fixture->activationAcknowledged.store(delivered && acknowledged == 1);
        fixture->activationReturned.store(true);
        return 1;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK testOwnerProcedure(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam)
{
    static const UINT associationMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.Association.v1");
    if (message == associationMessage)
    {
        const HWND editor = reinterpret_cast<HWND>(lParam);
        DWORD processId = 0;
        if (editor && IsWindow(editor))
            GetWindowThreadProcessId(editor, &processId);
        if (processId == static_cast<DWORD>(wParam))
        {
            testAdvertisedEditor = editor;
            if (testActivateOnAssociation)
            {
                testActivateOnAssociation = false;
                static const UINT activationMessage = RegisterWindowMessageW(
                    L"VideoProcessor.ConfigEditor.Activate.v1");
                DWORD_PTR acknowledged = 0;
                testAssociationActivationAcknowledged = activationMessage &&
                    SendMessageTimeoutW(editor, activationMessage, wParam,
                        reinterpret_cast<LPARAM>(window),
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &acknowledged) &&
                    acknowledged == 1;
            }
            return 1;
        }
        return 0;
    }
    return testOwnerOriginalProcedure ? CallWindowProcW(
        testOwnerOriginalProcedure, window, message, wParam, lParam) :
        DefWindowProcW(window, message, wParam, lParam);
}

QString repositoryPath(const QString& relative)
{
    QString source = QString::fromUtf8(__FILE__);
    source.replace(u'\\', u'/');
    const QString marker = QStringLiteral("/src/VideoProcessor-ConfigTests/ConfigEditorWindowTests.cpp");
    const qsizetype position = source.lastIndexOf(marker);
    if (position < 0) throw std::runtime_error("Cannot locate repository root");
    return source.left(position) + u'/' + relative;
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot read test file");
    return file.readAll();
}

QString copyFixture(QTemporaryDir& directory)
{
    if (!directory.isValid()) throw std::runtime_error("Cannot create temporary directory");
    const QString destination = directory.filePath(QStringLiteral("VideoProcessor.cfg"));
    if (!QFile::copy(repositoryPath(QStringLiteral(
        "test-fixtures/deployed-VideoProcessor-20260807-current.cfg")), destination))
        throw std::runtime_error("Cannot copy deployed configuration fixture");

    // The checked-in fixture follows the repository's Windows line-ending
    // policy. These UI tests inspect precise multi-line snippets, so give each
    // disposable copy a stable line ending without changing the source fixture
    // or weakening the editor's separate preservation tests.
    QByteArray contents = readBytes(destination);
    contents.replace("\r\n", "\n");
    QFile normalized(destination);
    if (!normalized.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        normalized.write(contents) != contents.size())
        throw std::runtime_error("Cannot normalize configuration fixture");
    normalized.close();
    return destination;
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
T* requireControl(QObject& root, const QString& name)
{
    T* control = root.findChild<T*>(name);
    if (!control)
        throw std::runtime_error(QStringLiteral("Missing control: %1").arg(name).toStdString());
    return control;
}

void selectData(QComboBox* combo, const QString& value)
{
    int index = combo->findData(value, Qt::UserRole, Qt::MatchFixedString);
    if (index < 0)
    {
        combo->addItem(value, value);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
    if (combo->isEditable()) combo->setEditText(value);
    QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection,
        Q_ARG(int, index));
    QCoreApplication::processEvents();
}

void save(ConfigEditorWindow& window)
{
    QPushButton* button = requireControl<QPushButton>(window, QStringLiteral("applyConfiguration"));
    require(button->isEnabled(), "Apply button was not enabled after an edit");
    button->click();
    QCoreApplication::processEvents();
    require(!button->isEnabled(), "Apply did not complete successfully");
}

void testEveryPageRoundTrips()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    {
        QFile file(path);
        require(file.open(QIODevice::Append),
            "Cannot add the GLSL-only shader fixture");
        const QByteArray glslOnlyMember =
            "\n[shader.nls.plus]\n"
            "shader_type: nls\n"
            "label: NLS+\n"
            "stage: pre_resize\n"
            "glsl_file: NLSPlus.glsl\n"
            "\n[shader.standard]\n"
            "type: multi\n"
            "\n[shader.standard.debanding_mild]\n"
            "shader_type: custom\n"
            "label: Debanding Mild\n"
            "stage: pre_resize\n"
            "hlsl_file: Debanding mild.hlsl\n"
            "threshold: 32\n";
        require(file.write(glslOnlyMember) == glslOnlyMember.size(),
            "Cannot write the GLSL-only shader fixture");
    }
    ConfigEditorWindow window(path, 0, true);

    QStackedWidget* pages = requireControl<QStackedWidget>(window,
        QStringLiteral("settingsPages"));
    require(pages->count() == 13,
        "Renderer Input and shader child pages were not added as dedicated settings pages");
    for (QPushButton* button : window.findChildren<QPushButton*>())
        require(!button->property("navChild").toBool(),
            "Grouped settings still expose child entries in the left navigation");
    QTabBar* sectionTabs = requireControl<QTabBar>(window,
        QStringLiteral("settingsSectionTabs"));
    require(window.findChildren<QTabBar*>(
        QStringLiteral("settingsSectionTabs")).size() == 1,
        "Grouped settings do not use one persistent tab control");
    require(!sectionTabs->expanding() &&
        sectionTabs->sizePolicy().horizontalPolicy() == QSizePolicy::Maximum,
        "Grouped settings tabs do not use compact Shader tab sizing");
    require(!sectionTabs->drawBase(),
        "Grouped settings tabs still draw the native white tab-bar base");
    const auto parentButton = [&window](const QString& text)
    {
        for (QPushButton* button : window.findChildren<QPushButton*>())
            if (button->property("nav").toBool() && button->text() == text)
                return button;
        return static_cast<QPushButton*>(nullptr);
    };
    const auto requireTabs = [sectionTabs](const QStringList& expected)
    {
        QStringList actual;
        for (int tab = 0; tab < sectionTabs->count(); ++tab)
            actual.push_back(sectionTabs->tabText(tab));
        require(actual == expected, "A grouped settings page has the wrong top tabs");
    };
    QPushButton* shadersNavigation = parentButton(QStringLiteral("Shaders"));
    QPushButton* vpRenderer = parentButton(QStringLiteral("VP Renderer"));
    QPushButton* directShow = parentButton(QStringLiteral("DirectShow"));
    require(shadersNavigation && vpRenderer && directShow,
        "A grouped settings parent is missing from the left navigation");
    shadersNavigation->click();
    requireTabs({ QStringLiteral("Standard"), QStringLiteral("NLS") });
    vpRenderer->click();
    requireTabs({ QStringLiteral("Rendering"), QStringLiteral("Screen Config"),
        QStringLiteral("Input Processing") });
    directShow->click();
    requireTabs({ QStringLiteral("General"), QStringLiteral("Input Processing") });
    QWidget* navigation = requireControl<QWidget>(window, QStringLiteral("sidebar"));
    require(navigation->minimumWidth() >= 172,
        "The settings navigation is too narrow for renderer child labels");
    const QString theme = VpTheme::StyleSheet();
    require(theme.contains(QStringLiteral("QPushButton[nav=\"true\"]:focus")) &&
        theme.contains(QStringLiteral("outline: 0")),
        "Focused settings navigation still uses the generic button focus box");
    require(theme.contains(QStringLiteral("QTabBar::tab:selected")) &&
        theme.contains(QStringLiteral("color: #f2f8fd")),
        "Shader section tabs do not have a visible selected state");
    require(theme.contains(QStringLiteral("min-height: 30px")) &&
        theme.contains(QStringLiteral("padding: 3px 12px")),
        "Settings navigation no longer reserves safe vertical label space");
    require(theme.contains(QStringLiteral("QDialog")) &&
        theme.contains(QStringLiteral("QDialog { background: #0b121b; }")),
        "Qt dialogs do not use the Config window background");
    window.show();
    QCoreApplication::processEvents();
    QPushButton* selectedNavigation = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>())
    {
        if (!button->property("nav").toBool()) continue;
        require(button->height() >= button->fontMetrics().height() + 6,
            "A settings navigation label can be clipped vertically");
        if (button->isChecked()) selectedNavigation = button;
    }
    require(selectedNavigation != nullptr,
        "Settings navigation has no selected item");
    QImage navigationImage(selectedNavigation->size(), QImage::Format_ARGB32);
    navigationImage.fill(Qt::transparent);
    selectedNavigation->render(&navigationImage);
    require(navigationImage.pixelColor(selectedNavigation->width() - 8,
        selectedNavigation->height() / 2) == QColor(QStringLiteral("#13283a")),
        "Settings navigation stylesheet did not render its selected background");

    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.general.capture_device")),
        QStringLiteral("Decklink Test Device"));
    QComboBox* captureInput = requireControl<QComboBox>(window,
        QStringLiteral("config.general.capture_input"));
    selectData(captureInput, QStringLiteral("HDMI"));
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.general.renderer")),
        QStringLiteral("VideoProcessor Renderer (Alpha)"));
    requireControl<QCheckBox>(window, QStringLiteral("config.general.fullscreen"))->setChecked(false);
    requireControl<QCheckBox>(window, QStringLiteral("config.general.startminimized"))->setChecked(false);
    requireControl<QCheckBox>(window, QStringLiteral("config.general.scene_detect"))->setChecked(false);
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.general.fullscreen_monitor_name")),
        QStringLiteral("Test Display"));
    selectData(requireControl<QComboBox>(window, QStringLiteral("config.general.video_conversion")),
        QStringLiteral("V210_TO_P010"));
    selectData(requireControl<QComboBox>(window, QStringLiteral("config.general.container_colorspace")),
        QStringLiteral("BT2020"));

    requireControl<QSpinBox>(window, QStringLiteral("config.queue.queue_size"))->setValue(48);
    requireControl<QSpinBox>(window, QStringLiteral("config.queue.lead_frames"))->setValue(3);
    requireControl<QSpinBox>(window,
        QStringLiteral("config.queue.reset_queue_too_large_percent"))->setValue(200);
    requireControl<QLineEdit>(window, QStringLiteral("config.queue.shortcut"))
        ->setText(QStringLiteral("Ctrl+q"));

    selectData(requireControl<QComboBox>(window, QStringLiteral("config.vprenderer.quality")),
        QStringLiteral("balanced"));
    requireControl<QLineEdit>(window, QStringLiteral("config.vprenderer.sdr_target_nits"))
        ->setText(QStringLiteral("220"));
    selectData(requireControl<QComboBox>(window, QStringLiteral("config.vprenderer.tone_mapping")),
        QStringLiteral("spline"));
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.input_processing.video_conversion")),
        QStringLiteral("V210_TO_P010"));
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.input_processing.hdr_colorspace")),
        QStringLiteral("FOLLOW_INPUT_LLDV"));

    requireControl<QLineEdit>(window,
        QStringLiteral("config.vprenderer.viewport.screen_aspect"))->setText(QStringLiteral("21:10"));
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.viewport.vertical_alignment")),
        QStringLiteral("bottom"));
    requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.viewport.automatic_crop"))->setCheckState(Qt::Checked);
    requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.viewport.anamorphic_enabled"))->setChecked(true);
    requireControl<QLineEdit>(window,
        QStringLiteral("config.vprenderer.viewport.anamorphic_scale"))->setText(QStringLiteral("4:3"));

    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.directshow.renderer_start_stop_time_method")),
        QStringLiteral("RATIONAL_RATIONAL"));
    requireControl<QCheckBox>(window,
        QStringLiteral("config.directshow.frame_offset.auto"))->setChecked(false);
    requireControl<QSpinBox>(window,
        QStringLiteral("config.directshow.frame_offset.value"))->setValue(75);
    QSpinBox* frameOffsetValue = requireControl<QSpinBox>(window,
        QStringLiteral("config.directshow.frame_offset.value"));
    QCheckBox* frameOffsetAuto = requireControl<QCheckBox>(window,
        QStringLiteral("config.directshow.frame_offset.auto"));
    require(frameOffsetValue->parentWidget() == frameOffsetAuto->parentWidget() &&
        frameOffsetValue->parentWidget()->layout()->indexOf(frameOffsetValue) <
        frameOffsetValue->parentWidget()->layout()->indexOf(frameOffsetAuto),
        "Frame offset Auto is not positioned after the numeric entry");
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.directshow.renderer_primaries")), QStringLiteral("BT2020"));
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.directshow.video_conversion")), QStringLiteral("NONE"));
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.directshow.container_colorspace")), QStringLiteral("REC709"));

    requireControl<QLineEdit>(window, QStringLiteral("config.lldv.max_cll"))
        ->setText(QStringLiteral("1200"));
    requireControl<QLineEdit>(window, QStringLiteral("config.lldv.max_fall"))
        ->setText(QStringLiteral("450"));

    requireControl<QLineEdit>(window, QStringLiteral("config.shortcuts.fullscreen_toggle"))
        ->setText(QStringLiteral("Ctrl+f"));
    QLineEdit* configShortcut = requireControl<QLineEdit>(window,
        QStringLiteral("config.shortcuts.config_editor"));
    require(configShortcut->text() == QStringLiteral("Ctrl+Shift+S"),
        "Open configuration did not default to Ctrl+Shift+S");
    configShortcut->setText(QStringLiteral("Ctrl+e"));
    QLineEdit* renderedCaptureShortcut = requireControl<QLineEdit>(window,
        QStringLiteral("config.shortcuts.capture_rendered_output"));
    require(renderedCaptureShortcut->text() == QStringLiteral("Ctrl+Alt+S"),
        "Rendered-output capture did not default to Ctrl+Alt+S");
    renderedCaptureShortcut->setText(QStringLiteral("Ctrl+Alt+C"));
    QLineEdit* noUiShortcut = requireControl<QLineEdit>(window,
        QStringLiteral("config.shortcuts.toggle_noui"));
    require(noUiShortcut->text() == QStringLiteral("Ctrl+Shift+U"),
        "Video-only UI toggle did not default to Ctrl+Shift+U");
    noUiShortcut->setText(QStringLiteral("Alt+u"));

    window.selectPage(10);
    require(requireControl<QCheckBox>(window, QStringLiteral("config.logging.enabled"))->isChecked(),
        "Logging did not default to enabled");
    require(!requireControl<QCheckBox>(window, QStringLiteral("config.logging.debug"))->isChecked(),
        "Enhanced logging did not default to disabled");
    require(requireControl<QSpinBox>(window,
        QStringLiteral("config.logging.debug_log_retention"))->value() == 10,
        "Debug log retention did not default to 10 files");
    requireControl<QSpinBox>(window,
        QStringLiteral("config.logging.debug_log_retention"))->setValue(25);
    requireControl<QCheckBox>(window, QStringLiteral("config.logging.debug"))->setChecked(true);
    require(!requireControl<QSpinBox>(window,
        QStringLiteral("config.logging.debug_log_retention"))->isEnabled(),
        "Log retention remained editable while enhanced logging was enabled");
    requireControl<QCheckBox>(window, QStringLiteral("config.logging.debug"))->setChecked(false);
    requireControl<QCheckBox>(window, QStringLiteral("config.logging.enabled"))->setChecked(false);
    require(!requireControl<QSpinBox>(window,
        QStringLiteral("config.logging.debug_log_retention"))->isEnabled(),
        "Debug log retention remained editable while logging was disabled");

    QListWidget* actions = requireControl<QListWidget>(window,
        QStringLiteral("config.actions.items"));
    require(actions->count() > 0, "Actions fixture did not load");
    actions->setCurrentRow(0);
    requireControl<QLineEdit>(window, QStringLiteral("config.actions.run"))
        ->setText(QStringLiteral("C:\\Tools\\verified-action.cmd 42"));
    selectData(requireControl<QComboBox>(window, QStringLiteral("config.actions.renderer")),
        QStringLiteral("*"));

    window.selectPage(9);
    QListWidget* shaders = requireControl<QListWidget>(window,
        QStringLiteral("config.shader.nls.modes"));
    require(shaders->count() > 1, "NLS fixture did not load");
    shaders->setCurrentRow(1);
    requireControl<QLineEdit>(window, QStringLiteral("config.shader.nls.label"))
        ->setText(QStringLiteral("Verified Stretch"));
    require(window.findChild<QSpinBox*>(QStringLiteral("config.shader.nls.order")) == nullptr,
        "NLS mode order remained exposed as a numeric setting");
    require(window.findChild<QCheckBox*>(QStringLiteral("config.shader.nls.show_parameters")) == nullptr,
        "Custom shader parameters remained a checkbox setting");
    QToolButton* parameterToggle = requireControl<QToolButton>(window,
        QStringLiteral("config.shader.nls.parameters_toggle"));
    require(!parameterToggle->isChecked(), "Custom shader parameters did not start collapsed");
    parameterToggle->click();
    require(parameterToggle->isChecked(), "Custom shader parameter disclosure did not expand");
    QTableWidget* parameters = requireControl<QTableWidget>(window,
        QStringLiteral("config.shader.nls.parameters"));
    require(theme.contains(QStringLiteral("QTableWidget::item:selected")) &&
        theme.contains(QStringLiteral("background: #173c59")),
        "Selected shader parameter rows can fall back to the platform light selection color");
    require(parameters->rowCount() > 0, "Shader parameter fixture did not load");
    const QModelIndex parameterIndex = parameters->model()->index(0, 1);
    parameters->edit(parameterIndex);
    QCoreApplication::processEvents();
    QLineEdit* parameterEditor = parameters->findChild<QLineEdit*>(
        QStringLiteral("config.shader.nls.parameter_value_editor"));
    require(parameterEditor != nullptr, "Shader parameter did not use the compact editor");
    const QRect valueCell = parameters->visualRect(parameterIndex);
    const QRect expectedEditor = valueCell.adjusted(1, 1, -1, -1);
    require(parameterEditor->geometry() == expectedEditor,
        "Shader parameter editor was not aligned to its value cell");
    bool changedParameter = false;
    for (int row = 0; row < parameters->rowCount(); ++row)
        if (parameters->item(row, 0) &&
            parameters->item(row, 0)->text().compare(QStringLiteral("strength"),
                Qt::CaseInsensitive) == 0)
        {
            parameters->item(row, 1)->setText(QStringLiteral("0.85"));
            changedParameter = true;
            break;
        }
    require(changedParameter, "Structured shader parameter did not load");
    requireControl<QPushButton>(window, QStringLiteral("config.shader.nls.move_down"))->click();

    int nlsPlusRow = -1;
    for (int row = 0; row < shaders->count(); ++row)
        if (shaders->item(row)->text() == QStringLiteral("NLS+"))
        {
            nlsPlusRow = row;
            break;
        }
    require(nlsPlusRow >= 0, "NLS+ shader mode did not load");
    shaders->setCurrentRow(nlsPlusRow);
    require(requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.nls.hlsl_file"))->text().isEmpty(),
        "A GLSL-only shader was given an unintended DirectShow fallback file");
    require(requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.nls.glsl_file"))->text() == QStringLiteral("NLSPlus.glsl"),
        "NLS+ did not retain its VP Renderer implementation");

    window.selectPage(8);
    QTabBar* shaderSections = requireControl<QTabBar>(window,
        QStringLiteral("settingsSectionTabs"));
    require(shaderSections->count() == 2 &&
        shaderSections->tabText(0) == QStringLiteral("Standard") &&
        shaderSections->tabText(1) == QStringLiteral("NLS") &&
        shaderSections->currentIndex() == 0,
        "Shaders page does not separate Standard and NLS in the requested order");
    QListWidget* standardShaders = requireControl<QListWidget>(window,
        QStringLiteral("config.shader.standard.items"));
    require(standardShaders->count() == 1,
        "Standard shader fixture did not load");
    int debandingRow = -1;
    for (int row = 0; row < standardShaders->count(); ++row)
        if (standardShaders->item(row)->text() == QStringLiteral("Debanding Mild"))
        {
            debandingRow = row;
            break;
        }
    require(debandingRow >= 0, "Debanding default shader did not load");
    standardShaders->setCurrentRow(debandingRow);
    require(requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.standard.shortcut"))->text().isEmpty(),
        "A shipped standard shader was assigned a shortcut by default");
    require(requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.standard.hlsl_file"))->text() ==
        QStringLiteral("Debanding mild.hlsl"),
        "Standard DirectShow shader file did not load");
    require(requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.standard.glsl_file"))->text().isEmpty(),
        "HLSL-only standard shader was given a VP Renderer fallback file");
    QToolButton* standardParameterToggle = requireControl<QToolButton>(window,
        QStringLiteral("config.shader.standard.parameters_toggle"));
    require(!standardParameterToggle->isChecked(),
        "Standard shader custom parameters did not start collapsed");
    standardParameterToggle->click();
    QTableWidget* standardParameters = requireControl<QTableWidget>(window,
        QStringLiteral("config.shader.standard.parameters"));
    require(standardParameters->rowCount() == 1 &&
        standardParameters->item(0, 0)->text() == QStringLiteral("threshold") &&
        standardParameters->item(0, 1)->text() == QStringLiteral("32"),
        "Standard shader custom parameters did not load");
    standardParameters->item(0, 1)->setText(QStringLiteral("28"));
    requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.standard.shortcut"))->setText(QStringLiteral("Ctrl+d"));
    const int standardCountBeforeAdd = standardShaders->count();
    answerInputDialog(QStringLiteral("Temporary Shader"));
    requireControl<QPushButton>(window, QStringLiteral("config.shader.standard.add"))->click();
    require(standardShaders->count() == standardCountBeforeAdd + 1,
        "Add standard shader did not create a shader configuration");
    require(standardShaders->currentItem() &&
        standardShaders->currentItem()->text() == QStringLiteral("Temporary Shader"),
        "New standard shader was not selected");
    require(requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.standard.shortcut"))->text().isEmpty(),
        "New standard shader unexpectedly received a shortcut");
    require(requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.standard.hlsl_file"))->text().isEmpty() &&
        requireControl<QLineEdit>(window,
            QStringLiteral("config.shader.standard.glsl_file"))->text().isEmpty(),
        "New standard shader did not begin with intentionally blank backend files");

    save(window);
    const QByteArray saved = readBytes(path);
    const QList<QByteArray> expected = {
        "capture_device: Decklink Test Device", "capture_input: HDMI",
        "renderer: VP Renderer", "fullscreen: false",
        "scene_detect: false",
        "fullscreen_monitor_name: Test Display", "container_colorspace: BT2020",
        "queue_size: 48", "lead_frames: 3", "reset_queue_too_large_percent: 200",
        "shortcut: Ctrl+Q", "quality: balanced", "sdr_target_nits: 220", "tone_mapping: spline",
        "screen_aspect: 21:10", "vertical_alignment: bottom", "anamorphic_scale: 4:3",
        "renderer_start_stop_time_method: RATIONAL_RATIONAL", "frame_offset: 75",
        "renderer_primaries: BT2020", "video_conversion: NONE",
        "container_colorspace: REC709", "max_cll: 1200", "max_fall: 450",
        "fullscreen_toggle: Ctrl+F", "config_editor: Ctrl+E",
        "capture_rendered_output: Ctrl+Alt+C", "toggle_noui: Alt+U",
        "renderer: *", "run: C:\\Tools\\verified-action.cmd 42",
        "enabled: false", "debug: false", "debug_log_retention: 25",
        "label: Verified Stretch", "order: 10", "strength: 0.85", "threshold: 28",
        "shortcut: Ctrl+D", "[shader.standard.temporary_shader]",
        "label: Temporary Shader"
    };
    for (const QByteArray& text : expected)
        require(saved.contains(text), text.constData());
    const int rendererRoot = saved.indexOf("[vprenderer.input_processing]");
    const int rendererRootEnd = saved.indexOf(QByteArray("\n["),
        rendererRoot + 1);
    const QByteArray rendererRootValues = rendererRoot >= 0 ? saved.mid(
        rendererRoot, rendererRootEnd < 0 ? -1 : rendererRootEnd - rendererRoot) :
        QByteArray();
    require(rendererRootValues.contains("video_conversion: V210_TO_P010"),
        "VP Renderer input override was not saved in its independent input-policy section");
    require(saved.indexOf("[shader.nls.protected]") < saved.indexOf("[shader.nls.standard]"),
        "NLS selection order was not persisted through section order");

    ConfigEditorWindow reloaded(path, 0, true);
    require(!requireControl<QCheckBox>(reloaded,
        QStringLiteral("config.general.fullscreen"))->isChecked(),
        "General Boolean did not reload");
    require(requireControl<QSpinBox>(reloaded,
        QStringLiteral("config.queue.queue_size"))->value() == 48,
        "Queue value did not reload");
    require(requireControl<QLineEdit>(reloaded,
        QStringLiteral("config.vprenderer.viewport.screen_aspect"))->text() ==
        QStringLiteral("21:10"), "Viewport value did not reload");
    require(requireControl<QComboBox>(reloaded,
        QStringLiteral("config.vprenderer.viewport.vertical_alignment"))->currentData().toString() ==
        QStringLiteral("bottom"), "Vertical alignment did not reload");
    require(requireControl<QComboBox>(reloaded,
        QStringLiteral("config.vprenderer.input_processing.video_conversion"))->currentData().toString() ==
        QStringLiteral("V210_TO_P010"), "VP Renderer input override did not reload");
    require(requireControl<QComboBox>(reloaded,
        QStringLiteral("config.directshow.video_conversion"))->currentData().toString() ==
        QStringLiteral("NONE"), "DirectShow input override did not reload");
}

void testRendererSectionTabsRemainSynchronizedDuringRapidClicks()
{
    QTemporaryDir directory;
    ConfigEditorWindow window(copyFixture(directory), 0, true);
    window.resize(1120, 760);
    window.show();
    window.selectPage(2);
    QCoreApplication::processEvents();

    QStackedWidget* pages = requireControl<QStackedWidget>(window,
        QStringLiteral("settingsPages"));
    QTabBar* sectionTabs = requireControl<QTabBar>(window,
        QStringLiteral("settingsSectionTabs"));
    require(window.findChildren<QTabBar*>(
        QStringLiteral("settingsSectionTabs")).size() == 1,
        "Grouped settings tabs are not one persistent control");

    const auto clickTab = [](QTabBar* tabs, int tabIndex)
    {
        const QPoint point = tabs->tabRect(tabIndex).center();
        const QPoint globalPoint = tabs->mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point),
            QPointF(globalPoint), Qt::LeftButton, Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(tabs, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point),
            QPointF(globalPoint), Qt::LeftButton, Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(tabs, &release);
    };
    const auto requireTabs = [sectionTabs](const QStringList& expected)
    {
        QStringList actual;
        for (int index = 0; index < sectionTabs->count(); ++index)
            actual.push_back(sectionTabs->tabText(index));
        require(actual == expected,
            "Persistent grouped tab labels changed unexpectedly");
    };
    const auto requirePage = [&window, pages](QTabBar* tabs, int tabIndex,
        int pageIndex, const QString& title, const QString& sentinelName)
    {
        require(tabs->currentIndex() == tabIndex,
            "Grouped tab selection is stale");
        require(pages->currentIndex() == pageIndex,
            "Grouped page content did not follow its selected tab");
        QLabel* pageTitle = pages->currentWidget()->findChild<QLabel*>(
            QStringLiteral("pageTitle"));
        require(pageTitle && pageTitle->text() == title,
            "Grouped page title did not follow its selected tab");
        QWidget* sentinel = requireControl<QWidget>(window, sentinelName);
        require(pages->currentWidget()->isAncestorOf(sentinel) &&
            sentinel->isVisibleTo(&window),
            "Grouped page displayed content does not match its title and tab");
    };
    const auto navigationButton = [&window](const QString& text)
    {
        for (QPushButton* button : window.findChildren<QPushButton*>())
            if (button->property("nav").toBool() && button->text() == text)
                return button;
        return static_cast<QPushButton*>(nullptr);
    };
    struct ExpectedPage
    {
        int tabIndex;
        int pageIndex;
        const char* title;
        const char* sentinel;
    };
    const auto runSequence = [sectionTabs, clickTab, requirePage](
        const std::vector<ExpectedPage>& sequence)
    {
        for (const ExpectedPage& expected : sequence)
        {
            clickTab(sectionTabs, expected.tabIndex);
            requirePage(sectionTabs, expected.tabIndex, expected.pageIndex,
                QString::fromLatin1(expected.title),
                QString::fromLatin1(expected.sentinel));
        }
    };

    requireTabs({ QStringLiteral("Rendering"), QStringLiteral("Screen Config"),
        QStringLiteral("Input Processing") });
    runSequence({
        { 1, 4, "Screen Config", "config.vprenderer.viewport.profiles" },
        { 2, 11, "Input processing", "config.vprenderer.input_processing.video_conversion" },
        { 0, 2, "Rendering", "config.vprenderer.profiles" },
        { 2, 11, "Input processing", "config.vprenderer.input_processing.video_conversion" },
        { 1, 4, "Screen Config", "config.vprenderer.viewport.profiles" },
        { 0, 2, "Rendering", "config.vprenderer.profiles" }
    });
    require(navigationButton(QStringLiteral("VP Renderer")) &&
        navigationButton(QStringLiteral("VP Renderer"))->isChecked(),
        "VP Renderer child page lost its parent navigation selection");

    window.selectPage(3);
    requireTabs({ QStringLiteral("General"), QStringLiteral("Input Processing") });
    runSequence({
        { 1, 12, "Input processing", "config.directshow.video_conversion" },
        { 0, 3, "General", "config.directshow.frame_offset.auto" },
        { 1, 12, "Input processing", "config.directshow.video_conversion" },
        { 0, 3, "General", "config.directshow.frame_offset.auto" }
    });
    require(navigationButton(QStringLiteral("DirectShow")) &&
        navigationButton(QStringLiteral("DirectShow"))->isChecked(),
        "DirectShow child page lost its parent navigation selection");

    window.selectPage(8);
    requireTabs({ QStringLiteral("Standard"), QStringLiteral("NLS") });
    runSequence({
        { 1, 9, "NLS", "config.shader.nls.modes" },
        { 0, 8, "Standard", "config.shader.standard.items" },
        { 1, 9, "NLS", "config.shader.nls.modes" },
        { 0, 8, "Standard", "config.shader.standard.items" }
    });
    require(navigationButton(QStringLiteral("Shaders")) &&
        navigationButton(QStringLiteral("Shaders"))->isChecked(),
        "Shader child page lost its parent navigation selection");
    require(!sectionTabs->drawBase(),
        "Persistent grouped tabs restored the native white tab-bar base");
}

void testEmptyStandardShadersCanCreateFirstShader()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.selectPage(8);
    QTabBar* shaderSections = requireControl<QTabBar>(window,
        QStringLiteral("settingsSectionTabs"));
    require(shaderSections->tabText(0) == QStringLiteral("Standard") &&
        shaderSections->currentIndex() == 0,
        "Standard is not the first shader section");
    window.show();
    QCoreApplication::processEvents();
    QListWidget* standardShaders = requireControl<QListWidget>(window,
        QStringLiteral("config.shader.standard.items"));
    require(standardShaders->count() == 0,
        "The no-default fixture unexpectedly contains a standard shader");

    answerInputDialog(QStringLiteral("My First Shader"));
    requireControl<QPushButton>(window, QStringLiteral("config.shader.standard.add"))->click();
    require(standardShaders->count() == 1 && standardShaders->currentItem() &&
        standardShaders->currentItem()->text() == QStringLiteral("My First Shader"),
        "The first Standard shader was not created and selected");
    require(requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.standard.shortcut"))->text().isEmpty(),
        "The first Standard shader unexpectedly received a shortcut");

    save(window);
    const QByteArray saved = readBytes(path);
    require(saved.contains("[shader.standard]\ntype: multi"),
        "Creating the first shader did not create the Standard shader group");
    const int sectionStart = saved.indexOf("[shader.standard.my_first_shader]");
    const int sectionEnd = saved.indexOf(QByteArray("\n["), sectionStart + 1);
    const QByteArray created = sectionStart >= 0 ? saved.mid(sectionStart,
        sectionEnd < 0 ? -1 : sectionEnd - sectionStart) : QByteArray();
    require(created.contains("shader_type: custom") &&
        created.contains("label: My First Shader") &&
        !created.contains("shortcut:") && !created.contains("when:"),
        "The first Standard shader did not save as a clean opt-in custom shader");
}

void testTwoColumnCardsShareRowHeight()
{
    QTemporaryDir directory;
    ConfigEditorWindow window(copyFixture(directory), 0, true);
    window.resize(1200, 800);
    window.show();
    QCoreApplication::processEvents();

    QStackedWidget* pages = requireControl<QStackedWidget>(window,
        QStringLiteral("settingsPages"));
    const auto card = [](QWidget* page, const QString& title) -> QFrame*
    {
        for (QLabel* label : page->findChildren<QLabel*>())
            if (label->property("cardTitle").toBool() && label->text() == title)
                return qobject_cast<QFrame*>(label->parentWidget());
        throw std::runtime_error(("Missing card: " + title).toStdString());
    };
    QFrame* hardware = card(pages->widget(0), QStringLiteral("Hardware"));
    QFrame* behavior = card(pages->widget(0), QStringLiteral("General behavior"));
    QFrame* display = card(pages->widget(0), QStringLiteral("Display"));
    QFrame* input = card(pages->widget(0), QStringLiteral("Input processing"));
    require(hardware->height() == behavior->height() && hardware->y() == behavior->y(),
        "The first two-column card row is not height-aligned");
    require(display->height() == input->height() && display->y() == input->y(),
        "The second two-column card row is not height-aligned");
    require((hardware->layout()->alignment() & Qt::AlignTop) != 0 &&
        (display->layout()->alignment() & Qt::AlignTop) != 0,
        "Two-column card contents are not top-justified");
    require(hardware->findChild<QComboBox*>(
        QStringLiteral("config.general.renderer")) == nullptr &&
        display->findChild<QComboBox*>(
        QStringLiteral("config.general.renderer")) != nullptr,
        "The renderer selector was not moved from Hardware to Display");
    require(display->findChild<QCheckBox*>(
        QStringLiteral("config.general.hide_legacy_renderers")) != nullptr,
        "Display does not contain the Hide legacy renderers checkbox");
    QCheckBox* videoOnly = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.noui"));
    require(videoOnly->text() == QStringLiteral("Start as Video Only"),
        "The Video Only startup checkbox does not use its explicit label");

    pages->setCurrentIndex(6);
    QCoreApplication::processEvents();
    QFrame* application = card(pages->widget(6), QStringLiteral("Application"));
    QFrame* capture = card(pages->widget(6), QStringLiteral("Capture & renderer"));
    require(application->height() == capture->height() &&
        application->y() == capture->y(),
        "The Shortcuts cards are not height-aligned");
    require((application->layout()->alignment() & Qt::AlignTop) != 0 &&
        (capture->layout()->alignment() & Qt::AlignTop) != 0,
        "The Shortcuts card contents are not top-justified");
    QLineEdit* populatedShortcut = requireControl<QLineEdit>(window,
        QStringLiteral("config.shortcuts.capture_3"));
    QLineEdit* emptyShortcut = requireControl<QLineEdit>(window,
        QStringLiteral("config.shortcuts.video_conversion_off"));
    require(populatedShortcut->width() == emptyShortcut->width() &&
        populatedShortcut->minimumWidth() == emptyShortcut->minimumWidth() &&
        populatedShortcut->maximumWidth() == emptyShortcut->maximumWidth(),
        "Empty shortcut editors do not retain the populated field width");
    QStringList shortcutLabels;
    for (QLabel* label : pages->widget(6)->findChildren<QLabel*>())
        shortcutLabels.append(label->text());
    require(shortcutLabels.contains(QStringLiteral("Screenshot")) &&
        !shortcutLabels.contains(QStringLiteral("Capture rendered output")) &&
        shortcutLabels.contains(QStringLiteral("Video conversion off")) &&
        shortcutLabels.contains(QStringLiteral("V210 to P010 conversion")) &&
        !shortcutLabels.contains(QStringLiteral("Active renderer: conversion off")) &&
        !shortcutLabels.contains(QStringLiteral("Active renderer: V210 to P010")),
        "Shared video-conversion shortcuts do not use compact renderer-neutral labels");
}

void testInheritedRendererInputSelectorsUseEffectiveLabels()
{
    QTemporaryDir directory;
    ConfigEditorWindow window(copyFixture(directory), 0, true);
    const auto verifyInherited = [&window](const QString& objectName,
        const QString& expectedText)
    {
        QComboBox* combo = requireControl<QComboBox>(window, objectName);
        require(combo->currentData().toString().isEmpty(),
            "An inherited input setting must retain its empty override value");
        require(combo->currentText() == expectedText,
            "An inherited input setting did not identify its effective value");
        require(combo->property("inherited").toBool(),
            "An inherited input setting is not styled as inherited");
    };
    verifyInherited(QStringLiteral("config.directshow.video_conversion"),
        QStringLiteral("Inherited: V210 to P010"));
    verifyInherited(QStringLiteral("config.vprenderer.input_processing.video_conversion"),
        QStringLiteral("Inherited: V210 to P010"));
    verifyInherited(QStringLiteral("config.directshow.hdr_colorspace"),
        QStringLiteral("Inherited: Follow input (LLDV)"));
}

void testSdrGammaAdjustmentLabelsAndPersistence()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    {
        ConfigEditorWindow window(path, 0, true);
        QComboBox* adjustment = requireControl<QComboBox>(window,
            QStringLiteral("config.vprenderer.sdr_adjust_gamma"));
        require(adjustment->findText(QStringLiteral("Auto")) >= 0 &&
            adjustment->findText(QStringLiteral("On")) >= 0 &&
            adjustment->findText(QStringLiteral("Off")) >= 0,
            "SDR transfer handling does not expose concise labels");
        selectData(adjustment, QStringLiteral("off"));
        save(window);
    }
    require(readBytes(path).contains("sdr_adjust_gamma: off"),
        "SDR gamma adjustment did not persist in the renderer profile");
    {
        ConfigEditorWindow window(path, 0, true);
        QComboBox* adjustment = requireControl<QComboBox>(window,
            QStringLiteral("config.vprenderer.sdr_adjust_gamma"));
        require(adjustment->currentData().toString() == QStringLiteral("off"),
            "SDR gamma adjustment did not reload from the renderer profile");
    }
}

void testLegacyVpInputOverrideMigratesToIndependentPolicySection()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    QFile file(path);
    require(file.open(QIODevice::Append),
        "Could not append the legacy VP input override fixture");
    file.write("\n[vprenderer]\nvideo_conversion: NONE\n");
    file.close();

    ConfigEditorWindow window(path, 0, true);
    QComboBox* conversion = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.input_processing.video_conversion"));
    require(conversion->currentData().toString() == QStringLiteral("NONE"),
        "The legacy VP input override was not loaded into the new selector");
    save(window);

    const QByteArray saved = readBytes(path);
    const int inputSection = saved.indexOf("[vprenderer.input_processing]");
    const int inputEnd = saved.indexOf(QByteArray("\n["), inputSection + 1);
    const QByteArray inputValues = inputSection >= 0 ? saved.mid(inputSection,
        inputEnd < 0 ? -1 : inputEnd - inputSection) : QByteArray();
    require(inputValues.contains("video_conversion: NONE"),
        "The migrated VP input override was not saved independently");
    require(!saved.contains("[vprenderer]\nvideo_conversion:"),
        "The legacy display-profile root still owns VP input processing");
    require(!saved.contains("[vprenderer.profile_1]"),
        "Migrating input policy created a synthetic display profile");
}

void answerInputDialog(const QString& text)
{
    auto* timer = new QTimer(qApp);
    timer->setInterval(1);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, text]
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QInputDialog*>(widget))
            {
                dialog->setTextValue(text);
                dialog->accept();
                timer->stop();
                timer->deleteLater();
                return;
            }
    });
    timer->start();
}

void answerMessageBox(int result)
{
    auto* timer = new QTimer(qApp);
    timer->setInterval(1);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, result]
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QMessageBox*>(widget))
            {
                if (QAbstractButton* button = dialog->button(
                    static_cast<QMessageBox::StandardButton>(result)))
                    button->click();
                else
                    dialog->done(result);
                timer->stop();
                timer->deleteLater();
                return;
            }
    });
    timer->start();
}

void testProfileLifecycleThroughWidgets()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    QListWidget* list = requireControl<QListWidget>(window,
        QStringLiteral("config.queue.profiles"));
    require(list->count() == 2, "Queue profiles did not load");

    list->setCurrentRow(1);
    QLineEdit* name = requireControl<QLineEdit>(window,
        QStringLiteral("config.queue.name"));
    name->setText(QStringLiteral("Cinema Queue"));
    QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
    require(list->currentItem()->text().startsWith(QStringLiteral("Cinema Queue")),
        "Profile rename did not update the list");

    requireControl<QPushButton>(window, QStringLiteral("config.queue.move_up"))->click();
    require(list->currentRow() == 0, "Move up did not reorder the profile");
    require(list->item(0)->text().contains(QStringLiteral("Default")),
        "First reordered profile was not marked default");

    const int beforeAdd = list->count();
    requireControl<QPushButton>(window, QStringLiteral("config.queue.add_profile"))->click();
    require(list->count() == beforeAdd + 1, "Add profile did not create a profile");
    require(list->currentItem()->text().startsWith(QStringLiteral("New 1")),
        "Add profile did not select the first available generated name");
    require(name->text() == QStringLiteral("New 1"),
        "A generated profile name was not loaded into Profile details");
    name->clear();
    answerMessageBox(QMessageBox::Ok);
    QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
    require(name->text() == QStringLiteral("New 1"),
        "Rejecting an empty profile name left the editor blank");
    QCoreApplication::processEvents();

    requireControl<QPushButton>(window, QStringLiteral("config.queue.add_profile"))->click();
    require(list->count() == beforeAdd + 2 &&
        list->currentItem()->text().startsWith(QStringLiteral("New 2")),
        "Add profile did not skip an existing generated name");
    answerMessageBox(QMessageBox::Yes);
    requireControl<QPushButton>(window, QStringLiteral("config.queue.remove_profile"))->click();
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->text().startsWith(QStringLiteral("New 1")))
        {
            list->setCurrentRow(row);
            break;
        }
    answerMessageBox(QMessageBox::Yes);
    requireControl<QPushButton>(window, QStringLiteral("config.queue.remove_profile"))->click();
    require(list->count() == beforeAdd, "Remove profile did not remove the selected profile");
    save(window);
    const QByteArray saved = readBytes(path);
    const QByteArray lowerSaved = saved.toLower();
    require(lowerSaved.contains("[queue.cinema_queue]"), "Renamed profile header was not saved");
    require(lowerSaved.indexOf("[queue.cinema_queue]") < lowerSaved.indexOf("[queue.profile_1]"),
        "Reordered profile file order was not saved");
    require(!lowerSaved.contains("new_1"), "Removed profile was serialized");
}

void testUnrelatedContentRemainsExact()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    const QByteArray original = readBytes(path);
    const QList<QByteArray> preservedBlocks = {
        "[directshow.conversion]\nconversion_method: SIMD\nmin_core_count: 1\nmax_core_count: 2",
        "[directshow.ppm]\nppm: auto",
        "# These audio scripts deliberately keep their millisecond delay arguments literal.",
        "# Disabled after VP-0095 topology-mode regression; retained for later retest: fullscreen_monitor_session_mode: target-only"
    };
    for (const QByteArray& block : preservedBlocks)
        require(original.contains(block), "Preservation marker missing from source fixture");

    ConfigEditorWindow window(path, 0, true);
    requireControl<QCheckBox>(window, QStringLiteral("config.general.scene_detect"))->setChecked(false);
    save(window);
    const QByteArray saved = readBytes(path);
    for (const QByteArray& block : preservedBlocks)
        require(saved.contains(block), "An unrelated manual block changed during UI save");
}

void testSceneDetectionDefaultsOffAndHidesManualOverrides()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    QByteArray configuration = readBytes(path);
    configuration.replace("scene_detect: true\n", "");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Cannot prepare the scene detection fixture");
    file.write(configuration);
    file.close();

    ConfigEditorWindow window(path, 0, true);
    QCheckBox* sceneDetection = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.scene_detect"));
    require(!sceneDetection->isChecked(),
        "Scene detection should be off when it is absent from configuration");
    require(window.findChild<QCheckBox*>(QStringLiteral(
        "config.general.disable_detection_features")) == nullptr,
        "Manual detection-feature override leaked into the editor UI");
    require(window.findChild<QCheckBox*>(QStringLiteral(
        "config.general.scene_correction_basic")) == nullptr,
        "Basic scene correction leaked into the editor UI");

    sceneDetection->setChecked(true);
    save(window);
    require(readBytes(path).contains("scene_detect: true"),
        "Enabling scene detection did not persist its configured value");
}

void testVirtualShaderOffOptionPersistsWhenConfigured()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    QByteArray configuration = readBytes(path);
    const qsizetype rootStart = configuration.indexOf("[shader.nls]\n");
    const qsizetype nextSection = configuration.indexOf("\n[shader.nls.", rootStart);
    require(rootStart >= 0 && nextSection > rootStart,
        "Shader root section was not found in the fixture");
    configuration.remove(rootStart, nextSection - rootStart + 1);
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Cannot prepare shader fixture without an Off section");
    require(file.write(configuration) == configuration.size(),
        "Cannot write shader fixture without an Off section");
    file.close();

    ConfigEditorWindow window(path, 0, true);
    QListWidget* modes = requireControl<QListWidget>(window,
        QStringLiteral("config.shader.nls.modes"));
    require(modes->count() >= 1 && modes->item(0)->text() == QStringLiteral("Off"),
        "The virtual Off option was not created without a root section");
    modes->setCurrentRow(0);
    QLineEdit* shortcut = requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.nls.shortcut"));
    require(shortcut->text().isEmpty(),
        "A virtual Off option unexpectedly received a shortcut");
    shortcut->setText(QStringLiteral("Ctrl+0"));
    save(window);
    const QByteArray saved = readBytes(path);
    require(saved.contains("[shader.nls]"),
        "Editing virtual Off did not create its configuration section");
    require(saved.contains("shortcut: Ctrl+0"),
        "Editing virtual Off did not persist its shortcut");
}

void testDisablingShaderRulePreservesShortcut()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    QListWidget* modes = requireControl<QListWidget>(window,
        QStringLiteral("config.shader.nls.modes"));
    require(modes->count() > 1, "NLS fixture did not load");
    modes->setCurrentRow(1);

    QLineEdit* shortcut = requireControl<QLineEdit>(window,
        QStringLiteral("config.shader.nls.shortcut"));
    QCheckBox* useRule = requireControl<QCheckBox>(window,
        QStringLiteral("config.shader.nls.use_rule"));
    QPlainTextEdit* rule = requireControl<QPlainTextEdit>(window,
        QStringLiteral("config.shader.nls.when"));
    shortcut->setText(QStringLiteral("Shift+N"));
    rule->setPlainText(QStringLiteral("${eotf} == \"HDR\""));
    require(useRule->isChecked(), "Entering a shader rule did not enable it");
    useRule->setChecked(false);
    save(window);

    const QByteArray saved = readBytes(path);
    const qsizetype start = saved.indexOf("[shader.nls.standard]");
    const qsizetype end = saved.indexOf("[shader.nls.protected]", start);
    require(start >= 0 && end > start, "Shader mode sections were not saved");
    const QByteArray standard = saved.mid(start, end - start);
    require(standard.contains("shortcut: Shift+N"),
        "Disabling a shader rule removed its manual shortcut");
    require(!standard.contains("when:"),
        "Disabling a shader rule did not remove only its when expression");
}

void testRendererProfileSectionsCollapseAndPersist()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.selectPage(2);
    window.show();
    QCoreApplication::processEvents();

    QToolButton* calibration = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.calibration"));
    QToolButton* sourceColor = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.sourceColor"));
    QToolButton* toneMapping = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.toneMapping"));
    QToolButton* scaling = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.scalingCleanup"));
    QToolButton* lut = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.lut"));
    require(!calibration->isChecked() && !sourceColor->isChecked() &&
        !toneMapping->isChecked() && !scaling->isChecked() &&
        !lut->isChecked(),
        "A renderer section was not collapsed initially");
    require(scaling->text() == QStringLiteral("Scaling and Processing") &&
        !scaling->text().contains(u'_'),
        "The scaling section exposes an internal identifier instead of a friendly heading");
    require(sourceColor->text() == QStringLiteral("Source transfer"),
        "The SDR input-transfer controls are not grouped under Source transfer");
    QWidget* calibrationContent = requireControl<QWidget>(window,
        QStringLiteral("rendererSection.calibration.content"));
    QWidget* toneMappingContent = requireControl<QWidget>(window,
        QStringLiteral("rendererSection.toneMapping.content"));
    require(toneMappingContent->findChild<QLineEdit*>(
        QStringLiteral("config.vprenderer.sdr_target_nits")) != nullptr &&
        toneMappingContent->findChild<QLineEdit*>(
            QStringLiteral("config.vprenderer.sdr_black_nits")) != nullptr &&
        calibrationContent->findChild<QLineEdit*>(
            QStringLiteral("config.vprenderer.sdr_target_nits")) == nullptr,
        "Tone-mapping target white/black controls are not grouped under Tone mapping");
    require(window.findChild<QComboBox*>(
        QStringLiteral("config.vprenderer.default_screen_profile")) == nullptr,
        "The obsolete legacy screen-profile selector is still exposed");
    require(window.findChild<QComboBox*>(
        QStringLiteral("config.vprenderer.deband")) == nullptr,
        "The overlapping legacy debanding toggle is still exposed");
    QToolButton* outputExperiments = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.outputExperiments"));
    require(!outputExperiments->isChecked(),
        "Output experiments were expanded initially");
    QToolButton* advancedOutput = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.advancedOutput"));
    require(!advancedOutput->isChecked(),
        "Advanced output was expanded initially");
    QCheckBox* vpOwnedPresenter = requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_vp_owned_dxgi_presenter"));
    require(requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.output_diagnostics")) &&
        requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_disable_shader_cache")) &&
        requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_disable_compute")) &&
        requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_force_8bit_sdr_swapchain")) &&
        vpOwnedPresenter &&
        requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_allow_limited_g22")),
        "Output experiment controls are missing from the editor");
    require(vpOwnedPresenter->accessibleName() ==
        QStringLiteral("Force VP-owned DXGI presenter (flip only, beta)"),
        "The VP-owned DXGI beta path is not labelled as a flip-only diagnostic");
    require(requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.report_bt2020_to_display")),
        "The renderer display-reporting control is not included in Display calibration");
    require(requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_gamma")),
        "The display-transfer control is not included in Display calibration");
    require(requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_presentation")) &&
        requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_range")),
        "The ordinary output controls are not included in Advanced output");
    require(requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.quality")),
        "Rendering quality is not available at the top of the renderer profile");
    QStackedWidget* pages = requireControl<QStackedWidget>(window,
        QStringLiteral("settingsPages"));
    require(pages->widget(0)->findChild<QCheckBox*>(
        QStringLiteral("config.general.switch_refresh_rate")) != nullptr &&
        pages->widget(2)->findChild<QCheckBox*>(
        QStringLiteral("config.general.switch_refresh_rate")) == nullptr,
        "Switch refresh rate was not moved from Renderer Basic to General Display");
    QComboBox* debanding = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.deband_strength"));
    require(debanding->findData(QStringLiteral("AUTO")) >= 0 &&
        debanding->findData(QStringLiteral("off")) >= 0 &&
        debanding->findData(QStringLiteral("light")) >= 0 &&
        debanding->findData(QStringLiteral("default")) >= 0,
        "The canonical debanding selector is missing an expected value");
    QComboBox* dithering = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.dithering"));
    require(dithering->itemText(dithering->findData(QStringLiteral("AUTO"))) ==
        QStringLiteral("Auto") &&
        dithering->itemText(dithering->findData(QStringLiteral("on"))) ==
        QStringLiteral("On") &&
        dithering->itemText(dithering->findData(QStringLiteral("off"))) ==
        QStringLiteral("Off"),
        "The dithering selector does not use concise Auto/On/Off labels");
    QComboBox* displayBitDepth = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.display_bit_depth"));
    require(displayBitDepth->currentData().toString().isEmpty() &&
        displayBitDepth->currentText() == QStringLiteral("Default: Auto"),
        "An unset value in the default renderer profile is not identified as a built-in default");
    require(displayBitDepth->itemText(
        displayBitDepth->findData(QStringLiteral("AUTO"))) == QStringLiteral("Auto") &&
        displayBitDepth->itemText(
        displayBitDepth->findData(QStringLiteral("8"))) == QStringLiteral("8-bit") &&
        displayBitDepth->itemText(
        displayBitDepth->findData(QStringLiteral("10"))) ==
        QStringLiteral("10-bit or higher"),
        "The display bit-depth selector is missing an expected value");
    require(!requireControl<QWidget>(window,
        QStringLiteral("rendererSection.lut.content"))->isVisibleTo(&window),
        "3D LUT renderer content is visible while collapsed");
    require(!calibrationContent->isVisibleTo(&window),
        "Display calibration content is visible while collapsed");

    sourceColor->click();
    require(sourceColor->isChecked() && requireControl<QWidget>(window,
        QStringLiteral("rendererSection.sourceColor.content"))->isVisibleTo(&window),
        "Source transfer section did not expand");
    QListWidget* profiles = requireControl<QListWidget>(window,
        QStringLiteral("config.vprenderer.profiles"));
    if (profiles->count() > 1) profiles->setCurrentRow(1);
    QCoreApplication::processEvents();
    require(sourceColor->isChecked(),
        "Renderer section expansion state changed when selecting another profile");
    QComboBox* outputPresentation = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_presentation"));
    QComboBox* outputRange = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_range"));
    require(outputPresentation->currentData().toString().isEmpty() &&
        outputPresentation->currentText() == QStringLiteral("Inherited: Direct") &&
        outputRange->currentData().toString().isEmpty() &&
        outputRange->currentText() == QStringLiteral("Inherited: Full"),
        "A secondary renderer profile does not expose inherited Advanced output values");

    // Selecting the canonical control must retire the compatibility toggle,
    // so saving cannot leave two conflicting debanding values in one profile.
    profiles->setCurrentRow(0);
    QCoreApplication::processEvents();
    selectData(debanding, QStringLiteral("light"));
    selectData(displayBitDepth, QStringLiteral("8"));
    save(window);
    const QByteArray saved = readBytes(path);
    require(saved.contains("deband_strength: light") &&
        !saved.contains("\ndeband:") &&
        saved.contains("display_bit_depth: 8"),
        "Renderer processing controls did not persist canonically");
}

void testOutputExperimentsPersistAndRestoreDefaults()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.selectPage(2);
    window.show();
    QCoreApplication::processEvents();

    QToolButton* section = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.outputExperiments"));
    section->click();
    QComboBox* outputPathProfile = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_path_profile"));
    QComboBox* outputPresentation = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_presentation"));
    QComboBox* outputRange = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_range"));
    QComboBox* outputGamma = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.output_gamma"));
    const QString originalPresentation = outputPresentation->currentData().toString();
    const QString originalRange = outputRange->currentData().toString();
    const QString originalGamma = outputGamma->currentData().toString();
    selectData(outputPathProfile, QStringLiteral("proposed"));
    require(outputPresentation->currentData().toString() == originalPresentation &&
        outputRange->currentData().toString() == originalRange &&
        outputGamma->currentData().toString() == originalGamma,
        "Diagnostic preset changed an ordinary output or calibration control");
    QCheckBox* limitedG22 = requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_allow_limited_g22"));
    QCheckBox* noCompute = requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_disable_compute"));
    QCheckBox* force8Bit = requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_force_8bit_sdr_swapchain"));
    QCheckBox* vpOwned = requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.diagnostic_vp_owned_dxgi_presenter"));
    selectData(outputPresentation, QStringLiteral("direct"));
    selectData(outputRange, QStringLiteral("limited"));
    selectData(outputGamma, QStringLiteral("2.4"));
    require(outputPathProfile->currentData().toString() == QStringLiteral("proposed"),
        "Editing ordinary output or display calibration changed the diagnostic preset");
    limitedG22->setChecked(true);
    noCompute->setChecked(true);
    force8Bit->setChecked(true);
    vpOwned->setChecked(true);
    require(outputPathProfile->currentData().toString() == QStringLiteral("custom"),
        "Editing a proposed output-path value did not select Custom");
    save(window);
    const QByteArray configured = readBytes(path);
    require(configured.contains("diagnostic_allow_limited_g22: true") &&
        configured.contains("diagnostic_disable_compute: true") &&
        configured.contains("diagnostic_force_8bit_sdr_swapchain: true") &&
        configured.contains("diagnostic_vp_owned_dxgi_presenter: true") &&
        configured.contains("output_gamma: 2.4") &&
        configured.contains("output_path_profile: custom"),
        "Output experiment controls did not persist with the renderer profile");

    answerMessageBox(QMessageBox::Yes);
    requireControl<QPushButton>(window,
        QStringLiteral("config.vprenderer.output_experiments.reset_defaults"))->click();
    require(!limitedG22->isChecked() && !noCompute->isChecked() &&
        !force8Bit->isChecked() && !vpOwned->isChecked(),
        "Restore Normal Diagnostics did not reset the output experiment controls");
    require(outputPathProfile->currentData().toString() == QStringLiteral("legacy"),
        "Restore Normal Diagnostics did not select normal diagnostics");
    require(outputPresentation->currentData().toString().compare(
        QStringLiteral("direct"), Qt::CaseInsensitive) == 0 &&
        outputRange->currentData().toString().compare(
        QStringLiteral("limited"), Qt::CaseInsensitive) == 0 &&
        outputGamma->currentData().toString() == QStringLiteral("2.4"),
        "Restore Normal Diagnostics changed ordinary output or calibration controls");
    save(window);
    const QByteArray restored = readBytes(path);
    require(restored.contains("diagnostic_allow_limited_g22: false") &&
        restored.contains("diagnostic_disable_compute: false") &&
        restored.contains("diagnostic_force_8bit_sdr_swapchain: false") &&
        restored.contains("diagnostic_vp_owned_dxgi_presenter: false") &&
        restored.contains("output_path_profile: legacy") &&
        restored.toLower().contains("output_presentation: direct") &&
        restored.toLower().contains("output_range: limited") &&
        restored.contains("output_gamma: 2.4"),
        "Restored normal diagnostics changed ordinary output or calibration settings");
}

void testScreenConfigSectionsAndInlineUnits()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.selectPage(4);
    window.show();
    QCoreApplication::processEvents();

    QToolButton* geometry = requireControl<QToolButton>(window,
        QStringLiteral("screenSection.geometry"));
    QToolButton* subtitles = requireControl<QToolButton>(window,
        QStringLiteral("screenSection.subtitles"));
    require(geometry->isChecked() && !subtitles->isChecked(),
        "Screen Config sections did not use the expected initial expansion state");
    require(geometry->text() == QStringLiteral("Screen geometry") &&
        subtitles->text() == QStringLiteral("Subtitles"),
        "Screen Config does not use friendly section headings");
    require(requireControl<QWidget>(window,
        QStringLiteral("screenSection.geometry.content"))->isVisibleTo(&window) &&
        !requireControl<QWidget>(window,
        QStringLiteral("screenSection.subtitles.content"))->isVisibleTo(&window),
        "Screen Config section content visibility does not follow its headers");

    subtitles->click();
    QCoreApplication::processEvents();
    QLineEdit* hold = requireControl<QLineEdit>(window,
        QStringLiteral("config.vprenderer.viewport.subtitle_hold_seconds"));
    QLabel* holdUnit = requireControl<QLabel>(window,
        QStringLiteral("config.vprenderer.viewport.subtitle_hold_seconds.unit"));
    QLabel* engageUnit = requireControl<QLabel>(window,
        QStringLiteral("config.vprenderer.viewport.subtitle_engage_drift_ms.unit"));
    QLabel* paddingUnit = requireControl<QLabel>(window,
        QStringLiteral("config.vprenderer.viewport.subtitle_padding_pixels.unit"));
    require(holdUnit->text() == QStringLiteral("ms") &&
        engageUnit->text() == QStringLiteral("ms") &&
        paddingUnit->text() == QStringLiteral("pixels"),
        "Screen Config fixed-unit inputs are missing inline unit labels");
    require(hold->text() == QStringLiteral("2000"),
        "Subtitle hold is not presented in milliseconds");
    require(hold->minimumWidth() > 0 &&
        hold->minimumWidth() == hold->maximumWidth() &&
        hold->alignment() == Qt::AlignRight &&
        holdUnit->x() >= hold->x() + hold->width(),
        "Screen Config unit input is not consistently sized, aligned, and labeled");

    hold->setText(QStringLiteral("1500"));
    save(window);
    require(readBytes(path).contains("subtitle_hold_seconds: 1.5"),
        "Millisecond subtitle hold did not preserve the seconds-based config contract");

    QListWidget* profiles = requireControl<QListWidget>(window,
        QStringLiteral("config.vprenderer.viewport.profiles"));
    if (profiles->count() > 1) profiles->setCurrentRow(1);
    QCoreApplication::processEvents();
    require(subtitles->isChecked(),
        "Screen Config section expansion state changed when selecting another profile");
}

void testQueueUnitsAndLutControlsUseConsistentRows()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.selectPage(1);
    window.show();
    QCoreApplication::processEvents();

    QSpinBox* queueDepth = requireControl<QSpinBox>(window,
        QStringLiteral("config.queue.queue_size"));
    QLabel* queueUnit = requireControl<QLabel>(window,
        QStringLiteral("config.queue.queue_size.unit"));
    QLabel* recoveryUnit = requireControl<QLabel>(window,
        QStringLiteral("config.queue.reset_queue_too_large_percent.unit"));
    const int fixedUnitFieldWidth = requireControl<QLineEdit>(window,
        QStringLiteral("config.vprenderer.viewport.subtitle_hold_seconds"))->minimumWidth();
    const QStringList queueValueKeys = {
        QStringLiteral("queue_size"),
        QStringLiteral("lead_frames"),
        QStringLiteral("startup_preroll_frames"),
        QStringLiteral("target_frames"),
        QStringLiteral("active_picture_lookahead_frames"),
        QStringLiteral("reset_after_render_restart_seconds"),
        QStringLiteral("reset_queue_too_large_percent")
    };
    for (const QString& key : queueValueKeys)
    {
        QSpinBox* value = requireControl<QSpinBox>(window,
            QStringLiteral("config.queue.") + key);
        require(value->minimumWidth() == fixedUnitFieldWidth &&
            value->maximumWidth() == fixedUnitFieldWidth &&
            value->alignment() == Qt::AlignRight,
            "Queue value controls do not use one consistent width and alignment");
    }
    require(queueDepth->suffix().isEmpty() &&
        queueUnit->text() == QStringLiteral("frames") &&
        recoveryUnit->text() == QStringLiteral("%"),
        "Queue units are still embedded in their value controls");
    require(queueUnit->x() >= queueDepth->x() + queueDepth->width(),
        "Queue unit label is not aligned beside its bounded value input");

    window.selectPage(3);
    QToolButton* lutSection = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.lut"));
    if (!lutSection->isChecked()) lutSection->click();
    QCoreApplication::processEvents();
    QComboBox* luminance = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.lut_reference_nits"));
    QComboBox* range = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.lut_reference_range"));
    require(!luminance->isEditable() &&
        luminance->findData(QStringLiteral("AUTO")) >= 0 &&
        luminance->findData(QStringLiteral("203")) >= 0,
        "LUT reference luminance is not a dropdown with common presets");
    const QPoint luminancePosition = luminance->mapTo(&window, QPoint(0, 0));
    const QPoint rangePosition = range->mapTo(&window, QPoint(0, 0));
    require(luminancePosition.x() == rangePosition.x() &&
        luminance->width() == range->width(),
        "LUT reference controls do not share aligned dropdown geometry");

    selectData(luminance, QStringLiteral("250"));
    save(window);
    require(readBytes(path).contains("lut_reference_nits: 250"),
        "Custom LUT reference luminance did not persist");
}

void testActiveProfileMarkersCoverRelevantLists()
{
    QTemporaryDir directory;
    ConfigEditorWindow window(copyFixture(directory), 0, true);
    auto* queue = requireControl<QListWidget>(window, QStringLiteral("config.queue.profiles"));
    auto* renderer = requireControl<QListWidget>(window, QStringLiteral("config.vprenderer.profiles"));
    auto* viewport = requireControl<QListWidget>(window, QStringLiteral("config.vprenderer.viewport.profiles"));
    auto* shader = requireControl<QListWidget>(window, QStringLiteral("config.shader.nls.modes"));
    window.setActiveProfileStatusForTesting(
        queue->item(0)->data(Qt::UserRole).toString(),
        renderer->item(0)->data(Qt::UserRole).toString(),
        viewport->item(0)->data(Qt::UserRole).toString(),
        { shader->item(0)->data(Qt::UserRole).toString() });
    require(queue->item(0)->data(Qt::UserRole + 12).toBool() &&
        renderer->item(0)->data(Qt::UserRole + 12).toBool() &&
        viewport->item(0)->data(Qt::UserRole + 12).toBool() &&
        shader->item(0)->data(Qt::UserRole + 12).toBool(),
        "A resolved active profile did not receive its active marker");

    window.selectPage(1);
    window.show();
    QCoreApplication::processEvents();
    queue->setCurrentRow(0);
    QCoreApplication::processEvents();
    const QRect row = queue->visualItemRect(queue->item(0));
    const QImage pixels = queue->viewport()->grab().toImage();
    const QColor dot = pixels.pixelColor(8, row.center().y());
    const QColor selectedBackground = pixels.pixelColor(2, row.center().y());
    require(dot.green() > dot.red() && dot.green() > dot.blue(),
        "The active marker was not painted as a green dot");
    require(selectedBackground.blue() > selectedBackground.red() &&
        selectedBackground.blue() > selectedBackground.green(),
        "The selected active row did not retain a full-width blue background");
}

void testActiveShaderMarkersUseAuthoritativeSet()
{
    QTemporaryDir directory;
    ConfigEditorWindow window(copyFixture(directory), 0, true);
    auto* shader = requireControl<QListWidget>(window,
        QStringLiteral("config.shader.nls.modes"));
    require(shader->count() >= 3,
        "Shader fixture does not expose Off and two shader members");

    shader->setCurrentRow(2);
    const QString off = shader->item(0)->data(Qt::UserRole).toString();
    const QString first = shader->item(1)->data(Qt::UserRole).toString();
    const QString second = shader->item(2)->data(Qt::UserRole).toString();

    window.setActiveProfileStatusForTesting({}, {}, {}, { off });
    require(shader->currentRow() == 2 &&
        shader->item(0)->data(Qt::UserRole + 12).toBool() &&
        !shader->item(2)->data(Qt::UserRole + 12).toBool(),
        "Off activity was confused with the selected editing row");

    window.setActiveProfileStatusForTesting({}, {}, {}, { first });
    require(!shader->item(0)->data(Qt::UserRole + 12).toBool() &&
        shader->item(1)->data(Qt::UserRole + 12).toBool() &&
        shader->currentRow() == 2,
        "A single active shader did not move independently of selection");

    window.setActiveProfileStatusForTesting({}, {}, {}, { first, second });
    require(shader->item(1)->data(Qt::UserRole + 12).toBool() &&
        shader->item(2)->data(Qt::UserRole + 12).toBool(),
        "Multiple authoritative active shaders were collapsed to one row");

    window.setActiveProfileStatusForTesting({}, {}, {}, { first }, false);
    for (int index = 0; index < shader->count(); ++index)
        require(!shader->item(index)->data(Qt::UserRole + 12).toBool(),
            "Unavailable shader state retained a guessed active marker");
}

void testActiveShaderStatusRejectsStaleGeneration()
{
    ActiveProfileStatus::Snapshot snapshot{};
    snapshot.rendererGeneration = 9;
    snapshot.shaderGeneration = 8;
    snapshot.shaderAvailable = 1;
    snapshot.shaderCount = 1;
    require(!ActiveProfileStatus::ShaderSetIsCurrent(snapshot),
        "A stale shader set was accepted for a newer renderer generation");
    snapshot.shaderGeneration = snapshot.rendererGeneration;
    require(ActiveProfileStatus::ShaderSetIsCurrent(snapshot),
        "A generation-current authoritative shader set was rejected");
}

void testStandaloneConfigAcceptsLiveActiveProfileStatus()
{
    ActiveProfileStatus::Publish(GetCurrentProcessId(), 1,
        { { "queue", "low_latency" }, { "display", "rec709" } },
        7, true, { "shader.nls.nonlinear_stretch",
            "shader.future.member" });
    ActiveProfileStatus::Snapshot snapshot;
    require(ActiveProfileStatus::Read(0, snapshot),
        "Standalone Config did not accept the live VP active-profile status");
    require(ActiveProfileStatus::ShaderSetIsCurrent(snapshot) &&
        snapshot.shaderCount == 2 &&
        std::string(snapshot.shaders[0]) == "shader.nls.nonlinear_stretch" &&
        std::string(snapshot.shaders[1]) == "shader.future.member",
        "Live status did not preserve the authoritative shader set");
    require(!ActiveProfileStatus::Read(GetCurrentProcessId() + 1, snapshot),
        "Active-profile status was accepted for the wrong VP process");
}

void testLutSelectorDiscoversInstallationLutFiles()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    const QString lutDirectory = directory.filePath(QStringLiteral("luts"));
    require(QDir().mkpath(lutDirectory), "Cannot create the temporary LUT directory");
    QFile lut(lutDirectory + QStringLiteral("/Test-Calibration.cube"));
    require(lut.open(QIODevice::WriteOnly), "Cannot create the temporary LUT fixture");
    lut.write("TITLE \"Test LUT\"\nLUT_3D_SIZE 2\n");
    lut.close();

    ConfigEditorWindow window(path, 0, true);
    QComboBox* selector = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.lut"));
    require(selector->findData(QStringLiteral("luts/Test-Calibration.cube")) >= 0,
        "The LUT selector did not discover a .cube file from the installation LUT folder");
    require(requireControl<QPushButton>(window,
        QStringLiteral("config.vprenderer.lut.open_folder"))->text() == QStringLiteral("Open LUT folder"),
        "The LUT folder action is missing");

    selectData(selector, QStringLiteral("luts/Test-Calibration.cube"));
    save(window);
    require(readBytes(path).contains("lut: luts/Test-Calibration.cube"),
        "The LUT selector did not persist a configuration-relative path");
}

void testChoiceLabelsAndVpRendererName()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);

    QComboBox* nominalRange = requireControl<QComboBox>(window,
        QStringLiteral("config.directshow.renderer_nominal_range"));
    require(nominalRange->itemText(0) == QStringLiteral("Auto") &&
        nominalRange->itemData(0).toString() == QStringLiteral("AUTO"),
        "DirectShow Auto is not presented with normal title casing");
    require(nominalRange->findText(QStringLiteral("Default")) < 0,
        "DirectShow selector still contains a fabricated Default entry");

    QCheckBox* frameOffsetAuto = requireControl<QCheckBox>(window,
        QStringLiteral("config.directshow.frame_offset.auto"));
    QSpinBox* frameOffsetValue = requireControl<QSpinBox>(window,
        QStringLiteral("config.directshow.frame_offset.value"));
    require(!frameOffsetAuto->isTristate() && !frameOffsetAuto->isChecked() &&
        frameOffsetValue->isEnabled() && frameOffsetValue->value() == 90,
        "Fixed Frame offset is not presented as an editable numeric value");

    QComboBox* startStop = requireControl<QComboBox>(window,
        QStringLiteral("config.directshow.renderer_start_stop_time_method"));
    require(startStop->itemData(0).toString() == QStringLiteral("CLOCK_SMART") &&
        startStop->findText(QStringLiteral("Default")) < 0,
        "Start/stop selector does not use its first real mode as the fallback");

    QComboBox* peakDetection = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.peak_detection"));
    require(peakDetection->findText(QStringLiteral("Auto")) >= 0 &&
        peakDetection->findText(QStringLiteral("AUTO")) < 0 &&
        peakDetection->findData(QStringLiteral("default")) < 0,
        "Renderer selector exposes duplicate or inconsistently cased automatic choices");

    QComboBox* shaderStage = requireControl<QComboBox>(window,
        QStringLiteral("config.shader.nls.stage"));
    require(shaderStage->findText(QStringLiteral("Before resize")) >= 0 &&
        shaderStage->findData(QStringLiteral("pre_resize")) >= 0 &&
        shaderStage->findText(QStringLiteral("pre_resize")) < 0,
        "Shader stage exposes its raw config token");

    for (QComboBox* choice : window.findChildren<QComboBox*>())
        for (int index = 0; index < choice->count(); ++index)
        {
            const QString visible = choice->itemText(index);
            require(visible != QStringLiteral("AUTO"),
                "A selector still exposes AUTO instead of Auto");
            if (!choice->isEditable())
                require(!visible.contains(u'_'),
                    "A known selector exposes a raw underscored config token");
        }
    for (QLineEdit* edit : window.findChildren<QLineEdit*>())
        require(edit->text() != QStringLiteral("AUTO"),
            "A text field still exposes AUTO instead of Auto");
    for (QCheckBox* check : window.findChildren<QCheckBox*>())
        require(!check->isTristate(),
            "A checkbox still exposes an indeterminate third state");

    window.selectPage(4);
    QCheckBox* automaticCrop = requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.viewport.automatic_crop"));
    require(!automaticCrop->isTristate() &&
        automaticCrop->checkState() != Qt::PartiallyChecked,
        "A profile Boolean is exposed as a three-state checkbox");

    QComboBox* renderer = requireControl<QComboBox>(window,
        QStringLiteral("config.general.renderer"));
    frameOffsetAuto->setChecked(true);
    require(!frameOffsetValue->isEnabled(),
        "Automatic Frame offset did not disable the numeric value");
    selectData(renderer, QStringLiteral("VideoProcessor Renderer (Alpha)"));
    save(window);
    const QByteArray saved = readBytes(path);
    require(saved.contains("renderer: VP Renderer"),
        "Legacy Alpha renderer display name was not normalized when saved");
    require(saved.contains("frame_offset: AUTO") &&
        !saved.contains("frame_offset: Auto"),
        "Frame offset did not preserve the canonical AUTO token on disk");
}

void testLegacyRendererVisibilityDefaultsHiddenAndPreservesShortcuts()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);

    QCheckBox* hideLegacyRenderers = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.hide_legacy_renderers"));
    require(hideLegacyRenderers->isChecked(),
        "Hide legacy renderers does not default to enabled when omitted");
    require(window.findChild<QLineEdit*>(
        QStringLiteral("config.shortcuts.render.1")) == nullptr,
        "A renderer shortcut was exposed without a visible discovered renderer");

    requireControl<QCheckBox>(window,
        QStringLiteral("config.general.fullscreen"))->setChecked(false);
    save(window);
    const QByteArray saved = readBytes(path);
    require(saved.contains("render.1: A") && saved.contains("render.2: M"),
        "Hidden renderer shortcuts were not preserved verbatim");
    require(!saved.contains("hide_legacy_renderers:"),
        "The editor wrote the unchanged default instead of preserving its omission");

    QTemporaryDir toggleDirectory;
    const QStringList filteredRenderers = {
        QStringLiteral("VP Renderer"), QStringLiteral("DirectShow - madVR") };
    QStringList allRenderers = {
        QStringLiteral("VP Renderer"), QStringLiteral("DirectShow - madVR"),
        QStringLiteral("Enhanced Video Renderer") };
    for (int index = 0; index < 128; ++index)
        allRenderers.push_back(QStringLiteral("Cached legacy renderer %1").arg(index));
    ConfigEditorWindow toggleWindow(copyFixture(toggleDirectory), 0, true,
        filteredRenderers, allRenderers);
    require(!QApplication::isEffectEnabled(Qt::UI_AnimateCombo),
        "Config left Qt's delayed combo-box animation enabled");
    QCheckBox* toggle = requireControl<QCheckBox>(toggleWindow,
        QStringLiteral("config.general.hide_legacy_renderers"));
    QComboBox* renderer = requireControl<QComboBox>(toggleWindow,
        QStringLiteral("config.general.renderer"));
    const int legacyIndex = renderer->findData(
        QStringLiteral("Enhanced Video Renderer"));
    auto* rendererList = qobject_cast<QListView*>(renderer->view());
    QLineEdit* legacyShortcut = requireControl<QLineEdit>(toggleWindow,
        QStringLiteral("config.shortcuts.render.3"));
    require(rendererList != nullptr && legacyIndex >= 0 &&
        legacyShortcut->isHidden(),
        "Renderer dropdown does not expose its cached list view");
    toggleWindow.show();
    for (QWidget* ancestor = renderer->parentWidget(); ancestor;
        ancestor = ancestor->parentWidget())
        if (auto* scrollArea = qobject_cast<QScrollArea*>(ancestor))
        {
            scrollArea->ensureWidgetVisible(renderer);
            break;
        }
    QCoreApplication::processEvents();
    require(renderer->isVisibleTo(&toggleWindow),
        "Renderer dropdown is not visible for the first-open latency test");
    QElapsedTimer firstOpenTimer;
    firstOpenTimer.start();
    renderer->showPopup();
    QCoreApplication::processEvents();
    const qint64 firstOpenElapsed = firstOpenTimer.elapsed();
    const std::string firstOpenFailure = QStringLiteral(
        "The cached renderer dropdown still performs deferred work on first open (%1 ms)")
        .arg(firstOpenElapsed).toStdString();
    require(firstOpenElapsed < 100,
        firstOpenFailure.c_str());
    renderer->hidePopup();
    const int cachedRendererCount = renderer->count();
    QElapsedTimer toggleTimer;
    toggleTimer.start();
    toggle->setChecked(false);
    require(toggleTimer.elapsed() < 100 &&
        !rendererList->isRowHidden(legacyIndex) &&
        !legacyShortcut->isHidden(),
        "Showing legacy renderers did not reveal cached UI rows");
    toggleTimer.restart();
    toggle->setChecked(true);
    require(toggleTimer.elapsed() < 100 &&
        rendererList->isRowHidden(legacyIndex) &&
        legacyShortcut->isHidden() &&
        renderer->count() == cachedRendererCount &&
        renderer->currentData().toString() == QStringLiteral("DirectShow - madVR"),
        "Hiding legacy renderers rebuilt the model or changed selection");
}

void testCustomShaderParametersExpandIntoPageScroll()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    QByteArray configuration = readBytes(path);
    const qsizetype section = configuration.indexOf("[shader.nls.standard]\n");
    const qsizetype nextSection = configuration.indexOf("\n[", section + 1);
    require(section >= 0 && nextSection > section,
        "Standard shader section was not found in the fixture");
    QByteArray parameters;
    for (int index = 0; index < 24; ++index)
        parameters += "custom_parameter_" + QByteArray::number(index) + ": value\n";
    configuration.insert(nextSection + 1, parameters);
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Cannot prepare long custom parameter fixture");
    require(file.write(configuration) == configuration.size(),
        "Cannot write long custom parameter fixture");
    file.close();

    ConfigEditorWindow window(path, 0, true);
    window.resize(840, 500);
    window.show();
    QCoreApplication::processEvents();
    QListWidget* modes = requireControl<QListWidget>(window,
        QStringLiteral("config.shader.nls.modes"));
    modes->setCurrentRow(1);
    QToolButton* parameterToggle = requireControl<QToolButton>(window,
        QStringLiteral("config.shader.nls.parameters_toggle"));
    parameterToggle->setChecked(true);
    QCoreApplication::processEvents();
    QTableWidget* table = requireControl<QTableWidget>(window,
        QStringLiteral("config.shader.nls.parameters"));
    require(table->rowCount() >= 24,
        "Long custom parameter list did not load");
    require(table->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
        "Custom parameter table retained an internal vertical scrollbar");
    int rowHeight = 0;
    for (int row = 0; row < table->rowCount(); ++row)
        rowHeight += table->rowHeight(row);
    require(table->height() == table->frameWidth() * 2 +
        table->horizontalHeader()->height() + rowHeight,
        "Custom parameter table did not expand to show every row");

    QScrollArea* pageScroll = nullptr;
    for (QWidget* ancestor = table->parentWidget(); ancestor;
        ancestor = ancestor->parentWidget())
        if (auto* scroll = qobject_cast<QScrollArea*>(ancestor))
        {
            pageScroll = scroll;
            break;
        }
    require(pageScroll && pageScroll->verticalScrollBar()->maximum() > 0,
        "Long custom parameter list did not overflow the containing page");
}

void testUnchangedActiveProfileStatusDoesNotInvalidateLists()
{
    QTemporaryDir directory;
    ConfigEditorWindow window(copyFixture(directory), 0, true);
    auto* shader = requireControl<QListWidget>(window,
        QStringLiteral("config.shader.nls.modes"));
    require(shader->count() >= 3,
        "Shader fixture does not expose enough active-profile rows");

    const QString first = shader->item(1)->data(Qt::UserRole).toString();
    window.setActiveProfileStatusForTesting({}, {}, {}, { first });
    int dataChanges = 0;
    QObject::connect(shader->model(), &QAbstractItemModel::dataChanged,
        [&dataChanges] { ++dataChanges; });

    window.setActiveProfileStatusForTesting({}, {}, {}, { first });
    require(dataChanges == 0,
        "An unchanged active-profile snapshot invalidated the list model");

    const QString second = shader->item(2)->data(Qt::UserRole).toString();
    window.setActiveProfileStatusForTesting({}, {}, {}, { second });
    require(dataChanges > 0,
        "A changed active-profile snapshot did not update the list model");
}

void testLegacyRendererVisibilityFiltersExistingUiImmediately()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    // Commit any fixture migration first so this test observes only the
    // visibility preference's apply classification.
    if (QPushButton* baselineApply = requireControl<QPushButton>(window,
        QStringLiteral("applyConfiguration")); baselineApply->isEnabled())
    {
        baselineApply->click();
        QCoreApplication::processEvents();
    }
    window.setRendererDiscoveryForTesting({
        QStringLiteral("VP Renderer"),
        QStringLiteral("DirectShow - madVR"),
        QStringLiteral("DirectShow - Video Renderer"),
        QStringLiteral("DirectShow - Video Mixing Renderer 9") }, {
        QStringLiteral("VP Renderer"),
        QStringLiteral("DirectShow - madVR") });
    QStackedWidget* pages = requireControl<QStackedWidget>(window,
        QStringLiteral("settingsPages"));
    pages->setCurrentIndex(7);
    QComboBox* renderer = requireControl<QComboBox>(window,
        QStringLiteral("config.general.renderer"));
    QComboBox* actionRenderer = requireControl<QComboBox>(window,
        QStringLiteral("config.actions.renderer"));
    const auto visibleComboItems = [](QComboBox* combo)
    {
        auto* view = qobject_cast<QListView*>(combo->view());
        require(view != nullptr, "Renderer combo does not use a list view");
        int count = 0;
        for (int index = 0; index < combo->count(); ++index)
            if (!view->isRowHidden(index)) ++count;
        return count;
    };
    const auto visibleRendererShortcuts = [&window]
    {
        int count = 0;
        for (QLineEdit* edit : window.findChildren<QLineEdit*>())
            if (edit->objectName().startsWith(QStringLiteral("config.shortcuts.render.")) &&
                !edit->isHidden())
                ++count;
        return count;
    };
    const int hiddenGeneralCount = visibleComboItems(renderer);
    const int hiddenActionCount = visibleComboItems(actionRenderer);
    const int hiddenShortcutCount = visibleRendererShortcuts();
    require(hiddenGeneralCount < renderer->count(),
        "The fixture exposes no hidden legacy renderer rows");
    QCheckBox* hideLegacyRenderers = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.hide_legacy_renderers"));
    QElapsedTimer timer;
    timer.start();
    hideLegacyRenderers->setChecked(false);
    QCoreApplication::processEvents();
    const qint64 elapsedMs = timer.elapsed();

    require(requireControl<QComboBox>(window,
        QStringLiteral("config.general.renderer")) == renderer,
        "Toggling legacy renderer visibility replaced the General selector");
    require(requireControl<QComboBox>(window,
        QStringLiteral("config.actions.renderer")) == actionRenderer,
        "Toggling legacy renderer visibility replaced the Actions selector");
    require(elapsedMs < 250,
        "Filtering the cached renderer UI took 250 ms or longer");
    require(!requireControl<QCheckBox>(window,
        QStringLiteral("config.general.hide_legacy_renderers"))->isChecked(),
        "Immediate renderer filtering lost the checkbox state");
    require(requireControl<QStackedWidget>(window,
        QStringLiteral("settingsPages"))->currentIndex() == 7,
        "Filtering renderer-dependent controls changed the active editor page");
    require(visibleComboItems(renderer) > hiddenGeneralCount,
        "General did not immediately expose the cached legacy renderer rows");
    require(visibleComboItems(actionRenderer) > hiddenActionCount,
        "Actions did not immediately expose the cached renderer rows");
    require(visibleRendererShortcuts() > hiddenShortcutCount,
        "Shortcuts did not immediately expose the cached renderer rows");
    require(!requireControl<QLabel>(window,
        QStringLiteral("configurationEffectSummary"))->text().startsWith(
            QStringLiteral("Restart renderer:")),
        "The UI-only visibility preference still requests a renderer restart");
    require(requireControl<QPushButton>(window,
        QStringLiteral("applyConfiguration"))->isEnabled(),
        "The immediately applied UI preference cannot be persisted");
}

void testGeneralInputApplyPreservesBackendOverrides()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    QFile file(path);
    require(file.open(QIODevice::Append),
        "Could not append the VP Renderer input override fixture");
    file.write("\n[vprenderer.input_processing]\nvideo_conversion: NONE\n");
    file.close();

    ConfigEditorWindow window(path, 0, true);
    // Keep [general] without an explicit conversion default and save an
    // unrelated General edit. The two backend-specific values must remain
    // independent instead of being flattened into the shared default.
    QCheckBox* switchRefreshRate = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.switch_refresh_rate"));
    switchRefreshRate->setChecked(!switchRefreshRate->isChecked());
    const QString effectSummary = requireControl<QLabel>(window,
        QStringLiteral("configurationEffectSummary"))->text();
    const std::string effectFailure = QStringLiteral(
        "General changes are not labelled as a renderer restart: %1")
        .arg(effectSummary).toStdString();
    require(effectSummary.startsWith(QStringLiteral("Restart renderer:")),
        effectFailure.c_str());
    requireControl<QPushButton>(window, QStringLiteral("applyConfiguration"))->click();
    QCoreApplication::processEvents();

    const QByteArray saved = readBytes(path);
    const auto section = [&saved](const QByteArray& name)
    {
        const int begin = saved.indexOf(QByteArray("[") + name + QByteArray("]"));
        const int end = begin < 0 ? -1 : saved.indexOf(QByteArray("\n["), begin + 1);
        return begin < 0 ? QByteArray() : saved.mid(begin,
            end < 0 ? -1 : end - begin);
    };
    require(!section("general").contains("video_conversion:"),
        "Applying an unrelated General change wrote a conversion default");
    require(section("directshow").contains("video_conversion: V210_TO_P010"),
        "Applying a General change flattened the DirectShow conversion override");
    require(section("vprenderer.input_processing").contains("video_conversion: NONE"),
        "Applying a General change flattened the VP Renderer conversion override");
}

void testNewActionStartsUnconfigured()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.selectPage(7);
    window.show();
    QCoreApplication::processEvents();

    QStackedWidget* actionDetails = requireControl<QStackedWidget>(window,
        QStringLiteral("config.actions.details"));
    require(actionDetails->currentIndex() == 1,
        "A configured action did not show its editable details");

    QTimer::singleShot(0, []
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QInputDialog*>(widget))
            {
                dialog->setTextValue(QStringLiteral("Test action"));
                dialog->accept();
                return;
            }
    });
    requireControl<QPushButton>(window, QStringLiteral("config.actions.add"))->click();
    QCoreApplication::processEvents();

    QListWidget* actions = requireControl<QListWidget>(window,
        QStringLiteral("config.actions.items"));
    require(actions->currentItem() && actions->currentItem()->text() ==
        QStringLiteral("Test action"), "New action was not selected");
    require(actionDetails->currentIndex() == 1,
        "The action editor did not appear after adding an action");
    require(!requireControl<QCheckBox>(window,
        QStringLiteral("config.actions.enabled"))->isChecked(),
        "New action was enabled before its draft was complete");
    QListWidget* events = requireControl<QListWidget>(window,
        QStringLiteral("config.actions.on"));
    for (int index = 0; index < events->count(); ++index)
        require(events->item(index)->checkState() == Qt::Unchecked,
            "New action fabricated an event selection");
    require(requireControl<QPlainTextEdit>(window,
        QStringLiteral("config.actions.when"))->toPlainText().isEmpty(),
        "New action fabricated a condition");

    events->item(0)->setCheckState(Qt::Checked);
    requireControl<QLineEdit>(window, QStringLiteral("config.actions.run"))
        ->setText(QStringLiteral("sd"));
    requireControl<QCheckBox>(window, QStringLiteral("config.actions.enabled"))
        ->setChecked(true);
    save(window);
    const QByteArray saved = readBytes(path);
    require(saved.contains("enabled: false") && saved.contains("run: sd"),
        "Incomplete enabled action was not automatically saved as a disabled draft");
    require(!requireControl<QCheckBox>(window,
        QStringLiteral("config.actions.enabled"))->isChecked(),
        "Automatically drafted action still appeared enabled after saving");
}

void testEmptyActionsShowEmptyState()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("VideoProcessor.cfg"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Unable to create empty action fixture");
    file.write("[general]\n");
    file.close();

    ConfigEditorWindow window(path, 0, true);
    window.selectPage(7);
    QCoreApplication::processEvents();
    requireControl<QListWidget>(window, QStringLiteral("config.actions.items"))->clearSelection();
    QStackedWidget* actionDetails = requireControl<QStackedWidget>(window,
        QStringLiteral("config.actions.details"));
    require(actionDetails->currentIndex() == 0,
        "An empty action list showed a disabled draft instead of the empty state");
}

void testMissingConfigurationCanBeCreatedFromEditor()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("VideoProcessor.cfg"));
    require(!QFileInfo::exists(path), "Missing-config fixture already exists");
    ConfigEditorWindow window(path, 0, true);
    window.show();
    QCoreApplication::processEvents();

    QPushButton* ok = requireControl<QPushButton>(window,
        QStringLiteral("okConfiguration"));
    QPushButton* apply = requireControl<QPushButton>(window,
        QStringLiteral("applyConfiguration"));
    QLabel* status = requireControl<QLabel>(window,
        QStringLiteral("configurationStatus"));
    require(ok->isEnabled() && apply->isEnabled(),
        "Missing configuration was not exposed as a creatable document");
    require(!status->text().contains(QStringLiteral("not loaded"),
        Qt::CaseInsensitive), "Missing configuration still reports a load failure");

    apply->click();
    QCoreApplication::processEvents();
    require(QFileInfo::exists(path),
        "Apply did not create the missing VideoProcessor.cfg");
    require(status->text().contains(QStringLiteral("created safely"),
        Qt::CaseInsensitive), "First save did not report configuration creation");
    require(!apply->isEnabled(),
        "First save left the newly created configuration dirty");
}

void testApplyOkCancelContract()
{
    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, 0, true);
        window.show();
        QCoreApplication::processEvents();

        QPushButton* ok = requireControl<QPushButton>(window,
            QStringLiteral("okConfiguration"));
        QPushButton* cancel = requireControl<QPushButton>(window,
            QStringLiteral("cancelConfiguration"));
        QPushButton* apply = requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"));
        QWidget* footer = requireControl<QWidget>(window, QStringLiteral("footer"));
        QList<QPushButton*> footerButtons;
        for (int index = 0; index < footer->layout()->count(); ++index)
            if (auto* button = qobject_cast<QPushButton*>(
                footer->layout()->itemAt(index)->widget()))
                footerButtons.push_back(button);
        require(footerButtons.size() == 3 && footerButtons[0] == ok &&
            footerButtons[1] == cancel && footerButtons[2] == apply,
            "The footer does not contain exactly OK, Cancel, Apply in order");
        require(window.findChild<QPushButton*>(QStringLiteral("reloadConfiguration")) == nullptr &&
            window.findChild<QPushButton*>(QStringLiteral("validateConfiguration")) == nullptr,
            "Reload or Validate is still exposed in the editor footer");
        // The deployed fixture intentionally exercises migrations; commit
        // those first so this test begins at its last successfully saved form.
        if (apply->isEnabled())
        {
            apply->click();
            QCoreApplication::processEvents();
        }
        require(!apply->isEnabled(), "Apply was enabled for a clean document");

        QCheckBox* fullscreen = requireControl<QCheckBox>(window,
            QStringLiteral("config.general.fullscreen"));
        fullscreen->setChecked(!fullscreen->isChecked());
        require(apply->isEnabled(), "Apply was not enabled by an edit");
        QLabel* effect = requireControl<QLabel>(window,
            QStringLiteral("configurationEffectSummary"));
        require(effect->text() == QStringLiteral(
            "Takes effect next start: Startup / input"),
            "The pending effect summary did not honestly classify the edit");

        apply->click();
        QCoreApplication::processEvents();
        require(window.isVisible(), "Apply closed the editor");
        require(!apply->isEnabled(), "Apply remained enabled after a successful save");
        require(effect->text() == QStringLiteral("No pending changes"),
            "The effect summary did not remain visible after Apply");
        require(requireControl<QLabel>(window,
            QStringLiteral("configurationStatus"))->text().contains(
                QStringLiteral("Takes effect when VideoProcessor next starts")),
            "VP-absent Apply did not report the next-start behavior");

        // A clean OK closes without rewriting the file or creating another
        // backup/runtime notification.
        const QStringList backupsBefore = QDir(directory.path()).entryList(
            { QStringLiteral("VideoProcessor.cfg.backup-*") }, QDir::Files);
        ok->click();
        QCoreApplication::processEvents();
        require(!window.isVisible(), "Clean OK did not close the editor");
        require(QDir(directory.path()).entryList({ QStringLiteral("VideoProcessor.cfg.backup-*") },
            QDir::Files) == backupsBefore,
            "Clean OK performed an unnecessary save");
    }

    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, 0, true);
        window.show();
        QCoreApplication::processEvents();
        QPushButton* apply = requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"));
        if (apply->isEnabled())
        {
            apply->click();
            QCoreApplication::processEvents();
        }
        const QByteArray original = readBytes(path);
        QCheckBox* fullscreen = requireControl<QCheckBox>(window,
            QStringLiteral("config.general.fullscreen"));
        const bool initial = fullscreen->isChecked();
        fullscreen->setChecked(!initial);
        requireControl<QPushButton>(window,
            QStringLiteral("cancelConfiguration"))->click();
        QCoreApplication::processEvents();
        require(!window.isVisible(), "Cancel did not close the editor");
        require(readBytes(path) == original, "Cancel changed the configuration file");
        window.show();
        QCoreApplication::processEvents();
        require(requireControl<QCheckBox>(window,
            QStringLiteral("config.general.fullscreen"))->isChecked() == initial,
            "Cancel retained the discarded working-copy value");
        require(!requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"))->isEnabled(),
            "Cancel retained a dirty working copy");
    }

    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, 0, true);
        window.show();
        QCoreApplication::processEvents();
        QPushButton* apply = requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"));
        if (apply->isEnabled())
        {
            apply->click();
            QCoreApplication::processEvents();
        }
        QCheckBox* fullscreen = requireControl<QCheckBox>(window,
            QStringLiteral("config.general.fullscreen"));
        fullscreen->setChecked(!fullscreen->isChecked());
        QFile external(path);
        require(external.open(QIODevice::Append), "Cannot create external-save conflict");
        external.write("\n# external edit\n");
        external.close();
        const QByteArray externallyEdited = readBytes(path);
        requireControl<QPushButton>(window,
            QStringLiteral("okConfiguration"))->click();
        QCoreApplication::processEvents();
        require(window.isVisible(), "OK closed after a failed safe save");
        require(readBytes(path) == externallyEdited,
            "Failed OK overwrote the external configuration change");
        require(requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"))->isEnabled(),
            "Failed OK cleared the dirty state");
    }

    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, 0, true);
        window.show();
        QCoreApplication::processEvents();
        QPushButton* apply = requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"));
        if (apply->isEnabled()) save(window);
        const QByteArray before = readBytes(path);
        QCheckBox* sceneDetection = requireControl<QCheckBox>(window,
            QStringLiteral("config.general.scene_detect"));
        sceneDetection->setChecked(!sceneDetection->isChecked());
        requireControl<QPushButton>(window,
            QStringLiteral("okConfiguration"))->click();
        QCoreApplication::processEvents();
        require(!window.isVisible() && readBytes(path) != before,
            "Dirty OK did not commit successfully before closing");
    }
}

void testDirectShowOnlyEffectDoesNotRestartAlpha()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    QByteArray contents = readBytes(path);
    contents.replace("renderer: DirectShow - madVR", "renderer: VP Renderer");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "Cannot create Alpha renderer fixture");
    require(file.write(contents) == contents.size(),
        "Cannot write Alpha renderer fixture");
    file.close();

    ConfigEditorWindow window(path, 0, true);
    QPushButton* apply = requireControl<QPushButton>(window,
        QStringLiteral("applyConfiguration"));
    if (apply->isEnabled())
    {
        apply->click();
        QCoreApplication::processEvents();
    }
    QSpinBox* offset = requireControl<QSpinBox>(window,
        QStringLiteral("config.directshow.frame_offset.value"));
    offset->setValue(offset->value() + 1);
    QLabel* effect = requireControl<QLabel>(window,
        QStringLiteral("configurationEffectSummary"));
    require(effect->text() == QStringLiteral("Takes effect next start: DirectShow"),
        "A DirectShow-only edit incorrectly promised to restart Alpha");
}

void testInvalidRendererIsRejectedContinuously()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.reveal();
    QCoreApplication::processEvents();

    QPushButton* apply = requireControl<QPushButton>(window,
        QStringLiteral("applyConfiguration"));
    if (apply->isEnabled())
    {
        apply->click();
        QCoreApplication::processEvents();
    }
    const QByteArray saved = readBytes(path);
    QPushButton* ok = requireControl<QPushButton>(window,
        QStringLiteral("okConfiguration"));
    QComboBox* renderer = requireControl<QComboBox>(window,
        QStringLiteral("config.general.renderer"));
    QLabel* status = requireControl<QLabel>(window,
        QStringLiteral("configurationStatus"));

    require(!renderer->isEditable(),
        "Renderer discovery selector still accepts arbitrary typed values");
    renderer->addItem(QStringLiteral("DirectShow - madVR1a"),
        QStringLiteral("DirectShow - madVR1a"));
    selectData(renderer, QStringLiteral("DirectShow - madVR1a"));
    QCoreApplication::processEvents();
    require(status->text().contains(QStringLiteral("renderer 'DirectShow - madVR1a' was not discovered")) &&
        status->styleSheet().contains(QStringLiteral("#ff8d86")),
        "An invalid renderer did not immediately show the first validation error in red");
    require(!ok->isEnabled() && !apply->isEnabled(),
        "OK or Apply remained enabled for an invalid renderer");

    ok->click();
    apply->click();
    QCoreApplication::processEvents();
    require(window.isVisible() && readBytes(path) == saved,
        "An invalid renderer candidate was saved or closed the editor");

    selectData(renderer, QStringLiteral("VP Renderer"));
    require(ok->isEnabled() && apply->isEnabled(),
        "Correcting the renderer did not restore OK and Apply");
    require(!status->styleSheet().contains(QStringLiteral("#ff8d86")),
        "Correcting the renderer did not clear the validation error styling");
}

void testShortcutEffectsAreClassifiedLive()
{
    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, 0, true);
        QPushButton* apply = requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"));
        if (apply->isEnabled()) save(window);

        requireControl<QLineEdit>(window,
            QStringLiteral("config.shortcuts.fullscreen_toggle"))
            ->setText(QStringLiteral("Ctrl+Alt+F11"));
        QLabel* effect = requireControl<QLabel>(window,
            QStringLiteral("configurationEffectSummary"));
        require(effect->text() == QStringLiteral("Apply shortcuts live: Shortcuts"),
            "A shortcut-only edit was not classified for live shortcut replacement");

        QSpinBox* queueSize = requireControl<QSpinBox>(window,
            QStringLiteral("config.queue.queue_size"));
        queueSize->setValue(queueSize->value() + 1);
        require(effect->text().startsWith(QStringLiteral("Reset queues:")) &&
            effect->text().contains(QStringLiteral("Queue")) &&
            effect->text().contains(QStringLiteral("Shortcuts")),
            "A mixed queue and shortcut edit did not show the strongest action");
    }

    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, 0, true);
        QPushButton* apply = requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"));
        if (apply->isEnabled()) save(window);

        requireControl<QLineEdit>(window, QStringLiteral("config.queue.shortcut"))
            ->setText(QStringLiteral("Ctrl+Alt+F10"));
        require(requireControl<QLabel>(window,
            QStringLiteral("configurationEffectSummary"))->text() ==
                QStringLiteral("Apply shortcuts live: Queue"),
            "A queue-profile shortcut-only edit was misclassified as a queue reset");
    }
}

void testDisplayWarningAndErrorPrecedence()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.reveal();
    QCoreApplication::processEvents();

    QLabel* status = requireControl<QLabel>(window,
        QStringLiteral("configurationStatus"));
    QPushButton* ok = requireControl<QPushButton>(window,
        QStringLiteral("okConfiguration"));
    QPushButton* apply = requireControl<QPushButton>(window,
        QStringLiteral("applyConfiguration"));
    require(status->text() == QStringLiteral("Warning: Display 'EPSON PJ' is not currently discoverable.") &&
        status->styleSheet().contains(QStringLiteral("#e0b45c")) && ok->isEnabled(),
        "An undiscovered display was not presented as a non-blocking advisory warning");

    QComboBox* renderer = requireControl<QComboBox>(window,
        QStringLiteral("config.general.renderer"));
    renderer->addItem(QStringLiteral("DirectShow - invalid"),
        QStringLiteral("DirectShow - invalid"));
    selectData(renderer, QStringLiteral("DirectShow - invalid"));
    require(status->text().contains(QStringLiteral("renderer 'DirectShow - invalid'")) &&
        !status->text().contains(QStringLiteral("Warning: Display")) &&
        status->styleSheet().contains(QStringLiteral("#ff8d86")) &&
        !ok->isEnabled() && !apply->isEnabled(),
        "A validation error did not override the display warning");

    selectData(renderer, QStringLiteral("DirectShow - madVR"));
    require(status->text().contains(QStringLiteral("Warning: Display 'EPSON PJ'")) &&
        status->styleSheet().contains(QStringLiteral("#e0b45c")) &&
        ok->isEnabled(),
        "Fixing all errors did not resurface the advisory display warning");
}

void testVideoOnlyStartupDefaultRoundTrips()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    QPushButton* apply = requireControl<QPushButton>(window,
        QStringLiteral("applyConfiguration"));
    if (apply->isEnabled()) save(window);

    QCheckBox* fullscreen = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.fullscreen"));
    QCheckBox* videoOnly = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.noui"));
    require(videoOnly->text() == QStringLiteral("Start as Video Only") &&
        videoOnly->parentWidget() == fullscreen->parentWidget(),
        "Start as Video Only is missing from the startup presentation controls");

    videoOnly->setChecked(true);
    QLabel* effect = requireControl<QLabel>(window,
        QStringLiteral("configurationEffectSummary"));
    require(effect->text() == QStringLiteral(
        "Takes effect next start: Startup / input"),
        "Video Only did not report next-start-only behavior");
    save(window);
    require(readBytes(path).contains("noui: true"),
        "Video Only did not persist the canonical [general] noui key");

    ConfigEditorWindow reopened(path, 0, true);
    require(requireControl<QCheckBox>(reopened,
        QStringLiteral("config.general.noui"))->isChecked(),
        "Video Only did not round-trip through a reopened editor");
}

void testRemainingEffectSummaryPrecedence()
{
    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, 0, true);
        QPushButton* apply = requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"));
        if (apply->isEnabled()) save(window);
        QLabel* effect = requireControl<QLabel>(window,
            QStringLiteral("configurationEffectSummary"));

        QSpinBox* queueSize = requireControl<QSpinBox>(window,
            QStringLiteral("config.queue.queue_size"));
        const int originalQueueSize = queueSize->value();
        queueSize->setValue(originalQueueSize + 1);
        require(effect->text() == QStringLiteral("Reset queues: Queue"),
            "A queue-only edit did not report a queue reset");
        queueSize->setValue(originalQueueSize);
        require(effect->text() == QStringLiteral("No pending changes") &&
            !apply->isEnabled(),
            "Reverting a queue edit did not clear its pending effect");

        QCheckBox* logging = requireControl<QCheckBox>(window,
            QStringLiteral("config.logging.enabled"));
        logging->setChecked(!logging->isChecked());
        require(effect->text() == QStringLiteral(
            "Takes effect next start: Logging"),
            "A logging-only edit did not report next-start behavior");
    }

    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, 0, true);
        QPushButton* apply = requireControl<QPushButton>(window,
            QStringLiteral("applyConfiguration"));
        if (apply->isEnabled()) save(window);
        requireControl<QLineEdit>(window,
            QStringLiteral("config.shortcuts.fullscreen_toggle"))
            ->setText(QStringLiteral("Ctrl+Alt+F11"));
        selectData(requireControl<QComboBox>(window,
            QStringLiteral("config.general.renderer")),
            QStringLiteral("VP Renderer"));
        QLabel* effect = requireControl<QLabel>(window,
            QStringLiteral("configurationEffectSummary"));
        require(effect->text().startsWith(QStringLiteral("Restart renderer:")) &&
            effect->text().contains(QStringLiteral("Shortcuts")) &&
            effect->text().contains(QStringLiteral("Startup / input")),
            "A renderer plus shortcut edit did not report the strongest restart action");
    }
}

QString accessibleName(QWidget* widget)
{
    QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(widget);
    return interface ? interface->text(QAccessible::Name).trimmed() : QString();
}

QShortcut* shortcutFor(ConfigEditorWindow& window, const QKeySequence& sequence)
{
    for (QShortcut* shortcut : window.findChildren<QShortcut*>())
        if (shortcut->key() == sequence) return shortcut;
    return nullptr;
}

void testDpiKeyboardAndAccessibilityBehavior()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.show();
    window.resize(1040, 700);
    QCoreApplication::processEvents();

    require(window.minimumWidth() == 1040 && window.minimumHeight() == 700,
        "Minimum window size no longer preserves the usable dashboard layout");
    QStackedWidget* pages = requireControl<QStackedWidget>(window,
        QStringLiteral("settingsPages"));
    require(pages->currentIndex() == 0, "General was not the initial page");

    QShortcut* next = shortcutFor(window, QKeySequence(QStringLiteral("Ctrl+Tab")));
    QShortcut* previous = shortcutFor(window, QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));
    QShortcut* saveKey = shortcutFor(window, QKeySequence::Save);
    require(next && previous && saveKey, "Required keyboard shortcuts are missing");
    QMetaObject::invokeMethod(next, "activated", Qt::DirectConnection);
    require(pages->currentIndex() == 1, "Ctrl+Tab did not select the next settings page");
    QMetaObject::invokeMethod(previous, "activated", Qt::DirectConnection);
    require(pages->currentIndex() == 0, "Ctrl+Shift+Tab did not select the previous settings page");

    QComboBox* capture = requireControl<QComboBox>(window,
        QStringLiteral("config.general.capture_device"));
    require((capture->focusPolicy() & Qt::TabFocus) != 0,
        "A primary field cannot receive keyboard focus");
    require(!accessibleName(capture).isEmpty(),
        "Capture device field has no accessible name");
    require(!accessibleName(requireControl<QLabel>(window,
        QStringLiteral("configurationStatus"))).isEmpty(),
        "Configuration status has no accessible name");

    window.selectPage(1);
    QCoreApplication::processEvents();
    QListWidget* queueProfiles = requireControl<QListWidget>(window,
        QStringLiteral("config.queue.profiles"));
    require(!accessibleName(queueProfiles).isEmpty(),
        "Queue profile list has no accessible name");
    QSplitter* queueSplitter = nullptr;
    for (QSplitter* splitter : window.findChildren<QSplitter*>())
        if (splitter->isVisible()) { queueSplitter = splitter; break; }
    require(queueSplitter && queueSplitter->orientation() == Qt::Horizontal,
        "Profile layout did not remain side-by-side at the supported minimum size");

    window.resize(1120, 760);
    QCoreApplication::processEvents();
    require(queueSplitter->orientation() == Qt::Horizontal,
        "Profile layout did not return to side-by-side at normal width");

    for (int page = 0; page < pages->count(); ++page)
    {
        window.selectPage(page);
        QCoreApplication::processEvents();
        for (QWidget* control : pages->currentWidget()->findChildren<QWidget*>())
        {
            if (!control->isVisibleTo(&window) || !control->isEnabled() ||
                (control->focusPolicy() & Qt::TabFocus) == 0 ||
                !control->objectName().startsWith(QStringLiteral("config.")))
                continue;
            if (accessibleName(control).isEmpty())
                throw std::runtime_error(QStringLiteral("Focusable control has no accessible name: %1")
                    .arg(control->objectName()).toStdString());
        }
    }
}

void testPrimaryDiscoverySelectorsOpenOnFieldClick()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.show();
    window.selectPage(0);
    QCoreApplication::processEvents();

    for (const QString& name : {
        QStringLiteral("config.general.capture_device"),
        QStringLiteral("config.general.fullscreen_monitor_name"),
        QStringLiteral("config.general.renderer") })
    {
        QComboBox* selector = requireControl<QComboBox>(window, name);
        require(!selector->isEditable(),
            "A discovery selector still behaves like a text editor");
        require(selector->lineEdit() == nullptr,
            "A discovery selector still owns an embedded text editor");
        require(selector->isVisibleTo(&window) && selector->isEnabled(),
            "A discovery selector is not available for direct interaction");

        const QPoint point = selector->rect().center();
        const QPoint globalPoint = selector->mapToGlobal(point);
        const ULONGLONG started = GetTickCount64();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point),
            QPointF(globalPoint), Qt::LeftButton, Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(selector, &press);
        QCoreApplication::processEvents();
        QWidget* popup = selector->view()->window();
        const ULONGLONG elapsed = GetTickCount64() - started;
        require(popup && popup->isVisible(),
            "Clicking the discovery selector field did not open its popup");
        require(elapsed < 250,
            "Discovery selector popup did not open promptly");
        selector->hidePopup();
        QCoreApplication::processEvents();
    }
}

void testOrdinaryEditsDoNotRequireDiskValidation()
{
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create validation test directory");
    const QString candidateDirectory =
        directory.filePath(QStringLiteral("removed-before-edit"));
    require(QDir().mkpath(candidateDirectory),
        "Cannot create validation candidate directory");
    const QString path = candidateDirectory +
        QStringLiteral("/VideoProcessor.cfg");
    require(QFile::copy(repositoryPath(QStringLiteral(
        "test-fixtures/deployed-VideoProcessor-20260807-current.cfg")), path),
        "Cannot copy validation candidate fixture");

    ConfigEditorWindow window(path, 0, true);
    const QString parked = directory.filePath(QStringLiteral("parked.cfg"));
    require(QFile::rename(path, parked) &&
        QDir().rmdir(candidateDirectory),
        "Cannot remove the candidate directory after loading");

    QCheckBox* fullscreen = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.fullscreen"));
    fullscreen->setChecked(!fullscreen->isChecked());
    QCoreApplication::processEvents();
    require(requireControl<QPushButton>(window,
        QStringLiteral("applyConfiguration"))->isEnabled(),
        "An ordinary edit synchronously required a disk validation copy");
    require(!requireControl<QLabel>(window,
        QStringLiteral("configurationStatus"))->text().contains(
            QStringLiteral("validation copy"), Qt::CaseInsensitive),
        "Interactive validation attempted disk-backed core validation");
}

void testWarmRevealPathGrantsForegroundPermission()
{
    const QByteArray source = readBytes(repositoryPath(QStringLiteral(
        "src/VideoProcessor-GUI/VideoProcessorDlg.cpp")));
    const qsizetype functionStart = source.indexOf(
        "bool SignalConfigurationEditorReveal(DWORD processId)");
    const qsizetype functionEnd = source.indexOf(
        "bool SendConfigurationEditorRevealOnce", functionStart);
    require(functionStart >= 0 && functionEnd > functionStart,
        "Cannot locate the stable Config reveal path");
    const QByteArray revealPath = source.mid(functionStart,
        functionEnd - functionStart);
    const qsizetype grant = revealPath.indexOf(
        "AllowSetForegroundWindow(processId)");
    const qsizetype signal = revealPath.indexOf("SetEvent(eventHandle)");
    require(grant >= 0 && signal > grant,
        "Warm Config reveal does not grant foreground permission before signaling");
}

void testNativeOwnerPreservesQtInputAndPopupAssociation()
{
    if (QGuiApplication::platformName().compare(
        QStringLiteral("windows"), Qt::CaseInsensitive) != 0)
        return;

    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"VP test owner",
        WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    require(owner != nullptr, "Cannot create native VP owner fixture");
    testAdvertisedEditor = nullptr;
    testOwnerOriginalProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        owner, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(testOwnerProcedure)));
    require(testOwnerOriginalProcedure != nullptr,
        "Cannot install native VP owner association fixture");
    ShowWindow(owner, SW_SHOWNOACTIVATE);
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, reinterpret_cast<quintptr>(owner), true);
    window.reveal();
    QCoreApplication::processEvents();

    HWND editor = reinterpret_cast<HWND>(window.effectiveWinId());
    const HWND actualOwner = GetWindow(editor, GW_OWNER);
    if (actualOwner != nullptr)
        throw std::runtime_error(QStringLiteral(
            "Config editor unexpectedly acquired a native owner: editor=%1 expected=%2 actual=%3")
            .arg(reinterpret_cast<quintptr>(editor), 0, 16)
            .arg(static_cast<quintptr>(0), 0, 16)
            .arg(reinterpret_cast<quintptr>(actualOwner), 0, 16)
            .toStdString());
    require((GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
        "Explicit reveal did not apply scoped topmost above fullscreen");
    require(appearsAbove(editor, owner),
        "Visible VP-associated Config was not above its video owner fixture");
	HWND competingTopmost = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"VP fullscreen cover fixture",
		WS_POPUP, 0, 0, 320, 200, nullptr, nullptr,
		GetModuleHandleW(nullptr), nullptr);
	require(competingTopmost != nullptr,
		"Cannot create competing topmost renderer fixture");
	ShowWindow(competingTopmost, SW_SHOWNOACTIVATE);
	SetWindowPos(competingTopmost, HWND_TOPMOST, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	require(appearsAbove(competingTopmost, editor),
		"Competing topmost fixture did not cover Config before the retry");
	window.reveal();
	QCoreApplication::processEvents();
	require(appearsAbove(editor, competingTopmost),
		"Repeated explicit reveal did not force Config back to the top");
	DestroyWindow(competingTopmost);
    require(testAdvertisedEditor == editor,
        "Config editor did not advertise its current native top-level HWND");

    QLineEdit* shortcut = requireControl<QLineEdit>(window,
        QStringLiteral("config.shortcuts.fullscreen_toggle"));
    shortcut->setFocus();
    shortcut->selectAll();
    QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier,
        QStringLiteral("z"));
    QApplication::sendEvent(shortcut, &keyPress);
    require(shortcut->text() == QStringLiteral("z"),
        "Native association interfered with normal Qt edit keyboard input");

    QComboBox* renderer = requireControl<QComboBox>(window,
        QStringLiteral("config.general.video_conversion"));
    for (QWidget* ancestor = renderer->parentWidget(); ancestor;
        ancestor = ancestor->parentWidget())
    {
        if (auto* scrollArea = qobject_cast<QScrollArea*>(ancestor))
        {
            scrollArea->ensureWidgetVisible(renderer);
            break;
        }
    }
    QCoreApplication::processEvents();
    require(renderer->isVisibleTo(&window),
        "Real Video conversion dropdown is not visible in Config");
    SetForegroundWindow(editor);
    window.raise();
    window.activateWindow();
    renderer->setFocus();
    QCoreApplication::processEvents();
    SetForegroundWindow(editor);
    QCoreApplication::processEvents();
    renderer->showPopup();
    const ULONGLONG openDeadline = GetTickCount64() + 500;
    while (!renderer->view()->window()->isVisible() &&
        GetTickCount64() < openDeadline)
    {
        QCoreApplication::processEvents();
        Sleep(10);
    }
    QWidget* popup = renderer->view()->window();
    require(popup != nullptr,
        "Native association did not preserve the Qt combo popup window");
    require(popup->isVisible(), "Renderer popup did not open for lease test");
    const ULONGLONG popupDeadline = GetTickCount64() + 1100;
    while (GetTickCount64() < popupDeadline)
    {
        QCoreApplication::processEvents();
        Sleep(20);
    }
    const HWND popupWindow = reinterpret_cast<HWND>(popup->effectiveWinId());
    require(popup->isVisible() && popupWindow && IsWindow(popupWindow),
        "Natural owner chain did not preserve the native combo popup");
    require(renderer->count() > 1,
        "Video conversion dropdown has no alternate mouse choice");
    const int previousRenderer = renderer->currentIndex();
    const int selectedRenderer = previousRenderer == 0 ? 1 : 0;
    const QModelIndex selectedIndex = renderer->model()->index(selectedRenderer,
        renderer->modelColumn(), renderer->rootModelIndex());
    const QPoint clickPoint = renderer->view()->visualRect(selectedIndex).center();
    QWidget* viewport = renderer->view()->viewport();
    const HWND viewportWindow = reinterpret_cast<HWND>(viewport->winId());
    SendMessageW(viewportWindow, WM_MOUSEMOVE, 0,
        MAKELPARAM(clickPoint.x(), clickPoint.y()));
    SendMessageW(viewportWindow, WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(clickPoint.x(), clickPoint.y()));
    SendMessageW(viewportWindow, WM_LBUTTONUP, 0,
        MAKELPARAM(clickPoint.x(), clickPoint.y()));
    QCoreApplication::processEvents();
    require(renderer->currentIndex() == selectedRenderer && !popup->isVisible(),
        "Mouse click could not select and close the real Video conversion dropdown");
    require((GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
        "Main editor lost scoped topmost during popup selection");

    QComboBox* hdrColorspace = requireControl<QComboBox>(window,
        QStringLiteral("config.general.hdr_colorspace"));
    require(hdrColorspace->isVisibleTo(&window),
        "Real HDR color space dropdown was not accessible after mouse selection");
    hdrColorspace->setFocus();
    QKeyEvent openPopup(QEvent::KeyPress, Qt::Key_Down, Qt::AltModifier);
    QApplication::sendEvent(hdrColorspace, &openPopup);
    QCoreApplication::processEvents();
    QWidget* hdrPopup = hdrColorspace->view()->window();
    require(hdrPopup && hdrPopup->isVisible(),
        "Keyboard could not open the real HDR color space dropdown");
    QKeyEvent navigatePopup(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(hdrColorspace->view(), &navigatePopup);
    QKeyEvent escapePopup(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(hdrColorspace->view(), &escapePopup);
    QCoreApplication::processEvents();
    require(!hdrPopup->isVisible(),
        "Keyboard Escape did not close the real HDR color space dropdown normally");
    renderer->showPopup();
    QCoreApplication::processEvents();
    require(popup->isVisible(), "Renderer popup did not reopen for activation cleanup");
    const UINT activationMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.Activate.v1");
    DWORD_PTR acknowledged = 0;
    require(SendMessageTimeoutW(editor, activationMessage,
        GetCurrentProcessId(), reinterpret_cast<LPARAM>(owner),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &acknowledged) &&
        acknowledged == 1,
        "Activation was not acknowledged before queuing reveal");
    QCoreApplication::processEvents();
    require(window.isVisible() && !popup->isVisible(),
        "Queued activation did not close the stale combo popup and reveal");

    window.hide();
    require((GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0 &&
        GetWindow(editor, GW_OWNER) == nullptr,
        "Hiding Config did not clear its native owner and normal z-order state");
    acknowledged = 0;
    require(SendMessageTimeoutW(editor, activationMessage,
        GetCurrentProcessId(), reinterpret_cast<LPARAM>(owner),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &acknowledged) &&
        acknowledged == 1 && !window.isVisible(),
        "Repeated activation did not acknowledge before deferred reveal");
    QCoreApplication::processEvents();
    require(window.isVisible(), "Deferred repeated activation did not reveal the editor");
    window.close();
    require(!window.isVisible() &&
        (GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
        "Closing Config did not remove its temporary topmost state");
    window.reveal();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    editor = reinterpret_cast<HWND>(window.effectiveWinId());
    require(window.isVisible() &&
        (GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
        "Explicit reveal did not restore scoped topmost state");

    // Force qwindows to destroy and recreate the native top level. VP must be
    // told the effective replacement HWND before the next activation.
    window.hide();
    window.setWindowFlag(Qt::Tool, true);
    window.show();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    editor = reinterpret_cast<HWND>(window.effectiveWinId());
    require(editor && IsWindow(editor) && testAdvertisedEditor == editor,
        "WinId recreation did not publish the replacement editor HWND");
    acknowledged = 0;
    require(SendMessageTimeoutW(testAdvertisedEditor, activationMessage,
        GetCurrentProcessId(), reinterpret_cast<LPARAM>(owner),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &acknowledged) &&
        acknowledged == 1,
        "Activation through the advertised replacement HWND was not acknowledged");
    QCoreApplication::processEvents();
    require(window.isVisible(),
        "Activation through the advertised replacement HWND did not reveal");
    shortcut->setFocus();
    shortcut->selectAll();
    QKeyEvent secondKeyPress(QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier,
        QStringLiteral("y"));
    QApplication::sendEvent(shortcut, &secondKeyPress);
    require(shortcut->text() == QStringLiteral("y"),
        "Repeated activation interfered with normal Qt keyboard focus");

    HWND replacementHost = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"STATIC", L"replacement VP fullscreen host", WS_OVERLAPPEDWINDOW,
        0, 0, 320, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(replacementHost != nullptr, "Cannot create replacement video host fixture");
    ShowWindow(replacementHost, SW_SHOWNOACTIVATE);
    acknowledged = 0;
    require(SendMessageTimeoutW(editor, activationMessage,
        GetCurrentProcessId(), reinterpret_cast<LPARAM>(replacementHost),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &acknowledged) && acknowledged == 1,
        "Config did not acknowledge replacement VP host association");
    QCoreApplication::processEvents();
    editor = reinterpret_cast<HWND>(window.effectiveWinId());
    const UINT targetMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.PresentationTarget.v1");
    DWORD_PTR targetAcknowledged = 0;
    require(SendMessageTimeoutW(editor, targetMessage, GetCurrentProcessId(),
        reinterpret_cast<LPARAM>(replacementHost),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &targetAcknowledged) &&
        targetAcknowledged == 1,
        "Replacement presentation target was not accepted");
    QCoreApplication::processEvents();
    require(GetWindow(editor, GW_OWNER) == nullptr &&
        (GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
        "Config did not remain independently topmost after host replacement");
    SetWindowPos(replacementHost, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	window.reveal();
    QCoreApplication::processEvents();
    require(appearsAbove(editor, replacementHost),
		"Explicit reveal did not restore Config above the video host");
	const UINT reassertMessage = RegisterWindowMessageW(
		L"VideoProcessor.ConfigEditor.Reassert.v1");
	const BOOL foregroundChanged = SetForegroundWindow(owner);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
	require((GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
        "External foreground unexpectedly removed visible Config topmost state");
	if (foregroundChanged)
		require(GetForegroundWindow() == owner,
			"Windows accepted the foreground change but Config immediately stole it back");

    // Renderer/host lifecycle traffic may reassert z-order while Config is
    // visible, but it must not steal foreground from the user's current app.
    PostMessageW(editor, reassertMessage, GetCurrentProcessId(),
        reinterpret_cast<LPARAM>(replacementHost));
    Sleep(100);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
	require((GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
        "Lifecycle reassert stole foreground or removed visible Config topmost state");
	if (foregroundChanged)
		require(GetForegroundWindow() == owner,
			"Lifecycle reassert stole an established external foreground");

    window.reveal();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    editor = reinterpret_cast<HWND>(window.effectiveWinId());
    require((GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
        "Explicit reveal did not reapply scoped topmost after a gated lifecycle reassert");
    window.hide();
    QCoreApplication::processEvents();
    require((GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
        "Hiding Config after explicit reveal did not remove scoped topmost state");
    DestroyWindow(replacementHost);
    Sleep(250);
    QCoreApplication::processEvents();
    require((GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
        "Destroyed VP owner left Config in the topmost band");
    window.hide();
    SetWindowLongPtrW(owner, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(testOwnerOriginalProcedure));
    testOwnerOriginalProcedure = nullptr;
    testAdvertisedEditor = nullptr;
    DestroyWindow(owner);
}

void testRealConfigurationDropdownRemainsClickable()
{
    const bool nativeWindows = QGuiApplication::platformName().compare(
        QStringLiteral("windows"), Qt::CaseInsensitive) == 0;
    HWND owner = nativeWindows ? CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
        L"VP dropdown owner", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) : nullptr;
    require(!nativeWindows || owner != nullptr,
        "Cannot create dropdown owner fixture");
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, reinterpret_cast<quintptr>(owner), true);
    window.show();
    QCoreApplication::processEvents();

    QComboBox* conversion = requireControl<QComboBox>(window,
        QStringLiteral("config.general.video_conversion"));
    for (QWidget* ancestor = conversion->parentWidget(); ancestor;
        ancestor = ancestor->parentWidget())
    {
        if (auto* scrollArea = qobject_cast<QScrollArea*>(ancestor))
        {
            scrollArea->ensureWidgetVisible(conversion);
            break;
        }
    }
    QCoreApplication::processEvents();
    require(conversion->isVisibleTo(&window) && conversion->count() > 1,
        "Real Video conversion dropdown is unavailable");
    conversion->showPopup();
    QCoreApplication::processEvents();
    QWidget* conversionPopup = conversion->view()->window();
    require(conversionPopup && conversionPopup->isVisible(),
        "Real Video conversion dropdown did not open");
	HWND firstPresentationTarget = nullptr;
	HWND secondPresentationTarget = nullptr;
	if (nativeWindows)
	{
		firstPresentationTarget = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
			L"VP transient renderer one", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200,
			nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		secondPresentationTarget = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
			L"VP transient renderer two", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200,
			nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		require(firstPresentationTarget && secondPresentationTarget,
			"Cannot create transient renderer fixtures");
		const UINT targetMessage = RegisterWindowMessageW(
			L"VideoProcessor.ConfigEditor.PresentationTarget.v1");
		for (const HWND target : { firstPresentationTarget,
			secondPresentationTarget })
		{
			DWORD_PTR acknowledged = 0;
			require(SendMessageTimeoutW(
				reinterpret_cast<HWND>(window.effectiveWinId()), targetMessage,
				GetCurrentProcessId(), reinterpret_cast<LPARAM>(target),
				SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &acknowledged) &&
				acknowledged == 1,
				"Transient renderer target change was not acknowledged");
			QCoreApplication::processEvents();
			require(conversionPopup->isVisible(),
				"Renderer target change closed an active Config dropdown");
			require(GetWindow(reinterpret_cast<HWND>(window.effectiveWinId()),
				GW_OWNER) == nullptr,
				"Renderer target change assigned Config a native owner");
		}
	}
    const ULONGLONG openUntil = GetTickCount64() + 1100;
    while (GetTickCount64() < openUntil)
    {
        QCoreApplication::processEvents();
        Sleep(20);
    }
    require(conversionPopup->isVisible(),
        "Real Video conversion dropdown could not remain open");

    const int original = conversion->currentIndex();
    const int alternate = original == 0 ? 1 : 0;
    const QModelIndex alternateIndex = conversion->model()->index(alternate,
        conversion->modelColumn(), conversion->rootModelIndex());
    const QPoint point = conversion->view()->visualRect(alternateIndex).center();
    QWidget* conversionViewport = conversion->view()->viewport();
    if (nativeWindows)
    {
        const HWND viewport = reinterpret_cast<HWND>(conversionViewport->winId());
        SendMessageW(viewport, WM_MOUSEMOVE, 0, MAKELPARAM(point.x(), point.y()));
        SendMessageW(viewport, WM_LBUTTONDOWN, MK_LBUTTON,
            MAKELPARAM(point.x(), point.y()));
        SendMessageW(viewport, WM_LBUTTONUP, 0, MAKELPARAM(point.x(), point.y()));
    }
    else
    {
        const QPoint globalPoint = conversionViewport->mapToGlobal(point);
        QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(globalPoint),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(conversionViewport, &move);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(point),
            QPointF(globalPoint), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(conversionViewport, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point),
            QPointF(globalPoint), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(conversionViewport, &release);
    }
    QCoreApplication::processEvents();
    require(conversion->currentIndex() == alternate &&
        !conversionPopup->isVisible(),
        "A native mouse click could not select the real Video conversion dropdown");

    QComboBox* hdr = requireControl<QComboBox>(window,
        QStringLiteral("config.general.hdr_colorspace"));
    QKeyEvent openHdr(QEvent::KeyPress, Qt::Key_Down, Qt::AltModifier);
    QApplication::sendEvent(hdr, &openHdr);
    QCoreApplication::processEvents();
    QWidget* hdrPopup = hdr->view()->window();
    require(hdrPopup && hdrPopup->isVisible(),
        "Real HDR color space dropdown was inaccessible after mouse selection");
    QKeyEvent navigateHdr(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(hdr->view(), &navigateHdr);
    QKeyEvent closeHdr(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(hdr->view(), &closeHdr);
    QCoreApplication::processEvents();
    require(!hdrPopup->isVisible(),
        "Real HDR color space dropdown did not close normally");

    QComboBox* container = requireControl<QComboBox>(window,
        QStringLiteral("config.general.container_colorspace"));
    container->showPopup();
    QCoreApplication::processEvents();
    require(container->view()->window()->isVisible(),
        "A subsequent real dropdown was no longer accessible");
    container->hidePopup();
    window.close();
	if (firstPresentationTarget)
		DestroyWindow(firstPresentationTarget);
	if (secondPresentationTarget)
		DestroyWindow(secondPresentationTarget);
    if (owner)
        DestroyWindow(owner);
}

void testColdHiddenAssociationDeliversPendingReveal()
{
    if (QGuiApplication::platformName().compare(
        QStringLiteral("windows"), Qt::CaseInsensitive) != 0)
        return;

    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
        L"VP cold-start owner", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(owner != nullptr, "Cannot create cold-start VP owner fixture");
    testAdvertisedEditor = nullptr;
    testAssociationActivationAcknowledged = false;
    testActivateOnAssociation = true;
    testOwnerOriginalProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        owner, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(testOwnerProcedure)));
    require(testOwnerOriginalProcedure != nullptr,
        "Cannot install cold-start association fixture");
    ShowWindow(owner, SW_SHOWNOACTIVATE);

    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, reinterpret_cast<quintptr>(owner), true);
    for (int attempt = 0; attempt < 4; ++attempt)
        QCoreApplication::processEvents();
    HWND editor = reinterpret_cast<HWND>(window.effectiveWinId());
    require(editor && IsWindow(editor) && testAdvertisedEditor == editor,
        "Cold hidden editor did not advertise its first usable native HWND");
    require(testAssociationActivationAcknowledged && window.isVisible(),
        "Pending activation immediately after association was not acknowledged and revealed");

    // Model an older tray-close already queued when the newer runtime reveal
    // arrives. The newer reveal must remain the final visible state.
    window.hide();
    QCoreApplication::postEvent(&window, new QCloseEvent());
    const UINT activationMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.Activate.v1");
    DWORD_PTR acknowledged = 0;
    require(SendMessageTimeoutW(editor, activationMessage,
        GetCurrentProcessId(), reinterpret_cast<LPARAM>(owner),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &acknowledged) &&
        acknowledged == 1 && !window.isVisible(),
        "New reveal did not acknowledge before superseding queued close");
    QCoreApplication::processEvents();
    require(window.isVisible(),
        "An older queued close hid the editor after a successful reveal");

    window.hide();
    SetWindowLongPtrW(owner, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(testOwnerOriginalProcedure));
    testOwnerOriginalProcedure = nullptr;
    testAdvertisedEditor = nullptr;
    testActivateOnAssociation = false;
    DestroyWindow(owner);
}

void testStableRevealEndpointSurvivesHiddenQtWindow()
{
	if (QGuiApplication::platformName().compare(
		QStringLiteral("windows"), Qt::CaseInsensitive) != 0)
		return;

	HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC",
		L"VP stable reveal owner", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200,
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	require(owner != nullptr, "Cannot create stable reveal owner fixture");
	QTemporaryDir directory;
	ConfigEditorWindow window(copyFixture(directory),
		reinterpret_cast<quintptr>(owner), true);
	window.show();
	QCoreApplication::processEvents();
	window.hide();
	QCoreApplication::processEvents();
	require(!window.isVisible(), "Config did not enter its hidden tray state");

	const std::wstring eventName =
		ConfigurationLiveApply::ConfigurationEditorRevealEventName(
			GetCurrentProcessId());
	HANDLE revealEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE,
		eventName.c_str());
	require(revealEvent != nullptr,
		"Stable Config reveal endpoint was not created");
	require(SetEvent(revealEvent) != FALSE,
		"Stable Config reveal endpoint could not be signaled");
	CloseHandle(revealEvent);
	const ULONGLONG deadline = GetTickCount64() + 1000;
	while (!window.isVisible() && GetTickCount64() < deadline)
	{
		QCoreApplication::processEvents();
		Sleep(5);
	}
	require(window.isVisible(),
		"Stable process endpoint did not reveal hidden Config");
	const HWND revealedEditor = reinterpret_cast<HWND>(window.effectiveWinId());
	const BOOL foregroundAccepted = SetForegroundWindow(revealedEditor);
	const ULONGLONG stabilityDeadline = GetTickCount64() + 3500;
	while (GetTickCount64() < stabilityDeadline)
	{
		QCoreApplication::processEvents();
		Sleep(10);
	}
	require(reinterpret_cast<HWND>(window.effectiveWinId()) == revealedEditor &&
		IsWindow(revealedEditor),
		"Config recreated its native HWND after a settled reveal");
	require((GetWindowLongPtrW(revealedEditor, GWL_EXSTYLE) &
		WS_EX_TOPMOST) != 0,
		"Config lost topmost placement after a settled reveal");
	if (foregroundAccepted)
		require(GetForegroundWindow() == revealedEditor,
			"Config lost foreground without user input after a settled reveal");
	window.hide();
	DestroyWindow(owner);
}

void testCrossThreadActivationHasNoReverseCallbackDeadlock()
{
    if (QGuiApplication::platformName().compare(
        QStringLiteral("windows"), Qt::CaseInsensitive) != 0)
        return;

    ReverseCallbackFixture fixture;
    std::thread ownerThread([&fixture]
    {
        const wchar_t* className = L"VP.ConfigEditor.ReverseCallbackTest";
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = reverseCallbackOwnerProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = className;
        RegisterClassW(&windowClass);
        HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, className,
            L"VP reverse-callback owner", WS_OVERLAPPEDWINDOW,
            0, 0, 320, 200, nullptr, nullptr, windowClass.hInstance, &fixture);
        fixture.owner.store(owner);
        if (!owner) return;
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    });

    const ULONGLONG ownerDeadline = GetTickCount64() + 2000;
    while (!fixture.owner.load() && GetTickCount64() < ownerDeadline)
        Sleep(1);
    const HWND owner = fixture.owner.load();
    bool advertisedCurrent = false;
    bool becameVisible = false;
    if (owner)
    {
        QTemporaryDir directory;
        const QString path = copyFixture(directory);
        ConfigEditorWindow window(path, reinterpret_cast<quintptr>(owner), true);
        const ULONGLONG activationDeadline = GetTickCount64() + 2000;
        while ((!fixture.activationReturned.load() || !window.isVisible()) &&
            GetTickCount64() < activationDeadline)
        {
            QCoreApplication::processEvents();
            Sleep(1);
        }
        advertisedCurrent = fixture.advertisedEditor.load() ==
            reinterpret_cast<HWND>(window.effectiveWinId());
        becameVisible = window.isVisible();
        window.hide();
        PostMessageW(owner, WM_CLOSE, 0, 0);
    }
    if (ownerThread.joinable()) ownerThread.join();

    require(owner != nullptr, "Cannot create reverse-callback owner thread fixture");
    require(fixture.activationReturned.load(),
        "Cross-thread Activate timed out in a reverse Association callback cycle");
    require(fixture.activationAcknowledged.load(),
        "Cross-thread Activate did not return ack=1");
    require(fixture.activationElapsedMs.load() < 250,
        "Cross-thread Activate did not acknowledge promptly");
    require(advertisedCurrent,
        "Cross-thread association did not advertise the current editor HWND");
    require(becameVisible,
        "Queued reveal did not make the cross-thread activated editor visible");
}

void testExternalForegroundLeavesConfigTopmost()
{
    if (QGuiApplication::platformName().compare(
        QStringLiteral("windows"), Qt::CaseInsensitive) != 0)
        return;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW receiverClass{};
    receiverClass.lpfnWndProc = externalFixtureReceiverProcedure;
    receiverClass.hInstance = instance;
    receiverClass.lpszClassName = L"VP.ConfigTests.FixtureReceiver.v1";
    RegisterClassW(&receiverClass);
    ExternalZOrderFixture fixture;
    HWND receiver = CreateWindowExW(WS_EX_TOOLWINDOW,
        receiverClass.lpszClassName, L"fixture receiver", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, instance, &fixture);
    require(receiver != nullptr, "Cannot create external fixture receiver");

    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(executable) +
        L"\" --zorder-helper " +
        std::to_wstring(reinterpret_cast<quintptr>(receiver));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessW(nullptr, command.data(), nullptr,
        nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    bool popupKeptForeground = false;
    bool externalForegroundKeepsConfigTopmost = false;
    bool hostUnchanged = false;
    bool spoofedTargetRejected = false;
    bool targetPlacementValid = false;
    bool independentOwnerPreserved = false;
    bool targetMainIsScoped = false;
    bool manualMovePreserved = false;
    bool repeatRevealClamped = false;
    if (launched)
    {
        const ULONGLONG readyDeadline = GetTickCount64() + 3000;
        while ((!fixture.owner || !fixture.host) && GetTickCount64() < readyDeadline)
        {
            QCoreApplication::processEvents();
            Sleep(1);
        }
        if (fixture.owner && fixture.host)
        {
            RECT hostBefore{};
            GetWindowRect(fixture.host, &hostBefore);
            const LONG_PTR styleBefore = GetWindowLongPtrW(fixture.host, GWL_STYLE);
            const LONG_PTR exStyleBefore = GetWindowLongPtrW(fixture.host, GWL_EXSTYLE);
            QTemporaryDir directory;
            ConfigEditorWindow window(copyFixture(directory),
                reinterpret_cast<quintptr>(fixture.owner), true);
            HWND editor = reinterpret_cast<HWND>(window.effectiveWinId());
            static const UINT targetMessage = RegisterWindowMessageW(
                L"VideoProcessor.ConfigEditor.PresentationTarget.v1");
            DWORD_PTR targetAck = 1;
            spoofedTargetRejected = SendMessageTimeoutW(editor, targetMessage,
                GetCurrentProcessId(), reinterpret_cast<LPARAM>(fixture.host),
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &targetAck) && targetAck == 0;
            targetAck = 0;
            require(SendMessageTimeoutW(editor, targetMessage,
                process.dwProcessId, reinterpret_cast<LPARAM>(fixture.host),
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &targetAck) && targetAck == 1,
                "Valid presentation target handoff was not acknowledged");
            RECT hiddenFrame{};
            GetWindowRect(editor, &hiddenFrame);
            const int preservedWidth = hiddenFrame.right - hiddenFrame.left;
            const int preservedHeight = hiddenFrame.bottom - hiddenFrame.top;
            window.reveal();
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            editor = reinterpret_cast<HWND>(window.effectiveWinId());
            RECT placedFrame{};
            GetWindowRect(editor, &placedFrame);
            MONITORINFO targetMonitor{};
            targetMonitor.cbSize = sizeof(targetMonitor);
            GetMonitorInfoW(MonitorFromWindow(fixture.host,
                MONITOR_DEFAULTTONEAREST), &targetMonitor);
            const RECT work = targetMonitor.rcWork;
            targetPlacementValid = placedFrame.left >= work.left &&
                placedFrame.top >= work.top && placedFrame.right <= work.right &&
                placedFrame.bottom <= work.bottom &&
                placedFrame.right - placedFrame.left == preservedWidth &&
                placedFrame.bottom - placedFrame.top == preservedHeight;
            independentOwnerPreserved = GetWindow(editor, GW_OWNER) == nullptr;
            targetMainIsScoped =
                (GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;

            SetForegroundWindow(editor);

            QWidget popup(&window, Qt::Popup);
            popup.resize(160, 80);
            popup.show();
            const HWND popupWindow = reinterpret_cast<HWND>(popup.winId());
            SetForegroundWindow(popupWindow);
            Sleep(250);
            QCoreApplication::processEvents();
            popupKeptForeground = popup.isVisible();

            AllowSetForegroundWindow(process.dwProcessId);
            static const UINT requestMessage = RegisterWindowMessageW(
                L"VideoProcessor.ConfigTests.ZOrderFixture.RequestForeground.v1");
            PostMessageW(fixture.host, requestMessage, 0, 0);
            // A visible Config window is now intentionally system-topmost.
            // The host may or may not receive foreground under Windows focus
            // policy, but it must not demote Config from the topmost band.
            Sleep(250);
            Sleep(250);
            QCoreApplication::processEvents();
            externalForegroundKeepsConfigTopmost =
                (GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
            RECT hostAfter{};
            GetWindowRect(fixture.host, &hostAfter);
            hostUnchanged = EqualRect(&hostBefore, &hostAfter) &&
                GetWindowLongPtrW(fixture.host, GWL_STYLE) == styleBefore &&
                GetWindowLongPtrW(fixture.host, GWL_EXSTYLE) == exStyleBefore &&
                (exStyleBefore & WS_EX_TOPMOST) != 0 &&
                (styleBefore & WS_POPUP) != 0;
            popup.hide();

            SetWindowPos(editor, nullptr, work.right - 100, work.bottom - 100,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            RECT manuallyMoved{};
            GetWindowRect(editor, &manuallyMoved);
            Sleep(250);
            QCoreApplication::processEvents();
            RECT afterLease{};
            GetWindowRect(editor, &afterLease);
            manualMovePreserved = EqualRect(&manuallyMoved, &afterLease) != FALSE;
            window.hide();
            window.reveal();
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            GetWindowRect(editor, &placedFrame);
            repeatRevealClamped = placedFrame.left >= work.left &&
                placedFrame.top >= work.top && placedFrame.right <= work.right &&
                placedFrame.bottom <= work.bottom;
            window.hide();
        }
        PostThreadMessageW(process.dwThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(process.hProcess, 3000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    DestroyWindow(receiver);

    require(launched, "Cannot launch external VP z-order fixture process");
    require(fixture.owner && fixture.host,
        "External VP z-order fixture did not publish its windows");
    require(spoofedTargetRejected,
        "Presentation target handoff accepted a spoofed process id");
    require(targetPlacementValid,
        "Reveal did not preserve size and fully clamp Config to target monitor work area");
    require(independentOwnerPreserved,
        "Presentation target handoff assigned Config a native owner");
    require(targetMainIsScoped,
        "Presentation-owned Config was not scoped above topmost fullscreen");
    require(manualMovePreserved,
        "Visible Config was pulled back after a manual move");
    require(repeatRevealClamped,
        "Re-hide/reveal did not return Config to the presentation target monitor");
    require(popupKeptForeground,
        "Natural Config ownership closed a same-process popup unexpectedly");
    require(externalForegroundKeepsConfigTopmost,
        "Visible Config lost topmost placement after external foreground activation");
    require(hostUnchanged,
        "Config foreground recovery changed fullscreen host rect or styles");
}

void testSyntheticPresentationTargetClamp()
{
    const QRect workArea(-1920, 40, 1920, 1040);
    const QRect offscreenFrame(1700, 900, 1040, 700);
    const QRect placed = ConfigEditorPlacement::ClampFrameToWorkArea(
        offscreenFrame, workArea);
    require(placed.size() == offscreenFrame.size(),
        "Presentation placement changed the Config window size");
    require(workArea.contains(placed),
        "Presentation placement did not fully clamp to a synthetic monitor work area");
    if (GetSystemMetrics(SM_CMONITORS) < 2)
        std::cout << "INFO physical two-monitor placement skipped: only one monitor; "
            "synthetic negative-origin clamp plus live target HWND coverage ran" << std::endl;
}

void testNormalWindowArchitectureHasNoLeasePolling()
{
    const QByteArray source = readBytes(repositoryPath(QStringLiteral(
        "src/VideoProcessor-Config/ConfigEditorWindow.cpp")));
    require(!source.contains("ownerMonitor_") &&
        !source.contains("maintainForegroundLease") &&
        !source.contains("maintainPopupZOrder") &&
        !source.contains("setInterval(200)"),
        "Config editor still contains polling, popup, or foreground lease logic");
}

int run(const char* name, const std::function<void()>& test)
{
    if (!testNameFilter.isEmpty() &&
        testNameFilter.compare(QString::fromUtf8(name), Qt::CaseInsensitive) != 0)
        return 0;
    ++selectedTestsRun;
    std::cerr << "START " << name << std::endl;
    try
    {
        test();
        std::cout << "PASS " << name << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << name << ": " << error.what() << std::endl;
        return 1;
    }
}
}

int main(int argc, char** argv)
{
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) ==
        QStringLiteral("--zorder-helper"))
    {
        bool ok = false;
        const quintptr receiver = QString::fromLocal8Bit(argv[2]).toULongLong(&ok);
        return ok ? runExternalZOrderFixture(reinterpret_cast<HWND>(receiver)) : 2;
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) ==
        QStringLiteral("--test"))
        testNameFilter = QString::fromLocal8Bit(argv[2]);
    QApplication application(argc, argv);
    QApplication::setStyle(VpTheme::CreateStyle());
    application.setStyleSheet(VpTheme::StyleSheet());
    int failures = 0;
    failures += run("every page round trips", testEveryPageRoundTrips);
    failures += run("renderer section tabs stay synchronized during rapid clicks",
        testRendererSectionTabsRemainSynchronizedDuringRapidClicks);
    failures += run("empty Standard shaders can create first shader",
        testEmptyStandardShadersCanCreateFirstShader);
    failures += run("two-column cards share row height", testTwoColumnCardsShareRowHeight);
    failures += run("inherited renderer Input selectors use effective labels",
        testInheritedRendererInputSelectorsUseEffectiveLabels);
    failures += run("SDR gamma adjustment labels and persistence",
        testSdrGammaAdjustmentLabelsAndPersistence);
    failures += run("legacy VP input override migrates independently",
        testLegacyVpInputOverrideMigratesToIndependentPolicySection);
    failures += run("profile lifecycle through widgets", testProfileLifecycleThroughWidgets);
    failures += run("unrelated content remains exact", testUnrelatedContentRemainsExact);
    failures += run("scene detection defaults off and hides manual overrides",
        testSceneDetectionDefaultsOffAndHidesManualOverrides);
    failures += run("virtual shader Off option persists", testVirtualShaderOffOptionPersistsWhenConfigured);
    failures += run("disabling shader rule preserves shortcut", testDisablingShaderRulePreservesShortcut);
    failures += run("custom shader parameters expand into page scroll",
        testCustomShaderParametersExpandIntoPageScroll);
    failures += run("renderer profile sections collapse and persist", testRendererProfileSectionsCollapseAndPersist);
    failures += run("output experiments persist and restore defaults",
        testOutputExperimentsPersistAndRestoreDefaults);
    failures += run("Screen Config sections and inline units", testScreenConfigSectionsAndInlineUnits);
    failures += run("queue units and LUT controls use consistent rows",
        testQueueUnitsAndLutControlsUseConsistentRows);
    failures += run("active profile markers cover relevant lists", testActiveProfileMarkersCoverRelevantLists);
    failures += run("active shader markers use authoritative set", testActiveShaderMarkersUseAuthoritativeSet);
    failures += run("unchanged active profile status avoids list invalidation",
        testUnchangedActiveProfileStatusDoesNotInvalidateLists);
    failures += run("active shader status rejects stale generation", testActiveShaderStatusRejectsStaleGeneration);
    failures += run("standalone Config reads live active profile status", testStandaloneConfigAcceptsLiveActiveProfileStatus);
    failures += run("LUT selector discovers installation LUT files", testLutSelectorDiscoversInstallationLutFiles);
    failures += run("choice labels and VP Renderer name", testChoiceLabelsAndVpRendererName);
    failures += run("legacy renderer visibility defaults hidden and preserves shortcuts", testLegacyRendererVisibilityDefaultsHiddenAndPreservesShortcuts);
	failures += run("legacy renderer visibility filters existing UI immediately",
		testLegacyRendererVisibilityFiltersExistingUiImmediately);
	failures += run("General input Apply preserves backend overrides",
		testGeneralInputApplyPreservesBackendOverrides);
    failures += run("new action starts unconfigured", testNewActionStartsUnconfigured);
    failures += run("empty actions show an empty state", testEmptyActionsShowEmptyState);
    failures += run("missing configuration can be created from editor",
        testMissingConfigurationCanBeCreatedFromEditor);
    failures += run("Apply OK Cancel contract", testApplyOkCancelContract);
    failures += run("DirectShow-only effect does not restart Alpha",
        testDirectShowOnlyEffectDoesNotRestartAlpha);
    failures += run("invalid renderer is rejected continuously",
        testInvalidRendererIsRejectedContinuously);
    failures += run("shortcut effects are classified live",
        testShortcutEffectsAreClassifiedLive);
    failures += run("display warning and error precedence",
        testDisplayWarningAndErrorPrecedence);
    failures += run("Video Only startup default round trips",
        testVideoOnlyStartupDefaultRoundTrips);
    failures += run("remaining effect summary precedence",
        testRemainingEffectSummaryPrecedence);
    failures += run("DPI keyboard and accessibility behavior", testDpiKeyboardAndAccessibilityBehavior);
    failures += run("primary discovery selectors open on field click",
        testPrimaryDiscoverySelectorsOpenOnFieldClick);
    failures += run("ordinary edits avoid disk validation",
        testOrdinaryEditsDoNotRequireDiskValidation);
    failures += run("warm reveal grants foreground permission",
        testWarmRevealPathGrantsForegroundPermission);
    failures += run("native owner preserves Qt input and popup association",
        testNativeOwnerPreservesQtInputAndPopupAssociation);
    failures += run("real configuration dropdown remains clickable",
        testRealConfigurationDropdownRemainsClickable);
    failures += run("cold hidden association delivers pending reveal",
        testColdHiddenAssociationDeliversPendingReveal);
	failures += run("stable reveal endpoint survives hidden Qt window",
		testStableRevealEndpointSurvivesHiddenQtWindow);
    failures += run("cross-thread activation avoids reverse callback deadlock",
        testCrossThreadActivationHasNoReverseCallbackDeadlock);
    failures += run("external foreground leaves Config topmost",
        testExternalForegroundLeavesConfigTopmost);
    failures += run("synthetic presentation target clamp",
        testSyntheticPresentationTargetClamp);
    failures += run("normal window architecture has no lease polling",
        testNormalWindowArchitectureHasNoLeasePolling);
    if (!testNameFilter.isEmpty() && selectedTestsRun == 0)
        return 2;
    return failures == 0 ? 0 : 1;
}
