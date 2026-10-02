/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

// AVX2-only kernels. Call only after CpuFeatures::SupportsAvx2Kernels().
// Compiled without /GL to preserve the baseline/AVX2 boundary.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <immintrin.h>
#include "CARGBtoP010VideoFrameFormatter.h"
#include "CARGBtoP010VideoFrameFormatter.Common.h"

#if !defined(__AVX2__)
#error This file requires /arch:AVX2
#endif

namespace
{
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
