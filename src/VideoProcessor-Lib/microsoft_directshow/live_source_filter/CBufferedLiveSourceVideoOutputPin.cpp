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
	m_hConvertedSemaphore = nullptr;
	
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

	m_hConvertedSemaphore = CreateSemaphore(nullptr, 0, 0x7fffffff, nullptr);
	if (!m_hConvertedSemaphore)
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
	// CRITICAL: Set inactive FIRST to stop all worker threads from accessing queues
	m_isActive.store(false, std::memory_order_release);
	
	// Signal conversion thread to shutdown
	if (m_hConversionShutdownEvent)
	{
		SetEvent(m_hConversionShutdownEvent);
	}
	
	// Signal delivery thread shutdown
	if (m_hShutdownEvent)
	{
		SetEvent(m_hShutdownEvent);
	}
	
	// Also signal semaphore to unblock delivery thread if waiting
	if (m_hConvertedSemaphore)
	{
		ReleaseSemaphore(m_hConvertedSemaphore, 1, nullptr);
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
		{
			CAutoLock lock(&m_convertedQueueLock);
			PurgeConvertedQueue();
		}
		{
			CAutoLock lock(&m_rawQueueLock);
			PurgeQueue();
		}
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

	if (m_hConvertedSemaphore)
	{
		CloseHandle(m_hConvertedSemaphore);
		m_hConvertedSemaphore = nullptr;
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

		// Update state atomics
		m_isActive.store(true, std::memory_order_release);
		m_isBuffering.store(true, std::memory_order_release);
		
		// Reset auto-purge timing state for clean startup
		{
			CAutoLock stateLock(&m_stateLock);
			m_lastAutoPurgeTime = 0;
			m_bufferingExitTime = 0;
			m_lastSeenFrameCounter = 0;
		}
		
		DebugLog::Log("Active(): Set m_isActive=true, m_isBuffering=true, reset timing state");

		// Log ASYNC conversion approach
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Active() - ASYNC conversion architecture:")));
		DbgLog((LOG_TRACE, 1, TEXT("  Raw frames → Conversion Worker (OFF critical path) → Pre-Converted Queue → Delivery Thread → MadVR")));
		DbgLog((LOG_TRACE, 1, TEXT("  Benefit: MadVR gets 100%% of frame time (conversion happens in parallel)")));
		DbgLog((LOG_TRACE, 1, TEXT("  Result: Zero conversion latency on delivery path")));
		
		DebugLog::Log("Active(): ASYNC architecture - Raw->Convert->Queue->Deliver->MadVR with queue size %zu", m_frameQueueMaxSize);
		
		// SAFETY: Ensure all events are created before starting threads
		if (!m_hConversionShutdownEvent || !m_hFrameAvailableEvent || !m_hConvertedSemaphore)
		{
			DbgLog((LOG_ERROR, 1, TEXT("Active(): Critical events not initialized")));
			DebugLog::Log("Active(): CRITICAL EVENTS NOT INITIALIZED - ConvShutdown=%p, FrameAvailable=%p, ConvertedAvailable=%p",
				m_hConversionShutdownEvent, m_hFrameAvailableEvent, m_hConvertedSemaphore);
			m_isActive.store(false, std::memory_order_release);
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
			m_isActive.store(false, std::memory_order_release);
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
			
			
			m_isActive.store(false, std::memory_order_release);
			return E_FAIL;
		}

		DebugLog::Log("Active(): Both threads created successfully");

		// MINIMAL STARTUP SYNC: Just ensure threads are created, no artificial delays
		// Threads will synchronize naturally through events and queues
		
		// Ensure both queues start empty and clean (no sleep needed)
		size_t purgedRaw = 0, purgedConverted = 0;
		{
			CAutoLock lock(&m_rawQueueLock);
			// Raw queue should already be empty, but ensure it
			while (!m_videoFrameQueue.empty())
			{
				VideoFrame popFrame = m_videoFrameQueue.front();
				popFrame.SourceBufferRelease();
				m_videoFrameQueue.pop_front();
				++purgedRaw;
			}
		}

		{
			CAutoLock lock(&m_convertedQueueLock);
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

		// Kick conversion thread once so it observes the fresh startup state.
		SetEvent(m_hFrameAvailableEvent);
		
		DebugLog::Log("Active(): Signaled conversion thread to start, activation complete");

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

		// CRITICAL: Set inactive FIRST before signaling shutdown
		// This ensures worker threads stop accessing queues immediately
		m_isActive.store(false, std::memory_order_release);

		// Signal shutdown events AFTER setting inactive
		if (m_hConversionShutdownEvent)
			SetEvent(m_hConversionShutdownEvent);
		
		if (m_hShutdownEvent)
			SetEvent(m_hShutdownEvent);
		
		// Signal semaphore to unblock delivery thread if waiting
		if (m_hConvertedSemaphore)
			ReleaseSemaphore(m_hConvertedSemaphore, 1, nullptr);
		
		// Signal frame available event to unblock conversion thread if waiting
		if (m_hFrameAvailableEvent)
			SetEvent(m_hFrameAvailableEvent);

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

		// Purge queues AFTER threads have exited
		{
			CAutoLock rawLock(&m_rawQueueLock);
			PurgeQueue();
		}
		{
			CAutoLock convLock(&m_convertedQueueLock);
			PurgeConvertedQueue();
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
	// Check active state (atomic, no lock needed)
	if (!m_isActive.load(std::memory_order_acquire))
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
	bool triggerRecovery = false;

	// Check for discontinuity (needs state lock for m_lastSeenFrameCounter)
	{
		CAutoLock stateLock(&m_stateLock);
		
		if (m_lastSeenFrameCounter > 0 && !m_isBuffering.load(std::memory_order_acquire))
		{
			const bool largeGap     = (newCounter > m_lastSeenFrameCounter) && ((newCounter - m_lastSeenFrameCounter) > 10);
			const bool counterReset = (newCounter < m_lastSeenFrameCounter);

			if (largeGap || counterReset)
			{
				DbgLog((LOG_TRACE, 1, TEXT("OnVideoFrame(): DISCONTINUITY DETECTED - triggering startup-like recovery")));
				DebugLog::Log("OnVideoFrame: Frame counter discontinuity detected (last=%llu, new=%llu) - triggering recovery", m_lastSeenFrameCounter, newCounter);
				triggerRecovery = true;
			}
		}
		
		// Update counter for next frame
		m_lastSeenFrameCounter = newCounter;
	}

	// Handle recovery if needed (purge both queues)
	if (triggerRecovery)
	{
		size_t purgedRaw = 0;
		{
			CAutoLock rawLock(&m_rawQueueLock);
			while (!m_videoFrameQueue.empty())
			{
				VideoFrame oldFrame = m_videoFrameQueue.front();
				oldFrame.SourceBufferRelease();
				m_videoFrameQueue.pop_front();
				++purgedRaw;
			}
		}

		size_t purgedConverted = 0;
		{
			CAutoLock convLock(&m_convertedQueueLock);
			while (!m_convertedSampleQueue.empty())
			{
				IMediaSample* pSample = m_convertedSampleQueue.front();
				m_convertedSampleQueue.pop_front();
				if (pSample) pSample->Release();
				++purgedConverted;
			}
		}

		// Enter buffering
		m_isBuffering.store(true, std::memory_order_release);
		{
			CAutoLock stateLock(&m_stateLock);
			m_lastSeenFrameCounter = 0;
		}
		
		DebugLog::Log("OnVideoFrame: Recovery complete - buffering enabled, purged %zu raw + %zu converted frames", 
			purgedRaw, purgedConverted);
	}

	// Add frame to raw queue
	{
		CAutoLock rawLock(&m_rawQueueLock);
		
		// Simple overflow protection - drop oldest if queue too full
		if (m_videoFrameQueue.size() >= m_frameQueueMaxSize)
		{
			VideoFrame oldFrame = m_videoFrameQueue.front();
			oldFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
			
			DebugLog::Log("OnVideoFrame: Raw queue OVERFLOW - dropped frame #%llu, size=%ze/%ze", 
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
					CAutoLock convLock(&m_convertedQueueLock);
					convertedSize = m_convertedSampleQueue.size();
				}
				
				DebugLog::Log("OnVideoFrame: Raw queue BACKING UP (raw=%zu/%ze, converted=%ze, buffering=%d, convFrames=%llu)",
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
        CAutoLock rawLock(&m_rawQueueLock);

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
    }

    // Reset simple proactive state only
    m_recentDeliveryFailures.store(0, std::memory_order_relaxed);
    m_lastQueueWarning = 0;
	
	DebugLog::Log("SetFrameQueueMaxSize: Queue size changed, reset failure counters");
	
	SetEvent(m_hFrameAvailableEvent);
	
	// Deliver new segment if active
	if (IsConnected() && m_isActive.load(std::memory_order_acquire))
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
	
	// Purge raw frames
	{
		CAutoLock rawLock(&m_rawQueueLock);
		
		size_t purgedFrames = 0;
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++purgedFrames;
		}
		
		DebugLog::Log("Reset(): Purged %zu raw frames from HDMI resync", purgedFrames);
	}

	// Wake conversion thread so it re-checks state immediately after reset.
	if (m_hFrameAvailableEvent)
		SetEvent(m_hFrameAvailableEvent);
	
	// Purge converted sample queue
	{
		CAutoLock convLock(&m_convertedQueueLock);
		
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
	m_frameCounter = 0;
	m_previousFrameCounter = 0;
	m_frameCounterOffset = 0;
	m_previousTimeStop = 0;
	m_startTimeOffset = 0;
	
	{
		CAutoLock stateLock(&m_stateLock);
		m_lastSeenFrameCounter = 0;
		m_lastAutoPurgeTime = 0;
		m_bufferingExitTime = 0;
	}
	
	// HDMI RESYNC: Enter buffering mode to rebuild queue state cleanly
	m_isBuffering.store(true, std::memory_order_release);
	
	DebugLog::Log("Reset(): Timeline reset, timing state cleared, buffering ENABLED for HDMI resync recovery");
	
	// Reset conversion metrics
	m_totalConversionTimeUs.store(0, std::memory_order_relaxed);
	m_conversionFrameCount.store(0, std::memory_order_relaxed);
	
	// Reset proactive state
	m_recentDeliveryFailures.store(0, std::memory_order_relaxed);
	m_lastQueueWarning = 0;
	
	DebugLog::Log("Reset(): Metrics and state reset complete");
	
	// Call base Reset for DirectShow signaling
	ALiveSourceVideoOutputPin::Reset();
	
	DebugLog::Log("CBufferedLiveSourceVideoOutputPin::Reset() - HDMI resync async queue reset completed");
}


size_t CBufferedLiveSourceVideoOutputPin::GetFrameQueueSize()
{
	CAutoLock rawLock(&m_rawQueueLock);
	return m_videoFrameQueue.size();
}


void CBufferedLiveSourceVideoOutputPin::PurgeQueue()
{
	// NOTE: Caller MUST hold m_rawQueueLock
	size_t purgedFrames = 0;

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
	
	if (purgedFrames > 0)
	{
		DbgLog((LOG_TRACE, 1, TEXT("PurgeQueue(): Purged %zu raw frames"), purgedFrames));
	}
}


void CBufferedLiveSourceVideoOutputPin::PurgeConvertedQueue()
{
	// NOTE: Caller MUST hold m_convertedQueueLock
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
	CAutoLock rawLock(const_cast<CCritSec*>(&m_rawQueueLock));

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

	// No hardware stop timestamp available
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
	return m_recentDeliveryFailures.load(std::memory_order_relaxed) > 2;
}


CBufferedLiveSourceVideoOutputPin::ProactiveQueueMetrics CBufferedLiveSourceVideoOutputPin::GetProactiveMetrics() const
{
	ProactiveQueueMetrics metrics = {};
	
	{
		CAutoLock rawLock(const_cast<CCritSec*>(&m_rawQueueLock));
		metrics.currentSize = m_videoFrameQueue.size();
	}
	
	{
		CAutoLock convLock(const_cast<CCritSec*>(&m_convertedQueueLock));
		metrics.convertedQueueSize = m_convertedSampleQueue.size();
	}
	
	metrics.maxSize = m_frameQueueMaxSize;
	metrics.proactiveTarget = GetProactiveQueueTarget();
	metrics.totalDropped = m_droppedFrameCount;
	metrics.recentFailures = m_recentDeliveryFailures.load(std::memory_order_relaxed);
	
	// Calculate average conversion time
	uint64_t convCount = m_conversionFrameCount.load(std::memory_order_relaxed);
	if (convCount > 0)
	{
		metrics.avgConversionTimeUs = m_totalConversionTimeUs.load(std::memory_order_relaxed) / convCount;
	}
	
	// Simple health check: queues below target and no recent failures
	metrics.isHealthy = (metrics.currentSize <= metrics.proactiveTarget) && 
	                    (metrics.recentFailures < 3) &&
	                    (metrics.convertedQueueSize < m_frameQueueMaxSize);
	
	return metrics;
}


DWORD CBufferedLiveSourceVideoOutputPin::ThreadProc()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	DebugLog::Log("DELIVERY THREAD: Started - event-driven with adaptive buffer management");

	// SAFETY: Validate handles before entering loop
	if (!m_hShutdownEvent || !m_hConvertedSemaphore)
	{
		DebugLog::Log("DELIVERY THREAD: Invalid event handles, exiting immediately");
		return 1;
	}

	HANDLE events[2] = { m_hShutdownEvent, m_hConvertedSemaphore };
	DWORD lastLatencyLogTime = 0;
	uint64_t framesSinceLastLog = 0;

	// Enhanced delivery performance tracking
	uint64_t deliverySuccessCount = 0;
	uint64_t deliveryFailureCount = 0;
	uint64_t totalDeliveryTimeUs = 0;
	uint64_t maxDeliveryTimeUs = 0;
	uint64_t minDeliveryTimeUs = UINT64_MAX;
	uint64_t bufferUnderrunCount = 0;

	// Delivery categorization (frame-rate aware)
	uint64_t instantDeliveryCount = 0;   // < 2ms
	uint64_t normalDeliveryCount = 0;    // >= 2ms AND <= 150% of frame interval
	uint64_t slowDeliveryCount = 0;      // > 150% of frame interval
	uint64_t totalDeliveryCount = 0;
	
	// 1-minute aggregation
	uint64_t instantDeliveryCount1Min = 0;
	uint64_t normalDeliveryCount1Min = 0;
	uint64_t slowDeliveryCount1Min = 0;
	uint64_t totalDeliveryCount1Min = 0;
	DWORD lastDeliveryStatsLogTime = GetTickCount();

	// Calculate frame interval thresholds (updated periodically from timing clock)
	uint64_t frameIntervalUs = 16667;  // Default: ~60fps = 16.667ms
	uint64_t slowDeliveryThresholdUs = 25000;  // 150% of 60fps frame = 25ms
	DWORD lastFrameIntervalUpdateTime = GetTickCount();

	while (true)
	{
		// SAFETY: Check shutdown before waiting
		if (!m_isActive.load(std::memory_order_acquire))
		{
			DebugLog::Log("DELIVERY THREAD: Not active before wait, exiting");
			break;
		}
		
		// Wait for converted samples or shutdown
		DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, INFINITE);

		if (waitResult == WAIT_OBJECT_0) // shutdown
		{
			DebugLog::Log("DELIVERY THREAD: Shutdown signal received");
			break;
		}

		if (waitResult != WAIT_OBJECT_0 + 1)
		{
			DebugLog::Log("DELIVERY THREAD: WaitForMultipleObjects FAILED result=%lu", waitResult);
			break;
		}

		if (!m_isActive.load(std::memory_order_acquire))
		{
			DebugLog::Log("DELIVERY THREAD: Not active, exiting");
			break;
		}

		// Update frame interval thresholds periodically (every 5 seconds)
		DWORD now = GetTickCount();
		if (now - lastFrameIntervalUpdateTime >= 5000)
		{
			if (m_frameDuration > 0)
			{
				// Convert REFERENCE_TIME (100ns units) to microseconds
				frameIntervalUs = m_frameDuration / 10;
				// Slow threshold: 150% of frame interval
				slowDeliveryThresholdUs = (frameIntervalUs * 150) / 100;
				
				DbgLog((LOG_TRACE, 1, TEXT("DELIVERY THREAD: Updated frame interval to %.2fms, slow threshold=%.2fms"),
					frameIntervalUs / 1000.0, slowDeliveryThresholdUs / 1000.0));
			}
			lastFrameIntervalUpdateTime = now;
		}

		// BUFFERING PHASE: do not deliver until we have enough converted samples
		if (m_isBuffering.load(std::memory_order_acquire))
		{
			size_t convertedQueueSize = 0;
			{
				CAutoLock convLock(&m_convertedQueueLock);
				convertedQueueSize = m_convertedSampleQueue.size();
			}

			// DYNAMIC BUFFERING: Use GetBufferingTarget() for fps-aware buffering
			const size_t bufferingTarget = GetBufferingTarget();

			if (convertedQueueSize < bufferingTarget)
			{
				continue; // Keep waiting for more samples
			}

			// Exit buffering - reset timeline state
			m_frameCounter = 0;
			m_previousFrameCounter = 0;
			m_frameCounterOffset = 0;
			m_previousTimeStop = 0;
			m_startTimeOffset = 0;
			m_isBuffering.store(false, std::memory_order_release);

			DebugLog::Log("DELIVERY THREAD: BUFFERING COMPLETE (%zu/%zu) - delivery starting",
				convertedQueueSize, bufferingTarget);
		}

		// MINIMUM BUFFER MAINTENANCE: Keep a minimum number of frames buffered during delivery
		// This prevents MadVR queue starvation and repeated frames
		const size_t minimumBufferLevel = GetBufferingTarget();

		for (;;)
		{
			if (!m_isActive.load(std::memory_order_acquire) || m_stopping.load(std::memory_order_acquire))
				break;

			size_t currentQueueSize = 0;

			// 1) Check queue has samples AND maintain minimum buffer
			{
				CAutoLock convLock(&m_convertedQueueLock);
				currentQueueSize = m_convertedSampleQueue.size();
				
				// Keep minimum buffer to prevent MadVR starvation
				if (currentQueueSize <= minimumBufferLevel)
					break; // Wait for more frames to build up buffer again
					
				if (currentQueueSize == 0)
					break; // empty queue, wait for conversion
			}

			// 2) Get the next sample (NO PEEKING, NO TIME CHECKS)
			IMediaSample* pSample = nullptr;
			{
				CAutoLock convLock(&m_convertedQueueLock);

				if (m_convertedSampleQueue.empty())
					break;

				pSample = m_convertedSampleQueue.front();
				m_convertedSampleQueue.pop_front();
			}

			if (!pSample)
				continue;

			// 3) DELIVER IMMEDIATELY - Let MadVR handle timing and buffering
			// MadVR is designed to buffer internally and will display frames at the right time
			// Our job is just to feed it continuously
			const auto deliveryStartTime = GetWallClockTime();
			HRESULT hr = Deliver(pSample);
			const auto deliveryEndTime = GetWallClockTime();
			const uint64_t deliveryTimeUs = (deliveryEndTime - deliveryStartTime) / 10;

			pSample->Release();

			// Track metrics
			totalDeliveryTimeUs += deliveryTimeUs;
			maxDeliveryTimeUs = std::max(maxDeliveryTimeUs, deliveryTimeUs);
			if (deliveryTimeUs > 0)
				minDeliveryTimeUs = std::min(minDeliveryTimeUs, deliveryTimeUs);

			++totalDeliveryCount;
			++totalDeliveryCount1Min;

			if (deliveryTimeUs < 2000)
			{
				++instantDeliveryCount;
				++instantDeliveryCount1Min;
			}
			else if (deliveryTimeUs <= slowDeliveryThresholdUs)
			{
				++normalDeliveryCount;
				++normalDeliveryCount1Min;
			}
			else
			{
				++slowDeliveryCount;
				++slowDeliveryCount1Min;
			}

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
				m_recentDeliveryFailures.store(0, std::memory_order_relaxed);
				++framesSinceLastLog;
				++deliverySuccessCount;
			}
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
	
	// SAFETY: Validate handles before entering loop
	if (!events[0] || !events[1])
	{
		DebugLog::Log("CONVERSION WORKER: Invalid event handles, exiting immediately");
		return 1;
	}

	// Conversion performance tracking
	DWORD lastConversionLogTime = 0;
	uint64_t framesSinceLastLog = 0;
	uint64_t totalTimeUs = 0;
	uint64_t maxTimeUs = 0;
	uint64_t minTimeUs = UINT64_MAX;
	uint64_t backpressureHits = 0;

	for (;;)
	{
		// SAFETY: Check shutdown before waiting
		if (!m_isActive.load(std::memory_order_acquire))
		{
			DebugLog::Log("CONVERSION WORKER: Not active before wait, exiting");
			break;
		}
		
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
		
		// SAFETY: Check active again after waking
		if (!m_isActive.load(std::memory_order_acquire))
		{
			DebugLog::Log("CONVERSION WORKER: Not active after wake, exiting");
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
				CAutoLock convLock(&m_convertedQueueLock);
				currentConvertedSize = m_convertedSampleQueue.size();
				
				// ENHANCED BACKPRESSURE: Multiple throttling levels based on queue fullness
				if (currentConvertedSize >= m_frameQueueMaxSize)
				{
					++backpressureHits;
					DebugLog::Log("CONVERSION WORKER: BACKPRESSURE hit - converted queue full (%zu/%zu), stopping conversion",
						currentConvertedSize, m_frameQueueMaxSize);
					break;  // Stop conversion completely
				}
				else if (currentConvertedSize >= (m_frameQueueMaxSize * 3) / 4)  // 75% full
				{
					// MODERATE BACKPRESSURE: Slow down conversion when queue is 75% full
					Sleep(2);  // 2ms pause to let delivery catch up
				}
				else if (currentConvertedSize >= (m_frameQueueMaxSize / 2))  // 50% full
				{
					// LIGHT BACKPRESSURE: Small pause when queue is 50% full
					Sleep(1);  // 1ms pause
				}
			}

			// Pop one raw frame.
			VideoFrame videoFrame{};
			bool hasFrame = false;
			size_t rawQueueSize = 0;

			{
				CAutoLock rawLock(&m_rawQueueLock);

				if (!m_isActive.load(std::memory_order_acquire))
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

			m_totalConversionTimeUs.fetch_add(convTimeUs, std::memory_order_relaxed);
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
				CAutoLock convLock(&m_convertedQueueLock);
				m_convertedSampleQueue.push_back(pSample);
			}

			// Signal delivery thread that a converted sample is available
			// SAFETY: Check handle is still valid before use
			if (m_hConvertedSemaphore && m_isActive.load(std::memory_order_acquire))
			{
				if (!ReleaseSemaphore(m_hConvertedSemaphore, 1, nullptr))
				{
					DebugLog::Log("CONVERSION WORKER: ReleaseSemaphore FAILED gle=%lu", GetLastError());
				}
			}
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
				CAutoLock rawLock(&m_rawQueueLock);
				rawQueueSize = m_videoFrameQueue.size();
			}
			{
				CAutoLock convLock(&m_convertedQueueLock);
				convertedQueueSize = m_convertedSampleQueue.size();
			}

			uint64_t avgTimeUs = (framesSinceLastLog > 0) ? (totalTimeUs / framesSinceLastLog) : 0;
			uint64_t totalConvFrames = m_conversionFrameCount.load(std::memory_order_relaxed);

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



size_t CBufferedLiveSourceVideoOutputPin::GetBufferingTarget() {

	size_t nominalTarget = (m_frameQueueMaxSize / 8);
	double fps = 60.0;
	if (m_timeScale > 0 && m_frameDurationTicks > 0) fps = (double)m_timeScale / (double)m_frameDurationTicks;

	// Calculate frame-rate appropriate target with MINIMUM of 3 frames
	// Low FPS (< 30fps like 23.976): Need fewer frames but more total time buffered
	// High FPS (>= 30fps): Need more frames for smooth playback
	size_t frames = fps > 30.0 ? nominalTarget + 1 : nominalTarget/2;
	
	// CRITICAL: Ensure minimum of 3 frames for MadVR buffering stability
	// At 23.976fps: 3 frames = ~125ms buffer (good for MadVR startup)
	// At 59.94fps: nominalTarget+1 = 5 frames = ~83ms buffer (already sufficient)
	frames = std::max<size_t>(3, frames);
	DebugLog::Log("GetBufferingTarget(): fps=%.2f, nominalTarget=%zu, finalTarget=%zu", 
		fps, nominalTarget, frames);
	
	return frames;
}


/*size_t CBufferedLiveSourceVideoOutputPin::GetBufferingTargetOLD()
{
	// SAFETY: Check if timing parameters are initialized
	// This prevents crashes when GetBufferingTarget() is called before Initialize()
	if (m_timeScale == 0 || m_frameDurationTicks == 0)
	{
		// Fallback to safe default when not initialized
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
*/
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
	
	// Purge raw frames
	{
		CAutoLock rawLock(&m_rawQueueLock);
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame oldFrame = m_videoFrameQueue.front();
			oldFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
		}
	}
	
	// Purge converted queue
	{
		CAutoLock convLock(&m_convertedQueueLock);
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
	m_isBuffering.store(true, std::memory_order_release);
	{
		CAutoLock stateLock(&m_stateLock);
		m_lastSeenFrameCounter = 0;
	}
	
	DebugLog::Log("OnBadTimestampDetected(): Recovery complete - buffering enabled, queues purged");
}

size_t CBufferedLiveSourceVideoOutputPin::GetConvertedQueueSize()
{
	CAutoLock convLock(&m_convertedQueueLock);
	return m_convertedSampleQueue.size();
}

REFERENCE_TIME CBufferedLiveSourceVideoOutputPin::NowStreamTime(CBaseFilter* f)
{

	CRefTime now;
	if (f) f->StreamTime(now); // baseclasses typically expose this
	return now;
}