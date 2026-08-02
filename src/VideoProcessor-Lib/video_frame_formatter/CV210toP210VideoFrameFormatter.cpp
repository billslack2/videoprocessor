/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "CV210toP210VideoFrameFormatter.h"

//
// The v210 unpacking follows FFmpeg's GPL-licensed v210dec.c implementation:
// https://github.com/FFmpeg/FFmpeg/blob/n4.4.8/libavcodec/v210dec.c
//


#define P210_WRITE_VALUE(d, v) (*d++ = (v << 6))


#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))


void CV210toP210VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("Null video state is not allowed");

    if (videoState->videoFrameEncoding != VideoFrameEncoding::V210)
        throw std::runtime_error("Can only handle V210 input");

    m_height = videoState->displayMode->FrameHeight();
    m_width = videoState->displayMode->FrameWidth();
	if (m_width == 0 || m_height == 0 || (m_width & 1) != 0)
		throw std::runtime_error("P210 output requires a positive, even width");

	// DeckLink's v210 rows are 128-byte aligned. Do not assume the active
	// width is a multiple of a six-pixel v210 pack: the final pack can contain
	// unused padding components.
	m_sourceStride = videoState->BytesPerRow();
	const uint32_t packedBytes =
		((m_width + PIXELS_PER_PACK - 1) / PIXELS_PER_PACK) * BYTES_PER_PACK;
	if (m_sourceStride < packedBytes)
		throw std::runtime_error("v210 input row is smaller than the frame width");
}


bool CV210toP210VideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame,
	BYTE* outBuffer)
{
	// Read V210
	// https://wiki.multimedia.cx/index.php/V210

	// Write P210
    // 10bpp per component, data in the high bits, zeros in the low bits (we assume little-endian native)
	// https://docs.microsoft.com/en-us/windows/win32/medfound/10-bit-and-16-bit-yuv-video-formats

    const uint32_t pixels = m_height * m_width;
    uint16_t* dstY = (uint16_t *)outBuffer;
    uint16_t* dstUV = (uint16_t*)(outBuffer + ((ptrdiff_t)pixels * sizeof(uint16_t)));

    const uint32_t packsPerLine =
		(m_width + PIXELS_PER_PACK - 1) / PIXELS_PER_PACK;

    for (uint32_t line = 0; line < m_height; line++)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(
			static_cast<const BYTE*>(inFrame.GetData()) +
			static_cast<size_t>(line) * m_sourceStride);
		uint16_t* y = dstY + static_cast<size_t>(line) * m_width;
		uint16_t* uv = dstUV + static_cast<size_t>(line) * m_width;

        for (uint32_t pack = 0; pack < packsPerLine; ++pack)
        {
			const uint32_t word0 = *src++;
			const uint32_t word1 = *src++;
			const uint32_t word2 = *src++;
			const uint32_t word3 = *src++;
			const uint16_t luma[PIXELS_PER_PACK] = {
				static_cast<uint16_t>((word0 >> 10) & 0x3FF),
				static_cast<uint16_t>(word1 & 0x3FF),
				static_cast<uint16_t>((word1 >> 20) & 0x3FF),
				static_cast<uint16_t>((word2 >> 10) & 0x3FF),
				static_cast<uint16_t>(word3 & 0x3FF),
				static_cast<uint16_t>((word3 >> 20) & 0x3FF) };
			const uint16_t chromaU[3] = {
				static_cast<uint16_t>(word0 & 0x3FF),
				static_cast<uint16_t>((word1 >> 10) & 0x3FF),
				static_cast<uint16_t>((word2 >> 20) & 0x3FF) };
			const uint16_t chromaV[3] = {
				static_cast<uint16_t>((word0 >> 20) & 0x3FF),
				static_cast<uint16_t>(word2 & 0x3FF),
				static_cast<uint16_t>((word3 >> 10) & 0x3FF) };
			const uint32_t remaining = m_width - pack * PIXELS_PER_PACK;
			const uint32_t pixelCount = std::min<uint32_t>(
				static_cast<uint32_t>(PIXELS_PER_PACK), remaining);
			for (uint32_t pixel = 0; pixel < pixelCount; ++pixel)
				P210_WRITE_VALUE(y, luma[pixel]);
			for (uint32_t pair = 0; pair < pixelCount / 2; ++pair)
			{
				P210_WRITE_VALUE(uv, chromaU[pair]);
				P210_WRITE_VALUE(uv, chromaV[pair]);
			}
        }
    }

	return true;
}


LONG CV210toP210VideoFrameFormatter::GetOutFrameSize() const
{
    const LONG pixels = m_height * m_width;

    return
        (pixels * sizeof(uint16_t)) +  // Every pixel 1 y
        (pixels / 2 * (2 * sizeof(uint16_t)));  // Every 2 pixels 2 16-bit numbers
}
