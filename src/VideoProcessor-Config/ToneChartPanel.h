#pragma once
#include "../VideoProcessor-ChartCore/ChartData.h"
#include "../VideoProcessor-ChartCore/ColourChart.h"
#include "../VideoProcessor-ChartCore/ToneCurveChart.h"
#include "../VideoProcessor-ChartCore/ToneCurveProvider.h"
#include "ToneProfileFile.h"
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

// The tone charts column on the Rendering page. Tabs hold the charts (only
// Brightness for now); the scene buttons under the tab bar, above the charts,
// tell every tab what scene to imagine and are not VP settings. The profile comes from the page.
class ToneChartPanel : public QWidget
{
public:
    ToneChartPanel()
    {
        // Not "config.*": those names mark configuration edits.
        setObjectName(QStringLiteral("toneCharts"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        unavailable_ = new QLabel;
        unavailable_->setProperty("help", true);
        unavailable_->setWordWrap(true);
        unavailable_->hide();
        layout->addWidget(unavailable_);

        // The tab bar, then what every tab shares (the scenes), then the
        // tab pages. His theme styles no tab page, so Windows would paint the
        // pages (white in light mode); they get his card colours on every PC.
        tabs_ = new QWidget;
        auto* area = new QVBoxLayout(tabs_);
        area->setContentsMargins(0, 0, 0, 0);
        area->setSpacing(6);
        tabBar_ = new QTabBar;
        tabBar_->setObjectName(QStringLiteral("toneChartsTabs"));
        tabBar_->setDrawBase(false);
        tabBar_->setExpanding(false); // a tab, not a full-width bar
        area->addWidget(tabBar_);
        shared_ = new QVBoxLayout;
        shared_->setSpacing(4);
        area->addLayout(shared_);
        pages_ = new QStackedWidget;
        pages_->setObjectName(QStringLiteral("toneChartsPages"));
        pages_->setStyleSheet(QStringLiteral(
            "#toneChartsPages { background: #101b27; border: 1px solid #273d53; border-radius: 4px; }"
            " #toneChartsBrightness, #toneChartsColour { background: #101b27; }"
            " #toneChartsDivider { background: #273d53; }"));
        area->addWidget(pages_, 1);
        connect(tabBar_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
        auto* brightness = new QWidget;
        brightness->setObjectName(QStringLiteral("toneChartsBrightness"));
        brightness->setAttribute(Qt::WA_StyledBackground, true);
        auto* charts = new QVBoxLayout(brightness);
        charts->setContentsMargins(4, 8, 4, 4);
        charts->setSpacing(4);
        charts->addWidget(heading(QStringLiteral("Tone curve: source nits in, on-screen nits out")));
        tone_ = new ToneCurveChart(ToneCurveChart::Kind::Tone);
        charts->addWidget(tone_, 1);
        // A thin line in the box's border colour marks where one chart ends.
        auto* divider = new QWidget;
        divider->setObjectName(QStringLiteral("toneChartsDivider"));
        divider->setAttribute(Qt::WA_StyledBackground, true);
        divider->setFixedHeight(1);
        charts->addSpacing(2);
        charts->addWidget(divider);
        charts->addSpacing(2);
        charts->addWidget(heading(QStringLiteral("Gamma tracking: visible difference from PQ (ΔE ITP)")));
        auto* trackingHelp = new QLabel(QStringLiteral(
            "0 is PQ, the source as mastered; below is darker, above is brighter. "
            "The band, ±1, is about one just-noticeable step."));
        trackingHelp->setProperty("toneHint", true);
        trackingHelp->setWordWrap(true);
        charts->addWidget(trackingHelp);
        tracking_ = new ToneCurveChart(ToneCurveChart::Kind::Tracking);
        charts->addWidget(tracking_, 1);
        charts->addStretch(0); // what the charts' shape leaves over, below them
        pages_->addWidget(brightness);
        tabBar_->addTab(QStringLiteral("Brightness"));
        addColourTab();
        // The Colour tab is computed only while it shows.
        connect(tabBar_, &QTabBar::currentChanged, this, [this] { redraw(); });
        layout->addWidget(tabs_, 1);

        // Compare with: libplacebo's default curve, then up to four profiles or
        // exported profile files, the edited profile among them in bold. The
        // chips are the legend.
        auto* compareRow = new QHBoxLayout;
        compareRow->setSpacing(6);
        compareRow->addWidget(new QLabel(QStringLiteral("Compare with:")));
        defaultChip_ = chip(QStringLiteral("libplacebo default"), QColor(QStringLiteral("#e8a93b")), true);
        defaultChip_->setObjectName(QStringLiteral("toneCharts.compareDefault"));
        defaultChip_->setChecked(true);
        defaultChip_->setToolTip(QStringLiteral("Show or hide libplacebo's default curve."));
        connect(defaultChip_, &QToolButton::toggled, this, [this] { redraw(); });
        compareRow->addWidget(defaultChip_);
        chips_ = new QHBoxLayout;
        chips_->setSpacing(6);
        compareRow->addLayout(chips_);
        // A push button, so his theme draws it (a tool button is Windows-styled).
        addCompare_ = new QPushButton(QStringLiteral("+ Add"));
        addCompare_->setObjectName(QStringLiteral("toneCharts.compareAdd"));
        auto* menu = new QMenu(addCompare_);
        addCompare_->setMenu(menu);
        connect(menu, &QMenu::aboutToShow, this, [this, menu] { fillCompareMenu(menu); });
        compareRow->addWidget(addCompare_);
        compareRow->addStretch(1);
        shared_->addLayout(compareRow);

        // The scenes: one row for peak detection on, one for off; only the row
        // the edited profile's peak detection allows is shown.
        for (int set = 0; set < 2; ++set)
        {
            sceneRows_[set] = new QWidget;
            // His theme draws a checked push button as an unchecked one; the
            // selected scene takes his primary button's colours.
            sceneRows_[set]->setStyleSheet(QStringLiteral(
                "QPushButton:checked { color: #f8fcff; background: #1479c4; border-color: #248dd6; }"));
            auto* row = new QHBoxLayout(sceneRows_[set]);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(6);
            row->addWidget(new QLabel(QStringLiteral("Scene:")));
            const auto& scenes = ChartData::Scenes(set == 0);
            for (size_t i = 0; i < scenes.size(); ++i)
            {
                auto* button = new QPushButton(QString::fromLatin1(scenes[i].name));
                button->setObjectName(QStringLiteral("toneCharts.scene.%1").arg(QString::fromLatin1(scenes[i].name)));
                button->setCheckable(true);
                button->setChecked(i == sceneIndex_[set]);
                connect(button, &QPushButton::clicked, this, [this, set, i] { sceneIndex_[set] = i; redraw(); });
                sceneButtons_[set].push_back(button);
                row->addWidget(button);
            }
            row->addStretch(1);
            shared_->addWidget(sceneRows_[set]);
        }
        sceneNote_ = new QLabel;
        sceneNote_->setProperty("toneHint", true);
        sceneNote_->setWordWrap(true);
        shared_->addWidget(sceneNote_);
    }

    // The profile as VP would run it; the panel adds the scene.
    void setRequest(const ToneCurveProvider::Request& request)
    {
        request_ = request;
        haveRequest_ = true;
        redraw();
    }

    // Where compared profiles come from. The page lists its profiles
    // (section, name), names the edited one, and turns a profile or an
    // exported file's values into a request exactly as for the edited one.
    struct CompareSources
    {
        std::function<std::vector<std::pair<QString, QString>>()> profiles;
        std::function<QString()> current;
        std::function<ToneCurveProvider::Request(const QString& section)> profileRequest;
        std::function<ToneCurveProvider::Request(const std::map<QString, QString>& values)> valuesRequest;
    };
    void setCompareSources(CompareSources sources) { sources_ = std::move(sources); }

private:
    static constexpr size_t kMaxCompared = 4;
    struct Compared
    {
        int id = 0;
        QString name;
        QColor color;
        QString section;                     // a profile on this page, or
        std::map<QString, QString> values;   // an exported file's settings
        bool fromFile = false;
        bool bySelecting = false;            // added by selecting it, not by "+ Add"
    };

    // A switch chip greys out when off; a compare chip removes itself; the
    // edited profile's chip is bold and stays.
    static QToolButton* chip(const QString& text, const QColor& color, bool toggle = false, bool bold = false)
    {
        auto* button = new QToolButton;
        button->setText(QStringLiteral("● ") + text);
        button->setCheckable(toggle);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { color: %1; background: #101d2a; border: %2px solid %1; border-radius: 10px; padding: 1px 8px;%3 }")
            .arg(color.name(), bold ? QStringLiteral("2") : QStringLiteral("1"),
                bold ? QStringLiteral(" font-weight: bold;") : QString()) + (toggle ? QStringLiteral(
            " QToolButton:!checked { color: #63798c; border-color: #2c4659; }") : QString()));
        return button;
    }

    QColor nextColor() const
    {
        // Blue first, so the profile edited when the page opens is blue.
        for (const char* candidate : { "#49b6ff", "#6fd08c", "#d77be0", "#f0d75a" })
        {
            const QColor color(QString::fromLatin1(candidate));
            if (std::none_of(compared_.begin(), compared_.end(),
                [&](const Compared& compared) { return compared.color == color; }))
                return color;
        }
        return QColor(QStringLiteral("#49b6ff"));
    }

    void fillCompareMenu(QMenu* menu)
    {
        menu->clear();
        const bool full = compared_.size() >= kMaxCompared;
        const QString current = sources_.current ? sources_.current() : QString();
        int others = 0;
        if (sources_.profiles)
            for (const auto& profile : sources_.profiles())
            {
                const QString section = profile.first, name = profile.second;
                if (section.compare(current, Qt::CaseInsensitive) == 0)
                {
                    // Listed, so a new profile is visibly there: its chip is the bold one.
                    menu->addAction(name + QStringLiteral(" (editing)"))->setEnabled(false);
                    continue;
                }
                const bool already = std::any_of(compared_.begin(), compared_.end(), [&](const Compared& compared) {
                    return !compared.fromFile && compared.section.compare(section, Qt::CaseInsensitive) == 0;
                });
                ++others;
                QAction* action = menu->addAction(name);
                action->setEnabled(!full && !already);
                connect(action, &QAction::triggered, this, [this, section, name] {
                    addCompared(Compared{ 0, name, nextColor(), section, {}, false });
                });
            }
        if (others == 0) menu->addAction(QStringLiteral("No other profiles"))->setEnabled(false);
        menu->addSeparator();
        QAction* file = menu->addAction(QStringLiteral("From file..."));
        file->setEnabled(!full);
        connect(file, &QAction::triggered, this, [this] { compareFile(); });
    }

    // An exported profile, drawn without importing it.
    void compareFile()
    {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Compare with an exported profile"),
            QString(), QStringLiteral("VP Rendering / DTM profile (*.vpprofile);;All files (*)"));
        if (path.isEmpty()) return;
        ToneProfileFile::Profile profile;
        QString error;
        if (!ToneProfileFile::Read(path, profile, error))
        {
            QMessageBox::warning(this, QStringLiteral("Compare with a file"), error);
            return;
        }
        std::map<QString, QString> values(profile.settings.begin(), profile.settings.end());
        const auto label = values.find(QStringLiteral("label"));
        const QString name = (label != values.end() && !label->second.isEmpty() ? label->second :
            QFileInfo(path).completeBaseName()) + QStringLiteral(" (file)");
        addCompared(Compared{ 0, name, nextColor(), QString(), std::move(values), true });
    }

    void addCompared(Compared compared)
    {
        if (compared_.size() >= kMaxCompared) return;
        compared.id = ++lastCompareId_;
        compared_.push_back(std::move(compared));
        rebuildChips();
        redraw();
    }

    void rebuildChips()
    {
        while (QLayoutItem* item = chips_->takeAt(0))
        {
            // Later: a chip's own click rebuilds the chips.
            if (QWidget* widget = item->widget()) { widget->hide(); widget->deleteLater(); }
            delete item;
        }
        for (const Compared& compared : compared_)
        {
            if (IsEdited(compared))
            {
                auto* button = chip(compared.name, compared.color, false, true);
                button->setObjectName(QStringLiteral("toneCharts.compare%1").arg(compared.id));
                button->setToolTip(QStringLiteral("Being edited: select another profile to move on."));
                chips_->addWidget(button);
                continue;
            }
            auto* button = chip(compared.name + QStringLiteral("  ✕"), compared.color);
            button->setObjectName(QStringLiteral("toneCharts.compare%1").arg(compared.id));
            button->setToolTip(QStringLiteral("Stop comparing %1.").arg(compared.name));
            const int id = compared.id;
            connect(button, &QToolButton::clicked, this, [this, id] {
                compared_.erase(std::remove_if(compared_.begin(), compared_.end(),
                    [id](const Compared& entry) { return entry.id == id; }), compared_.end());
                rebuildChips();
                redraw();
            });
            chips_->addWidget(button);
        }
    }

    bool IsEdited(const Compared& compared) const
    {
        return !compared.fromFile && !edited_.isEmpty() && compared.section.compare(edited_, Qt::CaseInsensitive) == 0;
    }

    // The edited profile always has a chip. Selecting one not in the row adds
    // it; when the row is full, the oldest chip added by selecting makes way
    // and the new one takes its colour. "+ Add" chips make way only when no
    // chip was added by selecting. Names follow the page.
    void followEdited(const std::vector<std::pair<QString, QString>>& profiles, const QString& current)
    {
        bool changed = edited_.compare(current, Qt::CaseInsensitive) != 0;
        edited_ = current;
        QString currentName;
        for (const auto& profile : profiles)
        {
            if (profile.first.compare(current, Qt::CaseInsensitive) == 0) currentName = profile.second;
            for (Compared& compared : compared_)
                if (!compared.fromFile && compared.section.compare(profile.first, Qt::CaseInsensitive) == 0
                    && compared.name != profile.second)
                {
                    compared.name = profile.second;
                    changed = true;
                }
        }
        if (!currentName.isEmpty() && std::none_of(compared_.begin(), compared_.end(),
            [this](const Compared& compared) { return IsEdited(compared); }))
        {
            if (compared_.size() >= kMaxCompared)
            {
                const auto selected = std::find_if(compared_.begin(), compared_.end(),
                    [](const Compared& compared) { return compared.bySelecting; });
                compared_.erase(selected != compared_.end() ? selected : compared_.begin());
            }
            compared_.push_back(Compared{ ++lastCompareId_, currentName, nextColor(), current, {}, false, true });
            changed = true;
        }
        if (changed) rebuildChips();
    }
    // The Colour tab: the display it imagines, the picture zone the map shows,
    // the map, then the chosen patch's saturation against strength. None of
    // these is saved or is a VP setting.
    void addColourTab()
    {
        auto* page = new QWidget;
        page->setObjectName(QStringLiteral("toneChartsColour"));
        page->setAttribute(Qt::WA_StyledBackground, true);
        auto* charts = new QVBoxLayout(page);
        charts->setContentsMargins(4, 8, 4, 4);
        charts->setSpacing(4);

        auto* displayRow = new QWidget;
        displayRow->setStyleSheet(QStringLiteral(
            "QPushButton:checked { color: #f8fcff; background: #1479c4; border-color: #248dd6; }"));
        auto* displays = new QHBoxLayout(displayRow);
        displays->setContentsMargins(0, 0, 0, 0);
        displays->setSpacing(6);
        displays->addWidget(new QLabel(QStringLiteral("Display:")));
        for (int i = 0; i < 3; ++i)
        {
            auto* button = new QPushButton(QString::fromLatin1(ChartData::kDisplayNames[i]));
            button->setObjectName(QStringLiteral("toneCharts.display.%1").arg(QString::fromLatin1(ChartData::kDisplayNames[i])));
            button->setCheckable(true);
            button->setChecked(i == display_);
            button->setToolTip(QStringLiteral("The display the Colour charts imagine. Not VP's target primaries setting."));
            connect(button, &QPushButton::clicked, this, [this, i] { display_ = i; redraw(); });
            displayButtons_.push_back(button);
            displays->addWidget(button);
        }
        displays->addStretch(1);
        charts->addWidget(displayRow);

        // The part of the picture the map shows, placed by the scene: Mid-tones
        // at the curve's knee, Peak at the scene peak (see redrawColour).
        auto* zoneRow = new QWidget;
        zoneRow->setStyleSheet(displayRow->styleSheet());
        auto* zones = new QHBoxLayout(zoneRow);
        zones->setContentsMargins(0, 0, 0, 0);
        zones->setSpacing(6);
        zones->addWidget(new QLabel(QStringLiteral("Show colours in:")));
        for (int i = 0; i < ChartData::kZoneCount; ++i)
        {
            auto* button = new QPushButton(QString::fromLatin1(ChartData::kZoneNames[i]));
            button->setObjectName(QStringLiteral("toneCharts.zone.%1").arg(QString::fromLatin1(ChartData::kZoneNames[i])));
            button->setCheckable(true);
            button->setChecked(i == zone_);
            connect(button, &QPushButton::clicked, this, [this, i] { zone_ = i; redraw(); });
            zoneButtons_.push_back(button);
            zones->addWidget(button);
        }
        zoneNote_ = new QLabel;
        zoneNote_->setProperty("toneHint", true);
        zones->addWidget(zoneNote_, 1);
        charts->addWidget(zoneRow);

        // The map and the sweep side by side, each with the full height.
        auto* pair = new QHBoxLayout;
        pair->setSpacing(6);
        auto* mapColumn = new QVBoxLayout;
        mapColumn->setSpacing(4);
        mapColumn->addWidget(heading(QStringLiteral("Where colours land on the display (u′v′)")));
        map_ = new ColourChart(ColourChart::Kind::Map);
        mapColumn->addWidget(map_, 1);
        pair->addLayout(mapColumn, 1);
        auto* divider = new QWidget;
        divider->setObjectName(QStringLiteral("toneChartsDivider"));
        divider->setAttribute(Qt::WA_StyledBackground, true);
        divider->setFixedWidth(1);
        pair->addWidget(divider);
        auto* sweepColumn = new QVBoxLayout;
        sweepColumn->setSpacing(4);

        auto* sweepHead = new QHBoxLayout;
        sweepHead->setSpacing(6);
        sweepHead->addWidget(heading(QStringLiteral("Saturation as the colour gets stronger")));
        sweepHead->addStretch(1);
        auto* patch = new QComboBox;
        patch->setObjectName(QStringLiteral("toneCharts.sweepPatch"));
        for (const auto& entry : ToneCurveProvider::Patches())
            patch->addItem(QString::fromLatin1(entry.name));
        patch->setCurrentIndex(static_cast<int>(patch_));
        connect(patch, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (index < 0) return;
            patch_ = static_cast<size_t>(index);
            redraw();
        });
        sweepHead->addWidget(patch);
        sweepColumn->addLayout(sweepHead);
        auto* sweepHelp = new QLabel(QStringLiteral(
            "Distance from white on screen over distance in the source: 1.00 keeps all its colour. "
            "Hollow points are clipped: the display could not reach that colour."));
        sweepHelp->setProperty("toneHint", true);
        sweepHelp->setWordWrap(true);
        sweepColumn->addWidget(sweepHelp);
        sweep_ = new ColourChart(ColourChart::Kind::Sweep);
        sweepColumn->addWidget(sweep_, 1);
        sweepColumn->addWidget(heading(QStringLiteral("How far each colour's hue turns")));
        auto* hueHelp = new QLabel(QStringLiteral(
            "Degrees round white on the map, in the zone shown. Plus turns red towards yellow, green "
            "towards cyan, blue towards magenta; minus the other way. Bars: the edited profile; "
            "ticks: the other lines. A faded swatch is a colour this scene cannot hold."));
        hueHelp->setProperty("toneHint", true);
        hueHelp->setWordWrap(true);
        sweepColumn->addWidget(hueHelp);
        hue_ = new ColourChart(ColourChart::Kind::Hue);
        sweepColumn->addWidget(hue_);
        pair->addLayout(sweepColumn, 1);
        charts->addLayout(pair, 1);
        auto* note = new QLabel(QStringLiteral(
            "The source is P3-mastered in a BT.2020 container. Every colour in a zone is driven "
            "equally hard: its strongest channel at the zone's nits, so a blue looks dimmer than a "
            "green. Mid-tones sit at the curve's knee, Peak at the scene's peak, the others evenly "
            "between in PQ. A dim ring is a colour this scene cannot hold. Not shown: contrast "
            "recovery, and the interpolation of VP Renderer's tone and gamut tables."));
        note->setProperty("toneHint", true);
        note->setWordWrap(true);
        charts->addWidget(note);
        charts->addStretch(0);
        pages_->addWidget(page);
        tabBar_->addTab(QStringLiteral("Colour"));
    }

    void redrawColour(const ToneCurveProvider::Request& request, const ToneCurveProvider::Result& result,
        const QString& editedName, const QColor& editedColor,
        const std::vector<std::tuple<QString, QColor, ToneCurveProvider::Request>>& others)
    {
        for (size_t i = 0; i < displayButtons_.size(); ++i)
            displayButtons_[i]->setChecked(static_cast<int>(i) == display_);
        for (size_t i = 0; i < zoneButtons_.size(); ++i)
            zoneButtons_[i]->setChecked(static_cast<int>(i) == zone_);
        const double zoneNits = ChartData::ZoneNits(request, result, zone_);
        zoneNote_->setText(QStringLiteral("strongest channel at %1 nits").arg(ToneCurveChart::Nits(zoneNits)));
        ColourChart::Data map, sweep;
        if (!ChartData::Colour(request, zoneNits, display_, zone_, patch_, editedName, editedColor,
            defaultChip_->isChecked(), others, map, sweep))
        { map_->clear(); sweep_->clear(); hue_->clear(); return; }
        hue_->setData(map);
        map_->setData(std::move(map));
        sweep_->setData(std::move(sweep));
    }

    static QLabel* heading(const QString& text)
    {
        auto* label = new QLabel(text);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        return label;
    }

    void showUnavailable(const QString& text)
    {
        unavailable_->setText(text);
        unavailable_->show();
        tabs_->hide();
    }

    void redraw()
    {
        if (!haveRequest_) return;
        const auto& functions = ToneCurveProvider::Registered();
        if (!functions.sample || !functions.nitsToPq || !functions.pqToNits)
        {
            showUnavailable(QStringLiteral("Charts need VP Renderer's files."));
            return;
        }
        if (!request_.betaRenderer)
        {
            showUnavailable(QStringLiteral("The charts show VP Renderer (Beta) only. Select it under Renderer to see them."));
            return;
        }
        ToneCurveProvider::Request request = request_;
        const int set = request.sceneAverageKnown ? 0 : 1;
        sceneRows_[set]->show();
        sceneRows_[1 - set]->hide();
        for (size_t i = 0; i < sceneButtons_[set].size(); ++i)
            sceneButtons_[set][i]->setChecked(i == sceneIndex_[set]);
        const ChartData::Scene& scene = ChartData::Scenes(set == 0)[sceneIndex_[set]];
        ChartData::PlaceInScene(scene, request);

        ToneCurveChart::Data chart;
        std::wstring error;
        if (!ChartData::Tone(request, chart, error))
        {
            showUnavailable(QStringLiteral("Charts need VP Renderer's files. %1")
                .arg(error.empty() ? QString() : QStringLiteral("Missing: %1").arg(QString::fromStdWString(error))));
            return;
        }
        unavailable_->hide();
        tabs_->show();

        // Compared profiles, as the page resolves them now. One whose profile
        // is gone is dropped; the edited profile is drawn bold, in its chip's
        // colour, from the page's live values.
        chart.showDefault = defaultChip_->isChecked();
        std::vector<std::tuple<QString, QColor, ToneCurveProvider::Request>> others; // for the Colour tab
        if (sources_.profiles && sources_.profileRequest && sources_.valuesRequest)
        {
            const auto profiles = sources_.profiles();
            const QString current = sources_.current ? sources_.current() : QString();
            const size_t before = compared_.size();
            compared_.erase(std::remove_if(compared_.begin(), compared_.end(), [&](const Compared& compared) {
                return !compared.fromFile && std::none_of(profiles.begin(), profiles.end(),
                    [&](const auto& profile) { return profile.first.compare(compared.section, Qt::CaseInsensitive) == 0; });
            }), compared_.end());
            if (compared_.size() != before) rebuildChips();
            followEdited(profiles, current);
            for (const Compared& compared : compared_)
            {
                if (IsEdited(compared))
                {
                    chart.profileName = compared.name;
                    chart.profileColor = compared.color;
                    continue;
                }
                ToneCurveProvider::Request other = compared.fromFile ?
                    sources_.valuesRequest(compared.values) : sources_.profileRequest(compared.section);
                ChartData::ShareScene(request, other);
                others.emplace_back(compared.name, compared.color, other);
                ChartData::AddCompare(compared.name, compared.color, other, chart);
            }
        }
        tone_->setData(chart);
        tracking_->setData(chart);
        if (tabBar_->currentIndex() == 1)
            redrawColour(request, chart.result, chart.profileName.isEmpty() ? QStringLiteral("Profile") :
                chart.profileName + QStringLiteral(" (editing)"), chart.profileColor, others);

        // What the scene is, then what libplacebo does to it beyond the curve.
        QStringList note;
        const auto& result = chart.result;
        QStringList parts;
        for (const auto& part : scene.mix)
            parts << QStringLiteral("%1% at %2 nits").arg(part.share * 100.0, 0, 'f', 0).arg(part.nits);
        note << (request.sceneAverageKnown ?
            QStringLiteral("The scene: %1, mastered at %2 nits. Peak detection measures an average of %3 nits for the knee and %4 nits for the dynamic target.")
                .arg(parts.join(QStringLiteral(", ")), ToneCurveChart::Nits(scene.mastering),
                    ToneCurveChart::Nits(result.scenePqAverageNits), ToneCurveChart::Nits(result.sceneLinearAverageNits)) :
            scene.mastering > 0 ?
            QStringLiteral("Peak detection is off: only the mastering peak, %1 nits; no scene average, so the fallback knee sets the knee.")
                .arg(ToneCurveChart::Nits(scene.mastering)) :
            QStringLiteral("Peak detection is off and there is no metadata: libplacebo assumes 10000 nits; no scene average, so the fallback knee sets the knee."));
        if (result.virtualTargetNits > 0)
            note << QStringLiteral("Dynamic target: tone mapped to a virtual %1 nits, then the whole picture ×%2.")
                .arg(ToneCurveChart::Nits(result.virtualTargetNits)).arg(result.exposure, 0, 'f', 2);
        if (result.masteringClip > 1e-3)
            note << QStringLiteral("Mastering soft clip at strength %1, from %2 nits.")
                .arg(result.masteringClip, 0, 'f', 2).arg(ToneCurveChart::Nits(0.95 * scene.mastering));
        note << QStringLiteral("Film strength and gamut overflow correction change colour, not this grey curve. "
            "The scene is not saved and is not a VP setting; VP smooths the measurement over time.");
        sceneNote_->setText(note.join(QLatin1Char(' ')));
    }

    QLabel* unavailable_ = nullptr;
    CompareSources sources_;
    std::vector<Compared> compared_;
    QString edited_; // the page's selected profile section
    int lastCompareId_ = 0;
    QToolButton* defaultChip_ = nullptr;
    QHBoxLayout* chips_ = nullptr;
    QPushButton* addCompare_ = nullptr;
    QWidget* tabs_ = nullptr; // the tab bar, the shared scene controls and the pages
    QTabBar* tabBar_ = nullptr;
    QVBoxLayout* shared_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    ToneCurveChart* tone_ = nullptr;
    ToneCurveChart* tracking_ = nullptr;
    QWidget* sceneRows_[2] = {}; // measured, static
    std::vector<QPushButton*> sceneButtons_[2];
    size_t sceneIndex_[2] = { 1, 0 }; // Mid; 1000 nits
    QLabel* sceneNote_ = nullptr;
    ColourChart* map_ = nullptr;
    ColourChart* sweep_ = nullptr;
    ColourChart* hue_ = nullptr;
    std::vector<QPushButton*> displayButtons_;
    int display_ = 1;          // P3-D65
    int zone_ = 1;             // Mid-tones
    std::vector<QPushButton*> zoneButtons_;
    QLabel* zoneNote_ = nullptr;
    size_t patch_ = 13;        // Light skin
    ToneCurveProvider::Request request_;
    bool haveRequest_ = false;
};
