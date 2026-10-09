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
struct SubtitlePanelTopEdge
{
    int y = 0, left = 0, right = 0;
    unsigned supportBasisPoints = 0;
    bool Valid() const { return right > left && supportBasisPoints >= 4500; }
};
// Ink coverage in a normalized 64x16 grid. Density preserves stroke changes
// in long lines where a one-bit cell would be set by several adjacent glyphs.
using SubtitleLineSignature = std::array<uint8_t, 64 * 16>;
// Immutable bounded analysis evidence for fixed-coordinate cue revalidation.
// Rows retain their actual source positions, including explicitly sampled bar
// boundary rows. Raw ink is captured before component grouping can fail.
struct SubtitleInkSnapshot {
    bool rawEvidenceComplete=false;
    int width=0,height=0,step=0;
    std::vector<int> sourceRows;
    std::vector<uint64_t> rawInk,ownedInk;
    // Independent sampled near-black evidence, available even if grouping fails.
    std::vector<uint64_t> blackBacking;
    bool Black(int x,int y) const {
        if(x<0 || y<0 || x>=width || y>=height)return false;
        const size_t pixel=static_cast<size_t>(y)*width+x;
        return pixel/64<blackBacking.size() && (blackBacking[pixel/64]&(uint64_t{1}<<(pixel%64)))!=0;
    }
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
    // Measured original opaque card footprint inside the active picture.
    // Independent of destination styling and display padding.
    SubtitleBoxRect sourcePanel;
    std::array<SubtitleBoxRect, 3> linePanels{};
    // True measured opaque interior, excluding the outside cleanup fringe.
    // Live detection always marks this measured, including an empty interior
    // for cues wholly in the letterbox. Legacy geometry fixtures may omit it.
    SubtitleBoxRect capturePanel;
    std::array<SubtitleBoxRect, 3> lineCapturePanels{};
    bool capturePanelMeasured = false;
    // Fresh observation evidence for queued-frame cue matching. The anchor is
    // the actual bar-intersecting line, excluding companion lines and padding.
    SubtitleBoxRect anchor;
    std::array<uint64_t, 16> signature{}, anchorSignature{};
    std::array<SubtitleBoxRect, 3> lineBounds{};
    std::array<SubtitleLineSignature, 3> lineSignatures{};
    // Optional broad backing evidence, always subordinate to current glyphs.
    std::array<SubtitlePanelTopEdge, 3> panelTopEdges{};
    uint64_t cue = 0;
    uint32_t observations = 0;
    int lineCount = 0;
    bool detected = false;
    bool held = false;
    bool revised = false;
    bool workLimit = false;
    // Detector-owned admission; measured means callers must not substitute
    // cleanup/display padding for the original backing's proximity evidence.
    bool nearBarEligibilityMeasured = false, nearBarEligible = false;
    const char* diagnosticReason = "not-analyzed";
    unsigned diagnosticComponentCount = 0;
    int diagnosticNearBarGap = -1;
    bool diagnosticNearBarGapInferred = false;
};
class SubtitleBoxDetector
{
public:
    SubtitleBoxResult Analyze(const AnalysisLumaSource& source, int pictureTop,
        int pictureBottom, uint64_t sequence, uint64_t viewportGeneration);
    void Reset();
    void SetNearBarDistance(int pixels) { if(m_nearBarDistance!=pixels) {Reset();m_nearBarDistance=pixels;} }
    void SetOptimizationMode(int mode) { if(m_optimizationMode!=mode){Reset();m_optimizationMode=mode;} }
    int OptimizationMode() const {return m_optimizationMode;}
    size_t SharedSampleCount() const {return m_sharedSampleCount;}
    int NearBarDistance() const {return m_nearBarDistance;}
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
        bool actualBarInk = false;
    };
    bool Detect(const AnalysisLumaSource& source, int pictureTop, int pictureBottom,
        SubtitleBoxRect& box, int& lines, std::array<uint64_t, 16>& signature, int nearBarDistance);
    // Immutable raw grid for this Analyze call only. Detector scratch remains separate.
    int m_optimizationMode=0;
    size_t m_sharedSampleCount=0;
    std::vector<uint8_t> m_sampledRows;
    std::vector<uint16_t> m_rawLuma;
    std::vector<uint32_t> m_rawChroma;
    std::vector<uint16_t> m_luma;
    std::vector<uint32_t> m_chroma;
    std::vector<uint8_t> m_mask;
    std::vector<uint16_t> m_labels;
    std::vector<int> m_flood;
    // Reused bounded native card refinement scratch; never a full-frame raster.
    std::vector<AnalysisLumaSample> m_nativeCardSamples;
    std::vector<uint8_t> m_nativeCardState;
    std::vector<Line> m_components, m_lines;
    std::shared_ptr<SubtitleInkSnapshot> m_inkSnapshot;
    SubtitleBoxResult m_result;
    SubtitleBoxRect m_currentBarAnchor, m_currentSourcePanel;
    std::array<SubtitleBoxRect, 3> m_currentLinePanels{};
    SubtitleBoxRect m_currentCapturePanel;
    std::array<SubtitleBoxRect, 3> m_currentLineCapturePanels{};
    std::array<uint64_t, 16> m_signature{};
    std::array<uint64_t, 16> m_currentAnchorSignature{};
    std::array<SubtitleBoxRect, 3> m_currentLineBounds{};
    std::array<SubtitleLineSignature, 3> m_currentLineSignatures{};
    std::array<SubtitlePanelTopEdge, 3> m_currentPanelTopEdges{};
    uint64_t m_generation = 0, m_viewport = 0, m_sequence = 0, m_nextCue = 0;
    int m_width = 0, m_height = 0, m_top = 0, m_bottom = 0, m_misses = 0;
    int m_nearBarDistance=0;
    bool m_workLimit = false, m_hasSequence = false;
    bool m_nearBarEligibilityMeasured=false,m_nearBarEligible=false;
    const char* m_diagnosticReason="not-analyzed";
    unsigned m_diagnosticComponentCount=0;
    int m_diagnosticNearBarGap=-1;
    bool m_diagnosticNearBarGapInferred=false;
};
