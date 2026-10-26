#pragma once
#include "ColourChart.h"
#include "ToneCurveChart.h"
#include "ToneCurveProvider.h"
#include <QColor>
#include <QString>
#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

// A scene and a profile turned into what the charts draw, apart from any
// window: the charts only draw it, and every number comes from the engine.
// Header-only, so the ConfigTests build of ConfigEditorWindow.cpp needs no
// new file.
namespace ChartData
{
    constexpr const char* kDisplayNames[3] = { "Rec.709", "P3-D65", "BT.2020" };
    constexpr int kSamples = 360;
    constexpr int kZoneCount = 5;
    constexpr const char* kZoneNames[kZoneCount] = { "Shadows", "Mid-tones", "Bright", "Highlights", "Peak" };

    // A scene the charts imagine, in nits. Measured scenes are a mix of
    // brightnesses, its brightest part the peak, for a source mastered at
    // `mastering`; peak detection measures both of its averages from the mix.
    // The shares are a stand-in until real scenes are measured. Static scenes
    // have only the InfoFrame mastering peak (0: none, read as 10000).
    struct Scene
    {
        const char* name;
        std::vector<ToneCurveProvider::ScenePart> mix;
        double mastering;

        double peak() const
        {
            if (mix.empty())
                return mastering > 0 ? mastering : 10000.0;
            double brightest = 0.0;
            for (const auto& part : mix)
                brightest = (std::max)(brightest, part.nits);
            return brightest;
        }
    };
    inline const std::vector<Scene>& Scenes(bool measured)
    {
        static const std::vector<Scene> withDetection = {
            { "Dark", { { 0.98, 1.7 }, { 0.02, 200 } }, 1000 },
            { "Mid", { { 0.97, 13 }, { 0.03, 600 } }, 1000 },
            { "Bright", { { 0.95, 51 }, { 0.05, 1000 } }, 1000 },
            { "Very bright", { { 0.90, 120 }, { 0.10, 1000 } }, 1000 },
            { "Hot master", { { 0.95, 50 }, { 0.05, 1300 } }, 1000 } };
        static const std::vector<Scene> withoutDetection = {
            { "1000 nits", {}, 1000 }, { "4000 nits", {}, 4000 }, { "No metadata", {}, 0 } };
        return measured ? withDetection : withoutDetection;
    }

    // The profile's request in the scene, sampled across the whole PQ range.
    inline void PlaceInScene(const Scene& scene, ToneCurveProvider::Request& request)
    {
        request.scenePeakNits = scene.peak();
        request.sceneMix = scene.mix;
        request.masteringPeakNits = scene.mastering;
        request.sourcePq.resize(kSamples + 1);
        for (int i = 0; i <= kSamples; ++i) request.sourcePq[i] = static_cast<double>(i) / kSamples;
    }

    // Another profile in the same scene as `placed`.
    inline void ShareScene(const ToneCurveProvider::Request& placed, ToneCurveProvider::Request& other)
    {
        other.scenePeakNits = placed.scenePeakNits;
        other.sceneMix = placed.sceneMix;
        other.masteringPeakNits = placed.masteringPeakNits;
        other.sourcePq = placed.sourcePq;
    }

    // The Brightness charts' data for a placed request: its curve, the
    // display peak and the axis ticks. False, with the engine's error, when
    // the engine cannot run.
    inline bool Tone(const ToneCurveProvider::Request& request, ToneCurveChart::Data& chart, std::wstring& error)
    {
        const auto& functions = ToneCurveProvider::Registered();
        if (!functions.sample(request, chart.result, error))
            return false;
        chart.sourcePq = request.sourcePq;
        chart.targetPq = functions.nitsToPq(request.targetNits);
        chart.sceneAverage = request.sceneAverageKnown;
        chart.pqToNits = functions.pqToNits;
        for (double nits : { 0.1, 1.0, 10.0, 100.0, 400.0, 1000.0, 4000.0, 10000.0 })
            chart.xTicks.emplace_back(functions.nitsToPq(nits), ToneCurveChart::Nits(nits));
        for (double nits : { 0.1, 1.0, 10.0, 25.0, 50.0, 75.0, 100.0, 150.0, 200.0, 300.0,
            400.0, 600.0, 1000.0, 2000.0, 4000.0 })
            if (nits < request.targetNits * 0.95)
                chart.yTicks.emplace_back(functions.nitsToPq(nits), ToneCurveChart::Nits(nits));
        chart.yTicks.emplace_back(chart.targetPq, ToneCurveChart::Nits(request.targetNits));
        return true;
    }

    // A compared profile's curve, already in the scene, added to the chart.
    inline void AddCompare(const QString& name, const QColor& color, const ToneCurveProvider::Request& other,
        ToneCurveChart::Data& chart)
    {
        const auto& functions = ToneCurveProvider::Registered();
        ToneCurveProvider::Result result;
        std::wstring ignored;
        if (functions.sample(other, result, ignored))
            chart.compares.push_back({ name, color, result.profilePq, functions.nitsToPq(other.targetNits) });
    }

    // The zone, from the edited profile's scene: Shadows halfway in PQ from
    // the scene's floor to the knee, Mid-tones the knee, Bright and
    // Highlights a third and two thirds of the way on to the peak.
    inline double ZoneNits(const ToneCurveProvider::Request& request, const ToneCurveProvider::Result& result, int zone)
    {
        const auto& functions = ToneCurveProvider::Registered();
        const double kneePq = result.kneePq, peakPq = (std::max)(result.scenePeakPq, kneePq);
        const double zonePq[kZoneCount - 1] = { (result.inputMinPq + kneePq) / 2.0, kneePq,
            kneePq + (peakPq - kneePq) / 3.0, kneePq + (peakPq - kneePq) * 2.0 / 3.0 };
        return zone == kZoneCount - 1 ? request.scenePeakNits : functions.pqToNits(zonePq[zone]);
    }

    // The Colour charts' data: the map at the zone and the chosen patch's
    // sweep, for the edited profile first, libplacebo's default when shown, then
    // the others. False when the edited profile cannot be drawn.
    inline bool Colour(const ToneCurveProvider::Request& request, double zoneNits, int display, int zone, size_t patch,
        const QString& editedName, const QColor& editedColor, bool showDefault,
        const std::vector<std::tuple<QString, QColor, ToneCurveProvider::Request>>& others,
        ColourChart::Data& map, ColourChart::Data& sweep)
    {
        const auto& functions = ToneCurveProvider::Registered();
        if (!functions.colour || !functions.corners) return false;
        const auto gamut = static_cast<ToneCurveProvider::Gamut>(display);
        if (!functions.corners(gamut, map.display) ||
            !functions.corners(ToneCurveProvider::Gamut::Rec709, map.rec709) ||
            !functions.corners(ToneCurveProvider::Gamut::Bt2020, map.bt2020))
            return false;
        map.displayName = QString::fromLatin1(kDisplayNames[display]);
        map.mapNits = sweep.mapNits = zoneNits;
        map.mapLabel = sweep.mapLabel = QString::fromLatin1(kZoneNames[zone]);
        sweep.patch = patch;
        std::vector<ToneCurveProvider::ColourSample> mapSamples, sweepSamples;
        for (size_t i = 0; i < ToneCurveProvider::Patches().size(); ++i)
            mapSamples.push_back({ i, zoneNits });
        // From 0.1 nits to the scene peak (or the mastering peak without
        // detection), evenly in PQ, the sweep's scale.
        const double bottomPq = functions.nitsToPq(0.1), topPq = functions.nitsToPq((std::max)(request.scenePeakNits, 10.0));
        constexpr int kSweep = 96;
        for (int i = 0; i <= kSweep; ++i)
        {
            const double nits = functions.pqToNits(bottomPq + (topPq - bottomPq) * i / kSweep);
            sweep.sweepNits.push_back(nits);
            sweepSamples.push_back({ patch, nits });
        }
        const auto add = [&](const QString& name, const QColor& color, bool bold, bool dashed,
            const ToneCurveProvider::Request& from, bool libplaceboDefault) {
            ColourChart::Series onMap{ name, color, bold, dashed, {} }, onSweep = onMap;
            std::wstring ignored;
            if (!functions.colour(from, libplaceboDefault, gamut, mapSamples, onMap.points, ignored) ||
                !functions.colour(from, libplaceboDefault, gamut, sweepSamples, onSweep.points, ignored))
                return false;
            map.series.push_back(std::move(onMap));
            sweep.series.push_back(std::move(onSweep));
            return true;
        };
        // The edited profile first: the map links its points to the rings.
        if (!add(editedName, editedColor, true, false, request, false)) return false;
        if (showDefault)
            add(QStringLiteral("Default"), QColor(QStringLiteral("#e8a93b")), false, true, request, true);
        for (const auto& [name, color, other] : others)
            add(name, color, false, false, other, false);
        map.sweepNits = sweep.sweepNits;
        return true;
    }
}
