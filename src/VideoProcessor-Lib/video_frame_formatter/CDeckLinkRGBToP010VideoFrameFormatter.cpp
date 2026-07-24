/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#include <pch.h>

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

	inline uint16_t Clamp10(int32_t value) noexcept
	{
		return static_cast<uint16_t>(value < 0 ? 0 : value > 1023 ? 1023 : value);
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
}


CDeckLinkRGBToP010VideoFrameFormatter::CDeckLinkRGBToP010VideoFrameFormatter()
{
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
	if (videoState->videoFrameEncoding != VideoFrameEncoding::R10b &&
		videoState->videoFrameEncoding != VideoFrameEncoding::R10l &&
		videoState->videoFrameEncoding != VideoFrameEncoding::R12L)
		throw std::runtime_error("Packed RGB to P010 conversion only supports R10b, R10l, or R12L");

	const auto width = videoState->displayMode->FrameWidth();
	const auto height = videoState->displayMode->FrameHeight();
	if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0)
		throw std::runtime_error("P010 conversion requires positive, even frame dimensions");
	if (videoState->videoFrameEncoding == VideoFrameEncoding::R12L && (width % 8) != 0)
		throw std::runtime_error("R12L frame width must be divisible by 8");

	const uint64_t outputSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 3ULL;
	if (outputSize > static_cast<uint64_t>(std::numeric_limits<LONG>::max()))
		throw std::runtime_error("P010 output frame is too large");

	m_encoding = videoState->videoFrameEncoding;
	m_width = static_cast<uint32_t>(width);
	m_height = static_cast<uint32_t>(height);
	m_inputStride = videoState->BytesPerRow();
	const uint32_t minimumStride = m_encoding == VideoFrameEncoding::R12L ?
		m_width * 36U / 8U : m_width * 4U;
	if (m_inputStride < minimumStride)
		throw std::runtime_error("Packed RGB input row is smaller than the frame width");
	m_outFrameSize = static_cast<LONG>(outputSize);
	m_useBT2020 = videoState->colorspace == ColorSpace::BT_2020;
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
	const uint8_t* source, RGB10& first, RGB10& second) const noexcept
{
	if (m_encoding == VideoFrameEncoding::R12L)
	{
		// SMPTE 268M Annex C method C4: two consecutive RGB pixels occupy nine bytes.
		first.r = static_cast<uint16_t>(source[0] | ((source[1] & 0x0F) << 8));
		first.g = static_cast<uint16_t>((source[1] >> 4) | (source[2] << 4));
		first.b = static_cast<uint16_t>(source[3] | ((source[4] & 0x0F) << 8));
		second.r = static_cast<uint16_t>((source[4] >> 4) | (source[5] << 4));
		second.g = static_cast<uint16_t>(source[6] | ((source[7] & 0x0F) << 8));
		second.b = static_cast<uint16_t>((source[7] >> 4) | (source[8] << 4));

		// Round 12-bit full-range components to the 10-bit domain used by P010.
		first.r = Clamp10((first.r + 2) >> 2);
		first.g = Clamp10((first.g + 2) >> 2);
		first.b = Clamp10((first.b + 2) >> 2);
		second.r = Clamp10((second.r + 2) >> 2);
		second.g = Clamp10((second.g + 2) >> 2);
		second.b = Clamp10((second.b + 2) >> 2);
		return;
	}

	const uint32_t firstWord = m_encoding == VideoFrameEncoding::R10l ?
		ReadLittleEndian32(source) : ReadBigEndian32(source);
	const uint32_t secondWord = m_encoding == VideoFrameEncoding::R10l ?
		ReadLittleEndian32(source + 4) : ReadBigEndian32(source + 4);
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
	const uint32_t bytesPerPixelPair = m_encoding == VideoFrameEncoding::R12L ? 9U : 8U;
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
			ReadPixelPair(source0, p00, p01);
			ReadPixelPair(source1, p10, p11);

			auto calculateY = [&coefficients](const RGB10& pixel) noexcept
			{
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
			const int32_t cb = ((coefficients.cbR * r + coefficients.cbG * g +
				coefficients.cbB * b + 32768) >> 16) + 512;
			const int32_t cr = ((coefficients.crR * r + coefficients.crG * g +
				coefficients.crB * b + 32768) >> 16) + 512;
			uv[0] = static_cast<uint16_t>(Clamp10(cb) << 6);
			uv[1] = static_cast<uint16_t>(Clamp10(cr) << 6);

			source0 += bytesPerPixelPair;
			source1 += bytesPerPixelPair;
			y0 += 2;
			y1 += 2;
			uv += 2;
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
