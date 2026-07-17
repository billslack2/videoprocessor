/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <dvdmedia.h>
#include <guid.h>
#include <IMediaSideData.h>

#include "CBufferedLiveSourceVideoOutputPin.h"


CBufferedLiveSourceVideoOutputPin::CBufferedLiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr) :
	ALiveSourceVideoOutputPin(filter, pLock, phr)
{
	// Initialize all member variables BEFORE creating events/threads
	m_frameQueueMaxSize.store(32, std::memory_order_relaxed);  // Default safe value
	m_isActive.store(false, std::memory_order_relaxed);
	m_isBuffering.store(true, std::memory_order_relaxed);  // Start in buffering mode
	m_lastSeenFrameCounter = 0;
	m_totalConversionTimeUs.store(0, std::memory_order_relaxed);
	m_conversionFrameCount.store(0, std::memory_order_relaxed);
	m_sceneAwareDetectedCount.store(0, std::memory_order_relaxed);
	m_sceneAwareLateCandidateCount.store(0, std::memory_order_relaxed);
	m_sceneAwareCorrectionDropCount.store(0, std::memory_order_relaxed);
	m_sceneAwareCorrectionRepeatCount.store(0, std::memory_order_relaxed);
	m_scenePhasePpmUnits.store(0, std::memory_order_relaxed);
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

	// Create auto-reset event for converted sample availability
	// Auto-reset: automatically resets after delivery thread wakes
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

	// Signal converted available event to unblock delivery thread if waiting
	if (m_hConvertedAvailableEvent)
	{
		SetEvent(m_hConvertedAvailableEvent);
	}

	// Wait for conversion thread to exit
	if (m_hConversionThread)
	{
		DbgLog((LOG_TRACE, 1, TEXT("~CBufferedLiveSourceVideoOutputPin: Waiting for conversion thread to exit...")));
		// Do not close a live thread handle and then destroy the queues it may
		// still be using. Shutdown is signaled above, so this wait is expected to
		// complete promptly while guaranteeing object lifetime safety.
		WaitForSingleObject(m_hConversionThread, INFINITE);
		CloseHandle(m_hConversionThread);
		m_hConversionThread = nullptr;
	}

	// CAMThread's base destructor also calls Close(), but queue cleanup below
	// must not race the delivery thread during this destructor body.
	if (ThreadExists())
		Close();

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

	if (m_hConvertedAvailableEvent)
	{
		CloseHandle(m_hConvertedAvailableEvent);
		m_hConvertedAvailableEvent = nullptr;
	}


	DbgLog((LOG_TRACE, 1, TEXT("~CBufferedLiveSourceVideoOutputPin: Async conversion shutdown complete")));
}


HRESULT CBufferedLiveSourceVideoOutputPin::Active()
{
	if (m_frameQueueMaxSize.load(std::memory_order_relaxed) == 0)
		throw std::runtime_error("Call SetFrameQueueMaxSize() before activating the graph");

	DebugLog::Log("CBufferedLiveSourceVideoOutputPin::Active() - Starting activation with queue size %zu", m_frameQueueMaxSize.load(std::memory_order_relaxed));

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

		DebugLog::Log("Active(): ASYNC architecture - Raw->Convert->Queue->Deliver->MadVR with queue size %zu", m_frameQueueMaxSize.load(std::memory_order_relaxed));

		// SAFETY: Ensure all events are created before starting threads
		if (!m_hConversionShutdownEvent || !m_hFrameAvailableEvent || !m_hConvertedAvailableEvent)
		{
			DbgLog((LOG_ERROR, 1, TEXT("Active(): Critical events not initialized")));
			DebugLog::Log("Active(): CRITICAL EVENTS NOT INITIALIZED - ConvShutdown=%p, FrameAvailable=%p, ConvertedAvailable=%p",
				m_hConversionShutdownEvent, m_hFrameAvailableEvent, m_hConvertedAvailableEvent);
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
				IMediaSample* pSample = m_convertedSampleQueue.front().sample;
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

		// Signal converted available event to unblock delivery thread if waiting
		if (m_hConvertedAvailableEvent)
			SetEvent(m_hConvertedAvailableEvent);

		// Signal frame available event to unblock conversion thread if waiting
		if (m_hFrameAvailableEvent)
			SetEvent(m_hFrameAvailableEvent);

		// Wait for conversion thread to exit FIRST
		if (m_hConversionThread)
		{
			DbgLog((LOG_TRACE, 1, TEXT("Inactive(): Waiting for conversion thread to exit...")));
			// Closing the handle while the worker is still running is unsafe: the
			// worker can touch this pin and its queues after Inactive() returns.
			WaitForSingleObject(m_hConversionThread, INFINITE);
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
			const bool largeGap = (newCounter > m_lastSeenFrameCounter) && ((newCounter - m_lastSeenFrameCounter) > 10);
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
				IMediaSample* pSample = m_convertedSampleQueue.front().sample;
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

		if (purgedConverted > 0 && m_hFrameAvailableEvent)
			SetEvent(m_hFrameAvailableEvent);

		DebugLog::Log("OnVideoFrame: Recovery complete - buffering enabled, purged %zu raw + %zu converted frames",
			purgedRaw, purgedConverted);
	}

	// Add frame to raw queue
	{
		CAutoLock rawLock(&m_rawQueueLock);
		const size_t queueMaxSize = m_frameQueueMaxSize.load(std::memory_order_relaxed);

		// Simple overflow protection - drop oldest if queue too full
		if (m_videoFrameQueue.size() >= queueMaxSize)
		{
			VideoFrame oldFrame = m_videoFrameQueue.front();
			oldFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);

			DebugLog::Log("OnVideoFrame: Raw queue OVERFLOW - dropped frame #%llu, size=%ze/%ze",
				oldFrame.GetCounter(), m_videoFrameQueue.size(), queueMaxSize);
		}

		// Add new frame
		videoFrame.SourceBufferAddRef();
		m_videoFrameQueue.push_back(videoFrame);

		// DIAGNOSTIC: Log when raw queue is backing up
		if (m_videoFrameQueue.size() >= (queueMaxSize * 3) / 4)  // 75% threshold
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

				DebugLog::Log("OnVideoFrame: Raw queue BACKING UP (raw=%zu/%zu, converted=%zu, buffering=%d, convFrames=%llu)",
					m_videoFrameQueue.size(), queueMaxSize, convertedSize,
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
		m_frameQueueMaxSize.load(std::memory_order_relaxed), frameQueueMaxSize));
	DebugLog::Log("SetFrameQueueMaxSize: Changing queue size from %zu to %zu",
		m_frameQueueMaxSize.load(std::memory_order_relaxed), frameQueueMaxSize);

	{
		CAutoLock rawLock(&m_rawQueueLock);

		m_frameQueueMaxSize.store(frameQueueMaxSize, std::memory_order_release);

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
				m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
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


LONG CBufferedLiveSourceVideoOutputPin::GetAllocatorBufferCount() const
{
	// Keep a few samples of headroom beyond the converted queue without
	// allowing a user queue setting to recreate the former 128-buffer spike.
	const size_t queueSize = m_frameQueueMaxSize.load(std::memory_order_relaxed);
	const size_t recommended = std::min<size_t>(48, std::max<size_t>(8, queueSize + 4));
	return static_cast<LONG>(recommended);
}


void CBufferedLiveSourceVideoOutputPin::SetSceneAwareTimingCorrection(bool enabled)
{
	const bool wasEnabled = m_sceneAwareTimingCorrection.exchange(enabled, std::memory_order_acq_rel);
	if (wasEnabled == enabled)
		return;

	// The conversion worker owns its signature buffer.  A generation change lets
	// it discard that buffer at a frame boundary without taking another lock.
	m_sceneDetectorGeneration.fetch_add(1, std::memory_order_release);
	if (!enabled)
		m_scenePhasePpmUnits.store(0, std::memory_order_release);
	DebugLog::Log("SCENE-AWARE CORRECTION: %s", enabled ? "enabled" : "disabled");
}


void CBufferedLiveSourceVideoOutputPin::Reset()
{

	if (true) {
	DebugLog::Log("CBufferedLiveSourceVideoOutputPin::Reset() - HDMI resync async queue reset starting");
	m_sceneDetectorGeneration.fetch_add(1, std::memory_order_release);

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
			IMediaSample* pSample = m_convertedSampleQueue.front().sample;
			m_convertedSampleQueue.pop_front();
			if (pSample)
			{
				pSample->Release();
				++purgedSamples;
			}
		}

		DebugLog::Log("Reset(): Purged %zu pre-converted samples from HDMI resync", purgedSamples);
	}
	}

	// Clear pending timestamp history for CLOCK_SMART modes
	ClearPendingTimestamps();

	{
		CAutoLock stateLock(&m_stateLock);
		m_lastSeenFrameCounter = 0;
		m_lastAutoPurgeTime = 0;
		m_bufferingExitTime = 0;
		m_lastSceneAwareCorrectionTime.store(0, std::memory_order_relaxed);
		m_lastCorrectedSceneEventId.store(0, std::memory_order_relaxed);
		m_scenePhasePpmUnits.store(0, std::memory_order_relaxed);
	}

	// HDMI RESYNC: Enter buffering mode to rebuild queue state cleanly
	m_isBuffering.store(true, std::memory_order_release);

	// Wake conversion after freeing converted queue space and restoring reset state.
	if (m_hFrameAvailableEvent)
		SetEvent(m_hFrameAvailableEvent);

	DebugLog::Log("Reset(): Timeline reset, timing state cleared, buffering ENABLED for HDMI resync recovery");

	// Reset conversion metrics
	m_totalConversionTimeUs.store(0, std::memory_order_relaxed);
	m_conversionFrameCount.store(0, std::memory_order_relaxed);
	m_sceneAwareCorrectionDropCount.store(0, std::memory_order_relaxed);
	m_sceneAwareCorrectionRepeatCount.store(0, std::memory_order_relaxed);
	m_sceneAwareDetectedCount.store(0, std::memory_order_relaxed);
	m_sceneAwareLateCandidateCount.store(0, std::memory_order_relaxed);

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
	m_droppedFrameCount.fetch_add(purgedFrames, std::memory_order_relaxed);

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
		IMediaSample* pSample = m_convertedSampleQueue.front().sample;
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
	return (m_frameQueueMaxSize.load(std::memory_order_relaxed) * 3) / 5;
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

	metrics.maxSize = m_frameQueueMaxSize.load(std::memory_order_relaxed);
	metrics.proactiveTarget = GetProactiveQueueTarget();
	metrics.totalDropped = m_droppedFrameCount.load(std::memory_order_relaxed);
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
		(metrics.convertedQueueSize < metrics.maxSize);

	return metrics;
}


DWORD CBufferedLiveSourceVideoOutputPin::ThreadProc()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	DebugLog::Log("DELIVERY THREAD: Started - event-driven with adaptive buffer management");

	// SAFETY: Validate handles before entering loop
	if (!m_hShutdownEvent || !m_hConvertedAvailableEvent)
	{
		DebugLog::Log("DELIVERY THREAD: Invalid event handles, exiting immediately");
		return 1;
	}

	HANDLE events[2] = { m_hShutdownEvent, m_hConvertedAvailableEvent };
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
	DWORD lastLateBindMissLogTime = 0;
	uint64_t lateBindMissesSinceLastLog = 0;
	DWORD lastDeliveryFailureLogTime = 0;
	uint64_t deliveryFailuresSinceLastLog = 0;

	// Calculate frame interval thresholds (updated periodically from timing clock)
	uint64_t frameIntervalUs = 16667;  // Default: ~60fps = 16.667ms
	uint64_t slowDeliveryThresholdUs = 25000;  // 150% of 60fps frame = 25ms
	DWORD lastFrameIntervalUpdateTime = GetTickCount();
	// Delivery-thread-only state.  The retained sample is used solely for a
	// rare scene-boundary repeat; no queue lock is held while it is copied or
	// delivered.  The offset keeps all subsequent media timestamps monotonic
	// after an inserted frame.
	IMediaSample* lastDeliveredSample = nullptr;
	REFERENCE_TIME syntheticTimestampOffset = 0;

	auto deliverTracked = [&](IMediaSample* sample) -> HRESULT
	{
		const auto deliveryStartTime = GetWallClockTime();
		const HRESULT result = Deliver(sample);
		const auto deliveryEndTime = GetWallClockTime();
		const uint64_t deliveryTimeUs = (deliveryEndTime - deliveryStartTime) / 10;

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

		if (FAILED(result))
		{
			m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
			++m_recentDeliveryFailures;
			++deliveryFailureCount;
			++deliveryFailuresSinceLastLog;
			const DWORD failureNow = GetTickCount();
			if (lastDeliveryFailureLogTime == 0 || failureNow - lastDeliveryFailureLogTime >= 5000)
			{
				DebugLog::Log("DELIVERY THREAD: Deliver() failed %llu time(s) in the last interval; last hr=0x%08x (consecutive=%u)",
					deliveryFailuresSinceLastLog, result, m_recentDeliveryFailures.load());
				deliveryFailuresSinceLastLog = 0;
				lastDeliveryFailureLogTime = failureNow;
			}
		}
		else
		{
			m_recentDeliveryFailures.store(0, std::memory_order_relaxed);
			++framesSinceLastLog;
			++deliverySuccessCount;
		}
		return result;
	};

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

		// Reset() re-enters buffering before new timestamps are produced.  Drop
		// delivery-thread-only synthetic state at that boundary so an old repeat
		// cannot shift a new DirectShow segment.
		if (m_isBuffering.load(std::memory_order_acquire) && syntheticTimestampOffset != 0)
		{
			syntheticTimestampOffset = 0;
			if (lastDeliveredSample)
			{
				lastDeliveredSample->Release();
				lastDeliveredSample = nullptr;
			}
		}

		// BUFFERING PHASE: do not deliver until we have enough converted samples
		if (m_isBuffering.load(std::memory_order_acquire))
		{
			bool freedConvertedQueueSpace = false;
			size_t convertedQueueSize = 0;

			// DYNAMIC BUFFERING: Use GetBufferingTarget() for fps-aware buffering
			const size_t bufferingTarget = GetBufferingTarget();

			const size_t maxFrames = std::max(bufferingTarget,
				std::min(m_frameQueueMaxSize.load(std::memory_order_relaxed), bufferingTarget + std::max<size_t>(2, bufferingTarget / 2)));

			size_t q = 0;
			{
				CAutoLock lock(&m_convertedQueueLock);
				q = m_convertedSampleQueue.size();

				if (q > maxFrames)
				{
					const size_t toDrop = q - maxFrames;
					for (size_t i = 0; i < toDrop; ++i)
					{
						IMediaSample* s = m_convertedSampleQueue.front().sample;
						m_convertedSampleQueue.pop_front();
						if (s) s->Release();
					}
					++bufferUnderrunCount; // or better: add a new bufferOverrunDropCount
					DebugLog::Log("DELIVERY THREAD: MAX BUFFER hit: dropped %zu old frames (q=%zu max=%zu)",
						toDrop, q, maxFrames);
					freedConvertedQueueSpace = true;
				}

				convertedQueueSize = m_convertedSampleQueue.size();
			}

			if (freedConvertedQueueSpace && m_hFrameAvailableEvent)
				SetEvent(m_hFrameAvailableEvent);


			if (convertedQueueSize < bufferingTarget)
			{
				continue; // Keep waiting for more samples
			}

			// Exit buffering without resetting timing state.  The queued samples
			// were already timestamped by the conversion worker; resetting the
			// origin here would make the next converted sample repeat media time 0.
			m_isBuffering.store(false, std::memory_order_release);

			DebugLog::Log("DELIVERY THREAD: BUFFERING COMPLETE (%zu/%zu) - delivery starting",
				convertedQueueSize, bufferingTarget);
		}

		// DRAIN LOOP: With auto-reset event, drain queue completely (no need to keep frames)
		// We use the pending timestamp history for late-binding instead
		for (;;)
		{
			if (!m_isActive.load(std::memory_order_acquire) || m_stopping.load(std::memory_order_acquire))
				break;

			// Pop one sample under lock
			IMediaSample* pSample = nullptr;
			bool isSafeCorrectionPoint = false;
			uint64_t sceneEventId = 0;
			SceneCorrectionAction sceneCorrectionAction = SceneCorrectionAction::None;
			bool freedConvertedQueueSpace = false;
			{
				CAutoLock convLock(&m_convertedQueueLock);

				if (m_convertedSampleQueue.empty())
					break;  // No more samples, wait for more

				const ConvertedSample convertedSample = m_convertedSampleQueue.front();
				pSample = convertedSample.sample;
				isSafeCorrectionPoint = convertedSample.isSafeCorrectionPoint;
				sceneEventId = convertedSample.sceneEventId;
				sceneCorrectionAction = convertedSample.sceneCorrectionAction;
				m_convertedSampleQueue.pop_front();
				freedConvertedQueueSpace = true;
			}

			if (freedConvertedQueueSpace && m_hFrameAvailableEvent)
				SetEvent(m_hFrameAvailableEvent);

			if (!pSample)
				continue;

			// Get timestamps for late-binding
			bool usedLateBoundStop = false;
			REFERENCE_TIME currentStart = 0, currentStop = 0;
			pSample->GetTime(&currentStart, &currentStop);

			// LATE BIND STOP TIME: Search pending timestamp history for best-fit next frame
			if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
				m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2)
			{
				// Calculate the theoretical stop time (next frame's start time)
				REFERENCE_TIME theoreticalStop = currentStart + m_frameDuration;

				// Search tolerance: 10% of frame duration
				static const double SEARCH_TOLERANCE_PERCENT = 0.10; // 10%
				const REFERENCE_TIME searchTolerance = (REFERENCE_TIME)(m_frameDuration * SEARCH_TOLERANCE_PERCENT);

				// Search the pending timestamp history
				REFERENCE_TIME bestStart = FindNextPendingTimestamp(currentStart, theoreticalStop, searchTolerance);

				if (bestStart != REFERENCE_TIME_INVALID)
				{
					// Found a good match - use the real next frame's start time as our stop time
					REFERENCE_TIME newStop = bestStart;

					// Final safety: stop must be after start
					if (newStop <= currentStart)
					{
						newStop = currentStart + m_frameDuration;
					}

					pSample->SetTime(&currentStart, &newStop);
					usedLateBoundStop = true;

					// Track success rate for periodic logging
					static uint64_t lateBindSuccessCount = 0;
					static uint64_t lateBindTotalCount = 0;
					static uint64_t lastLateBindLogCount = 0;
					++lateBindSuccessCount;
					++lateBindTotalCount;

					// Log summary every 600 frames (~10 seconds at 60fps, ~25 seconds at 24fps)
					if (lateBindTotalCount - lastLateBindLogCount >= 600)
					{
						double successRate = (lateBindSuccessCount * 100.0) / lateBindTotalCount;
						REFERENCE_TIME actualDelta = abs(bestStart - theoreticalStop);
						DebugLog::Log("LATE-BIND STATS: %llu/%llu (%.1f%%) success, last delta=%.3fms",
							lateBindSuccessCount, lateBindTotalCount, successRate, actualDelta / 10000.0);
						lastLateBindLogCount = lateBindTotalCount;
					}
				}
				else
				{
					// A timestamp miss often occurs in bursts while the graph is
					// recovering.  Logging every frame can then make the timing issue
					// worse, so report a bounded summary instead.
					++lateBindMissesSinceLastLog;
					const DWORD now = GetTickCount();
					if (lastLateBindMissLogTime == 0 || now - lastLateBindMissLogTime >= 5000)
					{
						DebugLog::Log("LATE-BIND MISS: %llu miss(es) in the last interval; target=%.3fms within ±%.3fms (searching pending history)",
							lateBindMissesSinceLastLog, theoreticalStop / 10000.0, searchTolerance / 10000.0);
						lateBindMissesSinceLastLog = 0;
						lastLateBindMissLogTime = now;
					}
				}
			}

			// Scene-only timing work is deliberately skipped when the feature is
			// disabled.  When enabled, a confirmed scene boundary may consume one
			// accumulated whole-frame phase correction.  Drops do not alter the
			// timeline; repeats advance a delivery-thread offset so all timestamps
			// remain monotonic after the inserted frame.
			if (m_sceneAwareTimingCorrection.load(std::memory_order_acquire))
			{
				const REFERENCE_TIME nowStreamTime = NowStreamTime(m_pFilter);
				const REFERENCE_TIME lateThreshold = std::max<REFERENCE_TIME>(1, (m_frameDuration * 3) / 4);
				// StreamTime() and sample timestamps must be in the same running
				// timeline before they can be compared.  After a graph restart the
				// filter clock can briefly be unavailable or belong to the previous
				// segment.  Never treat that mismatch as a scene correction.
				constexpr REFERENCE_TIME kMaxSaneSceneLateness =
					2 * REFERENCE_TIME_TICKS_PER_SECOND;
				const bool saneStreamComparison =
					nowStreamTime != REFERENCE_TIME_INVALID &&
					currentStart >= 0 &&
					nowStreamTime >= currentStart &&
					(nowStreamTime - currentStart) <= kMaxSaneSceneLateness;
				const DWORD correctionNow = GetTickCount();
				// Scene events are explicitly identified by the conversion worker.  A
				// correction can occur at most once for each event, while the short
				// interval guard prevents two genuinely distinct cuts from causing
				// back-to-back corrections on adjacent delivery iterations.
				const bool correctionIntervalElapsed =
					(correctionNow - m_lastSceneAwareCorrectionTime.load(std::memory_order_relaxed)) >= 250;
				const bool newSceneEvent = sceneEventId != 0 &&
					sceneEventId != m_lastCorrectedSceneEventId.load(std::memory_order_relaxed);
				const bool lateAtSafePoint = saneStreamComparison && isSafeCorrectionPoint &&
					(nowStreamTime - currentStart) > lateThreshold;
				bool sceneCorrectionApplied = false;
				if (lateAtSafePoint)
					m_sceneAwareLateCandidateCount.fetch_add(1, std::memory_order_relaxed);

				if (newSceneEvent && correctionIntervalElapsed && sceneCorrectionAction == SceneCorrectionAction::Drop)
				{
					m_lastSceneAwareCorrectionTime.store(correctionNow, std::memory_order_relaxed);
					m_lastCorrectedSceneEventId.store(sceneEventId, std::memory_order_relaxed);
					m_scenePhasePpmUnits.fetch_sub(static_cast<int64_t>(RATIONAL_TRIM_DENOMINATOR), std::memory_order_relaxed);
					m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
					m_sceneAwareCorrectionDropCount.fetch_add(1, std::memory_order_relaxed);
					DebugLog::Log("SCENE-AWARE CORRECTION: synthetic drop at scene boundary (event=%llu, phase=%lldppm)",
						sceneEventId, m_scenePhasePpmUnits.load(std::memory_order_relaxed));
					pSample->Release();
					continue;
				}

				if (newSceneEvent && correctionIntervalElapsed && sceneCorrectionAction == SceneCorrectionAction::Repeat && lastDeliveredSample)
				{
					const REFERENCE_TIME nominalFrameDuration = std::max<REFERENCE_TIME>(1, m_frameDuration);
					REFERENCE_TIME correctionFrameDuration = currentStop > currentStart ?
						(currentStop - currentStart) : nominalFrameDuration;
					// SMART late-binding can expose a transiently large interval while
					// the graph recovers; never let that turn one repeat into a large
					// timestamp jump.
					correctionFrameDuration = std::max<REFERENCE_TIME>(1,
						std::min(correctionFrameDuration, nominalFrameDuration * 2));
					const REFERENCE_TIME repeatStart = currentStart + syntheticTimestampOffset;
					const REFERENCE_TIME repeatStop = repeatStart + correctionFrameDuration;
					IMediaSample* repeatSample = nullptr;
					if (SUCCEEDED(CreateSyntheticRepeatSample(lastDeliveredSample, repeatStart, repeatStop, &repeatSample)))
					{
						const HRESULT repeatHr = deliverTracked(repeatSample);
						repeatSample->Release();
						if (SUCCEEDED(repeatHr))
						{
							syntheticTimestampOffset += correctionFrameDuration;
							m_scenePhasePpmUnits.fetch_add(static_cast<int64_t>(RATIONAL_TRIM_DENOMINATOR), std::memory_order_relaxed);
							m_lastSceneAwareCorrectionTime.store(correctionNow, std::memory_order_relaxed);
							m_lastCorrectedSceneEventId.store(sceneEventId, std::memory_order_relaxed);
							m_sceneAwareCorrectionRepeatCount.fetch_add(1, std::memory_order_relaxed);
							sceneCorrectionApplied = true;
							DebugLog::Log("SCENE-AWARE CORRECTION: synthetic repeat at scene boundary (event=%llu, phase=%lldppm)",
								sceneEventId, m_scenePhasePpmUnits.load(std::memory_order_relaxed));
						}
					}
				}

				if (lateAtSafePoint && newSceneEvent && correctionIntervalElapsed && !sceneCorrectionApplied)
				{
					m_lastSceneAwareCorrectionTime.store(correctionNow, std::memory_order_relaxed);
					m_lastCorrectedSceneEventId.store(sceneEventId, std::memory_order_relaxed);
					m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
					m_sceneAwareCorrectionDropCount.fetch_add(1, std::memory_order_relaxed);
					DebugLog::Log("SCENE-AWARE CORRECTION: dropped late frame at safe boundary (late=%.3fms, event=%llu)",
						(nowStreamTime - currentStart) / 10000.0, sceneEventId);
					pSample->Release();
					continue;
				}
			}

			// Apply any offset created by a prior synthetic repeat only after the
			// late-bind comparison above, which intentionally uses raw timestamps.
			if (syntheticTimestampOffset != 0)
			{
				REFERENCE_TIME deliveryStart = currentStart + syntheticTimestampOffset;
				REFERENCE_TIME deliveryStop = currentStop + syntheticTimestampOffset;
				pSample->SetTime(&deliveryStart, &deliveryStop);
			}

			// 4) DELIVER - Let MadVR handle timing and buffering
			HRESULT hr = deliverTracked(pSample);

			// Log CLOCK_SMART timing stats periodically
			static uint64_t smartLogCounter = 0;
			if ((m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
				m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2) &&
				++smartLogCounter % 600 == 0)
			{
				REFERENCE_TIME start = 0, stop = 0;
				pSample->GetTime(&start, &stop);
				REFERENCE_TIME duration = stop - start;
				DebugLog::Log("CLOCK_SMART%s: duration=%.3fms, start=%.3fms, stop=%.3fms, lateBound=%s",
					(m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2) ? "2" : "",
					duration / 10000.0, start / 10000.0, stop / 10000.0,
					usedLateBoundStop ? "YES" : "NO");
			}

			if (SUCCEEDED(hr) && m_sceneAwareTimingCorrection.load(std::memory_order_acquire))
			{
				pSample->AddRef();
				if (lastDeliveredSample)
					lastDeliveredSample->Release();
				lastDeliveredSample = pSample;
			}
			else if (!m_sceneAwareTimingCorrection.load(std::memory_order_acquire) && lastDeliveredSample)
			{
				lastDeliveredSample->Release();
				lastDeliveredSample = nullptr;
			}
			pSample->Release();
		}
	}

	DebugLog::Log("DELIVERY THREAD: Exiting");
	if (lastDeliveredSample)
		lastDeliveredSample->Release();
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
	DWORD lastSlowConversionLogTime = 0;
	uint64_t slowConversionsSinceLastLog = 0;
	uint64_t maxSlowConversionUs = 0;
	SceneDetectorState sceneDetectorState;
	uint64_t sceneDetectorGeneration = m_sceneDetectorGeneration.load(std::memory_order_acquire);

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
			const uint64_t currentSceneDetectorGeneration = m_sceneDetectorGeneration.load(std::memory_order_acquire);
			if (currentSceneDetectorGeneration != sceneDetectorGeneration)
			{
				sceneDetectorState = {};
				sceneDetectorGeneration = currentSceneDetectorGeneration;
			}
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
			}

			const size_t queueMaxSize = m_frameQueueMaxSize.load(std::memory_order_relaxed);
			if (currentConvertedSize >= queueMaxSize)
			{
				++backpressureHits;
				DebugLog::Log("CONVERSION WORKER: BACKPRESSURE hit - converted queue full (%zu/%zu), stopping conversion until delivery frees space",
					currentConvertedSize, queueMaxSize);
				break;  // Stop conversion until a frame arrives or delivery frees converted space.
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
				m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
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

			if (FAILED(hr) || hr == S_FRAME_NOT_RENDERED)
			{
				DebugLog::Log("CONVERSION WORKER: Frame conversion did not produce a sample for frame #%llu, hr=0x%08x",
					videoFrame.GetCounter(), hr);

				videoFrame.SourceBufferRelease();
				pSample->Release();
				m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
				continue;
			}

			// Release raw frame - we're done with it
			videoFrame.SourceBufferRelease();

			bool isSafeCorrectionPoint = false;
			uint64_t sceneEventId = 0;
			SceneCorrectionAction sceneCorrectionAction = SceneCorrectionAction::None;
			if (m_sceneAwareTimingCorrection.load(std::memory_order_acquire))
				isSafeCorrectionPoint = IsSafeSceneAwareCorrectionPoint(pSample, sceneDetectorState, sceneEventId, sceneCorrectionAction);

			// Pending timestamp history is only consumed by CLOCK_SMART/SMART2.
			// Publish it before enqueueing the sample so the delivery thread can
			// never consume a sample whose late-bind timestamp is not visible yet.
			// Avoid both operations for the other timing modes.
			if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
				m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2)
			{
				REFERENCE_TIME sampleStart = 0, sampleStop = 0;
				if (SUCCEEDED(pSample->GetTime(&sampleStart, &sampleStop)))
					RecordPendingTimestamp(sampleStart);
			}

			// Add converted sample to queue only after its timestamp history is
			// published. The delivery thread is then free to drain immediately.
			{
				CAutoLock convLock(&m_convertedQueueLock);
				m_convertedSampleQueue.push_back({ pSample, isSafeCorrectionPoint, sceneEventId, sceneCorrectionAction });
			}

			// Signal delivery thread that a converted sample is available
			// SAFETY: Check handle is still valid before use
			if (m_hConvertedAvailableEvent && m_isActive.load(std::memory_order_acquire))
			{
				SetEvent(m_hConvertedAvailableEvent);
			}
			++batchCount;

			// Slow conversions may occur in sustained bursts.  Do not make a
			// saturated CPU/disk path worse by logging once per frame.
			if (convTimeUs > 5000)  // > 5ms is unusual
			{
				++slowConversionsSinceLastLog;
				maxSlowConversionUs = std::max(maxSlowConversionUs, convTimeUs);
				const DWORD now = GetTickCount();
				if (lastSlowConversionLogTime == 0 || now - lastSlowConversionLogTime >= 5000)
				{
					DebugLog::Log("CONVERSION WORKER: %llu slow conversion(s) in the last interval; max=%.2fms, latest frame #%llu",
						slowConversionsSinceLastLog, maxSlowConversionUs / 1000.0, videoFrame.GetCounter());
					slowConversionsSinceLastLog = 0;
					maxSlowConversionUs = 0;
					lastSlowConversionLogTime = now;
				}
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


HRESULT CBufferedLiveSourceVideoOutputPin::CreateSyntheticRepeatSample(
	IMediaSample* source,
	REFERENCE_TIME start,
	REFERENCE_TIME stop,
	IMediaSample** repeatSample)
{
	if (!source || !repeatSample)
		return E_POINTER;
	*repeatSample = nullptr;

	IMediaSample* duplicate = nullptr;
	HRESULT hr = GetDeliveryBuffer(&duplicate, nullptr, nullptr, 0);
	if (FAILED(hr) || !duplicate)
		return FAILED(hr) ? hr : E_FAIL;

	BYTE* sourceData = nullptr;
	BYTE* duplicateData = nullptr;
	const long sourceLength = source->GetActualDataLength();
	if (sourceLength < 0 || FAILED(source->GetPointer(&sourceData)) || !sourceData ||
		FAILED(duplicate->GetPointer(&duplicateData)) || !duplicateData ||
		sourceLength > duplicate->GetSize())
	{
		duplicate->Release();
		return E_FAIL;
	}

	if (sourceLength > 0)
		memcpy(duplicateData, sourceData, static_cast<size_t>(sourceLength));
	duplicate->SetActualDataLength(sourceLength);
	duplicate->SetTime(&start, &stop);
	duplicate->SetSyncPoint(TRUE);
	duplicate->SetPreroll(FALSE);
	duplicate->SetDiscontinuity(FALSE);

	// Preserve HDR metadata on the rare synthetic sample without adding any
	// per-frame work to the normal path.  Side data is optional on DirectShow
	// samples, so an unavailable interface is not a correction failure.
	IMediaSideData* sourceSideData = nullptr;
	IMediaSideData* duplicateSideData = nullptr;
	if (SUCCEEDED(source->QueryInterface(__uuidof(IMediaSideData), reinterpret_cast<void**>(&sourceSideData))) &&
		SUCCEEDED(duplicate->QueryInterface(__uuidof(IMediaSideData), reinterpret_cast<void**>(&duplicateSideData))))
	{
		const GUID sideDataTypes[] = { IID_MediaSideDataHDRContentLightLevel, IID_MediaSideDataHDR };
		for (const GUID& type : sideDataTypes)
		{
			const BYTE* data = nullptr;
			size_t size = 0;
			if (SUCCEEDED(sourceSideData->GetSideData(type, &data, &size)) && data && size > 0)
				duplicateSideData->SetSideData(type, data, size);
		}
	}
	if (duplicateSideData)
		duplicateSideData->Release();
	if (sourceSideData)
		sourceSideData->Release();

	*repeatSample = duplicate;
	return S_OK;
}


bool CBufferedLiveSourceVideoOutputPin::IsSafeSceneAwareCorrectionPoint(
	IMediaSample* sample,
	SceneDetectorState& state,
	uint64_t& sceneEventId,
	SceneCorrectionAction& correctionAction)
{
	sceneEventId = 0;
	correctionAction = SceneCorrectionAction::None;
	// The HDR and MadVR paths use P010.  Keep scene analysis on the converted
	// sample so the detector sees exactly the format that is delivered, while
	// avoiding any work for unsupported render formats.
	if (!sample || !IsEqualGUID(m_mediaType.subtype, MEDIASUBTYPE_P010) || !m_mediaType.pbFormat)
		return false;

	LONG width = 0;
	LONG height = 0;
	if (IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo2) &&
		m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER2))
	{
		const VIDEOINFOHEADER2* videoInfo = reinterpret_cast<const VIDEOINFOHEADER2*>(m_mediaType.pbFormat);
		width = videoInfo->bmiHeader.biWidth;
		height = videoInfo->bmiHeader.biHeight;
	}
	else if (IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo) &&
		m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER))
	{
		const VIDEOINFOHEADER* videoInfo = reinterpret_cast<const VIDEOINFOHEADER*>(m_mediaType.pbFormat);
		width = videoInfo->bmiHeader.biWidth;
		height = videoInfo->bmiHeader.biHeight;
	}

	if (width <= 0 || height == 0)
		return false;

	const size_t lumaWidth = static_cast<size_t>(width);
	const size_t lumaHeight = static_cast<size_t>(height > 0 ? height : -height);
	const size_t lumaBytes = lumaWidth * lumaHeight * sizeof(uint16_t);
	if (sample->GetActualDataLength() < lumaBytes)
		return false;

	BYTE* data = nullptr;
	if (FAILED(sample->GetPointer(&data)) || !data)
		return false;

	SceneSignature current;
	uint64_t totalLuma = 0;
	size_t darkSampleCount = 0;
	for (size_t row = 0; row < SceneSignature::ROWS; ++row)
	{
		const size_t y = ((row * 2 + 1) * lumaHeight) / (SceneSignature::ROWS * 2);
		const uint16_t* line = reinterpret_cast<const uint16_t*>(data + (y * lumaWidth * sizeof(uint16_t)));
		for (size_t column = 0; column < SceneSignature::COLUMNS; ++column)
		{
			const size_t x = ((column * 2 + 1) * lumaWidth) / (SceneSignature::COLUMNS * 2);
			const uint16_t luma = static_cast<uint16_t>(line[x] >> 6);
			const size_t index = row * SceneSignature::COLUMNS + column;
			current.luma[index] = luma;
			current.histogram[std::min<size_t>(luma / 64, SceneSignature::HISTOGRAM_BINS - 1)]++;
			totalLuma += luma;
			if (luma <= 112)
				++darkSampleCount;
		}
	}

	current.averageLuma = static_cast<uint32_t>(totalLuma / current.luma.size());
	current.valid = true;
	// Keep the existing rational PPM timestamp trim untouched.  This separate
	// signed accumulator expresses the residual phase in microframes: a
	// negative applied PPM makes timestamps run faster, so it accumulates toward
	// a synthetic drop; a positive PPM accumulates toward a synthetic repeat.
	const int64_t appliedPpm = static_cast<int64_t>(m_currentRationalTrimNumerator) -
		static_cast<int64_t>(RATIONAL_TRIM_DENOMINATOR);
	const int64_t phase = m_scenePhasePpmUnits.fetch_add(-appliedPpm, std::memory_order_relaxed) - appliedPpm;
	constexpr int64_t kMaximumPhasePpmUnits = 8LL * 1000000LL;
	if (phase > kMaximumPhasePpmUnits || phase < -kMaximumPhasePpmUnits)
	{
		const int64_t boundedPhase = phase > kMaximumPhasePpmUnits ? kMaximumPhasePpmUnits :
			(phase < -kMaximumPhasePpmUnits ? -kMaximumPhasePpmUnits : phase);
		m_scenePhasePpmUnits.store(boundedPhase, std::memory_order_relaxed);
	}
	const size_t sampleCount = current.luma.size();
	const bool nearBlack = current.averageLuma <= 96 && darkSampleCount >= (sampleCount * 9) / 10;

	struct Difference
	{
		uint32_t averageLumaDifference = 0;
		uint32_t changedSampleCount = 0;
		uint32_t histogramDistance = 1000; // 0 = identical, 1000 = no overlap
	};

	const auto compare = [sampleCount](const SceneSignature& a, const SceneSignature& b,
		uint16_t changeThreshold) -> Difference
	{
		Difference result;
		uint64_t totalDifference = 0;
		for (size_t i = 0; i < sampleCount; ++i)
		{
			const uint16_t difference = static_cast<uint16_t>(abs(
				static_cast<int>(a.luma[i]) - static_cast<int>(b.luma[i])));
			totalDifference += difference;
			if (difference >= changeThreshold)
				++result.changedSampleCount;
		}
		result.averageLumaDifference = static_cast<uint32_t>(totalDifference / sampleCount);

		uint64_t intersection = 0;
		uint64_t bTotal = 0;
		for (size_t i = 0; i < SceneSignature::HISTOGRAM_BINS; ++i)
		{
			intersection += std::min(a.histogram[i], b.histogram[i]);
			bTotal += b.histogram[i];
		}
		if (bTotal > 0)
			result.histogramDistance = static_cast<uint32_t>(1000 - ((intersection * 1000) / bTotal));
		return result;
	};

	bool sceneEvent = false;
	if (state.previous.valid)
	{
		// Use a slightly lower per-sample threshold.  A soccer cut can keep a
		// similar green-field histogram even though the spatial content changes
		// substantially, so histogram distance must support the decision rather
		// than be a mandatory gate.
		const Difference immediate = compare(current, state.previous, 32);

		// A hard cut should produce one large change followed by a stable new
		// image. Keep a settling confirmation so a pan is not treated as a cut,
		// but allow normal motion in the new scene instead of requiring nearly
		// identical adjacent frames. The previous limits were too strict for
		// live sports cuts and caused real transitions to be missed.
		if (state.pendingHardCutValid)
		{
			const Difference settling = compare(current, state.pendingHardCut, 24);
			const bool settledAfterCandidate =
				settling.averageLumaDifference <= 64 &&
				settling.changedSampleCount <= (sampleCount * 50) / 100 &&
				settling.histogramDistance <= 140 &&
				(static_cast<uint64_t>(settling.averageLumaDifference) * 100 <=
					static_cast<uint64_t>(state.pendingInitialAverageLumaDifference) * 80) &&
				(static_cast<uint64_t>(settling.changedSampleCount) * 100 <=
					static_cast<uint64_t>(state.pendingInitialChangedSampleCount) * 85);

			if (settledAfterCandidate)
			{
				sceneEvent = true;
				state.pendingHardCutValid = false;
				state.pendingHardCutFrames = 0;
			}
			else if (++state.pendingHardCutFrames >= 4)
			{
				// The candidate was part of continuing motion rather than a cut.
				state.pendingHardCutValid = false;
				state.pendingHardCutFrames = 0;
				state.pendingInitialAverageLumaDifference = 0;
				state.pendingInitialChangedSampleCount = 0;
			}
		}

		// Keep the candidate confirmation above: a pan can exceed one of these
		// metrics, but normally will not produce a large first change followed by
		// a sufficiently stable new-scene signature.
		const bool broadSpatialChange =
			immediate.averageLumaDifference >= 44 &&
			immediate.changedSampleCount >= (sampleCount * 45) / 100;
		const bool hardSceneCut = broadSpatialChange &&
			(immediate.histogramDistance >= 55 || immediate.averageLumaDifference >= 64);

		if (hardSceneCut && !sceneEvent && !state.pendingHardCutValid)
		{
			state.pendingHardCut = current;
			state.pendingHardCutValid = true;
			state.pendingHardCutFrames = 0;
			state.pendingInitialAverageLumaDifference = immediate.averageLumaDifference;
			state.pendingInitialChangedSampleCount = immediate.changedSampleCount;
		}
	}

	// Near-black is a safe boundary, but count one event per black interval
	// rather than marking every black frame as a separate scene.
	if (nearBlack && !state.previousNearBlack)
	{
		sceneEvent = true;
		state.pendingHardCutValid = false;
		state.pendingHardCutFrames = 0;
		state.pendingInitialAverageLumaDifference = 0;
		state.pendingInitialChangedSampleCount = 0;
	}
	state.previousNearBlack = nearBlack;

	// Do not publish repeated corrections during a single transition or a
	// camera move. The interval is frame-rate aware (two seconds).
	if (state.framesUntilNextEvent > 0)
		--state.framesUntilNextEvent;
	if (sceneEvent && state.framesUntilNextEvent > 0)
		sceneEvent = false;
	state.previous = current;

	if (sceneEvent)
	{
		const REFERENCE_TIME frameDuration = std::max<REFERENCE_TIME>(1, m_frameDuration);
		const uint64_t cooldownFrames64 =
			(2ULL * REFERENCE_TIME_TICKS_PER_SECOND + static_cast<uint64_t>(frameDuration) - 1) /
			static_cast<uint64_t>(frameDuration);
		state.framesUntilNextEvent = static_cast<uint32_t>(std::min<uint64_t>(cooldownFrames64, 300));
		sceneEventId = m_sceneEventSequence.fetch_add(1, std::memory_order_relaxed) + 1;
		m_sceneAwareDetectedCount.fetch_add(1, std::memory_order_relaxed);
		const int64_t accumulatedPhase = m_scenePhasePpmUnits.load(std::memory_order_relaxed);
		if (accumulatedPhase >= static_cast<int64_t>(RATIONAL_TRIM_DENOMINATOR))
			correctionAction = SceneCorrectionAction::Drop;
		else if (accumulatedPhase <= -static_cast<int64_t>(RATIONAL_TRIM_DENOMINATOR))
			correctionAction = SceneCorrectionAction::Repeat;
	}

	return sceneEvent;
}


size_t CBufferedLiveSourceVideoOutputPin::GetBufferingTarget() {

	size_t nominalTarget = (m_frameQueueMaxSize.load(std::memory_order_relaxed) / 8);
	double fps = 60.0;
	if (m_timeScale > 0 && m_frameDurationTicks > 0) fps = (double)m_timeScale / (double)m_frameDurationTicks;

	// Calculate frame-rate appropriate target with MINIMUM of 3 frames
	// Low FPS (< 30fps like 23.976): Need MORE frames for stable buffering at higher lead times
	// High FPS (>= 30fps): Need more frames for smooth playback
	size_t frames;
	if (fps < 30.0)
	{
		// LOW FPS: At 23.976fps, each frame is ~42ms. With 200ms lead time, we need
		// at least 5 frames buffered (5 * 42ms = 210ms > 200ms lead time)
		// Use nominalTarget directly (no halving) for low fps
		frames = std::max<size_t>(5, nominalTarget);
	}
	else
	{
		frames = nominalTarget + 1;
	}

	// CRITICAL: Ensure minimum of 3 frames for MadVR buffering stability
	frames = std::max<size_t>(3, frames);

	// Log the buffering target periodically
	static double lastLoggedFps = 0.0;
	if (abs(fps - lastLoggedFps) > 1.0)
	{
		DebugLog::Log("GetBufferingTarget(): fps=%.2f, nominalTarget=%zu, finalTarget=%zu", fps, nominalTarget, frames);
		lastLoggedFps = fps;
	}

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
			IMediaSample* pSample = m_convertedSampleQueue.front().sample;
			m_convertedSampleQueue.pop_front();
			if (pSample) pSample->Release();
		}
	}

	// Clear timestamp history for CLOCK_SMART modes
	{
		std::lock_guard<std::mutex> lock(m_timestampHistoryMutex);
		memset(m_timestampHistory, 0, sizeof(m_timestampHistory));
		m_timestampHistoryIndex = 0;
	}

	// Clear pending timestamp history
	ClearPendingTimestamps();

	// Enter buffering mode
	m_isBuffering.store(true, std::memory_order_release);
	{
		CAutoLock stateLock(&m_stateLock);
		m_lastSeenFrameCounter = 0;
	}

	if (m_hFrameAvailableEvent)
		SetEvent(m_hFrameAvailableEvent);

	DebugLog::Log("OnBadTimestampDetected(): Recovery complete - buffering enabled, queues purged");
}

size_t CBufferedLiveSourceVideoOutputPin::GetConvertedQueueSize()
{
	CAutoLock convLock(&m_convertedQueueLock);
	return m_convertedSampleQueue.size();
}

REFERENCE_TIME CBufferedLiveSourceVideoOutputPin::NowStreamTime(CBaseFilter* f)
{
	if (!f)
		return REFERENCE_TIME_INVALID;

	CRefTime now;
	const HRESULT hr = f->StreamTime(now);
	if (FAILED(hr))
		return REFERENCE_TIME_INVALID;

	return now;
}

//
// PENDING TIMESTAMP HISTORY - Record timestamps as frames flow through conversion
// This allows late-binding to find "next frame" timestamps even when queue is empty
//

void CBufferedLiveSourceVideoOutputPin::RecordPendingTimestamp(REFERENCE_TIME timeStart)
{
	std::lock_guard<std::mutex> lock(m_pendingTimestampMutex);
	m_pendingTimestamps[m_pendingTimestampIndex].timeStart = timeStart;
	m_pendingTimestamps[m_pendingTimestampIndex].sequenceNumber = m_pendingSequenceCounter++;

	m_pendingTimestampIndex = (m_pendingTimestampIndex + 1) % PENDING_TIMESTAMP_SIZE;

}

REFERENCE_TIME CBufferedLiveSourceVideoOutputPin::FindNextPendingTimestamp(
	REFERENCE_TIME currentStart, REFERENCE_TIME theoreticalStop, REFERENCE_TIME tolerance) const
{
	std::lock_guard<std::mutex> lock(m_pendingTimestampMutex);

	REFERENCE_TIME bestMatch = REFERENCE_TIME_INVALID;
	REFERENCE_TIME bestDelta = REFERENCE_TIME_INVALID;
	size_t candidatesFound = 0;
	size_t candidatesInRange = 0;
	size_t candidatesAfterCurrent = 0;
	REFERENCE_TIME minTimestamp = REFERENCE_TIME_INVALID;
	REFERENCE_TIME maxTimestamp = REFERENCE_TIME_INVALID;

	// Search all pending timestamps for best match
	for (size_t i = 0; i < PENDING_TIMESTAMP_SIZE; i++)
	{
		const auto& record = m_pendingTimestamps[i];
		if (record.timeStart == 0)
			continue;  // Uninitialized slot

		++candidatesFound;

		// Track min/max for diagnostics
		if (minTimestamp == REFERENCE_TIME_INVALID || record.timeStart < minTimestamp)
			minTimestamp = record.timeStart;
		if (maxTimestamp == REFERENCE_TIME_INVALID || record.timeStart > maxTimestamp)
			maxTimestamp = record.timeStart;

		// CRITICAL: Must be AFTER current frame (in the future)
		if (record.timeStart <= currentStart)
			continue;

		++candidatesAfterCurrent;

		// Check if within tolerance of theoretical next
		const REFERENCE_TIME delta = abs(record.timeStart - theoreticalStop);
		if (delta <= tolerance)
		{
			++candidatesInRange;

			// Take the smallest timestamp that's greater than current AND closest to theoretical
			if (bestMatch == REFERENCE_TIME_INVALID || delta < bestDelta)
			{
				bestMatch = record.timeStart;
				bestDelta = delta;
			}
		}
	}

	// DIAGNOSTIC: Log search details every 10 searches (more frequent for debugging)
	static uint64_t searchCount = 0;
	++searchCount;
	if (searchCount % 10 == 0 || bestMatch == REFERENCE_TIME_INVALID)
	{
		/*DebugLog::Log("PENDING-SEARCH #%llu: current=%.3fms, target=%.3fms, tolerance=±%.3fms",
			searchCount, currentStart / 10000.0, theoreticalStop / 10000.0, tolerance / 10000.0);
		DebugLog::Log("  History: total=%zu, afterCurrent=%zu, inRange=%zu, min=%.3fms, max=%.3fms",
			candidatesFound, candidatesAfterCurrent, candidatesInRange,
			(minTimestamp != REFERENCE_TIME_INVALID) ? (minTimestamp / 10000.0) : 0.0,
			(maxTimestamp != REFERENCE_TIME_INVALID) ? (maxTimestamp / 10000.0) : 0.0);
		DebugLog::Log("  Result: bestMatch=%s (delta=%.3fms)",
			(bestMatch != REFERENCE_TIME_INVALID) ? "YES" : "NO",
			(bestDelta != REFERENCE_TIME_INVALID) ? (bestDelta / 10000.0) : 0.0);
			*/
	}

	return bestMatch;
}

void CBufferedLiveSourceVideoOutputPin::ClearPendingTimestamps()
{
	std::lock_guard<std::mutex> lock(m_pendingTimestampMutex);
	memset(m_pendingTimestamps, 0, sizeof(m_pendingTimestamps));
	m_pendingTimestampIndex = 0;
	// Don't reset sequence counter - it's monotonic across the lifetime
}
