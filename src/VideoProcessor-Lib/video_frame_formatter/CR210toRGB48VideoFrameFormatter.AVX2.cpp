/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
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
#include "CR210toRGB48VideoFrameFormatter.h"
#include "CR210toRGB48VideoFrameFormatter.Common.h"

#if !defined(__AVX2__)
#error This file requires /arch:AVX2
#endif

namespace
{
	inline __m128i PackEightComponents(__m256i components) noexcept
	{
		return _mm_packus_epi32(_mm256_castsi256_si128(components),
			_mm256_extracti128_si256(components, 1));
	}

	inline __m128i ShuffleRGB(__m128i red, __m128i green, __m128i blue,
		__m128i redMask, __m128i greenMask, __m128i blueMask) noexcept
	{
		return _mm_or_si128(_mm_or_si128(
			_mm_shuffle_epi8(red, redMask),
			_mm_shuffle_epi8(green, greenMask)),
			_mm_shuffle_epi8(blue, blueMask));
	}

	void ConvertEightPixelsAVX2(const uint8_t* source,
		uint16_t* destination) noexcept
	{
		const __m256i reverseEachWord = _mm256_setr_epi8(
			3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
			3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
		const __m256i mask10 = _mm256_set1_epi32(0x3ff);
		const __m256i packed = _mm256_shuffle_epi8(
			_mm256_loadu_si256(reinterpret_cast<const __m256i*>(source)),
			reverseEachWord);
		const __m256i red = _mm256_slli_epi32(
			_mm256_and_si256(_mm256_srli_epi32(packed, 20), mask10), 6);
		const __m256i green = _mm256_slli_epi32(
			_mm256_and_si256(_mm256_srli_epi32(packed, 10), mask10), 6);
		const __m256i blue = _mm256_slli_epi32(
			_mm256_and_si256(packed, mask10), 6);

		// Narrow each channel once, then form the continuous RGB triplet stream
		// with byte shuffles. This preserves every 10-bit code exactly; no range
		// scaling or clipping is performed.
		const __m128i red16 = PackEightComponents(red);
		const __m128i green16 = PackEightComponents(green);
		const __m128i blue16 = PackEightComponents(blue);
		const char z = static_cast<char>(-1);
		const __m128i group0 = ShuffleRGB(red16, green16, blue16,
			_mm_setr_epi8(0, 1, z, z, z, z, 2, 3, z, z, z, z, 4, 5, z, z),
			_mm_setr_epi8(z, z, 0, 1, z, z, z, z, 2, 3, z, z, z, z, 4, 5),
			_mm_setr_epi8(z, z, z, z, 0, 1, z, z, z, z, 2, 3, z, z, z, z));
		const __m128i group1 = ShuffleRGB(red16, green16, blue16,
			_mm_setr_epi8(z, z, 6, 7, z, z, z, z, 8, 9, z, z, z, z, 10, 11),
			_mm_setr_epi8(z, z, z, z, 6, 7, z, z, z, z, 8, 9, z, z, z, z),
			_mm_setr_epi8(4, 5, z, z, z, z, 6, 7, z, z, z, z, 8, 9, z, z));
		const __m128i group2 = ShuffleRGB(red16, green16, blue16,
			_mm_setr_epi8(z, z, z, z, 12, 13, z, z, z, z, 14, 15, z, z, z, z),
			_mm_setr_epi8(10, 11, z, z, z, z, 12, 13, z, z, z, z, 14, 15, z, z),
			_mm_setr_epi8(z, z, 10, 11, z, z, z, z, 12, 13, z, z, z, z, 14, 15));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(destination), group0);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(destination + 8), group1);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(destination + 16), group2);
	}

}

void CR210toRGB48VideoFrameFormatter::ConvertRowsAVX2(
	const uint8_t* sourceFrame, uint16_t* destinationFrame,
	uint32_t firstLine, uint32_t lineCount) const
{
	const uint32_t endLine = firstLine + lineCount;
	for (uint32_t line = firstLine; line < endLine; ++line)
	{
		const uint8_t* source = sourceFrame + static_cast<size_t>(line) * m_inputStride;
		uint16_t* destination = destinationFrame + static_cast<size_t>(line) * m_width * 3;
		uint32_t pixel = 0;


		{
			for (; pixel + 8U <= m_width; pixel += 8U)
			{
				ConvertEightPixelsAVX2(source, destination);
				source += 32;
				destination += 24;
			}
		}
		for (; pixel < m_width; ++pixel)
		{
			const uint32_t packed = ReadBigEndian32(source);
			*destination++ = Align10To16(static_cast<uint16_t>((packed >> 20) & 0x3FF));
			*destination++ = Align10To16(static_cast<uint16_t>((packed >> 10) & 0x3FF));
			*destination++ = Align10To16(static_cast<uint16_t>(packed & 0x3FF));
			source += 4;
		}
	}
}
