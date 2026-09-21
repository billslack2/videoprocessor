#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <AnalysisLumaSource.h>

// Diagnostic only. Half-open source-raster bounds; no source-pixel mutation,
// OCR, glyph extraction, crop authority or subtitle presentation decisions.
struct SubtitleBoxRect
{
    int left = 0, top = 0, right = 0, bottom = 0;
    bool Valid() const { return right > left && bottom > top; }
};
struct SubtitleBoxResult
{
    SubtitleBoxRect bounds;
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
private:
    struct Line { SubtitleBoxRect box; int ink = 0, components = 0; };
    bool Detect(const AnalysisLumaSource& source, int pictureTop, int pictureBottom,
        SubtitleBoxRect& box, int& lines, std::array<uint64_t, 16>& signature);
    std::vector<uint16_t> m_luma;
    std::vector<uint32_t> m_chroma;
    std::vector<uint8_t> m_mask;
    std::vector<int> m_flood;
    std::vector<Line> m_components, m_lines;
    SubtitleBoxResult m_result;
    std::array<uint64_t, 16> m_signature{};
    uint64_t m_generation = 0, m_viewport = 0, m_sequence = 0, m_nextCue = 0;
    int m_width = 0, m_height = 0, m_top = 0, m_bottom = 0, m_misses = 0;
    bool m_workLimit = false, m_hasSequence = false;
};
