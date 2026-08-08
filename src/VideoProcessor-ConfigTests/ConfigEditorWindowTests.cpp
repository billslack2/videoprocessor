#include "ConfigEditorWindow.h"
#include "VpTheme.h"

#include <QApplication>
#include <QAccessible>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
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
    QPushButton* button = requireControl<QPushButton>(window, QStringLiteral("saveChanges"));
    require(button->isEnabled(), "Save button was not enabled after an edit");
    button->click();
    QCoreApplication::processEvents();
    require(!button->isEnabled(), "Save did not complete successfully");
}

void testEveryPageRoundTrips()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);

    requireControl<QComboBox>(window, QStringLiteral("config.general.capture_device"))
        ->setEditText(QStringLiteral("Decklink Test Device"));
    QComboBox* captureInput = requireControl<QComboBox>(window,
        QStringLiteral("config.general.capture_input"));
    selectData(captureInput, QStringLiteral("HDMI"));
    requireControl<QComboBox>(window, QStringLiteral("config.general.renderer"))
        ->setEditText(QStringLiteral("VideoProcessor Renderer (Alpha)"));
    requireControl<QCheckBox>(window, QStringLiteral("config.general.fullscreen"))->setChecked(false);
    requireControl<QCheckBox>(window, QStringLiteral("config.general.startminimized"))->setChecked(false);
    requireControl<QCheckBox>(window, QStringLiteral("config.general.scene_detect"))->setChecked(false);
    requireControl<QComboBox>(window, QStringLiteral("config.general.fullscreen_monitor_name"))
        ->setEditText(QStringLiteral("Test Display"));
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

    requireControl<QLineEdit>(window,
        QStringLiteral("config.vprenderer.viewport.screen_aspect"))->setText(QStringLiteral("21:10"));
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
    selectData(requireControl<QComboBox>(window,
        QStringLiteral("config.directshow.renderer_primaries")), QStringLiteral("BT2020"));

    requireControl<QLineEdit>(window, QStringLiteral("config.lldv.max_cll"))
        ->setText(QStringLiteral("1200"));
    requireControl<QLineEdit>(window, QStringLiteral("config.lldv.max_fall"))
        ->setText(QStringLiteral("450"));

    requireControl<QLineEdit>(window, QStringLiteral("config.shortcuts.fullscreen_toggle"))
        ->setText(QStringLiteral("Ctrl+f"));
    requireControl<QLineEdit>(window, QStringLiteral("config.shortcuts.config_editor"))
        ->setText(QStringLiteral("Ctrl+e"));

    window.selectPage(9);
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

    save(window);
    const QByteArray saved = readBytes(path);
    const QList<QByteArray> expected = {
        "capture_device: Decklink Test Device", "capture_input: HDMI",
        "renderer: VP Renderer", "fullscreen: false",
        "scene_detect: false",
        "fullscreen_monitor_name: Test Display", "container_colorspace: BT2020",
        "queue_size: 48", "lead_frames: 3", "reset_queue_too_large_percent: 200",
        "shortcut: Ctrl+q", "quality: balanced", "sdr_target_nits: 220", "tone_mapping: spline",
        "screen_aspect: 21:10", "anamorphic_scale: 4:3",
        "renderer_start_stop_time_method: RATIONAL_RATIONAL", "frame_offset: 75",
        "renderer_primaries: BT2020", "max_cll: 1200", "max_fall: 450",
        "fullscreen_toggle: Ctrl+f", "config_editor: Ctrl+e",
        "renderer: *", "run: C:\\Tools\\verified-action.cmd 42",
        "enabled: false", "debug: false", "debug_log_retention: 25",
        "label: Verified Stretch", "order: 10", "strength: 0.85"
    };
    for (const QByteArray& text : expected)
        require(saved.contains(text), text.constData());
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
    answerInputDialog(QStringLiteral("Temporary Profile"));
    requireControl<QPushButton>(window, QStringLiteral("config.queue.add_profile"))->click();
    require(list->count() == beforeAdd + 1, "Add profile did not create a profile");
    require(list->currentItem()->text().startsWith(QStringLiteral("Temporary Profile")),
        "Added profile was not selected");
    QCoreApplication::processEvents();

    answerMessageBox(QMessageBox::Yes);
    requireControl<QPushButton>(window, QStringLiteral("config.queue.remove_profile"))->click();
    require(list->count() == beforeAdd, "Remove profile did not remove the selected profile");
    save(window);
    const QByteArray saved = readBytes(path);
    const QByteArray lowerSaved = saved.toLower();
    require(lowerSaved.contains("[queue.cinema_queue]"), "Renamed profile header was not saved");
    require(lowerSaved.indexOf("[queue.cinema_queue]") < lowerSaved.indexOf("[queue.profile_1]"),
        "Reordered profile file order was not saved");
    require(!lowerSaved.contains("temporary_profile"), "Removed profile was serialized");
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

void testRendererProfileSectionsCollapseAndPersist()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.selectPage(2);
    window.show();
    QCoreApplication::processEvents();

    QToolButton* basic = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.basic"));
    QToolButton* color = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.colorTone"));
    QToolButton* scaling = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.scalingCleanup"));
    QToolButton* lut = requireControl<QToolButton>(window,
        QStringLiteral("rendererSection.lut"));
    require(basic->isChecked(), "Basic renderer settings were not expanded initially");
    require(!color->isChecked() && !scaling->isChecked() &&
        !lut->isChecked(),
        "A secondary renderer section was not collapsed initially");
    require(scaling->text() == QStringLiteral("Scaling and cleanup") &&
        !scaling->text().contains(u'_'),
        "The scaling section exposes an internal identifier instead of a friendly heading");
    require(window.findChild<QComboBox*>(
        QStringLiteral("config.vprenderer.default_screen_profile")) == nullptr,
        "The obsolete legacy screen-profile selector is still exposed");
    require(window.findChild<QComboBox*>(
        QStringLiteral("config.vprenderer.deband")) == nullptr,
        "The overlapping legacy debanding toggle is still exposed");
    require(window.findChild<QCheckBox*>(
        QStringLiteral("config.vprenderer.output_diagnostics")) == nullptr &&
        window.findChild<QCheckBox*>(
        QStringLiteral("config.vprenderer.diagnostic_disable_shader_cache")) == nullptr,
        "Diagnostic-only renderer switches are still exposed in the editor");
    require(requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.report_bt2020_to_display")) &&
        requireControl<QCheckBox>(window,
        QStringLiteral("config.vprenderer.switch_refresh_rate")),
        "Common display controls are not included in the Basic section");
    QComboBox* debanding = requireControl<QComboBox>(window,
        QStringLiteral("config.vprenderer.deband_strength"));
    require(debanding->findData(QStringLiteral("AUTO")) >= 0 &&
        debanding->findData(QStringLiteral("off")) >= 0 &&
        debanding->findData(QStringLiteral("light")) >= 0 &&
        debanding->findData(QStringLiteral("default")) >= 0,
        "The canonical debanding selector is missing an expected value");
    require(requireControl<QWidget>(window,
        QStringLiteral("rendererSection.basic.content"))->isVisibleTo(&window),
        "Basic renderer content is not visible");
    require(!requireControl<QWidget>(window,
        QStringLiteral("rendererSection.lut.content"))->isVisibleTo(&window),
        "3D LUT renderer content is visible while collapsed");

    color->click();
    require(color->isChecked() && requireControl<QWidget>(window,
        QStringLiteral("rendererSection.colorTone.content"))->isVisibleTo(&window),
        "Color and tone section did not expand");
    QListWidget* profiles = requireControl<QListWidget>(window,
        QStringLiteral("config.vprenderer.profiles"));
    if (profiles->count() > 1) profiles->setCurrentRow(1);
    QCoreApplication::processEvents();
    require(color->isChecked(),
        "Renderer section expansion state changed when selecting another profile");

    // Selecting the canonical control must retire the compatibility toggle,
    // so saving cannot leave two conflicting debanding values in one profile.
    profiles->setCurrentRow(0);
    QCoreApplication::processEvents();
    selectData(debanding, QStringLiteral("light"));
    save(window);
    const QByteArray saved = readBytes(path);
    require(saved.contains("deband_strength: light") &&
        !saved.contains("\ndeband:"),
        "Saving the canonical debanding control left an overlapping legacy key");
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

void testReloadConfirmationRemainsOpenUntilExplicitChoice()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.show();
    QCoreApplication::processEvents();

    QCheckBox* sceneDetection = requireControl<QCheckBox>(window,
        QStringLiteral("config.general.scene_detect"));
    const bool initialSceneDetection = sceneDetection->isChecked();
    sceneDetection->setChecked(!initialSceneDetection);
    QPushButton* saveButton = requireControl<QPushButton>(window,
        QStringLiteral("saveChanges"));
    require(saveButton->isEnabled(), "The test configuration did not become dirty");

    requireControl<QPushButton>(window, QStringLiteral("reloadConfiguration"))->click();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QDialog* confirmation = window.findChild<QDialog*>(
        QStringLiteral("confirmationDialog"));
    require(confirmation && confirmation->isVisible(),
        "The Reload confirmation did not remain open for an explicit choice");
    confirmation->reject();
    QCoreApplication::processEvents();
    require(sceneDetection->isChecked() != initialSceneDetection && saveButton->isEnabled(),
        "Cancelling Reload discarded an unsaved setting");
}

void testCleanReloadDoesNotRebuildTheShell()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);
    window.show();
    QCoreApplication::processEvents();
    save(window);

    QWidget* originalShell = window.centralWidget();
    requireControl<QPushButton>(window, QStringLiteral("reloadConfiguration"))->click();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    require(window.centralWidget() == originalShell,
        "Clean Reload rebuilt the editor shell instead of leaving it stable");
    require(requireControl<QLabel>(window, QStringLiteral("configurationStatus"))->text() ==
        QStringLiteral("No unsaved changes to discard."),
        "Clean Reload did not explain that there was nothing to discard");
    require(window.findChild<QDialog*>(QStringLiteral("confirmationDialog")) == nullptr,
        "Clean Reload unexpectedly opened a confirmation dialog");
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
    renderer->setEditText(QStringLiteral("VideoProcessor Renderer (Alpha)"));
    save(window);
    const QByteArray saved = readBytes(path);
    require(saved.contains("renderer: VP Renderer"),
        "Legacy Alpha renderer display name was not normalized when saved");
    require(saved.contains("frame_offset: AUTO") &&
        !saved.contains("frame_offset: Auto"),
        "Frame offset did not preserve the canonical AUTO token on disk");
}

void testLegacyRendererVisibilityRemainsManualAndPreserved()
{
    QTemporaryDir directory;
    const QString path = copyFixture(directory);
    ConfigEditorWindow window(path, 0, true);

    require(window.findChild<QCheckBox*>(
        QStringLiteral("config.general.hide_legacy_renderers")) == nullptr,
        "The manual-only legacy renderer setting is still editable in the UI");
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
        "The editor wrote the omitted manual-only setting instead of using its true default");
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

int run(const char* name, const std::function<void()>& test)
{
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
    QApplication application(argc, argv);
    QApplication::setStyle(VpTheme::CreateStyle());
    application.setStyleSheet(VpTheme::StyleSheet());
    int failures = 0;
    failures += run("every page round trips", testEveryPageRoundTrips);
    failures += run("profile lifecycle through widgets", testProfileLifecycleThroughWidgets);
    failures += run("unrelated content remains exact", testUnrelatedContentRemainsExact);
    failures += run("scene detection defaults off and hides manual overrides",
        testSceneDetectionDefaultsOffAndHidesManualOverrides);
    failures += run("virtual shader Off option persists", testVirtualShaderOffOptionPersistsWhenConfigured);
    failures += run("renderer profile sections collapse and persist", testRendererProfileSectionsCollapseAndPersist);
    failures += run("LUT selector discovers installation LUT files", testLutSelectorDiscoversInstallationLutFiles);
    failures += run("Reload confirmation waits for an explicit choice", testReloadConfirmationRemainsOpenUntilExplicitChoice);
    failures += run("clean Reload keeps the editor stable", testCleanReloadDoesNotRebuildTheShell);
    failures += run("choice labels and VP Renderer name", testChoiceLabelsAndVpRendererName);
    failures += run("legacy renderer visibility remains manual and preserved", testLegacyRendererVisibilityRemainsManualAndPreserved);
    failures += run("new action starts unconfigured", testNewActionStartsUnconfigured);
    failures += run("empty actions show an empty state", testEmptyActionsShowEmptyState);
    failures += run("DPI keyboard and accessibility behavior", testDpiKeyboardAndAccessibilityBehavior);
    return failures == 0 ? 0 : 1;
}
