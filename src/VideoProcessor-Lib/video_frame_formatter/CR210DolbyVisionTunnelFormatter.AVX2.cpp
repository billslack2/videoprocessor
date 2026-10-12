/*
 * Copyright(C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-only
 */
// AVX2-only kernels. Runtime dispatch stays in the baseline formatter object.
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
#include "CR210DolbyVisionTunnelFormatter.h"

#if !defined(__AVX2__)
#error This file requires /arch:AVX2
#endif

namespace
{
	inline __m128i PackEight(__m256i words) noexcept
	{
		return _mm_packus_epi32(_mm256_castsi256_si128(words),
			_mm256_extracti128_si256(words, 1));
	}

	template<unsigned Shift>
	void ConvertRowsImpl(const uint8_t* source, uint16_t* output,
		uint32_t width, uint32_t height, uint32_t stride,
		uint32_t firstRow, uint32_t rowCount) noexcept
	{
		const __m256i reverseEachWord = _mm256_setr_epi8(
			3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
			3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
		const __m256i highMask = _mm256_set1_epi32(0xFF00);
		const __m256i lowMask = _mm256_set1_epi32(0xF0);
		const size_t planePixels = static_cast<size_t>(width) * height;
		for (uint32_t row = firstRow; row < firstRow + rowCount; ++row)
		{
			const uint8_t* input = source + static_cast<size_t>(row) * stride;
			uint16_t* y = output + static_cast<size_t>(row) * width;
			uint16_t* uv = output + planePixels + static_cast<size_t>(row) * width;
			uint32_t x = 0;
			for (; x + 8 <= width; x += 8)
			{
				const __m256i p = _mm256_shuffle_epi8(
					_mm256_loadu_si256(reinterpret_cast<const __m256i*>(
						input + static_cast<size_t>(x) * 4)), reverseEachWord);
				const __m256i yHigh = _mm256_and_si256(
					_mm256_srli_epi32(p, 2 + Shift), highMask);
				const __m256i yLow = _mm256_and_si256(
					_mm256_slli_epi32(p, 4 - Shift), lowMask);
				const __m256i uvHigh = _mm256_and_si256(
					_mm256_srli_epi32(p, 12 + Shift), highMask);
				const __m256i uvLow = _mm256_and_si256(
					_mm256_srli_epi32(p, Shift), lowMask);
				_mm_storeu_si128(reinterpret_cast<__m128i*>(y + x),
					PackEight(_mm256_or_si256(yHigh, yLow)));
				_mm_storeu_si128(reinterpret_cast<__m128i*>(uv + x),
					PackEight(_mm256_or_si256(uvHigh, uvLow)));
			}
			for (; x < width; ++x)
			{
				const uint8_t* b = input + static_cast<size_t>(x) * 4;
				const uint32_t p =
					(static_cast<uint32_t>(b[0]) << 24) |
					(static_cast<uint32_t>(b[1]) << 16) |
					(static_cast<uint32_t>(b[2]) << 8) | b[3];
				y[x] = static_cast<uint16_t>(
					((p >> (2 + Shift)) & 0xFF00U) |
					((p << (4 - Shift)) & 0xF0U));
				uv[x] = static_cast<uint16_t>(
					((p >> (12 + Shift)) & 0xFF00U) |
					((p >> Shift) & 0xF0U));
			}
		}
		_mm256_zeroupper();
	}
}

void CR210DolbyVisionTunnelFormatter::ConvertRowsAVX2(
	const uint8_t* source, uint16_t* output, uint32_t firstRow,
	uint32_t rowCount, bool leftAligned) const
{
	if (leftAligned)
		ConvertRowsImpl<2>(source, output, m_width, m_height, m_stride,
			firstRow, rowCount);
	else
		ConvertRowsImpl<0>(source, output, m_width, m_height, m_stride,
			firstRow, rowCount);
}
