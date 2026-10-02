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
#include "CR12BtoRGB48VideoFrameFormatter.h"
#include "CR12BtoRGB48VideoFrameFormatter.Common.h"

#if !defined(__AVX2__)
#error This file requires /arch:AVX2
#endif

namespace
{
	inline __m128i UnpackEightR12Components(const uint8_t* logical) noexcept
	{
		const __m128i packed = _mm_loadu_si128(
			reinterpret_cast<const __m128i*>(logical));
		const __m128i evenBytes = _mm_setr_epi8(
			0, 1, 3, 4, 6, 7, 9, 10,
			-1, -1, -1, -1, -1, -1, -1, -1);
		const __m128i oddBytes = _mm_setr_epi8(
			1, 2, 4, 5, 7, 8, 10, 11,
			-1, -1, -1, -1, -1, -1, -1, -1);
		const __m128i mask12 = _mm_set1_epi16(0x0fff);
		const __m128i even = _mm_and_si128(
			_mm_shuffle_epi8(packed, evenBytes), mask12);
		const __m128i odd = _mm_and_si128(_mm_srli_epi16(
			_mm_shuffle_epi8(packed, oddBytes), 4), mask12);
		return _mm_unpacklo_epi16(even, odd);
	}

	inline __m128i ExpandEightR12Components(__m128i values) noexcept
	{
		return _mm_or_si128(_mm_slli_epi16(values, 4),
			_mm_srli_epi16(values, 8));
	}

	void ConvertSixteenPixelsAVX2(const uint8_t* source,
		uint16_t* destination) noexcept
	{
		// Two documented R12B blocks contain 16 RGB pixels: 48 components in
		// 72 bytes. Normalize the per-32-bit-word byte order, then unpack the
		// continuous Method C4 stream in six groups of eight components.
		alignas(32) uint8_t logical[80] = {};
		const __m256i reverseEachWord = _mm256_setr_epi8(
			3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
			3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
		for (uint32_t offset = 0; offset < 64; offset += 32)
		{
			_mm256_store_si256(reinterpret_cast<__m256i*>(logical + offset),
				_mm256_shuffle_epi8(_mm256_loadu_si256(
					reinterpret_cast<const __m256i*>(source + offset)),
					reverseEachWord));
		}
		const __m128i reverseTwoWords = _mm_setr_epi8(
			3, 2, 1, 0, 7, 6, 5, 4,
			-1, -1, -1, -1, -1, -1, -1, -1);
		_mm_storel_epi64(reinterpret_cast<__m128i*>(logical + 64),
			_mm_shuffle_epi8(_mm_loadl_epi64(
				reinterpret_cast<const __m128i*>(source + 64)), reverseTwoWords));

		for (uint32_t group = 0; group < 6; group += 2)
		{
			const __m128i first = ExpandEightR12Components(
				UnpackEightR12Components(logical + group * 12U));
			const __m128i second = ExpandEightR12Components(
				UnpackEightR12Components(logical + (group + 1U) * 12U));
			__m256i output = _mm256_castsi128_si256(first);
			output = _mm256_inserti128_si256(output, second, 1);
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(
				destination + group * 8U), output);
		}
	}

}

void CR12BtoRGB48VideoFrameFormatter::ConvertRowsAVX2(
	const uint8_t* sourceFrame, uint16_t* destinationFrame,
	uint32_t firstLine, uint32_t lineCount) const
{
	const uint32_t blocksPerLine = m_width / PIXELS_PER_BLOCK;
	const uint32_t endLine = firstLine + lineCount;
	for (uint32_t line = firstLine; line < endLine; ++line)
	{
		const uint8_t* source = sourceFrame + static_cast<size_t>(line) * m_inputStride;
		uint16_t* destination = destinationFrame + static_cast<size_t>(line) * m_width * 3;
		uint32_t block = 0;

		{
			for (; block + 1 < blocksPerLine; block += 2)
			{
				ConvertSixteenPixelsAVX2(source, destination);
				source += BYTES_PER_BLOCK * 2U;
				destination += PIXELS_PER_BLOCK * 3U * 2U;
			}
		}
		for (; block < blocksPerLine; ++block)
		{
			ConvertBlockScalar(source, destination);
			source += BYTES_PER_BLOCK;
			destination += PIXELS_PER_BLOCK * 3;
		}
	}
}
