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
// High defaults and explicit Auto resets can be written as concrete values.
// Processing settings migrate to their visible Scaling owner. Legacy preset
// fallbacks are preserved when flattening them would change independent Scaling
// precedence. The editor and runtime share this lossless plan; neither load writes
// the user file. Nothing here selects ewa_lanczos4sharpest.
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

    inline bool ProcessingKey(const std::string& key)
    {
        return key == "upscaler" || key == "downscaler" || key == "sigmoid" ||
            key == "deband" || key == "deband_strength" || key == "dithering" ||
            key == "display_bit_depth";
    }

    // A preset is a fallback below explicit settings from independently selected
    // groups. Flattening a non-High preset into Rendering overrides would change
    // their precedence. Keep these legacy inputs readable until the operator
    // replaces them with explicit choices. Invalid presets must reach validation.
    inline bool NeedsPresetCompatibility(const Sections& sections)
    {
        for (const auto& section : sections)
        {
            if (section.first != "vprenderer" && section.first.rfind("vprenderer.", 0) != 0) continue;
            const std::string preset = Lower(Get(sections, section.first, "quality"));
            if (!preset.empty() && preset != "high") return true;
            // The older deband toggle can interact with an independently chosen
            // strength. Retain its original precedence instead of replacing it.
            if (!Get(sections, section.first, "deband").empty()) return true;
        }
        return false;
    }

    inline Plan BuildPresetDefaults(const Sections& sections, const std::vector<std::string>& order)
    {
        Plan plan;
        // Preserve the complete legacy fallback relationship, including Auto
        // resets, rather than attempting a lossy conversion across selections.
        if (NeedsPresetCompatibility(sections)) return plan;
        auto rendering = Variants(order, "vprenderer");
        auto scaling = Variants(order, "vprenderer.scaling");
        if (sections.count("vprenderer")) rendering.insert(rendering.begin(), "vprenderer");
        if (sections.count("vprenderer.scaling")) scaling.insert(scaling.begin(), "vprenderer.scaling");
        if (rendering.empty() && scaling.empty()) return plan;
        Sections work = sections;
        const auto set = [&](const std::string& section, const std::string& key, const std::string& value) {
            if (Get(work, section, key) == value) return;
            work[section][key] = value;
            plan.sets.emplace_back(section, key, value);
        };
        const auto remove = [&](const std::string& section, const std::string& key) {
            if (!work[section].erase(key)) return;
            plan.removals.emplace_back(section, key);
        };
        if (scaling.empty())
        {
            scaling.push_back("vprenderer.scaling.default");
            plan.newSections.push_back("vprenderer.scaling.Default");
        }
        // Ordinary defaults belong to Scaling. Existing Rendering overrides
        // remain attached to their owner; no copied selectors or second state.
        for (const auto& section : scaling)
        {
            for (const auto& key : ScalingPresetKeys())
            {
                const std::string raw = Get(work, section, key);
                if (ResetsPreset(key, raw) || (section == scaling.front() && raw.empty()))
                    set(section, key, PresetValue("high", key));
            }
            const std::string depth = Lower(Get(work, section, "display_bit_depth"));
            if (depth == "auto" || (section == scaling.front() && depth.empty()))
                set(section, "display_bit_depth", "output");
            remove(section, "quality");
        }
        for (const auto& section : rendering)
        {
            for (const auto& key : ScalingPresetKeys())
                if (ResetsPreset(key, Get(work, section, key)))
                    set(section, key, PresetValue("high", key));
            if (Lower(Get(work, section, "display_bit_depth")) == "auto")
                set(section, "display_bit_depth", "output");
            const auto legacy = Lower(Get(work, section, "deband"));
            if (Get(work, section, "deband_strength").empty() && !legacy.empty())
            {
                if (legacy == "on" || legacy == "true" || legacy == "yes" || legacy == "1")
                    set(section, "deband_strength", "default");
                else if (legacy == "off" || legacy == "false" || legacy == "no" || legacy == "0")
                    set(section, "deband_strength", "off");
                else if (legacy == "auto") set(section, "deband_strength", "default");
                else continue; // Leave malformed legacy input for validation.
                remove(section, "deband");
            }
            for (const auto& key : TonePresetKeys())
            {
                const auto raw = Lower(Get(work, section, key));
                // Missing named values inherit their original Rendering baseline.
                if (raw == "auto" || (section == rendering.front() && raw.empty()))
                    set(section, key, PresetValue("high", key));
            }
            remove(section, "quality");
        }
        return plan;
    }

    // VP-0215: Scaling is the sole processing owner. Load old Rendering values
    // into visible Scaling profiles, retaining the old higher-precedence value.
    // This plan changes memory only; the editor persists it on Save/Apply.
    inline Plan Build(const Sections& sections, const std::vector<std::string>& order)
    {
        Plan plan = BuildPresetDefaults(sections, order);
        Sections work = sections;
        std::vector<std::string> names = order;
        for (const auto& name : plan.newSections) {
            const auto normalized = Lower(name); work[normalized]; names.push_back(normalized);
        }
        for (const auto& item : plan.sets) work[std::get<0>(item)][std::get<1>(item)] = std::get<2>(item);
        for (const auto& item : plan.removals) work[item.first].erase(item.second);
        auto rendering = Variants(names, "vprenderer");
        auto scaling = Variants(names, "vprenderer.scaling");
        if (work.count("vprenderer")) rendering.insert(rendering.begin(), "vprenderer");
        if (work.count("vprenderer.scaling")) scaling.insert(scaling.begin(), "vprenderer.scaling");
        if (rendering.empty()) return plan;
        const auto set = [&](const std::string& owner, const std::string& key, const std::string& value) {
            if (Get(work, owner, key) == value) return;
            work[owner][key] = value; plan.sets.emplace_back(owner, key, value);
        };
        const auto quality = [&](const std::string& owner) {
            std::string value = Lower(Get(sections, owner, "quality"));
            if (value.empty()) value = Lower(Get(sections, rendering.front(), "quality"));
            return IsPreset(value) ? value : std::string("high");
        };
        if (scaling.empty()) {
            scaling.push_back("vprenderer.scaling.default");
            plan.newSections.push_back("vprenderer.scaling.Default");
            work[scaling.front()]; names.push_back(scaling.front());
            for (const auto& key : ScalingPresetKeys())
                set(scaling.front(), key, key == "dithering" ? "blue_noise" : PresetValue(quality(rendering.front()), key));
            set(scaling.front(), "display_bit_depth", "output");
        }
        const auto processing = [&](const std::string& owner) {
            Settings values;
            for (const char* key : { "upscaler", "downscaler", "sigmoid", "chroma_upscaler", "deband_strength", "dithering", "display_bit_depth" }) {
                std::string raw = Lower(Get(work, owner, key));
                if (raw.empty() && std::string(key) == "deband_strength") {
                    raw = Lower(Get(work, owner, "deband"));
                    if (raw == "on" || raw == "true" || raw == "yes" || raw == "1") raw = "default";
                    else if (raw == "false" || raw == "no" || raw == "0") raw = "off";
                }
                if (raw.empty()) continue;
                if (std::string(key) == "dithering") {
                    if (raw == "auto" || raw == "on" || raw == "true") raw = "blue_noise";
                    else if (raw == "false") raw = "off";
                } else if (raw == "auto" || (std::string(key) == "downscaler" && (raw == "none" || raw == "ewa_lanczos"))) {
                    if (const auto* value = PresetValue(quality(owner), key)) raw = value;
                    else if (std::string(key) == "chroma_upscaler") raw = "bicubic";
                }
                values[key] = raw;
            }
            return values;
        };
        for (size_t index = 0; index < rendering.size(); ++index) {
            const auto& source = rendering[index];
            const auto values = processing(source);
            if (values.empty()) continue;
            std::string target = scaling.front();
            if (index == 0) {
                // Rendering defaults used to override every independently
                // selected Scaling profile. Preserve that precedence on move.
                for (const auto& selected : scaling)
                    for (const auto& item : values) set(selected, item.first, item.second);
                for (const char* key : { "shortcut", "cycle_shortcut", "when" }) {
                    const auto saved = Get(work, source, key);
                    if (!saved.empty() && Get(work, scaling.front(), key).empty()) set(scaling.front(), key, saved);
                }
            } else {
                const auto suffix = source.substr(std::string("vprenderer.").size());
                target = "vprenderer.scaling." + suffix;
                bool import = true;
                if (!work.count(target)) {
                    bool redundant = true;
                    for (const auto& item : values)
                        redundant = redundant && Lower(Get(work, scaling.front(), item.first)) == item.second;
                    if (!redundant) {
                        work[target]; plan.newSections.push_back(target); scaling.push_back(target); names.push_back(target);
                    } else import = false;
                }
                if (import) {
                    for (const auto& item : values) set(target, item.first, item.second);
                    for (const char* key : { "label", "shortcut", "cycle_shortcut", "when" }) {
                        const auto saved = Get(work, source, key);
                        if (!saved.empty() && Get(work, target, key).empty()) set(target, key, saved);
                    }
                }
            }
            for (const char* key : { "upscaler", "downscaler", "sigmoid", "chroma_upscaler", "deband", "deband_strength", "dithering", "display_bit_depth" })
                if (work[source].erase(key)) plan.removals.emplace_back(source, key);
        }
        const auto finish = BuildPresetDefaults(work, names);
        plan.newSections.insert(plan.newSections.end(), finish.newSections.begin(), finish.newSections.end());
        plan.sets.insert(plan.sets.end(), finish.sets.begin(), finish.sets.end());
        plan.removals.insert(plan.removals.end(), finish.removals.begin(), finish.removals.end());
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
