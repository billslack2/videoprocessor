#pragma once

#include <ConfigFile.h>
#include <DisplayRuleExpression.h>
#include <vprenderer/NativeStatsOverlayPlacement.h>

// Shared renderer/editor ingress. Persisted percentages retain their original
// calibration; the default150 calibration is shown as100% by the editor.
namespace StatsOsdSettings
{
inline float ParseSizePercent(const std::string& raw)
{
    double value = 0.0;
    if (!DisplayRuleExpression::ParseNumber(ConfigFile::Trim(raw), value) ||
        !std::isfinite(value) || value < NativeStatsOverlayPlacement::kMinimumStatsSizePercent ||
        value > NativeStatsOverlayPlacement::kMaximumStatsSizePercent)
        return NativeStatsOverlayPlacement::kDefaultStatsSizePercent;
    return NativeStatsOverlayPlacement::NormalizeStatsSizePercent(static_cast<float>(value));
}

inline float ReadSizePercent(const ConfigFile* configuration)
{
    if (!configuration) return NativeStatsOverlayPlacement::kDefaultStatsSizePercent;
    std::string raw;
    return configuration->TryGetString("general", "stats_osd_size_percent", raw)
        ? ParseSizePercent(raw) : NativeStatsOverlayPlacement::kDefaultStatsSizePercent;
}

inline int UiPercentFromCalibration(float calibration)
{
    return static_cast<int>(std::lround(NativeStatsOverlayPlacement::ToDisplayStatsSizePercent(calibration)));
}

inline float CalibrationFromUiPercent(int display)
{
    return NativeStatsOverlayPlacement::FromDisplayStatsSizePercent(static_cast<float>(display));
}
}