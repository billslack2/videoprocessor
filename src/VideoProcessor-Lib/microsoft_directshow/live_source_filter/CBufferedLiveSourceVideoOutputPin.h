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
 * This class borrows heavily from DirectShow CSourceStream.
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

	std::deque<VideoFrame> m_videoFrameQueue;
	std::atomic_bool m_isActive = false;

	CCritSec m_filterCritSec;

	// Enhanced state for CLOCK_SMART timing improvements
	mutable REFERENCE_TIME m_nextVideoFrameStartTime = REFERENCE_TIME_INVALID;
	mutable bool m_hasValidNextTimestamp = false;

	// Core proactive frame management
	HANDLE m_hFrameAvailableEvent = nullptr;  // Event signaled when frames are added to the queue
	HANDLE m_hShutdownEvent = nullptr;        // Event signaled when thread should exit
	
	// Essential metrics for proactive decisions (simplified)
	std::atomic<uint32_t> m_recentDeliveryFailures = 0;   // Simple failure counter (reset periodically)
	DWORD m_lastQueueWarning = 0;                         // Throttle warnings only
	
	// Thread function, upon return thread exist.
	// Return codes > 0 indicate an error occured
	DWORD ThreadProc();

	// Remove all items from the videoFrameQueue
	void PurgeQueue();

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
	};
	
	ProactiveQueueMetrics GetProactiveMetrics() const;
};
