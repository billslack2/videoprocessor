#pragma once
#include <atomic>

enum class SubtitlePreviewMode
{
    None = 0,
    GreenBox = 1,
    BlackBackground = 2,
    GeneratedGrayBackground = 3
};

inline bool IsSubtitlePreviewShortcut(unsigned key, bool control, bool shift, bool alt,
    unsigned configuredKey='T', bool configuredControl=true, bool configuredShift=true, bool configuredAlt=false)
{
    return configuredKey!=0 && key==configuredKey && control==configuredControl &&
        shift==configuredShift && alt==configuredAlt;
}

inline bool SubtitlePreviewOverridesClassicHandling(SubtitlePreviewMode mode)
{
    return mode==SubtitlePreviewMode::BlackBackground ||
        mode==SubtitlePreviewMode::GeneratedGrayBackground;
}

inline bool SubtitlePreviewRequiresFullRaster(SubtitlePreviewMode mode)
{
    return mode==SubtitlePreviewMode::GreenBox;
}

inline SubtitlePreviewMode NextSubtitlePreviewMode(SubtitlePreviewMode mode)
{
    switch (mode) {
    case SubtitlePreviewMode::GeneratedGrayBackground: return SubtitlePreviewMode::BlackBackground;
    case SubtitlePreviewMode::BlackBackground: return SubtitlePreviewMode::None;
    default: return SubtitlePreviewMode::GeneratedGrayBackground;
    }
}

// Temporary playback choice: source resync and fresh construction always use
// normal configured picture placement, independent of old experiment flags.
class SubtitlePreviewState
{
public:
    SubtitlePreviewMode Current() const { return m_mode.load(std::memory_order_acquire); }
    void ResetForSourceChange() { m_mode.store(SubtitlePreviewMode::None, std::memory_order_release); }
    SubtitlePreviewMode Cycle() {
        auto current=Current();
        for (;;) {
            const auto next=NextSubtitlePreviewMode(current);
            if(m_mode.compare_exchange_weak(current,next,std::memory_order_acq_rel)) return next;
        }
    }
private:
    std::atomic<SubtitlePreviewMode> m_mode{SubtitlePreviewMode::None};
};

inline const char* SubtitlePreviewModeName(SubtitlePreviewMode mode)
{
    switch (mode) {
    case SubtitlePreviewMode::None: return "normal configured subtitles";
    case SubtitlePreviewMode::GreenBox: return "green box";
    case SubtitlePreviewMode::BlackBackground: return "black background";
    case SubtitlePreviewMode::GeneratedGrayBackground: return "generated gray background";
    default: return "unknown";
    }
}
