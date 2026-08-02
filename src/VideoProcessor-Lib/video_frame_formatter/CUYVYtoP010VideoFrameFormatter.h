/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <video_frame_formatter/IVideoFrameFormatter.h>

/**
 * Video frame formatter which reads UYVY (8-bit 4:2:2) and writes to P010 (10-bit 4:2:0 planar)
 * Upscales 8-bit to 10-bit by bit-shifting
 * Downsamples chroma from 4:2:2 to 4:2:0 by averaging vertical pairs
 * 
 * AVX2 optimization provides significant speedup by:
 * - Processing 32 pixels per iteration (vs 2 in scalar)
 * - Using SIMD for 8-bit to 16-bit expansion (_mm256_cvtepu8_epi16)
 * - Vectorized vertical chroma averaging
 */
class CUYVYtoP010VideoFrameFormatter : public IVideoFrameFormatter
{
public:
	CUYVYtoP010VideoFrameFormatter();
	virtual ~CUYVYtoP010VideoFrameFormatter() = default;

	// IVideoFrameFormatter
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;
	VideoFrameFormatterOutputContract GetOutputContract() const override
	{
		return { VideoFrameSampleRange::LIMITED, 8, 8 };
	}
	
	// Performance metrics (matches V210 interface)
	void GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const override
	{
		currentUs = m_performanceWindow.lastTimeUs;
		avg10s = m_performanceWindow.GetAverage();
		max10s = m_performanceWindow.GetMax();
	}

private:
	uint32_t m_height = 0;
	uint32_t m_width = 0;
	uint32_t m_srcStride = 0;  // Source stride from VideoState
	
	// CPU feature detection (cached)
	mutable bool m_cpuFeaturesChecked = false;
	mutable bool m_hasAVX2 = false;
	
	bool CheckCPUFeatures() const;
	
	// Conversion implementations
	void ConvertScalar(const uint8_t* src, uint16_t* dstY, uint16_t* dstUV) const;
	void ConvertAVX2(const uint8_t* src, uint16_t* dstY, uint16_t* dstUV) const;
	
	// Rolling window performance tracking (matches V210 pattern)
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
};
