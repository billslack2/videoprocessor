/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "CUYVYtoP010VideoFrameFormatter.h"

// UYVY format: [U0 Y0 V0 Y1] [U2 Y2 V2 Y3] ... (4:2:2, 8-bit per component)
// Each macropixel (4 bytes) contains 2 pixels worth of Y and 1 shared U/V pair
// P010 format: Planar 4:2:0, 10-bit in 16-bit words (data in high 10 bits)

void CUYVYtoP010VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("Null video state is not allowed");

	if (videoState->videoFrameEncoding != VideoFrameEncoding::UYVY)
		throw std::runtime_error("Can only handle UYVY input");

	m_height = videoState->displayMode->FrameHeight();
	if (m_height % 2 != 0)
		throw std::runtime_error("P010 output requires even number of lines");

	m_width = videoState->displayMode->FrameWidth();
	if (m_width % 2 != 0)
		throw std::runtime_error("P010 output requires even width");

	// Get the proper stride from VideoState (handles any padding/alignment)
	m_srcStride = videoState->BytesPerRow();
}

bool CUYVYtoP010VideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame,
	BYTE* outBuffer)
{
	// UYVY is packed: U Y V Y (4:2:2 - chroma sampled every 2 pixels horizontally)
	// P010 is planar: Y plane (full res) + interleaved UV plane (half res both dimensions)

	const uint8_t* src = static_cast<const uint8_t*>(inFrame.GetData());

	uint16_t* dstY = reinterpret_cast<uint16_t*>(outBuffer);
	uint16_t* dstUV = reinterpret_cast<uint16_t*>(outBuffer + m_width * m_height * sizeof(uint16_t));

	// Process line pairs (for 4:2:0 vertical subsampling)
	for (uint32_t line = 0; line < m_height; line += 2)
	{
		const uint8_t* srcLine0 = src + static_cast<ptrdiff_t>(line) * m_srcStride;
		const uint8_t* srcLine1 = src + static_cast<ptrdiff_t>(line + 1) * m_srcStride;

		uint16_t* dstY0 = dstY + static_cast<ptrdiff_t>(line) * m_width;
		uint16_t* dstY1 = dstY + static_cast<ptrdiff_t>(line + 1) * m_width;
		uint16_t* dstUVLine = dstUV + static_cast<ptrdiff_t>(line / 2) * m_width;

		// Process pixel pairs (UYVY contains 2 pixels per 4 bytes)
		for (uint32_t x = 0; x < m_width; x += 2)
		{
			// Read UYVY macropixels from both lines
			// UYVY format: [U0 Y0 V0 Y1] - 4 bytes for 2 pixels
			// Line 0:
			const uint8_t u0_line0 = srcLine0[0];
			const uint8_t y0_line0 = srcLine0[1];
			const uint8_t v0_line0 = srcLine0[2];
			const uint8_t y1_line0 = srcLine0[3];

			// Line 1:
			const uint8_t u0_line1 = srcLine1[0];
			const uint8_t y0_line1 = srcLine1[1];
			const uint8_t v0_line1 = srcLine1[2];
			const uint8_t y1_line1 = srcLine1[3];

			// Write Y values (8-bit to P010: shift left 8 bits)
			// P010 stores 10-bit in upper bits of 16-bit words
			// 8-bit (0-255) << 8 = 16-bit value with 8-bit data in upper byte
			// This matches how 10-bit data would be stored: value << 6
			// For 8-bit: (value << 2) << 6 = value << 8 to scale and position correctly
			*dstY0++ = static_cast<uint16_t>(y0_line0) << 8;
			*dstY0++ = static_cast<uint16_t>(y1_line0) << 8;
			*dstY1++ = static_cast<uint16_t>(y0_line1) << 8;
			*dstY1++ = static_cast<uint16_t>(y1_line1) << 8;

			// Average chroma vertically (4:2:2 ? 4:2:0) and convert 8-bit to P010
			const uint16_t u_avg = ((uint16_t)u0_line0 + (uint16_t)u0_line1 + 1) >> 1;  // Average with rounding
			const uint16_t v_avg = ((uint16_t)v0_line0 + (uint16_t)v0_line1 + 1) >> 1;

			*dstUVLine++ = u_avg << 8;  // 8-bit to P010
			*dstUVLine++ = v_avg << 8;

			// Advance source pointers (4 bytes per macropixel = 2 pixels)
			srcLine0 += 4;
			srcLine1 += 4;
		}
	}

	return true;
}

LONG CUYVYtoP010VideoFrameFormatter::GetOutFrameSize() const
{
	// P010 format:
	// - Y plane: width × height × 2 bytes (16-bit per pixel)
	// - UV plane: width × height/2 × 2 bytes (interleaved U/V, half vertical resolution)
	const LONG yPlaneSize = m_width * m_height * sizeof(uint16_t);
	const LONG uvPlaneSize = m_width * (m_height / 2) * sizeof(uint16_t);  // Interleaved UV

	return yPlaneSize + uvPlaneSize;
}
