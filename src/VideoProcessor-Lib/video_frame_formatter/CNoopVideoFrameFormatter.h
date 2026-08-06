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
  * Video frame formatter which simply does a direct copy
  */
class CNoopVideoFrameFormatter:
	public IVideoFrameFormatter
{
public:

	virtual ~CNoopVideoFrameFormatter() {}

	// IVideoFrameFormatter
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;
	VideoFrameFormatterOutputContract GetOutputContract() const override;

	// Public method to get conversion performance metrics
	void GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const
	{
		currentUs = m_performanceWindow.lastTimeUs;
		avg10s = m_performanceWindow.GetAverage();
		max10s = m_performanceWindow.GetMax();
	}

private:
	int m_bytesPerVideoFrame = 0;
	VideoFrameEncoding m_encoding = VideoFrameEncoding::UNKNOWN;

	// Rolling window performance tracking (for stats overlay)
	struct RollingPerformanceWindow
	{
		static const size_t WINDOW_SIZE = 600;  // 10 seconds @ 60fps
		double times[WINDOW_SIZE];
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
