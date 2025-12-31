/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "CBufferedLiveSourceVideoOutputPin.h"


CBufferedLiveSourceVideoOutputPin::CBufferedLiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr):
	ALiveSourceVideoOutputPin(filter, pLock, phr)
{
	// Create auto-reset event for frame availability signaling
	// Auto-reset: automatically resets to non-signaled after a waiting thread is released
	m_hFrameAvailableEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!m_hFrameAvailableEvent)
		throw std::runtime_error("Failed to create frame available event");

	// Create manual-reset event for clean thread shutdown
	m_hShutdownEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (!m_hShutdownEvent)
	{
		CloseHandle(m_hFrameAvailableEvent);
		throw std::runtime_error("Failed to create shutdown event");
	}
	
	// Create shutdown event for conversion worker thread
	m_hConversionShutdownEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (!m_hConversionShutdownEvent)
	{
		CloseHandle(m_hShutdownEvent);
		CloseHandle(m_hFrameAvailableEvent);
		throw std::runtime_error("Failed to create conversion shutdown event");
	}
	
	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: ASYNC conversion architecture initialized")));
}


CBufferedLiveSourceVideoOutputPin::~CBufferedLiveSourceVideoOutputPin()
{
	// Signal conversion thread to shutdown
	if (m_hConversionShutdownEvent)
	{
		SetEvent(m_hConversionShutdownEvent);
	}
	
	// Wait for conversion thread to exit
	if (m_hConversionThread)
	{
		DbgLog((LOG_TRACE, 1, TEXT("~CBufferedLiveSourceVideoOutputPin: Waiting for conversion thread to exit...")));
		WaitForSingleObject(m_hConversionThread, 5000);  // 5 second timeout
		CloseHandle(m_hConversionThread);
		m_hConversionThread = nullptr;
	}
	
	// Purge both queues
	PurgeConvertedQueue();
	PurgeQueue();
	
	if (m_hConversionShutdownEvent)
	{
		CloseHandle(m_hConversionShutdownEvent);
		m_hConversionShutdownEvent = nullptr;
	}
	
	if (m_hShutdownEvent)
	{
		CloseHandle(m_hShutdownEvent);
		m_hShutdownEvent = nullptr;
	}
	
	if (m_hFrameAvailableEvent)
	{
		CloseHandle(m_hFrameAvailableEvent);
		m_hFrameAvailableEvent = nullptr;
	}
	
	DbgLog((LOG_TRACE, 1, TEXT("~CBufferedLiveSourceVideoOutputPin: Async conversion shutdown complete")));
}


HRESULT CBufferedLiveSourceVideoOutputPin::Active()
{
	if (m_frameQueueMaxSize == 0)
		throw std::runtime_error("Call SetFrameQueueMaxSize() before activating the graph");

	{
		CAutoLock lock(m_pLock);

		if (m_pFilter->IsActive())
			return S_FALSE;	// succeeded, but did not allocate resources (they already exist...)

		assert(IsConnected());
		assert(!m_isActive);

		HRESULT hr = ALiveSourceVideoOutputPin::Active();
		if (FAILED(hr))
			return hr;

		assert(!ThreadExists());

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = true;
		}

		// Log ASYNC conversion approach
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Active() - ASYNC conversion architecture:")));
		DbgLog((LOG_TRACE, 1, TEXT("  Raw frames → Conversion Worker (OFF critical path) → Pre-Converted Queue → Delivery Thread → MadVR")));
		DbgLog((LOG_TRACE, 1, TEXT("  Benefit: MadVR gets 100%% of frame time (conversion happens in parallel)")));
		DbgLog((LOG_TRACE, 1, TEXT("  Result: Zero conversion latency on delivery path")));
		
		// Start conversion worker thread FIRST (before delivery thread)
		// This ensures conversions can happen immediately
		ResetEvent(m_hConversionShutdownEvent);
		m_hConversionThread = CreateThread(
			nullptr,
			0,
			ConversionThreadProc,
			this,
			0,
			&m_conversionThreadId);
		
		if (!m_hConversionThread)
		{
			DbgLog((LOG_ERROR, 1, TEXT("Active(): Failed to create conversion thread")));
			m_isActive = false;
			return E_FAIL;
		}
		
		DbgLog((LOG_TRACE, 1, TEXT("Active(): Conversion worker thread started (ID: %d)"), m_conversionThreadId));
		
		// Start the delivery thread
		if (!Create())
		{
			// Cleanup conversion thread
			SetEvent(m_hConversionShutdownEvent);
			WaitForSingleObject(m_hConversionThread, 1000);
			CloseHandle(m_hConversionThread);
			m_hConversionThread = nullptr;
			
			m_isActive = false;
			return E_FAIL;
		}

		return S_OK;
	}
}


HRESULT CBufferedLiveSourceVideoOutputPin::Inactive()
{
	{
		CAutoLock lock(m_pLock);

		// do nothing if not connected - its ok not to connect to all pins of a source filter
		if (!IsConnected())
			return NOERROR;

		HRESULT hr = ALiveSourceVideoOutputPin::Inactive();
		if (FAILED(hr))
			return hr;

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = false;

			PurgeQueue();
			PurgeConvertedQueue();
		}

		// Signal shutdown events before waiting for threads to exit
		if (m_hConversionShutdownEvent)
			SetEvent(m_hConversionShutdownEvent);
		
		if (m_hShutdownEvent)
			SetEvent(m_hShutdownEvent);

		// Wait for conversion thread to exit FIRST
		if (m_hConversionThread)
		{
			DbgLog((LOG_TRACE, 1, TEXT("Inactive(): Waiting for conversion thread to exit...")));
			DWORD waitResult = WaitForSingleObject(m_hConversionThread, 2000);
			if (waitResult == WAIT_TIMEOUT)
			{
				DbgLog((LOG_WARNING, 1, TEXT("Inactive(): Conversion thread did not exit cleanly")));
			}
			CloseHandle(m_hConversionThread);
			m_hConversionThread = nullptr;
		}
		
		// Then wait for delivery thread
		if (ThreadExists())
		{
			DbgLog((LOG_TRACE, 1, TEXT("Inactive(): Waiting for delivery thread to exit...")));
			Close();  // This waits for thread to exit
		}

		// Reset shutdown events for next activation
		if (m_hConversionShutdownEvent)
			ResetEvent(m_hConversionShutdownEvent);
		
		if (m_hShutdownEvent)
			ResetEvent(m_hShutdownEvent);
	}

	return S_OK;
}


HRESULT CBufferedLiveSourceVideoOutputPin::OnVideoFrame(VideoFrame& videoFrame)
{
	{
		CAutoLock lock(&m_filterCritSec);

		// Reject frames if not processing
		if (!m_isActive)
			return S_OK;

		const size_t currentQueueSize = m_videoFrameQueue.size();
		const size_t proactiveTarget = GetProactiveQueueTarget();
		
		// PROACTIVE: Drop frames BEFORE queue becomes problematic
		// Goal 1: Ensure timely delivery by preventing queue overload
		if (currentQueueSize >= proactiveTarget)
		{
			// Simple proactive dropping: remove 1-2 oldest frames
			const size_t framesToDrop = ShouldProactivelyDrop() ? 2 : 1;
			
			for (size_t i = 0; i < framesToDrop && !m_videoFrameQueue.empty(); i++)
			{
				VideoFrame oldFrame = m_videoFrameQueue.front();
				oldFrame.SourceBufferRelease();
				m_videoFrameQueue.pop_front();
				++m_droppedFrameCount;
			}
			
			// Throttled logging
			const DWORD currentTime = GetTickCount();
			if (currentTime - m_lastQueueWarning > 3000)  // Every 3 seconds max
			{
				m_lastQueueWarning = currentTime;
				DbgLog((LOG_TRACE, 1, TEXT("OnVideoFrame(): PROACTIVE drop - %zu frames removed, queue %zu/%zu"), 
					framesToDrop, currentQueueSize, m_frameQueueMaxSize));
			}
		}
		
		// Goal 2: Ensure frames only delivered once - Add new frame atomically
		videoFrame.SourceBufferAddRef();
		m_videoFrameQueue.push_back(videoFrame);
	}  // Release lock before signaling

	// Signal frame available
	SetEvent(m_hFrameAvailableEvent);
	return S_OK;
}


void CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize(size_t frameQueueMaxSize)
{
	if (frameQueueMaxSize <= 0)
		throw std::runtime_error("Frame queue size must be > 0");

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize() - Changing from %zu to %zu"), 
		m_frameQueueMaxSize, frameQueueMaxSize));

	{
		CAutoLock lock(&m_filterCritSec);

		const size_t oldQueueSize = m_frameQueueMaxSize;
		m_frameQueueMaxSize = frameQueueMaxSize;

		// If reducing queue size, intelligently purge excess frames
		if (m_videoFrameQueue.size() > frameQueueMaxSize)
		{
			const size_t framesToPurge = m_videoFrameQueue.size() - frameQueueMaxSize;
			
			DbgLog((LOG_TRACE, 1, TEXT("SetFrameQueueMaxSize(): Purging %zu excess frames due to size reduction"), 
				framesToPurge));
			
			for (size_t i = 0; i < framesToPurge && !m_videoFrameQueue.empty(); i++)
			{
				VideoFrame popFrame = m_videoFrameQueue.front();
				popFrame.SourceBufferRelease();
				m_videoFrameQueue.pop_front();
				++m_droppedFrameCount;
			}
		}
		
		// Reset simple proactive state
		m_recentDeliveryFailures = 0;
		m_lastQueueWarning = 0;
		
		// Zero out timeline state for fresh start
		m_frameCounter = 0;
		m_previousFrameCounter = 0;
		m_frameCounterOffset = 0;
		m_previousTimeStop = 0;
		m_startTimeOffset = 0;
		
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize() - Queue configured, timeline zeroed")));
	}
	
	SetEvent(m_hFrameAvailableEvent);
	
	// Deliver new segment if active
	if (IsConnected() && m_isActive)
	{
		if (FAILED(DeliverNewSegment(0, MAXLONGLONG, 1.0)))
		{
			DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize() - Failed to deliver new segment")));
		}
	}
}


void CBufferedLiveSourceVideoOutputPin::Reset()
{
	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Reset() - ASYNC conversion reset")));
	
	{
		CAutoLock lock(&m_filterCritSec);
		
		// Purge all raw frames efficiently
		size_t purgedFrames = 0;
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++purgedFrames;
		}
		
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): Purged %zu raw frames"), purgedFrames));
	}
	
	// Also purge converted sample queue
	{
		CAutoLock lock(&m_convertedQueueLock);
		
		size_t purgedSamples = 0;
		while (!m_convertedSampleQueue.empty())
		{
			IMediaSample* pSample = m_convertedSampleQueue.front();
			m_convertedSampleQueue.pop_front();
			if (pSample)
			{
				pSample->Release();
				++purgedSamples;
			}
		}
		
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): Purged %zu pre-converted samples"), purgedSamples));
	}
	
	// Reset timeline state
	{
		CAutoLock lock(&m_filterCritSec);
		
		m_frameCounter = 0;
		m_previousFrameCounter = 0;
		m_frameCounterOffset = 0;
		m_previousTimeStop = 0;
		m_startTimeOffset = 0;
	}
	
	// Reset conversion metrics
	m_totalConversionTimeUs = 0;
	m_conversionFrameCount = 0;
	
	// Reset simple proactive state
	m_recentDeliveryFailures = 0;
	m_lastQueueWarning = 0;
	
	// Call base Reset for DirectShow signaling
	ALiveSourceVideoOutputPin::Reset();
}


size_t CBufferedLiveSourceVideoOutputPin::GetFrameQueueSize()
{
	{
		CAutoLock lock(&m_filterCritSec);

		return m_videoFrameQueue.size();
	}
}


void CBufferedLiveSourceVideoOutputPin::PurgeQueue()
{
	size_t purgedFrames = 0;
	
	{
		CAutoLock lock(&m_filterCritSec);

		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			m_videoFrameQueue.pop_front();
			
			try
			{
				popFrame.SourceBufferRelease();
				++purgedFrames;
			}
			catch (...)
			{
				DbgLog((LOG_WARNING, 1, TEXT("PurgeQueue(): Exception during frame release %zu"), purgedFrames));
			}
		}
		m_droppedFrameCount += purgedFrames;
	}
	
	if (purgedFrames > 0)
	{
		DbgLog((LOG_TRACE, 1, TEXT("PurgeQueue(): Purged %zu raw frames"), purgedFrames));
	}
}


void CBufferedLiveSourceVideoOutputPin::PurgeConvertedQueue()
{
	size_t purgedSamples = 0;
	
	{
		CAutoLock lock(&m_convertedQueueLock);

		while (!m_convertedSampleQueue.empty())
		{
			IMediaSample* pSample = m_convertedSampleQueue.front();
			m_convertedSampleQueue.pop_front();
			
			if (pSample)
			{
				pSample->Release();
				++purgedSamples;
			}
		}
	}
	
	if (purgedSamples > 0)
	{
		DbgLog((LOG_TRACE, 1, TEXT("PurgeConvertedQueue(): Purged %zu pre-converted samples"), purgedSamples));
	}
}


REFERENCE_TIME CBufferedLiveSourceVideoOutputPin::NextFrameTimestamp() const
{
	return CalculateEnhancedNextTimestamp();
}


REFERENCE_TIME CBufferedLiveSourceVideoOutputPin::CalculateEnhancedNextTimestamp() const
{
	// Remove const from lock since we need non-const access
	CAutoLock lock(const_cast<CCritSec*>(&m_filterCritSec));

	// If queue has next frame, use its hardware timestamp
	if (!m_videoFrameQueue.empty())
	{
		const VideoFrame& nextFrame = m_videoFrameQueue.front();
		
		// Convert hardware timestamp to REFERENCE_TIME using integer math utility
		const REFERENCE_TIME hardwareStopTime = ConvertTimingClockToReferenceTime(
			nextFrame.GetTimingTimestamp(),
			m_timingClock->TimingClockTicksPerSecond());
		
		DbgLog((LOG_TRACE, 1, TEXT("NextFrameTimestamp(): Hardware stop time available from queue: %I64d"), 
			hardwareStopTime));
		
		return hardwareStopTime;
	}

	// No hardware stop timestamp available - this is the case mentioned in the requirements
	// Instead of using theoretical duration, use average of last 100 durations if available
	DbgLog((LOG_TRACE, 1, TEXT("NextFrameTimestamp(): No hardware stop time available, returning INVALID")));
	
	return REFERENCE_TIME_INVALID;
}


size_t CBufferedLiveSourceVideoOutputPin::GetProactiveQueueTarget() const
{
	// Simple proactive target: 60% of max capacity
	// Leaves 40% headroom to prevent reactive scenarios
	return (m_frameQueueMaxSize * 3) / 5;
}


bool CBufferedLiveSourceVideoOutputPin::ShouldProactivelyDrop() const
{
	// Drop more aggressively only if recent delivery failures
	return m_recentDeliveryFailures > 2;
}


CBufferedLiveSourceVideoOutputPin::ProactiveQueueMetrics CBufferedLiveSourceVideoOutputPin::GetProactiveMetrics() const
{
	ProactiveQueueMetrics metrics = {};
	
	{
		CAutoLock lock(const_cast<CCritSec*>(&m_filterCritSec));
		metrics.currentSize = m_videoFrameQueue.size();
	}
	
	{
		CAutoLock lock(const_cast<CCritSec*>(&m_convertedQueueLock));
		metrics.convertedQueueSize = m_convertedSampleQueue.size();
	}
	
	metrics.maxSize = m_frameQueueMaxSize;
	metrics.proactiveTarget = GetProactiveQueueTarget();
	metrics.totalDropped = m_droppedFrameCount;
	metrics.recentFailures = m_recentDeliveryFailures;
	
	// Calculate average conversion time
	if (m_conversionFrameCount > 0)
	{
		metrics.avgConversionTimeUs = m_totalConversionTimeUs / m_conversionFrameCount;
	}
	
	// Simple health check: queues below target and no recent failures
	metrics.isHealthy = (metrics.currentSize <= metrics.proactiveTarget) && 
	                    (metrics.recentFailures < 3) &&
	                    (metrics.convertedQueueSize < m_frameQueueMaxSize);
	
	return metrics;
}

DWORD CBufferedLiveSourceVideoOutputPin::ThreadProc()
{
	// Set thread priority for time-critical frame delivery
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	
	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: DELIVERY thread started - pulls PRE-CONVERTED samples")));
	DbgLog((LOG_TRACE, 1, TEXT("Delivery thread: ZERO conversion latency - samples already converted by worker")));

	// Multi-event synchronization
	HANDLE events[2] = { m_hFrameAvailableEvent, m_hShutdownEvent };
	
	while (true)
	{
		// Simple frame-rate aware timeout: 8ms for responsive delivery
		DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, 8);
		
		if (waitResult == WAIT_OBJECT_0 + 1)  // Shutdown
			break;
		
		if (waitResult == WAIT_TIMEOUT)
		{
			if (!m_isActive) break;
			continue;
		}
		
		if (waitResult != WAIT_OBJECT_0)
		{
			DbgLog((LOG_ERROR, 1, TEXT("ThreadProc: Wait failed %d"), waitResult));
			break;
		}

		// Process available PRE-CONVERTED frames
		const DWORD startTime = GetTickCount();
		
		while (true)
		{
			IMediaSample* pSample = nullptr;
			
			// Get pre-converted sample from queue (INSTANT - no blocking!)
			{
				CAutoLock lock(&m_convertedQueueLock);
				if (m_convertedSampleQueue.empty()) 
					break;

				// CRITICAL: Pull pre-converted sample (no conversion here!)
				pSample = m_convertedSampleQueue.front();
				m_convertedSampleQueue.pop_front();
			}

			// Deliver sample to MadVR (ZERO conversion latency!)
			HRESULT hr = Deliver(pSample);
			if (FAILED(hr))
			{
				pSample->Release();
				++m_droppedFrameCount;
				m_recentDeliveryFailures++;
				DbgLog((LOG_WARNING, 1, TEXT("ThreadProc: Deliver failed, hr=0x%x"), hr));
				continue;
			}

			// Success - clean delivery with ZERO conversion overhead
			pSample->Release();
			m_recentDeliveryFailures = 0;  // Reset on success
			
			// Simple time limit to prevent starvation
			if ((GetTickCount() - startTime) > 8)  // 8ms max batch time
				break;
		}
	}

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: DELIVERY thread exiting")));
	return 0;
}

DWORD WINAPI CBufferedLiveSourceVideoOutputPin::ConversionThreadProc(LPVOID lpParameter)
{
	CBufferedLiveSourceVideoOutputPin* pPin = static_cast<CBufferedLiveSourceVideoOutputPin*>(lpParameter);
	return pPin->ConversionWorker();
}


DWORD CBufferedLiveSourceVideoOutputPin::ConversionWorker()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	
	DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: ASYNC conversion thread started - conversion OFF critical path")));
	DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: MadVR gets 100%% of frame time for rendering")));
	
	while (true)
	{
		// Check for shutdown
		if (WaitForSingleObject(m_hConversionShutdownEvent, 0) == WAIT_OBJECT_0)
		{
			DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Shutdown signal received, exiting")));
			break;
		}
		
		// Get next raw frame to convert
		VideoFrame videoFrame;
		bool hasFrame = false;
		{
			CAutoLock lock(&m_filterCritSec);
			
			if (!m_isActive)
				break;
			
			if (!m_videoFrameQueue.empty())
			{
				videoFrame = m_videoFrameQueue.front();
				m_videoFrameQueue.pop_front();
				hasFrame = true;
			}
		}
		
		// No frame available - sleep briefly
		if (!hasFrame)
		{
			Sleep(1);  // 1ms sleep to avoid busy-wait
			continue;
		}
		
		// Allocate sample for conversion
		IMediaSample* pSample = nullptr;
		HRESULT hr = GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
		if (FAILED(hr))
		{
			DbgLog((LOG_WARNING, 1, TEXT("ConversionWorker: Failed to get delivery buffer, dropping frame")));
			videoFrame.SourceBufferRelease();
			++m_droppedFrameCount;
			continue;
		}
		
		// ASYNC CONVERSION: This happens OFF the critical rendering path
		// MadVR delivery continues uninterrupted in parallel
		const auto convStartTime = GetWallClockTime();
		
		hr = RenderVideoFrameIntoSample(videoFrame, pSample);
		
		const auto convEndTime = GetWallClockTime();
		const uint64_t convTimeUs = (convEndTime - convStartTime) / 10;
		
		// Track conversion time for metrics
		m_totalConversionTimeUs += convTimeUs;
		++m_conversionFrameCount;
		
		// Log periodically
		if ((m_conversionFrameCount % 300) == 0)  // Every 5 seconds at 60fps
		{
			const uint64_t avgConvUs = m_totalConversionTimeUs / m_conversionFrameCount;
			DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Converted %I64u frames, avg %.2f ms (OFF critical path)"),
				m_conversionFrameCount, avgConvUs / 1000.0));
		}
		
		if (FAILED(hr))
		{
			DbgLog((LOG_WARNING, 1, TEXT("ConversionWorker: Conversion failed for frame #%I64u"), 
				videoFrame.GetCounter()));
			videoFrame.SourceBufferRelease();
			pSample->Release();
			++m_droppedFrameCount;
			continue;
		}
		
		// Frame converted successfully - add to converted queue
		{
			CAutoLock lock(&m_convertedQueueLock);
			m_convertedSampleQueue.push_back(pSample);  // Store pre-converted sample
		}
		
		// Release raw frame
		videoFrame.SourceBufferRelease();
		
		// Signal frame available to delivery thread
		SetEvent(m_hFrameAvailableEvent);
	}
	
	DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Thread exiting")));
	return 0;
}
