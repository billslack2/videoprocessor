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


#define V210_READ_PACK_BLOCK(a, b, c)  \
    do {                             \
        val  = *src++;               \
        a = val & 0x3FF;             \
        b = (val >> 10) & 0x3FF;       \
        c = (val >> 20) & 0x3FF;       \
    } while (0)

#define P010_WRITE_VALUE(d, v) (*d++ = (v << 6))

#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))

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

    const uint32_t aligned_width = ((m_width + 47) / 48) * 48;
    const uint32_t stride = aligned_width * 8 / 3;
    const uint32_t expectedBytes = m_height * stride;

    if (videoState->BytesPerFrame() != expectedBytes)
        throw std::runtime_error("Unexpected amount of bytes for frame");
}

bool CV210toP010VideoFrameFormatter::FormatVideoFrame(
    const VideoFrame& inFrame,
    BYTE* outBuffer)
{
    const uint32_t pixels = m_height * m_width;
    const uint32_t aligned_width = ((m_width + 47) / 48) * 48;
    const uint32_t stride = aligned_width * 8 / 3;

    uint16_t* dstY = reinterpret_cast<uint16_t*>(outBuffer);
    uint16_t* dstUV = reinterpret_cast<uint16_t*>(outBuffer + (static_cast<ptrdiff_t>(pixels) * sizeof(uint16_t)));

    const BYTE* inData = reinterpret_cast<const BYTE*>(inFrame.GetData());

    // Special handling for 1280x720, which is not a multiple of 6.
    bool special720 = (m_width == 1280 && m_height == 720);

    for (uint32_t line = 0; line < m_height; ++line)
    {
        // Set up the source pointer for this line.
        const uint32_t* src = reinterpret_cast<const uint32_t*>(inData + (line * stride));
        uint32_t val;

        if (special720)
        {
            // For 1280x720, m_width % 6 equals 2.
            // To decode a complete pack, we decode an extra 4 pixels:
            // fullDecodedWidth = 1280 + 4 = 1284.
            uint32_t extraPixels = m_width % PIXELS_PER_PACK;          // 2
            uint32_t extraNeeded = (extraPixels == 0) ? 0 : (PIXELS_PER_PACK - extraPixels); // 4
            uint32_t fullDecodedWidth = m_width + extraNeeded;           // 1284

            std::vector<uint16_t> tempY(fullDecodedWidth);
            std::vector<uint16_t> tempUV(fullDecodedWidth);
            uint16_t* yPtr = tempY.data();
            uint16_t* uvPtr = tempUV.data();

            // Decode full packs for the extended line.
            uint32_t fullPacks = fullDecodedWidth / PIXELS_PER_PACK;
            for (uint32_t pack = 0; pack < fullPacks; ++pack)
            {
                uint16_t u, y1, y2, v;

                V210_READ_PACK_BLOCK(u, y1, v);
                if (line % 2 == 0) { *uvPtr++ = u << 6; }
                *yPtr++ = y1 << 6;
                if (line % 2 == 0) { *uvPtr++ = v << 6; }

                V210_READ_PACK_BLOCK(y1, u, y2);
                *yPtr++ = y1 << 6;
                if (line % 2 == 0) { *uvPtr++ = u << 6; }
                *yPtr++ = y2 << 6;

                V210_READ_PACK_BLOCK(v, y1, u);
                if (line % 2 == 0) { *uvPtr++ = v << 6; }
                *yPtr++ = y1 << 6;
                if (line % 2 == 0) { *uvPtr++ = u << 6; }

                V210_READ_PACK_BLOCK(y1, v, y2);
                *yPtr++ = y1 << 6;
                if (line % 2 == 0) { *uvPtr++ = v << 6; }
                *yPtr++ = y2 << 6;
            }

            uint16_t* lineDstY = dstY + line * m_width;
            uint16_t* lineDstUV = (line % 2 == 0) ? (dstUV + (line / 2) * m_width) : nullptr;

            // Set leftmost 2 pixels to black (luma) and neutral (chroma).
            lineDstY[0] = 0;
            lineDstY[1] = 0;
            if (line % 2 == 0 && lineDstUV)
            {
                lineDstUV[0] = CHROMA_NEUTRAL;
                lineDstUV[1] = CHROMA_NEUTRAL;
            }

            // Copy the central 1276 pixels from the decoded data.
            memcpy(lineDstY + 2, tempY.data() + 2, 1276 * sizeof(uint16_t));
            if (line % 2 == 0 && lineDstUV)
            {
                memcpy(lineDstUV + 2, tempUV.data() + 2, 1276 * sizeof(uint16_t));
            }

            // Set rightmost 2 pixels to black (luma) and neutral (chroma).
            lineDstY[1278] = 0;
            lineDstY[1279] = 0;
            if (line % 2 == 0 && lineDstUV)
            {
                lineDstUV[1278] = CHROMA_NEUTRAL;
                lineDstUV[1279] = CHROMA_NEUTRAL;
            }
        }
        else
        {
            // For resolutions that align on a 6-pixel boundary,
            // decode exactly m_width pixels.
            std::vector<uint16_t> tempY(m_width);
            std::vector<uint16_t> tempUV(m_width);
            uint16_t* yPtr = tempY.data();
            uint16_t* uvPtr = tempUV.data();

            const uint32_t fullPacks = m_width / PIXELS_PER_PACK;
            const uint32_t extraPixels = m_width % PIXELS_PER_PACK;

            for (uint32_t pack = 0; pack < fullPacks; ++pack)
            {
                uint16_t u, y1, y2, v;

                V210_READ_PACK_BLOCK(u, y1, v);
                if (line % 2 == 0) { *uvPtr++ = u << 6; }
                *yPtr++ = y1 << 6;
                if (line % 2 == 0) { *uvPtr++ = v << 6; }

                V210_READ_PACK_BLOCK(y1, u, y2);
                *yPtr++ = y1 << 6;
                if (line % 2 == 0) { *uvPtr++ = u << 6; }
                *yPtr++ = y2 << 6;

                V210_READ_PACK_BLOCK(v, y1, u);
                if (line % 2 == 0) { *uvPtr++ = v << 6; }
                *yPtr++ = y1 << 6;
                if (line % 2 == 0) { *uvPtr++ = u << 6; }

                V210_READ_PACK_BLOCK(y1, v, y2);
                *yPtr++ = y1 << 6;
                if (line % 2 == 0) { *uvPtr++ = v << 6; }
                *yPtr++ = y2 << 6;
            }

            if (extraPixels)
            {
                // This branch is unlikely to be hit for non-special cases.
            }

            // For non-special cases, simply copy the decoded data.
            uint16_t* lineDstY = dstY + line * m_width;
            uint16_t* lineDstUV = (line % 2 == 0) ? (dstUV + (line / 2) * m_width) : nullptr;
            memcpy(lineDstY, tempY.data(), m_width * sizeof(uint16_t));
            if (line % 2 == 0 && lineDstUV)
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
        (pixels / 2 / 2 * (2 * sizeof(uint16_t)));
}
