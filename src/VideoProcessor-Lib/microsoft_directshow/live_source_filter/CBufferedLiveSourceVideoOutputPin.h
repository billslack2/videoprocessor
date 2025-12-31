/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <deque>

#include <microsoft_directshow/DirectShowDefines.h>
#include "ALiveSourceVideoOutputPin.h"

#include "CLiveSource.h"


/**
 * This is an buffered output pin, any presented frame will be buffered first
 * and then a separate thread will deliver the buffers to the renderer.
 *
 * ASYNC CONVERSION ARCHITECTURE:
 * Raw frames ? Conversion Worker Thread ? Pre-Converted Samples ? Delivery Thread ? Renderer
 * 
 * This removes conversion time from the critical rendering path.
 */
class CBufferedLiveSourceVideoOutputPin:
	public ALiveSourceVideoOutputPin,
	public CAMThread
{
public:

	CBufferedLiveSourceVideoOutputPin(
		CLiveSource* filter,
		CCritSec* pLock,
		HRESULT* phr);
	virtual ~CBufferedLiveSourceVideoOutputPin();

	// CBaseOutputPin
	HRESULT Active() override;
	HRESULT Inactive() override;

	// ALiveSourceVideoOutputPin
	HRESULT OnVideoFrame(VideoFrame&) override;
	void SetFrameQueueMaxSize(size_t) override;
	size_t GetFrameQueueSize() override;
	void Reset() override;
	REFERENCE_TIME NextFrameTimestamp() const override;

private:

	size_t m_frameQueueMaxSize = 0;

	// Raw frame queue (input from capture device)
	std::deque<VideoFrame> m_videoFrameQueue;
	
	// Pre-converted sample queue (output from conversion worker)
	std::deque<IMediaSample*> m_convertedSampleQueue;
	CCritSec m_convertedQueueLock;
	
	std::atomic_bool m_isActive = false;

	CCritSec m_filterCritSec;

	// Core proactive frame management
	HANDLE m_hFrameAvailableEvent = nullptr;  // Event signaled when frames are added to the queue
	HANDLE m_hShutdownEvent = nullptr;        // Event signaled when thread should exit
	
	// Async conversion infrastructure
	HANDLE m_hConversionThread = nullptr;             // Conversion worker thread handle
	HANDLE m_hConversionShutdownEvent = nullptr;      // Event signaled when conversion thread should exit
	DWORD m_conversionThreadId = 0;                   // Conversion thread ID
	std::atomic<uint64_t> m_totalConversionTimeUs = 0;  // Total conversion time for metrics
	std::atomic<uint64_t> m_conversionFrameCount = 0;   // Number of frames converted
	
	// Essential metrics for proactive decisions (simplified)
	std::atomic<uint32_t> m_recentDeliveryFailures = 0;   // Simple failure counter (reset periodically)
	DWORD m_lastQueueWarning = 0;                         // Throttle warnings only
	
	// Thread function, upon return thread exist.
	// Return codes > 0 indicate an error occured
	DWORD ThreadProc();
	
	// Conversion worker thread function
	static DWORD WINAPI ConversionThreadProc(LPVOID lpParameter);
	DWORD ConversionWorker();

	// Remove all items from the videoFrameQueue
	void PurgeQueue();
	
	// Purge converted sample queue
	void PurgeConvertedQueue();

	// Calculate next frame timestamp with enhanced logic for CLOCK_SMART
	REFERENCE_TIME CalculateEnhancedNextTimestamp() const;
	
	// Simplified proactive frame management
	size_t GetProactiveQueueTarget() const;
	bool ShouldProactivelyDrop() const;

	// Simple health monitoring for proactive management
	struct ProactiveQueueMetrics
	{
		size_t currentSize;
		size_t maxSize;
		size_t proactiveTarget;
		uint64_t totalDropped;
		uint32_t recentFailures;
		bool isHealthy;
		
		// Async conversion metrics
		size_t convertedQueueSize;
		uint64_t avgConversionTimeUs;
	};
	
	ProactiveQueueMetrics GetProactiveMetrics() const;
};
