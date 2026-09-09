#pragma once
#include "ColorOutputProfileMigration.h"
#include <cctype>

// Shared plan for runtime loading and the comment-preserving Config editor.
// Rendering keeps processing policy; Color owns one complete calibration set.
namespace CalibrationProfileMigration
{
    using Settings = ColorOutputProfileMigration::Settings;
    using Sections = ColorOutputProfileMigration::Sections;
    struct Plan
    {
        Sections additions;
        std::vector<std::string> additionOrder;
        std::vector<std::pair<std::string, std::string>> archives;
        std::map<std::string, std::vector<std::string>> removals;
        bool Empty() const { return removals.empty(); }
    };

    inline bool IsKey(const std::string& key)
    {
        return key == "calibration_lut_enabled" || key == "calibration_lut_bt709" ||
            key == "calibration_lut_p3_d65" || key == "calibration_lut_bt2020" ||
            key == "hdr_tone_map_target_gamma" || key == "calibration_lut_input_transfer" ||
            key == "calibration_lut_input_gamma";
    }
    inline bool IsArchive(const std::string& section)
    {
        return section == "calibration_archive" ||
            section.rfind("calibration_archive.", 0) == 0 ||
            section.rfind("calibration_archive_", 0) == 0;
    }
    inline bool IsRendering(const std::string& section)
    {
        if (!ColorOutputProfileMigration::IsProfile(section, "vprenderer")) return false;
        for (const char* child : { "input", "input_processing", "scaling", "color", "output", "viewport", "zoom" })
            if (section == std::string("vprenderer.") + child) return false;
        return true;
    }
    inline std::string Lower(const std::string& value)
    {
        std::string result = value;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }
    inline Settings Defaults()
    {
        return { { "calibration_lut_enabled", "false" },
            { "calibration_lut_bt709", "none" }, { "calibration_lut_p3_d65", "none" },
            { "calibration_lut_bt2020", "none" }, { "hdr_tone_map_target_gamma", "2.2" } };
    }
    inline void OverlayCalibration(Settings& destination, const Settings& source)
    {
        for (const auto& value : source)
        {
            if (!IsKey(value.first) || value.first == "calibration_lut_input_gamma") continue;
            if (value.first == "calibration_lut_input_transfer")
            {
                if (!source.count("hdr_tone_map_target_gamma"))
                    destination["hdr_tone_map_target_gamma"] = Lower(value.second) == "display" ? "2.2" : Lower(value.second);
                continue;
            }
            std::string normalized = value.second;
            if (value.first == "calibration_lut_enabled")
            {
                normalized = Lower(normalized);
                if (normalized == "yes" || normalized == "on" || normalized == "1") normalized = "true";
                if (normalized == "no" || normalized == "off" || normalized == "0") normalized = "false";
            }
            else if (value.first == "hdr_tone_map_target_gamma" || Lower(normalized) == "none")
                normalized = Lower(normalized);
            destination[value.first] = normalized;
        }
    }
    inline void OverlayColor(Settings& destination, const Settings& source)
    {
        for (const auto& value : source)
        {
            if (ColorOutputProfileMigration::IsSelector(value.first) || IsKey(value.first)) continue;
            if (value.first == "sdr_target_primaries")
            {
                if (!source.count("target_primaries")) destination["target_primaries"] = value.second;
                continue;
            }
            destination[value.first] = value.second;
        }
    }
    inline Plan Build(const Sections& sections, const std::vector<std::string>& order)
    {
        Plan plan;
        std::vector<std::string> renderings, colors;
        std::set<std::string> reserved;
        for (const auto& item : sections) reserved.insert(item.first);
        for (const auto& name : order)
        {
            if (IsRendering(name))
            {
                renderings.push_back(name);
                for (const auto& value : sections.at(name))
                    if (IsKey(value.first)) plan.removals[name].push_back(value.first);
            }
            if (ColorOutputProfileMigration::IsProfile(name, "vprenderer.color")) colors.push_back(name);
        }
        if (plan.Empty()) return plan;
        const auto allocate = [&reserved](const std::string& base)
        {
            std::string name = base;
            for (int suffix = 2; reserved.count(name); ++suffix) name = base + "_" + std::to_string(suffix);
            reserved.insert(name);
            return name;
        };
        const auto renderingRoot = std::find(renderings.begin(), renderings.end(), "vprenderer");
        const std::string renderingBaseline = renderingRoot == renderings.end() ? renderings.front() : *renderingRoot;
        Settings baseline = Defaults();
        OverlayCalibration(baseline, sections.at(renderingBaseline));
        std::vector<std::pair<std::string, Settings>> contracts = { { renderingBaseline, baseline } };
        for (const auto& name : renderings)
        {
            Settings effective = baseline;
            OverlayCalibration(effective, sections.at(name));
            if (std::none_of(contracts.begin(), contracts.end(), [&effective](const std::pair<std::string, Settings>& item)
                { return item.second == effective; })) contracts.emplace_back(name, effective);
            if (plan.removals.count(name))
                plan.archives.emplace_back(name, allocate("calibration_archive." +
                    (name == "vprenderer" ? "rendering" : name.substr(11))));
        }
        if (colors.empty()) colors.push_back(allocate("vprenderer.color.default"));
        const auto colorRoot = std::find(colors.begin(), colors.end(), "vprenderer.color");
        const std::string colorBaseline = colorRoot == colors.end() ? colors.front() : *colorRoot;
        const auto originalBaseline = sections.find(colorBaseline);
        const Settings empty;
        const Settings& baseColor = originalBaseline == sections.end() ? empty : originalBaseline->second;
        for (const auto& color : colors)
        {
            const auto original = sections.find(color);
            const Settings& ownColor = original == sections.end() ? empty : original->second;
            Settings effectiveColor;
            OverlayColor(effectiveColor, baseColor);
            OverlayColor(effectiveColor, ownColor);
            Settings actualCalibration = baseline;
            OverlayCalibration(actualCalibration, baseColor);
            OverlayCalibration(actualCalibration, ownColor);
            plan.additionOrder.push_back(color);
            plan.additions[color];
            for (const auto& value : actualCalibration)
                if (!ownColor.count(value.first)) plan.additions[color].emplace(value);
            // Every displaced/distinct set remains directly selectable, including
            // the old baseline when an already unified Color value supersedes it.
            for (const auto& contract : contracts)
            {
                if (contract.second == actualCalibration) continue;
                const std::string colorName = color == "vprenderer.color" ? "default" : color.substr(17);
                const std::string renderingName = contract.first == "vprenderer" ? "default" : contract.first.substr(11);
                const std::string added = allocate("vprenderer.color." + colorName + "_lut_" + renderingName);
                plan.additionOrder.push_back(added);
                plan.additions[added] = effectiveColor;
                for (const auto& value : contract.second) plan.additions[added][value.first] = value.second;
            }
        }
        return plan;
    }
    inline bool Apply(Sections& sections, std::vector<std::string>& order)
    {
        const Plan plan = Build(sections, order);
        if (plan.Empty()) return false;
        for (const auto& item : plan.archives)
        {
            sections.emplace(item.second, sections.at(item.first));
            order.push_back(item.second);
        }
        for (const auto& item : plan.removals)
            for (const auto& key : item.second) sections[item.first].erase(key);
        for (const auto& name : plan.additionOrder)
        {
            if (!sections.count(name)) order.push_back(name);
            for (const auto& value : plan.additions.at(name)) sections[name].emplace(value);
        }
        return true;
    }
}
