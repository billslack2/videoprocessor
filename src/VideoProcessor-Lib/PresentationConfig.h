#pragma once
#include "ConfigFile.h"
#include "ConfigSchema.h"

// Device-wide DXGI pending-present cap, independent of the capture FIFO.
namespace PresentationConfig
{
    constexpr int DefaultDepth = 2;
    constexpr int MaximumDepth = 8;
    inline bool Validate(const ConfigFile& config, std::string& error)
    {
        const std::vector<ConfigSchema::KeyRule> rules = {
            ConfigSchema::Boolean("present_every_vsync"),
            ConfigSchema::Integer("presentation_queue_depth", 1, MaximumDepth)
        };
        // Validate only owned fields here; other owners validate their fields.
        for (const auto& rule : rules) {
            std::string value;
            if (config.TryGetString("general", rule.key, value) && !rule.validator(value)) {
                error = std::string("[general] key '") + rule.key + "' has an invalid presentation value";
                return false;
            }
        }
        return true;
    }
    inline int Depth(const ConfigFile& config)
    {
        std::string value;
        if (!config.TryGetString("general", "presentation_queue_depth", value) ||
            !ConfigSchema::Integer("presentation_queue_depth", 1, MaximumDepth).validator(value))
            return DefaultDepth;
        return std::stoi(value);
    }
    inline unsigned BufferCount(unsigned appliedDepth, bool blit)
    {
        return blit ? 1u : appliedDepth + 1u;
    }
}
