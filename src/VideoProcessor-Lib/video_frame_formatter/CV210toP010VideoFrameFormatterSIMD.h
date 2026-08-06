/*
 * High-performance SIMD-optimized V210?P010 converter with black bar detection support
 */

#pragma once

#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <immintrin.h>  // AVX2 intrinsics
#include <vector>

// Black bar detection structure
struct BlackBarInfo
{
    bool hasLetterbox = false;      // Top/bottom black bars
    bool hasPillarbox = false;      // Left/right black bars
    uint32_t letterboxTop = 0;      // First non-black row
    uint32_t letterboxBottom = 0;   // Last non-black row
    uint32_t pillarboxLeft = 0;     // First non-black column
    uint32_t pillarboxRight = 0;    // Last non-black column
};

/**
 * High-performance SIMD-optimized V210?P010 converter with integrated black bar detection
 */
class CV210toP010VideoFrameFormatterSIMD : public IVideoFrameFormatter
{
public:
    virtual ~CV210toP010VideoFrameFormatterSIMD() {}

    // IVideoFrameFormatter
    void OnVideoState(VideoStateComPtr& videoState) override;
    bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
    LONG GetOutFrameSize() const override;

    // Black bar detection
    const BlackBarInfo& GetLastBlackBarInfo() const { return m_lastBlackBarInfo; }
    void EnableBlackBarDetection(bool enable) { m_detectBlackBars = enable; }

private:
    uint32_t m_height = 0;
    uint32_t m_width = 0;
    // Pre-allocated buffers
    std::vector<uint16_t> m_tempY;
    std::vector<uint16_t> m_tempUV;
    
    // Black bar detection
    bool m_detectBlackBars = false;
    BlackBarInfo m_lastBlackBarInfo;
    std::vector<uint32_t> m_rowLuminanceSum;    // Sum of Y values per row for letterbox detection
    std::vector<uint32_t> m_colLuminanceSum;    // Sum of Y values per column for pillarbox detection
    
    // SIMD processing methods
    void ProcessLine_AVX2(const uint32_t* src, uint16_t* dstY, uint16_t* dstUV, 
                         uint32_t width, bool isEvenLine, uint32_t lineIndex);
    void ProcessLine_Generic(const uint32_t* src, uint16_t* dstY, uint16_t* dstUV, 
                            uint32_t width, bool isEvenLine, uint32_t lineIndex);
    
    // Black bar analysis
    void AnalyzeBlackBars();
    bool IsBlackThreshold(uint32_t luminanceSum, uint32_t pixelCount);
    
    // CPU feature detection
    static bool HasAVX2();
    bool m_useAVX2 = false;
};
