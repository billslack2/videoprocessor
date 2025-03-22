/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <cmath>
#include <cstring>
#include <vector>
#include "CV210toP010VideoFrameFormatter.h"

#define CHROMA_NEUTRAL (512 << 6)
#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))

 // Enum to define the two block output patterns.
enum BlockPattern {
    PATTERN_UV_Y_UV, // Write: if even -> UV, then Y, then if even -> UV.
    PATTERN_Y_UV_Y   // Write: Y, then if even -> UV, then Y.
};

// Process a block's three 10-bit values according to the given pattern.
static inline void processBlock(uint16_t a, uint16_t b, uint16_t c,
    uint16_t*& yPtr, uint16_t*& uvPtr,
    bool isEven, BlockPattern pattern)
{
    if (pattern == PATTERN_UV_Y_UV) {
        if (isEven) { *uvPtr++ = a << 6; }
        *yPtr++ = b << 6;
        if (isEven) { *uvPtr++ = c << 6; }
    }
    else { // PATTERN_Y_UV_Y
        *yPtr++ = a << 6;
        if (isEven) { *uvPtr++ = b << 6; }
        *yPtr++ = c << 6;
    }
}

// Decode a block: read a 32-bit word, extract three 10-bit values, and process them.
static inline void decodeBlock(const uint32_t*& src,
    uint16_t*& yPtr, uint16_t*& uvPtr,
    bool isEven, BlockPattern pattern)
{
    uint32_t val = *src++;
    uint16_t a = val & 0x3FF;
    uint16_t b = (val >> 10) & 0x3FF;
    uint16_t c = (val >> 20) & 0x3FF;
    processBlock(a, b, c, yPtr, uvPtr, isEven, pattern);
}

// Decode one pack of pixels by processing four blocks with the appropriate patterns.
static inline void decodePack(const uint32_t*& src,
    uint16_t*& yPtr, uint16_t*& uvPtr,
    bool isEven)
{
    // Block 1: corresponds to V210_READ_PACK_BLOCK(u, y1, v)
    decodeBlock(src, yPtr, uvPtr, isEven, PATTERN_UV_Y_UV);
    // Block 2: corresponds to V210_READ_PACK_BLOCK(y1, u, y2)
    decodeBlock(src, yPtr, uvPtr, isEven, PATTERN_Y_UV_Y);
    // Block 3: corresponds to V210_READ_PACK_BLOCK(v, y1, u)
    decodeBlock(src, yPtr, uvPtr, isEven, PATTERN_UV_Y_UV);
    // Block 4: corresponds to V210_READ_PACK_BLOCK(y1, v, y2)
    decodeBlock(src, yPtr, uvPtr, isEven, PATTERN_Y_UV_Y);
}

// Decode a full line (multiple packs) using decodePack.
static inline void decodeFullPacks(const uint32_t*& src, uint16_t* bufferY, uint16_t* bufferUV, uint32_t packs, bool isEven)
{
    uint16_t* yPtr = bufferY;
    uint16_t* uvPtr = bufferUV;
    for (uint32_t i = 0; i < packs; ++i)
    {
        decodePack(src, yPtr, uvPtr, isEven);
    }
}

// Set border pixels (first and last 2 pixels) to black (luma) and copy the central pixels.
static inline void setBordersAndCopy(uint16_t* dst, const uint16_t* src, int totalPixels)
{
    dst[0] = 0;
    dst[1] = 0;
    memcpy(dst + 2, src + 2, (totalPixels - 4) * sizeof(uint16_t));
    dst[totalPixels - 2] = 0;
    dst[totalPixels - 1] = 0;
}

// Set border pixels for chroma (first and last 2 pixels) to neutral and copy the central pixels.
static inline void setUVBordersAndCopy(uint16_t* dst, const uint16_t* src, int totalPixels)
{
    dst[0] = CHROMA_NEUTRAL;
    dst[1] = CHROMA_NEUTRAL;
    memcpy(dst + 2, src + 2, (totalPixels - 4) * sizeof(uint16_t));
    dst[totalPixels - 2] = CHROMA_NEUTRAL;
    dst[totalPixels - 1] = CHROMA_NEUTRAL;
}

void CV210toP010VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
    if (!videoState)
        throw std::runtime_error("Null video state is not allowed");

    if (videoState->videoFrameEncoding != VideoFrameEncoding::V210)
        throw std::runtime_error("Can only handle V210 input");

    m_height = videoState->displayMode->FrameHeight();
    if (m_height % 2 != 0)
        throw std::runtime_error("P010 output needs an even amount of input lines");

    m_width = videoState->displayMode->FrameWidth();
    // Either width is a multiple of 6 or it is the special 1280x720 case.
    if ((m_width % PIXELS_PER_PACK != 0) && !(m_width == 1280 && m_height == 720))
        throw std::runtime_error("Can only handle conversions which align with V210 boundary (6 pixels)");

    // Precompute and cache aligned width and stride for the frame.
    m_alignedWidth = ((m_width + 47) / 48) * 48;
    m_stride = m_alignedWidth * 8 / 3;
    const uint32_t expectedBytes = m_height * m_stride;

    if (videoState->BytesPerFrame() != expectedBytes)
        throw std::runtime_error("Unexpected amount of bytes for frame");
}

bool CV210toP010VideoFrameFormatter::FormatVideoFrame(
    const VideoFrame& inFrame,
    BYTE* outBuffer)
{
    const uint32_t pixels = m_height * m_width;
    uint16_t* dstY = reinterpret_cast<uint16_t*>(outBuffer);
    uint16_t* dstUV = reinterpret_cast<uint16_t*>(outBuffer + (static_cast<ptrdiff_t>(pixels) * sizeof(uint16_t)));

    const BYTE* inData = reinterpret_cast<const BYTE*>(inFrame.GetData());

    // Special handling for 1280x720, which is not a multiple of 6.
    bool special720 = (m_width == 1280 && m_height == 720);

    for (uint32_t line = 0; line < m_height; ++line)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(inData + (line * m_stride));
        bool isEvenLine = (line % 2 == 0);

        if (special720)
        {
            // For 1280x720, m_width % 6 equals 2.
            // To decode a complete pack, decode an extra 4 pixels:
            // fullDecodedWidth = 1280 + 4 = 1284.
            const uint32_t extraPixels = m_width % PIXELS_PER_PACK;           // 2
            const uint32_t extraNeeded = (extraPixels == 0) ? 0 : (PIXELS_PER_PACK - extraPixels); // 4
            const uint32_t fullDecodedWidth = m_width + extraNeeded;            // 1284

            std::vector<uint16_t> tempY(fullDecodedWidth);
            std::vector<uint16_t> tempUV(fullDecodedWidth);
            uint32_t fullPacks = fullDecodedWidth / PIXELS_PER_PACK;
            decodeFullPacks(src, tempY.data(), tempUV.data(), fullPacks, isEvenLine);

            uint16_t* lineDstY = dstY + line * m_width;
            uint16_t* lineDstUV = (isEvenLine) ? (dstUV + (line / 2) * m_width) : nullptr;

            // Set border pixels and copy the central pixels (ensuring left/right pixels are forced black/neutral).
            setBordersAndCopy(lineDstY, tempY.data(), m_width);
            if (isEvenLine && lineDstUV)
            {
                setUVBordersAndCopy(lineDstUV, tempUV.data(), m_width);
            }
        }
        else
        {
            std::vector<uint16_t> tempY(m_width);
            std::vector<uint16_t> tempUV(m_width);
            const uint32_t fullPacks = m_width / PIXELS_PER_PACK;
            decodeFullPacks(src, tempY.data(), tempUV.data(), fullPacks, isEvenLine);

            uint16_t* lineDstY = dstY + line * m_width;
            uint16_t* lineDstUV = (isEvenLine) ? (dstUV + (line / 2) * m_width) : nullptr;
            memcpy(lineDstY, tempY.data(), m_width * sizeof(uint16_t));
            if (isEvenLine && lineDstUV)
            {
                memcpy(lineDstUV, tempUV.data(), m_width * sizeof(uint16_t));
            }
        }
    }

    return true;
}

LONG CV210toP010VideoFrameFormatter::GetOutFrameSize() const
{
    const LONG pixels = m_height * m_width;
    return (pixels * sizeof(uint16_t)) +
        ((pixels / 4) * (2 * sizeof(uint16_t)));
}
