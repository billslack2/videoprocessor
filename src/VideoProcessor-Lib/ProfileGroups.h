#pragma once
#include <string>
#include <vector>

// One identity table for model groups, activation commands and live indicators.
// Adding a target-model group here automatically gives its editor live status.
namespace ProfileGroups
{
    struct Definition
    {
        const char* name;
        const char* section;
        bool inheritRoot;
        bool targetModel = true;
        bool shader = false;
        bool multiSelection = false;
        const char* expectedShaderType = nullptr;
    };
    inline const std::vector<Definition>& Definitions()
    {
        static const std::vector<Definition> definitions = {
            { "input", "vprenderer.input", true },
            { "scaling", "vprenderer.scaling", true },
            { "motion", "vprenderer.motion", true },
            { "display", "vprenderer", false },
            { "color", "vprenderer.color", true },
            { "viewport", "vprenderer.viewport", true },
            { "zoom", "vprenderer.zoom", true },
            { "subtitles", "vprenderer.subtitles", true },
            { "queue", "queue", true },
            { "lldv", "lldv", true },
            { "output_display", "output_display", true },
            { "output", "vprenderer.output", true, false },
            { "nls", "shader.nls", false, false, true, false, "nls" },
            { "standard_shaders", "shader.standard", false, false, true, true, "custom" }
        };
        return definitions;
    }
    inline const Definition* ForSection(const std::string& section)
    {
        for (const auto& group : Definitions())
            if (section == group.section) return &group;
        return nullptr;
    }
    inline const Definition* ForGroup(const std::string& name)
    {
        for (const auto& group : Definitions())
            if (name == group.name) return &group;
        return nullptr;
    }
}
