#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <memory>
#include <AnalysisLumaSource.h>

// Diagnostic only. Half-open source-raster bounds; no source-pixel mutation,
// OCR, glyph extraction, crop authority or subtitle presentation decisions.
struct SubtitleBoxRect
{
    int left = 0, top = 0, right = 0, bottom = 0;
    bool Valid() const { return right > left && bottom > top; }
};
// Ink coverage in a normalized 64x16 grid. Density preserves stroke changes
// in long lines where a one-bit cell would be set by several adjacent glyphs.
using SubtitleLineSignature = std::array<uint8_t, 64 * 16>;
// Immutable bounded analysis evidence for fixed-coordinate cue revalidation.
// Rows retain their actual source positions, including explicitly sampled bar
// boundary rows. Raw ink is captured before component grouping can fail.
struct SubtitleInkSnapshot {
    int width=0,height=0,step=0;
    std::vector<int> sourceRows;
    std::vector<uint64_t> rawInk,ownedInk;
    bool Get(int x,int y,bool owned=false) const {
        if(x<0 || y<0 || x>=width || y>=height) return false;
        const size_t pixel=static_cast<size_t>(y)*width+x;
        const auto& bits=owned?ownedInk:rawInk;
        return pixel/64<bits.size() && (bits[pixel/64]&(uint64_t{1}<<(pixel%64)))!=0;
    }
};
struct SubtitleBoxResult
{
    SubtitleBoxRect bounds;
    // Fresh observation evidence for queued-frame cue matching. The anchor is
    // the actual bar-intersecting line, excluding companion lines and padding.
    SubtitleBoxRect anchor;
    std::array<uint64_t, 16> signature{}, anchorSignature{};
    std::array<SubtitleBoxRect, 3> lineBounds{};
    std::array<SubtitleLineSignature, 3> lineSignatures{};
    uint64_t cue = 0;
    uint32_t observations = 0;
    int lineCount = 0;
    bool detected = false;
    bool held = false;
    bool revised = false;
    bool workLimit = false;
};
class SubtitleBoxDetector
{
public:
    SubtitleBoxResult Analyze(const AnalysisLumaSource& source, int pictureTop,
        int pictureBottom, uint64_t sequence, uint64_t viewportGeneration);
    void Reset();
    std::shared_ptr<const SubtitleInkSnapshot> InkSnapshot() const {return m_inkSnapshot;}
    // UHD is sampled at 960x540: one quarter per axis, one sixteenth of pixels.
    static int SamplingStep(int width, int height);
private:
    struct Line {
        SubtitleBoxRect box;
        int ink = 0, components = 0, topBarInk = 0, bottomBarInk = 0;
        uint16_t label = 0;
        int peakLuma = 0, peakPixel = 0;
        bool hasTextCore = true;
    };
    bool Detect(const AnalysisLumaSource& source, int pictureTop, int pictureBottom,
        SubtitleBoxRect& box, int& lines, std::array<uint64_t, 16>& signature);
    std::vector<uint16_t> m_luma;
    std::vector<uint32_t> m_chroma;
    std::vector<uint8_t> m_mask;
    std::vector<uint16_t> m_labels;
    std::vector<int> m_flood;
    std::vector<Line> m_components, m_lines;
    std::shared_ptr<SubtitleInkSnapshot> m_inkSnapshot;
    SubtitleBoxResult m_result;
    SubtitleBoxRect m_currentBarAnchor;
    std::array<uint64_t, 16> m_signature{};
    std::array<uint64_t, 16> m_currentAnchorSignature{};
    std::array<SubtitleBoxRect, 3> m_currentLineBounds{};
    std::array<SubtitleLineSignature, 3> m_currentLineSignatures{};
    uint64_t m_generation = 0, m_viewport = 0, m_sequence = 0, m_nextCue = 0;
    int m_width = 0, m_height = 0, m_top = 0, m_bottom = 0, m_misses = 0;
    bool m_workLimit = false, m_hasSequence = false;
};
