/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#include <pch.h>
#include <VideoFrame.h>
#include <VideoState.h>

#include <CpuFeatures.h>
#include "CR12BtoRGB48VideoFrameFormatter.Common.h"

#include <limits>

#include "CR12BtoRGB48VideoFrameFormatter.h"


CR12BtoRGB48VideoFrameFormatter::CR12BtoRGB48VideoFrameFormatter()
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
	if (m_hasAVX2 && m_conversionMethod != ConversionMethod::SCALAR)
	{
		ConvertRowsAVX2(sourceFrame, destinationFrame, firstLine, lineCount);
		return;
	}

	const uint32_t blocksPerLine = m_width / PIXELS_PER_BLOCK;
	const uint32_t endLine = firstLine + lineCount;
	for (uint32_t line = firstLine; line < endLine; ++line)
	{
		const uint8_t* source = sourceFrame + static_cast<size_t>(line) * m_inputStride;
		uint16_t* destination = destinationFrame + static_cast<size_t>(line) * m_width * 3;
		uint32_t block = 0;

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
