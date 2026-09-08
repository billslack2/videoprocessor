#pragma once

#include "DisplayRefreshRatePolicy.h"
#include <cmath>
#include <cwctype>
#include <string>

namespace OsdTimingPolicy
{
// Shared OSD-only switch. Keep rolling averages, but publish available samples
// immediately by default. Renderer readiness/correction gates are independent.
constexpr bool WarmingEnabled = false;

// Capacity uses the known input format while reset temporarily invalidates
// the measured drift estimate. This fallback never feeds cadence correction.
inline double FrameBudgetRate(double measuredHz, double nominalHz)
{
    const auto valid = [](double hz) {
        return std::isfinite(hz) && hz >= 10.0 && hz <= 500.0;
    };
    return valid(measuredHz) ? measuredHz : (valid(nominalHz) ? nominalHz : 0.0);
}

inline double DisplayRate(DisplayRefreshRateInput input,
    bool warmingEnabled = WarmingEnabled)
{
    if (!warmingEnabled)
        input.stable = true;
    const auto result = EvaluateDisplayRefreshRate(input);
    if (result.decision == DisplayRefreshRateDecision::Quarantined ||
        result.decision == DisplayRefreshRateDecision::Unavailable)
        return 0.0;
    if (warmingEnabled)
        return result.selectedRateHz;
    return input.fresh && std::isfinite(input.candidateRateHz) &&
        input.candidateRateHz >= 10.0 && input.candidateRateHz <= 240.0 ?
        input.candidateRateHz : 0.0;
}

inline std::wstring Status(std::wstring text,
    bool warmingEnabled = WarmingEnabled)
{
    if (warmingEnabled)
        return text;
    std::wstring lower = text;
    for (auto& character : lower)
        character = static_cast<wchar_t>(std::towlower(character));
    // Do not claim renderer readiness when its status is still warming.
    if (lower.find(L"warming") != std::wstring::npos ||
        lower.find(L"settling") != std::wstring::npos)
        return L"Measuring";
    return text;
}
}
