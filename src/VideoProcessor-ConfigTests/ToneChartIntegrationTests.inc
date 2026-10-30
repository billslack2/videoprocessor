// Included in ConfigEditorWindowTests.cpp's existing anonymous namespace.
// The real-provider test is opt-in: its executable needs the normal packaged
// config/ + sibling vprenderer-beta/ runtime layout.
namespace ToneChartIntegration {
template<class F> bool wait(F condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 20000) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    return condition();
}

void ready(QWidget* panel)
{
    require(wait([&] {
        const QVariant requested = panel->property("toneCharts.requestedGeneration");
        const QVariant applied = panel->property("toneCharts.readyGeneration");
        return requested.isValid() && applied.isValid() && requested == applied;
    }), "Real chart provider did not apply the latest editor request");
    QApplication::processEvents();
}

QWidget* graph(QWidget* panel, const QString& accessibleName)
{
    for (QWidget* child : panel->findChildren<QWidget*>())
        if ((dynamic_cast<ToneCurveChart*>(child) || dynamic_cast<ColourChart*>(child)) &&
            child->accessibleName() == accessibleName)
            return child;
    throw std::runtime_error("Chart widget missing");
}

QImage pixels(QWidget* widget)
{
    QImage image(widget->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    widget->render(&image);
    return image;
}

void usable(QWidget* panel)
{
    auto* tabs = requireControl<QTabBar>(*panel, "toneChartsTabs");
    require(tabs->isEnabled(), "Chart tabs are disabled during background work");
    for (QWidget* child : panel->findChildren<QWidget*>()) {
        if (auto* label = qobject_cast<QLabel*>(child))
            require(!label->text().contains("Updating charts", Qt::CaseInsensitive) ||
                !label->isVisibleTo(panel), "Loading message is visible");
        if (child->objectName().startsWith("toneCharts.scene.") ||
            child->objectName().startsWith("toneCharts.display.") ||
            child->objectName().startsWith("toneCharts.zone."))
            require(child->isEnabled(), "Chart choice is disabled during background work");
    }
}

void selectProfile(QListWidget* profiles, const QString& section)
{
    for (int row = 0; row < profiles->count(); ++row)
        if (profiles->item(row)->data(Qt::UserRole).toString().compare(section, Qt::CaseInsensitive) == 0) {
            profiles->setCurrentRow(row);
            return;
        }
    throw std::runtime_error("Rendering profile missing");
}

void writeFixture(const QString& path, int firstWhite = 203, int secondWhite = 100)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot create chart editor fixture");
    file.write(QString("[general]\nrenderer: VP Renderer\n"
        "[vprenderer.First]\nquality: fast\npeak_detection: high_quality\ntone_mapping: spline\n"
        "dynamic_target: true\ncustom_tone_mapping: true\ngamut_mapping: perceptual\nsdr_black_nits: 0.005\n"
        "sdr_target_nits: %1\nknee_adaptation: 0.6\nfilm_strength: 0\n"
        "[vprenderer.Second]\nquality: fast\npeak_detection: high_quality\ntone_mapping: spline\n"
        "dynamic_target: true\ncustom_tone_mapping: true\ngamut_mapping: perceptual\nsdr_black_nits: 0.005\n"
        "sdr_target_nits: %2\nknee_adaptation: 0.1\nfilm_strength: 0\n")
        .arg(firstWhite).arg(secondWhite).toUtf8());
}
}

void testToneChartsRealEditorUpdates()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("VideoProcessor.cfg");
    ToneChartIntegration::writeFixture(path);
    const QByteArray original = readBytes(path);
    ConfigEditorWindow window(path, 0, true);
    window.resize(1920, 1080);
    window.show();
    window.selectPage(2);
    auto* panel = requireControl<QWidget>(window, "toneCharts");
    auto* profiles = requireControl<QListWidget>(window, "config.vprenderer.profiles");
    ToneChartIntegration::selectProfile(profiles, "vprenderer.first");
    ToneChartIntegration::ready(panel);
    requireControl<QAbstractButton>(window, "workspaceAdvancedToggle")->click();
    QApplication::processEvents();
    ToneChartIntegration::ready(panel);
    auto* tone = ToneChartIntegration::graph(panel, "Tone curve chart");
    auto* tracking = ToneChartIntegration::graph(panel, "Gamma tracking chart");
    const auto changed = [&](QWidget* chart, const QImage& before, const char* message) {
        ToneChartIntegration::ready(panel);
        require(ToneChartIntegration::pixels(chart) != before, message);
        ToneChartIntegration::usable(panel);
    };

    // Test the real slider preview path: its edit is not committed until
    // release, but the visible curves must already follow the preview value.
    auto* knee = requireControl<QSlider>(window, "config.vprenderer.knee_adaptation.slider");
    const QImage beforeKnee = ToneChartIntegration::pixels(tone);
    const QRect initialGeometry = tone->geometry();
    knee->setSliderDown(true);
    knee->setValue(900000);
    ToneChartIntegration::usable(panel);
    require(tone->geometry() == initialGeometry, "Pending chart update moved the graph");
    changed(tone, beforeKnee, "Knee preview did not update the tone chart pixels");
    knee->setSliderDown(false);
    ToneChartIntegration::ready(panel);

    const QImage beforeWhite = ToneChartIntegration::pixels(tone);
    const QImage beforeTracking = ToneChartIntegration::pixels(tracking);
    requireControl<QLineEdit>(window, "config.vprenderer.sdr_target_nits")->setText("130");
    changed(tone, beforeWhite, "Target white edit did not update the tone chart pixels");
    require(ToneChartIntegration::pixels(tracking) != beforeTracking,
        "Target white edit did not update gamma tracking pixels");
    require(panel->property("toneCharts.readyTargetNits").toDouble() == 130,
        "Target white edit applied the wrong request");

    const QImage beforeProfile = ToneChartIntegration::pixels(tone);
    ToneChartIntegration::selectProfile(profiles, "vprenderer.second");
    changed(tone, beforeProfile, "Profile selection did not update the tone chart pixels");
    require(panel->property("toneCharts.readyTargetNits").toDouble() == 100,
        "Selected profile applied the wrong target white");

    const QImage beforeScene = ToneChartIntegration::pixels(tone);
    requireControl<QPushButton>(window, "toneCharts.scene.Dark")->click();
    changed(tone, beforeScene, "Scene selection did not update the tone chart pixels");

    const QImage beforeDefault = ToneChartIntegration::pixels(tone);
    requireControl<QToolButton>(window, "toneCharts.compareDefault")->click();
    changed(tone, beforeDefault, "Default curve switch did not update the tone chart pixels");

    QToolButton* firstCompare = nullptr;
    for (auto* button : panel->findChildren<QToolButton*>())
        if (button->toolTip().contains("Stop comparing First")) firstCompare = button;
    require(firstCompare, "Switching profiles did not retain the former profile comparison");
    const QImage beforeCompare = ToneChartIntegration::pixels(tone);
    firstCompare->click();
    changed(tone, beforeCompare, "Removing a comparison did not update the tone chart pixels");

    requireControl<QTabBar>(window, "toneChartsTabs")->setCurrentIndex(1);
    QApplication::processEvents();
    auto* map = ToneChartIntegration::graph(panel, "Colour map chart");
    auto* sweep = ToneChartIntegration::graph(panel, "Saturation sweep chart");
    auto* hue = ToneChartIntegration::graph(panel, "Hue shift chart");
    const QImage beforeDisplay = ToneChartIntegration::pixels(map);
    requireControl<QPushButton>(window, "toneCharts.display.Rec.709")->click();
    changed(map, beforeDisplay, "Colour display selection did not update the map pixels");
    const QImage beforeZone = ToneChartIntegration::pixels(map);
    const QImage beforeHue = ToneChartIntegration::pixels(hue);
    requireControl<QPushButton>(window, "toneCharts.zone.Highlights")->click();
    changed(map, beforeZone, "Colour zone selection did not update the map pixels");
    require(ToneChartIntegration::pixels(hue) != beforeHue,
        "Colour zone selection did not update hue chart pixels");
    const QImage beforePatch = ToneChartIntegration::pixels(sweep);
    requireControl<QComboBox>(window, "toneCharts.sweepPatch")->setCurrentIndex(0);
    changed(sweep, beforePatch, "Colour patch selection did not update sweep chart pixels");
    const QImage beforeFilm = ToneChartIntegration::pixels(sweep);
    requireControl<QLineEdit>(window, "config.vprenderer.film_strength")->setText("1.75");
    changed(sweep, beforeFilm, "Film strength edit did not update Colour chart pixels with Dynamic target active");
    require(readBytes(path) == original, "Background chart updates wrote to the configuration file");
    std::cout << "  real provider: knee preview, target white, profile, scene, default, compare, display, zone, patch, film strength updated pixels with Dynamic target active" << std::endl;
}

void testToneChartsRealConfiguredProfiles()
{
    const QString source = qEnvironmentVariable("VP_TEST_CHART_CONFIG");
    require(!source.isEmpty(), "Set VP_TEST_CHART_CONFIG to the source configuration for this opt-in test");
    QTemporaryDir directory;
    const QString path = directory.filePath("VideoProcessor.cfg");
    require(QFile::copy(source, path), "Cannot create isolated copy of the chart configuration");
    const QByteArray original = readBytes(path);
    ConfigEditorWindow window(path, 0, true);
    window.resize(1920, 1080);
    window.show();window.selectPage(2);
    auto* panel = requireControl<QWidget>(window, "toneCharts");
    auto* profiles = requireControl<QListWidget>(window, "config.vprenderer.profiles");
    require(profiles->count() > 0, "Configuration has no Rendering profiles");
    requireControl<QAbstractButton>(window, "workspaceAdvancedToggle")->click();
    QApplication::processEvents();
    auto* tone = ToneChartIntegration::graph(panel, "Tone curve chart");
    for (int row = 0; row < profiles->count(); ++row) {
        const QString section = profiles->item(row)->data(Qt::UserRole).toString();
        profiles->setCurrentRow(row);
        ToneChartIntegration::ready(panel);
        ToneChartIntegration::usable(panel);
        const double oldTarget = panel->property("toneCharts.readyTargetNits").toDouble();
        const QImage beforeTarget = ToneChartIntegration::pixels(tone);
        requireControl<QLineEdit>(window, "config.vprenderer.sdr_target_nits")->setText(oldTarget < 100 ? "130" : "79");
        ToneChartIntegration::ready(panel);
        require(ToneChartIntegration::pixels(tone) != beforeTarget,
            "Configured profile target edit did not update chart pixels");
        // The input scene remains synthetic; exercise its controls without
        // relying on any capture card, playing media, or installed settings.
        auto* mid = window.findChild<QPushButton*>("toneCharts.scene.Mid");
        if (mid && mid->isVisibleTo(panel)) {
            mid->click();ToneChartIntegration::ready(panel);
            const QImage beforeScene = ToneChartIntegration::pixels(tone);
            requireControl<QPushButton>(window, "toneCharts.scene.Dark")->click();
            ToneChartIntegration::ready(panel);
            require(ToneChartIntegration::pixels(tone) != beforeScene,
                "Configured profile scene selection did not update chart pixels");
        }
        ToneChartIntegration::usable(panel);
        std::cout << "  configured chart profile updated: " << section.toStdString() << std::endl;
    }
    require(readBytes(path) == original, "Configured-profile chart test modified its file");
}

void testToneChartsRealEditorWarmsOtherProfiles()
{
    ChartWorkerTest::Provider provider;
    QTemporaryDir directory;
    const QString path = directory.filePath("VideoProcessor.cfg");
    ToneChartIntegration::writeFixture(path, 317, 431);
    const QByteArray original = readBytes(path);
    ConfigEditorWindow window(path, 0, true);
    auto* panel = requireControl<QWidget>(window, "toneCharts");
    auto* profiles = requireControl<QListWidget>(window, "config.vprenderer.profiles");
    ToneChartIntegration::ready(panel);
    ToneChartWork::Service::Get().drain();
    const int calls = ChartWorkerTest::calls.load();
    // Construction may also sample its provisional 203-nit default request.
    // The two real profiles must each be sampled exactly once before Advanced.
    require(ChartWorkerTest::targetCalls(317) == 1 && ChartWorkerTest::targetCalls(431) == 1,
        "Actual editor callbacks did not warm each Rendering profile once before Advanced");
    ToneChartIntegration::selectProfile(profiles, "vprenderer.second");
    ToneChartIntegration::ready(panel);
    ToneChartWork::Service::Get().drain();
    require(ChartWorkerTest::calls.load() == calls,
        "Selecting an editor-warmed profile sampled the engine again");
    require(panel->property("toneCharts.readyTargetNits").toDouble() == 431,
        "Warmed editor profile did not apply");
    require(readBytes(path) == original, "Background warming wrote to the configuration file");
}
