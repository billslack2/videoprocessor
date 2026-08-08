/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#include <pch.h>

#include <immintrin.h>
#include <intrin.h>
#include <limits>

#include "CR210toRGB48VideoFrameFormatter.h"


namespace
{
	inline uint16_t Align10To16(uint16_t value)
	{
		// Preserve documented limited-range codes and legal excursions in the
		// most-significant ten bits of the RGB48 component.
		return static_cast<uint16_t>(value << 6);
	}

	inline uint32_t ReadBigEndian32(const uint8_t* source)
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


CR210toRGB48VideoFrameFormatter::CR210toRGB48VideoFrameFormatter()
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


CR210toRGB48VideoFrameFormatter::~CR210toRGB48VideoFrameFormatter()
{
	for (uint32_t i = 0; i < m_workerCount; ++i)
	{
		WaitForThreadpoolWorkCallbacks(m_conversionWork[i], TRUE);
		CloseThreadpoolWork(m_conversionWork[i]);
	}
}


void CR210toRGB48VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState || !videoState->displayMode)
		throw std::runtime_error("R210 conversion requires a valid video state and display mode");
	if (videoState->videoFrameEncoding != VideoFrameEncoding::R210)
		throw std::runtime_error("R210 converter received a non-R210 video state");

	const auto width = videoState->displayMode->FrameWidth();
	const auto height = videoState->displayMode->FrameHeight();
	if (width <= 0 || height <= 0)
		throw std::runtime_error("R210 conversion requires positive frame dimensions");

	const uint64_t outputSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 6ULL;
	if (outputSize > static_cast<uint64_t>(std::numeric_limits<LONG>::max()))
		throw std::runtime_error("R210 output frame is too large");

	m_width = static_cast<uint32_t>(width);
	m_height = static_cast<uint32_t>(height);
	m_inputStride = videoState->BytesPerRow();
	if (m_inputStride < m_width * 4U)
		throw std::runtime_error("R210 input row is smaller than the packed frame width");
	m_outFrameSize = static_cast<LONG>(outputSize);
}


bool CR210toRGB48VideoFrameFormatter::FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer)
{
	if (m_outFrameSize <= 0)
		throw std::runtime_error("Call OnVideoState before converting R210 frames");
	if (!inFrame.GetData() || !outBuffer)
		throw std::runtime_error("R210 conversion requires valid input and output buffers");

	const auto startTime = GetWallClockTime();
	const auto* sourceFrame = static_cast<const uint8_t*>(inFrame.GetData());
	auto* destinationFrame = reinterpret_cast<uint16_t*>(outBuffer);

	if (m_workerCount > 0 && static_cast<uint64_t>(m_width) * m_height >= 1920ULL * 1080ULL)
	{
		m_workerSourceFrame = sourceFrame;
		m_workerDestinationFrame = destinationFrame;
		const uint32_t linesPerSegment = m_height / (m_workerCount + 1);
		uint32_t firstLine = 0;
		for (uint32_t i = 0; i < m_workerCount; ++i)
		{
			m_workerFirstLine[i] = firstLine;
			m_workerLineCount[i] = linesPerSegment;
			firstLine += linesPerSegment;
			SubmitThreadpoolWork(m_conversionWork[i]);
		}

		ConvertRows(sourceFrame, destinationFrame, firstLine, m_height - firstLine);
		for (uint32_t i = 0; i < m_workerCount; ++i)
			WaitForThreadpoolWorkCallbacks(m_conversionWork[i], FALSE);
	}
	else
		ConvertRows(sourceFrame, destinationFrame, 0, m_height);

	AddPerformanceSample(static_cast<double>((GetWallClockTime() - startTime) / 10));
	return true;
}


void CR210toRGB48VideoFrameFormatter::ConvertRows(
	const uint8_t* sourceFrame, uint16_t* destinationFrame,
	uint32_t firstLine, uint32_t lineCount) const
{
	const uint32_t endLine = firstLine + lineCount;
	for (uint32_t line = firstLine; line < endLine; ++line)
	{
		const uint8_t* source = sourceFrame + static_cast<size_t>(line) * m_inputStride;
		uint16_t* destination = destinationFrame + static_cast<size_t>(line) * m_width * 3;
		uint32_t pixel = 0;
		const bool useAVX2 = m_conversionMethod == ConversionMethod::AVX2 ||
			(m_conversionMethod == ConversionMethod::AUTO && m_hasAVX2);
		if (useAVX2)
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


void CALLBACK CR210toRGB48VideoFrameFormatter::ConversionWorkCallback(
	PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK work)
{
	auto* formatter = static_cast<CR210toRGB48VideoFrameFormatter*>(context);
	uint32_t workerIndex = 0;
	while (workerIndex < formatter->m_workerCount && formatter->m_conversionWork[workerIndex] != work)
		++workerIndex;
	if (workerIndex == formatter->m_workerCount)
		return;

	formatter->ConvertRows(
		formatter->m_workerSourceFrame,
		formatter->m_workerDestinationFrame,
		formatter->m_workerFirstLine[workerIndex],
		formatter->m_workerLineCount[workerIndex]);
}


LONG CR210toRGB48VideoFrameFormatter::GetOutFrameSize() const
{
	if (m_outFrameSize <= 0)
		throw std::runtime_error("Call OnVideoState before querying the R210 output size");
	return m_outFrameSize;
}


void CR210toRGB48VideoFrameFormatter::GetConversionPerformance(
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
		if (m_conversionTimes[i] > max10s)
			max10s = m_conversionTimes[i];
	}
	avg10s /= static_cast<double>(m_conversionTimeCount);
}


void CR210toRGB48VideoFrameFormatter::AddPerformanceSample(double timeUs)
{
	m_lastConversionTimeUs = timeUs;
	m_conversionTimes[m_conversionTimeIndex] = timeUs;
	m_conversionTimeIndex = (m_conversionTimeIndex + 1) % PERFORMANCE_WINDOW_SIZE;
	if (m_conversionTimeCount < PERFORMANCE_WINDOW_SIZE)
		++m_conversionTimeCount;
}
