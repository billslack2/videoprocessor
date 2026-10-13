#pragma once
#include <ToneMappingTuning.h>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

// GUI-only protections for the tone settings. They block Save and Apply in the
// Config GUI and never change what VP's loader accepts, so no saved
// configuration stops loading. ToneMappingTuning::Validate runs first and keeps
// its own rules, scene threshold high above low among them.
namespace ToneGuiRules
{
    struct Problem
    {
        std::string key;
        std::string message;
    };

    inline std::string Number(double value)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << value;
        return text.str();
    }

    // `get(key)` returns the configured text; empty or "auto" means Auto.
    template<class Getter> inline bool Check(Getter get, Problem& problem)
    {
        using namespace ToneMappingTuning;
        const auto effective = [&](int index) {
            Override value;
            // An unparsable value is Validate's to report; judge the default here.
            if (!Parse(index, get(Specs()[index].key), value)) return Specs()[index].defaultValue;
            return value.value_or(Specs()[index].defaultValue);
        };
        // The value VP runs, Auto included. On a bound, libplacebo's knee becomes
        // a step and float rounding picks between two very different curves.
        const double low = effective(KneeMinimum), high = effective(KneeMaximum);
        const double knee = effective(KneeDefault);
        if (!(knee > low && knee < high))
        {
            problem = { "knee_default", "Fallback knee (" + Number(knee) +
                ") must lie strictly between knee minimum (" + Number(low) +
                ") and knee maximum (" + Number(high) + "). Move it, or move the bounds." };
            return false;
        }
        // libplacebo treats a percentile of 0 exactly as 100.
        Override percentile;
        if (Parse(Percentile, get(Specs()[Percentile].key), percentile) && percentile && *percentile == 0.0)
        {
            problem = { "percentile",
                "A percentile of 0 means the same as 100. Use 100, or a value between 0 and 100." };
            return false;
        }
        return true;
    }

    // The peak-detection mode a percentile stands for, or null. libplacebo's
    // high-quality mode differs from the standard one only by percentile 99.995.
    inline const char* ModeForPercentile(double percentile)
    {
        if (percentile == 100.0) return "on";
        if (std::abs(percentile - 99.995) < 1e-9) return "high_quality";
        return nullptr;
    }

    // What makes a hidden setting visible again.
    inline const char* ShowHint(int index)
    {
        using namespace ToneMappingTuning;
        if (index < SlopeTuning) return "choosing Spline or ST2094-40";
        if (index < KneeOffset) return "choosing Spline";
        if (index == KneeOffset) return "choosing BT.2390";
        if (index == ReinhardContrast) return "choosing Reinhard";
        return "turning peak detection on";
    }
}
