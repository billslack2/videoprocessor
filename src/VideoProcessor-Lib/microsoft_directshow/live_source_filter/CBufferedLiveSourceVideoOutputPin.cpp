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
		}
		
		// Add new frame
		videoFrame.SourceBufferAddRef();
		m_videoFrameQueue.push_back(videoFrame);
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
		}

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

			HRESULT hr = Deliver(pSample);
			pSample->Release();

			if (FAILED(hr))
			{
				++m_droppedFrameCount;
				++m_recentDeliveryFailures;
				DbgLog((LOG_WARNING, 1, TEXT("ThreadProc: Deliver failed, hr=0x%08x"), hr));

				// Optional: break out on terminal graph states.
				// if (hr == VFW_E_WRONG_STATE || hr == VFW_E_NOT_CONNECTED) break;

				continue;
			}

			m_recentDeliveryFailures = 0;
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
					break;
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

			if ((m_conversionFrameCount % 300) == 0)
			{
				const uint64_t avgConvUs = (m_conversionFrameCount > 0)
					? (m_totalConversionTimeUs / m_conversionFrameCount)
					: 0;
				DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Converted %I64u frames, avg %.2f ms"),
					m_conversionFrameCount, avgConvUs / 1000.0));
			}

			if (FAILED(hr))
			{
				DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Conversion failed for frame #%I64u"),
					videoFrame.GetCounter()));

				videoFrame.SourceBufferRelease();
				pSample->Release();
				++m_droppedFrameCount;
				continue;
			}

			// Enqueue converted sample.
			{
				CAutoLock lock(&m_convertedQueueLock);
				m_convertedSampleQueue.push_back(pSample);
			}

			// Release raw frame now that conversion is done.
			videoFrame.SourceBufferRelease();

			// Signal delivery thread that converted samples are available.
			SetEvent(m_hConvertedAvailableEvent);
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



