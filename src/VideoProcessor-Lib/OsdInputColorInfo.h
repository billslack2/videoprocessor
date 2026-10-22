#pragma once

#include <VideoState.h>
#include <string>

// Input color diagnostics describe state actually accepted by this renderer,
// never a candidate retained for a replacement graph. This intentionally does
// not infer color primaries from RGB/YUV storage or the capture format.
namespace OsdInputColorInfo
{
struct Text
{
    std::wstring eotf = L"---";
    std::wstring colorspace = L"---";
};

class AcceptedState
{
public:
    void Reset() { valid_ = false; hdrAvailable_ = false; }
    void Publish(const VideoState* effective, bool rendererAccepted)
    {
        valid_ = rendererAccepted && effective && effective->valid;
        hdrAvailable_ = valid_ && effective->hdrData != nullptr;
        if (!valid_) return;
        eotf_ = effective->eotf;
        colorspace_ = effective->colorspace;
        if (hdrAvailable_) hdr_ = *effective->hdrData;
    }
    // Older plugins can lack the optional rendered-frame HDR export. Their
    // static fallback still uses the same accepted input publication as color.
    const HDRData* Hdr(bool rendererUsable) const
    {
        return valid_ && rendererUsable && hdrAvailable_ && eotf_ != EOTF::SDR &&
            eotf_ != EOTF::UNKNOWN ? &hdr_ : nullptr;
    }
    Text Describe(const VideoState* wire, bool rendererUsable) const
    {
        Text text;
        if (!wire || !wire->valid) return text;
        const bool usable = valid_ && rendererUsable;
        text.eotf = usable ? ToString(eotf_) : L"---";
        text.colorspace = usable ? ToString(colorspace_) : L"---";
        if (!usable || eotf_ != wire->eotf)
            text.eotf += std::wstring(L" (wire ") + ToString(wire->eotf) + L")";
        if (!usable || colorspace_ != wire->colorspace)
            text.colorspace += std::wstring(L" (wire ") + ToString(wire->colorspace) + L")";
        return text;
    }
private:
    bool valid_ = false;
    EOTF eotf_ = EOTF::UNKNOWN;
    ColorSpace colorspace_ = ColorSpace::UNKNOWN;
    bool hdrAvailable_ = false;
    HDRData hdr_{};
};
}
