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
namespace
{
	constexpr int32_t Expand8To10(uint8_t value)
	{
		// Map both endpoints exactly: 0 -> 0 and 255 -> 1023.
		return (static_cast<int32_t>(value) * 1023 + 127) / 255;
	}

	constexpr int32_t Clamp10(int32_t value)
	{
		return value < 0 ? 0 : (value > 1023 ? 1023 : value);
	}
}

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

			// Convert in the 10-bit full-range domain. Do not use an 8-bit
			// intermediate: 255 << 2 is 1020, not the P010 full-range white
			// endpoint of 1023.
			auto toLuma = [&](uint8_t r, uint8_t g, uint8_t b)
			{
				return Clamp10((Y_R * Expand8To10(r) + Y_G * Expand8To10(g) +
					Y_B * Expand8To10(b) + 32768) >> 16);
			};
			const int32_t y00 = toLuma(r00, g00, b00);
			const int32_t y01 = toLuma(r01, g01, b01);
			const int32_t y10 = toLuma(r10, g10, b10);
			const int32_t y11 = toLuma(r11, g11, b11);

			*dstY0++ = static_cast<uint16_t>(y00 << 6);
			*dstY0++ = static_cast<uint16_t>(y01 << 6);
			*dstY1++ = static_cast<uint16_t>(y10 << 6);
			*dstY1++ = static_cast<uint16_t>(y11 << 6);
			// Average RGB over 2x2 block for chroma subsampling
			int32_t r_avg = (r00 + r01 + r10 + r11 + 2) >> 2;
			int32_t g_avg = (g00 + g01 + g10 + g11 + 2) >> 2;
			int32_t b_avg = (b00 + b01 + b10 + b11 + 2) >> 2;

			const int32_t rAvg10 = Expand8To10(static_cast<uint8_t>(r_avg));
			const int32_t gAvg10 = Expand8To10(static_cast<uint8_t>(g_avg));
			const int32_t bAvg10 = Expand8To10(static_cast<uint8_t>(b_avg));
			const int32_t cb = Clamp10(((CB_R * rAvg10 + CB_G * gAvg10 +
				CB_B * bAvg10 + 32768) >> 16) + 512);
			const int32_t cr = Clamp10(((CR_R * rAvg10 + CR_G * gAvg10 +
				CR_B * bAvg10 + 32768) >> 16) + 512);

			*dstUVLine++ = static_cast<uint16_t>(cb << 6);
			*dstUVLine++ = static_cast<uint16_t>(cr << 6);
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
