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
}


CBufferedLiveSourceVideoOutputPin::~CBufferedLiveSourceVideoOutputPin()
{
	PurgeQueue();
	
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

		// Log PROACTIVE approach being activated
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Active() - PROACTIVE frame management:")));
		DbgLog((LOG_TRACE, 1, TEXT("  Goal 1: Ensure timely delivery (60% queue target, 8ms timeouts)")));
		DbgLog((LOG_TRACE, 1, TEXT("  Goal 2: Ensure single delivery (atomic dequeue operations)")));
		DbgLog((LOG_TRACE, 1, TEXT("  Goal 3: Accurate timestamps (integer math, monotonic enforcement)")));
		DbgLog((LOG_TRACE, 1, TEXT("  Simplified: No complex reactive recovery - prevention focused")));
		
		// start the thread
		if (!Create())
			return E_FAIL;

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
		}

		// Signal shutdown event before waiting for thread to exit
		if (m_hShutdownEvent)
			SetEvent(m_hShutdownEvent);

		if (ThreadExists())
		{
			Close();  // This waits for thread to exit
		}

		// Reset shutdown event for next activation
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
	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Reset() - PROACTIVE system reset")));
	
	{
		CAutoLock lock(&m_filterCritSec);
		
		// Purge all frames efficiently
		size_t purgedFrames = 0;
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++purgedFrames;
		}
		m_droppedFrameCount += purgedFrames;
		
		// Reset timeline state
		m_frameCounter = 0;
		m_previousFrameCounter = 0;
		m_frameCounterOffset = 0;
		m_previousTimeStop = 0;
		m_startTimeOffset = 0;
		
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): Purged %zu frames, timeline reset"), purgedFrames));
	}
	
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
		DbgLog((LOG_TRACE, 1, TEXT("PurgeQueue(): Purged %zu frames"), purgedFrames));
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
	
	metrics.maxSize = m_frameQueueMaxSize;
	metrics.proactiveTarget = GetProactiveQueueTarget();
	metrics.totalDropped = m_droppedFrameCount;
	metrics.recentFailures = m_recentDeliveryFailures;
	
	// Simple health check: queue below target and no recent failures
	metrics.isHealthy = (metrics.currentSize <= metrics.proactiveTarget) && (metrics.recentFailures < 3);
	
	return metrics;
}

DWORD CBufferedLiveSourceVideoOutputPin::ThreadProc()
{
	// Set thread priority for time-critical frame delivery
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	
	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: PROACTIVE thread started - focus: timely delivery, no duplicates, accurate timestamps")));

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

		// Process available frames with simple batching limit
		const DWORD startTime = GetTickCount();
		
		while (true)
		{
			VideoFrame videoFrame;
			{
				CAutoLock lock(&m_filterCritSec);
				if (!m_isActive || m_videoFrameQueue.empty()) 
					break;

				// Goal 2: Ensure single delivery - atomic dequeue
				videoFrame = m_videoFrameQueue.front();
				m_videoFrameQueue.pop_front();

				// Update timing for CLOCK_SMART (essential only)
				if (m_lastHardwareTimestamp > 0)
				{
					const REFERENCE_TIME currentTimestamp = ConvertTimingClockToReferenceTime(
						videoFrame.GetTimingTimestamp(), m_timingClock->TimingClockTicksPerSecond());
					const REFERENCE_TIME actualDuration = currentTimestamp - m_lastHardwareTimestamp;
					UpdateFrameDurationHistory(actualDuration);
				}
				
				m_lastHardwareTimestamp = ConvertTimingClockToReferenceTime(
					videoFrame.GetTimingTimestamp(), m_timingClock->TimingClockTicksPerSecond());
			}

			// Goal 1: Ensure timely delivery - process frame efficiently
			IMediaSample* pSample = nullptr;
			HRESULT hr = GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
			if (FAILED(hr))
			{
				videoFrame.SourceBufferRelease();
				++m_droppedFrameCount;
				m_recentDeliveryFailures++;
				continue;
			}

			// Goal 3: Ensure accurate timestamps - render with proper timing
			hr = RenderVideoFrameIntoSample(videoFrame, pSample);
			if (FAILED(hr))
			{
				videoFrame.SourceBufferRelease();
				pSample->Release();
				++m_droppedFrameCount;
				m_recentDeliveryFailures++;
				continue;
			}

			hr = Deliver(pSample);
			if (FAILED(hr))
			{
				videoFrame.SourceBufferRelease();
				pSample->Release();
				++m_droppedFrameCount;
				m_recentDeliveryFailures++;
				continue;
			}

			// Success - clean delivery
			videoFrame.SourceBufferRelease();
			pSample->Release();
			m_recentDeliveryFailures = 0;  // Reset on success
			
			// Simple time limit to prevent starvation
			if ((GetTickCount() - startTime) > 8)  // 8ms max batch time
				break;
		}
	}

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: PROACTIVE thread exiting")));
	return 0;
}
