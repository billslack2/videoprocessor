#pragma once
#include "ToneCurveProvider.h"
#include <HdrTargetLuminance.h>
#include <RendererProfileConfig.h>
#include <ToneMappingTuning.h>
#include <QString>
#include <functional>

// One Rendering / DTM profile's values as a tone-curve request: every chart
// reads a profile through this one mapping, so no two can disagree on what a
// profile means. Header-only, so the ConfigTests build of
// ConfigEditorWindow.cpp needs no new file.
namespace ToneProfileRequest
{
    // A chart request from any source of one profile's values: the edited
    // profile, another profile, or an exported profile file. `builtInDefault`
    // is what an unset Rendering key means; the host owns that table.
    inline ToneCurveProvider::Request From(const std::function<QString(const char*)>& get,
        const std::function<QString(const QString&)>& builtInDefault)
    {
        ToneCurveProvider::Request request;
        const auto choice = [&](const char* name, const char* fallback) {
            const QString chosen = get(name).toLower();
            return (chosen.isEmpty() ? QString::fromLatin1(fallback) : chosen).toStdString();
        };
        request.quality = choice("quality", "high");
        request.toneMapping = choice("tone_mapping", "auto");
        request.peakDetection = choice("peak_detection", "auto");
        const std::string peak = request.peakDetection != "auto" ? request.peakDetection :
            request.quality == "fast" ? "off" : request.quality == "balanced" ? "on" : "high_quality";
        request.sceneAverageKnown = peak != "off";
        ToneMappingTuning::Read([&](const char* name, std::string& raw) {
            raw = get(name).toStdString();
            return !raw.empty();
        }, request.toneTuning);
        const QString dynamicTarget = get("dynamic_target");
        RendererProfileConfig::ParseBoolean((dynamicTarget.isEmpty() ?
            builtInDefault(QStringLiteral("dynamic_target")) : dynamicTarget).toStdString(),
            request.dynamicTarget);
        request.gamutMapping = choice("gamut_mapping", "auto");
        const QString overflow = get("custom_tone_mapping");
        RendererProfileConfig::ParseBoolean((overflow.isEmpty() ?
            builtInDefault(QStringLiteral("custom_tone_mapping")) : overflow).toStdString(),
            request.gamutOverflowCorrection);
        double white = 0.0, black = HdrTargetLuminance::DefaultBlack;
        request.targetNits = HdrTargetLuminance::ParseWhite(get("sdr_target_nits").toStdString(), white) ?
            white : 203.0;
        const QString blackText = get("sdr_black_nits").toLower();
        if (!blackText.isEmpty()) HdrTargetLuminance::ParseBlack(blackText.toStdString(), request.targetNits, black);
        request.blackNits = black;
        return request;
    }
}
