#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

// Shared by the runtime parser and the comment-preserving Config editor.
// Normalized section/key names, in original file order. No disk writes here.
namespace ColorOutputProfileMigration
{
    using Settings = std::map<std::string, std::string>;
    using Sections = std::map<std::string, Settings>;
    struct Plan
    {
        Sections additions;
        std::vector<std::pair<std::string, std::string>> archives;
        bool Empty() const { return archives.empty(); }
    };
    inline bool IsProfile(const std::string& section, const std::string& root)
    {
        return section == root || (section.rfind(root + ".", 0) == 0 && section.size() > root.size() + 1 &&
            section.find('.', root.size() + 1) == std::string::npos);
    }
    inline bool IsSelector(const std::string& key)
    {
        return key == "when" || key == "shortcut" || key == "cycle_shortcut" ||
            key == "label" || key == "priority";
    }
    inline Plan Build(const Sections& sections, const std::vector<std::string>& order)
    {
        Plan plan;
        std::vector<std::string> outputs, colors;
        for (const auto& name : order)
        {
            if (IsProfile(name, "vprenderer.output")) outputs.push_back(name);
            if (IsProfile(name, "vprenderer.color")) colors.push_back(name);
        }
        if (outputs.empty()) return plan;
        // A literal root was the inherited baseline, even if it appeared later.
        const auto root = std::find(outputs.begin(), outputs.end(), "vprenderer.output");
        const auto& baseline = sections.at(root == outputs.end() ? outputs.front() : *root);
        if (colors.empty()) colors.push_back("vprenderer.color.default");
        for (const auto& color : colors)
        {
            auto& added = plan.additions[color];
            for (const auto& setting : baseline)
            {
                if (IsSelector(setting.first)) continue;
                // Already unified explicit values take precedence in mixed files.
                const auto existing = sections.find(color);
                if (existing == sections.end() || !existing->second.count(setting.first))
                    added.emplace(setting);
            }
        }
        std::set<std::string> reserved;
        for (const auto& section : sections) reserved.insert(section.first);
        for (const auto& output : outputs)
        {
            const std::string base = "legacy_output" + output.substr(17);
            std::string archived = base;
            for (int suffix = 2; reserved.count(archived); ++suffix)
                archived = base + "_" + std::to_string(suffix);
            reserved.insert(archived);
            plan.archives.emplace_back(output, archived);
        }
        return plan;
    }
    inline bool Apply(Sections& sections, std::vector<std::string>& order)
    {
        const auto plan = Build(sections, order);
        if (plan.Empty()) return false;
        for (const auto& item : plan.additions)
        {
            if (!sections.count(item.first)) order.push_back(item.first);
            for (const auto& setting : item.second) sections[item.first].emplace(setting);
        }
        for (const auto& item : plan.archives)
        {
            sections.emplace(item.second, sections.at(item.first));
            sections.erase(item.first);
            std::replace(order.begin(), order.end(), item.first, item.second);
        }
        return true;
    }
}
