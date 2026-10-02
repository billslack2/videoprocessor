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
#include "CUYVYtoP210VideoFrameFormatter.h"

#if !defined(__AVX2__)
#error This file requires /arch:AVX2
#endif

namespace
{
	inline __m128i GatherAlternatingBytes(__m256i packed,
		const __m256i& shuffle) noexcept
	{
		const __m256i selected = _mm256_shuffle_epi8(packed, shuffle);
		return _mm_unpacklo_epi64(_mm256_castsi256_si128(selected),
			_mm256_extracti128_si256(selected, 1));
	}
}

void CUYVYtoP210VideoFrameFormatter::ConvertAVX2(
	const uint8_t* sourceFrame, uint16_t* destinationY,
	uint16_t* destinationUV) const
{
	const __m256i yShuffle = _mm256_setr_epi8(
		1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1,
		1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1);
	const __m256i uvShuffle = _mm256_setr_epi8(
		0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1);
	const uint32_t vectorWidth = m_width & ~15U;

	for (uint32_t line = 0; line < m_height; ++line)
	{
		const uint8_t* source = sourceFrame + static_cast<size_t>(line) * m_sourceStride;
		uint16_t* y = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(line) * m_width;
		uint32_t x = 0;
		for (; x < vectorWidth; x += 16)
		{
			const __m256i packed = _mm256_loadu_si256(
				reinterpret_cast<const __m256i*>(source));
			const __m256i y16 = _mm256_slli_epi16(
				_mm256_cvtepu8_epi16(GatherAlternatingBytes(packed, yShuffle)), 8);
			const __m256i uv16 = _mm256_slli_epi16(
				_mm256_cvtepu8_epi16(GatherAlternatingBytes(packed, uvShuffle)), 8);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(y), y16);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(uv), uv16);
			source += 32;
			y += 16;
			uv += 16;
		}
		for (; x < m_width; x += 2)
		{
			*uv++ = static_cast<uint16_t>(*source++) << 8;
			*y++ = static_cast<uint16_t>(*source++) << 8;
			*uv++ = static_cast<uint16_t>(*source++) << 8;
			*y++ = static_cast<uint16_t>(*source++) << 8;
		}
	}
}
