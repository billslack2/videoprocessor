/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <Windows.h>

#include <video_frame_formatter/IVideoFrameFormatter.h>

/**
 * Video frame formatter which reads ARGB/BGRA (8-bit 4:4:4:4 RGB) and writes to P010 (10-bit 4:2:0 YUV planar)
 * 
 * Performs RGB to YUV color space conversion using BT.709 or BT.2020 matrix (based on colorspace),
 * then downsamples chroma from 4:4:4 to 4:2:0 by averaging 2x2 pixel blocks.
 * 
 * Supports both ARGB (A R G B byte order) and BGRA (B G R A byte order) input formats.
 */
class CARGBtoP010VideoFrameFormatter : public IVideoFrameFormatter
{
public:
	enum class ConversionMethod
	{
		AUTO,
		SCALAR,
		AVX2,
	};

	CARGBtoP010VideoFrameFormatter();
	~CARGBtoP010VideoFrameFormatter() override;
	CARGBtoP010VideoFrameFormatter(const CARGBtoP010VideoFrameFormatter&) = delete;
	CARGBtoP010VideoFrameFormatter& operator=(const CARGBtoP010VideoFrameFormatter&) = delete;

	// IVideoFrameFormatter
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;
	VideoFrameFormatterOutputContract GetOutputContract() const override
	{
		return { VideoFrameSampleRange::FULL, 10, 6 };
	}
	
	// Performance metrics
	void GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const override
	{
		currentUs = m_performanceWindow.lastTimeUs;
		avg10s = m_performanceWindow.GetAverage();
		max10s = m_performanceWindow.GetMax();
	}
	void SetConversionMethod(ConversionMethod method) { m_conversionMethod = method; }

private:
	uint32_t m_height = 0;
	uint32_t m_width = 0;
	uint32_t m_srcStride = 0;
	bool m_isBGRA = false;  // true for BGRA, false for ARGB
	bool m_hasAVX2 = false;
	ConversionMethod m_conversionMethod = ConversionMethod::AUTO;
	
	// BT.709 vs BT.2020 selection based on colorspace
	bool m_useBT2020 = false;
	
	// Rolling window performance tracking
	struct RollingPerformanceWindow
	{
		static const size_t WINDOW_SIZE = 600;  // 10 seconds @ 60fps
		double times[WINDOW_SIZE] = {0};
		size_t currentIndex = 0;
		size_t samplesCollected = 0;
		double lastTimeUs = 0.0;
		
		void AddSample(double timeUs)
		{
			lastTimeUs = timeUs;
			times[currentIndex] = timeUs;
			currentIndex = (currentIndex + 1) % WINDOW_SIZE;
			if (samplesCollected < WINDOW_SIZE)
				samplesCollected++;
		}
		
		double GetAverage() const
		{
			if (samplesCollected == 0) return 0.0;
			double sum = 0.0;
			for (size_t i = 0; i < samplesCollected; i++)
				sum += times[i];
			return sum / samplesCollected;
		}
		
		double GetMax() const
		{
			if (samplesCollected == 0) return 0.0;
			double maxVal = times[0];
			for (size_t i = 1; i < samplesCollected; i++)
				if (times[i] > maxVal)
					maxVal = times[i];
			return maxVal;
		}
	};
	
	mutable RollingPerformanceWindow m_performanceWindow;
	static constexpr uint32_t MAX_WORKERS = 5;
	PTP_WORK m_conversionWork[MAX_WORKERS] = {};
	uint32_t m_workerCount = 0;
	const uint8_t* m_workerSource = nullptr;
	uint16_t* m_workerDestinationY = nullptr;
	uint16_t* m_workerDestinationUV = nullptr;
	uint32_t m_workerFirstPair[MAX_WORKERS] = {};
	uint32_t m_workerPairCount[MAX_WORKERS] = {};

	void ConvertScalar(const uint8_t* source, uint16_t* destinationY,
		uint16_t* destinationUV, uint32_t firstPair, uint32_t pairCount) const;
	void ConvertAVX2(const uint8_t* source, uint16_t* destinationY,
		uint16_t* destinationUV, uint32_t firstPair, uint32_t pairCount) const;
	void ConvertRowPairs(const uint8_t* source, uint16_t* destinationY,
		uint16_t* destinationUV, uint32_t firstPair, uint32_t pairCount) const;
	static void CALLBACK ConversionWorkCallback(
		PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work);
};
