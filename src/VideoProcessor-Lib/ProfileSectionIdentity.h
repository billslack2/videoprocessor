#pragma once
#include "ConfigFile.h"

namespace ProfileSectionIdentity
{
    inline std::string Resolve(const ConfigFile& config, const std::string& root,
        const std::string& profile)
    {
        if (profile.empty()) return {};
        const std::string name = ConfigFile::NormalizeName(profile);
        return name == "base" && config.HasSection(root) ? root : root + "." + name;
    }

    inline bool Validate(const ConfigFile& config, const std::string& root, std::string& error)
    {
        if (!config.HasSection(root) || !config.HasSection(root + ".base")) return true;
        error = "[" + root + ".base] conflicts with [" + root +
            "]'s internal Base profile; rename the named Base profile and update its references";
        return false;
    }
}
