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

#include "CR12BtoRGB48VideoFrameFormatter.h"


namespace
{
	constexpr uint32_t PIXELS_PER_BLOCK = 8;
	constexpr uint32_t BYTES_PER_BLOCK = 36;

	inline uint16_t Expand12To16(uint16_t value)
	{
		// Bit replication maps both endpoints exactly: 0x000 -> 0x0000, 0xFFF -> 0xFFFF.
		return static_cast<uint16_t>((value << 4) | (value >> 8));
	}

	inline uint8_t Byte(const uint8_t* source, uint32_t word, uint32_t byte)
	{
		return source[word * 4 + byte];
	}

	inline uint16_t LowNibble(const uint8_t* source, uint32_t word, uint32_t byte)
	{
		return static_cast<uint16_t>(Byte(source, word, byte) & 0x0F);
	}

	inline uint16_t HighNibble(const uint8_t* source, uint32_t word, uint32_t byte)
	{
		return static_cast<uint16_t>(Byte(source, word, byte) >> 4);
	}

	inline void StoreRGB48(uint16_t*& destination, uint16_t red, uint16_t green, uint16_t blue)
	{
		*destination++ = Expand12To16(red);
		*destination++ = Expand12To16(green);
		*destination++ = Expand12To16(blue);
	}

	void ConvertBlockScalar(const uint8_t* source, uint16_t* destination)
	{
		// Mapping from the DeckLink SDK R12B table (eight pixels / nine 32-bit words).
		StoreRGB48(destination,
			Byte(source, 0, 3) | (LowNibble(source, 0, 2) << 8),
			HighNibble(source, 0, 2) | (static_cast<uint16_t>(Byte(source, 0, 1)) << 4),
			Byte(source, 0, 0) | (LowNibble(source, 1, 3) << 8));
		StoreRGB48(destination,
			HighNibble(source, 1, 3) | (static_cast<uint16_t>(Byte(source, 1, 2)) << 4),
			Byte(source, 1, 1) | (LowNibble(source, 1, 0) << 8),
			HighNibble(source, 1, 0) | (static_cast<uint16_t>(Byte(source, 2, 3)) << 4));
		StoreRGB48(destination,
			Byte(source, 2, 2) | (LowNibble(source, 2, 1) << 8),
			HighNibble(source, 2, 1) | (static_cast<uint16_t>(Byte(source, 2, 0)) << 4),
			Byte(source, 3, 3) | (LowNibble(source, 3, 2) << 8));
		StoreRGB48(destination,
			HighNibble(source, 3, 2) | (static_cast<uint16_t>(Byte(source, 3, 1)) << 4),
			Byte(source, 3, 0) | (LowNibble(source, 4, 3) << 8),
			HighNibble(source, 4, 3) | (static_cast<uint16_t>(Byte(source, 4, 2)) << 4));
		StoreRGB48(destination,
			Byte(source, 4, 1) | (LowNibble(source, 4, 0) << 8),
			HighNibble(source, 4, 0) | (static_cast<uint16_t>(Byte(source, 5, 3)) << 4),
			Byte(source, 5, 2) | (LowNibble(source, 5, 1) << 8));
		StoreRGB48(destination,
			HighNibble(source, 5, 1) | (static_cast<uint16_t>(Byte(source, 5, 0)) << 4),
			Byte(source, 6, 3) | (LowNibble(source, 6, 2) << 8),
			HighNibble(source, 6, 2) | (static_cast<uint16_t>(Byte(source, 6, 1)) << 4));
		StoreRGB48(destination,
			Byte(source, 6, 0) | (LowNibble(source, 7, 3) << 8),
			HighNibble(source, 7, 3) | (static_cast<uint16_t>(Byte(source, 7, 2)) << 4),
			Byte(source, 7, 1) | (LowNibble(source, 7, 0) << 8));
		StoreRGB48(destination,
			HighNibble(source, 7, 0) | (static_cast<uint16_t>(Byte(source, 8, 3)) << 4),
			Byte(source, 8, 2) | (LowNibble(source, 8, 1) << 8),
			HighNibble(source, 8, 1) | (static_cast<uint16_t>(Byte(source, 8, 0)) << 4));
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


CR12BtoRGB48VideoFrameFormatter::CR12BtoRGB48VideoFrameFormatter()
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


CR12BtoRGB48VideoFrameFormatter::~CR12BtoRGB48VideoFrameFormatter()
{
	for (uint32_t i = 0; i < m_workerCount; ++i)
	{
		WaitForThreadpoolWorkCallbacks(m_conversionWork[i], TRUE);
		CloseThreadpoolWork(m_conversionWork[i]);
	}
}


void CR12BtoRGB48VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState || !videoState->displayMode)
		throw std::runtime_error("R12B conversion requires a valid video state and display mode");

	if (videoState->videoFrameEncoding != VideoFrameEncoding::R12B)
		throw std::runtime_error("R12B converter received a non-R12B video state");

	const auto width = videoState->displayMode->FrameWidth();
	const auto height = videoState->displayMode->FrameHeight();
	if (width <= 0 || height <= 0)
		throw std::runtime_error("R12B conversion requires positive frame dimensions");
	if ((width % PIXELS_PER_BLOCK) != 0)
		throw std::runtime_error("R12B frame width must be divisible by 8");

	const uint64_t outputSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 6ULL;
	if (outputSize > static_cast<uint64_t>(std::numeric_limits<LONG>::max()))
		throw std::runtime_error("R12B output frame is too large");

	m_width = static_cast<uint32_t>(width);
	m_height = static_cast<uint32_t>(height);
	const uint32_t minimumInputStride = m_width * BYTES_PER_BLOCK / PIXELS_PER_BLOCK;
	m_inputStride = videoState->BytesPerRow();
	if (m_inputStride < minimumInputStride)
		throw std::runtime_error("R12B input row is smaller than the packed frame width");
	m_outFrameSize = static_cast<LONG>(outputSize);
}


bool CR12BtoRGB48VideoFrameFormatter::FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer)
{
	if (m_outFrameSize <= 0)
		throw std::runtime_error("Call OnVideoState before converting R12B frames");
	if (!inFrame.GetData() || !outBuffer)
		throw std::runtime_error("R12B conversion requires valid input and output buffers");

	const auto startTime = GetWallClockTime();
	const auto* sourceFrame = static_cast<const uint8_t*>(inFrame.GetData());
	auto* destinationFrame = reinterpret_cast<uint16_t*>(outBuffer);

	// R12B conversion is bandwidth-heavy at 4K60.  Rows are independent, so split large
	// frames between the calling thread and up to two reusable Windows thread-pool work items.
	// Smaller frames stay single-threaded to avoid scheduling overhead.  If work-item
	// creation failed, the same single-threaded path remains available.
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

	const auto elapsedUs = static_cast<double>((GetWallClockTime() - startTime) / 10);
	AddPerformanceSample(elapsedUs);
	return true;
}


void CR12BtoRGB48VideoFrameFormatter::ConvertRows(
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
		if (m_hasAVX2 && m_conversionMethod != ConversionMethod::SCALAR)
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


void CALLBACK CR12BtoRGB48VideoFrameFormatter::ConversionWorkCallback(
	PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK work)
{
	auto* formatter = static_cast<CR12BtoRGB48VideoFrameFormatter*>(context);
	uint32_t workerIndex = 0;
	while (workerIndex < formatter->m_workerCount &&
		formatter->m_conversionWork[workerIndex] != work)
		++workerIndex;
	if (workerIndex == formatter->m_workerCount)
		return;

	formatter->ConvertRows(
		formatter->m_workerSourceFrame,
		formatter->m_workerDestinationFrame,
		formatter->m_workerFirstLine[workerIndex],
		formatter->m_workerLineCount[workerIndex]);
}


LONG CR12BtoRGB48VideoFrameFormatter::GetOutFrameSize() const
{
	if (m_outFrameSize <= 0)
		throw std::runtime_error("Call OnVideoState before querying the R12B output size");
	return m_outFrameSize;
}


void CR12BtoRGB48VideoFrameFormatter::GetConversionPerformance(
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


void CR12BtoRGB48VideoFrameFormatter::AddPerformanceSample(double timeUs)
{
	m_lastConversionTimeUs = timeUs;
	m_conversionTimes[m_conversionTimeIndex] = timeUs;
	m_conversionTimeIndex = (m_conversionTimeIndex + 1) % PERFORMANCE_WINDOW_SIZE;
	if (m_conversionTimeCount < PERFORMANCE_WINDOW_SIZE)
		++m_conversionTimeCount;
}
