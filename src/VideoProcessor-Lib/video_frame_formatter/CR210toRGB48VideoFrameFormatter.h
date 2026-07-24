/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#pragma once

#include <Windows.h>

#include <video_frame_formatter/IVideoFrameFormatter.h>


/** Converts Blackmagic r210 (packed big-endian 10-bit RGB) directly to packed RGB48LE. */
class CR210toRGB48VideoFrameFormatter : public IVideoFrameFormatter
{
public:
	CR210toRGB48VideoFrameFormatter();
	~CR210toRGB48VideoFrameFormatter() override;

	CR210toRGB48VideoFrameFormatter(const CR210toRGB48VideoFrameFormatter&) = delete;
	CR210toRGB48VideoFrameFormatter& operator=(const CR210toRGB48VideoFrameFormatter&) = delete;

	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;
	void GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const override;

private:
	static constexpr size_t PERFORMANCE_WINDOW_SIZE = 600;
	static constexpr uint32_t MAX_WORKERS = 2;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_inputStride = 0;
	LONG m_outFrameSize = 0;
	double m_conversionTimes[PERFORMANCE_WINDOW_SIZE] = {};
	size_t m_conversionTimeIndex = 0;
	size_t m_conversionTimeCount = 0;
	double m_lastConversionTimeUs = 0.0;

	PTP_WORK m_conversionWork[MAX_WORKERS] = {};
	uint32_t m_workerCount = 0;
	const uint8_t* m_workerSourceFrame = nullptr;
	uint16_t* m_workerDestinationFrame = nullptr;
	uint32_t m_workerFirstLine[MAX_WORKERS] = {};
	uint32_t m_workerLineCount[MAX_WORKERS] = {};

	void AddPerformanceSample(double timeUs);
	void ConvertRows(const uint8_t* sourceFrame, uint16_t* destinationFrame,
		uint32_t firstLine, uint32_t lineCount) const;
	static void CALLBACK ConversionWorkCallback(
		PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work);
};
