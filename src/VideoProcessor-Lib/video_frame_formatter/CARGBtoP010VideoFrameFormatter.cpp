/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <immintrin.h>
#include <intrin.h>
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

	bool CpuSupportsAVX2() noexcept
	{
		int cpuInfo[4] = {};
		__cpuid(cpuInfo, 0);
		if (cpuInfo[0] < 7)
			return false;
		__cpuid(cpuInfo, 1);
		constexpr int osxsave = 1 << 27;
		constexpr int avx = 1 << 28;
		if ((cpuInfo[2] & (osxsave | avx)) != (osxsave | avx))
			return false;
		if ((_xgetbv(0) & 0x6) != 0x6)
			return false;
		__cpuidex(cpuInfo, 7, 0);
		return (cpuInfo[1] & (1 << 5)) != 0;
	}

	inline __m256i Expand8To10AVX2(__m256i value) noexcept
	{
		// Exact round(value * 1023 / 255) = value * 4 + round(value / 85).
		const __m256i one = _mm256_set1_epi32(1);
		__m256i extra = _mm256_and_si256(
			_mm256_cmpgt_epi32(value, _mm256_set1_epi32(42)), one);
		extra = _mm256_add_epi32(extra, _mm256_and_si256(
			_mm256_cmpgt_epi32(value, _mm256_set1_epi32(127)), one));
		extra = _mm256_add_epi32(extra, _mm256_and_si256(
			_mm256_cmpgt_epi32(value, _mm256_set1_epi32(212)), one));
		return _mm256_add_epi32(_mm256_slli_epi32(value, 2), extra);
	}

	inline __m256i FullRangeMatrixAVX2(__m256i r, __m256i g, __m256i b,
		int32_t coefficientR, int32_t coefficientG, int32_t coefficientB,
		int32_t outputOffset) noexcept
	{
		__m256i result = _mm256_mullo_epi32(r, _mm256_set1_epi32(coefficientR));
		result = _mm256_add_epi32(result,
			_mm256_mullo_epi32(g, _mm256_set1_epi32(coefficientG)));
		result = _mm256_add_epi32(result,
			_mm256_mullo_epi32(b, _mm256_set1_epi32(coefficientB)));
		result = _mm256_srai_epi32(
			_mm256_add_epi32(result, _mm256_set1_epi32(32768)), 16);
		result = _mm256_add_epi32(result, _mm256_set1_epi32(outputOffset));
		result = _mm256_max_epi32(result, _mm256_setzero_si256());
		result = _mm256_min_epi32(result, _mm256_set1_epi32(1023));
		return _mm256_slli_epi32(result, 6);
	}

	inline void ExtractRGB8AVX2(__m256i pixels, bool bgra,
		__m256i& r, __m256i& g, __m256i& b) noexcept
	{
		const __m256i mask = _mm256_set1_epi32(0xff);
		if (bgra)
		{
			b = _mm256_and_si256(pixels, mask);
			g = _mm256_and_si256(_mm256_srli_epi32(pixels, 8), mask);
			r = _mm256_and_si256(_mm256_srli_epi32(pixels, 16), mask);
		}
		else
		{
			r = _mm256_and_si256(_mm256_srli_epi32(pixels, 8), mask);
			g = _mm256_and_si256(_mm256_srli_epi32(pixels, 16), mask);
			b = _mm256_srli_epi32(pixels, 24);
		}
	}

	inline void StoreEightP010Samples(uint16_t* destination,
		__m256i samples) noexcept
	{
		const __m128i packed = _mm_packus_epi32(
			_mm256_castsi256_si128(samples),
			_mm256_extracti128_si256(samples, 1));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(destination), packed);
	}
}


CARGBtoP010VideoFrameFormatter::CARGBtoP010VideoFrameFormatter() :
	m_hasAVX2(CpuSupportsAVX2())
{
	for (uint32_t i = 0; i < MAX_WORKERS; ++i)
	{
		m_conversionWork[i] = CreateThreadpoolWork(
			ConversionWorkCallback, this, nullptr);
		if (!m_conversionWork[i])
			break;
		++m_workerCount;
	}
}

CARGBtoP010VideoFrameFormatter::~CARGBtoP010VideoFrameFormatter()
{
	for (uint32_t i = 0; i < m_workerCount; ++i)
	{
		WaitForThreadpoolWorkCallbacks(m_conversionWork[i], TRUE);
		CloseThreadpoolWork(m_conversionWork[i]);
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

	const uint32_t totalPairs = m_height / 2;
	if (m_workerCount > 0 &&
		static_cast<uint64_t>(m_width) * m_height >= 1920ULL * 1080ULL)
	{
		m_workerSource = src;
		m_workerDestinationY = dstY;
		m_workerDestinationUV = dstUV;
		const uint32_t pairsPerSegment = totalPairs / (m_workerCount + 1);
		uint32_t firstPair = 0;
		for (uint32_t i = 0; i < m_workerCount; ++i)
		{
			m_workerFirstPair[i] = firstPair;
			m_workerPairCount[i] = pairsPerSegment;
			firstPair += pairsPerSegment;
			SubmitThreadpoolWork(m_conversionWork[i]);
		}
		ConvertRowPairs(src, dstY, dstUV,
			firstPair, totalPairs - firstPair);
		for (uint32_t i = 0; i < m_workerCount; ++i)
			WaitForThreadpoolWorkCallbacks(m_conversionWork[i], FALSE);
	}
	else
		ConvertRowPairs(src, dstY, dstUV, 0, totalPairs);

	// Track performance
	const auto endTime = GetWallClockTime();
	const uint64_t conversionTimeUs = (endTime - startTime) / 10;
	m_performanceWindow.AddSample(static_cast<double>(conversionTimeUs));

	return true;
}


void CARGBtoP010VideoFrameFormatter::ConvertRowPairs(
	const uint8_t* source, uint16_t* destinationY,
	uint16_t* destinationUV, uint32_t firstPair, uint32_t pairCount) const
{
	if (m_hasAVX2 && m_conversionMethod != ConversionMethod::SCALAR)
		ConvertAVX2(source, destinationY, destinationUV, firstPair, pairCount);
	else
		ConvertScalar(source, destinationY, destinationUV, firstPair, pairCount);
}


void CARGBtoP010VideoFrameFormatter::ConvertScalar(
	const uint8_t* source, uint16_t* destinationY,
	uint16_t* destinationUV, uint32_t firstPair, uint32_t pairCount) const
{
	const int32_t yR = m_useBT2020 ? BT2020_Y_R : BT709_Y_R;
	const int32_t yG = m_useBT2020 ? BT2020_Y_G : BT709_Y_G;
	const int32_t yB = m_useBT2020 ? BT2020_Y_B : BT709_Y_B;
	const int32_t cbR = m_useBT2020 ? BT2020_CB_R : BT709_CB_R;
	const int32_t cbG = m_useBT2020 ? BT2020_CB_G : BT709_CB_G;
	const int32_t cbB = m_useBT2020 ? BT2020_CB_B : BT709_CB_B;
	const int32_t crR = m_useBT2020 ? BT2020_CR_R : BT709_CR_R;
	const int32_t crG = m_useBT2020 ? BT2020_CR_G : BT709_CR_G;
	const int32_t crB = m_useBT2020 ? BT2020_CR_B : BT709_CR_B;

	const uint32_t firstLine = firstPair * 2;
	const uint32_t endLine = firstLine + pairCount * 2;
	for (uint32_t line = firstLine; line < endLine; line += 2)
	{
		const uint8_t* source0 = source + static_cast<size_t>(line) * m_srcStride;
		const uint8_t* source1 = source0 + m_srcStride;
		uint16_t* y0 = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* y1 = y0 + m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(line / 2) * m_width;

		for (uint32_t x = 0; x < m_width; x += 2)
		{
			uint8_t r00, g00, b00, r01, g01, b01;
			uint8_t r10, g10, b10, r11, g11, b11;
			if (m_isBGRA)
			{
				b00 = source0[0]; g00 = source0[1]; r00 = source0[2];
				b01 = source0[4]; g01 = source0[5]; r01 = source0[6];
				b10 = source1[0]; g10 = source1[1]; r10 = source1[2];
				b11 = source1[4]; g11 = source1[5]; r11 = source1[6];
			}
			else
			{
				r00 = source0[1]; g00 = source0[2]; b00 = source0[3];
				r01 = source0[5]; g01 = source0[6]; b01 = source0[7];
				r10 = source1[1]; g10 = source1[2]; b10 = source1[3];
				r11 = source1[5]; g11 = source1[6]; b11 = source1[7];
			}

			auto toLuma = [&](uint8_t r, uint8_t g, uint8_t b)
			{
				return Clamp10((yR * Expand8To10(r) + yG * Expand8To10(g) +
					yB * Expand8To10(b) + 32768) >> 16);
			};
			*y0++ = static_cast<uint16_t>(toLuma(r00, g00, b00) << 6);
			*y0++ = static_cast<uint16_t>(toLuma(r01, g01, b01) << 6);
			*y1++ = static_cast<uint16_t>(toLuma(r10, g10, b10) << 6);
			*y1++ = static_cast<uint16_t>(toLuma(r11, g11, b11) << 6);

			const int32_t rAverage = (r00 + r01 + r10 + r11 + 2) >> 2;
			const int32_t gAverage = (g00 + g01 + g10 + g11 + 2) >> 2;
			const int32_t bAverage = (b00 + b01 + b10 + b11 + 2) >> 2;
			const int32_t rAverage10 = Expand8To10(static_cast<uint8_t>(rAverage));
			const int32_t gAverage10 = Expand8To10(static_cast<uint8_t>(gAverage));
			const int32_t bAverage10 = Expand8To10(static_cast<uint8_t>(bAverage));
			const int32_t cb = Clamp10(((cbR * rAverage10 + cbG * gAverage10 +
				cbB * bAverage10 + 32768) >> 16) + 512);
			const int32_t cr = Clamp10(((crR * rAverage10 + crG * gAverage10 +
				crB * bAverage10 + 32768) >> 16) + 512);
			*uv++ = static_cast<uint16_t>(cb << 6);
			*uv++ = static_cast<uint16_t>(cr << 6);
			source0 += 8;
			source1 += 8;
		}
	}
}


void CARGBtoP010VideoFrameFormatter::ConvertAVX2(
	const uint8_t* source, uint16_t* destinationY,
	uint16_t* destinationUV, uint32_t firstPair, uint32_t pairCount) const
{
	if ((m_width & 7U) != 0)
	{
		ConvertScalar(source, destinationY, destinationUV, firstPair, pairCount);
		return;
	}
	const int32_t yR = m_useBT2020 ? BT2020_Y_R : BT709_Y_R;
	const int32_t yG = m_useBT2020 ? BT2020_Y_G : BT709_Y_G;
	const int32_t yB = m_useBT2020 ? BT2020_Y_B : BT709_Y_B;
	const int32_t cbR = m_useBT2020 ? BT2020_CB_R : BT709_CB_R;
	const int32_t cbG = m_useBT2020 ? BT2020_CB_G : BT709_CB_G;
	const int32_t cbB = m_useBT2020 ? BT2020_CB_B : BT709_CB_B;
	const int32_t crR = m_useBT2020 ? BT2020_CR_R : BT709_CR_R;
	const int32_t crG = m_useBT2020 ? BT2020_CR_G : BT709_CR_G;
	const int32_t crB = m_useBT2020 ? BT2020_CR_B : BT709_CR_B;
	const __m256i swapAdjacent = _mm256_setr_epi32(1, 0, 3, 2, 5, 4, 7, 6);
	const __m256i selectEven = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
	const uint32_t vectorWidth = m_width;

	const uint32_t firstLine = firstPair * 2;
	const uint32_t endLine = firstLine + pairCount * 2;
	for (uint32_t line = firstLine; line < endLine; line += 2)
	{
		const uint8_t* source0 = source + static_cast<size_t>(line) * m_srcStride;
		const uint8_t* source1 = source0 + m_srcStride;
		uint16_t* y0 = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* y1 = y0 + m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(line / 2) * m_width;
		uint32_t x = 0;
		for (; x < vectorWidth; x += 8)
		{
			const __m256i pixels0 = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(source0 + x * 4U));
			const __m256i pixels1 = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(source1 + x * 4U));
			__m256i r0, g0, b0, r1, g1, b1;
			ExtractRGB8AVX2(pixels0, m_isBGRA, r0, g0, b0);
			ExtractRGB8AVX2(pixels1, m_isBGRA, r1, g1, b1);

			StoreEightP010Samples(y0 + x, FullRangeMatrixAVX2(
				Expand8To10AVX2(r0), Expand8To10AVX2(g0), Expand8To10AVX2(b0),
				yR, yG, yB, 0));
			StoreEightP010Samples(y1 + x, FullRangeMatrixAVX2(
				Expand8To10AVX2(r1), Expand8To10AVX2(g1), Expand8To10AVX2(b1),
				yR, yG, yB, 0));

			auto averageTwoByTwo = [&swapAdjacent](__m256i evenRow,
				__m256i oddRow) noexcept
			{
				__m256i sum = _mm256_add_epi32(evenRow,
					_mm256_permutevar8x32_epi32(evenRow, swapAdjacent));
				sum = _mm256_add_epi32(sum, oddRow);
				sum = _mm256_add_epi32(sum,
					_mm256_permutevar8x32_epi32(oddRow, swapAdjacent));
				return _mm256_srli_epi32(
					_mm256_add_epi32(sum, _mm256_set1_epi32(2)), 2);
			};
			const __m256i rAverage = Expand8To10AVX2(averageTwoByTwo(r0, r1));
			const __m256i gAverage = Expand8To10AVX2(averageTwoByTwo(g0, g1));
			const __m256i bAverage = Expand8To10AVX2(averageTwoByTwo(b0, b1));
			const __m256i cb = _mm256_permutevar8x32_epi32(
				FullRangeMatrixAVX2(rAverage, gAverage, bAverage,
					cbR, cbG, cbB, 512), selectEven);
			const __m256i cr = _mm256_permutevar8x32_epi32(
				FullRangeMatrixAVX2(rAverage, gAverage, bAverage,
					crR, crG, crB, 512), selectEven);
			const __m128i cbLow = _mm256_castsi256_si128(cb);
			const __m128i crLow = _mm256_castsi256_si128(cr);
			const __m128i chromaLow = _mm_unpacklo_epi32(cbLow, crLow);
			const __m128i chromaHigh = _mm_unpackhi_epi32(cbLow, crLow);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(uv + x),
				_mm_packus_epi32(chromaLow, chromaHigh));
		}
	}
}


void CALLBACK CARGBtoP010VideoFrameFormatter::ConversionWorkCallback(
	PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK work)
{
	auto* formatter = static_cast<CARGBtoP010VideoFrameFormatter*>(context);
	uint32_t workerIndex = 0;
	while (workerIndex < formatter->m_workerCount &&
		formatter->m_conversionWork[workerIndex] != work)
		++workerIndex;
	if (workerIndex == formatter->m_workerCount)
		return;

	formatter->ConvertRowPairs(
		formatter->m_workerSource,
		formatter->m_workerDestinationY,
		formatter->m_workerDestinationUV,
		formatter->m_workerFirstPair[workerIndex],
		formatter->m_workerPairCount[workerIndex]);
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
