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

	m_hConvertedAvailableEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!m_hConvertedAvailableEvent)
		throw std::runtime_error("Failed to create converted available event");

	
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

	if (m_hConvertedAvailableEvent)
	{
		CloseHandle(m_hConvertedAvailableEvent);
		m_hConvertedAvailableEvent = nullptr;
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
			m_isBuffering.store(true, std::memory_order_release);
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

		// MINIMAL STARTUP SYNC: Just ensure threads are created, no artificial delays
		// Threads will synchronize naturally through events and queues
		
		// Ensure both queues start empty and clean (no sleep needed)
		{
			CAutoLock lock2(&m_filterCritSec);
			// Raw queue should already be empty, but ensure it
			while (!m_videoFrameQueue.empty())
			{
				VideoFrame popFrame = m_videoFrameQueue.front();
				popFrame.SourceBufferRelease();
				m_videoFrameQueue.pop_front();
			}
			// STARTUP STATE: Enter buffering, reset counter tracking
			m_lastSeenFrameCounter = 0;
			m_isBuffering = true;
		}

		{
			CAutoLock lock2(&m_convertedQueueLock);
			// Converted queue should already be empty, but ensure it
			while (!m_convertedSampleQueue.empty())
			{
				IMediaSample* pSample = m_convertedSampleQueue.front();
				m_convertedSampleQueue.pop_front();
				if (pSample) pSample->Release();
			}
		}
		
		DbgLog((LOG_TRACE, 1, TEXT("Active(): Startup complete - threads ready, queues clean, buffering enabled")));

		// Kick both threads once so they observe the fresh startup state.
		// They will just block again if no work exists yet.
		SetEvent(m_hFrameAvailableEvent);        // conversion thread
		SetEvent(m_hConvertedAvailableEvent);    // delivery thread


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

		const uint64_t newCounter = videoFrame.GetCounter();

		// SIMPLE DISCONTINUITY DETECTION - if counter jumps or resets, trigger recovery
		// No cooldown needed - the buffering flag prevents re-entry
		if (m_lastSeenFrameCounter > 0 && !m_isBuffering)
		{
			const bool largeGap     = (newCounter > m_lastSeenFrameCounter) && ((newCounter - m_lastSeenFrameCounter) > 10);
			const bool counterReset = (newCounter < m_lastSeenFrameCounter);

			if (largeGap || counterReset)
			{
				DbgLog((LOG_TRACE, 1, TEXT("OnVideoFrame(): DISCONTINUITY DETECTED - triggering startup-like recovery")));
				DbgLog((LOG_TRACE, 1, TEXT("  Last counter: %I64u, New counter: %I64u"), m_lastSeenFrameCounter, newCounter));
				DebugLog::Log("OnVideoFrame: Frame counter discontinuity detected (last=%llu, new=%llu) - triggering recovery", m_lastSeenFrameCounter, newCounter);

				// PURGE BOTH QUEUES - identical to startup
				while (!m_videoFrameQueue.empty())
				{
					VideoFrame oldFrame = m_videoFrameQueue.front();
					oldFrame.SourceBufferRelease();
					m_videoFrameQueue.pop_front();
				}

				SetEvent(m_hConvertedAvailableEvent); // TODO: purge here?


				{
					CAutoLock lock2(&m_convertedQueueLock);
					while (!m_convertedSampleQueue.empty())
					{
						IMediaSample* pSample = m_convertedSampleQueue.front();
						m_convertedSampleQueue.pop_front();
						if (pSample) pSample->Release();
					}
				}

				// ENTER BUFFERING - this is the KEY. Don't deliver until queue fills!
				// The delivery thread will reset timeline when it exits buffering
				m_isBuffering = true;
				m_lastSeenFrameCounter = 0;  // Reset so we don't immediately trigger again
				
				
				DbgLog((LOG_TRACE, 1, TEXT("OnVideoFrame(): Recovery triggered - buffering enabled, queues purged")));
				DebugLog::Log("OnVideoFrame: Recovery complete - buffering enabled, queues purged");
			}
		}

		// Update counter for next frame
		m_lastSeenFrameCounter = newCounter;

		// Simple overflow protection - drop oldest if queue too full
		if (m_videoFrameQueue.size() >= m_frameQueueMaxSize)
		{
			VideoFrame oldFrame = m_videoFrameQueue.front();
			oldFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
			
			DbgLog((LOG_WARNING, 1, TEXT("OnVideoFrame(): Raw queue overflow - dropped frame, size=%ze"), m_videoFrameQueue.size()));
			DebugLog::Log("OnVideoFrame: Raw queue overflow (size=%ze) - dropped frame", m_frameQueueMaxSize);
		}
		
		// Add new frame
		videoFrame.SourceBufferAddRef();
		m_videoFrameQueue.push_back(videoFrame);

		// DIAGNOSTIC: Log when raw queue is backing up
		if (m_videoFrameQueue.size() >= (m_frameQueueMaxSize * 3) / 4)  // 75% threshold
		{
			static DWORD lastBackupLog = 0;
			DWORD now = GetTickCount();
			if (now - lastBackupLog >= 5000)  // Log at most every 5 seconds
			{
				uint64_t convFrames = m_conversionFrameCount.load();
				DbgLog((LOG_WARNING, 1, TEXT("OnVideoFrame(): Raw queue backup - size=%ze/%ze, buffering=%d"),
					m_videoFrameQueue.size(), m_frameQueueMaxSize,
					m_isBuffering.load(std::memory_order_acquire) ? 1 : 0));
				DebugLog::Log("OnVideoFrame: Raw queue backing up (size=%ze/%ze, buffering=%d, convFrames=%llu)",
					m_videoFrameQueue.size(), m_frameQueueMaxSize,
					m_isBuffering.load(std::memory_order_acquire) ? 1 : 0,
					convFrames);
				lastBackupLog = now;
			}
		}
	}

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

        // Reset simple proactive state only
        m_recentDeliveryFailures = 0;
        m_lastQueueWarning = 0;
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
	DebugLog::Log("CBufferedLiveSourceVideoOutputPin::Reset() - HDMI resync async queue reset starting");
	
	{
		CAutoLock lock(&m_filterCritSec);
		
		// Purge all raw frames
		size_t purgedFrames = 0;
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++purgedFrames;
		}

		// Wake both threads so they re-check state immediately after reset.
		// - Conversion thread may need to observe buffering/raw-empty and just block cleanly.
		// - Delivery thread may be waiting in INFINITE wait and should re-check buffering state.
		if (m_hFrameAvailableEvent)        SetEvent(m_hFrameAvailableEvent);
		if (m_hConvertedAvailableEvent)    SetEvent(m_hConvertedAvailableEvent);

		
		DebugLog::Log("Reset(): Purged %zu raw frames from HDMI resync", purgedFrames);
	}
	
	// Purge converted sample queue
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
		
		DebugLog::Log("Reset(): Purged %zu pre-converted samples from HDMI resync", purgedSamples);
	}
	
	// CRITICAL: Reset timeline state and enter buffering mode (identical to startup)
	{
		CAutoLock lock(&m_filterCritSec);
		
		m_frameCounter = 0;
		m_previousFrameCounter = 0;
		m_frameCounterOffset = 0;
		m_previousTimeStop = 0;
		m_startTimeOffset = 0;
		m_lastSeenFrameCounter = 0;
		
		// HDMI RESYNC: Enter buffering mode to rebuild queue state cleanly
		m_isBuffering.store(true, std::memory_order_release);
		
		DebugLog::Log("Reset(): Timeline reset and buffering enabled for HDMI resync recovery");
	}
	
	// Reset conversion metrics
	m_totalConversionTimeUs = 0;
	m_conversionFrameCount = 0;
	
	// Reset proactive state
	m_recentDeliveryFailures = 0;
	m_lastQueueWarning = 0;
	
	// Call base Reset for DirectShow signaling
	ALiveSourceVideoOutputPin::Reset();
	
	DebugLog::Log("CBufferedLiveSourceVideoOutputPin::Reset() - HDMI resync async queue reset completed");
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

// NOTE: This update removes ALL startup/reset sleeps and removes polling.
// It makes the threads event-driven:
//   - Conversion thread waits for RAW frames (m_hFrameAvailableEvent) or shutdown.
//   - Delivery thread waits for CONVERTED samples (m_hConvertedAvailableEvent) or shutdown.
// You MUST add/create/close a new event handle:
//   HANDLE m_hConvertedAvailableEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); // auto-reset
// And signal it ONLY when a converted sample is enqueued.
//
// Also recommended: in Reset(), after purging queues + setting buffering=true, call:
//   SetEvent(m_hFrameAvailableEvent);
//   SetEvent(m_hConvertedAvailableEvent);
// so both threads wake and re-check state immediately.

DWORD CBufferedLiveSourceVideoOutputPin::ThreadProc()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: DELIVERY thread started - waits on CONVERTED samples")));

	// Wait only for converted samples or shutdown. No timeout, no polling.
	HANDLE events[2] = { m_hConvertedAvailableEvent, m_hShutdownEvent };

	// Latency tracking for diagnostics
	DWORD lastLatencyLogTime = 0;
	uint64_t framesSinceLastLog = 0;
	uint64_t totalDeliveryTimeUs = 0;

	while (true)
	{
		DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);

		if (waitResult == WAIT_OBJECT_0 + 1) // shutdown
			break;

		if (waitResult != WAIT_OBJECT_0) // unexpected
		{
			DbgLog((LOG_ERROR, 1, TEXT("ThreadProc: WaitForMultipleObjects failed %lu"), waitResult));
			break;
		}

		if (!m_isActive.load(std::memory_order_acquire))
			break;

		// BUFFERING: do not deliver until we have enough converted samples.
		if (m_isBuffering.load(std::memory_order_acquire))
		{
			size_t convertedQueueSize = 0;
			{
				CAutoLock lock(&m_convertedQueueLock);
				convertedQueueSize = m_convertedSampleQueue.size();
			}

			const size_t bufferingTarget = GetBufferingTarget();

			if (convertedQueueSize < bufferingTarget)
			{
				// Not ready yet - go back to waiting for more converted samples.
				DbgLog((LOG_TRACE, 1, TEXT("ThreadProc: BUFFERING - waiting (%zu/%zu)"),
					convertedQueueSize, bufferingTarget));
				continue;
			}

			// Exit buffering: do the timeline reset once, then immediately start delivering.
			{
				CAutoLock lock(&m_filterCritSec);

				m_frameCounter = 0;
				m_previousFrameCounter = 0;
				m_frameCounterOffset = 0;
				m_previousTimeStop = 0;
				m_startTimeOffset = 0;

				m_isBuffering.store(false, std::memory_order_release);
			}

			DbgLog((LOG_TRACE, 1, TEXT("ThreadProc: BUFFERING COMPLETE (%zu/%zu) - delivery starting"),
				convertedQueueSize, bufferingTarget));
			DebugLog::Log("ThreadProc: Buffering complete with queue size %zu - exiting buffering mode", convertedQueueSize);
		}

		// Delivery timing measurement
		DWORD deliveryStartTime = GetTickCount();

		// Deliver everything currently available.
		for (;;)
		{
			IMediaSample* pSample = nullptr;

			{
				CAutoLock lock(&m_convertedQueueLock);
				if (m_convertedSampleQueue.empty())
					break;

				pSample = m_convertedSampleQueue.front();
				m_convertedSampleQueue.pop_front();
			}

			DWORD deliverStartTime = GetTickCount();
			HRESULT hr = Deliver(pSample);
			DWORD deliverEndTime = GetTickCount();
			uint32_t deliverTimeMs = deliverEndTime - deliverStartTime;
			
			pSample->Release();

			if (FAILED(hr))
			{
				++m_droppedFrameCount;
				++m_recentDeliveryFailures;
				
				// CRITICAL: Log the actual error code with description
				const char* hrDesc = "";
				switch (hr)
				{
				case VFW_E_NOT_CONNECTED:
					hrDesc = "NOT_CONNECTED";
					break;
				case VFW_E_WRONG_STATE:
					hrDesc = "WRONG_STATE";
					break;
				case VFW_E_NO_ALLOCATOR:
					hrDesc = "NO_ALLOCATOR";
					break;
				case E_INVALIDARG:
					hrDesc = "INVALID_ARG";
					break;
				case S_FALSE:
					hrDesc = "S_FALSE (downstream rejected)";
					break;
				case E_UNEXPECTED:
					hrDesc = "UNEXPECTED";
					break;
				default:
					hrDesc = "UNKNOWN";
				}
				
				DbgLog((LOG_WARNING, 1, TEXT("ThreadProc: Deliver FAILED hr=0x%08x (%S) - took %ums"),
					hr, hrDesc, deliverTimeMs));
				DebugLog::Log("ThreadProc: Deliver() FAILED hr=0x%08x (%s) - took %ums (failures=%u)",
					hr, hrDesc, deliverTimeMs, m_recentDeliveryFailures.load());

				continue;
			}

			//TODO: Just back pressure?
			// Log slow Deliver() calls (taking >5ms is unusual)
			//if (deliverTimeMs > 5)
			//{
			////	DebugLog::Log("ThreadProc: Deliver() took %ums (slow) - downstream latency?", deliverTimeMs);
			//}

			m_recentDeliveryFailures = 0;
			++framesSinceLastLog;
		}

		// Log delivery performance periodically
		DWORD now = GetTickCount();
		DWORD deliveryEndTime = now;
		uint32_t deliveryTimeMs = deliveryEndTime - deliveryStartTime;

		if (now - lastLatencyLogTime >= 10000)  // Every 10 seconds
		{
			size_t rawQueueSize = 0;
			size_t convertedQueueSize = 0;
			{
				CAutoLock lock(&m_filterCritSec);
				rawQueueSize = m_videoFrameQueue.size();
			}
			{
				CAutoLock lock(&m_convertedQueueLock);
				convertedQueueSize = m_convertedSampleQueue.size();
			}

			uint64_t avgConversionTimeUs = 0;
			if (m_conversionFrameCount > 0)
			{
				avgConversionTimeUs = m_totalConversionTimeUs / m_conversionFrameCount;
			}

			DebugLog::Log("DELIVERY THREAD STATS (10s): Frames=%llu, RawQueue=%zu, ConvertedQueue=%zu, AvgConvUs=%llu, IsBuffering=%d, DroppedFrames=%llu, RecentFailures=%u, DeliveryTimeMs=%u",
				framesSinceLastLog,
				rawQueueSize,
				convertedQueueSize,
				avgConversionTimeUs,
				m_isBuffering.load(std::memory_order_acquire) ? 1 : 0,
				m_droppedFrameCount,
				m_recentDeliveryFailures.load(),
				deliveryTimeMs);

			DbgLog((LOG_TRACE, 1, TEXT("ThreadProc: DELIVERY STATS - Frames=%llu, RawQ=%zu, ConvQ=%zu, AvgConvUs=%llu, Buffering=%d"),
				framesSinceLastLog, rawQueueSize, convertedQueueSize, avgConversionTimeUs,
				m_isBuffering.load(std::memory_order_acquire) ? 1 : 0));

			framesSinceLastLog = 0;
			lastLatencyLogTime = now;
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
	DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: ASYNC conversion thread started - waits on RAW frames")));

	// Wait for raw frames (m_hFrameAvailableEvent) or conversion shutdown.
	HANDLE events[2] = { m_hFrameAvailableEvent, m_hConversionShutdownEvent };

	// Conversion performance tracking
	DWORD lastConversionLogTime = 0;
	uint64_t framesSinceLastLog = 0;
	uint64_t totalTimeUs = 0;
	uint64_t maxTimeUs = 0;
	uint64_t minTimeUs = UINT64_MAX;

	for (;;)
	{
		DWORD wr = WaitForMultipleObjects(2, events, FALSE, INFINITE);

		if (wr == WAIT_OBJECT_0 + 1) // conversion shutdown
		{
			DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Shutdown signal received, exiting")));
			break;
		}

		if (wr != WAIT_OBJECT_0)
		{
			DbgLog((LOG_ERROR, 1, TEXT("ConversionWorker: WaitForMultipleObjects failed %lu"), wr));
			break;
		}

		// We were woken because "raw might be available".
		// Convert as many as we can until raw is empty or converted queue hits backpressure.
		for (;;)
		{
			if (!m_isActive.load(std::memory_order_acquire))
				return 0;

			// BACKPRESSURE: If converted queue is full, stop converting and let delivery drain.
			{
				CAutoLock lock(&m_convertedQueueLock);
				if (m_convertedSampleQueue.size() >= m_frameQueueMaxSize)
				{
					DbgLog((LOG_TRACE, 2, TEXT("ConversionWorker: Backpressure hit - converted queue full (%zu/%zu)"),
						m_convertedSampleQueue.size(), m_frameQueueMaxSize));
					break;
				}
			}

			// Pop one raw frame.
			VideoFrame videoFrame{};
			bool hasFrame = false;

			{
				CAutoLock lock(&m_filterCritSec);

				if (!m_isActive)
					return 0;

				if (!m_videoFrameQueue.empty())
				{
					videoFrame = m_videoFrameQueue.front();
					m_videoFrameQueue.pop_front();
					hasFrame = true;
				}
			}

			if (!hasFrame)
				break; // no more raw frames right now

			// Allocate sample for conversion (still your current architecture).
			IMediaSample* pSample = nullptr;
			HRESULT hr = GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
			if (FAILED(hr))
			{
				DbgLog((LOG_WARNING, 1, TEXT("ConversionWorker: GetDeliveryBuffer failed, dropping frame")));
				DebugLog::Log("ConversionWorker: GetDeliveryBuffer failed hr=0x%08x, dropping frame", hr);
				videoFrame.SourceBufferRelease();
				++m_droppedFrameCount;
				continue;
			}

			const auto convStartTime = GetWallClockTime();
			hr = RenderVideoFrameIntoSample(videoFrame, pSample);
			const auto convEndTime = GetWallClockTime();
			const uint64_t convTimeUs = (convEndTime - convStartTime) / 10;

			m_totalConversionTimeUs += convTimeUs;
			++m_conversionFrameCount;
			++framesSinceLastLog;

			totalTimeUs += convTimeUs;
			maxTimeUs = std::max(maxTimeUs, convTimeUs);
			if (convTimeUs > 0)
				minTimeUs = std::min(minTimeUs, convTimeUs);

			if (FAILED(hr))
			{
				DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Conversion failed for frame #%I64u"),
					videoFrame.GetCounter()));
				DebugLog::Log("ConversionWorker: Conversion failed for frame #%llu, hr=0x%08x", videoFrame.GetCounter(), hr);

				videoFrame.SourceBufferRelease();
				pSample->Release();
				++m_droppedFrameCount;
				continue;
			}

			// Enqueue converted sample.
			{
				CAutoLock lock(&m_convertedQueueLock);
				m_convertedSampleQueue.push_back(pSample);
				
				// AUTO-RESET: If converted queue grows too large, latency is building up
				// This indicates downstream (MadVR) isn't consuming fast enough
				// BUT: Don't trigger during startup - MadVR needs time to stabilize
				// AND: Add cooldown to prevent rapid-fire purges
				static DWORD lastPurgeTime = 0;
				DWORD now = GetTickCount();
				
				// Calculate time since we exited buffering (use 3 second grace period after any buffering exit)
				// This handles both initial startup AND refresh rate changes
				static DWORD bufferingExitTime = 0;
				bool isInGracePeriod = false;
				
				if (m_isBuffering.load(std::memory_order_acquire))
				{
					// Currently in buffering - reset the grace period timer
					bufferingExitTime = 0;
					isInGracePeriod = true;
				}
				else
				{
					// Not in buffering - check if we just exited
					if (bufferingExitTime == 0)
					{
						// First frame after exiting buffering - start the grace period
						bufferingExitTime = now;
						DebugLog::Log("ConversionWorker: Exited buffering - starting 3 second grace period");
					}
					
					// Check if we're still in the grace period
					isInGracePeriod = (now - bufferingExitTime) < 3000;
				}
				
				// Skip auto-purge during grace period (startup or after refresh rate change)
				if (isInGracePeriod)
				{
					// During grace period, just let the queue grow - MadVR is still stabilizing
				}
				// Skip if we purged recently (cooldown: 5 seconds between resets to let system stabilize)
				else if (now - lastPurgeTime < 5000)
				{
					// Too soon since last purge
				}
				// Threshold: more than 12 frames = ~200ms latency
				else if (m_convertedSampleQueue.size() > 12)
				{
					DebugLog::Log("ConversionWorker: Converted queue too large (%zu > 12) - triggering FULL RESET (like manual reset)",
						m_convertedSampleQueue.size());
					
					// Purge ALL converted samples
					size_t purged = 0;
					while (!m_convertedSampleQueue.empty())
					{
						IMediaSample* pOldSample = m_convertedSampleQueue.front();
						m_convertedSampleQueue.pop_front();
						if (pOldSample) pOldSample->Release();
						++purged;
						++m_droppedFrameCount;
					}
					
					lastPurgeTime = now;
					bufferingExitTime = 0;  // Reset grace period timer for when we exit buffering again
					DebugLog::Log("ConversionWorker: Purged all %zu converted samples, entering buffering mode", purged);
					
					// CRITICAL: Enter buffering mode to re-synchronize timeline (like manual reset does)
					// This is what fixes the latency - not just purging, but re-syncing the timeline
					m_isBuffering.store(true, std::memory_order_release);
				}
			}

			// Release raw frame now that conversion is done.
			videoFrame.SourceBufferRelease();

			// Signal delivery thread that converted samples are available.
			SetEvent(m_hConvertedAvailableEvent);
		}

		// Log conversion worker performance periodically
		DWORD now = GetTickCount();
		if (now - lastConversionLogTime >= 10000)  // Every 10 seconds
		{
			size_t rawQueueSize = 0;
			size_t convertedQueueSize = 0;
			{
				CAutoLock lock(&m_filterCritSec);
				rawQueueSize = m_videoFrameQueue.size();
			}
			{
				CAutoLock lock(&m_convertedQueueLock);
				convertedQueueSize = m_convertedSampleQueue.size();
			}

			uint64_t avgTimeUs = (framesSinceLastLog > 0) ? (totalTimeUs / framesSinceLastLog) : 0;
			uint64_t totalConvFrames = m_conversionFrameCount.load();

			DebugLog::Log("CONVERSION WORKER STATS (10s): Frames=%llu, Avg=%.2fms, Min=%.2fms, Max=%.2fms, RawQueue=%zu, ConvertedQueue=%zu, TotalConverted=%llu",
				framesSinceLastLog,
				avgTimeUs / 1000.0,
				(minTimeUs == UINT64_MAX ? 0.0 : minTimeUs / 1000.0),
				maxTimeUs / 1000.0,
				rawQueueSize,
				convertedQueueSize,
				totalConvFrames);

			DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: STATS - Frames=%llu, Avg=%.2fms, Min=%.2fms, Max=%.2fms, RawQ=%zu, ConvQ=%zu"),
				framesSinceLastLog,
				avgTimeUs / 1000.0,
				(minTimeUs == UINT64_MAX ? 0.0 : minTimeUs / 1000.0),
				maxTimeUs / 1000.0,
				rawQueueSize,
				convertedQueueSize));

			framesSinceLastLog = 0;
			totalTimeUs = 0;
			maxTimeUs = 0;
			minTimeUs = UINT64_MAX;
			lastConversionLogTime = now;
		}
	}

	DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Thread exiting")));
	return 0;
}

size_t CBufferedLiveSourceVideoOutputPin::GetBufferingTarget() 
{
	// Compute fps from the format (preferred), fallback to 60.
	double fps = 60.0;
	if (m_timeScale > 0 && m_frameDurationTicks > 0)
		fps = (double)m_timeScale / (double)m_frameDurationTicks;

	// Target buffering time (ms). Keep live latency low.
	// 60p -> 50ms => ~3 frames
	// 24p -> 80ms => ~2 frames (since each is ~41.7ms)
	double targetMs = 50.0;
	if (fps < 30.0) targetMs = 85.0;     // 23.976/24p needs fewer frames but more ms per frame
	if (fps > 90.0) targetMs = 35.0;     // high fps, lower ms target

	// Convert ms to frames: frames = ceil(fps * targetMs / 1000)
	size_t frames = (size_t)std::ceil((fps * targetMs) / 1000.0);

	// Clamp: never less than 2 frames, never more than 6 (keeps latency sane)
	frames = std::max<size_t>(2, std::min<size_t>(6, frames));

	// Also respect queue max size (leave headroom; don't require full queue)
	// If user set a tiny queue, don't demand more than it can hold.
	frames = std::min<size_t>(frames, (m_frameQueueMaxSize > 0) ? m_frameQueueMaxSize : frames);

	return frames;
}

void CBufferedLiveSourceVideoOutputPin::OnBadTimestampDetected()
{
	// COOLDOWN: Prevent rapid-fire recovery triggers
	static DWORD lastRecoveryTime = 0;
	DWORD now = GetTickCount();
	
	// Only allow recovery once per 500ms to prevent feedback loops
	if (now - lastRecoveryTime < 500)
	{
		return;  // Skip - too soon since last recovery
	}
	lastRecoveryTime = now;

	DebugLog::Log("CBufferedLiveSourceVideoOutputPin::OnBadTimestampDetected() - Bad CLOCK_SMART timestamp detected, triggering recovery");
	
	{
		CAutoLock lock(&m_filterCritSec);
		
		// Purge raw frames
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame oldFrame = m_videoFrameQueue.front();
			oldFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
		}
		
		SetEvent(m_hConvertedAvailableEvent); // Wake delivery thread
	}
	
	// Purge converted queue
	{
		CAutoLock lock(&m_convertedQueueLock);
		while (!m_convertedSampleQueue.empty())
		{
			IMediaSample* pSample = m_convertedSampleQueue.front();
			m_convertedSampleQueue.pop_front();
			if (pSample) pSample->Release();
		}
	}
	
	// CRITICAL: Clear hardware timestamp queue to prevent stale timestamps causing repeated rejections
	{
		std::lock_guard<std::mutex> lock(m_timestampQueueMutex);
		size_t queueSizeBefore = m_hardwareTimestampQueue.size();
		m_hardwareTimestampQueue.clear();
		
		// Reset validation parameters (will be recalculated on first enqueue)
		m_expectedFrameDuration = 0;
		m_minValidDuration = 0;
		m_maxValidDuration = 0;
		
		if (queueSizeBefore > 0)
		{
			DebugLog::Log("OnBadTimestampDetected(): Cleared %zu stale timestamps from queue", queueSizeBefore);
		}
	}
	
	// Enter buffering mode
	{
		CAutoLock lock(&m_filterCritSec);
		m_isBuffering = true;
		m_lastSeenFrameCounter = 0;
	}
	
	DebugLog::Log("OnBadTimestampDetected(): Recovery complete - buffering enabled, queues purged");
}

































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































