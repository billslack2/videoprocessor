/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <vector>


 /**
  * Video frame formatter which reads V210 and write to P010
  * (that's YUV422 to YUV420 both in 10 bit, all assuming this is running on little endian hardware)
  */
class CV210toP010VideoFrameFormatter:
	public IVideoFrameFormatter
{
public:

	virtual ~CV210toP010VideoFrameFormatter() {}

	// IVideoFrameFormatter
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;

private:
	uint32_t m_height = 0;
	uint32_t m_width = 0;
	uint32_t m_alignedWidth;
	uint32_t m_stride;
	bool m_special720 = false;

	// Pre-allocated buffers to avoid per-frame allocation
	std::vector<uint16_t> m_tempY;
	std::vector<uint16_t> m_tempUV;

	// Performance tracking and optimization features
#ifdef _DEBUG
	mutable uint64_t m_totalConversions = 0;
	mutable uint64_t m_totalConversionTimeUs = 0;
	mutable uint64_t m_simdConversions = 0;        // Track SIMD usage
	mutable uint64_t m_scalarConversions = 0;      // Track scalar fallbacks
	mutable uint64_t m_avx2ConversionTimeUs = 0;   // Track AVX2 performance
	mutable uint64_t m_scalarConversionTimeUs = 0; // Track scalar performance
#endif

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

	// CPU feature caching for performance
	mutable bool m_cpuFeaturesChecked = false;
	mutable bool m_hasAVX2 = false;              // For V210 SIMD (disabled)
	mutable bool m_hasAVX2MemoryOps = false;     // For safe memory operations
	
	// Performance optimization methods
	bool CheckCPUFeatures() const;
	bool HasAVX2MemoryOps() const;               // Safe memory operations check
	void LogPerformanceStats() const;
	
public:
	// Public method to get conversion performance metrics
	void GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const
	{
		currentUs = m_performanceWindow.lastTimeUs;
		avg10s = m_performanceWindow.GetAverage();
		max10s = m_performanceWindow.GetMax();
	}
	
private:
	// ?? COMPILER-FRIENDLY: Cleaner conversion methods (optimized for /O2 /Ob2 /Oi /Ot)
	bool ConvertV210ToP010(const uint8_t* srcData, uint32_t srcStride, 
	                      uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	bool ConvertV210ToP010_720p(const uint8_t* srcData, uint32_t srcStride,
	                           uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	bool ConvertV210ToP010_Standard(const uint8_t* srcData, uint32_t srcStride,
	                               uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	bool ConvertV210ToP010_SIMD(const uint8_t* srcData, uint32_t srcStride,
	                           uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	void LogConversionPerformance(uint64_t conversionTimeUs, bool success) const;
};
