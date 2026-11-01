#pragma once
#include "ConfigFile.h"
#include <sstream>

// Compare all parsed configuration, including unmodeled detector/shader keys,
// except the two destination-only Screen controls. Length prefixes avoid
// delimiter ambiguity; retain section order because default profiles use it.
inline std::string PicturePlacementConfigurationContract(const ConfigFile& config)
{
    std::ostringstream out;
    for (const auto& section : config.GetSectionNames()) {
        out << 'S' << section.size() << ':' << section;
        const auto* values=config.GetSectionValues(section);
        if (!values) continue;
        const bool screen=section=="vprenderer.viewport" || section.find("vprenderer.viewport.")==0;
        for (const auto& value : *values) {
            if (screen && (value.first=="picture_alignment_mode" || value.first=="picture_offset_pixels")) continue;
            out << 'K' << value.first.size() << ':' << value.first << value.second.size() << ':' << value.second;
        }
        out << 'E';
    }
    return out.str();
}

template<class Settings, class Fingerprint>
bool IsPicturePlacementOnlyUpdate(const Settings& current, const Settings& next,
    Fingerprint fingerprint)
{
    if (current.autoPictureAlignment==next.autoPictureAlignment &&
        current.pictureOffsetPixels==next.pictureOffsetPixels) return false;
    if (current.placementConfigurationContract!=next.placementConfigurationContract) return false;
    auto normalized=next;
    normalized.autoPictureAlignment=current.autoPictureAlignment;
    normalized.pictureOffsetPixels=current.pictureOffsetPixels;
    normalized.configurationIdentity=current.configurationIdentity;
    return fingerprint(current)==fingerprint(normalized);
}
