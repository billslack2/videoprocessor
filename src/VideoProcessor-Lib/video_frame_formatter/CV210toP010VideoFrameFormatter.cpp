/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "CV210toP010VideoFrameFormatter.h"
#include <vector>
#include <immintrin.h>

// ---------------------------------------------------------------------
// Macros for V210 unpacking
#define V210_READ_PACK_BLOCK(a, b, c) \
    do {                              \
        val  = *src++;                \
        a = val & 0x3FF;              \
        b = (val >> 10) & 0x3FF;      \
        c = (val >> 20) & 0x3FF;      \
    } while (0)

#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))

// 720p special-case handling
#define P010_WRITE_VALUE_720(dst, idx, val) ((dst)[(idx)] = (val) << 6)
#define CHROMA_NEUTRAL (512 << 6)

#define FAST_BORDER_COPY_Y(dst, src, width) \
    do { \
        (dst)[0] = 0; (dst)[1] = 0; \
        memcpy((dst) + 2, (src) + 2, ((width) - 4) * sizeof(uint16_t)); \
        (dst)[(width) - 2] = 0; (dst)[(width) - 1] = 0; \
    } while (0)

#define FAST_BORDER_COPY_UV(dst, src, width) \
    do { \
        (dst)[0] = CHROMA_NEUTRAL; (dst)[1] = CHROMA_NEUTRAL; \
        memcpy((dst) + 2, (src) + 2, ((width) - 4) * sizeof(uint16_t)); \
        (dst)[(width) - 2] = CHROMA_NEUTRAL; (dst)[(width) - 1] = CHROMA_NEUTRAL; \
    } while (0)

// CPU feature detection
bool CV210toP010VideoFrameFormatter::CheckCPUFeatures() const
{
    if (!m_cpuFeaturesChecked)
    {
        m_hasAVX2 = false;
        m_hasAVX2MemoryOps = false;
        m_cpuFeaturesChecked = true;
    }
    return false;
}

bool CV210toP010VideoFrameFormatter::HasAVX2MemoryOps() const
{
    CheckCPUFeatures();
    return m_hasAVX2MemoryOps;
}

void CV210toP010VideoFrameFormatter::LogPerformanceStats() const
{
#ifdef _DEBUG
    if (m_totalConversions > 0)
    {
        const double avgTimeUs = static_cast<double>(m_totalConversionTimeUs) / m_totalConversions;
        DbgLog((LOG_TRACE, 1, TEXT("V210->P010 Performance: %llu frames, Avg %.1f us"),
                m_totalConversions, avgTimeUs));
    }
#endif
}

// ---------------------------------------------------------------------
void CV210toP010VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
    if (!videoState)
        throw std::runtime_error("Null video state is not allowed");

    if (videoState->videoFrameEncoding != VideoFrameEncoding::V210)
        throw std::runtime_error("Can only handle V210 input");

    m_height = videoState->displayMode->FrameHeight();
    if (m_height % 2 != 0)
        throw std::runtime_error("P010 output needs an even amount of input lines");

    uint32_t origWidth = videoState->displayMode->FrameWidth();
    bool special720 = (origWidth == 1280 && m_height == 720);
    m_special720 = special720;

    if (!special720 && (origWidth % PIXELS_PER_PACK != 0))
        throw std::runtime_error("Can only handle conversions which align with V210 boundary (6 pixels)");

    const uint32_t bytes = videoState->BytesPerFrame();
    uint32_t expectedBytes;
    if (special720)
    {
        const uint32_t aligned_width = ((1280 + 47) / 48) * 48;
        const uint32_t src_stride = aligned_width * 8 / 3;
        expectedBytes = m_height * src_stride;

        const uint32_t extraPixels = origWidth % PIXELS_PER_PACK;
        const uint32_t extraNeeded = (extraPixels == 0) ? 0 : (PIXELS_PER_PACK - extraPixels);
        const uint32_t fullDecodedWidth = origWidth + extraNeeded;

        m_tempY.resize(fullDecodedWidth);
        m_tempUV.resize(fullDecodedWidth);
    }
    else
    {
        expectedBytes = m_height * ((origWidth / PIXELS_PER_PACK) * BYTES_PER_PACK);
        m_tempY.clear();
        m_tempUV.clear();
        m_tempY.shrink_to_fit();
        m_tempUV.shrink_to_fit();
    }

    if (bytes != expectedBytes)
        throw std::runtime_error("Unexpected amount of bytes for frame");

    m_width = origWidth;
}

// ---------------------------------------------------------------------
// FormatVideoFrame: For 720p, decode an extra 4 pixels per line (to form a full pack)
// and then copy the central m_width pixels into the destination with border fixes.
// Non-720p processing remains unchanged.
bool CV210toP010VideoFrameFormatter::FormatVideoFrame(
    const VideoFrame& inFrame,
    BYTE* outBuffer)
{
    const auto startTime = GetWallClockTime();

    const uint32_t pixels = m_height * m_width;
    const uint32_t yPlaneSize = pixels * sizeof(uint16_t);
    
    uint16_t* dstY = reinterpret_cast<uint16_t*>(outBuffer);
    uint16_t* dstUV = reinterpret_cast<uint16_t*>(outBuffer + yPlaneSize);

    const uint32_t alignedWidth = ((m_width + 47) / 48) * 48;
    const uint32_t srcStride = alignedWidth * 8 / 3;

    const bool conversionSuccess = ConvertV210ToP010(
        static_cast<const uint8_t*>(inFrame.GetData()),
        srcStride,
        dstY,
        dstUV,
        m_width,
        m_height
    );

    const auto endTime = GetWallClockTime();
    const uint64_t conversionTime = (endTime - startTime) / 10;
    LogConversionPerformance(conversionTime, conversionSuccess);

    return conversionSuccess;
}

LONG CV210toP010VideoFrameFormatter::GetOutFrameSize() const
{
    const LONG pixels = m_height * m_width;
    return (pixels * sizeof(uint16_t)) +
        (pixels / 2 / 2 * (2 * sizeof(uint16_t)));
}

// ---------------------------------------------------------------------
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010(
    const uint8_t* srcData,
    uint32_t srcStride, 
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    if (m_special720)
    {
        return ConvertV210ToP010_720p(srcData, srcStride, dstY, dstUV, width, height);
    }
    else
    {
        return ConvertV210ToP010_Standard(srcData, srcStride, dstY, dstUV, width, height);
    }
}

// ---------------------------------------------------------------------
// 720p conversion with border handling
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_720p(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY, 
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    const uint32_t extraPixels = width % PIXELS_PER_PACK;
    const uint32_t extraNeeded = (extraPixels == 0) ? 0 : (PIXELS_PER_PACK - extraPixels);
    const uint32_t fullDecodedWidth = width + extraNeeded;
    const uint32_t fullPacks = fullDecodedWidth / PIXELS_PER_PACK;
    
    uint16_t* const tempYData = m_tempY.data();
    uint16_t* const tempUVData = m_tempUV.data();
    
    for (uint32_t line = 0; line < height; line++)
    {
        const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(
            srcData + static_cast<ptrdiff_t>(line * srcStride));
        const bool isEvenLine = (line & 1) == 0;

        // Fill UV buffer for even lines
        if (isEvenLine)
        {
            for (size_t i = 0; i < m_tempUV.size(); ++i) {
                tempUVData[i] = CHROMA_NEUTRAL;
            }
        }

        const uint32_t* src = srcLine;
        for (uint32_t pack = 0; pack < fullPacks; ++pack) 
        {
            const uint32_t off = pack * PIXELS_PER_PACK;
            uint32_t val;
            uint16_t u, y1, y2, v;

            if (isEvenLine)
            {
                V210_READ_PACK_BLOCK(u, y1, v);
                P010_WRITE_VALUE_720(tempUVData, off + 0, u);
                P010_WRITE_VALUE_720(tempYData, off + 0, y1);
                P010_WRITE_VALUE_720(tempUVData, off + 1, v);

                V210_READ_PACK_BLOCK(y1, u, y2);
                P010_WRITE_VALUE_720(tempYData, off + 1, y1);
                P010_WRITE_VALUE_720(tempUVData, off + 2, u);
                P010_WRITE_VALUE_720(tempYData, off + 2, y2);

                V210_READ_PACK_BLOCK(v, y1, u);
                P010_WRITE_VALUE_720(tempUVData, off + 3, v);
                P010_WRITE_VALUE_720(tempYData, off + 3, y1);
                P010_WRITE_VALUE_720(tempUVData, off + 4, u);

                V210_READ_PACK_BLOCK(y1, v, y2);
                P010_WRITE_VALUE_720(tempYData, off + 4, y1);
                P010_WRITE_VALUE_720(tempUVData, off + 5, v);
                P010_WRITE_VALUE_720(tempYData, off + 5, y2);
            }
            else
            {
                V210_READ_PACK_BLOCK(u, y1, v);
                P010_WRITE_VALUE_720(tempYData, off + 0, y1);

                V210_READ_PACK_BLOCK(y1, u, y2);
                P010_WRITE_VALUE_720(tempYData, off + 1, y1);
                P010_WRITE_VALUE_720(tempYData, off + 2, y2);

                V210_READ_PACK_BLOCK(v, y1, u);
                P010_WRITE_VALUE_720(tempYData, off + 3, y1);

                V210_READ_PACK_BLOCK(y1, v, y2);
                P010_WRITE_VALUE_720(tempYData, off + 4, y1);
                P010_WRITE_VALUE_720(tempYData, off + 5, y2);
            }
        }
        
        uint16_t* lineDstY = dstY + line * width;
        uint16_t* lineDstUV = isEvenLine ? (dstUV + (line / 2) * width) : nullptr;

        FAST_BORDER_COPY_Y(lineDstY, tempYData, width);
        
        if (isEvenLine && lineDstUV)
        {
            FAST_BORDER_COPY_UV(lineDstUV, tempUVData, width);
        }
    }
    
    return true;
}

// ---------------------------------------------------------------------
// Standard resolution conversion - CLEAN SCALAR IMPLEMENTATION
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_Standard(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV, 
    uint32_t width,
    uint32_t height) noexcept
{
    const uint32_t packsPerLine = width / PIXELS_PER_PACK;
    
    for (uint32_t line = 0; line < height; line++)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(
            srcData + line * srcStride);
        const bool isEvenLine = (line % 2) == 0;
        
        uint16_t* lineY = dstY + line * width;
        uint16_t* lineUV = isEvenLine ? (dstUV + (line / 2) * width) : nullptr;
        
        uint16_t* currentDstY = lineY;
        uint16_t* currentDstUV = lineUV;
        
        for (uint32_t pack = 0; pack < packsPerLine; pack++)
        {
            uint32_t val;
            uint16_t u, y1, y2, v;
            
            if (isEvenLine)
            {
                V210_READ_PACK_BLOCK(u, y1, v);
                *currentDstUV++ = u << 6; *currentDstY++ = y1 << 6; *currentDstUV++ = v << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                *currentDstY++ = y1 << 6; *currentDstUV++ = u << 6; *currentDstY++ = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                *currentDstUV++ = v << 6; *currentDstY++ = y1 << 6; *currentDstUV++ = u << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                *currentDstY++ = y1 << 6; *currentDstUV++ = v << 6; *currentDstY++ = y2 << 6;
            }
            else
            {
                V210_READ_PACK_BLOCK(u, y1, v);
                *currentDstY++ = y1 << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                *currentDstY++ = y1 << 6; *currentDstY++ = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                *currentDstY++ = y1 << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                *currentDstY++ = y1 << 6; *currentDstY++ = y2 << 6;
            }
        }
    }
    
    return true;
}

// ---------------------------------------------------------------------
// SIMD implementation - DISABLED, just calls Standard for now
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_SIMD(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV, 
    uint32_t width,
    uint32_t height) noexcept
{
    // SIMD implementation disabled - fallback to scalar
    return ConvertV210ToP010_Standard(srcData, srcStride, dstY, dstUV, width, height);
}

// ---------------------------------------------------------------------
void CV210toP010VideoFrameFormatter::LogConversionPerformance(uint64_t conversionTimeUs, bool success) const
{
    m_performanceWindow.AddSample(static_cast<double>(conversionTimeUs));
    
#ifdef _DEBUG
    m_totalConversions++;
    m_totalConversionTimeUs += conversionTimeUs;
    m_scalarConversions++;
    m_scalarConversionTimeUs += conversionTimeUs;
    
    if (m_totalConversions % 100 == 0)
    {
        LogPerformanceStats();
    }
#endif
}
