#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <locale>
#include <sstream>
#include <string>

// libplacebo 7.360.1 tone constants and registered peak-option bounds.
// Missing keys inherit; AUTO explicitly restores the native preset value.
namespace ToneMappingTuning
{
    enum Key { KneeAdaptation, KneeMinimum, KneeMaximum, KneeDefault,
        SlopeTuning, SlopeOffset, SplineContrast, KneeOffset, ReinhardContrast,
        Percentile, SmoothingPeriod, SceneThresholdLow, SceneThresholdHigh,
        BlackCutoff, Count };
    struct Spec {
        const char* key;
        const char* label;
        double minimum, maximum, defaultValue;
        const char* unit;
        const char* help;
    };
    inline const std::array<Spec, Count>& Specs()
    {
        static const std::array<Spec, Count> specs = {{
            {"knee_adaptation", "Knee adaptation", 0, 1, .4, "", "How strongly the knee follows scene-average brightness. Also used by ST2094-40."},
            {"knee_minimum", "Knee minimum", .000001, .499999, .1, "PQ fraction", "Lower knee bound; greater than 0 and less than 0.5. Also used by ST2094-40."},
            {"knee_maximum", "Knee maximum", .500001, .999999, .8, "PQ fraction", "Upper knee bound; greater than 0.5 and less than 1. Also used by ST2094-40."},
            {"knee_default", "Fallback knee", 0, 1, .4, "PQ fraction", "Used only without scene-average metadata. Must lie between the knee bounds. Also used by ST2094-40."},
            {"slope_tuning", "Slope tuning", 0, 10, 1.5, "", "Shapes the slope as source and display peaks differ."},
            {"slope_offset", "Slope offset", 0, 1, .2, "", "Offset used to shape the spline slope."},
            {"spline_contrast", "Spline contrast", 0, 1.5, .5, "", "Higher preserves midtones at the cost of shadow/highlight detail."},
            {"knee_offset", "BT.2390 knee offset", .5, 2, 1, "", "BT.2390 only. Library default: 1; specification behavior: 0.5."},
            {"reinhard_contrast", "Reinhard contrast", .000001, .999999, .5, "", "Reinhard only. Greater than 0 and less than 1."},
            {"percentile", "Peak percentile", 0, 100, 100, "%", "100 or 0 measures the brightest pixel. Lower values can clip bright details."},
            {"smoothing_period", "Smoothing period", 0, 1000, 20, "frames", "Higher reacts more slowly. 0 disables smoothing."},
            {"scene_threshold_low", "Scene threshold: low", 0, 100, 1, "% PQ", "Start reducing smoothing. Either threshold at 0 disables scene-change handling."},
            {"scene_threshold_high", "Scene threshold: high", 0, 100, 3, "% PQ", "Finish reducing smoothing. Must exceed low when both are nonzero."},
            {"black_cutoff", "Black cutoff", 0, 100, 1, "% PQ", "Exclude very dark pixels from measurement. 0 disables the cutoff."}
        }};
        return specs;
    }
    // The shared host library is C++14; keep optional values portable across
    // the host, renderer, and C++17 configuration editor.
    struct Override {
        bool specified = false;
        double number = 0;
        Override() = default;
        Override(double value) : specified(true), number(value) {}
        Override& operator=(double value) { specified = true; number = value; return *this; }
        explicit operator bool() const { return specified; }
        double operator*() const { return number; }
        bool has_value() const { return specified; }
        double value_or(double fallback) const { return specified ? number : fallback; }
        void reset() { specified = false; number = 0; }
        bool operator==(const Override& other) const { return specified == other.specified && (!specified || number == other.number); }
        bool operator!=(const Override& other) const { return !(*this == other); }
    };
    using Overrides = std::array<Override, Count>;
    inline int Find(const std::string& key) {
        for (int i = 0; i < Count; ++i) if (key == Specs()[i].key) return i;
        return -1;
    }
    inline bool IsAuto(std::string value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return true;
        value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value.empty() || value == "auto";
    }
    inline bool Parse(int index, const std::string& text, Override& value) {
        if (IsAuto(text)) { value.reset(); return true; }
        std::istringstream stream(text); stream.imbue(std::locale::classic());
        double number = 0;
        if (!(stream >> number) || !std::isfinite(number)) return false;
        stream >> std::ws;
        const Spec& spec = Specs()[index];
        if (!stream.eof() || number < spec.minimum || number > spec.maximum) return false;
        value = number; return true;
    }
    template<class Getter> inline void Read(Getter get, Overrides& values) {
        for (int i = 0; i < Count; ++i) {
            std::string raw;
            if (get(Specs()[i].key, raw)) {
                Override parsed;
                if (Parse(i, raw, parsed)) values[i] = parsed;
            }
        }
    }
    template<class Getter> inline bool Validate(Getter get, std::string& error, std::string& key) {
        Overrides overrides{};
        std::array<double, Count> values{};
        for (int i = 0; i < Count; ++i) {
            const auto& spec = Specs()[i];
            const std::string raw = get(spec.key);
            if (!Parse(i, raw, overrides[i])) {
                key = spec.key;
                std::ostringstream message; message.imbue(std::locale::classic());
                message << spec.label << " must be Auto or a number from " << spec.minimum << " to " << spec.maximum;
                error = message.str(); return false;
            }
            values[i] = overrides[i].value_or(spec.defaultValue);
        }
        if (overrides[KneeDefault] && (values[KneeDefault] < values[KneeMinimum] || values[KneeDefault] > values[KneeMaximum])) {
            key = "knee_default"; error = "Fallback knee must lie between the effective knee minimum and maximum"; return false;
        }
        if (values[SceneThresholdLow] > 0 && values[SceneThresholdHigh] > 0 && values[SceneThresholdLow] >= values[SceneThresholdHigh]) {
            key = "scene_threshold_high"; error = "Scene threshold high must exceed low when both are nonzero"; return false;
        }
        return true;
    }
    inline std::string Fingerprint(const Overrides& values) {
        std::ostringstream result; result.imbue(std::locale::classic()); result.precision(17);
        for (const auto& value : values) { if (value) result << *value; else result << "auto"; result << '|'; }
        return result.str();
    }
}
