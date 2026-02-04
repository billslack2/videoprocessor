/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "CARGBtoP010VideoFrameFormatter.h"

// ARGB format: [A R G B] per pixel (4 bytes, 8-bit per component)
// BGRA format: [B G R A] per pixel (4 bytes, 8-bit per component)
// P010 format: Planar 4:2:0, 10-bit in 16-bit words (data in high 10 bits)
//
// RGB to YUV conversion using standard matrices:
// BT.709 (HD):
//   Y  =  0.2126 * R + 0.7152 * G + 0.0722 * B
//   Cb = -0.1146 * R - 0.3854 * G + 0.5000 * B + 128
//   Cr =  0.5000 * R - 0.4542 * G - 0.0458 * B + 128
//
// BT.2020 (UHD/HDR):
//   Y  =  0.2627 * R + 0.6780 * G + 0.0593 * B
//   Cb = -0.1396 * R - 0.3604 * G + 0.5000 * B + 128
//   Cr =  0.5000 * R - 0.4598 * G - 0.0402 * B + 128

// Fixed-point coefficients (scaled by 65536 for precision)
// BT.709
static const int32_t BT709_Y_R  =  13933;  // 0.2126 * 65536
static const int32_t BT709_Y_G  =  46871;  // 0.7152 * 65536
static const int32_t BT709_Y_B  =   4732;  // 0.0722 * 65536
static const int32_t BT709_CB_R = -7508;   // -0.1146 * 65536
static const int32_t BT709_CB_G = -25259;  // -0.3854 * 65536
static const int32_t BT709_CB_B =  32768;  // 0.5000 * 65536
static const int32_t BT709_CR_R =  32768;  // 0.5000 * 65536
static const int32_t BT709_CR_G = -29763;  // -0.4542 * 65536
static const int32_t BT709_CR_B = -3005;   // -0.0458 * 65536

// BT.2020
static const int32_t BT2020_Y_R  =  17218;  // 0.2627 * 65536
static const int32_t BT2020_Y_G  =  44444;  // 0.6780 * 65536
static const int32_t BT2020_Y_B  =   3886;  // 0.0593 * 65536
static const int32_t BT2020_CB_R = -9147;   // -0.1396 * 65536
static const int32_t BT2020_CB_G = -23621;  // -0.3604 * 65536
static const int32_t BT2020_CB_B =  32768;  // 0.5000 * 65536
static const int32_t BT2020_CR_R =  32768;  // 0.5000 * 65536
static const int32_t BT2020_CR_G = -30134;  // -0.4598 * 65536
static const int32_t BT2020_CR_B = -2634;   // -0.0402 * 65536

void CARGBtoP010VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("Null video state is not allowed");

	if (videoState->videoFrameEncoding != VideoFrameEncoding::ARGB_8BIT &&
		videoState->videoFrameEncoding != VideoFrameEncoding::BGRA_8BIT)
		throw std::runtime_error("Can only handle ARGB or BGRA input");

	m_isBGRA = (videoState->videoFrameEncoding == VideoFrameEncoding::BGRA_8BIT);

	m_height = videoState->displayMode->FrameHeight();
	if (m_height % 2 != 0)
		throw std::runtime_error("P010 output requires even number of lines");

	m_width = videoState->displayMode->FrameWidth();
	if (m_width % 2 != 0)
		throw std::runtime_error("P010 output requires even width");

	// Get the proper stride from VideoState
	m_srcStride = videoState->BytesPerRow();
	
	// Select color matrix based on colorspace
	m_useBT2020 = (videoState->colorspace == ColorSpace::BT_2020);
}

bool CARGBtoP010VideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame,
	BYTE* outBuffer)
{
	const auto startTime = GetWallClockTime();

	const uint8_t* src = static_cast<const uint8_t*>(inFrame.GetData());

	uint16_t* dstY = reinterpret_cast<uint16_t*>(outBuffer);
	uint16_t* dstUV = reinterpret_cast<uint16_t*>(outBuffer + m_width * m_height * sizeof(uint16_t));

	// Select coefficients based on colorspace
	const int32_t Y_R  = m_useBT2020 ? BT2020_Y_R  : BT709_Y_R;
	const int32_t Y_G  = m_useBT2020 ? BT2020_Y_G  : BT709_Y_G;
	const int32_t Y_B  = m_useBT2020 ? BT2020_Y_B  : BT709_Y_B;
	const int32_t CB_R = m_useBT2020 ? BT2020_CB_R : BT709_CB_R;
	const int32_t CB_G = m_useBT2020 ? BT2020_CB_G : BT709_CB_G;
	const int32_t CB_B = m_useBT2020 ? BT2020_CB_B : BT709_CB_B;
	const int32_t CR_R = m_useBT2020 ? BT2020_CR_R : BT709_CR_R;
	const int32_t CR_G = m_useBT2020 ? BT2020_CR_G : BT709_CR_G;
	const int32_t CR_B = m_useBT2020 ? BT2020_CR_B : BT709_CR_B;

	// Process line pairs (for 4:2:0 vertical subsampling)
	for (uint32_t line = 0; line < m_height; line += 2)
	{
		const uint8_t* srcLine0 = src + static_cast<ptrdiff_t>(line) * m_srcStride;
		const uint8_t* srcLine1 = src + static_cast<ptrdiff_t>(line + 1) * m_srcStride;

		uint16_t* dstY0 = dstY + static_cast<ptrdiff_t>(line) * m_width;
		uint16_t* dstY1 = dstY + static_cast<ptrdiff_t>(line + 1) * m_width;
		uint16_t* dstUVLine = dstUV + static_cast<ptrdiff_t>(line / 2) * m_width;

		// Process pixel pairs (2x2 blocks for 4:2:0 chroma subsampling)
		for (uint32_t x = 0; x < m_width; x += 2)
		{
			// Read 4 pixels (2x2 block) and extract RGB
			uint8_t r00, g00, b00;
			uint8_t r01, g01, b01;
			uint8_t r10, g10, b10;
			uint8_t r11, g11, b11;

			if (m_isBGRA)
			{
				// BGRA: [B G R A]
				b00 = srcLine0[0]; g00 = srcLine0[1]; r00 = srcLine0[2];
				b01 = srcLine0[4]; g01 = srcLine0[5]; r01 = srcLine0[6];
				b10 = srcLine1[0]; g10 = srcLine1[1]; r10 = srcLine1[2];
				b11 = srcLine1[4]; g11 = srcLine1[5]; r11 = srcLine1[6];
			}
			else
			{
				// ARGB: [A R G B]
				r00 = srcLine0[1]; g00 = srcLine0[2]; b00 = srcLine0[3];
				r01 = srcLine0[5]; g01 = srcLine0[6]; b01 = srcLine0[7];
				r10 = srcLine1[1]; g10 = srcLine1[2]; b10 = srcLine1[3];
				r11 = srcLine1[5]; g11 = srcLine1[6]; b11 = srcLine1[7];
			}

			// Convert RGB to Y for all 4 pixels
			// Y = (Y_R * R + Y_G * G + Y_B * B) >> 16
			int32_t y00 = (Y_R * r00 + Y_G * g00 + Y_B * b00) >> 16;
			int32_t y01 = (Y_R * r01 + Y_G * g01 + Y_B * b01) >> 16;
			int32_t y10 = (Y_R * r10 + Y_G * g10 + Y_B * b10) >> 16;
			int32_t y11 = (Y_R * r11 + Y_G * g11 + Y_B * b11) >> 16;

			// Clamp Y to 0-255
			y00 = (y00 < 0) ? 0 : (y00 > 255) ? 255 : y00;
			y01 = (y01 < 0) ? 0 : (y01 > 255) ? 255 : y01;
			y10 = (y10 < 0) ? 0 : (y10 > 255) ? 255 : y10;
			y11 = (y11 < 0) ? 0 : (y11 > 255) ? 255 : y11;

			// Write Y values (8-bit to P010: shift left 8 bits)
			*dstY0++ = static_cast<uint16_t>(y00) << 8;
			*dstY0++ = static_cast<uint16_t>(y01) << 8;
			*dstY1++ = static_cast<uint16_t>(y10) << 8;
			*dstY1++ = static_cast<uint16_t>(y11) << 8;

			// Average RGB over 2x2 block for chroma subsampling
			int32_t r_avg = (r00 + r01 + r10 + r11 + 2) >> 2;
			int32_t g_avg = (g00 + g01 + g10 + g11 + 2) >> 2;
			int32_t b_avg = (b00 + b01 + b10 + b11 + 2) >> 2;

			// Convert averaged RGB to Cb/Cr
			// Cb = (CB_R * R + CB_G * G + CB_B * B) >> 16 + 128
			// Cr = (CR_R * R + CR_G * G + CR_B * B) >> 16 + 128
			int32_t cb = ((CB_R * r_avg + CB_G * g_avg + CB_B * b_avg) >> 16) + 128;
			int32_t cr = ((CR_R * r_avg + CR_G * g_avg + CR_B * b_avg) >> 16) + 128;

			// Clamp Cb/Cr to 0-255
			cb = (cb < 0) ? 0 : (cb > 255) ? 255 : cb;
			cr = (cr < 0) ? 0 : (cr > 255) ? 255 : cr;

			// Write UV values (8-bit to P010: shift left 8 bits)
			*dstUVLine++ = static_cast<uint16_t>(cb) << 8;  // U
			*dstUVLine++ = static_cast<uint16_t>(cr) << 8;  // V

			// Advance source pointers (8 bytes per 2 pixels)
			srcLine0 += 8;
			srcLine1 += 8;
		}
	}

	// Track performance
	const auto endTime = GetWallClockTime();
	const uint64_t conversionTimeUs = (endTime - startTime) / 10;
	m_performanceWindow.AddSample(static_cast<double>(conversionTimeUs));

	return true;
}

LONG CARGBtoP010VideoFrameFormatter::GetOutFrameSize() const
{
	// P010 format:
	// - Y plane: width × height × 2 bytes (16-bit per pixel)
	// - UV plane: width × height/2 × 2 bytes (interleaved U/V, half vertical resolution)
	const LONG yPlaneSize = m_width * m_height * sizeof(uint16_t);
	const LONG uvPlaneSize = m_width * (m_height / 2) * sizeof(uint16_t);

	return yPlaneSize + uvPlaneSize;
}
