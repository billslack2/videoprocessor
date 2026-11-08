// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// DV next step (f), planner answers 2026-10-17 section 5: the display profile.
// What the display VP feeds can do: peak, black, EOTF, gamut, link depth, and
// whether it performs its own dynamic mapping (a future DV-capable display VP
// would feed rather than tone-map for). A new display = a new profile.
//
// Pure: no renderer, GPU or libplacebo state. Today it is built only from the
// existing renderer target settings (no new configuration keys):
//   peak      <- target nits (the renderer's HDR->SDR target white)
//   black     <- the resolved target black (LibplaceboRenderParameters::ResolveTargetBlack)
//   eotf      <- the output transfer the renderer resolved for the target
//   gamut     <- the target primaries setting
//   linkDepth <- display_bit_depth ("8" / "10"; "auto" = 0, follows the swapchain)
//   doesOwnDynamicMapping <- always false (no display today maps for itself)
// Example: an LED projector on an 8-bit input link, VP tone maps (does-its-own-DM: no).
//
// Future [display.*] profile (not read yet; keys reserved here so the parser
// lands as a profile change, not a rewrite):
//   [display.<name>]
//   peak_nits = 120            ; measured peak white
//   black_nits = 0.005         ; measured black
//   eotf = bt1886 | gamma22 | gamma24 | srgb | pq | hlg
//   gamut = bt709 | p3 | bt2020
//   link_bit_depth = 8 | 10 | 12
//   dynamic_mapping = off | on ; on = the display maps metadata itself; VP
//                              ; passes the scene descriptor on instead of tone mapping
// A profile value, when present, would replace the renderer-derived field;
// absent values keep the derivation above.

#include <string>

enum class DisplayEotf
{
    Unknown,
    Bt1886,
    Srgb,
    Gamma22,
    Gamma24,
    Pq,
    Hlg,
    Linear,
    Other,
};

enum class DisplayGamut
{
    Unknown,
    Bt709,
    DisplayP3,
    Bt2020,
};

inline const char* ToString(DisplayEotf eotf)
{
    switch (eotf)
    {
    case DisplayEotf::Bt1886: return "bt1886";
    case DisplayEotf::Srgb: return "srgb";
    case DisplayEotf::Gamma22: return "gamma22";
    case DisplayEotf::Gamma24: return "gamma24";
    case DisplayEotf::Pq: return "pq";
    case DisplayEotf::Hlg: return "hlg";
    case DisplayEotf::Linear: return "linear";
    case DisplayEotf::Other: return "other";
    default: return "unknown";
    }
}

inline const char* ToString(DisplayGamut gamut)
{
    switch (gamut)
    {
    case DisplayGamut::Bt709: return "bt709";
    case DisplayGamut::DisplayP3: return "p3";
    case DisplayGamut::Bt2020: return "bt2020";
    default: return "unknown";
    }
}

struct DisplayProfile
{
    float peakNits = 0.0f;
    float blackNits = 0.0f;
    DisplayEotf eotf = DisplayEotf::Unknown;
    DisplayGamut gamut = DisplayGamut::Unknown;
    int linkBitDepth = 0;               // 0 = auto / unknown (follows the swapchain)
    bool doesOwnDynamicMapping = false; // true: feed metadata, do not tone map
    bool fromRendererSettings = false;  // built from renderer target settings (no [display.*] profile)
};

// The renderer settings the profile is derived from today.
struct DisplayProfileRendererSettings
{
    double targetNits = 0.0;
    double resolvedBlackNits = 0.0;
    DisplayEotf outputEotf = DisplayEotf::Unknown;
    DisplayGamut targetGamut = DisplayGamut::Unknown;
    std::string displayBitDepth = "auto";
};

namespace DisplayProfiles
{
    // display_bit_depth: "8" / "10" force the link depth; anything else (auto) = 0.
    inline int ParseLinkBitDepth(const std::string& displayBitDepth)
    {
        if (displayBitDepth == "8") return 8;
        if (displayBitDepth == "10") return 10;
        return 0;
    }

    inline DisplayProfile FromRendererSettings(const DisplayProfileRendererSettings& settings)
    {
        DisplayProfile profile;
        profile.peakNits = static_cast<float>(settings.targetNits);
        profile.blackNits = static_cast<float>(settings.resolvedBlackNits);
        profile.eotf = settings.outputEotf;
        profile.gamut = settings.targetGamut;
        profile.linkBitDepth = ParseLinkBitDepth(settings.displayBitDepth);
        profile.doesOwnDynamicMapping = false;
        profile.fromRendererSettings = true;
        return profile;
    }
}
