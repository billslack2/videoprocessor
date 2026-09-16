#pragma once
#include "ConfigFile.h"

namespace BackendInputConfig
{
    // A backend section is an override, never another backend's shared default.
    inline bool TryGet(const ConfigFile& config, bool vpRenderer,
        const char* key, std::string& value)
    {
        const auto read = [&](const char* section)
        {
            return config.TryGetString(section, key, value) && !ConfigFile::Trim(value).empty();
        };
        if (read(vpRenderer ? "vprenderer.input_processing" : "directshow")) return true;
        if (vpRenderer && (read("vprenderer.input") || read("vprenderer"))) return true;
        return read("general") || read("command_line");
    }
}
