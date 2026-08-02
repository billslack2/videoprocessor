/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, version 3.
 */

#pragma once

#include <Windows.h>

#include <video_frame_formatter/IVideoFrameFormatter.h>


/** Converts DeckLink r210, R10b, R10l, or R12L packed RGB to 10-bit P010. */
class CDeckLinkRGBToP010VideoFrameFormatter : public IVideoFrameFormatter
{
public:
	CDeckLinkRGBToP010VideoFrameFormatter();
	~CDeckLinkRGBToP010VideoFrameFormatter() override;

	CDeckLinkRGBToP010VideoFrameFormatter(const CDeckLinkRGBToP010VideoFrameFormatter&) = delete;
	CDeckLinkRGBToP010VideoFrameFormatter& operator=(const CDeckLinkRGBToP010VideoFrameFormatter&) = delete;

	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;
	void GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const override;

private:
	struct RGB10
	{
		uint16_t r;
		uint16_t g;
		uint16_t b;
	};

	static constexpr size_t PERFORMANCE_WINDOW_SIZE = 600;
	// Five reusable workers plus the delivery thread keep 4K60 conversion below one
	// frame period without creating or destroying threads in the hot path.
	static constexpr uint32_t MAX_WORKERS = 5;

	VideoFrameEncoding m_encoding = VideoFrameEncoding::UNKNOWN;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_inputStride = 0;
	LONG m_outFrameSize = 0;
	bool m_useBT2020 = false;
	double m_conversionTimes[PERFORMANCE_WINDOW_SIZE] = {};
	size_t m_conversionTimeIndex = 0;
	size_t m_conversionTimeCount = 0;
	double m_lastConversionTimeUs = 0.0;

	PTP_WORK m_conversionWork[MAX_WORKERS] = {};
	uint32_t m_workerCount = 0;
	const uint8_t* m_workerSourceFrame = nullptr;
	uint16_t* m_workerDestinationY = nullptr;
	uint16_t* m_workerDestinationUV = nullptr;
	uint32_t m_workerFirstPair[MAX_WORKERS] = {};
	uint32_t m_workerPairCount[MAX_WORKERS] = {};

	void AddPerformanceSample(double timeUs);
	void ConvertRowPairs(const uint8_t* sourceFrame, uint16_t* destinationY,
		uint16_t* destinationUV, uint32_t firstPair, uint32_t pairCount) const;
	void ReadPixelPair(const uint8_t* source, RGB10& first, RGB10& second) const noexcept;
	static void CALLBACK ConversionWorkCallback(
		PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work);
};
