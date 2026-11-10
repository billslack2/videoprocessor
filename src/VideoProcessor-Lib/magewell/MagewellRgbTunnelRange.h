#pragma once
#include <magewell/MagewellSdkApi.h>
#include <stdexcept>

// Capture-thread-owned lease. Treating both input and output as full-range
// prevents nominal-range expansion/clipping of an RGB8 data tunnel. Restore
// the exact prior override (including AUTO/UNKNOWN) before ordinary capture.
class MagewellRgbTunnelRange
{
public:
    MagewellRgbTunnelRange(const MagewellSdkApi& api, HCHANNEL channel)
        : api_(api), channel_(channel) {}
    MagewellRgbTunnelRange(const MagewellRgbTunnelRange&) = delete;
    MagewellRgbTunnelRange& operator=(const MagewellRgbTunnelRange&) = delete;
    bool Active() const { return active_; }
    MWCAP_VIDEO_QUANTIZATION_RANGE OriginalRange() const { return saved_; }
    void Enable()
    {
        if (active_) return;
        if (!api_.MWGetVideoInputQuantizationRange || !api_.MWSetVideoInputQuantizationRange ||
            api_.MWGetVideoInputQuantizationRange(channel_, &saved_) != MW_SUCCEEDED)
            throw std::runtime_error("Magewell cannot save the input range for raw DV capture");
        pending_ = true; // also restore if a failed write partially applied
        MWCAP_VIDEO_QUANTIZATION_RANGE actual = MWCAP_VIDEO_QUANTIZATION_UNKNOWN;
        if (api_.MWSetVideoInputQuantizationRange(channel_, MWCAP_VIDEO_QUANTIZATION_FULL) != MW_SUCCEEDED ||
            api_.MWGetVideoInputQuantizationRange(channel_, &actual) != MW_SUCCEEDED ||
            actual != MWCAP_VIDEO_QUANTIZATION_FULL)
            throw std::runtime_error("Magewell cannot establish byte-preserving RGB8 DV capture");
        active_ = true;
    }
    bool Restore() noexcept
    {
        if (!pending_) return true;
        // A transient SDK failure must not silently leave the user's override
        // changed. Bound retries; the caller reports persistent restoration failure.
        for (unsigned attempt = 0; attempt < 3; ++attempt)
        {
            MWCAP_VIDEO_QUANTIZATION_RANGE actual = MWCAP_VIDEO_QUANTIZATION_FULL;
            if (api_.MWSetVideoInputQuantizationRange(channel_, saved_) == MW_SUCCEEDED &&
                api_.MWGetVideoInputQuantizationRange(channel_, &actual) == MW_SUCCEEDED && actual == saved_)
            {
                pending_ = active_ = false;
                return true;
            }
        }
        return false;
    }
private:
    const MagewellSdkApi& api_;
    HCHANNEL channel_;
    MWCAP_VIDEO_QUANTIZATION_RANGE saved_ = MWCAP_VIDEO_QUANTIZATION_UNKNOWN;
    bool pending_ = false;
    bool active_ = false;
};
