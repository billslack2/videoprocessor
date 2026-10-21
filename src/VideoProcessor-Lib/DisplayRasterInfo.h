#pragma once
#include "DisplayRefreshRatePolicy.h"
#include <mutex>
#include <string>
#include <limits>
#include <vector>
namespace DisplayRasterInfo
{
struct Snapshot
{
    DisplayOutputModeState mode{};
    bool hasMode = false;
    uint32_t drawnWidth = 0, drawnHeight = 0;
};
// Publication is coherent, and a read-back started before a display notification
// cannot resurrect the previous monitor's mode. No Windows calls occur here.
class Cache
{
public:
    uint64_t Generation() const { std::lock_guard<std::mutex> lock(mutex_); return generation_; }
    Snapshot Read() const { std::lock_guard<std::mutex> lock(mutex_); return value_; }
    void Invalidate() { std::lock_guard<std::mutex> lock(mutex_); ClearLocked(); }
    void ClearDrawn() { std::lock_guard<std::mutex> lock(mutex_); value_.drawnWidth=value_.drawnHeight=0; }
    void RecordMode(const DisplayOutputModeState& mode)
    { std::lock_guard<std::mutex> lock(mutex_); RecordLocked(mode); }
    bool PublishObservation(uint64_t generation, bool queried, const DisplayOutputModeState& mode)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_) return false;
        if (!queried) { ClearLocked(); return false; }
        RecordLocked(mode);
        return value_.hasMode;
    }
    void RecordDrawn(int width, int height)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        value_.drawnWidth=width>0 && height>0 ? static_cast<uint32_t>(width) : 0;
        value_.drawnHeight=width>0 && height>0 ? static_cast<uint32_t>(height) : 0;
    }
private:
    void ClearLocked() { ++generation_; value_={}; }
    void RecordLocked(const DisplayOutputModeState& mode)
    {
        if (!DisplayOutputModeMatches(mode,mode)) { ClearLocked(); return; }
        if (value_.hasMode && DisplayOutputModeChangesRaster(value_.mode,mode))
            value_.drawnWidth=value_.drawnHeight=0;
        ++generation_; value_.mode=mode; value_.hasMode=true;
    }
    mutable std::mutex mutex_;
    uint64_t generation_=0;
    Snapshot value_{};
};
inline std::string Dimensions(uint32_t width,uint32_t height)
{ return std::to_string(width)+"x"+std::to_string(height); }
inline std::string FormatDetails(const Snapshot& value)
{
    if (!value.hasMode || !DisplayOutputModeMatches(value.mode,value.mode)) return {};
    const auto& mode=value.mode;
    const bool interlaced=mode.scan==DisplayOutputScan::InterlacedUpper || mode.scan==DisplayOutputScan::InterlacedLower;
    std::string text=" | WIRE: "+Dimensions(mode.targetWidth,mode.targetHeight)+(interlaced ? "i" : "");
    if(mode.sourceWidth!=mode.targetWidth || mode.sourceHeight!=mode.targetHeight)
        text+=" | DESKTOP: "+Dimensions(mode.sourceWidth,mode.sourceHeight);
    if(value.drawnWidth && value.drawnHeight &&
        (value.drawnWidth!=mode.sourceWidth || value.drawnHeight!=mode.sourceHeight))
        text+=" | DRAWN: "+Dimensions(value.drawnWidth,value.drawnHeight);
    return text;
}
inline bool RasterTextValid(const std::wstring& text,bool allowInterlaced)
{
    size_t at=0;
    const auto number=[&]() {
        uint64_t value=0; const size_t begin=at;
        while(at<text.size() && text[at]>=L'0' && text[at]<=L'9') {
            value=value*10+(text[at++]-L'0');
            if(value>(std::numeric_limits<uint32_t>::max)()) return false;
        }
        return at>begin && value>0;
    };
    if(!number() || at>=text.size() || text[at++]!=L'x' || !number()) return false;
    if(allowInterlaced && at<text.size() && text[at]==L'i') ++at;
    return at==text.size();
}
inline std::wstring Detail(const std::wstring& details,const wchar_t* marker,bool allowInterlaced=false)
{
    const std::wstring tag(marker);
    const auto begin=details.find(tag);
    if(begin==std::wstring::npos) return {};
    const auto valueBegin=begin+tag.size(), end=details.find(L" | ",valueBegin);
    const auto value=details.substr(valueBegin,end==std::wstring::npos ? end : end-valueBegin);
    return RasterTextValid(value,allowInterlaced) ? value : std::wstring();
}
// Preserve the compact native panel width. Extra stages occupy continuation
// rows instead of silently clipping the diagnostic that explains the mismatch.
inline std::vector<std::wstring> ResolutionLines(const std::wstring& input,const std::wstring& details)
{
    std::vector<std::wstring> lines{input.empty() ? L"---" : input};
    const auto wire=Detail(details,L" | WIRE: ",true);
    if(wire.empty()) return lines;
    lines[0]+=L" -> "+wire;
    const auto drawn=Detail(details,L" | DRAWN: "), desktop=Detail(details,L" | DESKTOP: ");
    if(!drawn.empty()) lines.push_back(L"  [drawn "+drawn+L"]");
    if(!desktop.empty()) lines.push_back(L"  [GPU-scaled from "+desktop+L"]");
    return lines;
}

inline std::wstring FormatResolution(const std::wstring& input,const std::wstring& details)
{
    const auto lines=ResolutionLines(input,details);
    std::wstring text;
    for(const auto& line:lines) text+=line;
    return text;
}

}
