/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#include <pch.h>

#include <array>
#include <immintrin.h>
#include <intrin.h>
#include <limits>

#include "CDeckLinkRGBToP010VideoFrameFormatter.h"


namespace
{
	struct RGBToYuvCoefficients
	{
		int32_t yR;
		int32_t yG;
		int32_t yB;
		int32_t cbR;
		int32_t cbG;
		int32_t cbB;
		int32_t crR;
		int32_t crG;
		int32_t crB;
	};

	struct LimitedRGBToYuvCoefficients
	{
		int32_t yR;
		int32_t yG;
		int32_t yB;
		int32_t cbR;
		int32_t cbG;
		int32_t cbB;
		int32_t crR;
		int32_t crG;
		int32_t crB;
	};

	// Coefficients use 16 fractional bits. Input and output are both 10-bit full-range values.
	constexpr RGBToYuvCoefficients BT709 = {
		13933, 46871, 4732,
		-7508, -25259, 32768,
		32768, -29763, -3005
	};
	constexpr RGBToYuvCoefficients BT2020 = {
		17218, 44444, 3886,
		-9147, -23621, 32768,
		32768, -30134, -2634
	};

	// Q20 coefficients map DeckLink's documented limited RGB intervals to
	// limited P010: Y 64-940 and Cb/Cr 64-960. r210 uses a distinct 64-960
	// input span; R10b/R10l use 64-940.
	constexpr LimitedRGBToYuvCoefficients BT709_R10 = {
		222927, 749942, 75707,
		-122880, -413378, 536258,
		536258, -487086, -49172
	};
	constexpr LimitedRGBToYuvCoefficients BT709_R210 = {
		217951, 733202, 74017,
		-120138, -404150, 524288,
		524288, -476214, -48074
	};
	constexpr LimitedRGBToYuvCoefficients BT2020_R10 = {
		275461, 710935, 62181,
		-149755, -386503, 536258,
		536258, -493128, -43130
	};
	constexpr LimitedRGBToYuvCoefficients BT2020_R210 = {
		269312, 695065, 60793,
		-146413, -377875, 524288,
		524288, -482120, -42168
	};
	// The largest absolute coefficient sum is 1,072,516. With the widest
	// possible post-offset input magnitude (959), every Q20 matrix sum fits in
	// signed 32-bit lanes, including legal out-of-nominal-range input codes.
	static_assert(1072516LL * 959LL < INT32_MAX,
		"Limited RGB AVX2 matrix requires wider intermediates");

	inline int32_t RoundQ20(int64_t value) noexcept
	{
		constexpr int64_t half = 1LL << 19;
		return value >= 0 ?
			static_cast<int32_t>((value + half) >> 20) :
			-static_cast<int32_t>(((-value) + half) >> 20);
	}

	inline uint16_t Clamp10(int32_t value) noexcept
	{
		return static_cast<uint16_t>(value < 0 ? 0 : value > 1023 ? 1023 : value);
	}

	std::array<uint16_t, 4096> BuildScale12To10Table()
	{
		std::array<uint16_t, 4096> result{};
		for (uint32_t value = 0; value < result.size(); ++value)
		{
			result[value] = static_cast<uint16_t>(
				(value * 1023U + 2047U) / 4095U);
		}
		return result;
	}

	const std::array<uint16_t, 4096> SCALE_12_TO_10 =
		BuildScale12To10Table();

	inline uint16_t Scale12To10(uint16_t value) noexcept
	{
		// The immutable 8 KiB table retains exact normalized round-to-nearest
		// while avoiding six constant divisions for every pair of R12 pixels.
		return SCALE_12_TO_10[value];
	}

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

	inline uint32_t ReadLittleEndian32(const uint8_t* source) noexcept
	{
		uint32_t value;
		memcpy(&value, source, sizeof(value));
		return value;
	}

	inline uint32_t ReadBigEndian32(const uint8_t* source) noexcept
	{
		return (static_cast<uint32_t>(source[0]) << 24) |
			(static_cast<uint32_t>(source[1]) << 16) |
			(static_cast<uint32_t>(source[2]) << 8) |
			static_cast<uint32_t>(source[3]);
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


CDeckLinkRGBToP010VideoFrameFormatter::CDeckLinkRGBToP010VideoFrameFormatter()
{
	m_hasAVX2 = CpuSupportsAVX2();
	for (uint32_t i = 0; i < MAX_WORKERS; ++i)
	{
		m_conversionWork[i] = CreateThreadpoolWork(ConversionWorkCallback, this, nullptr);
		if (!m_conversionWork[i])
			break;
		++m_workerCount;
	}
}


CDeckLinkRGBToP010VideoFrameFormatter::~CDeckLinkRGBToP010VideoFrameFormatter()
{
	for (uint32_t i = 0; i < m_workerCount; ++i)
	{
		WaitForThreadpoolWorkCallbacks(m_conversionWork[i], TRUE);
		CloseThreadpoolWork(m_conversionWork[i]);
	}
}


void CDeckLinkRGBToP010VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState || !videoState->displayMode)
		throw std::runtime_error("Packed RGB conversion requires a valid video state and display mode");
	if (videoState->videoFrameEncoding != VideoFrameEncoding::R210 &&
		videoState->videoFrameEncoding != VideoFrameEncoding::R10b &&
		videoState->videoFrameEncoding != VideoFrameEncoding::R10l &&
		videoState->videoFrameEncoding != VideoFrameEncoding::R12B &&
		videoState->videoFrameEncoding != VideoFrameEncoding::R12L)
		throw std::runtime_error("Packed RGB to P010 conversion only supports r210, R10b, R10l, R12B, or R12L");

	const auto width = videoState->displayMode->FrameWidth();
	const auto height = videoState->displayMode->FrameHeight();
	if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0)
		throw std::runtime_error("P010 conversion requires positive, even frame dimensions");
	if ((videoState->videoFrameEncoding == VideoFrameEncoding::R12B ||
		videoState->videoFrameEncoding == VideoFrameEncoding::R12L) &&
		(width % 8) != 0)
		throw std::runtime_error("R12B/R12L frame width must be divisible by 8");

	const uint64_t outputSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 3ULL;
	if (outputSize > static_cast<uint64_t>(std::numeric_limits<LONG>::max()))
		throw std::runtime_error("P010 output frame is too large");

	m_encoding = videoState->videoFrameEncoding;
	m_width = static_cast<uint32_t>(width);
	m_height = static_cast<uint32_t>(height);
	m_inputStride = videoState->BytesPerRow();
	const uint32_t minimumStride =
		(m_encoding == VideoFrameEncoding::R12B || m_encoding == VideoFrameEncoding::R12L) ?
		m_width * 36U / 8U : m_width * 4U;
	if (m_inputStride < minimumStride)
		throw std::runtime_error("Packed RGB input row is smaller than the frame width");
	m_outFrameSize = static_cast<LONG>(outputSize);
	m_useBT2020 = videoState->colorspace == ColorSpace::BT_2020;
}


VideoFrameFormatterOutputContract
CDeckLinkRGBToP010VideoFrameFormatter::GetOutputContract() const
{
	if (m_encoding == VideoFrameEncoding::R12B ||
		m_encoding == VideoFrameEncoding::R12L)
		return { VideoFrameSampleRange::FULL, 10, 6 };
	if (m_encoding == VideoFrameEncoding::R210 ||
		m_encoding == VideoFrameEncoding::R10b ||
		m_encoding == VideoFrameEncoding::R10l)
		return { VideoFrameSampleRange::LIMITED, 10, 6 };
	return {};
}


bool CDeckLinkRGBToP010VideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame, BYTE* outBuffer)
{
	if (m_outFrameSize <= 0)
		throw std::runtime_error("Call OnVideoState before converting packed RGB frames");
	if (!inFrame.GetData() || !outBuffer)
		throw std::runtime_error("Packed RGB conversion requires valid input and output buffers");

	const auto startTime = GetWallClockTime();
	const auto* sourceFrame = static_cast<const uint8_t*>(inFrame.GetData());
	auto* destinationY = reinterpret_cast<uint16_t*>(outBuffer);
	auto* destinationUV = destinationY + static_cast<size_t>(m_width) * m_height;
	const uint32_t totalPairs = m_height / 2;

	if (m_workerCount > 0 && static_cast<uint64_t>(m_width) * m_height >= 1920ULL * 1080ULL)
	{
		m_workerSourceFrame = sourceFrame;
		m_workerDestinationY = destinationY;
		m_workerDestinationUV = destinationUV;
		const uint32_t pairsPerSegment = totalPairs / (m_workerCount + 1);
		uint32_t firstPair = 0;
		for (uint32_t i = 0; i < m_workerCount; ++i)
		{
			m_workerFirstPair[i] = firstPair;
			m_workerPairCount[i] = pairsPerSegment;
			firstPair += pairsPerSegment;
			SubmitThreadpoolWork(m_conversionWork[i]);
		}

		ConvertRowPairs(sourceFrame, destinationY, destinationUV,
			firstPair, totalPairs - firstPair);
		for (uint32_t i = 0; i < m_workerCount; ++i)
			WaitForThreadpoolWorkCallbacks(m_conversionWork[i], FALSE);
	}
	else
		ConvertRowPairs(sourceFrame, destinationY, destinationUV, 0, totalPairs);

	AddPerformanceSample(static_cast<double>((GetWallClockTime() - startTime) / 10));
	return true;
}


void CDeckLinkRGBToP010VideoFrameFormatter::ReadPixelPair(
	const uint8_t* source, uint32_t pixelPairIndex,
	RGB10& first, RGB10& second) const noexcept
{
	if (m_encoding == VideoFrameEncoding::R12B || m_encoding == VideoFrameEncoding::R12L)
	{
		// SMPTE 268M Annex C method C4: two consecutive RGB pixels occupy
		// nine bytes. R12L stores the stream directly; R12B byte-swaps every
		// 32-bit word, including words crossed by the pair boundary.
		uint8_t byte0;
		uint8_t byte1;
		uint8_t byte2;
		uint8_t byte3;
		uint8_t byte4;
		uint8_t byte5;
		uint8_t byte6;
		uint8_t byte7;
		uint8_t byte8;
		if (m_encoding == VideoFrameEncoding::R12B)
		{
			const uint8_t* block = source +
				static_cast<size_t>(pixelPairIndex / 4U) * 36U;
			switch (pixelPairIndex & 3U)
			{
			case 0:
				byte0 = block[3]; byte1 = block[2]; byte2 = block[1];
				byte3 = block[0]; byte4 = block[7]; byte5 = block[6];
				byte6 = block[5]; byte7 = block[4]; byte8 = block[11];
				break;
			case 1:
				byte0 = block[10]; byte1 = block[9]; byte2 = block[8];
				byte3 = block[15]; byte4 = block[14]; byte5 = block[13];
				byte6 = block[12]; byte7 = block[19]; byte8 = block[18];
				break;
			case 2:
				byte0 = block[17]; byte1 = block[16]; byte2 = block[23];
				byte3 = block[22]; byte4 = block[21]; byte5 = block[20];
				byte6 = block[27]; byte7 = block[26]; byte8 = block[25];
				break;
			default:
				byte0 = block[24]; byte1 = block[31]; byte2 = block[30];
				byte3 = block[29]; byte4 = block[28]; byte5 = block[35];
				byte6 = block[34]; byte7 = block[33]; byte8 = block[32];
				break;
			}
		}
		else
		{
			byte0 = source[0]; byte1 = source[1]; byte2 = source[2];
			byte3 = source[3]; byte4 = source[4]; byte5 = source[5];
			byte6 = source[6]; byte7 = source[7]; byte8 = source[8];
		}
		first.r = static_cast<uint16_t>(byte0 | ((byte1 & 0x0F) << 8));
		first.g = static_cast<uint16_t>((byte1 >> 4) | (byte2 << 4));
		first.b = static_cast<uint16_t>(byte3 | ((byte4 & 0x0F) << 8));
		second.r = static_cast<uint16_t>((byte4 >> 4) | (byte5 << 4));
		second.g = static_cast<uint16_t>(byte6 | ((byte7 & 0x0F) << 8));
		second.b = static_cast<uint16_t>((byte7 >> 4) | (byte8 << 4));

		first.r = Scale12To10(first.r);
		first.g = Scale12To10(first.g);
		first.b = Scale12To10(first.b);
		second.r = Scale12To10(second.r);
		second.g = Scale12To10(second.g);
		second.b = Scale12To10(second.b);
		return;
	}

	const uint32_t firstWord = m_encoding == VideoFrameEncoding::R10l ?
		ReadLittleEndian32(source) : ReadBigEndian32(source);
	const uint32_t secondWord = m_encoding == VideoFrameEncoding::R10l ?
		ReadLittleEndian32(source + 4) : ReadBigEndian32(source + 4);
	if (m_encoding == VideoFrameEncoding::R210)
	{
		// r210 is [00 R10 G10 B10] in a big-endian 32-bit word.
		first.r = static_cast<uint16_t>((firstWord >> 20) & 0x3FF);
		first.g = static_cast<uint16_t>((firstWord >> 10) & 0x3FF);
		first.b = static_cast<uint16_t>(firstWord & 0x3FF);
		second.r = static_cast<uint16_t>((secondWord >> 20) & 0x3FF);
		second.g = static_cast<uint16_t>((secondWord >> 10) & 0x3FF);
		second.b = static_cast<uint16_t>(secondWord & 0x3FF);
		return;
	}

	first.r = static_cast<uint16_t>((firstWord >> 22) & 0x3FF);
	first.g = static_cast<uint16_t>((firstWord >> 12) & 0x3FF);
	first.b = static_cast<uint16_t>((firstWord >> 2) & 0x3FF);
	second.r = static_cast<uint16_t>((secondWord >> 22) & 0x3FF);
	second.g = static_cast<uint16_t>((secondWord >> 12) & 0x3FF);
	second.b = static_cast<uint16_t>((secondWord >> 2) & 0x3FF);
}


void CDeckLinkRGBToP010VideoFrameFormatter::ConvertRowPairs(
	const uint8_t* sourceFrame, uint16_t* destinationY, uint16_t* destinationUV,
	uint32_t firstPair, uint32_t pairCount) const
{
	const RGBToYuvCoefficients& coefficients = m_useBT2020 ? BT2020 : BT709;
	const bool limitedInput = m_encoding == VideoFrameEncoding::R210 ||
		m_encoding == VideoFrameEncoding::R10b ||
		m_encoding == VideoFrameEncoding::R10l;
	if (limitedInput && m_hasAVX2 &&
		m_conversionMethod != ConversionMethod::SCALAR)
	{
		ConvertLimited10RowPairsAVX2(sourceFrame, destinationY, destinationUV,
			firstPair, pairCount);
		return;
	}
	if (!limitedInput && m_hasAVX2 &&
		m_conversionMethod != ConversionMethod::SCALAR)
	{
		ConvertR12RowPairsAVX2(sourceFrame, destinationY, destinationUV,
			firstPair, pairCount);
		return;
	}
	const LimitedRGBToYuvCoefficients& limitedCoefficients = m_useBT2020 ?
		(m_encoding == VideoFrameEncoding::R210 ? BT2020_R210 : BT2020_R10) :
		(m_encoding == VideoFrameEncoding::R210 ? BT709_R210 : BT709_R10);
	const bool r12b = m_encoding == VideoFrameEncoding::R12B;
	const uint32_t bytesPerPixelPair =
		(m_encoding == VideoFrameEncoding::R12B || m_encoding == VideoFrameEncoding::R12L) ? 9U : 8U;
	const uint32_t endPair = firstPair + pairCount;

	for (uint32_t pair = firstPair; pair < endPair; ++pair)
	{
		const uint32_t line = pair * 2;
		const uint8_t* source0 = sourceFrame + static_cast<size_t>(line) * m_inputStride;
		const uint8_t* source1 = source0 + m_inputStride;
		uint16_t* y0 = destinationY + static_cast<size_t>(line) * m_width;
		uint16_t* y1 = y0 + m_width;
		uint16_t* uv = destinationUV + static_cast<size_t>(pair) * m_width;

		for (uint32_t x = 0; x < m_width; x += 2)
		{
			RGB10 p00, p01, p10, p11;
			ReadPixelPair(source0, x / 2U, p00, p01);
			ReadPixelPair(source1, x / 2U, p10, p11);

			auto calculateY = [&coefficients, &limitedCoefficients,
				limitedInput](const RGB10& pixel) noexcept
			{
				if (limitedInput)
				{
					const int32_t r = static_cast<int32_t>(pixel.r) - 64;
					const int32_t g = static_cast<int32_t>(pixel.g) - 64;
					const int32_t b = static_cast<int32_t>(pixel.b) - 64;
					return Clamp10(64 + RoundQ20(
						static_cast<int64_t>(limitedCoefficients.yR) * r +
						static_cast<int64_t>(limitedCoefficients.yG) * g +
						static_cast<int64_t>(limitedCoefficients.yB) * b));
				}
				return Clamp10((coefficients.yR * pixel.r + coefficients.yG * pixel.g +
					coefficients.yB * pixel.b + 32768) >> 16);
			};
			y0[0] = static_cast<uint16_t>(calculateY(p00) << 6);
			y0[1] = static_cast<uint16_t>(calculateY(p01) << 6);
			y1[0] = static_cast<uint16_t>(calculateY(p10) << 6);
			y1[1] = static_cast<uint16_t>(calculateY(p11) << 6);

			const int32_t r = (p00.r + p01.r + p10.r + p11.r + 2) >> 2;
			const int32_t g = (p00.g + p01.g + p10.g + p11.g + 2) >> 2;
			const int32_t b = (p00.b + p01.b + p10.b + p11.b + 2) >> 2;
			int32_t cb;
			int32_t cr;
			if (limitedInput)
			{
				const int32_t rOffset = r - 64;
				const int32_t gOffset = g - 64;
				const int32_t bOffset = b - 64;
				cb = 512 + RoundQ20(
					static_cast<int64_t>(limitedCoefficients.cbR) * rOffset +
					static_cast<int64_t>(limitedCoefficients.cbG) * gOffset +
					static_cast<int64_t>(limitedCoefficients.cbB) * bOffset);
				cr = 512 + RoundQ20(
					static_cast<int64_t>(limitedCoefficients.crR) * rOffset +
					static_cast<int64_t>(limitedCoefficients.crG) * gOffset +
					static_cast<int64_t>(limitedCoefficients.crB) * bOffset);
			}
			else
			{
				cb = ((coefficients.cbR * r + coefficients.cbG * g +
					coefficients.cbB * b + 32768) >> 16) + 512;
				cr = ((coefficients.crR * r + coefficients.crG * g +
					coefficients.crB * b + 32768) >> 16) + 512;
			}
			uv[0] = static_cast<uint16_t>(Clamp10(cb) << 6);
			uv[1] = static_cast<uint16_t>(Clamp10(cr) << 6);

			if (!r12b)
			{
				source0 += bytesPerPixelPair;
				source1 += bytesPerPixelPair;
			}
			y0 += 2;
			y1 += 2;
			uv += 2;
		}
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


void CALLBACK CDeckLinkRGBToP010VideoFrameFormatter::ConversionWorkCallback(
	PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK work)
{
	auto* formatter = static_cast<CDeckLinkRGBToP010VideoFrameFormatter*>(context);
	uint32_t workerIndex = 0;
	while (workerIndex < formatter->m_workerCount && formatter->m_conversionWork[workerIndex] != work)
		++workerIndex;
	if (workerIndex == formatter->m_workerCount)
		return;

	formatter->ConvertRowPairs(
		formatter->m_workerSourceFrame,
		formatter->m_workerDestinationY,
		formatter->m_workerDestinationUV,
		formatter->m_workerFirstPair[workerIndex],
		formatter->m_workerPairCount[workerIndex]);
}


LONG CDeckLinkRGBToP010VideoFrameFormatter::GetOutFrameSize() const
{
	if (m_outFrameSize <= 0)
		throw std::runtime_error("Call OnVideoState before querying the P010 output size");
	return m_outFrameSize;
}


void CDeckLinkRGBToP010VideoFrameFormatter::GetConversionPerformance(
	double& currentUs, double& avg10s, double& max10s) const
{
	currentUs = m_lastConversionTimeUs;
	avg10s = 0.0;
	max10s = 0.0;
	if (m_conversionTimeCount == 0)
		return;

	for (size_t i = 0; i < m_conversionTimeCount; ++i)
	{
		avg10s += m_conversionTimes[i];
		max10s = (std::max)(max10s, m_conversionTimes[i]);
	}
	avg10s /= static_cast<double>(m_conversionTimeCount);
}


void CDeckLinkRGBToP010VideoFrameFormatter::AddPerformanceSample(double timeUs)
{
	m_lastConversionTimeUs = timeUs;
	m_conversionTimes[m_conversionTimeIndex] = timeUs;
	m_conversionTimeIndex = (m_conversionTimeIndex + 1) % PERFORMANCE_WINDOW_SIZE;
	if (m_conversionTimeCount < PERFORMANCE_WINDOW_SIZE)
		++m_conversionTimeCount;
}
