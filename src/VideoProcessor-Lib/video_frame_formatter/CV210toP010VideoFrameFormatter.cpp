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

 // ---------------------------------------------------------------------
 // Existing macros (unchanged)
#define V210_READ_PACK_BLOCK(a, b, c) \
    do {                              \
        val  = *src++;                \
        a = val & 0x3FF;              \
        b = (val >> 10) & 0x3FF;      \
        c = (val >> 20) & 0x3FF;      \
    } while (0)



#define P010_WRITE_VALUE(d, v) (*d++ = (v << 6))

#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))

// ---------------------------------------------------------------------
// New macros for 720p special-case handling
#define P010_WRITE_VALUE_720(dst, idx, val) ((dst)[(idx)] = (val) << 6)


#define CHROMA_NEUTRAL (512 << 6)  // 10-bit mid value shifted for P010 format

#define SET_BORDERS_AND_COPY(dst, src, totalPixels)                            \
    do {                                                                       \
        (dst)[0] = 0;                                                          \
        (dst)[1] = 0;                                                          \
        memcpy((dst) + 2, (src) + 2, ((totalPixels) - 4) * sizeof(uint16_t));  \
        (dst)[(totalPixels) - 2] = 0;                                          \
        (dst)[(totalPixels) - 1] = 0;                                          \
    } while (0)

#define SET_UV_BORDERS_AND_COPY(dst, src, totalPixels)                         \
    do {                                                                       \
        (dst)[0] = CHROMA_NEUTRAL;                                             \
        (dst)[1] = CHROMA_NEUTRAL;                                             \
        memcpy((dst) + 2, (src) + 2, ((totalPixels) - 4) * sizeof(uint16_t));  \
        (dst)[(totalPixels) - 2] = CHROMA_NEUTRAL;                             \
        (dst)[(totalPixels) - 1] = CHROMA_NEUTRAL;                             \
    } while (0)

// ---------------------------------------------------------------------
// OnVideoState: For 720p, do not pad m_width here; instead, set a flag (m_special720)
// so that FormatVideoFrame() can later decode extra pixels and fix borders.
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
    m_special720 = special720; // m_special720 is assumed declared in the class header.

    if (!special720 && (origWidth % PIXELS_PER_PACK != 0))
        throw std::runtime_error("Can only handle conversions which align with V210 boundry (6 pixels)");

    const uint32_t bytes = videoState->BytesPerFrame();
    uint32_t expectedBytes;
    if (special720)
    {
        const uint32_t aligned_width = ((1280 + 47) / 48) * 48;
        const uint32_t src_stride = aligned_width * 8 / 3;
        expectedBytes = m_height * src_stride;
    }
    else
    {
        expectedBytes = m_height * ((origWidth / PIXELS_PER_PACK) * BYTES_PER_PACK);
    }
    if (bytes != expectedBytes)
        throw std::runtime_error("Unexpected amount of bytes for frame");

    // For 720p, leave m_width as origWidth (1280) so that border-handling occurs in FormatVideoFrame.
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
    // P010: 10bpp per component; data in the high bits, zeros in the low bits.
    // https://docs.microsoft.com/en-us/windows/win32/medfound/10-bit-and-16-bit-yuv-video-formats

    const uint32_t pixels = m_height * m_width;
    uint16_t* dstY = (uint16_t*)outBuffer;
    uint16_t* dstUV = (uint16_t*)(outBuffer + ((ptrdiff_t)pixels * sizeof(uint16_t)));

    // For source reading, use m_width (1280 for 720p)
    uint32_t src_width = m_width;
    const uint32_t aligned_width = ((src_width + 47) / 48) * 48;
    const uint32_t src_stride = aligned_width * 8 / 3;

    for (uint32_t line = 0; line < m_height; line++)
    {
        const uint32_t* src = (const uint32_t*)((const BYTE*)inFrame.GetData() + (ptrdiff_t)(line * src_stride));
        bool isEvenLine = (line % 2 == 0);

        if (m_special720)
        {
            // For 1280x720, m_width % 6 equals 2.
            // To decode a complete pack, decode an extra 4 pixels:
            const uint32_t extraPixels = m_width % PIXELS_PER_PACK;             // 1280 % 6 = 2
            const uint32_t extraNeeded = (extraPixels == 0) ? 0 : (PIXELS_PER_PACK - extraPixels); // 4
            const uint32_t fullDecodedWidth = m_width + extraNeeded;              // 1280 + 4 = 1284
            const uint32_t fullPacks = fullDecodedWidth / PIXELS_PER_PACK;

            // Allocate temporary buffers for the full decoded line.
            std::vector<uint16_t> tempY(fullDecodedWidth);
            std::vector<uint16_t> tempUV(fullDecodedWidth);

            std::fill(tempUV.begin(), tempUV.end(), CHROMA_NEUTRAL);


            // Decode full packs from source into temp arrays using our macro.
            for (uint32_t pack = 0; pack < (fullPacks); ++pack) {

                uint32_t off = pack * PIXELS_PER_PACK;
                uint32_t val;
                uint16_t u, y1, y2, v;

                if (isEvenLine) {

                    V210_READ_PACK_BLOCK(u, y1, v);
                    P010_WRITE_VALUE_720(tempUV, off + 0, u);
                    P010_WRITE_VALUE_720(tempY, off + 0, y1);
                    P010_WRITE_VALUE_720(tempUV, off + 1, v);

                    V210_READ_PACK_BLOCK(y1, u, y2);
                    P010_WRITE_VALUE_720(tempY, off + 1, y1);
                    P010_WRITE_VALUE_720(tempUV, off + 2, u);
                    P010_WRITE_VALUE_720(tempY, off + 2, y2);

                    V210_READ_PACK_BLOCK(v, y1, u);
                    P010_WRITE_VALUE_720(tempUV, off + 3, v);
                    P010_WRITE_VALUE_720(tempY, off + 3, y1);
                    P010_WRITE_VALUE_720(tempUV, off + 4, u);

                    V210_READ_PACK_BLOCK(y1, v, y2);
                    P010_WRITE_VALUE_720(tempY, off + 4, y1);
                    P010_WRITE_VALUE_720(tempUV, off + 5, v);
                    P010_WRITE_VALUE_720(tempY, off + 5, y2);
                }
                else {

                    V210_READ_PACK_BLOCK(u, y1, v);
                    P010_WRITE_VALUE_720(tempY, off + 0, y1);

                    V210_READ_PACK_BLOCK(y1, u, y2);
                    P010_WRITE_VALUE_720(tempY, off + 1, y1);
                    P010_WRITE_VALUE_720(tempY, off + 2, y2);

                    V210_READ_PACK_BLOCK(v, y1, u);
                    P010_WRITE_VALUE_720(tempY, off + 3, y1);

                    V210_READ_PACK_BLOCK(y1, v, y2);
                    P010_WRITE_VALUE_720(tempY, off + 4, y1);
                    P010_WRITE_VALUE_720(tempY, off + 5, y2);
                }
            }
            // Set destination pointers for this line.
            uint16_t* lineDstY = dstY + line * m_width;
            uint16_t* lineDstUV = (isEvenLine) ? (dstUV + (line / 2) * m_width) : nullptr;

            SET_BORDERS_AND_COPY(lineDstY, tempY.data(), m_width);
            if (isEvenLine && lineDstUV)
            {
                SET_UV_BORDERS_AND_COPY(lineDstUV, tempUV.data(), m_width);
            }
        }
        else
        {
            // Non-720p: process pack-by-pack as before.
            const uint32_t packsPerLine = m_width / PIXELS_PER_PACK;
            for (uint32_t pack = 0; pack < packsPerLine; pack++)
            {
                uint32_t val;
                uint16_t u, y1, y2, v;
                if (isEvenLine)
                {
                    V210_READ_PACK_BLOCK(u, y1, v);
                    P010_WRITE_VALUE(dstUV, u);
                    P010_WRITE_VALUE(dstY, y1);
                    P010_WRITE_VALUE(dstUV, v);

                    V210_READ_PACK_BLOCK(y1, u, y2);
                    P010_WRITE_VALUE(dstY, y1);
                    P010_WRITE_VALUE(dstUV, u);
                    P010_WRITE_VALUE(dstY, y2);

                    V210_READ_PACK_BLOCK(v, y1, u);
                    P010_WRITE_VALUE(dstUV, v);
                    P010_WRITE_VALUE(dstY, y1);
                    P010_WRITE_VALUE(dstUV, u);

                    V210_READ_PACK_BLOCK(y1, v, y2);
                    P010_WRITE_VALUE(dstY, y1);
                    P010_WRITE_VALUE(dstUV, v);
                    P010_WRITE_VALUE(dstY, y2);
                }
                else
                {
                    V210_READ_PACK_BLOCK(u, y1, v);
                    P010_WRITE_VALUE(dstY, y1);

                    V210_READ_PACK_BLOCK(y1, u, y2);
                    P010_WRITE_VALUE(dstY, y1);
                    P010_WRITE_VALUE(dstY, y2);

                    V210_READ_PACK_BLOCK(v, y1, u);
                    P010_WRITE_VALUE(dstY, y1);

                    V210_READ_PACK_BLOCK(y1, v, y2);
                    P010_WRITE_VALUE(dstY, y1);
                    P010_WRITE_VALUE(dstY, y2);
                }
            }
        }
    }

    return true;
}

LONG CV210toP010VideoFrameFormatter::GetOutFrameSize() const
{
    const LONG pixels = m_height * m_width;
    return (pixels * sizeof(uint16_t)) +
        (pixels / 2 / 2 * (2 * sizeof(uint16_t)));  // One Y per pixel, UV for every 2 pixels on even rows.
}
