#pragma once

#include <VideoState.h>
#include <video_frame_formatter/IVideoFrameFormatter.h>

#include <array>
#include <cmath>

namespace SourceMasteringPrimariesPolicy
{
    enum class Selection
    {
        Keep,
        DisplayP3D65,
        Bt709,
        Bt2020,
        Cie,
    };

    struct Decision
    {
        Selection selection = Selection::Keep;
        std::array<double, 8> cie{}; // red, green, blue, white x/y
        const char* outcome = "";
    };

    inline bool HasCompletePrimaries(const HDRData* hdr)
    {
        if (!hdr) return false;
        const double points[] = {
            hdr->displayPrimaryRedX, hdr->displayPrimaryRedY,
            hdr->displayPrimaryGreenX, hdr->displayPrimaryGreenY,
            hdr->displayPrimaryBlueX, hdr->displayPrimaryBlueY,
            hdr->whitePointX, hdr->whitePointY,
        };
        for (double point : points)
            if (!(point > 0.0) || !std::isfinite(point)) return false;
        return true;
    }

    inline Decision Keep(const char* outcome)
    {
        Decision result;
        result.outcome = outcome;
        return result;
    }

    inline Decision UseCie(const std::array<double, 8>& cie,
        const char* outcome)
    {
        Decision result;
        result.selection = Selection::Cie;
        result.cie = cie;
        result.outcome = outcome;
        return result;
    }

    inline Decision Resolve(const VideoState& state,
        const DolbyVisionTunnelFrameMetadata& dolby,
        bool tunnelSdrFallback, bool assumeP3)
    {
        if (state.eotf != EOTF::PQ || tunnelSdrFallback)
            return {};

        const HDRData* hdr = state.hdrData.get();
        if (state.dolbyVisionTunnel)
        {
            if (!hdr || !hdr->primariesFilledByVp)
                return Keep("forced (Dolby Vision)");
            const bool l9Malformed = dolby.valid && dolby.sourcePrimariesMalformed;
            const bool hasL9 = dolby.valid && dolby.hasSourcePrimaries && !l9Malformed;
            if (!assumeP3)
                return Keep(l9Malformed ?
                    "VP default BT.2020 (Dolby Vision, malformed L9 ignored)" :
                    hasL9 ? "VP default BT.2020 (Dolby Vision, L9 present)" :
                        "VP default BT.2020 (Dolby Vision, no L9)");
            if (l9Malformed)
                return Keep("VP default BT.2020 (Dolby Vision, malformed L9 ignored)");
            if (!hasL9)
            {
                Decision result;
                result.selection = Selection::DisplayP3D65;
                result.outcome = "assumed P3-D65 (Dolby Vision, no L9)";
                return result;
            }

            static const std::array<std::array<double, 8>, 9> presetCie = {{
                {{0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329}},
                {{0.64, 0.33, 0.30, 0.60, 0.15, 0.06, 0.3127, 0.329}},
                {{0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.329}},
                {{0.63, 0.34, 0.31, 0.595, 0.155, 0.07, 0.3127, 0.329}},
                {{0.64, 0.33, 0.29, 0.60, 0.15, 0.06, 0.3127, 0.329}},
                {{0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.314, 0.351}},
                {{0.7347, 0.2653, 0.0, 1.0, 0.0001, -0.077, 0.32168, 0.33767}},
                {{0.73, 0.28, 0.14, 0.855, 0.10, -0.05, 0.3127, 0.329}},
                {{0.766, 0.275, 0.225, 0.80, 0.089, -0.087, 0.3127, 0.329}},
            }};
            static const char* const presetNames[] = {
                "Dolby Vision L9 index 0, DCI-P3 D65",
                "Dolby Vision L9 index 1, BT.709",
                "Dolby Vision L9 index 2, BT.2020",
                "Dolby Vision L9 index 3, SMPTE-C",
                "Dolby Vision L9 index 4, BT.601",
                "Dolby Vision L9 index 5, DCI-P3",
                "Dolby Vision L9 index 6, ACES",
                "Dolby Vision L9 index 7, S-Gamut",
                "Dolby Vision L9 index 8, S-Gamut-3.Cine"
            };

            if (dolby.sourcePrimaryIndex == 255)
            {
                if (!dolby.hasCustomSourcePrimaries)
                    return Keep("L9 custom primaries missing coordinates, VP default BT.2020");
                std::array<double, 8> cie{};
                for (size_t i = 0; i < cie.size(); ++i)
                {
                    const uint16_t coordinate = dolby.sourcePrimaryCoordinates[i];
                    if (coordinate == 0)
                        return Keep("L9 custom primaries invalid, VP default BT.2020");
                    cie[i] = static_cast<double>(coordinate) / 32767.0;
                }
                return UseCie(cie, "Dolby Vision L9 custom primaries");
            }
            if (dolby.sourcePrimaryIndex < presetCie.size())
            {
                if (dolby.sourcePrimaryIndex == 0)
                {
                    Decision result;
                    result.selection = Selection::DisplayP3D65;
                    result.outcome = presetNames[0];
                    return result;
                }
                if (dolby.sourcePrimaryIndex == 1)
                {
                    Decision result;
                    result.selection = Selection::Bt709;
                    result.outcome = presetNames[1];
                    return result;
                }
                if (dolby.sourcePrimaryIndex == 2)
                {
                    Decision result;
                    result.selection = Selection::Bt2020;
                    result.outcome = presetNames[2];
                    return result;
                }
                return UseCie(presetCie[dolby.sourcePrimaryIndex],
                    presetNames[dolby.sourcePrimaryIndex]);
            }
            return Keep("Unsupported Dolby Vision L9; VP default BT.2020");
        }

        if (hdr && hdr->IsValid())
            return Keep("declared");
        const bool forced = HasCompletePrimaries(hdr);
        if (!assumeP3)
            return Keep(forced ?
                "container (forced primaries without luminance ignored)" :
                "container (no block)");
        if (forced)
        {
            return UseCie({
                hdr->displayPrimaryRedX, hdr->displayPrimaryRedY,
                hdr->displayPrimaryGreenX, hdr->displayPrimaryGreenY,
                hdr->displayPrimaryBlueX, hdr->displayPrimaryBlueY,
                hdr->whitePointX, hdr->whitePointY,
            }, "forced (no luminance)");
        }
        Decision result;
        result.selection = Selection::DisplayP3D65;
        result.outcome = "assumed P3-D65 (no block)";
        return result;
    }
}
