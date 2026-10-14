#pragma once
#include "ConfigFile.h"
#include <string>
#include <cmath>
namespace MotionConfig {
struct Settings {
    std::string smoothMotion="off", mixer="oversample", deinterlace="auto";
    std::string algorithm="bwdif", fieldOrder="auto", cadence="field";
    bool skipSpatial=false;
    std::string Fingerprint() const {
        return smoothMotion+"|"+mixer+"|"+deinterlace+"|"+algorithm+"|"+fieldOrder+"|"+cadence+"|"+(skipSpatial?"1":"0");
    }
};
inline void Read(const ConfigFile& config,const std::string& section,Settings& settings) {
    auto read=[&](const char* key,std::string& dst){std::string text;if(config.TryGetString(section,key,text))dst=ConfigFile::NormalizeName(text);};
    read("smooth_motion",settings.smoothMotion);read("frame_mixer",settings.mixer);
    read("deinterlace",settings.deinterlace);read("deinterlace_algorithm",settings.algorithm);
    read("field_order",settings.fieldOrder);read("deinterlace_cadence",settings.cadence);
    config.TryGetBool(section,"deinterlace_skip_spatial",settings.skipSpatial);
}
inline bool Mix(const Settings& settings,double sourceHz,double displayHz) {
    if(settings.smoothMotion=="off"||!std::isfinite(sourceHz)||!std::isfinite(displayHz)||sourceHz<=0||displayHz<=0)return false;
    if(settings.smoothMotion=="always")return true;
    const double ratio=displayHz/sourceHz;
    return std::abs(ratio-std::round(ratio))>0.002 || ratio<0.998;
}
}
