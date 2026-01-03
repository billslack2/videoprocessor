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
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>


 /**
  * Video frame formatter which reads V210 and write to P010
  * (that's YUV422 to YUV420 both in 10 bit, all assuming this is running on little endian hardware)
  */
class CV210toP010VideoFrameFormatter:
	public IVideoFrameFormatter
{
public:

	CV210toP010VideoFrameFormatter();
	virtual ~CV210toP010VideoFrameFormatter();

	// Conversion method enumeration for high-res (non-720p) paths
	enum class ConversionMethod
	{
		AUTO,           // Automatically select based on CPU features and frame size
		SIMD,           // AVX2 SIMD with threading support
		OPTIMIZED,      // Optimized scalar implementation
		STANDARD,       // Standard scalar implementation (baseline)
	};

	// IVideoFrameFormatter
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;

	// Configuration methods for conversion behavior
	void SetConversionMethod(ConversionMethod method) { m_conversionMethod = method; }
	ConversionMethod GetConversionMethod() const { return m_conversionMethod; }
	
	void SetMinCoreCount(uint32_t minCores) { m_minCoreCount = std::max(1u, minCores); }
	uint32_t GetMinCoreCount() const { return m_minCoreCount; }
	
	void SetMaxCoreCount(uint32_t maxCores) { m_maxCoreCount = std::max(m_minCoreCount, maxCores); }
	uint32_t GetMaxCoreCount() const { return m_maxCoreCount; }

	// Configuration file loading
	void LoadConfigurationFile();

private:
	uint32_t m_height = 0;
	uint32_t m_width = 0;
	uint32_t m_alignedWidth;
	uint32_t m_stride;
	bool m_special720 = false;

	// Configuration for conversion method and threading
	ConversionMethod m_conversionMethod = ConversionMethod::AUTO;
	uint32_t m_minCoreCount = 1;    // Minimum cores to use (default: 1)
	uint32_t m_maxCoreCount = 2;    // Maximum cores to use (default: 2, 0 = auto-detect)

	// Pre-allocated buffers to avoid per-frame allocation
	std::vector<uint16_t> m_tempY;
	std::vector<uint16_t> m_tempUV;

	// ========================================
	// Thread pool for parallel processing
	// Uses simple spin-wait pattern for low latency
	// Dynamically scales based on available CPU cores
	// ========================================
	uint32_t GetMaxThreadCount() const
	{
		uint32_t cores = std::thread::hardware_concurrency();
		if (cores == 0) cores = 4;  // Fallback

		// Apply configured limits
		uint32_t minCores = m_minCoreCount;
		uint32_t maxCores = m_maxCoreCount;
		
		// If maxCores is 0 (auto), leave 2 cores for OS/UI
		if (maxCores == 0)
		{
			maxCores = std::max(minCores, cores >= 2 ? cores - 2 : cores);
		}

		// Ensure min <= max and both are valid
		minCores = std::min(minCores, cores);
		maxCores = std::min(maxCores, cores);
		maxCores = std::max(maxCores, minCores);

		return std::min(8u, maxCores);  // Cap at 8 threads
	}
	static constexpr uint32_t MAX_THREADS = 8;  // Will be dynamically selected at runtime
	static constexpr uint32_t MIN_LINES_FOR_THREADING = 720;  // Enable threading for 720p and above
	
	struct ThreadWorkItem
	{
		const uint8_t* srcData = nullptr;
		uint32_t srcStride = 0;
		uint16_t* dstY = nullptr;
		uint16_t* dstUV = nullptr;
		uint32_t width = 0;
		uint32_t startLine = 0;
		uint32_t endLine = 0;
	};
	
	struct ThreadContext
	{
		std::thread thread;
		std::atomic<int> state{0};  // 0=idle, 1=working, 2=exit
		ThreadWorkItem work;
		
		ThreadContext() = default;
		~ThreadContext() = default;
		ThreadContext(const ThreadContext&) = delete;
		ThreadContext& operator=(const ThreadContext&) = delete;
		ThreadContext(ThreadContext&&) = delete;
		ThreadContext& operator=(ThreadContext&&) = delete;
	};
	
	std::unique_ptr<ThreadContext[]> m_threadContexts;
	bool m_threadsInitialized = false;
	
	void InitializeThreadPool();
	void ShutdownThreadPool();
	static void ThreadWorkerStatic(CV210toP010VideoFrameFormatter* self, uint32_t threadIndex);
	
	// Process a segment of lines (used by both main thread and worker threads)
	void ProcessLineSegment(
		const uint8_t* srcData, uint32_t srcStride,
		uint16_t* dstY, uint16_t* dstUV,
		uint32_t width, uint32_t startLine, uint32_t endLine) noexcept;

	// Performance tracking and optimization features
#ifdef _DEBUG
	mutable uint64_t m_totalConversions = 0;
	mutable uint64_t m_totalConversionTimeUs = 0;
	mutable uint64_t m_simdConversions = 0;
	mutable uint64_t m_scalarConversions = 0;
	mutable uint64_t m_avx2ConversionTimeUs = 0;
	mutable uint64_t m_scalarConversionTimeUs = 0;
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
	mutable bool m_hasAVX2 = false;
	mutable bool m_hasAVX2MemoryOps = false;
	mutable uint32_t m_actualMaxThreads = 0;
	
	// Performance optimization methods
	bool CheckCPUFeatures() const;
	bool HasAVX2MemoryOps() const;
	uint32_t GetActualMaxThreads() const;
	uint32_t GetPhysicalCoreCount() const;  // Get physical core count (ignoring E-cores)
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
	// Conversion methods
	bool ConvertV210ToP010(const uint8_t* srcData, uint32_t srcStride, 
	                      uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	bool ConvertV210ToP010_720p(const uint8_t* srcData, uint32_t srcStride,
	                           uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	bool ConvertV210ToP010_Standard(const uint8_t* srcData, uint32_t srcStride,
	                               uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	bool ConvertV210ToP010_Optimized(const uint8_t* srcData, uint32_t srcStride,
	                                  uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	bool ConvertV210ToP010_SIMD(const uint8_t* srcData, uint32_t srcStride,
	                           uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	bool ConvertV210ToP010_Threaded(const uint8_t* srcData, uint32_t srcStride,
	                               uint16_t* dstY, uint16_t* dstUV, uint32_t width, uint32_t height) noexcept;
	void LogConversionPerformance(uint64_t conversionTimeUs, bool success) const;

public:
	// Load configuration from a file
	bool LoadConfigurationFile(const char* filename);
	
private:
	// Configuration values
	uint32_t m_configuredMinCoreCount = 2;
	uint32_t m_configuredMaxCoreCount = 0;
	ConversionMethod m_configuredConversionMethod = ConversionMethod::AUTO;
	
	// Apply the current configuration settings
	void ApplyConfiguration();
};
