#pragma once
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// Shared by the runtime parser and the comment-preserving Config editor.
// Normalized section/key names, in original file order. No disk writes here.
//
// Quality is retired. Missing preset defaults and explicit Auto choices are
// materialized using the effective old High, Balanced or Fast preset. Explicit
// choices are retained; output-dependent dither precision uses "output".
// Debanding, dithering and dither depth move from Rendering
// profiles to Scaling. VP applies [vprenderer], then the chosen Scaling profile,
// then the chosen Rendering profile, so a non-default Rendering profile's values
// are carried into a Scaling copy with the same selectors. Nothing here ever
// writes ewa_lanczos4sharpest.
namespace QualityProfileMigration
{
    using Settings = std::map<std::string, std::string>;
    using Sections = std::map<std::string, Settings>;
    struct Plan
    {
        // New sections, appended in this order, by the name a reader should see.
        std::vector<std::string> newSections;
        // (normalized section, key, value); a new section is named by its normalized name.
        std::vector<std::tuple<std::string, std::string, std::string>> sets;
        std::vector<std::pair<std::string, std::string>> removals;
        std::vector<std::string> warnings;
        bool Empty() const { return newSections.empty() && sets.empty() && removals.empty(); }
    };

    inline std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
    inline std::string Get(const Sections& sections, const std::string& section, const std::string& key)
    {
        const auto found = sections.find(section);
        if (found == sections.end()) return {};
        const auto value = found->second.find(key);
        return value == found->second.end() ? std::string() : value->second;
    }
    // True only for a concrete choice. An absent value may inherit; an explicit
    // Auto resets to the selected preset and must not inherit a concrete choice.
    inline bool IsSet(const Sections& sections, const std::string& section, const std::string& key)
    {
        const std::string value = Lower(Get(sections, section, key));
        return !value.empty() && value != "auto";
    }
    inline std::vector<std::string> Variants(const std::vector<std::string>& order, const std::string& root)
    {
        static const std::set<std::string> children = { "input", "input_processing", "scaling",
            "motion", "color", "output", "viewport", "zoom", "subtitles" };
        std::vector<std::string> variants;
        for (const auto& name : order)
        {
            if (name.size() <= root.size() + 1 || name.compare(0, root.size() + 1, root + ".") != 0) continue;
            const std::string rest = name.substr(root.size() + 1);
            if (rest.find('.') != std::string::npos || (root == "vprenderer" && children.count(rest))) continue;
            variants.push_back(name);
        }
        return variants;
    }

    inline const std::vector<std::string>& ScalingPresetKeys()
    {
        static const std::vector<std::string> keys = { "upscaler", "downscaler", "sigmoid",
            "deband_strength", "dithering" };
        return keys;
    }
    inline const std::vector<std::string>& TonePresetKeys()
    {
        static const std::vector<std::string> keys = { "peak_detection", "contrast_recovery" };
        return keys;
    }
    inline const std::vector<std::string>& MovedKeys()
    {
        static const std::vector<std::string> keys = { "deband_strength", "dithering", "display_bit_depth" };
        return keys;
    }
    // The retired presets in VP's own values (LibplaceboRenderParameters.cpp
    // QualityPreset; libplacebo pl_render_fast_params / pl_render_default_params).
    inline const char* PresetValue(const std::string& quality, const std::string& key)
    {
        static const std::map<std::string, const char*> fast = {
            { "upscaler", "none" }, { "downscaler", "gpu" }, { "sigmoid", "off" },
            { "deband_strength", "off" }, { "dithering", "off" },
            { "peak_detection", "off" }, { "contrast_recovery", "0" } };
        static const std::map<std::string, const char*> balanced = {
            { "upscaler", "lanczos" }, { "downscaler", "hermite" }, { "sigmoid", "on" },
            { "deband_strength", "off" }, { "dithering", "blue_noise" },
            { "peak_detection", "on" }, { "contrast_recovery", "0" } };
        static const std::map<std::string, const char*> high = {
            { "upscaler", "ewa_lanczossharp" }, { "downscaler", "hermite" }, { "sigmoid", "on" },
            { "deband_strength", "default" }, { "dithering", "blue_noise" },
            { "peak_detection", "high_quality" }, { "contrast_recovery", "0.3" } };
        if (key == "display_bit_depth") return "output";
        const auto* table = quality == "fast" ? &fast : quality == "balanced" ? &balanced :
            quality == "high" ? &high : nullptr;
        if (!table) return nullptr;
        const auto found = table->find(key);
        return found == table->end() ? nullptr : found->second;
    }
    inline bool ResetsPreset(const std::string& key, const std::string& raw)
    {
        const std::string value = Lower(raw);
        return value == "auto" || (key == "downscaler" && (value == "none" || value == "ewa_lanczos"));
    }
    inline bool IsPreset(const std::string& quality) { return quality == "fast" || quality == "balanced" || quality == "high"; }

    // A Rendering profile's own value for a moved key, reading the legacy deband switch.
    inline std::string MovedValue(const Sections& sections, const std::string& section, const std::string& key,
        const std::string& quality)
    {
        if (IsSet(sections, section, key)) return Get(sections, section, key);
        if (Lower(Get(sections, section, key)) == "auto")
            if (const char* value = PresetValue(quality, key)) return value;
        if (key != "deband_strength") return {};
        const std::string legacy = Lower(Get(sections, section, "deband"));
        if (legacy == "on" || legacy == "true") return "default";
        if (legacy == "off" || legacy == "false") return "off";
        if (legacy == "auto")
            if (const char* value = PresetValue(quality, key)) return value;
        return {};
    }

    inline Plan Build(const Sections& sections, const std::vector<std::string>& order)
    {
        Plan plan;
        const std::vector<std::string> display = Variants(order, "vprenderer");
        const bool displayRoot = sections.count("vprenderer") != 0;
        std::vector<std::string> scaling = Variants(order, "vprenderer.scaling");
        const bool scalingRoot = sections.count("vprenderer.scaling") != 0;

        std::vector<std::string> rendering = display;
        if (displayRoot) rendering.insert(rendering.begin(), "vprenderer");
        if (scalingRoot) scaling.insert(scaling.begin(), "vprenderer.scaling");
        if (rendering.empty() && scaling.empty()) return plan;

        Sections work = sections;
        auto set = [&](const std::string& section, const std::string& key, const std::string& value) {
            if (Get(work, section, key) == value) return;
            work[section][key] = value;
            plan.sets.emplace_back(section, key, value);
        };
        auto remove = [&](const std::string& section, const std::string& key) {
            if (!work[section].count(key)) return;
            work[section].erase(key);
            plan.removals.emplace_back(section, key);
        };

        // The Rendering fallback: the literal root, else the first Rendering profile.
        const std::string fallback = displayRoot ? "vprenderer" : (display.empty() ? std::string() : display.front());
        // The Scaling baseline every Scaling profile inherits. Created only when needed.
        std::string baseline = scaling.empty() ? std::string() : scaling.front();
        const std::vector<std::string> scalingBefore = scaling;
        auto ensureBaseline = [&]() {
            if (!baseline.empty()) return;
            baseline = "vprenderer.scaling.default";
            plan.newSections.push_back("vprenderer.scaling.Default");
            work[baseline];
        };
        std::map<std::string, std::set<std::string>> presetFilled;
        const auto quality = [&](const std::string& section) {
            std::string result = Lower(Get(sections, section, "quality"));
            if (result.empty()) result = Lower(Get(sections, fallback, "quality"));
            return result.empty() ? std::string("high") : result;
        };
        // These legacy Rendering overrides still apply after Scaling at runtime.
        // Resolve explicit Auto before retiring Quality, without inventing new
        // Rendering overrides for absent scaling fields.
        for (const auto& section : rendering)
            for (const char* key : { "upscaler", "downscaler", "sigmoid" })
                if (ResetsPreset(key, Get(sections, section, key)))
                    if (const char* value = PresetValue(quality(section), key)) set(section, key, value);

        // 1. The Rendering fallback's values go under every Scaling profile.
        if (!fallback.empty())
        {
            for (const auto& key : MovedKeys())
            {
                const std::string value = MovedValue(sections, fallback, key, quality(fallback));
                if (value.empty()) continue;
                ensureBaseline();
                if (!IsSet(work, baseline, key)) set(baseline, key, value);
                for (const auto& section : scaling)
                    if (IsSet(work, section, key) && Lower(Get(work, section, key)) != Lower(value))
                        plan.warnings.push_back("[" + section + "] keeps its own " + key +
                            "; [" + fallback + "] " + key + " '" + value + "' no longer overrides it");
            }
            const std::string q = quality(fallback);
            if (IsPreset(q))
            {
                for (const auto& key : ScalingPresetKeys())
                {
                    if (IsSet(sections, fallback, key) || !MovedValue(sections, fallback, key, quality(fallback)).empty()) continue;
                    ensureBaseline();
                    if (IsSet(work, baseline, key)) continue;
                    set(baseline, key, PresetValue(q, key));
                    presetFilled[baseline].insert(key);
                }
                for (const auto& key : TonePresetKeys())
                    if (!IsSet(sections, fallback, key))
                    {
                        set(fallback, key, PresetValue(q, key));
                        presetFilled[fallback].insert(key);
                    }
            }
        }

        // Materialize the baseline even when Quality was omitted (old High).
        // Named Scaling profiles retain their ordinary inheritance, while an
        // explicit Auto there resets the preset instead of inheriting the baseline.
        ensureBaseline();
        for (const auto& key : ScalingPresetKeys())
            if (!IsSet(work, baseline, key))
                if (const char* value = PresetValue(quality(baseline), key))
                {
                    set(baseline, key, value);
                    presetFilled[baseline].insert(key);
                }
        if (!IsSet(work, baseline, "display_bit_depth")) set(baseline, "display_bit_depth", "output");
        for (const auto& section : scaling)
        {
            const bool ownPreset = IsPreset(Lower(Get(sections, section, "quality")));
            for (const auto& key : ScalingPresetKeys())
            {
                const std::string raw = Lower(Get(sections, section, key));
                if (!ResetsPreset(key, raw) && !(ownPreset && raw.empty())) continue;
                if (const char* value = PresetValue(quality(section), key))
                {
                    // An explicit Rendering fallback was moved here above and
                    // has precedence over the old Scaling Auto.
                    if (section == baseline && (key == "deband_strength" || key == "dithering") &&
                        !MovedValue(sections, fallback, key, quality(fallback)).empty()) continue;
                    set(section, key, value);
                    presetFilled[section].insert(key);
                }
            }
            if (Lower(Get(sections, section, "display_bit_depth")) == "auto" &&
                (section != baseline || MovedValue(sections, fallback, "display_bit_depth", quality(fallback)).empty()))
                set(section, "display_bit_depth", "output");
        }

        // 3. Other Rendering profiles: tone values stay, scaling values go to a copy.
        std::set<std::string> reserved;
        for (const auto& section : work) reserved.insert(section.first);
        // A named Base clashes with a literal [vprenderer.scaling] (ProfileSectionIdentity).
        reserved.insert("vprenderer.scaling.base");
        std::set<std::string> scalingShortcuts, scalingCycles;
        for (const auto& section : scalingBefore)
        {
            if (IsSet(sections, section, "shortcut")) scalingShortcuts.insert(Lower(Get(sections, section, "shortcut")));
            if (IsSet(sections, section, "cycle_shortcut")) scalingCycles.insert(Lower(Get(sections, section, "cycle_shortcut")));
        }
        bool scalingRules = false;
        for (const auto& section : scalingBefore)
            scalingRules = scalingRules || (section != baseline && IsSet(sections, section, "when"));
        // Unset here means unset in the profile and in the fallback it inherits.
        const auto inherits = [&](const std::string& section, const std::string& key) {
            return !IsSet(sections, section, key) && (fallback.empty() || !IsSet(sections, fallback, key));
        };
        for (const auto& profile : display)
        {
            if (profile == fallback) continue;
            const std::string q = quality(profile);
            for (const auto& key : TonePresetKeys())
            {
                const std::string own = Lower(Get(sections, profile, key));
                if (own != "auto" && !inherits(profile, key)) continue;
                if (IsPreset(q)) set(profile, key, PresetValue(q, key));
            }

            Settings copy;
            for (const auto& key : MovedKeys())
            {
                const std::string value = MovedValue(sections, profile, key, quality(profile));
                if (!value.empty()) copy[key] = value;
            }
            const auto explicitInBaseline = [&](const std::string& key) {
                return !baseline.empty() && IsSet(work, baseline, key) && !presetFilled[baseline].count(key);
            };
            for (const auto& key : ScalingPresetKeys())
            {
                if (copy.count(key) || !inherits(profile, key) || explicitInBaseline(key)) continue;
                if (IsPreset(q))
                {
                    const std::string desired = PresetValue(q, key);
                    if (Lower(Get(work, baseline, key)) != desired) copy[key] = desired;
                }
            }
            if (!copy.empty())
            {
                const std::string name = profile.substr(std::string("vprenderer.").size());
                std::string when = Get(sections, profile, "when");
                std::string shortcut = Get(sections, profile, "shortcut");
                std::string cycle = Get(sections, profile, "cycle_shortcut");
                if (!shortcut.empty() && scalingShortcuts.count(Lower(shortcut)))
                {
                    plan.warnings.push_back("[" + profile + "] shortcut '" + shortcut +
                        "' is already a Scaling shortcut; its Scaling copy is not given it");
                    shortcut.clear();
                }
                if (!cycle.empty() && scalingCycles.count(Lower(cycle))) cycle.clear();
                if (when.empty() && shortcut.empty() && cycle.empty())
                {
                    plan.warnings.push_back("[" + profile + "] has no rule or free shortcut to select a "
                        "Scaling copy; its debanding, dithering and Quality fall back to the Scaling defaults");
                }
                else
                {
                    ensureBaseline();
                    std::string target = "vprenderer.scaling." + name;
                    for (int suffix = 2; reserved.count(target); ++suffix)
                        target = "vprenderer.scaling." + name + "_" + std::to_string(suffix);
                    reserved.insert(target);
                    plan.newSections.push_back(target);
                    work[target];
                    if (!when.empty()) set(target, "when", when);
                    if (!shortcut.empty())
                    {
                        set(target, "shortcut", shortcut);
                        scalingShortcuts.insert(Lower(shortcut));
                    }
                    if (!cycle.empty())
                    {
                        set(target, "cycle_shortcut", cycle);
                        scalingCycles.insert(Lower(cycle));
                    }
                    if (IsSet(sections, profile, "label")) set(target, "label", Get(sections, profile, "label"));
                    for (const auto& value : copy) set(target, value.first, value.second);
                    if (scalingRules)
                        plan.warnings.push_back("[" + target + "] comes after the existing Scaling profiles; "
                            "when one of their rules also matches, that profile wins");
                }
            }
        }

        // 4. Nothing keeps the retired or moved keys.
        for (const auto& section : rendering)
            for (const char* key : { "quality", "deband", "deband_strength", "dithering", "display_bit_depth" })
                remove(section, key);
        for (const auto& section : scaling)
            remove(section, "quality");
        return plan;
    }

    inline std::vector<std::string> Apply(Sections& sections, std::vector<std::string>& order)
    {
        const auto plan = Build(sections, order);
        for (const auto& section : plan.newSections)
        {
            const std::string name = Lower(section);
            if (sections.count(name)) continue;
            sections[name];
            order.push_back(name);
        }
        for (const auto& item : plan.sets)
            sections[std::get<0>(item)][std::get<1>(item)] = std::get<2>(item);
        for (const auto& item : plan.removals)
            sections[item.first].erase(item.second);
        return plan.warnings;
    }
}
