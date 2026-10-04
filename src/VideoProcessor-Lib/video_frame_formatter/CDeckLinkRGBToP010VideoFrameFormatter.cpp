/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#include <pch.h>
#include <VideoFrame.h>
#include <VideoState.h>

#include <array>
#include <CpuFeatures.h>
#include "CDeckLinkRGBToP010VideoFrameFormatter.Common.h"
using namespace DeckLinkP010Detail;

namespace DeckLinkP010Detail
{
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
}

#include <limits>

#include "CDeckLinkRGBToP010VideoFrameFormatter.h"


CDeckLinkRGBToP010VideoFrameFormatter::CDeckLinkRGBToP010VideoFrameFormatter()
{
	m_hasAVX2 = CpuFeatures::SupportsAvx2Kernels();
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
