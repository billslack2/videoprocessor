/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "CUYVYtoP010VideoFrameFormatter.h"
#include <immintrin.h>
#include <intrin.h>

// UYVY format: [U0 Y0 V0 Y1] [U2 Y2 V2 Y3] ... (4:2:2, 8-bit per component)
// Each macropixel (4 bytes) contains 2 pixels worth of Y and 1 shared U/V pair
// P010 format: Planar 4:2:0, 10-bit in 16-bit words (data in high 10 bits)
//
// AVX2 optimization strategy:
// - Process 32 pixels (64 UYVY bytes) per iteration
// - Use shuffle/permute to separate Y from UV
// - Use _mm256_cvtepu8_epi16 for 8-bit to 16-bit expansion
// - SIMD averaging for vertical chroma subsampling

CUYVYtoP010VideoFrameFormatter::CUYVYtoP010VideoFrameFormatter()
{
	// Pre-check CPU features on construction
	CheckCPUFeatures();
}

bool CUYVYtoP010VideoFrameFormatter::CheckCPUFeatures() const
{
	if (!m_cpuFeaturesChecked)
	{
		int cpuInfo[4];
		__cpuid(cpuInfo, 0);
		int nIds = cpuInfo[0];

		m_hasAVX2 = false;

		if (nIds >= 7)
		{
			__cpuidex(cpuInfo, 7, 0);
			m_hasAVX2 = (cpuInfo[1] & (1 << 5)) != 0; // EBX bit 5 is AVX2
		}

		m_cpuFeaturesChecked = true;
	}
	return m_hasAVX2;
}

void CUYVYtoP010VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("Null video state is not allowed");

	if (videoState->videoFrameEncoding != VideoFrameEncoding::UYVY &&
		videoState->videoFrameEncoding != VideoFrameEncoding::HDYC)
		throw std::runtime_error("Can only handle UYVY or HDYC input");

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
	const auto startTime = GetWallClockTime();

	const uint8_t* src = static_cast<const uint8_t*>(inFrame.GetData());
	uint16_t* dstY = reinterpret_cast<uint16_t*>(outBuffer);
	uint16_t* dstUV = reinterpret_cast<uint16_t*>(outBuffer + m_width * m_height * sizeof(uint16_t));

	// Choose conversion method based on CPU features
	if (m_hasAVX2)
	{
		ConvertAVX2(src, dstY, dstUV);
	}
	else
	{
		ConvertScalar(src, dstY, dstUV);
	}

	// Track performance
	const auto endTime = GetWallClockTime();
	const uint64_t conversionTimeUs = (endTime - startTime) / 10;  // 100ns ticks to microseconds
	m_performanceWindow.AddSample(static_cast<double>(conversionTimeUs));

	return true;
}

void CUYVYtoP010VideoFrameFormatter::ConvertScalar(
	const uint8_t* src,
	uint16_t* dstY,
	uint16_t* dstUV) const
{
	// Original scalar implementation - process line pairs for 4:2:0 subsampling
	for (uint32_t line = 0; line < m_height; line += 2)
	{
		const uint8_t* srcLine0 = src + static_cast<ptrdiff_t>(line) * m_srcStride;
		const uint8_t* srcLine1 = src + static_cast<ptrdiff_t>(line + 1) * m_srcStride;

		uint16_t* dstY0 = dstY + static_cast<ptrdiff_t>(line) * m_width;
		uint16_t* dstY1 = dstY + static_cast<ptrdiff_t>(line + 1) * m_width;
		uint16_t* dstUVLine = dstUV + static_cast<ptrdiff_t>(line / 2) * m_width;

		for (uint32_t x = 0; x < m_width; x += 2)
		{
			// Read UYVY macropixels from both lines
			const uint8_t u0_line0 = srcLine0[0];
			const uint8_t y0_line0 = srcLine0[1];
			const uint8_t v0_line0 = srcLine0[2];
			const uint8_t y1_line0 = srcLine0[3];

			const uint8_t u0_line1 = srcLine1[0];
			const uint8_t y0_line1 = srcLine1[1];
			const uint8_t v0_line1 = srcLine1[2];
			const uint8_t y1_line1 = srcLine1[3];

			// Write Y values (8-bit to P010: shift left 8 bits)
			*dstY0++ = static_cast<uint16_t>(y0_line0) << 8;
			*dstY0++ = static_cast<uint16_t>(y1_line0) << 8;
			*dstY1++ = static_cast<uint16_t>(y0_line1) << 8;
			*dstY1++ = static_cast<uint16_t>(y1_line1) << 8;

			// Average chroma vertically (4:2:2 ? 4:2:0) and convert 8-bit to P010
			const uint16_t u_avg = ((uint16_t)u0_line0 + (uint16_t)u0_line1 + 1) >> 1;
			const uint16_t v_avg = ((uint16_t)v0_line0 + (uint16_t)v0_line1 + 1) >> 1;

			*dstUVLine++ = u_avg << 8;
			*dstUVLine++ = v_avg << 8;

			srcLine0 += 4;
			srcLine1 += 4;
		}
	}
}

void CUYVYtoP010VideoFrameFormatter::ConvertAVX2(
	const uint8_t* src,
	uint16_t* dstY,
	uint16_t* dstUV) const
{
	// AVX2 processes 32 pixels per iteration
	// UYVY: 4 bytes per 2 pixels, so 32 pixels = 64 bytes = 2 x 256-bit loads
	// 
	// UYVY byte layout (32 pixels = 64 bytes):
	// [U0 Y0 V0 Y1] [U2 Y2 V2 Y3] ... [U30 Y30 V30 Y31]
	//
	// We need to:
	// 1. Extract all Y values (at odd byte positions: 1,3,5,7,...)
	// 2. Extract U values (at positions 0,4,8,12,...) and V values (at positions 2,6,10,14,...)
	// 3. Expand 8-bit to 16-bit and shift left by 8 for P010 format
	// 4. Average U/V vertically between line pairs

	// Shuffle mask to extract Y values from UYVY
	// UYVY bytes: [U0 Y0 V0 Y1 U2 Y2 V2 Y3 U4 Y4 V4 Y5 U6 Y6 V6 Y7] (16 bytes = 8 pixels)
	// We want Y:  [Y0 Y1 Y2 Y3 Y4 Y5 Y6 Y7] at positions 1,3,5,7,9,11,13,15
	const __m256i shuffle_y = _mm256_setr_epi8(
		1, 3, 5, 7, 9, 11, 13, 15,  // First 128-bit lane: Y from bytes 1,3,5,7,9,11,13,15
		-1, -1, -1, -1, -1, -1, -1, -1,  // Padding (will be filled from second load)
		1, 3, 5, 7, 9, 11, 13, 15,  // Second 128-bit lane (for second 16 bytes of input)
		-1, -1, -1, -1, -1, -1, -1, -1   // Padding
	);

	// Shuffle mask to extract U values (positions 0,4,8,12 in each 16-byte chunk)
	const __m256i shuffle_u = _mm256_setr_epi8(
		0, 4, 8, 12, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		0, 4, 8, 12, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1
	);

	// Shuffle mask to extract V values (positions 2,6,10,14 in each 16-byte chunk)
	const __m256i shuffle_v = _mm256_setr_epi8(
		2, 6, 10, 14, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		2, 6, 10, 14, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1
	);

	// Rounding constant for averaging (add 1 before right shift)
	const __m256i round_const = _mm256_set1_epi16(1);

	// Number of pixels we can process with full AVX2 iterations
	// Each iteration processes 16 pixels (32 UYVY bytes = 1 x 256-bit load)
	const uint32_t pixelsPerIter = 16;
	const uint32_t fullIters = m_width / pixelsPerIter;
	const uint32_t remainderPixels = m_width % pixelsPerIter;

	// Process line pairs
	for (uint32_t line = 0; line < m_height; line += 2)
	{
		const uint8_t* srcLine0 = src + static_cast<ptrdiff_t>(line) * m_srcStride;
		const uint8_t* srcLine1 = src + static_cast<ptrdiff_t>(line + 1) * m_srcStride;

		uint16_t* dstY0 = dstY + static_cast<ptrdiff_t>(line) * m_width;
		uint16_t* dstY1 = dstY + static_cast<ptrdiff_t>(line + 1) * m_width;
		uint16_t* dstUVLine = dstUV + static_cast<ptrdiff_t>(line / 2) * m_width;

		// Prefetch next lines
		if (line + 4 < m_height)
		{
			_mm_prefetch(reinterpret_cast<const char*>(src + static_cast<ptrdiff_t>(line + 2) * m_srcStride), _MM_HINT_T0);
			_mm_prefetch(reinterpret_cast<const char*>(src + static_cast<ptrdiff_t>(line + 3) * m_srcStride), _MM_HINT_T0);
		}

		// Process 16 pixels per iteration
		for (uint32_t i = 0; i < fullIters; i++)
		{
			// Load 32 bytes from each line (16 pixels worth of UYVY)
			__m256i in0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine0));
			__m256i in1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(srcLine1));

			// Extract Y values from line 0
			// Shuffle to get Y bytes packed together
			__m256i y0_bytes = _mm256_shuffle_epi8(in0, shuffle_y);
			// The Y values are now at: [Y0-Y7 in lo lane bits 0-63, Y8-Y15 in hi lane bits 0-63]
			// Extract lower 64 bits from each lane and combine
			__m128i y0_lo = _mm256_castsi256_si128(y0_bytes);  // Y0-Y7 in bits 0-63
			__m128i y0_hi = _mm256_extracti128_si256(y0_bytes, 1);  // Y8-Y15 in bits 0-63
			// Combine into single 128-bit register with all 16 Y bytes
			__m128i y0_packed = _mm_unpacklo_epi64(y0_lo, y0_hi);

			// Expand Y0 from 8-bit to 16-bit and shift left 8 for P010
			__m256i y0_16 = _mm256_cvtepu8_epi16(y0_packed);
			y0_16 = _mm256_slli_epi16(y0_16, 8);

			// Extract Y values from line 1
			__m256i y1_bytes = _mm256_shuffle_epi8(in1, shuffle_y);
			__m128i y1_lo = _mm256_castsi256_si128(y1_bytes);
			__m128i y1_hi = _mm256_extracti128_si256(y1_bytes, 1);
			__m128i y1_packed = _mm_unpacklo_epi64(y1_lo, y1_hi);
			__m256i y1_16 = _mm256_cvtepu8_epi16(y1_packed);
			y1_16 = _mm256_slli_epi16(y1_16, 8);

			// Store Y values
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstY0), y0_16);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(dstY1), y1_16);

			// Extract U values from both lines
			__m256i u0_bytes = _mm256_shuffle_epi8(in0, shuffle_u);
			__m256i u1_bytes = _mm256_shuffle_epi8(in1, shuffle_u);
			// U values are now at bits 0-31 of each 128-bit lane (4 U values per lane)
			__m128i u0_lo = _mm256_castsi256_si128(u0_bytes);  // U0,U2,U4,U6
			__m128i u0_hi = _mm256_extracti128_si256(u0_bytes, 1);  // U8,U10,U12,U14
			__m128i u0_packed = _mm_unpacklo_epi32(u0_lo, u0_hi);  // All 8 U values in 64 bits
			
			__m128i u1_lo = _mm256_castsi256_si128(u1_bytes);
			__m128i u1_hi = _mm256_extracti128_si256(u1_bytes, 1);
			__m128i u1_packed = _mm_unpacklo_epi32(u1_lo, u1_hi);

			// Extract V values from both lines
			__m256i v0_bytes = _mm256_shuffle_epi8(in0, shuffle_v);
			__m256i v1_bytes = _mm256_shuffle_epi8(in1, shuffle_v);
			__m128i v0_lo = _mm256_castsi256_si128(v0_bytes);
			__m128i v0_hi = _mm256_extracti128_si256(v0_bytes, 1);
			__m128i v0_packed = _mm_unpacklo_epi32(v0_lo, v0_hi);
			
			__m128i v1_lo = _mm256_castsi256_si128(v1_bytes);
			__m128i v1_hi = _mm256_extracti128_si256(v1_bytes, 1);
			__m128i v1_packed = _mm_unpacklo_epi32(v1_lo, v1_hi);

			// Expand U values to 16-bit for averaging
			__m128i u0_16 = _mm_cvtepu8_epi16(u0_packed);
			__m128i u1_16 = _mm_cvtepu8_epi16(u1_packed);
			__m128i v0_16 = _mm_cvtepu8_epi16(v0_packed);
			__m128i v1_16 = _mm_cvtepu8_epi16(v1_packed);

			// Average U and V vertically (add with rounding)
			__m128i u_sum = _mm_add_epi16(u0_16, u1_16);
			__m128i v_sum = _mm_add_epi16(v0_16, v1_16);
			__m128i round_const_128 = _mm_set1_epi16(1);
			u_sum = _mm_add_epi16(u_sum, round_const_128);
			v_sum = _mm_add_epi16(v_sum, round_const_128);
			__m128i u_avg = _mm_srli_epi16(u_sum, 1);
			__m128i v_avg = _mm_srli_epi16(v_sum, 1);

			// Shift left 8 for P010 format
			u_avg = _mm_slli_epi16(u_avg, 8);
			v_avg = _mm_slli_epi16(v_avg, 8);

			// Interleave U and V for P010 UV plane (U0 V0 U1 V1 ...)
			__m128i uv_lo = _mm_unpacklo_epi16(u_avg, v_avg);  // U0 V0 U1 V1 U2 V2 U3 V3
			__m128i uv_hi = _mm_unpackhi_epi16(u_avg, v_avg);  // U4 V4 U5 V5 U6 V6 U7 V7

			// Store UV values (16 values = 8 UV pairs)
			_mm_storeu_si128(reinterpret_cast<__m128i*>(dstUVLine), uv_lo);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(dstUVLine + 8), uv_hi);

			srcLine0 += 32;  // 16 pixels * 2 bytes/pixel
			srcLine1 += 32;
			dstY0 += 16;
			dstY1 += 16;
			dstUVLine += 16;  // 8 UV pairs * 2 values per pair
		}

		// Handle remaining pixels with scalar code
		for (uint32_t x = 0; x < remainderPixels; x += 2)
		{
			const uint8_t u0_line0 = srcLine0[0];
			const uint8_t y0_line0 = srcLine0[1];
			const uint8_t v0_line0 = srcLine0[2];
			const uint8_t y1_line0 = srcLine0[3];

			const uint8_t u0_line1 = srcLine1[0];
			const uint8_t y0_line1 = srcLine1[1];
			const uint8_t v0_line1 = srcLine1[2];
			const uint8_t y1_line1 = srcLine1[3];

			*dstY0++ = static_cast<uint16_t>(y0_line0) << 8;
			*dstY0++ = static_cast<uint16_t>(y1_line0) << 8;
			*dstY1++ = static_cast<uint16_t>(y0_line1) << 8;
			*dstY1++ = static_cast<uint16_t>(y1_line1) << 8;

			const uint16_t u_avg = ((uint16_t)u0_line0 + (uint16_t)u0_line1 + 1) >> 1;
			const uint16_t v_avg = ((uint16_t)v0_line0 + (uint16_t)v0_line1 + 1) >> 1;

			*dstUVLine++ = u_avg << 8;
			*dstUVLine++ = v_avg << 8;

			srcLine0 += 4;
			srcLine1 += 4;
		}
	}
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
