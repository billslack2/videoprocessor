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
	// Initialize all member variables BEFORE creating events/threads
	m_frameQueueMaxSize = 32;  // Default safe value
	m_isActive.store(false, std::memory_order_relaxed);
	m_isBuffering.store(true, std::memory_order_relaxed);  // Start in buffering mode
	m_lastSeenFrameCounter = 0;
	m_totalConversionTimeUs.store(0, std::memory_order_relaxed);
	m_conversionFrameCount.store(0, std::memory_order_relaxed);
	m_recentDeliveryFailures.store(0, std::memory_order_relaxed);
	m_lastQueueWarning = 0;
	m_hConversionThread = nullptr;
	m_conversionThreadId = 0;
	m_hFrameAvailableEvent = nullptr;
	m_hShutdownEvent = nullptr;
	m_hConversionShutdownEvent = nullptr;
	m_hConvertedAvailableEvent = nullptr;
	
	// Initialize auto-purge timing state
	m_lastAutoPurgeTime = 0;
	m_bufferingExitTime = 0;

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
		m_hFrameAvailableEvent = nullptr;
		throw std::runtime_error("Failed to create shutdown event");
	}
	
	// Create shutdown event for conversion worker thread
	m_hConversionShutdownEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (!m_hConversionShutdownEvent)
	{
		CloseHandle(m_hShutdownEvent);
		m_hShutdownEvent = nullptr;
		CloseHandle(m_hFrameAvailableEvent);
		m_hFrameAvailableEvent = nullptr;
		throw std::runtime_error("Failed to create conversion shutdown event");
	}

	m_hConvertedAvailableEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!m_hConvertedAvailableEvent)
	{
		CloseHandle(m_hConversionShutdownEvent);
		m_hConversionShutdownEvent = nullptr;
		CloseHandle(m_hShutdownEvent);
		m_hShutdownEvent = nullptr;
		CloseHandle(m_hFrameAvailableEvent);
		m_hFrameAvailableEvent = nullptr;
		throw std::runtime_error("Failed to create converted available event");
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
	
	// Purge both queues - with null checks for safety
	try 
	{
		PurgeConvertedQueue();
		PurgeQueue();
	}
	catch (...)
	{
		DbgLog((LOG_WARNING, 1, TEXT("~CBufferedLiveSourceVideoOutputPin: Exception during queue purge")));
	}
	
	// Clean up events with null checks
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

	DebugLog::Log("CBufferedLiveSourceVideoOutputPin::Active() - Starting activation with queue size %zu", m_frameQueueMaxSize);

	{
		CAutoLock lock(m_pLock);

		if (m_pFilter->IsActive())
		{
			DebugLog::Log("Active(): Filter already active, returning S_FALSE");
			return S_FALSE;	// succeeded, but did not allocate resources (they already exist...)
		}

		assert(IsConnected());
		assert(!m_isActive);

		HRESULT hr = ALiveSourceVideoOutputPin::Active();
		if (FAILED(hr))
		{
			DebugLog::Log("Active(): ALiveSourceVideoOutputPin::Active() FAILED hr=0x%08x", hr);
			return hr;
		}

		assert(!ThreadExists());

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = true;
			m_isBuffering.store(true, std::memory_order_release);
			
			// Reset auto-purge timing state for clean startup
			m_lastAutoPurgeTime = 0;
			m_bufferingExitTime = 0;
			
			DebugLog::Log("Active(): Set m_isActive=true, m_isBuffering=true, reset timing state");
		}

		// Log ASYNC conversion approach
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Active() - ASYNC conversion architecture:")));
		DbgLog((LOG_TRACE, 1, TEXT("  Raw frames → Conversion Worker (OFF critical path) → Pre-Converted Queue → Delivery Thread → MadVR")));
		DbgLog((LOG_TRACE, 1, TEXT("  Benefit: MadVR gets 100%% of frame time (conversion happens in parallel)")));
		DbgLog((LOG_TRACE, 1, TEXT("  Result: Zero conversion latency on delivery path")));
		
		DebugLog::Log("Active(): ASYNC architecture - Raw->Convert->Queue->Deliver->MadVR with queue size %zu", m_frameQueueMaxSize);
		
		// SAFETY: Ensure all events are created before starting threads
		if (!m_hConversionShutdownEvent || !m_hFrameAvailableEvent || !m_hConvertedAvailableEvent)
		{
			DbgLog((LOG_ERROR, 1, TEXT("Active(): Critical events not initialized")));
			DebugLog::Log("Active(): CRITICAL EVENTS NOT INITIALIZED - ConvShutdown=%p, FrameAvailable=%p, ConvertedAvailable=%p",
				m_hConversionShutdownEvent, m_hFrameAvailableEvent, m_hConvertedAvailableEvent);
			m_isActive = false;
			return E_FAIL;
		}
		
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
			DebugLog::Log("Active(): FAILED to create conversion thread, GetLastError=%lu", GetLastError());
			m_isActive = false;
			return E_FAIL;
		}
		
		DbgLog((LOG_TRACE, 1, TEXT("Active(): Conversion worker thread started (ID: %d)"), m_conversionThreadId));
		DebugLog::Log("Active(): Conversion worker thread started (ID: %lu)", m_conversionThreadId);
		
		// Start the delivery thread
		if (!Create())
		{
			// Cleanup conversion thread
			DebugLog::Log("Active(): FAILED to create delivery thread, cleaning up conversion thread");
			SetEvent(m_hConversionShutdownEvent);
			WaitForSingleObject(m_hConversionThread, 1000);
			CloseHandle(m_hConversionThread);
			m_hConversionThread = nullptr;
			
			m_isActive = false;
			return E_FAIL;
		}

		DebugLog::Log("Active(): Both threads created successfully");

		// MINIMAL STARTUP SYNC: Just ensure threads are created, no artificial delays
		// Threads will synchronize naturally through events and queues
		
		// Ensure both queues start empty and clean (no sleep needed)
		size_t purgedRaw = 0, purgedConverted = 0;
		{
			CAutoLock lock2(&m_filterCritSec);
			// Raw queue should already be empty, but ensure it
			while (!m_videoFrameQueue.empty())
			{
				VideoFrame popFrame = m_videoFrameQueue.front();
				popFrame.SourceBufferRelease();
				m_videoFrameQueue.pop_front();
				++purgedRaw;
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
				++purgedConverted;
			}
		}
		
		DbgLog((LOG_TRACE, 1, TEXT("Active(): Startup complete - threads ready, queues clean, buffering enabled")));
		DebugLog::Log("Active(): Startup complete - purged %zu raw + %zu converted, buffering enabled", 
			purgedRaw, purgedConverted);

		// Kick both threads once so they observe the fresh startup state.
		// They will just block again if no work exists yet.
		SetEvent(m_hFrameAvailableEvent);        // conversion thread
		SetEvent(m_hConvertedAvailableEvent);    // delivery thread

		DebugLog::Log("Active(): Signaled both threads to start, activation complete");

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
		{
			static DWORD lastInactiveLog = 0;
			DWORD now = GetTickCount();
			if (now - lastInactiveLog >= 5000)  // Log every 5s when inactive
			{
				DebugLog::Log("OnVideoFrame: Rejecting frame #%llu - not active", videoFrame.GetCounter());
				lastInactiveLog = now;
			}
			return S_OK;
		}

		const uint64_t newCounter = videoFrame.GetCounter();

		// SIMPLE DISCONTINUITY DETECTION - if counter jumps or resets, trigger recovery
		if (m_lastSeenFrameCounter > 0 && !m_isBuffering)
		{
			const bool largeGap     = (newCounter > m_lastSeenFrameCounter) && ((newCounter - m_lastSeenFrameCounter) > 10);
			const bool counterReset = (newCounter < m_lastSeenFrameCounter);

			if (largeGap || counterReset)
			{
				DbgLog((LOG_TRACE, 1, TEXT("OnVideoFrame(): DISCONTINUITY DETECTED - triggering startup-like recovery")));
				DebugLog::Log("OnVideoFrame: Frame counter discontinuity detected (last=%llu, new=%llu) - triggering recovery", m_lastSeenFrameCounter, newCounter);

				// PURGE BOTH QUEUES - identical to startup
				size_t purgedRaw = 0;
				while (!m_videoFrameQueue.empty())
				{
					VideoFrame oldFrame = m_videoFrameQueue.front();
					oldFrame.SourceBufferRelease();
					m_videoFrameQueue.pop_front();
					++purgedRaw;
				}

				SetEvent(m_hConvertedAvailableEvent);

				size_t purgedConverted = 0;
				{
					CAutoLock lock2(&m_convertedQueueLock);
					while (!m_convertedSampleQueue.empty())
					{
						IMediaSample* pSample = m_convertedSampleQueue.front();
						m_convertedSampleQueue.pop_front();
						if (pSample) pSample->Release();
						++purgedConverted;
					}
				}

				// ENTER BUFFERING
				m_isBuffering = true;
				m_lastSeenFrameCounter = 0;
				
				DebugLog::Log("OnVideoFrame: Recovery complete - buffering enabled, purged %zu raw + %zu converted frames", 
					purgedRaw, purgedConverted);
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
			
			DebugLog::Log("OnVideoFrame: Raw queue OVERFLOW - dropped frame #%llu, size=%zu/%zu", 
				oldFrame.GetCounter(), m_videoFrameQueue.size(), m_frameQueueMaxSize);
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
				size_t convertedSize = 0;
				{
					CAutoLock lock2(&m_convertedQueueLock);
					convertedSize = m_convertedSampleQueue.size();
				}
				
				DebugLog::Log("OnVideoFrame: Raw queue BACKING UP (raw=%zu/%zu, converted=%zu, buffering=%d, convFrames=%llu)",
					m_videoFrameQueue.size(), m_frameQueueMaxSize, convertedSize,
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
    DebugLog::Log("SetFrameQueueMaxSize: Changing queue size from %zu to %zu", 
		m_frameQueueMaxSize, frameQueueMaxSize);

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
            DebugLog::Log("SetFrameQueueMaxSize: Queue size reduction - purging %zu excess frames (current=%zu, new=%zu)",
				framesToPurge, m_videoFrameQueue.size(), frameQueueMaxSize);

            for (size_t i = 0; i < framesToPurge && !m_videoFrameQueue.empty(); i++)
            {
                VideoFrame popFrame = m_videoFrameQueue.front();
                popFrame.SourceBufferRelease();
                m_videoFrameQueue.pop_front();
                ++m_droppedFrameCount;
            }
			
			DebugLog::Log("SetFrameQueueMaxSize: Purged %zu frames, queue now has %zu frames", 
				framesToPurge, m_videoFrameQueue.size());
        }

        // Reset simple proactive state only
        m_recentDeliveryFailures = 0;
        m_lastQueueWarning = 0;
		
		DebugLog::Log("SetFrameQueueMaxSize: Queue size changed, reset failure counters");
    }
	
	SetEvent(m_hFrameAvailableEvent);
	
	// Deliver new segment if active
	if (IsConnected() && m_isActive)
	{
		if (FAILED(DeliverNewSegment(0, MAXLONGLONG, 1.0)))
		{
			DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize() - Failed to deliver new segment")));
			DebugLog::Log("SetFrameQueueMaxSize: FAILED to deliver new segment");
		}
		else
		{
			DebugLog::Log("SetFrameQueueMaxSize: New segment delivered successfully");
		}
	}
	else
	{
		DebugLog::Log("SetFrameQueueMaxSize: Not connected or not active, skipping new segment delivery");
	}
	
	DebugLog::Log("SetFrameQueueMaxSize: Complete - signaled conversion thread");
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

		
		DebugLog::Log("Reset(): Purged %zu raw frames from HDMI resync, signaled threads", purgedFrames);
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
		
		// Reset auto-purge timing state for clean recovery
		m_lastAutoPurgeTime = 0;
		m_bufferingExitTime = 0;
		
		// HDMI RESYNC: Enter buffering mode to rebuild queue state cleanly
		m_isBuffering.store(true, std::memory_order_release);
		
		DebugLog::Log("Reset(): Timeline reset, timing state cleared, buffering ENABLED for HDMI resync recovery");
	}
	
	// Reset conversion metrics
	m_totalConversionTimeUs = 0;
	m_conversionFrameCount = 0;
	
	// Reset proactive state
	m_recentDeliveryFailures = 0;
	m_lastQueueWarning = 0;
	
	DebugLog::Log("Reset(): Metrics and state reset complete");
	
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

	// SAFETY: Check if timing clock is initialized
	if (!m_timingClock)
	{
		DbgLog((LOG_TRACE, 1, TEXT("CalculateEnhancedNextTimestamp(): Timing clock not initialized")));
		return REFERENCE_TIME_INVALID;
	}

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
	DebugLog::Log("DELIVERY THREAD: Started - event-driven with adaptive buffer management");

	HANDLE events[2] = { m_hConvertedAvailableEvent, m_hShutdownEvent };
	DWORD lastLatencyLogTime = 0;
	uint64_t framesSinceLastLog = 0;

	// Enhanced delivery performance tracking
	uint64_t deliverySuccessCount = 0;
	uint64_t deliveryFailureCount = 0;
	uint64_t totalDeliveryTimeUs = 0;
	uint64_t maxDeliveryTimeUs = 0;
	uint64_t minDeliveryTimeUs = UINT64_MAX;
	uint64_t bufferUnderrunCount = 0;

	while (true)
	{
		// Wait for converted samples or shutdown
		DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);

		if (waitResult == WAIT_OBJECT_0 + 1) // shutdown
		{
			DebugLog::Log("DELIVERY THREAD: Shutdown signal received");
			break;
		}

		if (waitResult != WAIT_OBJECT_0)
		{
			DebugLog::Log("DELIVERY THREAD: WaitForMultipleObjects FAILED result=%lu", waitResult);
			break;
		}

		if (!m_isActive.load(std::memory_order_acquire))
		{
			DebugLog::Log("DELIVERY THREAD: Not active, exiting");
			break;
		}

		// BUFFERING PHASE: do not deliver until we have enough converted samples
		if (m_isBuffering.load(std::memory_order_acquire))
		{
			size_t convertedQueueSize = 0;
			{
				CAutoLock lock(&m_convertedQueueLock);
				convertedQueueSize = m_convertedSampleQueue.size();
			}

			// DYNAMIC BUFFERING: Use GetBufferingTarget() for consistency
			const size_t bufferingTarget = GetBufferingTarget();

			if (convertedQueueSize < bufferingTarget)
			{
				continue; // Keep waiting for more samples
			}

			// Exit buffering
			{
				CAutoLock lock(&m_filterCritSec);
				m_frameCounter = 0;
				m_previousFrameCounter = 0;
				m_frameCounterOffset = 0;
				m_previousTimeStop = 0;
				m_startTimeOffset = 0;
				m_isBuffering.store(false, std::memory_order_release);
			}

			DebugLog::Log("DELIVERY THREAD: BUFFERING COMPLETE (%zu/%zu) - delivery starting",
				convertedQueueSize, bufferingTarget);
		}

		// STEADY-STATE: Deliver as long as samples are available
		// No minimum threshold - just drain the queue naturally
		for (;;)
		{
			size_t currentQueueSize = 0;
			IMediaSample* pSample = nullptr;

			{
				CAutoLock lock(&m_convertedQueueLock);
				currentQueueSize = m_convertedSampleQueue.size();

				// Dequeue if we have ANY samples
				if (currentQueueSize == 0)
				{
					break; // Queue empty, wait for more
				}

				pSample = m_convertedSampleQueue.front();
				m_convertedSampleQueue.pop_front();
			}

			// Measure delivery time (time spent in Deliver() call)
			const auto deliveryStartTime = GetWallClockTime();

			// Deliver() will block if MadVR isn't ready
			HRESULT hr = Deliver(pSample);

			const auto deliveryEndTime = GetWallClockTime();
			const uint64_t deliveryTimeUs = (deliveryEndTime - deliveryStartTime) / 10;

			pSample->Release();

			// Track delivery performance statistics
			totalDeliveryTimeUs += deliveryTimeUs;
			maxDeliveryTimeUs = std::max(maxDeliveryTimeUs, deliveryTimeUs);
			if (deliveryTimeUs > 0)
				minDeliveryTimeUs = std::min(minDeliveryTimeUs, deliveryTimeUs);

			if (FAILED(hr))
			{
				++m_droppedFrameCount;
				++m_recentDeliveryFailures;
				++deliveryFailureCount;
				DebugLog::Log("DELIVERY THREAD: Deliver() FAILED hr=0x%08x (failures=%u)",
					hr, m_recentDeliveryFailures.load());
			}
			else
			{
				m_recentDeliveryFailures = 0;
				++framesSinceLastLog;
				++deliverySuccessCount;
			}

			// Log slow deliveries (similar to conversion worker)
			if (deliveryTimeUs > 20000)  // > 20ms is unusual for Deliver()
			{
				DebugLog::Log("DELIVERY THREAD: Slow delivery took %.2fms (MadVR blocking?)",
					deliveryTimeUs / 1000.0);
			}

			if (!m_isActive.load(std::memory_order_acquire))
				break;
		}

		// Log delivery worker performance periodically (match conversion worker format)
		DWORD now = GetTickCount();
		if (now - lastLatencyLogTime >= 10000)
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

			// Calculate delivery statistics
			uint64_t avgDeliveryTimeUs = (deliverySuccessCount > 0) ? (totalDeliveryTimeUs / deliverySuccessCount) : 0;
			uint64_t totalDeliveryAttempts = deliverySuccessCount + deliveryFailureCount;

			// Delivery worker stats (matching conversion worker format)
			DebugLog::Log("DELIVERY THREAD STATS (10s): Frames=%llu, Successes=%llu, Failures=%llu, Avg=%.2fms, Min=%.2fms, Max=%.2fms, RawQueue=%zu, ConvertedQueue=%zu, DroppedFrames=%llu",
				totalDeliveryAttempts,
				deliverySuccessCount,
				deliveryFailureCount,
				avgDeliveryTimeUs / 1000.0,
				(minDeliveryTimeUs == UINT64_MAX ? 0.0 : minDeliveryTimeUs / 1000.0),
				maxDeliveryTimeUs / 1000.0,
				rawQueueSize,
				convertedQueueSize,
				m_droppedFrameCount);

			// Reset counters for next period
			framesSinceLastLog = 0;
			deliverySuccessCount = 0;
			deliveryFailureCount = 0;
			totalDeliveryTimeUs = 0;
			maxDeliveryTimeUs = 0;
			minDeliveryTimeUs = UINT64_MAX;
			bufferUnderrunCount = 0;
			lastLatencyLogTime = now;
		}
	}

	DebugLog::Log("DELIVERY THREAD: Exiting");
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
	DebugLog::Log("CONVERSION WORKER: Started - will convert raw frames and signal delivery thread");

	// Wait for raw frames (m_hFrameAvailableEvent) or conversion shutdown.
	HANDLE events[2] = { m_hFrameAvailableEvent, m_hConversionShutdownEvent };

	// Conversion performance tracking
	DWORD lastConversionLogTime = 0;
	uint64_t framesSinceLastLog = 0;
	uint64_t totalTimeUs = 0;
	uint64_t maxTimeUs = 0;
	uint64_t minTimeUs = UINT64_MAX;
	uint64_t backpressureHits = 0;

	for (;;)
	{
		DWORD wr = WaitForMultipleObjects(2, events, FALSE, INFINITE);

		if (wr == WAIT_OBJECT_0 + 1) // conversion shutdown
		{
			DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Shutdown signal received, exiting")));
			DebugLog::Log("CONVERSION WORKER: Shutdown signal received, exiting");
			break;
		}

		if (wr != WAIT_OBJECT_0)
		{
			DbgLog((LOG_ERROR, 1, TEXT("ConversionWorker: WaitForMultipleObjects failed %lu"), wr));
			DebugLog::Log("CONVERSION WORKER: WaitForMultipleObjects FAILED result=%lu", wr);
			break;
		}

		// Convert as many as we can until raw is empty or converted queue hits backpressure.
		size_t batchCount = 0;
		for (;;)
		{
			if (!m_isActive.load(std::memory_order_acquire))
			{
				DebugLog::Log("CONVERSION WORKER: Not active, returning");
				return 0;
			}

			// BACKPRESSURE: If converted queue is full, stop converting and let delivery drain.
			size_t currentConvertedSize = 0;
			{
				CAutoLock lock(&m_convertedQueueLock);
				currentConvertedSize = m_convertedSampleQueue.size();
				if (currentConvertedSize >= m_frameQueueMaxSize)
				{
					++backpressureHits;
					DebugLog::Log("CONVERSION WORKER: BACKPRESSURE hit - converted queue full (%zu/%zu), stopping conversion",
						currentConvertedSize, m_frameQueueMaxSize);
					break;
				}
			}

			// Pop one raw frame.
			VideoFrame videoFrame{};
			bool hasFrame = false;
			size_t rawQueueSize = 0;

			{
				CAutoLock lock(&m_filterCritSec);

				if (!m_isActive)
				{
					DebugLog::Log("CONVERSION WORKER: Not active during raw frame check, returning");
					return 0;
				}

				rawQueueSize = m_videoFrameQueue.size();
				if (!m_videoFrameQueue.empty())
				{
					videoFrame = m_videoFrameQueue.front();
					m_videoFrameQueue.pop_front();
					hasFrame = true;
				}
			}

			if (!hasFrame)
			{
				break; // no more raw frames right now
			}

			// Allocate sample for conversion
			IMediaSample* pSample = nullptr;
			HRESULT hr = GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
			if (FAILED(hr))
			{
				DebugLog::Log("CONVERSION WORKER: GetDeliveryBuffer FAILED hr=0x%08x, dropping frame counter=%llu", 
					hr, videoFrame.GetCounter());
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
				DebugLog::Log("CONVERSION WORKER: Conversion FAILED for frame #%llu, hr=0x%08x", 
					videoFrame.GetCounter(), hr);

				videoFrame.SourceBufferRelease();
				pSample->Release();
				++m_droppedFrameCount;
				continue;
			}

			// Release raw frame - we're done with it
			videoFrame.SourceBufferRelease();

			// Add converted sample to queue
			{
				CAutoLock lock(&m_convertedQueueLock);
				m_convertedSampleQueue.push_back(pSample);
			}

			// Signal delivery thread that a converted sample is available
			SetEvent(m_hConvertedAvailableEvent);
			++batchCount;

			// Log slow conversions
			if (convTimeUs > 5000)  // > 5ms is unusual
			{
				DebugLog::Log("CONVERSION WORKER: Slow conversion took %.2fms for frame #%llu", 
					convTimeUs / 1000.0, videoFrame.GetCounter());
			}
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

			DebugLog::Log("CONVERSION WORKER STATS (10s): Frames=%llu, Avg=%.2fms, Min=%.2fms, Max=%.2fms, RawQueue=%zu, ConvertedQueue=%zu, TotalConverted=%llu, BackpressureHits=%llu",
				framesSinceLastLog,
				avgTimeUs / 1000.0,
				(minTimeUs == UINT64_MAX ? 0.0 : minTimeUs / 1000.0),
				maxTimeUs / 1000.0,
				rawQueueSize,
				convertedQueueSize,
				totalConvFrames,
				backpressureHits);

			framesSinceLastLog = 0;
			totalTimeUs = 0;
			maxTimeUs = 0;
			minTimeUs = UINT64_MAX;
			backpressureHits = 0;
			lastConversionLogTime = now;
		}
	}

	DbgLog((LOG_TRACE, 1, TEXT("ConversionWorker: Thread exiting")));
	DebugLog::Log("CONVERSION WORKER: Thread exiting");
	return 0;
}

size_t CBufferedLiveSourceVideoOutputPin::GetBufferingTarget() 
{
	// SAFETY: Check if timing parameters are initialized
	// This prevents crashes when GetBufferingTarget() is called before Initialize()
	if (m_timeScale == 0 || m_frameDurationTicks == 0)
	{
		// Fallback to safe default when not initialized
		// Use reasonable defaults: 3 frames for typical content
		DbgLog((LOG_TRACE, 1, TEXT("GetBufferingTarget(): Timing not initialized, using safe default of 3 frames")));
		return 3;
	}
	
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

size_t CBufferedLiveSourceVideoOutputPin::GetConvertedQueueSize()
{
	CAutoLock lock(&m_convertedQueueLock);
	return m_convertedSampleQueue.size();
}