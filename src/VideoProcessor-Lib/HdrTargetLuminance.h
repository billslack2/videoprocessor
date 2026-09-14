#pragma once

#include <cmath>
#include <string>

// libplacebo nominal luminance bounds (colorspace.c): PL_COLOR_HDR_BLACK
// through PQ 1.0 (10000 nits). Zero is unspecified to libplacebo, so VP's
// explicit zero black continues to resolve to the positive black floor.
namespace HdrTargetLuminance
{
    constexpr double BlackFloor = 1e-6;
    constexpr double Maximum = 10000.0;

    inline bool ValidWhite(double value)
    {
        return std::isfinite(value) && value > BlackFloor && value <= Maximum &&
            static_cast<float>(value) > static_cast<float>(BlackFloor);
    }

    inline bool ParseWhite(const std::string& text, double& value)
    {
        try
        {
            size_t consumed = 0;
            value = std::stod(text, &consumed);
            return consumed == text.size() && ValidWhite(value);
        }
        catch (const std::exception&) { return false; }
    }

    inline bool ValidBlack(double value, double white)
    {
        const double effective = value < BlackFloor ? BlackFloor : value;
        return std::isfinite(value) && value >= 0.0 && ValidWhite(white) &&
            effective < white && static_cast<float>(effective) < static_cast<float>(white);
    }
}
