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
#include "CDeckLinkRGBToP010VideoFrameFormatter.h"
#include "CDeckLinkRGBToP010VideoFrameFormatter.Common.h"
using namespace DeckLinkP010Detail;

#if !defined(__AVX2__)
#error This file requires /arch:AVX2
#endif

namespace
{
	inline void DecodeR12BlockAVX2(const uint8_t* source, bool bigEndian,
		int32_t* red, int32_t* green, int32_t* blue) noexcept
	{
		// Both DeckLink R12 layouts carry the SMPTE 268M C4 byte stream.
		// R12B reverses each 32-bit word. Normalize a complete eight-pixel
		// block once instead of reconstructing four independently crossed
		// pixel pairs through ReadPixelPair.
		alignas(32) uint8_t logicalBytes[36];
		const uint8_t* bytes = source;
		if (bigEndian)
		{
			const __m256i reverseEachWord = _mm256_setr_epi8(
				3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
				3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
			_mm256_store_si256(reinterpret_cast<__m256i*>(logicalBytes),
				_mm256_shuffle_epi8(
					_mm256_loadu_si256(reinterpret_cast<const __m256i*>(source)),
					reverseEachWord));
			logicalBytes[32] = source[35];
			logicalBytes[33] = source[34];
			logicalBytes[34] = source[33];
			logicalBytes[35] = source[32];
			bytes = logicalBytes;
		}

		for (uint32_t pair = 0; pair < 4; ++pair)
		{
			const uint8_t* packed = bytes + pair * 9U;
			const uint32_t index = pair * 2U;
			red[index] = Scale12To10(static_cast<uint16_t>(
				packed[0] | ((packed[1] & 0x0F) << 8)));
			green[index] = Scale12To10(static_cast<uint16_t>(
				(packed[1] >> 4) | (packed[2] << 4)));
			blue[index] = Scale12To10(static_cast<uint16_t>(
				packed[3] | ((packed[4] & 0x0F) << 8)));
			red[index + 1] = Scale12To10(static_cast<uint16_t>(
				(packed[4] >> 4) | (packed[5] << 4)));
			green[index + 1] = Scale12To10(static_cast<uint16_t>(
				packed[6] | ((packed[7] & 0x0F) << 8)));
			blue[index + 1] = Scale12To10(static_cast<uint16_t>(
				(packed[7] >> 4) | (packed[8] << 4)));
		}
	}

	inline __m256i RoundQ20AVX2(__m256i value) noexcept
	{
		const __m256i sign = _mm256_srai_epi32(value, 31);
		const __m256i absolute = _mm256_abs_epi32(value);
		const __m256i roundedAbsolute = _mm256_srli_epi32(
			_mm256_add_epi32(absolute, _mm256_set1_epi32(1 << 19)), 20);
		return _mm256_sub_epi32(_mm256_xor_si256(roundedAbsolute, sign), sign);
	}

	inline __m256i ClampShift10AVX2(__m256i value) noexcept
	{
		value = _mm256_max_epi32(value, _mm256_setzero_si256());
		value = _mm256_min_epi32(value, _mm256_set1_epi32(1023));
		return _mm256_slli_epi32(value, 6);
	}

	inline __m256i LimitedMatrixAVX2(__m256i r, __m256i g, __m256i b,
		int32_t coefficientR, int32_t coefficientG, int32_t coefficientB,
		int32_t outputOffset) noexcept
	{
		__m256i value = _mm256_mullo_epi32(r, _mm256_set1_epi32(coefficientR));
		value = _mm256_add_epi32(value,
			_mm256_mullo_epi32(g, _mm256_set1_epi32(coefficientG)));
		value = _mm256_add_epi32(value,
			_mm256_mullo_epi32(b, _mm256_set1_epi32(coefficientB)));
		value = RoundQ20AVX2(value);
		return ClampShift10AVX2(_mm256_add_epi32(value,
			_mm256_set1_epi32(outputOffset)));
	}

	inline __m256i FullRangeMatrixAVX2(__m256i r, __m256i g, __m256i b,
		int32_t coefficientR, int32_t coefficientG, int32_t coefficientB,
		int32_t outputOffset) noexcept
	{
		__m256i value = _mm256_mullo_epi32(r, _mm256_set1_epi32(coefficientR));
		value = _mm256_add_epi32(value,
			_mm256_mullo_epi32(g, _mm256_set1_epi32(coefficientG)));
		value = _mm256_add_epi32(value,
			_mm256_mullo_epi32(b, _mm256_set1_epi32(coefficientB)));
		value = _mm256_srai_epi32(
			_mm256_add_epi32(value, _mm256_set1_epi32(32768)), 16);
		return ClampShift10AVX2(_mm256_add_epi32(value,
			_mm256_set1_epi32(outputOffset)));
	}

	inline __m256i LoadPacked10WordsAVX2(const uint8_t* source,
		bool littleEndian) noexcept
	{
		__m256i words = _mm256_loadu_si256(
			reinterpret_cast<const __m256i*>(source));
		if (!littleEndian)
		{
			const __m256i byteSwap = _mm256_setr_epi8(
				3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
				3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
			words = _mm256_shuffle_epi8(words, byteSwap);
		}
		return words;
	}

	inline void ExtractPacked10RGBAVX2(__m256i words, bool r210,
		__m256i& r, __m256i& g, __m256i& b) noexcept
	{
		const __m256i mask = _mm256_set1_epi32(0x3ff);
		if (r210)
		{
			r = _mm256_and_si256(_mm256_srli_epi32(words, 20), mask);
			g = _mm256_and_si256(_mm256_srli_epi32(words, 10), mask);
			b = _mm256_and_si256(words, mask);
		}
		else
		{
			r = _mm256_and_si256(_mm256_srli_epi32(words, 22), mask);
			g = _mm256_and_si256(_mm256_srli_epi32(words, 12), mask);
			b = _mm256_and_si256(_mm256_srli_epi32(words, 2), mask);
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

void CDeckLinkRGBToP010VideoFrameFormatter::ConvertLimited10RowPairsAVX2(
	const uint8_t* sourceFrame, uint16_t* destinationY, uint16_t* destinationUV,
	uint32_t firstPair, uint32_t pairCount) const
{
	const LimitedRGBToYuvCoefficients& coefficients = m_useBT2020 ?
		(m_encoding == VideoFrameEncoding::R210 ? BT2020_R210 : BT2020_R10) :
		(m_encoding == VideoFrameEncoding::R210 ? BT709_R210 : BT709_R10);
	const bool r210 = m_encoding == VideoFrameEncoding::R210;
	const bool littleEndian = m_encoding == VideoFrameEncoding::R10l;
	const __m256i inputOffset = _mm256_set1_epi32(64);
	const __m256i swapAdjacent = _mm256_setr_epi32(1, 0, 3, 2, 5, 4, 7, 6);
	const __m256i selectEven = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
	const uint32_t vectorWidth = m_width & ~7U;
	const uint32_t endPair = firstPair + pairCount;

	for (uint32_t pair = firstPair; pair < endPair; ++pair)
	{
		const uint32_t line = pair * 2;
		const uint8_t* source0 = sourceFrame + static_cast<size_t>(line) * m_inputStride;
		const uint8_t* source1 = source0 + m_inputStride;
		uint16_t* y0 = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* y1 = y0 + m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(pair) * m_width;

		uint32_t x = 0;
		for (; x < vectorWidth; x += 8)
		{
			const __m256i words0 = LoadPacked10WordsAVX2(source0 + x * 4U,
				littleEndian);
			const __m256i words1 = LoadPacked10WordsAVX2(source1 + x * 4U,
				littleEndian);
			__m256i r0, g0, b0, r1, g1, b1;
			ExtractPacked10RGBAVX2(words0, r210, r0, g0, b0);
			ExtractPacked10RGBAVX2(words1, r210, r1, g1, b1);

			const __m256i yValues0 = LimitedMatrixAVX2(
				_mm256_sub_epi32(r0, inputOffset),
				_mm256_sub_epi32(g0, inputOffset),
				_mm256_sub_epi32(b0, inputOffset),
				coefficients.yR, coefficients.yG, coefficients.yB, 64);
			const __m256i yValues1 = LimitedMatrixAVX2(
				_mm256_sub_epi32(r1, inputOffset),
				_mm256_sub_epi32(g1, inputOffset),
				_mm256_sub_epi32(b1, inputOffset),
				coefficients.yR, coefficients.yG, coefficients.yB, 64);
			StoreEightP010Samples(y0 + x, yValues0);
			StoreEightP010Samples(y1 + x, yValues1);

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
			const __m256i averageR = _mm256_sub_epi32(
				averageTwoByTwo(r0, r1), inputOffset);
			const __m256i averageG = _mm256_sub_epi32(
				averageTwoByTwo(g0, g1), inputOffset);
			const __m256i averageB = _mm256_sub_epi32(
				averageTwoByTwo(b0, b1), inputOffset);
			const __m256i cb = _mm256_permutevar8x32_epi32(
				LimitedMatrixAVX2(averageR, averageG, averageB,
					coefficients.cbR, coefficients.cbG, coefficients.cbB, 512),
				selectEven);
			const __m256i cr = _mm256_permutevar8x32_epi32(
				LimitedMatrixAVX2(averageR, averageG, averageB,
					coefficients.crR, coefficients.crG, coefficients.crB, 512),
				selectEven);
			const __m128i cbLow = _mm256_castsi256_si128(cb);
			const __m128i crLow = _mm256_castsi256_si128(cr);
			const __m128i interleavedLow = _mm_unpacklo_epi32(cbLow, crLow);
			const __m128i interleavedHigh = _mm_unpackhi_epi32(cbLow, crLow);
			const __m128i packedChroma = _mm_packus_epi32(
				interleavedLow, interleavedHigh);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(uv + x), packedChroma);
		}

		for (; x < m_width; x += 2)
		{
			RGB10 p00, p01, p10, p11;
			ReadPixelPair(source0 + x * 4U, 0, p00, p01);
			ReadPixelPair(source1 + x * 4U, 0, p10, p11);
			auto calculateY = [&coefficients](const RGB10& pixel) noexcept
			{
				const int32_t r = static_cast<int32_t>(pixel.r) - 64;
				const int32_t g = static_cast<int32_t>(pixel.g) - 64;
				const int32_t b = static_cast<int32_t>(pixel.b) - 64;
				return Clamp10(64 + RoundQ20(
					static_cast<int64_t>(coefficients.yR) * r +
					static_cast<int64_t>(coefficients.yG) * g +
					static_cast<int64_t>(coefficients.yB) * b));
			};
			y0[x] = static_cast<uint16_t>(calculateY(p00) << 6);
			y0[x + 1] = static_cast<uint16_t>(calculateY(p01) << 6);
			y1[x] = static_cast<uint16_t>(calculateY(p10) << 6);
			y1[x + 1] = static_cast<uint16_t>(calculateY(p11) << 6);

			const int32_t r = ((p00.r + p01.r + p10.r + p11.r + 2) >> 2) - 64;
			const int32_t g = ((p00.g + p01.g + p10.g + p11.g + 2) >> 2) - 64;
			const int32_t b = ((p00.b + p01.b + p10.b + p11.b + 2) >> 2) - 64;
			const int32_t cb = 512 + RoundQ20(
				static_cast<int64_t>(coefficients.cbR) * r +
				static_cast<int64_t>(coefficients.cbG) * g +
				static_cast<int64_t>(coefficients.cbB) * b);
			const int32_t cr = 512 + RoundQ20(
				static_cast<int64_t>(coefficients.crR) * r +
				static_cast<int64_t>(coefficients.crG) * g +
				static_cast<int64_t>(coefficients.crB) * b);
			uv[x] = static_cast<uint16_t>(Clamp10(cb) << 6);
			uv[x + 1] = static_cast<uint16_t>(Clamp10(cr) << 6);
		}
	}
}

void CDeckLinkRGBToP010VideoFrameFormatter::ConvertR12RowPairsAVX2(
	const uint8_t* sourceFrame, uint16_t* destinationY, uint16_t* destinationUV,
	uint32_t firstPair, uint32_t pairCount) const
{
	const RGBToYuvCoefficients& coefficients = m_useBT2020 ? BT2020 : BT709;
	const bool bigEndian = m_encoding == VideoFrameEncoding::R12B;
	const __m256i swapAdjacent = _mm256_setr_epi32(1, 0, 3, 2, 5, 4, 7, 6);
	const __m256i selectEven = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
	const uint32_t endPair = firstPair + pairCount;

	for (uint32_t pair = firstPair; pair < endPair; ++pair)
	{
		const uint32_t line = pair * 2;
		const uint8_t* source0 = sourceFrame + static_cast<size_t>(line) * m_inputStride;
		const uint8_t* source1 = source0 + m_inputStride;
		uint16_t* y0 = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* y1 = y0 + m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(pair) * m_width;

		for (uint32_t x = 0; x < m_width; x += 8)
		{
			alignas(32) int32_t red0[8], green0[8], blue0[8];
			alignas(32) int32_t red1[8], green1[8], blue1[8];
			const size_t blockOffset = static_cast<size_t>(x / 8U) * 36U;
			DecodeR12BlockAVX2(source0 + blockOffset, bigEndian,
				red0, green0, blue0);
			DecodeR12BlockAVX2(source1 + blockOffset, bigEndian,
				red1, green1, blue1);

			const __m256i r0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(red0));
			const __m256i g0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(green0));
			const __m256i b0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(blue0));
			const __m256i r1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(red1));
			const __m256i g1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(green1));
			const __m256i b1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(blue1));
			StoreEightP010Samples(y0 + x, FullRangeMatrixAVX2(r0, g0, b0,
				coefficients.yR, coefficients.yG, coefficients.yB, 0));
			StoreEightP010Samples(y1 + x, FullRangeMatrixAVX2(r1, g1, b1,
				coefficients.yR, coefficients.yG, coefficients.yB, 0));

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
			const __m256i averageR = averageTwoByTwo(r0, r1);
			const __m256i averageG = averageTwoByTwo(g0, g1);
			const __m256i averageB = averageTwoByTwo(b0, b1);
			const __m256i cb = _mm256_permutevar8x32_epi32(
				FullRangeMatrixAVX2(averageR, averageG, averageB,
					coefficients.cbR, coefficients.cbG, coefficients.cbB, 512),
				selectEven);
			const __m256i cr = _mm256_permutevar8x32_epi32(
				FullRangeMatrixAVX2(averageR, averageG, averageB,
					coefficients.crR, coefficients.crG, coefficients.crB, 512),
				selectEven);
			const __m128i cbLow = _mm256_castsi256_si128(cb);
			const __m128i crLow = _mm256_castsi256_si128(cr);
			_mm_storeu_si128(reinterpret_cast<__m128i*>(uv + x),
				_mm_packus_epi32(_mm_unpacklo_epi32(cbLow, crLow),
					_mm_unpackhi_epi32(cbLow, crLow)));
		}
	}
}
