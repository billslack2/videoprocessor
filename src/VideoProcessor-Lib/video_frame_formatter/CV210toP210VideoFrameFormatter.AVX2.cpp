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
#include "CV210toP210VideoFrameFormatter.h"

#if !defined(__AVX2__)
#error This file requires /arch:AVX2
#endif

namespace
{
    constexpr uint32_t PIXELS_PER_PACK = 6;
    constexpr size_t BYTES_PER_PACK = 4 * sizeof(uint32_t);
	inline void StoreTwelveSamples(uint16_t* destination,
		__m256i firstEight, __m256i finalFour) noexcept
	{
		const __m128i first = _mm_packus_epi32(
			_mm256_castsi256_si128(firstEight),
			_mm256_extracti128_si256(firstEight, 1));
		const __m128i last = _mm_packus_epi32(
			_mm256_castsi256_si128(finalFour),
			_mm256_castsi256_si128(finalFour));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(destination), first);
		_mm_storel_epi64(reinterpret_cast<__m128i*>(destination + 8), last);
	}
}

void CV210toP210VideoFrameFormatter::ConvertAVX2(
	const uint8_t* sourceFrame, uint16_t* destinationY,
	uint16_t* destinationUV) const
{
	const __m256i mask = _mm256_set1_epi32(0x3ff);
	const __m256i yIndex0 = _mm256_setr_epi32(0, 1, 1, 2, 3, 3, 4, 5);
	const __m256i yShift0 = _mm256_setr_epi32(10, 0, 20, 10, 0, 20, 10, 0);
	const __m256i yIndex1 = _mm256_setr_epi32(5, 6, 7, 7, 0, 0, 0, 0);
	const __m256i yShift1 = _mm256_setr_epi32(20, 10, 0, 20, 0, 0, 0, 0);
	const __m256i uvIndex0 = _mm256_setr_epi32(0, 0, 1, 2, 2, 3, 4, 4);
	const __m256i uvShift0 = _mm256_setr_epi32(0, 20, 10, 0, 20, 10, 0, 20);
	const __m256i uvIndex1 = _mm256_setr_epi32(5, 6, 6, 7, 0, 0, 0, 0);
	const __m256i uvShift1 = _mm256_setr_epi32(10, 0, 20, 10, 0, 0, 0, 0);
	const uint32_t vectorWidth = m_width - (m_width % 12U);

	for (uint32_t line = 0; line < m_height; ++line)
	{
		const uint8_t* source = sourceFrame + static_cast<size_t>(line) * m_sourceStride;
		uint16_t* y = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(line) * m_width;
		uint32_t x = 0;
		for (; x < vectorWidth; x += 12)
		{
			const __m256i packed = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(source));
			__m256i yFirst = _mm256_and_si256(
				_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(packed, yIndex0),
					yShift0), mask);
			__m256i yLast = _mm256_and_si256(
				_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(packed, yIndex1),
					yShift1), mask);
			__m256i uvFirst = _mm256_and_si256(
				_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(packed, uvIndex0),
					uvShift0), mask);
			__m256i uvLast = _mm256_and_si256(
				_mm256_srlv_epi32(_mm256_permutevar8x32_epi32(packed, uvIndex1),
					uvShift1), mask);
			StoreTwelveSamples(y, _mm256_slli_epi32(yFirst, 6),
				_mm256_slli_epi32(yLast, 6));
			StoreTwelveSamples(uv, _mm256_slli_epi32(uvFirst, 6),
				_mm256_slli_epi32(uvLast, 6));
			source += 32;
			y += 12;
			uv += 12;
		}

		for (; x < m_width; x += PIXELS_PER_PACK)
		{
			const auto* words = reinterpret_cast<const uint32_t*>(source);
			const uint32_t word0 = words[0];
			const uint32_t word1 = words[1];
			const uint32_t word2 = words[2];
			const uint32_t word3 = words[3];
			const uint16_t luma[PIXELS_PER_PACK] = {
				static_cast<uint16_t>((word0 >> 10) & 0x3ff),
				static_cast<uint16_t>(word1 & 0x3ff),
				static_cast<uint16_t>((word1 >> 20) & 0x3ff),
				static_cast<uint16_t>((word2 >> 10) & 0x3ff),
				static_cast<uint16_t>(word3 & 0x3ff),
				static_cast<uint16_t>((word3 >> 20) & 0x3ff) };
			const uint16_t chroma[PIXELS_PER_PACK] = {
				static_cast<uint16_t>(word0 & 0x3ff),
				static_cast<uint16_t>((word0 >> 20) & 0x3ff),
				static_cast<uint16_t>((word1 >> 10) & 0x3ff),
				static_cast<uint16_t>(word2 & 0x3ff),
				static_cast<uint16_t>((word2 >> 20) & 0x3ff),
				static_cast<uint16_t>((word3 >> 10) & 0x3ff) };
			const uint32_t pixelCount = (std::min)(
				static_cast<uint32_t>(PIXELS_PER_PACK), m_width - x);
			for (uint32_t sample = 0; sample < pixelCount; ++sample)
			{
				*y++ = static_cast<uint16_t>(luma[sample] << 6);
				*uv++ = static_cast<uint16_t>(chroma[sample] << 6);
			}
			source += BYTES_PER_PACK;
		}
	}
}
