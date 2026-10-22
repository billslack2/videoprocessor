#pragma once

#include <ConfigFile.h>
#include <DisplayRuleExpression.h>
#include <vprenderer/NativeStatsOverlayPlacement.h>

// Shared renderer ingress policy. UI/schema validation rejects bad persisted
// values; direct/plugin callers still retain the established 100% baseline.
namespace StatsOsdSettings
{
inline float ReadSizePercent(const ConfigFile* configuration)
{
    if (!configuration) return NativeStatsOverlayPlacement::kDefaultStatsSizePercent;
    std::string raw;
    double value = 0.0;
    if (!configuration->TryGetString("general", "stats_osd_size_percent", raw) ||
        !DisplayRuleExpression::ParseNumber(ConfigFile::Trim(raw), value))
        return NativeStatsOverlayPlacement::kDefaultStatsSizePercent;
    if (!std::isfinite(value) || value < NativeStatsOverlayPlacement::kMinimumStatsSizePercent ||
        value > NativeStatsOverlayPlacement::kMaximumStatsSizePercent)
        return NativeStatsOverlayPlacement::kDefaultStatsSizePercent;
    return NativeStatsOverlayPlacement::NormalizeStatsSizePercent(static_cast<float>(value));
}
}