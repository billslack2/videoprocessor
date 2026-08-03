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
#include <SceneDetector.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>

#include <ConfigFile.h>
#include <DirectShowDeliveryOutcome.h>
#include <microsoft_directshow/DirectShowEpochPrimePolicy.h>
#include <microsoft_directshow/DirectShowVideoTimingAdapter.h>
#include <LiveEpochConvergenceController.h>
#include <LiveSteadyQueuePolicy.h>
#include <RationalLiveOutputSequencer.h>
#include "CBufferedLiveSourceVideoOutputPin.h"
#include "WindowsOcrSubtitleDetector.h"
#include "GpuSubtitleDetector.h"
#include "../../P010ActivePictureEvidence.h"

namespace
{
const char* TimestampMethodName(DirectShowStartStopTimeMethod method) noexcept
{
	switch (method)
	{
	case DS_SSTM_CLOCK_SMART: return "Clock-Smart";
	case DS_SSTM_CLOCK_THEO: return "Clock-Theo";
	case DS_SSTM_CLOCK_CLOCK: return "Clock-Clock";
	case DS_SSTM_THEO_THEO: return "Theo-Theo";
	case DS_SSTM_RATIONAL_RATIONAL: return "Rational-Rational";
	case DS_SSTM_CLOCK_RATIONAL: return "Clock-Rational";
	case DS_SSTM_CLOCK_SMART2: return "Clock-Smart2";
	case DS_SSTM_CLOCK_NONE: return "Clock-None";
	case DS_SSTM_THEO_NONE: return "Theo-None";
	case DS_SSTM_NONE: return "None";
	default: return "Unknown";
	}
}
}


CBufferedLiveSourceVideoOutputPin::CBufferedLiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr) :
	ALiveSourceVideoOutputPin(filter, pLock, phr)
{
	// Initialize all member variables BEFORE creating events/threads
	m_frameProcessor.Configure(
		[this](VideoFrame& frame, IMediaSample* sample)
		{
			return RenderVideoFrameIntoSample(frame, sample);
		},
		[]()
		{
			return static_cast<uint64_t>(GetWallClockTime());
		});
	m_frameQueueMaxSize.store(32, std::memory_order_relaxed);  // Default safe value
	m_isActive.store(false, std::memory_order_relaxed);
	m_isBuffering.store(true, std::memory_order_relaxed);  // Start in buffering mode
	m_totalConversionTimeUs.store(0, std::memory_order_relaxed);
	m_conversionFrameCount.store(0, std::memory_order_relaxed);
	m_sceneAwareDetectedCount.store(0, std::memory_order_relaxed);
	m_sceneAwareLateCandidateCount.store(0, std::memory_order_relaxed);
	m_sceneAwareCorrectionDropCount.store(0, std::memory_order_relaxed);
	m_sceneAwareCorrectionRepeatCount.store(0, std::memory_order_relaxed);
	m_subtitleRelocationCount.store(0, std::memory_order_relaxed);
	m_scenePhasePpmUnits.store(0, std::memory_order_relaxed);
	m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_relaxed);
	m_sceneSecondsUntilPlan.store(0.0, std::memory_order_relaxed);
	m_scenePredictedAction.store(0, std::memory_order_relaxed);
	m_sceneCorrectionPlanned.store(false, std::memory_order_relaxed);
	m_sceneDisplayRefreshRateHz.store(0.0, std::memory_order_relaxed);
	m_sceneDeliveryRateHz.store(0.0, std::memory_order_relaxed);
	m_sceneTimingRatesCompatible.store(false, std::memory_order_relaxed);
	m_sceneTimingReady.store(false, std::memory_order_relaxed);
	m_sceneWarmupIntervals.store(0, std::memory_order_relaxed);
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

	// Conversion owns submission and compositing. Stop its analyzer only after
	// conversion is guaranteed not to enter the subtitle path again.
	StopSubtitleAnalysisWorker();

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
	if (m_frameQueueMaxSize.load(std::memory_order_relaxed) == 0)
		throw std::runtime_error("Call SetFrameQueueMaxSize() before activating the graph");

	DebugLog::Log(
		"CBufferedLiveSourceVideoOutputPin::Active() - Starting activation "
		"timestamp_method=%s timestamp_method_id=%d queue_size=%zu",
		TimestampMethodName(m_timestamp), static_cast<int>(m_timestamp),
		m_frameQueueMaxSize.load(std::memory_order_relaxed));

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

		// Establish a clean queue epoch before capture callbacks or either worker
		// can observe the active state. This avoids a startup race in which a
		// worker consumes a frame while Active() is still purging old queues.
		size_t purgedRaw = 0;
		size_t purgedConverted = 0;
		const uint64_t activeEpoch =
			m_queueEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
		const size_t activePrimeTarget =
			DirectShowEpochPrimePolicy::PrimeTarget(
				m_frameQueueMaxSize.load(std::memory_order_acquire),
				static_cast<size_t>(std::max<LONG>(
					0, GetNegotiatedAllocatorBufferCount())));
		const size_t activePrimeRawTarget =
			std::min(
				DirectShowEpochPrimePolicy::RawBridgeTarget(
					DirectShowEpochPrimePolicy::MaximumRetainedRawQueueFrames),
				m_frameQueueMaxSize.load(std::memory_order_acquire));
		{
			CAutoLock primeLock(&m_primeStateLock);
			m_primeTargetFrames.store(activePrimeTarget, std::memory_order_release);
			m_primeRawTargetFrames.store(
				activePrimeRawTarget, std::memory_order_release);
			m_primeQueueEpoch.store(activeEpoch, std::memory_order_release);
			m_primePrefillReachedEpoch.store(0, std::memory_order_release);
			m_primeStartedTick.store(0, std::memory_order_release);
		}
		m_steadyQueueEpoch.store(0, std::memory_order_release);
		m_currentEpochDeliverySuccessCount.store(0, std::memory_order_release);
		m_convergenceAppliedEpoch.store(0, std::memory_order_release);
		m_convergenceAppliedTick.store(0, std::memory_order_release);
		m_convergenceDeliverySuccessCount.store(0, std::memory_order_release);
		m_convergenceTargetFrames.store(0, std::memory_order_release);
		m_convergenceHardBlockRecovered.store(false, std::memory_order_release);
		m_convergenceConvertedQueueWasFull.store(false, std::memory_order_release);
		m_sceneDetectorGeneration.fetch_add(1, std::memory_order_release);
		m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
		purgedRaw = PurgeQueue();
		{
			CAutoLock diagnosticsLock(&m_rawDiagnosticsLock);
			m_rawOverflowLogCount = 0;
			m_lastRawOverflowLogTime = 0;
		}
		ResetTimingControllerToPipelineEpoch({
			m_queueEpoch.load(std::memory_order_acquire) });
		{
			CAutoLock convertedLock(&m_convertedQueueLock);
			purgedConverted = m_processedFrameQueue.Flush();
			m_publishedConvertedQueueDepth.store(0, std::memory_order_release);
		}
		ResetEvent(m_hFrameAvailableEvent);
		ResetEvent(m_hConvertedAvailableEvent);
		m_liveOutputTrace.Clear();
		m_liveConvergenceTrace.Clear();
		m_liveOutputMetricsTrace.Clear();
		m_liveOutputTraceRunId = GetTickCount64();
		m_liveOutputTraceExportOrdinal.store(0, std::memory_order_release);

		// Update state atomics
		m_isActive.store(true, std::memory_order_release);
		m_isBuffering.store(true, std::memory_order_release);
		m_captureThreadId.store(0, std::memory_order_release);
		m_deliveryThreadId.store(0, std::memory_order_release);
		m_inputFrameCount.store(0, std::memory_order_release);
		m_dequeueCount.store(0, std::memory_order_release);
		m_deliveryAttemptCount.store(0, std::memory_order_release);
		m_deliverySuccessCount.store(0, std::memory_order_release);
		m_currentEpochDeliverySuccessCount.store(
			0, std::memory_order_release);
		m_lastDeliverySuccessQueueEpoch.store(0, std::memory_order_release);
		CompleteCoordinatedReset();
		m_lastInputTick.store(0, std::memory_order_release);
		m_lastConversionTick.store(0, std::memory_order_release);
		m_lastDequeueTick.store(0, std::memory_order_release);
		m_lastDeliveryStartTick.store(0, std::memory_order_release);
		m_lastDeliverySuccessTick.store(0, std::memory_order_release);
		m_deliveryInProgress.store(false, std::memory_order_release);
		m_maximumSuccessfulDeliveryDurationUs.store(0, std::memory_order_release);
		m_sourceBufferConversionInFlight.store(false, std::memory_order_release);
		m_sourceBufferConversionCaptureArrivalTick.store(0, std::memory_order_release);
		m_retainedSourceBufferHighWater.store(0, std::memory_order_release);
		m_resetInProgress.store(false, std::memory_order_release);
		m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
		m_scenePhasePpmUnits.store(0, std::memory_order_release);
		m_lastSceneAwareCorrectionTime.store(0, std::memory_order_release);
		m_lastCorrectedSceneEventId.store(0, std::memory_order_release);

		// Reset auto-purge timing state for clean startup
		{
			CAutoLock stateLock(&m_stateLock);
			m_lastAutoPurgeTime = 0;
			m_bufferingExitTime = 0;
		}

		const size_t downstreamEstimate =
			m_downstreamPrimeTargetFrames.load(std::memory_order_acquire);
		DebugLog::Log(
			"Active(): Set m_isActive=true, m_isBuffering=true, reset timing state prime_epoch=%llu converted_prime=%zu raw_bridge=%zu launch_reservoir=%zu madvr_estimated_pipeline=%zu estimate_satisfied=%d",
			static_cast<unsigned long long>(activeEpoch), activePrimeTarget,
			activePrimeRawTarget, activePrimeTarget + activePrimeRawTarget,
			downstreamEstimate,
			downstreamEstimate > 0 &&
				activePrimeTarget + activePrimeRawTarget >= downstreamEstimate ? 1 : 0);

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

		// Inactive() stops the optional analyzer so graph/renderer teardown can
		// never hang on an in-flight DirectML call. A renderer restart does not
		// reapply configuration, so explicitly recreate the analyzer for every
		// new active graph epoch.
		if (m_subtitleRepositionMode.load(std::memory_order_acquire) !=
			SubtitleRepositionMode::DISABLED)
		{
			ResetSubtitleAnalysis();
			StartSubtitleAnalysisWorker();
			DebugLog::Log(
				"Active(): Subtitle analyzer restarted for the new graph epoch");
		}

		// MINIMAL STARTUP SYNC: Just ensure threads are created, no artificial delays
		// Threads will synchronize naturally through events and queues

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
		{
			CAutoLock primeLock(&m_primeStateLock);
			m_primeQueueEpoch.store(0, std::memory_order_release);
			m_primeTargetFrames.store(0, std::memory_order_release);
			m_primeRawTargetFrames.store(0, std::memory_order_release);
			m_primePrefillReachedEpoch.store(0, std::memory_order_release);
			m_primeStartedTick.store(0, std::memory_order_release);
		}

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

		// Conversion is the only producer of subtitle-analysis jobs. Stop the
		// optional GPU worker here, before graph/source teardown can block the
		// UI in an object destructor.
		StopSubtitleAnalysisWorker();

		// Then wait for delivery thread
		if (ThreadExists())
		{
			DbgLog((LOG_TRACE, 1, TEXT("Inactive(): Waiting for delivery thread to exit...")));
			Close();  // This waits for thread to exit
		}

		// The three pipeline workers have stopped, so take and persist the
		// bounded trace now. This keeps file I/O completely off live paths.
		WriteLiveOutputTrace("inactive");

		// Purge queues AFTER threads have exited
		PurgeQueue();
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
	if (CoordinatedResetRequested())
		return S_OK;

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

	m_captureThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
	m_inputFrameCount.fetch_add(1, std::memory_order_relaxed);
	m_lastInputTick.store(GetTickCount64(), std::memory_order_release);

	uint64_t callbackEpoch = m_queueEpoch.load(std::memory_order_acquire);
	const uint64_t newCounter = videoFrame.GetCounter();
	// Add frame to raw queue
	uint64_t overflowLogCount = 0;
	uint64_t overflowFrameCounter = 0;
	size_t overflowQueueSize = 0;
	size_t overflowQueueMaxSize = 0;
	size_t acceptedRawQueueDepth = 0;
	if (!m_isActive.load(std::memory_order_acquire))
		return S_OK;

	// The queue performs the second epoch check while it owns its transport
	// state. A callback that raced a reset releases its acquired source-buffer
	// reference rather than publishing stale work into the next segment.
	const uint64_t captureArrivalTick = GetTickCount64();
	VideoFrame capturedFrame = videoFrame;
	capturedFrame.SetCaptureArrivalTick(captureArrivalTick);
	capturedFrame.SourceBufferAddRef();
	const PipelineEpoch currentEpoch{
		m_queueEpoch.load(std::memory_order_acquire) };
	const bool limitPrimeRawRetention = callbackEpoch != 0 &&
		callbackEpoch == m_primeQueueEpoch.load(std::memory_order_acquire) &&
		m_steadyQueueEpoch.load(std::memory_order_acquire) != callbackEpoch;
	size_t discardedByLimitedPush = 0;
	const EpochBoundedQueuePushResult pushResult = limitPrimeRawRetention ?
		m_captureFrameQueue.PushWithMaximum(
			std::move(capturedFrame), { callbackEpoch }, currentEpoch,
			DirectShowEpochPrimePolicy::MaximumRetainedRawQueueFrames,
			&discardedByLimitedPush) :
		m_captureFrameQueue.Push(
			std::move(capturedFrame), { callbackEpoch }, currentEpoch);
	EpochBoundedQueueMetrics rawMetrics = m_captureFrameQueue.Metrics();
	m_publishedRawQueueDepth.store(rawMetrics.depth, std::memory_order_release);
	const size_t retainedSourceBuffers = rawMetrics.depth +
		(m_sourceBufferConversionInFlight.load(std::memory_order_acquire) ? 1 : 0);
	size_t retainedHighWater =
		m_retainedSourceBufferHighWater.load(std::memory_order_relaxed);
	while (retainedSourceBuffers > retainedHighWater &&
		!m_retainedSourceBufferHighWater.compare_exchange_weak(
			retainedHighWater, retainedSourceBuffers,
			std::memory_order_release, std::memory_order_relaxed))
	{
	}
	if (pushResult == EpochBoundedQueuePushResult::RejectedStale ||
		pushResult == EpochBoundedQueuePushResult::RejectedNoCapacity)
		return S_OK;

	// Measure the fail-open interval from the first fresh sample. A slow HDMI
	// handshake can leave the graph active without capture frames for seconds.
	{
		CAutoLock primeLock(&m_primeStateLock);
		if (callbackEpoch == m_primeQueueEpoch.load(std::memory_order_acquire) &&
			m_primeStartedTick.load(std::memory_order_acquire) == 0)
		{
			m_primeStartedTick.store(
				captureArrivalTick, std::memory_order_release);
		}
	}

	const size_t queueMaxSize = rawMetrics.capacity;
	acceptedRawQueueDepth = rawMetrics.depth;
	if (pushResult == EpochBoundedQueuePushResult::AcceptedAfterOverflowDiscard)
	{
		const size_t discardedByPush = limitPrimeRawRetention ?
			discardedByLimitedPush : 1;
		m_droppedFrameCount.fetch_add(
			discardedByPush, std::memory_order_relaxed);
		{
			CAutoLock diagnosticsLock(&m_rawDiagnosticsLock);
			m_rawOverflowLogCount += discardedByPush;
			const DWORD now = GetTickCount();
			if (m_lastRawOverflowLogTime == 0 || now - m_lastRawOverflowLogTime >= 5000)
			{
				overflowLogCount = m_rawOverflowLogCount;
				overflowFrameCounter = newCounter;
				overflowQueueSize = rawMetrics.depth;
				overflowQueueMaxSize = queueMaxSize;
				m_rawOverflowLogCount = 0;
				m_lastRawOverflowLogTime = now;
			}
		}
	}

	// DIAGNOSTIC: Log when raw queue is backing up.
	if (queueMaxSize > 0 && acceptedRawQueueDepth >= (queueMaxSize * 3) / 4)
	{
		static DWORD lastBackupLog = 0;
		DWORD now = GetTickCount();
		if (now - lastBackupLog >= 5000)  // Log at most every 5 seconds
		{
			uint64_t convFrames = m_conversionFrameCount.load();
			size_t convertedSize = 0;
			{
				CAutoLock convLock(&m_convertedQueueLock);
				convertedSize = m_processedFrameQueue.Size();
			}

			DebugLog::Log("OnVideoFrame: Raw queue BACKING UP (raw=%zu/%zu, converted=%zu, buffering=%d, convFrames=%llu)",
				acceptedRawQueueDepth, queueMaxSize, convertedSize,
				m_isBuffering.load(std::memory_order_acquire) ? 1 : 0,
				convFrames);
			lastBackupLog = now;
		}
	}

	LiveOutputTraceRecord captureTrace;
	captureTrace.kind = LiveOutputTraceKind::CaptureAccepted;
	captureTrace.frameNumber = newCounter;
	captureTrace.pipelineEpoch = callbackEpoch;
	captureTrace.captureTimestamp =
		static_cast<uint64_t>(videoFrame.GetTimingTimestamp());
	captureTrace.captureArrivalTick = captureArrivalTick;
	captureTrace.eventTick = captureArrivalTick;
	captureTrace.sourceDiscontinuity = videoFrame.IsSourceDiscontinuity();
	captureTrace.rawQueueDepth = static_cast<uint32_t>(acceptedRawQueueDepth);
	captureTrace.convertedQueueDepth = static_cast<uint32_t>(
		m_publishedConvertedQueueDepth.load(std::memory_order_acquire));
	m_liveOutputTrace.Record(captureTrace);

	if (overflowLogCount > 0)
	{
		DebugLog::Log("OnVideoFrame: Raw queue OVERFLOW - %llu frame(s) dropped in interval; last=#%llu, size=%zu/%zu",
			overflowLogCount, overflowFrameCounter, overflowQueueSize, overflowQueueMaxSize);
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

	m_frameQueueMaxSize.store(frameQueueMaxSize, std::memory_order_release);
	const size_t framesToPurge = m_captureFrameQueue.Resize(frameQueueMaxSize);
	// The historical converted queue is trimmed by the delivery path, not here.
	// Preserve that behavior while ensuring a later queue-size increase does not
	// leave the extracted transport at its construction-time capacity.
	{
		CAutoLock convLock(&m_convertedQueueLock);
		m_processedFrameQueue.SetCapacityWithoutDiscard(frameQueueMaxSize);
	}
	const EpochBoundedQueueMetrics rawMetrics = m_captureFrameQueue.Metrics();
	m_publishedRawQueueDepth.store(rawMetrics.depth, std::memory_order_release);
	if (framesToPurge > 0)
	{
		DbgLog((LOG_TRACE, 1, TEXT("SetFrameQueueMaxSize(): Purging %zu excess frames due to size reduction"),
			framesToPurge));
		DebugLog::Log("SetFrameQueueMaxSize: Queue size reduction - purging %zu excess frames (current=%zu, new=%zu)",
			framesToPurge, rawMetrics.depth, frameQueueMaxSize);
		m_droppedFrameCount.fetch_add(framesToPurge, std::memory_order_relaxed);
		DebugLog::Log("SetFrameQueueMaxSize: Purged %zu frames, queue now has %zu frames",
			framesToPurge, rawMetrics.depth);
	}

	// Reset simple proactive state only
	m_recentDeliveryFailures.store(0, std::memory_order_relaxed);
	m_lastQueueWarning = 0;

	DebugLog::Log("SetFrameQueueMaxSize: Queue size changed, reset failure counters");

	SetEvent(m_hFrameAvailableEvent);
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
	m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
	m_scenePhasePpmUnits.store(0, std::memory_order_release);
	m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_release);
	m_scenePredictedAction.store(0, std::memory_order_release);
	m_sceneCorrectionPlanned.store(false, std::memory_order_release);
	m_lastSceneAwareCorrectionTime.store(0, std::memory_order_release);
	m_lastCorrectedSceneEventId.store(0, std::memory_order_release);
	m_subtitlePanelLumaInitialized.store(false, std::memory_order_release);
	m_subtitleSceneEventId.store(0, std::memory_order_release);
	DebugLog::Log("SCENE-AWARE CORRECTION: %s", enabled ? "enabled" : "disabled");
}

void CBufferedLiveSourceVideoOutputPin::SetSceneCorrectionUpstreamSample(bool enabled)
{
	const bool previous =
		m_sceneCorrectionUpstreamSample.exchange(enabled, std::memory_order_acq_rel);
	if (previous == enabled)
	{
		DebugLog::Log(
			"SCENE-AWARE CORRECTION MODE: %s",
			enabled ? "UPSTREAM_SAMPLE" : "RENDERER_GAP");
		return;
	}

	// A mode switch changes how output slots are consumed. Force delivery-thread
	// state to start a fresh generation; normal application use sets this before
	// the graph starts.
	m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
	m_scenePhasePpmUnits.store(0, std::memory_order_release);
	m_lastSceneAwareCorrectionTime.store(0, std::memory_order_release);
	m_lastCorrectedSceneEventId.store(0, std::memory_order_release);
	DebugLog::Log(
		"SCENE-AWARE CORRECTION MODE: %s",
		enabled ? "UPSTREAM_SAMPLE" : "RENDERER_GAP");
}

void CBufferedLiveSourceVideoOutputPin::SetSubtitleRepositioning(bool enabled)
{
	SetSubtitleRepositioningMode(enabled ?
		SubtitleRepositionMode::BASIC :
		SubtitleRepositionMode::DISABLED);
}

void CBufferedLiveSourceVideoOutputPin::SetSubtitleRepositioningMode(
	SubtitleRepositionMode mode)
{
	const SubtitleRepositionMode previous =
		m_subtitleRepositionMode.exchange(mode, std::memory_order_acq_rel);
	if (previous != mode)
	{
		ResetSubtitleAnalysis();
		if (mode != SubtitleRepositionMode::DISABLED)
			StartSubtitleAnalysisWorker();
		else
			StopSubtitleAnalysisWorker();
		const char* modeName =
			mode == SubtitleRepositionMode::ADVANCED ? "ADVANCED" :
			(mode == SubtitleRepositionMode::BASIC ? "BASIC" : "DISABLED");
		DbgLog((LOG_TRACE, 1, TEXT("Subtitle repositioning mode changed")));
		DebugLog::Log(
			"SUBTITLE REPOSITION: %s (asynchronous analysis)",
			modeName);
	}
}

bool CBufferedLiveSourceVideoOutputPin::GetSceneTimingPrediction(
	double& secondsUntilCorrection, double& secondsUntilPlan,
	int& action, bool& planned) const
{
	action = m_scenePredictedAction.load(std::memory_order_acquire);
	secondsUntilCorrection =
		m_sceneSecondsUntilCorrection.load(std::memory_order_acquire);
	secondsUntilPlan = m_sceneSecondsUntilPlan.load(std::memory_order_acquire);
	planned = m_sceneCorrectionPlanned.load(std::memory_order_acquire);
	return action != 0 && std::isfinite(secondsUntilCorrection);
}

bool CBufferedLiveSourceVideoOutputPin::GetSceneTimingLastCorrection(
	int& action, double& secondsFromDeadline, uint64_t& correctionTick) const
{
	action = m_sceneLastCorrectionAction.load(std::memory_order_acquire);
	secondsFromDeadline =
		m_sceneLastCorrectionSecondsFromDeadline.load(std::memory_order_acquire);
	correctionTick = m_sceneLastCorrectionTick.load(std::memory_order_acquire);
	return action != 0 && correctionTick != 0;
}

void CBufferedLiveSourceVideoOutputPin::SetSceneTimingRates(
	double displayRefreshRateHz,
	double deliveryRateHz)
{
	// Scene Detect hides a small one-frame timing correction at a cut.  It
	// must never try to act as a frame-rate converter: beyond 50 ppm, a
	// correction is due within seconds/minutes and is not safely hideable.
	constexpr double kMaximumSceneTimingMismatchPpm = 200.0;
	const bool ratesInRange =
		displayRefreshRateHz >= 10.0 && displayRefreshRateHz <= 240.0 &&
		deliveryRateHz >= 10.0 && deliveryRateHz <= 240.0;
	const double mismatchPpm = ratesInRange ?
		abs((displayRefreshRateHz / deliveryRateHz - 1.0) * 1000000.0) : 0.0;
	const bool cadenceCompatible =
		ratesInRange && mismatchPpm <= kMaximumSceneTimingMismatchPpm;
	m_sceneTimingRatesCompatible.store(cadenceCompatible, std::memory_order_release);
	const bool valid = cadenceCompatible;
	if (!valid)
	{
		const double previousDisplay =
			m_sceneDisplayRefreshRateHz.exchange(0.0, std::memory_order_acq_rel);
		const double previousDelivery =
			m_sceneDeliveryRateHz.exchange(0.0, std::memory_order_acq_rel);
		if (previousDisplay > 0.0 || previousDelivery > 0.0)
		{
			m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
			m_scenePhasePpmUnits.store(0, std::memory_order_release);
			m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_release);
			m_scenePredictedAction.store(0, std::memory_order_release);
			m_sceneCorrectionPlanned.store(false, std::memory_order_release);
			m_lastSceneAwareCorrectionTime.store(0, std::memory_order_release);
			m_lastCorrectedSceneEventId.store(0, std::memory_order_release);
			DebugLog::Log(
				"SCENE-AWARE RATES: unavailable or incompatible (display=%.6f Hz, delivery=%.6f Hz, mismatch=%.1f ppm); corrections suspended",
				displayRefreshRateHz, deliveryRateHz, mismatchPpm);
		}
		return;
	}

	const double previousDisplay =
		m_sceneDisplayRefreshRateHz.exchange(displayRefreshRateHz, std::memory_order_acq_rel);
	const double previousDelivery =
		m_sceneDeliveryRateHz.exchange(deliveryRateHz, std::memory_order_acq_rel);

	const bool timingBecameValid = previousDisplay <= 0.0 || previousDelivery <= 0.0;
	if (timingBecameValid)
	{
		m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
		m_scenePhasePpmUnits.store(0, std::memory_order_release);
		m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_release);
		m_scenePredictedAction.store(0, std::memory_order_release);
		m_sceneCorrectionPlanned.store(false, std::memory_order_release);
		m_lastSceneAwareCorrectionTime.store(0, std::memory_order_release);
		m_lastCorrectedSceneEventId.store(0, std::memory_order_release);
		const double mismatchPpm =
			((displayRefreshRateHz / deliveryRateHz) - 1.0) * 1000000.0;
		const double secondsPerWholeFrame =
			abs(displayRefreshRateHz - deliveryRateHz) > 0.0000001 ?
			1.0 / abs(displayRefreshRateHz - deliveryRateHz) : 0.0;
		DebugLog::Log(
			"SCENE-AWARE RATES: display=%.6f Hz delivery=%.6f Hz mismatch=%+.1f ppm predicted_interval=%.1fs",
			displayRefreshRateHz, deliveryRateHz, mismatchPpm, secondsPerWholeFrame);
	}

	// Normal measurements move by a few PPM and must not reset accumulated
	// phase. A 500-PPM step indicates a real mode/rate change and establishes a
	// new prediction epoch.
	const auto materiallyChanged = [](double previous, double current) {
		return previous > 0.0 &&
			abs(current - previous) / previous >= 0.0005;
	};
	if (!timingBecameValid &&
		(materiallyChanged(previousDisplay, displayRefreshRateHz) ||
			materiallyChanged(previousDelivery, deliveryRateHz)))
	{
		m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
		m_scenePhasePpmUnits.store(0, std::memory_order_release);
		m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_release);
		m_scenePredictedAction.store(0, std::memory_order_release);
		m_sceneCorrectionPlanned.store(false, std::memory_order_release);
		m_lastSceneAwareCorrectionTime.store(0, std::memory_order_release);
		m_lastCorrectedSceneEventId.store(0, std::memory_order_release);
		DebugLog::Log(
			"SCENE-AWARE RATE CHANGE: display %.6f->%.6f Hz, delivery %.6f->%.6f Hz; phase reset",
			previousDisplay, displayRefreshRateHz, previousDelivery, deliveryRateHz);
	}
}

void CBufferedLiveSourceVideoOutputPin::SetSceneTimingReadiness(
	bool ready,
	uint64_t intervalsObserved)
{
	m_sceneWarmupIntervals.store(intervalsObserved, std::memory_order_release);
	const bool previous =
		m_sceneTimingReady.exchange(ready, std::memory_order_acq_rel);
	if (previous == ready)
		return;

	if (!ready)
	{
		m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
		m_scenePhasePpmUnits.store(0, std::memory_order_release);
		m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_release);
		m_scenePredictedAction.store(0, std::memory_order_release);
		m_sceneCorrectionPlanned.store(false, std::memory_order_release);
		m_lastSceneAwareCorrectionTime.store(0, std::memory_order_release);
		m_lastCorrectedSceneEventId.store(0, std::memory_order_release);
	}

	DebugLog::Log(
		"SCENE-AWARE TIMING: %s after %llu display intervals",
		ready ? "stable; corrections enabled" : "warming; corrections suspended",
		intervalsObserved);
}

void CBufferedLiveSourceVideoOutputPin::SetSceneTimingPhase(
	int64_t vblankQpc,
	int64_t refreshPeriodQpc,
	int64_t qpcFrequency)
{
	// A valid refresh period is approximately 10-240 Hz. Reject stale or bogus
	// UI/DWM data rather than allowing a timing diagnostic to perturb delivery.
	const bool valid =
		vblankQpc > 0 && qpcFrequency > 0 && refreshPeriodQpc > 0 &&
		refreshPeriodQpc <= qpcFrequency / 10 &&
		refreshPeriodQpc >= qpcFrequency / 240;
	if (!valid)
	{
		m_sceneLastVBlankQpc.store(0, std::memory_order_release);
		m_sceneRefreshPeriodQpc.store(0, std::memory_order_release);
		m_sceneQpcFrequency.store(0, std::memory_order_release);
		return;
	}

	m_sceneLastVBlankQpc.store(vblankQpc, std::memory_order_release);
	m_sceneRefreshPeriodQpc.store(refreshPeriodQpc, std::memory_order_release);
	m_sceneQpcFrequency.store(qpcFrequency, std::memory_order_release);
}


void CBufferedLiveSourceVideoOutputPin::Reset()
{
	// Multiple reset sources exist (UI, timing recovery, frame-counter
	// discontinuity, and the delivery thread). Keep their complete downstream
	// flush transactions from overlapping.
	CAutoLock resetTransactionLock(&m_resetTransactionGate);
	m_resetInProgress.store(true, std::memory_order_release);
	m_latencySnapshotAvailable.store(false, std::memory_order_release);
	LiveOutputTraceRecord resetStartTrace;
	resetStartTrace.kind = LiveOutputTraceKind::ResetStarted;
	resetStartTrace.pipelineEpoch = m_queueEpoch.load(std::memory_order_acquire);
	resetStartTrace.eventTick = GetTickCount64();
	m_liveOutputTrace.Record(resetStartTrace);

	DebugLog::Log("CBufferedLiveSourceVideoOutputPin::Reset() - HDMI resync async queue reset starting");
	m_deliveryFlushing.store(true, std::memory_order_release);

	DirectShowSegmentTransitionResult transitionResult;
	try
	{
		transitionResult = m_directShowSegmentTransition.Execute(
			[this]()
			{
				// BeginFlush must be sent before waiting for an in-flight
				// Receive/Deliver; this is what unblocks a renderer that is
				// waiting internally.
				return DeliverBeginFlush();
			},
			[this]()
			{
				// No Deliver call can start while queues, timestamp state, and the
				// DirectShow segment are changed below.
				CAutoLock deliveryLock(&m_deliveryGate);

		m_sceneDetectorGeneration.fetch_add(1, std::memory_order_release);
		m_sceneTimingGeneration.fetch_add(1, std::memory_order_acq_rel);
		m_sceneTimingReady.store(false, std::memory_order_release);
		m_sceneWarmupIntervals.store(0, std::memory_order_release);

		// Establish the new epoch before flushing. A callback that races this
		// boundary is rejected by CaptureFrameQueue instead of becoming an
		// accidental first sample in the new DirectShow segment.
		const uint64_t resetEpoch =
			m_queueEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
		const size_t resetPrimeTarget =
			DirectShowEpochPrimePolicy::PrimeTarget(
				m_frameQueueMaxSize.load(std::memory_order_acquire),
				static_cast<size_t>(std::max<LONG>(
					0, GetNegotiatedAllocatorBufferCount())));
		const size_t resetPrimeRawTarget =
			std::min(
				DirectShowEpochPrimePolicy::RawBridgeTarget(
					DirectShowEpochPrimePolicy::MaximumRetainedRawQueueFrames),
				m_frameQueueMaxSize.load(std::memory_order_acquire));
		{
			CAutoLock primeLock(&m_primeStateLock);
			m_primeTargetFrames.store(resetPrimeTarget, std::memory_order_release);
			m_primeRawTargetFrames.store(
				resetPrimeRawTarget, std::memory_order_release);
			m_primeQueueEpoch.store(resetEpoch, std::memory_order_release);
			m_primePrefillReachedEpoch.store(0, std::memory_order_release);
			m_primeStartedTick.store(0, std::memory_order_release);
		}
		m_steadyQueueEpoch.store(0, std::memory_order_release);
		m_currentEpochDeliverySuccessCount.store(0, std::memory_order_release);
		m_convergenceAppliedEpoch.store(0, std::memory_order_release);
		m_convergenceAppliedTick.store(0, std::memory_order_release);
		m_convergenceDeliverySuccessCount.store(0, std::memory_order_release);
		m_convergenceTargetFrames.store(0, std::memory_order_release);
		m_convergenceHardBlockRecovered.store(false, std::memory_order_release);
		m_convergenceConvertedQueueWasFull.store(false, std::memory_order_release);
		m_maximumSuccessfulDeliveryDurationUs.store(0, std::memory_order_release);
		const size_t purgedFrames = PurgeQueue();
		m_retainedSourceBufferHighWater.store(
			m_sourceBufferConversionInFlight.load(std::memory_order_acquire) ? 1 : 0,
			std::memory_order_release);
		DebugLog::Log("Reset(): Purged %zu raw frames from HDMI resync", purgedFrames);

		// Purge converted samples from the old segment.
		{
			CAutoLock convLock(&m_convertedQueueLock);
			const size_t purgedSamples = m_processedFrameQueue.Flush();
			m_publishedConvertedQueueDepth.store(0, std::memory_order_release);
			DebugLog::Log("Reset(): Purged %zu pre-converted samples from HDMI resync", purgedSamples);
		}

		ClearPendingTimestamps();
		{
			CAutoLock stateLock(&m_stateLock);
			m_lastAutoPurgeTime = 0;
			m_bufferingExitTime = 0;
			m_lastSceneAwareCorrectionTime.store(0, std::memory_order_relaxed);
			m_lastCorrectedSceneEventId.store(0, std::memory_order_relaxed);
			m_scenePhasePpmUnits.store(0, std::memory_order_relaxed);
			m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_relaxed);
			m_scenePredictedAction.store(0, std::memory_order_relaxed);
			m_sceneCorrectionPlanned.store(false, std::memory_order_relaxed);
		}

		m_isBuffering.store(true, std::memory_order_release);
		m_totalConversionTimeUs.store(0, std::memory_order_relaxed);
		m_conversionFrameCount.store(0, std::memory_order_relaxed);
		m_sceneAwareCorrectionDropCount.store(0, std::memory_order_relaxed);
		m_sceneAwareCorrectionRepeatCount.store(0, std::memory_order_relaxed);
		ResetSubtitleAnalysis();
		m_activePictureAspectRatio.store(0.0, std::memory_order_release);
		m_activePictureAspectStable.store(false, std::memory_order_release);
		{
			std::lock_guard<std::mutex> lock(m_activePictureRectangleMutex);
			m_activePictureRectangle = {};
		}
		m_activePictureRectangleGeneration.fetch_add(1, std::memory_order_acq_rel);
		m_activePictureDetectorGeneration.fetch_add(1, std::memory_order_acq_rel);
		m_subtitlePanelLumaInitialized.store(false, std::memory_order_relaxed);
		m_sceneAwareDetectedCount.store(0, std::memory_order_relaxed);
		m_sceneAwareLateCandidateCount.store(0, std::memory_order_relaxed);
		m_recentDeliveryFailures.store(0, std::memory_order_relaxed);
		m_lastQueueWarning = 0;

		// Reset base timestamp/media-time state without sending another flush.
		ResetTimingState();
		ResetTimingControllerToPipelineEpoch({
			m_queueEpoch.load(std::memory_order_acquire) });
			},
			[this]()
			{
				return DeliverEndFlush();
			},
			[this]()
			{
				return DeliverNewSegment(0, MAXLONGLONG, 1.0);
			});
	}
	catch (...)
	{
		m_deliveryFlushing.store(false, std::memory_order_release);
		m_resetInProgress.store(false, std::memory_order_release);
		throw;
	}

	m_deliveryFlushing.store(false, std::memory_order_release);
	m_resetInProgress.store(false, std::memory_order_release);
	DebugLog::Log(
		"DirectShow segment transaction: epoch=%llu begin_flush_hr=0x%08lx "
		"end_flush_hr=0x%08lx new_segment_hr=0x%08lx",
		static_cast<unsigned long long>(
			m_queueEpoch.load(std::memory_order_acquire)),
		static_cast<unsigned long>(transitionResult.beginFlushResult),
		static_cast<unsigned long>(transitionResult.endFlushResult),
		static_cast<unsigned long>(transitionResult.newSegmentResult));

	if (FAILED(transitionResult.beginFlushResult))
		throw std::runtime_error("Failed to deliver beginflush");
	if (FAILED(transitionResult.endFlushResult))
		throw std::runtime_error("Failed to deliver endflush");
	if (FAILED(transitionResult.newSegmentResult))
		throw std::runtime_error("Failed to deliver new segment");

	// Wake both workers after the new segment is fully established.
	if (m_hFrameAvailableEvent)
		SetEvent(m_hFrameAvailableEvent);
	if (m_hConvertedAvailableEvent)
		SetEvent(m_hConvertedAvailableEvent);

	CompleteCoordinatedReset();
	LiveOutputTraceRecord resetCompleteTrace;
	resetCompleteTrace.kind = LiveOutputTraceKind::ResetCompleted;
	resetCompleteTrace.pipelineEpoch = m_queueEpoch.load(std::memory_order_acquire);
	resetCompleteTrace.eventTick = GetTickCount64();
	m_liveOutputTrace.Record(resetCompleteTrace);
	// A DirectShow graph rebuild can create a new pin immediately after this
	// reset. Persist a distinct snapshot before that new graph clears memory.
	// This is a reset boundary, never a capture, conversion, or delivery path.
	WriteLiveOutputTrace("reset");
	DebugLog::Log(
		"CBufferedLiveSourceVideoOutputPin::Reset() - queues/timing reset, buffering enabled, new segment delivered prime_epoch=%llu converted_prime=%zu raw_bridge=%zu madvr_estimated_pipeline=%zu",
		static_cast<unsigned long long>(
			m_primeQueueEpoch.load(std::memory_order_acquire)),
		m_primeTargetFrames.load(std::memory_order_acquire),
		m_primeRawTargetFrames.load(std::memory_order_acquire),
		m_downstreamPrimeTargetFrames.load(std::memory_order_acquire));
}


size_t CBufferedLiveSourceVideoOutputPin::GetFrameQueueSize()
{
	return m_captureFrameQueue.Size();
}


void CBufferedLiveSourceVideoOutputPin::SetOutputReadinessDeliveryReserve(
	size_t reserveFrames)
{
	const size_t capacity = m_frameQueueMaxSize.load(std::memory_order_acquire);
	const size_t boundedReserve = capacity > 0 ?
		std::min(reserveFrames, capacity) : 0;
	m_outputReadinessDeliveryReserve.store(
		boundedReserve, std::memory_order_release);
	DebugLog::Log(
		"Output readiness VP reserve updated: frames=%zu capacity=%zu",
		boundedReserve, capacity);
	if (m_hConvertedAvailableEvent)
		SetEvent(m_hConvertedAvailableEvent);
}


void CBufferedLiveSourceVideoOutputPin::SetQueueFramePolicy(
	size_t startupPrerollFrames, size_t steadyReserveFrames,
	bool steadyReserveConfigured)
{
	const size_t capacity = m_frameQueueMaxSize.load(std::memory_order_acquire);
	// Preserve the requested value so later capacity changes retain the
	// configuration intent. GetBufferingTarget/GetDeliveryReserve clamp it at
	// consumption time; 16 is also the schema-enforced public upper bound.
	const size_t boundedStartup = std::min(startupPrerollFrames, size_t{ 16 });
	const size_t boundedReserve = std::min(steadyReserveFrames, size_t{ 16 });
	m_configuredStartupPrerollFrames.store(
		boundedStartup, std::memory_order_release);
	m_configuredSteadyReserveFrames.store(
		boundedReserve, std::memory_order_release);
	m_configuredSteadyReserveExplicit.store(
		steadyReserveConfigured, std::memory_order_release);
	DebugLog::Log(
		"DirectShow queue policy updated: requested-startup-preroll=%zu requested-steady-target=%zu steady-explicit=%d capacity=%zu",
		boundedStartup, boundedReserve, steadyReserveConfigured ? 1 : 0,
		capacity);
	if (m_hConvertedAvailableEvent)
		SetEvent(m_hConvertedAvailableEvent);
}


bool CBufferedLiveSourceVideoOutputPin::GetLivenessSnapshot(
	RendererLivenessSnapshot& snapshot) const
{
	const uint64_t snapshotEpoch =
		m_queueEpoch.load(std::memory_order_acquire);
	snapshot.supported = true;
	snapshot.active = m_isActive.load(std::memory_order_acquire);
	snapshot.buffering = m_isBuffering.load(std::memory_order_acquire);
	snapshot.deliveryInProgress =
		m_deliveryInProgress.load(std::memory_order_acquire);
	snapshot.resetInProgress =
		m_resetInProgress.load(std::memory_order_acquire);
	snapshot.captureThreadId =
		m_captureThreadId.load(std::memory_order_relaxed);
	snapshot.conversionThreadId = m_conversionThreadId;
	snapshot.deliveryThreadId =
		m_deliveryThreadId.load(std::memory_order_relaxed);
	snapshot.queueEpoch = snapshotEpoch;
	snapshot.inputCount = m_inputFrameCount.load(std::memory_order_relaxed);
	snapshot.conversionCount =
		m_conversionFrameCount.load(std::memory_order_relaxed);
	snapshot.dequeueCount = m_dequeueCount.load(std::memory_order_relaxed);
	snapshot.deliveryAttemptCount =
		m_deliveryAttemptCount.load(std::memory_order_relaxed);
	snapshot.deliverySuccessCount =
		m_deliverySuccessCount.load(std::memory_order_relaxed);
	snapshot.currentEpochDeliverySuccessCount =
		m_currentEpochDeliverySuccessCount.load(std::memory_order_acquire);
	snapshot.lastDeliverySuccessQueueEpoch =
		m_lastDeliverySuccessQueueEpoch.load(std::memory_order_acquire);
	snapshot.lastInputTick = m_lastInputTick.load(std::memory_order_acquire);
	snapshot.lastConversionTick =
		m_lastConversionTick.load(std::memory_order_acquire);
	snapshot.lastDequeueTick =
		m_lastDequeueTick.load(std::memory_order_acquire);
	snapshot.lastDeliveryStartTick =
		m_lastDeliveryStartTick.load(std::memory_order_acquire);
	snapshot.lastDeliverySuccessTick =
		m_lastDeliverySuccessTick.load(std::memory_order_acquire);
	snapshot.maximumSuccessfulDeliveryDurationUs =
		m_maximumSuccessfulDeliveryDurationUs.load(std::memory_order_acquire);
	snapshot.rawQueueDepth =
		m_publishedRawQueueDepth.load(std::memory_order_acquire);
	snapshot.convertedQueueDepth =
		m_publishedConvertedQueueDepth.load(std::memory_order_acquire);
	snapshot.queueCapacity =
		m_frameQueueMaxSize.load(std::memory_order_acquire);
	snapshot.convergenceAppliedEpoch =
		m_convergenceAppliedEpoch.load(std::memory_order_acquire);
	snapshot.convergenceAppliedTick =
		m_convergenceAppliedTick.load(std::memory_order_acquire);
	snapshot.convergenceDeliverySuccessCount =
		m_convergenceDeliverySuccessCount.load(std::memory_order_acquire);
	snapshot.convergenceTargetFrames =
		m_convergenceTargetFrames.load(std::memory_order_acquire);
	snapshot.convergenceHardBlockRecovered =
		m_convergenceHardBlockRecovered.load(std::memory_order_acquire);
	snapshot.convergenceConvertedQueueWasFull =
		m_convergenceConvertedQueueWasFull.load(std::memory_order_acquire);
	snapshot.primePrefillReachedEpoch =
		m_primePrefillReachedEpoch.load(std::memory_order_acquire);
	snapshot.primeTargetFrames =
		m_primeTargetFrames.load(std::memory_order_acquire);
	snapshot.primeRawTargetFrames =
		m_primeRawTargetFrames.load(std::memory_order_acquire);
	const bool conversionOwnsSourceBuffer =
		m_sourceBufferConversionInFlight.load(std::memory_order_acquire);
	snapshot.retainedSourceBufferCount = snapshot.rawQueueDepth +
		(conversionOwnsSourceBuffer ? 1 : 0);
	snapshot.retainedSourceBufferHighWater =
		m_retainedSourceBufferHighWater.load(std::memory_order_acquire);
	VideoFrame oldestRawFrame;
	uint64_t oldestArrivalTick = 0;
	if (m_captureFrameQueue.TryPeekCurrent(
			{ snapshot.queueEpoch }, oldestRawFrame))
		oldestArrivalTick = oldestRawFrame.GetCaptureArrivalTick();
	const uint64_t conversionArrivalTick =
		m_sourceBufferConversionCaptureArrivalTick.load(
			std::memory_order_acquire);
	if (conversionOwnsSourceBuffer && conversionArrivalTick != 0 &&
		(oldestArrivalTick == 0 || conversionArrivalTick < oldestArrivalTick))
		oldestArrivalTick = conversionArrivalTick;
	const uint64_t snapshotTick = GetTickCount64();
	snapshot.oldestRetainedSourceBufferAgeMs = oldestArrivalTick != 0 &&
		snapshotTick >= oldestArrivalTick ? snapshotTick - oldestArrivalTick : 0;
	snapshot.deliveryReserveFrames =
		m_outputReadinessDeliveryReserve.load(std::memory_order_acquire);
	// A reset can update epoch-owned proof and queue fields concurrently. Fail
	// closed rather than hand the readiness controller a mixed-epoch snapshot.
	if (snapshotEpoch != m_queueEpoch.load(std::memory_order_acquire))
	{
		snapshot = {};
		return false;
	}
	return true;
}


bool CBufferedLiveSourceVideoOutputPin::GetLatencySnapshot(
	RendererLatencySnapshot& snapshot) const
{
	if (m_resetInProgress.load(std::memory_order_acquire) ||
		!m_latencySnapshotAvailable.load(std::memory_order_acquire))
	{
		snapshot = {};
		return false;
	}

	for (int attempt = 0; attempt < 4; ++attempt)
	{
		const uint64_t sequenceBefore =
			m_latencySnapshotSequence.load(std::memory_order_acquire);
		if ((sequenceBefore & 1ULL) != 0)
			continue;

		RendererLatencySnapshot candidate;
		candidate.supported = true;
		candidate.scheduledPresentationKnown =
			m_scheduledPresentationKnown.load(std::memory_order_relaxed);
		candidate.vpInternalMs =
			m_vpInternalLatencyMs.load(std::memory_order_relaxed);
		candidate.dsScheduleLeadMs =
			m_dsScheduleLeadMs.load(std::memory_order_relaxed);
		candidate.scheduledLatencyMs =
			m_scheduledLatencyMs.load(std::memory_order_relaxed);
		const uint64_t sequenceAfter =
			m_latencySnapshotSequence.load(std::memory_order_acquire);
		if (sequenceBefore == sequenceAfter)
		{
			snapshot = candidate;
			return true;
		}
	}

	snapshot = {};
	return false;
}


size_t CBufferedLiveSourceVideoOutputPin::PurgeQueue()
{
	const size_t purgedFrames = m_captureFrameQueue.Flush();
	m_publishedRawQueueDepth.store(0, std::memory_order_release);
	m_droppedFrameCount.fetch_add(purgedFrames, std::memory_order_relaxed);

	if (purgedFrames > 0)
	{
		DbgLog((LOG_TRACE, 1, TEXT("PurgeQueue(): Purged %zu raw frames"), purgedFrames));
	}
	return purgedFrames;
}


void CBufferedLiveSourceVideoOutputPin::PurgeConvertedQueue()
{
	// NOTE: Caller MUST hold m_convertedQueueLock
	const size_t purgedSamples = m_processedFrameQueue.Flush();
	m_publishedConvertedQueueDepth.store(0, std::memory_order_release);

	if (purgedSamples > 0)
	{
		DbgLog((LOG_TRACE, 1, TEXT("PurgeConvertedQueue(): Purged %zu pre-converted samples"), purgedSamples));
	}
}


void CBufferedLiveSourceVideoOutputPin::WriteLiveOutputTrace(const char* boundary)
{
	const std::vector<LiveOutputTraceRecord> eventRecords =
		m_liveOutputTrace.Snapshot();
	const std::vector<LiveOutputTraceRecord> metricRecords =
		m_liveOutputMetricsTrace.Snapshot();
	const std::vector<LiveOutputTraceRecord> convergenceRecords =
		m_liveConvergenceTrace.Snapshot();
	if (eventRecords.empty() && metricRecords.empty() &&
		convergenceRecords.empty())
		return;

	std::string traceDirectory = DebugLog::GetLogFilePath();
	const std::string::size_type separator =
		traceDirectory.find_last_of("\\\\/");
	if (separator == std::string::npos)
		traceDirectory.clear();
	else
		traceDirectory.resize(separator + 1);

	const uint64_t exportOrdinal =
		m_liveOutputTraceExportOrdinal.fetch_add(1, std::memory_order_relaxed) + 1;
	const uint64_t epoch = m_queueEpoch.load(std::memory_order_acquire);
	const std::string artifactBase =
		traceDirectory + "vp_live_output_trace-run-" +
		std::to_string(m_liveOutputTraceRunId) + "-" + boundary + "-" +
		std::to_string(exportOrdinal) + "-epoch-" + std::to_string(epoch);
	const std::string eventsPath = artifactBase + "-events.csv";
	const std::string metricsPath = artifactBase + "-metrics.csv";
	const std::string convergencePath = artifactBase + "-convergence.csv";
	const std::string manifestPath = artifactBase + "-manifest.json";

	const auto writeCsv = [&](const std::string& path,
		const std::vector<LiveOutputTraceRecord>& records,
		uint64_t droppedRecords,
		const char* artifactKind) -> bool
	{
		if (records.empty())
			return true;

		std::ofstream stream(path, std::ios::out | std::ios::trunc);
		if (!stream.is_open())
		{
			DebugLog::Log("LIVE OUTPUT TRACE: unable to write %s", path.c_str());
			return false;
		}

		stream << "# artifact_kind=" << artifactKind << '\n';
		stream << "# run_id=" << m_liveOutputTraceRunId << '\n';
		stream << "# downstream_renderer_queue_occupancy=unknown\n";
		stream << "# export_boundary=" << boundary << '\n';
		stream << "# dropped_trace_records=" << droppedRecords << '\n';
		LiveOutputTrace::WriteCsv(stream, records);
		return true;
	};

	LONG videoWidth = 0;
	LONG videoHeight = 0;
	std::string outputSubtype = "unknown";
	{
		CAutoLock mediaTypeLock(&m_mediaTypeLock);
		if (IsEqualGUID(m_mediaType.subtype, MEDIASUBTYPE_P010))
			outputSubtype = "P010";
		else if (IsEqualGUID(m_mediaType.subtype, MEDIASUBTYPE_P210))
			outputSubtype = "P210";

		if (IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo2) &&
			m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER2))
		{
			const VIDEOINFOHEADER2* info =
				reinterpret_cast<const VIDEOINFOHEADER2*>(m_mediaType.pbFormat);
			videoWidth = info->bmiHeader.biWidth;
			videoHeight = info->bmiHeader.biHeight;
		}
		else if (IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo) &&
			m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER))
		{
			const VIDEOINFOHEADER* info =
				reinterpret_cast<const VIDEOINFOHEADER*>(m_mediaType.pbFormat);
			videoWidth = info->bmiHeader.biWidth;
			videoHeight = info->bmiHeader.biHeight;
		}
	}

	bool hdrMetadataPresent = false;
	int ppmCorrection = 0;
	{
		CAutoLock timingLock(&m_timingStateLock);
		hdrMetadataPresent = m_hdrData != nullptr;
		ppmCorrection = static_cast<int>(
			m_currentRationalTrimNumerator - RATIONAL_TRIM_DENOMINATOR);
	}

	std::string configuredResetDelay = "unknown";
	char executablePath[MAX_PATH] = {};
	if (GetModuleFileNameA(nullptr, executablePath, MAX_PATH) > 0)
	{
		std::string configPath(executablePath);
		const std::string::size_type executableSeparator =
			configPath.find_last_of("\\\\/");
		if (executableSeparator != std::string::npos)
		{
			configPath.resize(executableSeparator + 1);
			configPath += ConfigFile::DEFAULT_FILENAME;
			ConfigFile config;
			if (config.Load(configPath))
				(void)config.TryGetString(
					"queue_recovery",
					"reset_after_render_restart_seconds",
					configuredResetDelay);
		}
	}

	std::ofstream manifest(manifestPath, std::ios::out | std::ios::trunc);
	if (manifest.is_open())
	{
		SYSTEMTIME exportedUtc = {};
		GetSystemTime(&exportedUtc);
		char exportedUtcText[32] = {};
		sprintf_s(exportedUtcText, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
			exportedUtc.wYear, exportedUtc.wMonth, exportedUtc.wDay,
			exportedUtc.wHour, exportedUtc.wMinute, exportedUtc.wSecond,
			exportedUtc.wMilliseconds);
		manifest << std::fixed << std::setprecision(6);
		manifest << "{\n";
		manifest << "  \"schema_version\": 4,\n";
		manifest << "  \"run_id\": " << m_liveOutputTraceRunId << ",\n";
		manifest << "  \"exported_utc\": \"" << exportedUtcText << "\",\n";
		manifest << "  \"export_boundary\": \"" << boundary << "\",\n";
		manifest << "  \"pipeline_epoch\": " << epoch << ",\n";
		manifest << "  \"timestamp_method\": \"" <<
			TimestampMethodName(m_timestamp) << "\",\n";
		manifest << "  \"timestamp_method_id\": " <<
			static_cast<int>(m_timestamp) << ",\n";
		manifest << "  \"presentation_lead_frames_configured\": " <<
			(m_presentationLeadFramesConfigured ? "true" : "false") << ",\n";
		manifest << "  \"presentation_lead_frames\": " <<
			m_presentationLeadFrames << ",\n";
		manifest << "  \"input_rate_numerator\": " << m_timeScale << ",\n";
		manifest << "  \"input_rate_denominator\": " << m_frameDurationTicks << ",\n";
		manifest << "  \"input_rate_hz\": " <<
			(m_frameDurationTicks > 0 ?
				static_cast<double>(m_timeScale) / m_frameDurationTicks : 0.0) <<
			",\n";
		manifest << "  \"input_signal\": \"" <<
			(hdrMetadataPresent ? "HDR" : "SDR") << "\",\n";
		manifest << "  \"hdr_metadata_present\": " <<
			(hdrMetadataPresent ? "true" : "false") << ",\n";
		manifest << "  \"video_width\": " << videoWidth << ",\n";
		manifest << "  \"video_height\": " <<
			(videoHeight < 0 ? -videoHeight : videoHeight) << ",\n";
		manifest << "  \"output_subtype\": \"" << outputSubtype << "\",\n";
		manifest << "  \"vp_queue_capacity\": " <<
			m_frameQueueMaxSize.load(std::memory_order_acquire) << ",\n";
		manifest << "  \"vp_buffering_target\": " << GetBufferingTarget() << ",\n";
		manifest << "  \"vp_delivery_reserve\": " << GetDeliveryReserve() << ",\n";
		manifest << "  \"vp_steady_target_scope\": \"converted_queue\",\n";
		manifest << "  \"vp_convergence_requires_raw_depth_known\": true,\n";
		manifest << "  \"vp_steady_hold\": \"pre-conversion-backpressure\",\n";
		manifest << "  \"vp_convergence_timestamp_catch_up\": "
			"\"final-delivery-boundary\",\n";
		manifest << "  \"vp_convergence_raw_target\": 0,\n";
		manifest << "  \"vp_steady_high_water\": " <<
			(IsSteadyQueueTargetConfigured() ?
				std::max<size_t>(1, GetConfiguredSteadyQueueTarget()) : 0) << ",\n";
		manifest << "  \"vp_convergence_minimum_block_us\": " <<
			LiveEpochConvergenceController::kMinimumIngressBlockUs << ",\n";
		manifest << "  \"vp_convergence_block_periods\": " <<
			LiveEpochConvergenceController::kIngressBlockPeriods << ",\n";
		manifest << "  \"vp_convergence_recovery_deliveries\": " <<
			LiveEpochConvergenceController::kRequiredRecoveryDeliveries << ",\n";
		manifest << "  \"vp_convergence_paced_warmup_deliveries\": " <<
			LiveEpochConvergenceController::kMinimumPacedWarmupDeliveries << ",\n";
		manifest << "  \"vp_convergence_paced_deliveries\": " <<
			LiveEpochConvergenceController::kRequiredPacedDeliveries << ",\n";
		manifest << "  \"vp_convergence_minimum_paced_priming_depth\": " <<
			LiveEpochConvergenceController::kMinimumPacedPrimingDepth << ",\n";
		manifest << "  \"vp_convergence_observation_timeout_ms\": " <<
			LiveEpochConvergenceController::kBlockObservationTimeoutMs << ",\n";
		manifest << "  \"vp_convergence_armed_window_ms\": " <<
			LiveEpochConvergenceController::kArmedConvergenceWindowMs << ",\n";
		manifest << "  \"vp_convergence_record_count\": " <<
			convergenceRecords.size() << ",\n";
		manifest << "  \"ppm_correction\": " << ppmCorrection << ",\n";
		manifest << "  \"rational_timing_shadow_comparisons\": " <<
			RationalTimingShadowComparisonCount() << ",\n";
		manifest << "  \"rational_timing_shadow_mismatches\": " <<
			RationalTimingShadowMismatchCount() << ",\n";
		manifest << "  \"rational_timing_controller_applied\": " <<
			RationalTimingControllerAppliedCount() << ",\n";
		manifest << "  \"measured_display_refresh_hz\": " <<
			m_sceneDisplayRefreshRateHz.load(std::memory_order_acquire) << ",\n";
		manifest << "  \"delivery_rate_hz\": " <<
			m_sceneDeliveryRateHz.load(std::memory_order_acquire) << ",\n";
		manifest << "  \"configured_post_renderer_start_reset_seconds\": \"" <<
			configuredResetDelay << "\",\n";
		manifest << "  \"reset_reason\": \"not_available_in_output_pin\",\n";
		manifest << "  \"output_sync_class\": \"operator_required_monitor_or_projector\",\n";
		manifest << "  \"madvr_cpu_queue_setting\": \"operator_required\",\n";
		manifest << "  \"madvr_gpu_queue_setting\": \"operator_required\",\n";
		manifest << "  \"madvr_queue_occupancy\": \"unobservable\"\n";
		manifest << "}\n";
	}
	else
	{
		DebugLog::Log(
			"LIVE OUTPUT TRACE: unable to write %s", manifestPath.c_str());
	}

	const bool eventsWritten = writeCsv(
		eventsPath,
		eventRecords,
		m_liveOutputTrace.DroppedRecordCount(),
		"events");
	const bool metricsWritten = writeCsv(
		metricsPath,
		metricRecords,
		m_liveOutputMetricsTrace.DroppedRecordCount(),
		"metrics");
	const bool convergenceWritten = writeCsv(
		convergencePath,
		convergenceRecords,
		m_liveConvergenceTrace.DroppedRecordCount(),
		"convergence");
	if (eventsWritten && metricsWritten && convergenceWritten &&
		manifest.is_open())
	{
		DebugLog::Log(
			"LIVE OUTPUT TRACE: run=%llu boundary=%s wrote events=%zu "
			"metrics=%zu convergence=%zu manifest=%s",
			m_liveOutputTraceRunId,
			boundary,
			eventRecords.size(),
			metricRecords.size(),
			convergenceRecords.size(),
			manifestPath.c_str());
	}
}


REFERENCE_TIME CBufferedLiveSourceVideoOutputPin::NextFrameTimestamp() const
{
	return CalculateEnhancedNextTimestamp();
}


REFERENCE_TIME CBufferedLiveSourceVideoOutputPin::CalculateEnhancedNextTimestamp() const
{
	// SAFETY: Check if timing clock is initialized
	if (!m_timingClock)
	{
		DbgLog((LOG_TRACE, 1, TEXT("CalculateEnhancedNextTimestamp(): Timing clock not initialized")));
		return REFERENCE_TIME_INVALID;
	}

	// If queue has next frame, use its hardware timestamp
	VideoFrame nextFrame{};
	if (m_captureFrameQueue.TryPeekCurrent(
		{ m_queueEpoch.load(std::memory_order_acquire) }, nextFrame))
	{
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
		metrics.currentSize = m_captureFrameQueue.Size();
	}

	{
		CAutoLock convLock(const_cast<CCritSec*>(&m_convertedQueueLock));
		metrics.convertedQueueSize = m_processedFrameQueue.Size();
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
	m_deliveryThreadId.store(GetCurrentThreadId(), std::memory_order_release);
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
	uint64_t frameIntervalUs = m_frameDuration > 0 ?
		static_cast<uint64_t>(m_frameDuration / 10) : 16667;
	uint64_t slowDeliveryThresholdUs = (frameIntervalUs * 150) / 100;
	DWORD lastFrameIntervalUpdateTime = GetTickCount();
	uint64_t lastSuccessfullyDeliveredEpoch = UINT64_MAX;
	uint64_t lastSuccessfullyDeliveredFrameNumber = 0;
	DirectShowDeliveryOutcomeClassifier deliveryOutcomeClassifier;
	LiveEpochConvergenceController epochConvergenceController;
	// VP-0066-9 owns normal Rational-Rational output time at the final
	// delivery boundary. Conversion may run ahead or stale live pictures may
	// later be removed, but only a successfully delivered sample advances this
	// presentation sequence. Scene cadence remains a separate owner while it is
	// active and already stamps its samples on this same delivery thread.
	RationalLiveOutputSequencer deliveryTimestampSequencer(
		m_timeScale, m_frameDurationTicks, m_frameDuration);
	uint64_t rationalSourceGapSlotsToSuppress = 0;
	bool rationalCatchUpAnchorValid = false;
	REFERENCE_TIME rationalCatchUpMinimumStart = 0;
	uint64_t rationalLatchEpoch = 0;
	DirectShowLiveTimestampCatchUp legacyTimestampCatchUp;
	uint64_t legacyConvertedCatchUpEpoch = 0;
	bool legacyConvertedCatchUpPending = false;
	uint64_t legacyIntentionalRawGapEpoch = 0;
	bool legacyIntentionalRawGapPending = false;
	LiveOutputTraceRecord latestTimelineSnapshot;
	bool latestTimelineSnapshotAvailable = false;
	uint64_t latencyClockDiscontinuityLoggedEpoch = 0;
	bool downstreamRejectedUntilNewEpoch = false;
	uint64_t downstreamRejectedEpoch = 0;

	// When Scene Detect is enabled, this delivery-thread-only planner selects
	// display-rate slots and whole-picture repeat/drop actions. The unified
	// deliveryTimestampSequencer remains the only final DirectShow timestamp
	// owner; the anchor/index below are planner coordinates only.
	struct SceneOutputCadence
	{
		bool active = false;
		uint64_t generation = 0;
		uint64_t queueEpoch = 0;
		double displayRateHz = 0.0;
		long double anchor = 0.0L;
		uint64_t nextOutputIndex = 0;
		// Signed outstanding timing debt in frames. A repeat pays one frame of
		// positive debt; a drop pays one frame of negative debt. If either is
		// taken early or late at a scene cut, the non-zero remainder is retained
		// and directly shifts the next correction deadline.
		long double contentPhaseFrames = 0.0L;
	};
	SceneOutputCadence sceneCadence;
	struct PendingUpstreamRepeat
	{
		IMediaSample* sample = nullptr;
		uint64_t queueEpoch = 0;
		uint64_t timingGeneration = 0;
		uint64_t frameNumber = 0;
		uint64_t captureTimestamp = 0;
		uint64_t captureArrivalTick = 0;
		uint32_t processingDurationUs = 0;
		uint64_t sceneEventId = 0;
		long double phaseBefore = 0.0L;
		double secondsFromDeadline = 0.0;
		bool atSceneBoundary = false;
	};
	PendingUpstreamRepeat pendingUpstreamRepeat;
	// Advanced scene correction can add or remove a presentation sample. Keep
	// the optional media-time stream continuous with that output cadence. This
	// is delivery-thread-only and is reset with the scene cadence, so the
	// ordinary source/media timeline is untouched when Scene Detect is off or
	// when Basic (renderer-gap) mode is selected.
	LONGLONG advancedMediaTimeOffset = 0;

	const auto resetSceneCadence = [&]()
	{
		if (pendingUpstreamRepeat.sample)
			pendingUpstreamRepeat.sample->Release();
		pendingUpstreamRepeat = {};
		sceneCadence = {};
		advancedMediaTimeOffset = 0;
		m_scenePhasePpmUnits.store(0, std::memory_order_release);
		m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_release);
		m_sceneSecondsUntilPlan.store(0.0, std::memory_order_release);
		m_scenePredictedAction.store(0, std::memory_order_release);
		m_sceneCorrectionPlanned.store(false, std::memory_order_release);
		m_sceneLastCorrectionAction.store(0, std::memory_order_release);
		m_sceneLastCorrectionSecondsFromDeadline.store(0.0, std::memory_order_release);
		m_sceneLastCorrectionTick.store(0, std::memory_order_release);
	};

	const auto applyAdvancedMediaTimeOffset =
		[](IMediaSample* sample, LONGLONG offset) -> bool
	{
		if (!sample || offset == 0)
			return true;

		LONGLONG mediaStart = 0;
		LONGLONG mediaStop = 0;
		if (FAILED(sample->GetMediaTime(&mediaStart, &mediaStop)))
			return true; // Media time is optional on DirectShow samples.

		const LONGLONG maxValue = (std::numeric_limits<LONGLONG>::max)();
		const LONGLONG minValue = (std::numeric_limits<LONGLONG>::min)();
		if ((offset > 0 &&
				(mediaStart > maxValue - offset || mediaStop > maxValue - offset)) ||
			(offset < 0 &&
				(mediaStart < minValue - offset || mediaStop < minValue - offset)))
			return false;

		mediaStart += offset;
		mediaStop += offset;
		return SUCCEEDED(sample->SetMediaTime(&mediaStart, &mediaStop));
	};

	const auto sceneOutputTime = [&sceneCadence](uint64_t outputIndex) -> REFERENCE_TIME
	{
		const long double ticks =
			(static_cast<long double>(outputIndex) * REFERENCE_TIME_TICKS_PER_SECOND) /
			sceneCadence.displayRateHz;
		return static_cast<REFERENCE_TIME>(llround(sceneCadence.anchor + ticks));
	};

	const auto readConfirmedRunningStreamTime =
		[this](REFERENCE_TIME& streamTime) -> bool
	{
		streamTime = REFERENCE_TIME_INVALID;
		if (!m_pFilter)
			return false;
		FILTER_STATE before = State_Stopped;
		FILTER_STATE after = State_Stopped;
		if (m_pFilter->GetState(0, &before) != S_OK ||
			before != State_Running)
			return false;
		const REFERENCE_TIME observed = NowStreamTime(m_pFilter);
		if (observed == REFERENCE_TIME_INVALID ||
			m_pFilter->GetState(0, &after) != S_OK ||
			after != State_Running)
			return false;
		streamTime = observed;
		return true;
	};

	auto deliverTracked = [&](IMediaSample* sample,
		uint64_t expectedQueueEpoch,
		uint64_t frameNumber,
		uint64_t captureTimestamp,
		uint64_t captureArrivalTick,
		uint32_t processingDurationUs,
		bool sceneBoundary,
		RationalLiveOutputCadence outputCadence,
		double displayRateHz,
		uint32_t presentationGapSlotsBefore,
		bool sourceDiscontinuity) -> HRESULT
	{
		CAutoLock deliveryLock(&m_deliveryGate);
		if (m_deliveryFlushing.load(std::memory_order_acquire) ||
			expectedQueueEpoch != m_queueEpoch.load(std::memory_order_acquire))
			return VFW_E_WRONG_STATE;

		m_deliveryAttemptCount.fetch_add(1, std::memory_order_relaxed);
		m_lastDeliveryStartTick.store(
			GetTickCount64(), std::memory_order_release);
		m_deliveryInProgress.store(true, std::memory_order_release);
		const REFERENCE_TIME observedClockTime = NowStreamTime(m_pFilter);
		REFERENCE_TIME runningClockBeforeDelivery = REFERENCE_TIME_INVALID;
		const bool graphRunningBeforeDelivery =
			readConfirmedRunningStreamTime(runningClockBeforeDelivery);
		const bool timestampOwnershipEnabled =
			m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL;
		const bool catchUpAnchorForAttempt =
			timestampOwnershipEnabled && rationalCatchUpAnchorValid;
		RationalLiveOutputTimestampDecision timestampDecision;
		if (timestampOwnershipEnabled)
		{
			RationalLiveOutputTimestampInput timestampInput;
			timestampInput.epoch = expectedQueueEpoch;
			timestampInput.ppmCorrection = GetCurrentPPMCorrection();
			timestampInput.pipelineOffset = GetRationalPipelineOffset();
			timestampInput.presentationLead = GetRampedLeadTime();
			timestampInput.cadence = outputCadence;
			timestampInput.displayRateHz = displayRateHz;
			timestampInput.sourceDiscontinuity = sourceDiscontinuity;
			timestampInput.presentationGapSlotsBefore =
				presentationGapSlotsBefore;
			timestampInput.sourceFrameNumber = frameNumber;
			timestampInput.sourceFrameNumberValid = true;
			// Source identity is recovery telemetry, not presentation identity. The
			// Rational owner advances only on successful delivery; otherwise ordinary
			// counter gaps become explicit PTS holes and force madVR repeats. Planned
			// VP catch-up remains recorded through sourceGapSlotsToSuppress.
			timestampInput.sourceGapSlotsToSuppress =
				rationalSourceGapSlotsToSuppress;
			timestampInput.minimumPresentationStartValid =
				catchUpAnchorForAttempt;
			timestampInput.minimumPresentationStart =
				rationalCatchUpMinimumStart;
			timestampDecision = deliveryTimestampSequencer.Preview(timestampInput);
			if (timestampDecision.valid &&
				timestampDecision.observedSourceGapMaterial &&
				graphRunningBeforeDelivery &&
				runningClockBeforeDelivery != REFERENCE_TIME_INVALID)
			{
				// A material internal latest-wins gap is too large to encode as
				// N synthetic PTS slots. Rebase this same transactional preview to
				// the confirmed Running graph clock instead of leaving it late.
				timestampInput.minimumPresentationStartValid = true;
				timestampInput.minimumPresentationStart =
					runningClockBeforeDelivery + timestampInput.presentationLead;
				timestampDecision =
					deliveryTimestampSequencer.Preview(timestampInput);
				DebugLog::Log(
					"VP-0066 RATIONAL MATERIAL CATCH-UP: epoch=%llu "
					"observed_gap=%llu minimum_start=%.3fms",
					expectedQueueEpoch,
					timestampDecision.observedSourceGapSlotsBefore,
					timestampInput.minimumPresentationStart / 10000.0);
			}
			if (!timestampDecision.valid ||
				FAILED(sample->SetTime(
					&timestampDecision.start, &timestampDecision.stop)) ||
				FAILED(sample->SetMediaTime(
					&timestampDecision.mediaStart, &timestampDecision.mediaStop)) ||
				FAILED(sample->SetDiscontinuity(
					timestampDecision.discontinuity ? TRUE : FALSE)))
			{
				DebugLog::Log(
					"VP-0066-9 DELIVERY TIMESTAMP OWNER: sample stamp failed "
					"(epoch=%llu); requesting serialized reset",
					expectedQueueEpoch);
				m_deliveryInProgress.store(false, std::memory_order_release);
				RequestCoordinatedReset("delivery-timestamp-stamp-failure");
				return E_FAIL;
			}
		}
		else if (sourceDiscontinuity &&
			FAILED(sample->SetDiscontinuity(TRUE)))
		{
			DebugLog::Log(
				"DELIVERY THREAD: source discontinuity stamp failed "
				"(epoch=%llu); requesting serialized reset",
				expectedQueueEpoch);
			m_deliveryInProgress.store(false, std::memory_order_release);
			RequestCoordinatedReset("source-discontinuity-stamp-failure");
			return E_FAIL;
		}
		const DirectShowDeliveryTicket deliveryTicket =
			m_directShowFrameDeliverer.Begin(
				sample,
				[this](IMediaSample* deliverySample)
				{
					return AttachPendingMediaType(deliverySample);
				},
				[]()
				{
					return GetWallClockTime();
				});

		REFERENCE_TIME presentationStart = 0;
		REFERENCE_TIME presentationStop = 0;
		const HRESULT presentationTimeResult =
			sample->GetTime(&presentationStart, &presentationStop);
		const uint64_t deliveryAttemptTick = GetTickCount64();
		REFERENCE_TIME streamTime = REFERENCE_TIME_INVALID;
		if (observedClockTime != REFERENCE_TIME_INVALID)
		{
			// Legacy capture-clock and Rational timestamps are epoch-relative. The
			// raw graph clock can briefly be absolute before Run, so normalize it
			// strictly for diagnostics; it never owns sample timestamps here.
			const bool normalized = m_latencyStreamTimeNormalizer.Normalize(
				expectedQueueEpoch, observedClockTime, streamTime);
			const bool relativeClockRebased =
				m_latencyStreamTimeNormalizer.LastObservationRebased();
			if (relativeClockRebased)
			{
				m_latencyStabilizer.Reset();
				m_latencySnapshotAvailable.store(false, std::memory_order_release);
			}
			if (relativeClockRebased &&
				latencyClockDiscontinuityLoggedEpoch != expectedQueueEpoch)
			{
				latencyClockDiscontinuityLoggedEpoch = expectedQueueEpoch;
				DebugLog::Log(
					"VP PTS TIMING REBASE: epoch=%llu reason=graph-stream-time-rollback "
					"observed_clock_100ns=%lld; latency telemetry rewarming",
					expectedQueueEpoch,
					static_cast<long long>(observedClockTime));
			}
			if (!normalized)
				streamTime = REFERENCE_TIME_INVALID;
		}
		RendererLatencySnapshot latencySnapshot;
		RendererLatencySnapshot displayedLatencySnapshot;
		bool latencyDisplayReady = false;
		if (CalculateVpInternalLatency(
			captureArrivalTick, deliveryAttemptTick, latencySnapshot))
		{
			// Every timestamp method shares the VP-owned monotonic residence
			// boundary.  Add the DirectShow scheduling boundary whenever that
			// method supplied a sample start time and the graph clock is valid.
			// Start-only samples return a success status from GetTime and are
			// therefore supported; DS_SSTM_NONE intentionally reports only VP
			// internal residence rather than retaining a stale scheduled value.
			if (SUCCEEDED(presentationTimeResult) &&
				streamTime != REFERENCE_TIME_INVALID)
			{
				CalculateScheduledLatency(
					captureArrivalTick, deliveryAttemptTick,
					presentationStart, streamTime, latencySnapshot);
			}
			latencyDisplayReady = m_latencyStabilizer.Observe(
				expectedQueueEpoch, deliveryAttemptTick,
				latencySnapshot, displayedLatencySnapshot);
			if (latencyDisplayReady)
			{
				const bool wasAvailable = m_latencySnapshotAvailable.load(
					std::memory_order_acquire);
				m_latencySnapshotSequence.fetch_add(1, std::memory_order_acq_rel);
				m_scheduledPresentationKnown.store(
					displayedLatencySnapshot.scheduledPresentationKnown,
					std::memory_order_relaxed);
				m_vpInternalLatencyMs.store(
					displayedLatencySnapshot.vpInternalMs,
					std::memory_order_relaxed);
				m_dsScheduleLeadMs.store(
					displayedLatencySnapshot.dsScheduleLeadMs,
					std::memory_order_relaxed);
				m_scheduledLatencyMs.store(
					displayedLatencySnapshot.scheduledLatencyMs,
					std::memory_order_relaxed);
				m_latencySnapshotSequence.fetch_add(1, std::memory_order_release);
				m_latencySnapshotAvailable.store(true, std::memory_order_release);
				if (!wasAvailable)
				{
					DebugLog::Log(
						"VP LATENCY METRIC READY: epoch=%llu vp_internal=%.2fms "
						"pts_known=%d pts_lead=%.2fms vp_to_requested_pts=%.2fms "
						"startup_ignore_ms=%llu evidence_ms=%llu",
						expectedQueueEpoch,
						displayedLatencySnapshot.vpInternalMs,
						displayedLatencySnapshot.scheduledPresentationKnown ? 1 : 0,
						displayedLatencySnapshot.dsScheduleLeadMs,
						displayedLatencySnapshot.scheduledLatencyMs,
						RendererLatencyStabilizer::IGNORE_MS,
						RendererLatencyStabilizer::EVIDENCE_MS);
				}
				// The first stable sample establishes that the measurement is usable,
				// but a live-capture regression can be a slow PTS-lead drift. Keep a
				// lightweight, delivery-thread-owned steady-state trace so diagnosis
				// does not depend on exporting the trace only after a later reset.
				const DWORD latencyNow = GetTickCount();
				if (lastLatencyLogTime == 0 ||
					latencyNow - lastLatencyLogTime >= 10000)
				{
					const size_t rawDepth = m_publishedRawQueueDepth.load(
						std::memory_order_acquire);
					const size_t convertedDepth =
						m_publishedConvertedQueueDepth.load(
							std::memory_order_acquire);
					DebugLog::Log(
						"VP LATENCY (10s): epoch=%llu method=%s frame=%llu "
						"vp_internal=%.2fms pts_lead=%s%.2fms scheduled=%s%.2fms "
						"pts_start=%lld graph_time=%lld queue=%zu/%zu/%zu target=%zu "
						"source_gap=%u ppm=%d deliveries=%llu",
						static_cast<unsigned long long>(expectedQueueEpoch),
						TimestampMethodName(m_timestamp),
						static_cast<unsigned long long>(frameNumber),
						displayedLatencySnapshot.vpInternalMs,
						displayedLatencySnapshot.scheduledPresentationKnown ? "" : "N/A ",
						displayedLatencySnapshot.dsScheduleLeadMs,
						displayedLatencySnapshot.scheduledPresentationKnown ? "" : "N/A ",
						displayedLatencySnapshot.scheduledLatencyMs,
						static_cast<long long>(presentationStart),
						static_cast<long long>(streamTime), rawDepth, convertedDepth,
						rawDepth + convertedDepth,
						GetConfiguredSteadyQueueTarget(),
						timestampDecision.sourceGapSlotsBefore,
						GetCurrentPPMCorrection(),
						static_cast<unsigned long long>(framesSinceLastLog));
					lastLatencyLogTime = latencyNow;
					framesSinceLastLog = 0;
				}
			}
			else
			{
				m_latencySnapshotAvailable.store(false, std::memory_order_release);
			}
		}
		else
		{
			m_latencySnapshotAvailable.store(false, std::memory_order_release);
		}
		LiveOutputTraceRecord deliveryAttemptTrace;
		deliveryAttemptTrace.kind = LiveOutputTraceKind::DeliveryAttempted;
		deliveryAttemptTrace.frameNumber = frameNumber;
		deliveryAttemptTrace.pipelineEpoch = expectedQueueEpoch;
		deliveryAttemptTrace.captureTimestamp = captureTimestamp;
		deliveryAttemptTrace.captureArrivalTick = captureArrivalTick;
		deliveryAttemptTrace.eventTick = deliveryAttemptTick;
		deliveryAttemptTrace.presentationStart = presentationStart;
		deliveryAttemptTrace.presentationStop = presentationStop;
		deliveryAttemptTrace.streamTime = streamTime;
		deliveryAttemptTrace.observedClockTime = observedClockTime;
		deliveryAttemptTrace.vpInternalUs = static_cast<int64_t>(
			llround(latencySnapshot.vpInternalMs * 1000.0));
		deliveryAttemptTrace.dsScheduleLeadUs = static_cast<int64_t>(
			llround(latencySnapshot.dsScheduleLeadMs * 1000.0));
		deliveryAttemptTrace.scheduledLatencyUs = static_cast<int64_t>(
			llround(latencySnapshot.scheduledLatencyMs * 1000.0));
		deliveryAttemptTrace.scheduledLatencyKnown =
			latencySnapshot.scheduledPresentationKnown;
		deliveryAttemptTrace.latencyDisplayReady = latencyDisplayReady;
		if (latencyDisplayReady)
		{
			deliveryAttemptTrace.displayedVpInternalUs = static_cast<int64_t>(
				llround(displayedLatencySnapshot.vpInternalMs * 1000.0));
			deliveryAttemptTrace.displayedDsScheduleLeadUs = static_cast<int64_t>(
				llround(displayedLatencySnapshot.dsScheduleLeadMs * 1000.0));
			deliveryAttemptTrace.displayedScheduledLatencyUs = static_cast<int64_t>(
				llround(displayedLatencySnapshot.scheduledLatencyMs * 1000.0));
		}
		if (timestampDecision.valid)
		{
			deliveryAttemptTrace.mediaStart = timestampDecision.mediaStart;
			deliveryAttemptTrace.mediaStop = timestampDecision.mediaStop;
			deliveryAttemptTrace.outputSequence = timestampDecision.outputSequence;
		}
		deliveryAttemptTrace.timestampOwner = timestampOwnershipEnabled ?
			(outputCadence == RationalLiveOutputCadence::Display ? 2 : 1) :
			(outputCadence == RationalLiveOutputCadence::Display ? 2 : 0);
		deliveryAttemptTrace.timestampMethod = static_cast<uint8_t>(m_timestamp);
		deliveryAttemptTrace.sourceGapSlotsBefore =
			timestampDecision.valid ? timestampDecision.sourceGapSlotsBefore : 0;
		deliveryAttemptTrace.observedSourceGapSlotsBefore =
			timestampDecision.valid ? static_cast<uint32_t>(std::min<uint64_t>(
				timestampDecision.observedSourceGapSlotsBefore,
				(std::numeric_limits<uint32_t>::max)())) : 0;
		deliveryAttemptTrace.sourceGapSuppressed =
			timestampDecision.valid && timestampDecision.sourceGapSuppressed;
		deliveryAttemptTrace.intentionalSourceGapSlotsSuppressed =
			timestampDecision.valid ? static_cast<uint32_t>(std::min<uint64_t>(
				timestampDecision.intentionalSourceGapSlotsSuppressed,
				(std::numeric_limits<uint32_t>::max)())) : 0;
		deliveryAttemptTrace.materialSourceGapSuppressed =
			timestampDecision.valid &&
			timestampDecision.materialSourceGapSuppressed;
		deliveryAttemptTrace.sourceDiscontinuity = sourceDiscontinuity;
		deliveryAttemptTrace.rawQueueDepth = static_cast<uint32_t>(
			m_publishedRawQueueDepth.load(std::memory_order_acquire));
		deliveryAttemptTrace.convertedQueueDepth = static_cast<uint32_t>(
			m_publishedConvertedQueueDepth.load(std::memory_order_acquire));
		deliveryAttemptTrace.totalQueueDepth =
			deliveryAttemptTrace.rawQueueDepth +
			deliveryAttemptTrace.convertedQueueDepth;
		deliveryAttemptTrace.queueCapacity = static_cast<uint32_t>(
			m_frameQueueMaxSize.load(std::memory_order_acquire));
		deliveryAttemptTrace.processingDurationUs = processingDurationUs;
		deliveryAttemptTrace.sceneBoundary = sceneBoundary;
		m_liveOutputTrace.Record(deliveryAttemptTrace);
		const DirectShowDeliveryResult deliveryResult =
			m_directShowFrameDeliverer.Complete(
				deliveryTicket,
				[this](IMediaSample* deliverySample)
				{
					return Deliver(deliverySample);
				},
				[this](uint64_t mediaTypeGeneration, HRESULT result)
				{
					CompletePendingMediaType(mediaTypeGeneration, result);
				},
				[]()
				{
					return GetWallClockTime();
				});
		m_deliveryInProgress.store(false, std::memory_order_release);
		const HRESULT result = deliveryResult.result;
		const uint64_t deliveryTimeUs = deliveryResult.durationUs;
		LiveOutputTraceRecord deliveryCompleteTrace = deliveryAttemptTrace;
		deliveryCompleteTrace.kind = LiveOutputTraceKind::DeliveryCompleted;
		deliveryCompleteTrace.eventTick = GetTickCount64();
		deliveryCompleteTrace.deliveryDurationUs = static_cast<uint32_t>(
			std::min<uint64_t>(deliveryTimeUs, std::numeric_limits<uint32_t>::max()));
		deliveryCompleteTrace.deliveryResult = static_cast<int32_t>(result);
		m_liveOutputTrace.Record(deliveryCompleteTrace);
		latestTimelineSnapshot = deliveryCompleteTrace;
		latestTimelineSnapshotAvailable = true;

		totalDeliveryTimeUs += deliveryTimeUs;
		maxDeliveryTimeUs = std::max(maxDeliveryTimeUs, deliveryTimeUs);
		if (deliveryTimeUs > 0)
			minDeliveryTimeUs = std::min(minDeliveryTimeUs, deliveryTimeUs);
		++totalDeliveryCount;
		++totalDeliveryCount1Min;
		const DirectShowDeliveryOutcome outcome = deliveryOutcomeClassifier.Classify(
			{ result, deliveryTimeUs, slowDeliveryThresholdUs });
		switch (outcome.latencyClass)
		{
		case DirectShowDeliveryLatencyClass::Instant:
			++instantDeliveryCount;
			++instantDeliveryCount1Min;
			break;
		case DirectShowDeliveryLatencyClass::Normal:
			++normalDeliveryCount;
			++normalDeliveryCount1Min;
			break;
		case DirectShowDeliveryLatencyClass::Slow:
			++slowDeliveryCount;
			++slowDeliveryCount1Min;
			break;
		}

		if (outcome.deliveryFailed)
		{
			if (outcome.countDroppedFrame)
				m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
			if (outcome.incrementRecentFailures)
				m_recentDeliveryFailures.fetch_add(1, std::memory_order_relaxed);
			++deliveryFailureCount;
				++deliveryFailuresSinceLastLog;
			LiveEpochConvergenceInput convergenceInput;
			convergenceInput.epoch = expectedQueueEpoch;
			convergenceInput.epochActive =
				!m_isBuffering.load(std::memory_order_acquire) &&
				expectedQueueEpoch ==
					m_queueEpoch.load(std::memory_order_acquire);
			convergenceInput.vpConvertedDepth = m_processedFrameQueue.Size();
			convergenceInput.targetConfigured = IsSteadyQueueTargetConfigured();
			convergenceInput.desiredVpDepth = GetConfiguredSteadyQueueTarget();
			convergenceInput.deliveryCompleted = true;
			convergenceInput.deliverySucceeded = false;
			convergenceInput.deliveryDurationUs = deliveryTimeUs;
			convergenceInput.nominalFrameDurationUs = frameIntervalUs;
			convergenceInput.vpRawDepth = m_captureFrameQueue.Size();
			convergenceInput.rawDepthKnown = true;
			convergenceInput.resetOrFlushInProgress =
				m_resetInProgress.load(std::memory_order_acquire) ||
				m_deliveryFlushing.load(std::memory_order_acquire);
			convergenceInput.sceneCadenceActive =
				outputCadence == RationalLiveOutputCadence::Display;
			convergenceInput.observationTickMs = GetTickCount64();
			const LiveEpochConvergenceDecision failureConvergenceDecision =
				epochConvergenceController.Observe(convergenceInput);
			if (convergenceInput.targetConfigured)
			{
				LiveOutputTraceRecord convergenceTrace = deliveryCompleteTrace;
				convergenceTrace.kind = LiveOutputTraceKind::ConvergenceState;
				convergenceTrace.queueTarget = static_cast<uint32_t>(
					convergenceInput.desiredVpDepth);
				convergenceTrace.convergenceSuccessCount =
					failureConvergenceDecision.successfulDeliveryCount;
				convergenceTrace.convergenceBlockCount =
					failureConvergenceDecision.ingressBlockCount;
				convergenceTrace.convergenceRecoveryStreak =
					failureConvergenceDecision.consecutiveRecoveryDeliveryCount;
				convergenceTrace.convergencePacedStreak =
					failureConvergenceDecision.consecutivePacedDeliveryCount;
				convergenceTrace.convergenceBlockThresholdUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						failureConvergenceDecision.ingressBlockThresholdUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergenceNormalThresholdUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						failureConvergenceDecision.normalDeliveryThresholdUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergencePacedMinimumUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						failureConvergenceDecision.pacedDeliveryMinimumUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergencePacedMaximumUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						failureConvergenceDecision.pacedDeliveryMaximumUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergencePacedPrimingDepth =
					static_cast<uint32_t>(failureConvergenceDecision.pacedPrimingDepth);
				convergenceTrace.convergenceElapsedMs =
					static_cast<uint32_t>(std::min<uint64_t>(
						failureConvergenceDecision.elapsedSinceFirstSuccessMs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergenceRawZero =
					failureConvergenceDecision.rawDepthKnown &&
					!failureConvergenceDecision.rawBacklogObserved;
				convergenceTrace.convergenceRawBacklog =
					failureConvergenceDecision.rawBacklogObserved;
				convergenceTrace.convergenceState = static_cast<uint8_t>(
					failureConvergenceDecision.state);
				convergenceTrace.convergenceReason = static_cast<uint8_t>(
					failureConvergenceDecision.reason);
				convergenceTrace.convergenceActivation = static_cast<uint8_t>(
					failureConvergenceDecision.activation);
				m_liveOutputTrace.Record(convergenceTrace);
				m_liveConvergenceTrace.Record(convergenceTrace);
			}
			const DWORD failureNow = GetTickCount();
			if (lastDeliveryFailureLogTime == 0 || failureNow - lastDeliveryFailureLogTime >= 5000)
			{
				DebugLog::Log("DELIVERY THREAD: Deliver() failed %llu time(s) in the last interval; last hr=0x%08x (consecutive=%u)",
					deliveryFailuresSinceLastLog, result, m_recentDeliveryFailures.load());
				deliveryFailuresSinceLastLog = 0;
				lastDeliveryFailureLogTime = failureNow;
			}
		}
		else if (outcome.deliverySucceeded)
		{
			uint64_t maximumDeliveryDuration =
				m_maximumSuccessfulDeliveryDurationUs.load(
					std::memory_order_relaxed);
			while (deliveryTimeUs > maximumDeliveryDuration &&
				!m_maximumSuccessfulDeliveryDurationUs.compare_exchange_weak(
					maximumDeliveryDuration, deliveryTimeUs,
					std::memory_order_release, std::memory_order_relaxed))
			{
			}
			const bool timestampCommitSucceeded =
				!timestampOwnershipEnabled ||
				deliveryTimestampSequencer.Commit(timestampDecision);
			if (!timestampCommitSucceeded)
			{
				DebugLog::Log(
					"VP-0066-9 DELIVERY TIMESTAMP OWNER: failed to commit "
					"successful delivery sequence (epoch=%llu)",
					expectedQueueEpoch);
			}
			else if (timestampDecision.valid &&
				timestampDecision.observedSourceGapSlotsBefore > 0)
			{
				DebugLog::Log(
					"VP-0066 RATIONAL SOURCE GAP OBSERVED: epoch=%llu "
					"frame=%llu missing_slots=%llu pts_action=contiguous",
					expectedQueueEpoch, frameNumber,
					static_cast<unsigned long long>(
						timestampDecision.observedSourceGapSlotsBefore));
			}
			if (timestampCommitSucceeded && timestampDecision.valid)
			{
				if (timestampDecision.intentionalSourceGapSlotsSuppressed > 0)
				{
					rationalSourceGapSlotsToSuppress =
						timestampDecision.intentionalSourceGapSlotsSuppressed >=
							rationalSourceGapSlotsToSuppress ? 0 :
						rationalSourceGapSlotsToSuppress -
							timestampDecision.intentionalSourceGapSlotsSuppressed;
				}
				if (sourceDiscontinuity)
				{
					// A source-counter reset establishes a new identity domain. Any
					// unconsumed queue/scene suppression belonged to the old domain.
					rationalSourceGapSlotsToSuppress = 0;
				}
				if (catchUpAnchorForAttempt)
				{
					rationalCatchUpAnchorValid = false;
					rationalCatchUpMinimumStart = 0;
				}
			}
			if (outcome.clearRecentFailures)
				m_recentDeliveryFailures.store(0, std::memory_order_relaxed);
			if (DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(m_timestamp) &&
				SUCCEEDED(presentationTimeResult) &&
				presentationStop > presentationStart)
			{
				legacyTimestampCatchUp.CommitSuccessfulStop(
					expectedQueueEpoch, presentationStop);
			}
			m_currentEpochDeliverySuccessCount.fetch_add(
				1, std::memory_order_acq_rel);
			m_lastDeliverySuccessQueueEpoch.store(
				expectedQueueEpoch, std::memory_order_release);
			m_deliverySuccessCount.fetch_add(1, std::memory_order_relaxed);
			m_lastDeliverySuccessTick.store(
				GetTickCount64(), std::memory_order_release);

			// After a synchronous downstream hard block has recovered, or sustained
			// frame-paced ingress plus local backlog proves the initial burst is over,
			// perform one live catch-up for this epoch:
			// discard stale raw backlog and retain the configured converted reserve.
			// The delivery sequencer owns final timestamps and advances only after a
			// successful delivery. These are black-box downstream
			// pacing/backpressure signals, never madVR occupancy measurements or a
			// claim about physical presentation readiness.
			const bool steadyTargetConfigured =
				IsSteadyQueueTargetConfigured();
			const size_t desiredVpDepth = GetConfiguredSteadyQueueTarget();
			const size_t rawDepthBeforeConvergence =
				m_captureFrameQueue.Size();
			const size_t convertedDepthBeforeConvergence =
				m_processedFrameQueue.Size();
			LiveEpochConvergenceInput convergenceInput;
			convergenceInput.epoch = expectedQueueEpoch;
			convergenceInput.epochActive =
				!m_isBuffering.load(std::memory_order_acquire) &&
				expectedQueueEpoch ==
					m_queueEpoch.load(std::memory_order_acquire);
			convergenceInput.vpConvertedDepth =
				convertedDepthBeforeConvergence;
			convergenceInput.desiredVpDepth = desiredVpDepth;
			convergenceInput.targetConfigured = steadyTargetConfigured;
			convergenceInput.deliveryCompleted = true;
			convergenceInput.deliverySucceeded = true;
			convergenceInput.deliveryDurationUs = deliveryTimeUs;
			convergenceInput.nominalFrameDurationUs = frameIntervalUs;
			convergenceInput.vpRawDepth = rawDepthBeforeConvergence;
			convergenceInput.rawDepthKnown = true;
			convergenceInput.resetOrFlushInProgress =
				m_resetInProgress.load(std::memory_order_acquire) ||
				m_deliveryFlushing.load(std::memory_order_acquire);
			convergenceInput.sceneCadenceActive =
				outputCadence == RationalLiveOutputCadence::Display;
			convergenceInput.observationTickMs = GetTickCount64();
			const LiveEpochConvergenceDecision convergenceDecision =
				epochConvergenceController.Observe(convergenceInput);
			if (steadyTargetConfigured)
			{
				LiveOutputTraceRecord convergenceProbe = deliveryCompleteTrace;
				convergenceProbe.kind = LiveOutputTraceKind::ConvergenceState;
				convergenceProbe.rawQueueDepth = static_cast<uint32_t>(
					rawDepthBeforeConvergence);
				convergenceProbe.convertedQueueDepth = static_cast<uint32_t>(
					convertedDepthBeforeConvergence);
				convergenceProbe.totalQueueDepth =
					convergenceProbe.rawQueueDepth +
					convergenceProbe.convertedQueueDepth;
				convergenceProbe.queueTarget = static_cast<uint32_t>(desiredVpDepth);
				convergenceProbe.queueDepthBefore = static_cast<uint32_t>(
					convertedDepthBeforeConvergence);
				convergenceProbe.convergenceSuccessCount =
					convergenceDecision.successfulDeliveryCount;
				convergenceProbe.convergenceBlockCount =
					convergenceDecision.ingressBlockCount;
				convergenceProbe.convergenceRecoveryStreak =
					convergenceDecision.consecutiveRecoveryDeliveryCount;
				convergenceProbe.convergencePacedStreak =
					convergenceDecision.consecutivePacedDeliveryCount;
				convergenceProbe.convergenceBlockThresholdUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.ingressBlockThresholdUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceProbe.convergenceNormalThresholdUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.normalDeliveryThresholdUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceProbe.convergencePacedMinimumUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.pacedDeliveryMinimumUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceProbe.convergencePacedMaximumUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.pacedDeliveryMaximumUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceProbe.convergencePacedPrimingDepth =
					static_cast<uint32_t>(convergenceDecision.pacedPrimingDepth);
				convergenceProbe.convergenceElapsedMs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.elapsedSinceFirstSuccessMs,
						std::numeric_limits<uint32_t>::max()));
				convergenceProbe.convergenceRawZero =
					convergenceDecision.rawDepthKnown &&
					!convergenceDecision.rawBacklogObserved;
				convergenceProbe.convergenceRawBacklog =
					convergenceDecision.rawBacklogObserved;
				convergenceProbe.convergenceState = static_cast<uint8_t>(
					convergenceDecision.state);
				convergenceProbe.convergenceReason = static_cast<uint8_t>(
					convergenceDecision.reason);
				convergenceProbe.convergenceActivation = static_cast<uint8_t>(
					convergenceDecision.activation);
				m_liveOutputTrace.Record(convergenceProbe);
				m_liveConvergenceTrace.Record(convergenceProbe);
			}
			if (convergenceDecision.requestConvergence)
			{
				size_t actualRawDepthBefore = 0;
				size_t discardedRawFrames = 0;
				size_t rawDepthAfterConvergence = 0;
				size_t actualConvertedDepthBefore = 0;
				size_t discardedConvertedFrames = 0;
				size_t convertedDepthAfterConvergence = 0;
				// Publish steady mode before trimming so conversion cannot refill the
				// queue between this one-shot catch-up and persistent enforcement.
				m_steadyQueueEpoch.store(
					expectedQueueEpoch, std::memory_order_release);
				actualRawDepthBefore = m_captureFrameQueue.Size();
				// The initial raw bridge helped the converted prime reach downstream,
				// but at live cadence it can never drain by itself. Once downstream
				// pacing proves the prime was accepted, remove that stale bridge as
				// part of the same timestamp-aware convergence transaction.
				discardedRawFrames = m_captureFrameQueue.TrimTo(0);
				rawDepthAfterConvergence = m_captureFrameQueue.Size();
				m_publishedRawQueueDepth.store(
					rawDepthAfterConvergence, std::memory_order_release);
				{
					CAutoLock convertedLock(&m_convertedQueueLock);
					actualConvertedDepthBefore = m_processedFrameQueue.Size();
					discardedConvertedFrames =
						m_processedFrameQueue.TrimTo(desiredVpDepth);
					convertedDepthAfterConvergence =
						m_processedFrameQueue.Size();
					m_publishedConvertedQueueDepth.store(
						convertedDepthAfterConvergence, std::memory_order_release);
				}
				const size_t discardedStaleFrames =
					discardedRawFrames + discardedConvertedFrames;
				if (DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(m_timestamp) &&
					discardedConvertedFrames > 0)
				{
					// Legacy modes stamp samples during conversion. Arm a one-shot
					// delivery-boundary splice so removing old converted samples does
					// not leave their timestamp span scheduled in DirectShow.
					legacyTimestampCatchUp.Arm(expectedQueueEpoch);
					legacyConvertedCatchUpEpoch = expectedQueueEpoch;
					legacyConvertedCatchUpPending = true;
				}
				legacyIntentionalRawGapEpoch = expectedQueueEpoch;
				legacyIntentionalRawGapPending =
					DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(m_timestamp) &&
					discardedRawFrames > 0;
				if (timestampOwnershipEnabled && discardedStaleFrames > 0)
				{
					// The trim deliberately replaces stale live work. Retain its exact
					// source-counter span as trace/recovery accounting. If
					// the graph clock is running, align that one transactional rebase
					// to current graph time plus configured lead; this catches up a
					// slow HDMI handshake without treating pre-Run backlog as time.
					rationalSourceGapSlotsToSuppress =
						discardedStaleFrames >
							(std::numeric_limits<uint64_t>::max)() -
							rationalSourceGapSlotsToSuppress ?
						(std::numeric_limits<uint64_t>::max)() :
						rationalSourceGapSlotsToSuppress + discardedStaleFrames;
					REFERENCE_TIME rawCatchUpClock = REFERENCE_TIME_INVALID;
					const bool graphStayedRunning =
						graphRunningBeforeDelivery &&
						readConfirmedRunningStreamTime(rawCatchUpClock);
					if (rawCatchUpClock != REFERENCE_TIME_INVALID)
					{
						rationalCatchUpAnchorValid = true;
						rationalCatchUpMinimumStart =
							rawCatchUpClock + GetRampedLeadTime();
					}
					DebugLog::Log(
						"VP-0066 RATIONAL CATCH-UP: epoch=%llu discarded=%zu "
						"running=%d clock_valid=%d minimum_start=%.3fms",
						expectedQueueEpoch, discardedStaleFrames,
						graphStayedRunning ? 1 : 0,
						rationalCatchUpAnchorValid ? 1 : 0,
						rationalCatchUpMinimumStart / 10000.0);
				}
				// The pre-catch-up samples described the stale live backlog we just
				// removed. Rewarm presentation telemetry from the caught-up path so
				// the UI never advertises that transient as steady latency.
				m_latencyStabilizer.Reset();
				m_latencySnapshotAvailable.store(false, std::memory_order_release);
				LiveOutputTraceRecord convergenceTrace;
				convergenceTrace.kind = LiveOutputTraceKind::PlannedDrop;
				convergenceTrace.pipelineEpoch = expectedQueueEpoch;
				convergenceTrace.eventTick = GetTickCount64();
				convergenceTrace.presentationStart = presentationStart;
				convergenceTrace.presentationStop = presentationStop;
				if (timestampDecision.valid)
				{
					convergenceTrace.mediaStart = timestampDecision.mediaStart;
					convergenceTrace.mediaStop = timestampDecision.mediaStop;
					convergenceTrace.outputSequence = timestampDecision.outputSequence;
				}
				convergenceTrace.rawQueueDepth = static_cast<uint32_t>(
					rawDepthAfterConvergence);
				convergenceTrace.convertedQueueDepth = static_cast<uint32_t>(
					convertedDepthAfterConvergence);
				convergenceTrace.totalQueueDepth =
					convergenceTrace.rawQueueDepth +
					convergenceTrace.convertedQueueDepth;
				convergenceTrace.queueCapacity = static_cast<uint32_t>(
					m_frameQueueMaxSize.load(std::memory_order_acquire));
				convergenceTrace.queueTarget = static_cast<uint32_t>(desiredVpDepth);
				convergenceTrace.queueDepthBefore = static_cast<uint32_t>(
					actualConvertedDepthBefore);
				convergenceTrace.queueDepthAfter = static_cast<uint32_t>(
					convertedDepthAfterConvergence);
				convergenceTrace.queueDiscarded = static_cast<uint32_t>(
					discardedStaleFrames);
				convergenceTrace.rawQueueDiscarded = static_cast<uint32_t>(
					discardedRawFrames);
				convergenceTrace.convertedQueueDiscarded = static_cast<uint32_t>(
					discardedConvertedFrames);
				convergenceTrace.convergenceSuccessCount =
					convergenceDecision.successfulDeliveryCount;
				convergenceTrace.convergenceBlockCount =
					convergenceDecision.ingressBlockCount;
				convergenceTrace.convergenceRecoveryStreak =
					convergenceDecision.consecutiveRecoveryDeliveryCount;
				convergenceTrace.convergencePacedStreak =
					convergenceDecision.consecutivePacedDeliveryCount;
				convergenceTrace.convergenceBlockThresholdUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.ingressBlockThresholdUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergenceNormalThresholdUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.normalDeliveryThresholdUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergencePacedMinimumUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.pacedDeliveryMinimumUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergencePacedMaximumUs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.pacedDeliveryMaximumUs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergencePacedPrimingDepth =
					static_cast<uint32_t>(convergenceDecision.pacedPrimingDepth);
				convergenceTrace.convergenceElapsedMs =
					static_cast<uint32_t>(std::min<uint64_t>(
						convergenceDecision.elapsedSinceFirstSuccessMs,
						std::numeric_limits<uint32_t>::max()));
				convergenceTrace.convergenceRawZero =
					actualRawDepthBefore == 0;
				convergenceTrace.convergenceRawBacklog =
					actualRawDepthBefore > 0;
				convergenceTrace.convergenceState = static_cast<uint8_t>(
					convergenceDecision.state);
				convergenceTrace.convergenceReason = static_cast<uint8_t>(
					convergenceDecision.reason);
				convergenceTrace.convergenceActivation = static_cast<uint8_t>(
					convergenceDecision.activation);
				convergenceTrace.convergenceApplied = true;
				convergenceTrace.intentionalDrop = discardedStaleFrames > 0;
				convergenceTrace.timestampOwner = timestampOwnershipEnabled ? 1 : 0;
				convergenceTrace.timestampMethod = static_cast<uint8_t>(m_timestamp);
				m_liveOutputTrace.Record(convergenceTrace);
				m_liveConvergenceTrace.Record(convergenceTrace);
				m_convergenceAppliedTick.store(
					convergenceTrace.eventTick, std::memory_order_release);
				m_convergenceDeliverySuccessCount.store(
					m_currentEpochDeliverySuccessCount.load(
						std::memory_order_acquire),
					std::memory_order_release);
				m_convergenceTargetFrames.store(
					desiredVpDepth, std::memory_order_release);
				m_convergenceHardBlockRecovered.store(
					convergenceDecision.activation ==
						LiveEpochConvergenceActivation::HardBlockRecovery,
					std::memory_order_release);
				m_convergenceConvertedQueueWasFull.store(
					m_primePrefillReachedEpoch.load(std::memory_order_acquire) ==
						expectedQueueEpoch,
					std::memory_order_release);
				// Publish the epoch last so an acquiring observer cannot combine a
				// current proof epoch with partially published proof details.
				m_convergenceAppliedEpoch.store(
					expectedQueueEpoch, std::memory_order_release);
				DebugLog::Log(
					"VP-0066-9 QUEUE CONVERGENCE: epoch=%llu target=%zu "
					"raw=%zu->%zu discarded_raw=%zu "
					"converted=%zu->%zu discarded_converted=%zu "
					"discarded_stale=%zu steady_high_water=%zu activation=%s "
					"paced_streak=%u paced_band=%llu..%lluus priming_depth=%zu "
					"timestamp_method=%s legacy_timestamp_catch_up=%d latency_rewarm=1 "
					"madvr_queue=unobservable",
					expectedQueueEpoch, desiredVpDepth,
					actualRawDepthBefore, rawDepthAfterConvergence,
					discardedRawFrames,
					actualConvertedDepthBefore,
					convertedDepthAfterConvergence, discardedConvertedFrames,
					discardedStaleFrames,
					std::max<size_t>(1, desiredVpDepth),
					ToString(convergenceDecision.activation),
					convergenceDecision.consecutivePacedDeliveryCount,
					convergenceDecision.pacedDeliveryMinimumUs,
					convergenceDecision.pacedDeliveryMaximumUs,
					convergenceDecision.pacedPrimingDepth,
					TimestampMethodName(m_timestamp),
					DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(m_timestamp) &&
						discardedConvertedFrames > 0 ? 1 : 0);
			}
			++framesSinceLastLog;
			++deliverySuccessCount;
		}
		else if (outcome.deliveryRejected)
		{
			downstreamRejectedUntilNewEpoch = true;
			downstreamRejectedEpoch = expectedQueueEpoch;
			static DWORD lastRejectedDeliveryLogTime = 0;
			const DWORD rejectedNow = GetTickCount();
			if (lastRejectedDeliveryLogTime == 0 ||
				rejectedNow - lastRejectedDeliveryLogTime >= 5000)
			{
				DebugLog::Log(
					"DELIVERY THREAD: downstream rejected sample with S_FALSE; "
					"timeline not committed and drain paused");
				lastRejectedDeliveryLogTime = rejectedNow;
			}
		}
		return result;
	};

	const auto signalConvertedQueueIfNeeded = [&]()
	{
		bool hasQueuedSamples = false;
		{
			CAutoLock convertedLock(&m_convertedQueueLock);
			hasQueuedSamples = m_processedFrameQueue.Size() > 0;
		}
		if (hasQueuedSamples && m_hConvertedAvailableEvent)
			SetEvent(m_hConvertedAvailableEvent);
	};

	uint64_t lastQueueSnapshotTick = 0;
	const auto recordQueueSnapshot = [&]()
	{
		const uint64_t now = GetTickCount64();
		if (lastQueueSnapshotTick != 0 && now - lastQueueSnapshotTick < 1000)
			return;
		lastQueueSnapshotTick = now;

		// This uses the same atomically published raw/converted depths shown by
		// the DirectShow OSD. It is intentionally a VP-only queue snapshot;
		// downstream madVR queue occupancy remains unavailable.
		const uint32_t rawDepth = static_cast<uint32_t>(
			m_publishedRawQueueDepth.load(std::memory_order_acquire));
		const uint32_t convertedDepth = static_cast<uint32_t>(
			m_publishedConvertedQueueDepth.load(std::memory_order_acquire));
		const uint64_t currentEpoch =
			m_queueEpoch.load(std::memory_order_acquire);
		LiveOutputTraceRecord queueTrace =
			latestTimelineSnapshotAvailable &&
			latestTimelineSnapshot.pipelineEpoch == currentEpoch ?
			latestTimelineSnapshot : LiveOutputTraceRecord{};
		queueTrace.kind = LiveOutputTraceKind::QueueSnapshot;
		queueTrace.pipelineEpoch = currentEpoch;
		queueTrace.eventTick = now;
		queueTrace.rawQueueDepth = rawDepth;
		queueTrace.convertedQueueDepth = convertedDepth;
		queueTrace.totalQueueDepth = rawDepth + convertedDepth;
		queueTrace.queueCapacity = static_cast<uint32_t>(
			m_frameQueueMaxSize.load(std::memory_order_acquire));
		queueTrace.timestampMethod = static_cast<uint8_t>(m_timestamp);
		const EpochBoundedQueueMetrics rawMetrics = m_captureFrameQueue.Metrics();
		const EpochBoundedQueueMetrics convertedMetrics =
			m_processedFrameQueue.Metrics();
		const uint64_t rawDiscarded = rawMetrics.overflowDiscarded +
			rawMetrics.staleDiscarded;
		const uint64_t convertedDiscarded = convertedMetrics.overflowDiscarded +
			convertedMetrics.staleDiscarded;
		queueTrace.rawQueueDiscarded = static_cast<uint32_t>(std::min<uint64_t>(
			rawDiscarded, (std::numeric_limits<uint32_t>::max)()));
		queueTrace.convertedQueueDiscarded = static_cast<uint32_t>(
			std::min<uint64_t>(convertedDiscarded,
				(std::numeric_limits<uint32_t>::max)()));
		queueTrace.queueDiscarded = static_cast<uint32_t>(std::min<uint64_t>(
			rawDiscarded + convertedDiscarded,
			(std::numeric_limits<uint32_t>::max)()));
		m_liveOutputMetricsTrace.Record(queueTrace);
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
		const DWORD deliveryWaitMs =
			m_isBuffering.load(std::memory_order_acquire) ? 100 : INFINITE;
		DWORD waitResult = WaitForMultipleObjects(
			2, events, FALSE, deliveryWaitMs);

		if (waitResult == WAIT_OBJECT_0) // shutdown
		{
			DebugLog::Log("DELIVERY THREAD: Shutdown signal received");
			break;
		}

		if (waitResult != WAIT_OBJECT_0 + 1 && waitResult != WAIT_TIMEOUT)
		{
			DebugLog::Log("DELIVERY THREAD: WaitForMultipleObjects FAILED result=%lu", waitResult);
			break;
		}

		if (!m_isActive.load(std::memory_order_acquire))
		{
			DebugLog::Log("DELIVERY THREAD: Not active, exiting");
			break;
		}

		recordQueueSnapshot();

		// IMemInputPin::Receive S_FALSE rejects the sample and requires upstream
		// to stop sending until a flush completes. Reset advances the authoritative
		// queue epoch inside its flush transaction; deliverTracked's gate then
		// prevents a new-epoch sample from crossing before NewSegment completes.
		if (downstreamRejectedUntilNewEpoch)
		{
			const uint64_t currentEpoch =
				m_queueEpoch.load(std::memory_order_acquire);
			if (currentEpoch == downstreamRejectedEpoch)
				continue;
			downstreamRejectedUntilNewEpoch = false;
			downstreamRejectedEpoch = 0;
			DebugLog::Log(
				"DELIVERY THREAD: downstream rejection pause cleared by new epoch");
		}
		const uint64_t observedLatchEpoch =
			m_queueEpoch.load(std::memory_order_acquire);
		if (observedLatchEpoch != rationalLatchEpoch)
		{
			rationalLatchEpoch = observedLatchEpoch;
			rationalSourceGapSlotsToSuppress = 0;
			rationalCatchUpAnchorValid = false;
			rationalCatchUpMinimumStart = 0;
		}

		if (pendingUpstreamRepeat.sample)
		{
			const uint64_t currentQueueEpoch =
				m_queueEpoch.load(std::memory_order_acquire);
			const uint64_t currentTimingGeneration =
				m_sceneTimingGeneration.load(std::memory_order_acquire);
			const bool pendingStillValid =
				!m_isBuffering.load(std::memory_order_acquire) &&
				m_sceneAwareTimingCorrection.load(std::memory_order_acquire) &&
				m_sceneCorrectionUpstreamSample.load(std::memory_order_acquire) &&
				pendingUpstreamRepeat.queueEpoch == currentQueueEpoch &&
				pendingUpstreamRepeat.timingGeneration == currentTimingGeneration &&
				sceneCadence.active;

			if (!pendingStillValid)
			{
				resetSceneCadence();
				signalConvertedQueueIfNeeded();
				continue;
			}

			const HRESULT repeatHr = deliverTracked(
				pendingUpstreamRepeat.sample,
				pendingUpstreamRepeat.queueEpoch,
				pendingUpstreamRepeat.frameNumber,
				pendingUpstreamRepeat.captureTimestamp,
				pendingUpstreamRepeat.captureArrivalTick,
				pendingUpstreamRepeat.processingDurationUs,
				pendingUpstreamRepeat.atSceneBoundary,
				RationalLiveOutputCadence::Display,
				sceneCadence.displayRateHz,
				0,
				false);
			if (repeatHr == S_OK)
			{
				++sceneCadence.nextOutputIndex;
				sceneCadence.contentPhaseFrames =
					pendingUpstreamRepeat.phaseBefore - 1.0L;
				m_scenePhasePpmUnits.store(
					static_cast<int64_t>(llround(
						sceneCadence.contentPhaseFrames * 1000000.0L)),
					std::memory_order_release);
				m_lastSceneAwareCorrectionTime.store(
					GetTickCount(), std::memory_order_relaxed);
				m_sceneLastCorrectionAction.store(1, std::memory_order_release);
				m_sceneLastCorrectionSecondsFromDeadline.store(
					pendingUpstreamRepeat.secondsFromDeadline,
					std::memory_order_release);
				m_sceneLastCorrectionTick.store(
					GetTickCount64(), std::memory_order_release);
				if (pendingUpstreamRepeat.atSceneBoundary)
					m_lastCorrectedSceneEventId.store(
						pendingUpstreamRepeat.sceneEventId,
						std::memory_order_relaxed);
				m_sceneAwareCorrectionRepeatCount.fetch_add(
					1, std::memory_order_relaxed);
				DebugLog::Log(
					"SCENE-AWARE CORRECTION: deferred upstream sample at %s "
					"(event=%llu, phase=%+.6Lf -> %+.6Lf frames, media-offset=%+lld)",
					"scene boundary",
					pendingUpstreamRepeat.sceneEventId,
					pendingUpstreamRepeat.phaseBefore,
					sceneCadence.contentPhaseFrames,
					advancedMediaTimeOffset);
				pendingUpstreamRepeat.sample->Release();
				pendingUpstreamRepeat = {};
			}
			else
			{
				DebugLog::Log(
					"SCENE-AWARE CORRECTION: optional deferred repeat was not "
					"accepted (hr=0x%08x); correction abandoned without "
					"advancing the delivery timeline",
					repeatHr);
				pendingUpstreamRepeat.sample->Release();
				pendingUpstreamRepeat = {};
			}

			// At most one sample is submitted per wakeup in experimental mode.
			// An auto-reset event is not a counting semaphore: explicitly retain
			// the queue's readiness if conversion ran ahead while this duplicate
			// was pending.
			signalConvertedQueueIfNeeded();
			continue;
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
			const uint64_t candidateEpoch =
				m_queueEpoch.load(std::memory_order_acquire);
			size_t convertedQueueSize = 0;

			// DYNAMIC BUFFERING: Use GetBufferingTarget() for fps-aware buffering
			const size_t bufferingTarget = GetBufferingTarget();
			const size_t rawBridgeTarget =
				m_primeRawTargetFrames.load(std::memory_order_acquire);

			const size_t maxFrames = std::max(bufferingTarget,
				std::min(m_frameQueueMaxSize.load(std::memory_order_relaxed), bufferingTarget + std::max<size_t>(2, bufferingTarget / 2)));

			size_t q = 0;
			{
				CAutoLock lock(&m_convertedQueueLock);
				q = m_processedFrameQueue.Size();

				if (q > maxFrames)
				{
					const size_t toDrop = q - maxFrames;
					(void)m_processedFrameQueue.TrimTo(maxFrames);
					m_publishedConvertedQueueDepth.store(
						m_processedFrameQueue.Size(), std::memory_order_release);
					++bufferUnderrunCount; // or better: add a new bufferOverrunDropCount
					DebugLog::Log("DELIVERY THREAD: MAX BUFFER hit: dropped %zu old frames (q=%zu max=%zu)",
						toDrop, q, maxFrames);
				}

				convertedQueueSize = m_processedFrameQueue.Size();
			}

			const uint64_t candidatePrimeStartedTick =
				m_primeStartedTick.load(std::memory_order_acquire);
			const uint64_t candidateNow = GetTickCount64();
			const bool candidateTimedOut =
				candidatePrimeStartedTick != 0 &&
				candidateNow >= candidatePrimeStartedTick &&
				candidateNow - candidatePrimeStartedTick >=
					DirectShowEpochPrimePolicy::PrimeTimeoutMs;
			if (!DirectShowEpochPrimePolicy::CanReleaseBuffering(
				candidateEpoch,
				m_queueEpoch.load(std::memory_order_acquire),
				m_isActive.load(std::memory_order_acquire),
				m_stopping.load(std::memory_order_acquire),
				m_deliveryFlushing.load(std::memory_order_acquire),
				m_resetInProgress.load(std::memory_order_acquire),
				convertedQueueSize, bufferingTarget,
				m_captureFrameQueue.CurrentDepth({ candidateEpoch }),
				rawBridgeTarget,
				candidateTimedOut))
			{
				continue; // Keep waiting for more samples
			}

			// The conversion worker assigns timestamps before placing samples in
			// m_processedFrameQueue.  The preroll samples above are therefore
			// already part of this DirectShow segment.  Do not restart the timing
			// origin here: doing so makes the first sample converted after preroll
			// reuse the timestamp range of the queued samples (for example,
			// 180-263 ms followed again by 180 ms at 59.94 Hz).  That overlap is
			// neither a new DirectShow segment nor a valid live catch-up; madVR is
			// free to interpret it as late/repeated content.
			//
			// A real timing restart remains owned by Reset(), which purges the
			// queues and delivers a new segment before any new samples are made.
			uint64_t completedEpoch = 0;
			uint64_t primeEpoch = 0;
			uint64_t fillMs = 0;
			size_t committedTarget = bufferingTarget;
			size_t committedRawTarget = rawBridgeTarget;
			size_t committedRawDepth = 0;
			bool primePrefillReached = false;
			bool primeTimedOut = false;
			{
				// Reset uses the same gate before changing epoch, queues, and the
				// buffering flag. Revalidate every part of the candidate while
				// holding it so old-epoch depth can never release a new epoch.
				CAutoLock deliveryLock(&m_deliveryGate);
				if (!m_isBuffering.load(std::memory_order_acquire))
				{
					continue;
				}

				const size_t confirmedTarget = GetBufferingTarget();
				committedTarget = confirmedTarget;
				committedRawTarget =
					m_primeRawTargetFrames.load(std::memory_order_acquire);
				{
					CAutoLock convertedLock(&m_convertedQueueLock);
					convertedQueueSize = m_processedFrameQueue.Size();
				}
				completedEpoch =
					m_queueEpoch.load(std::memory_order_acquire);
				committedRawDepth =
					m_captureFrameQueue.CurrentDepth({ completedEpoch });
				primeEpoch =
					m_primeQueueEpoch.load(std::memory_order_acquire);
				const uint64_t primeStartedTick =
					m_primeStartedTick.load(std::memory_order_acquire);
				const uint64_t commitNow = GetTickCount64();
				primeTimedOut = primeStartedTick != 0 &&
					commitNow >= primeStartedTick &&
					commitNow - primeStartedTick >=
						DirectShowEpochPrimePolicy::PrimeTimeoutMs;
				if (!DirectShowEpochPrimePolicy::CanReleaseBuffering(
					candidateEpoch,
					completedEpoch,
					m_isActive.load(std::memory_order_acquire),
					m_stopping.load(std::memory_order_acquire),
					m_deliveryFlushing.load(std::memory_order_acquire),
					m_resetInProgress.load(std::memory_order_acquire),
					convertedQueueSize, confirmedTarget,
					committedRawDepth, committedRawTarget, primeTimedOut))
				{
					continue;
				}

				primePrefillReached = completedEpoch != 0 &&
					completedEpoch == primeEpoch &&
					convertedQueueSize >= confirmedTarget &&
					committedRawDepth >= committedRawTarget;
				if (primePrefillReached)
					m_primePrefillReachedEpoch.store(
						completedEpoch, std::memory_order_release);
				fillMs = primeStartedTick != 0 && commitNow >= primeStartedTick ?
					commitNow - primeStartedTick : 0;
				m_isBuffering.store(false, std::memory_order_release);
			}

			DebugLog::Log("DELIVERY THREAD: BUFFERING COMPLETE converted=%zu/%zu raw=%zu/%zu - delivery starting with continuous pre-stamped timeline prime_epoch=%llu prime_prefill=%d prime_timeout=%d fill_ms=%llu",
				convertedQueueSize, committedTarget,
				committedRawDepth, committedRawTarget,
				static_cast<unsigned long long>(primeEpoch),
				primePrefillReached ? 1 : 0,
				primeTimedOut ? 1 : 0,
				static_cast<unsigned long long>(fillMs));
		}

		// Keep one converted sample as a stable handoff cushion.  Capture callbacks
		// drive further conversion; do not make every delivery pop immediately
		// refill the queue. This is the established origin/main live-fill policy.
		for (;;)
		{
			if (!m_isActive.load(std::memory_order_acquire) ||
				m_stopping.load(std::memory_order_acquire) ||
				m_isBuffering.load(std::memory_order_acquire))
				break;

			// Sustained live delivery remains in this drain loop rather than the
			// outer event wait. Sample here so the trace tracks the same steady
			// R/C/T/capacity state the OSD reports once per second.
			recordQueueSnapshot();

			// Pop one sample under lock
			IMediaSample* pSample = nullptr;
			ProcessedFrame convertedSample;
			{
				CAutoLock convLock(&m_convertedQueueLock);
				const PipelineEpoch currentEpoch{
					m_queueEpoch.load(std::memory_order_acquire) };
				if (!m_processedFrameQueue.TryPopCurrentIfDepthAbove(
					currentEpoch, GetDeliveryReserve(), convertedSample))
					break;  // No more samples, wait for more

				pSample = convertedSample.sample;
				m_publishedConvertedQueueDepth.store(
					m_processedFrameQueue.Size(),
					std::memory_order_release);
			}
			// Delivery made room below the steady target. Wake conversion if a
			// fresh raw frame was retained while madVR was consuming a sample.
			if (m_captureFrameQueue.Size() > 0 && m_hFrameAvailableEvent)
				SetEvent(m_hFrameAvailableEvent);
			bool isSafeCorrectionPoint = convertedSample.isSafeCorrectionPoint;
			uint64_t sceneEventId = convertedSample.sceneEventId;
			const uint64_t convertedQueueEpoch = convertedSample.queueEpoch;
			const uint64_t convertedSceneTimingGeneration =
				convertedSample.sceneTimingGeneration;

			if (!pSample)
				break;

			m_dequeueCount.fetch_add(1, std::memory_order_relaxed);
			m_lastDequeueTick.store(
				GetTickCount64(), std::memory_order_release);

			if (m_deliverNewSegment.exchange(false, std::memory_order_acq_rel))
			{
				DebugLog::Log(
					"DELIVERY THREAD: timing requested a new segment; performing serialized reset");
				pSample->Release();
				RequestCoordinatedReset("buffered-timing-new-segment");
				break;
			}

			const uint64_t currentQueueEpoch =
				m_queueEpoch.load(std::memory_order_acquire);
			const uint64_t currentSceneTimingGeneration =
				m_sceneTimingGeneration.load(std::memory_order_acquire);
			// Scene Detect samples the converted P010 luma plane.  Keep the user
			// selection intact for a later P010 graph, but never alter cadence or
			// timestamps while the active graph delivers another subtype.
			const bool sceneEnabled =
				m_sceneAwareTimingCorrection.load(std::memory_order_acquire) &&
				IsEqualGUID(m_mediaType.subtype, MEDIASUBTYPE_P010);
			const bool sceneTimingReady =
				m_sceneTimingReady.load(std::memory_order_acquire);

			// A reset can purge the queues after this sample was converted but
			// before delivery popped it. Never send an old-segment sample or reuse
			// retained pixels across that boundary.
			if (convertedQueueEpoch != currentQueueEpoch)
			{
				resetSceneCadence();
				pSample->Release();
				break;
			}

			if (!sceneEnabled)
			{
				if (sceneCadence.active)
					resetSceneCadence();
			}
			else if (!sceneTimingReady)
			{
				if (sceneCadence.active)
					resetSceneCadence();
			}
			else if (sceneCadence.active &&
				sceneCadence.queueEpoch != currentQueueEpoch)
			{
				resetSceneCadence();
			}
			else if (sceneCadence.active &&
				sceneCadence.generation != currentSceneTimingGeneration)
			{
				// Drain/discard queued decisions from the previous timing
				// generation. The first current-generation sample continues at
				// the exact next display-cadence timestamp, so no legacy/source
				// timestamp is spliced into this DirectShow segment.
				if (convertedSceneTimingGeneration != currentSceneTimingGeneration)
				{
					pSample->Release();
					continue;
				}

				const REFERENCE_TIME continuousAnchor =
					sceneOutputTime(sceneCadence.nextOutputIndex);
				sceneCadence.generation = currentSceneTimingGeneration;
				sceneCadence.anchor =
					static_cast<long double>(continuousAnchor);
				sceneCadence.nextOutputIndex = 0;
				sceneCadence.contentPhaseFrames = 0.0L;
				const double currentDisplayRate =
					m_sceneDisplayRefreshRateHz.load(std::memory_order_acquire);
				if (currentDisplayRate >= 10.0 && currentDisplayRate <= 240.0)
					sceneCadence.displayRateHz = currentDisplayRate;
				m_scenePhasePpmUnits.store(0, std::memory_order_release);
				DebugLog::Log(
					"SCENE-AWARE CADENCE: timing generation %llu continued without timestamp-domain splice",
					currentSceneTimingGeneration);
			}

			if (!sceneCadence.active &&
				convertedSceneTimingGeneration != currentSceneTimingGeneration)
			{
				isSafeCorrectionPoint = false;
				sceneEventId = 0;
			}
			else if (sceneCadence.active &&
				convertedSceneTimingGeneration != currentSceneTimingGeneration)
			{
				pSample->Release();
				continue;
			}

			// Conversion can run ahead and its first discontinuous sample can be
			// purged during buffering. The delivery component applies this flag
			// and the optional late-bound stop as one sample preparation step.
			const bool lateBindStop =
				m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
				m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2;
			static const double SEARCH_TOLERANCE_PERCENT = 0.10;
			const REFERENCE_TIME searchTolerance =
				static_cast<REFERENCE_TIME>(m_frameDuration * SEARCH_TOLERANCE_PERCENT);
			const bool epochStartDiscontinuity =
				lastSuccessfullyDeliveredEpoch != currentQueueEpoch;
			const bool sourceGapDiscontinuity =
				convertedSample.sourceDiscontinuity;
			if (legacyConvertedCatchUpPending &&
				legacyConvertedCatchUpEpoch != currentQueueEpoch)
			{
				legacyConvertedCatchUpPending = false;
				legacyConvertedCatchUpEpoch = 0;
			}
			if (legacyIntentionalRawGapPending &&
				legacyIntentionalRawGapEpoch != currentQueueEpoch)
			{
				legacyIntentionalRawGapPending = false;
				legacyIntentionalRawGapEpoch = 0;
			}
			const bool deliveredSourceFrameGap =
				lastSuccessfullyDeliveredEpoch == currentQueueEpoch &&
				convertedSample.frameNumber > lastSuccessfullyDeliveredFrameNumber &&
				convertedSample.frameNumber - lastSuccessfullyDeliveredFrameNumber > 1;
			if (legacyIntentionalRawGapPending &&
				!legacyConvertedCatchUpPending && deliveredSourceFrameGap &&
				legacyIntentionalRawGapEpoch == currentQueueEpoch)
			{
				// The retained converted reserve is delivered before the source
				// counter reaches the raw frames removed by convergence. Re-arm at
				// that exact intentional boundary so the hardware-clock timeline
				// remains continuous without hiding unrelated capture loss.
				legacyTimestampCatchUp.Arm(currentQueueEpoch);
				legacyIntentionalRawGapPending = false;
				legacyIntentionalRawGapEpoch = 0;
				DebugLog::Log(
					"VP-0066 LEGACY RAW CATCH-UP: method=%s epoch=%llu frame=%llu",
					TimestampMethodName(m_timestamp), currentQueueEpoch,
					convertedSample.frameNumber);
			}
			const bool markDiscontinuity =
				epochStartDiscontinuity || sourceGapDiscontinuity;
			const DirectShowSamplePreparationResult preparation =
				m_directShowFrameDeliverer.Prepare({
					pSample, markDiscontinuity,
					lateBindStop, m_frameDuration, searchTolerance,
					[](IMediaSample* preparedSample, BOOL discontinuity)
					{
						return preparedSample->SetDiscontinuity(discontinuity);
					},
					[](IMediaSample* preparedSample, REFERENCE_TIME* start, REFERENCE_TIME* stop)
					{
						return preparedSample->GetTime(start, stop);
					},
					[](IMediaSample* preparedSample, REFERENCE_TIME* start, REFERENCE_TIME* stop)
					{
						return preparedSample->SetTime(start, stop);
					},
					[this](REFERENCE_TIME currentStart, REFERENCE_TIME theoreticalStop, REFERENCE_TIME tolerance)
					{
						return FindNextPendingTimestamp(currentStart, theoreticalStop, tolerance);
					} });
			if (markDiscontinuity)
			{
				DebugLog::Log(
					"DirectShow sample discontinuity: epoch=%llu frame=%llu "
					"origin=%s epoch_start=%d source_gap=%d",
					static_cast<unsigned long long>(currentQueueEpoch),
					static_cast<unsigned long long>(convertedSample.frameNumber),
					epochStartDiscontinuity && sourceGapDiscontinuity ?
						"epoch-start+source-gap" :
						(epochStartDiscontinuity ? "epoch-start" : "source-gap"),
					epochStartDiscontinuity ? 1 : 0,
					sourceGapDiscontinuity ? 1 : 0);
			}
			if (FAILED(preparation.discontinuityResult))
				DebugLog::Log(
					"DELIVERY THREAD: failed to normalize sample discontinuity (hr=0x%08x)",
					preparation.discontinuityResult);

			const bool usedLateBoundStop = preparation.lateBoundStopApplied;
			REFERENCE_TIME currentStart = 0;
			REFERENCE_TIME currentStop = 0;
			// Read back the prepared value: Clock-Smart late binding may have
			// replaced the original stop before the catch-up splice is applied.
			HRESULT sampleTimeHr = pSample->GetTime(&currentStart, &currentStop);
			if (DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(m_timestamp) &&
				SUCCEEDED(sampleTimeHr) && currentStop > currentStart)
			{
				const DirectShowLiveCatchUpDecision catchUp =
					legacyTimestampCatchUp.Adjust(
						currentQueueEpoch, currentStart, currentStop);
				REFERENCE_TIME adjustedStart = catchUp.start;
				REFERENCE_TIME adjustedStop = catchUp.stop;
				if (catchUp.adjusted &&
					FAILED(pSample->SetTime(&adjustedStart, &adjustedStop)))
				{
					DebugLog::Log(
						"VP-0066 LEGACY TIMESTAMP CATCH-UP: failed to stamp "
						"method=%s epoch=%llu; requesting reset",
						TimestampMethodName(m_timestamp), currentQueueEpoch);
					pSample->Release();
					RequestCoordinatedReset("legacy-catch-up-stamp-failure");
					break;
				}
				currentStart = catchUp.start;
				currentStop = catchUp.stop;
				if (catchUp.rebased)
				{
					legacyConvertedCatchUpPending = false;
					legacyConvertedCatchUpEpoch = 0;
					DebugLog::Log(
						"VP-0066 LEGACY TIMESTAMP CATCH-UP: method=%s epoch=%llu "
						"frame=%llu offset=%.3fms next_start=%.3fms",
						TimestampMethodName(m_timestamp), currentQueueEpoch,
						convertedSample.frameNumber,
						catchUp.offset / 10000.0,
						catchUp.start / 10000.0);
				}
			}
			if (lateBindStop)
			{
				if (usedLateBoundStop)
				{
					static uint64_t lateBindSuccessCount = 0;
					static uint64_t lateBindTotalCount = 0;
					static uint64_t lastLateBindLogCount = 0;
					++lateBindSuccessCount;
					++lateBindTotalCount;
					if (lateBindTotalCount - lastLateBindLogCount >= 600)
					{
						const double successRate = (lateBindSuccessCount * 100.0) / lateBindTotalCount;
						const REFERENCE_TIME actualDelta = abs(
							preparation.matchedNextStart - preparation.theoreticalStop);
						DebugLog::Log("LATE-BIND STATS: %llu/%llu (%.1f%%) success, last delta=%.3fms",
							lateBindSuccessCount, lateBindTotalCount, successRate, actualDelta / 10000.0);
						lastLateBindLogCount = lateBindTotalCount;
					}
				}
				else
				{
					++lateBindMissesSinceLastLog;
					const DWORD now = GetTickCount();
					if (lastLateBindMissLogTime == 0 || now - lastLateBindMissLogTime >= 5000)
					{
						DebugLog::Log("LATE-BIND MISS: %llu miss(es) in the last interval; target=%.3fms within ±%.3fms (searching pending history)",
							lateBindMissesSinceLastLog, preparation.theoreticalStop / 10000.0,
							searchTolerance / 10000.0);
						lateBindMissesSinceLastLog = 0;
						lastLateBindMissLogTime = now;
					}
				}
			}

			// Scene Detect selects a coherent display-rate slot plan. The unified
			// delivery sequencer applies the one final DirectShow timestamp. This makes
			// each synthetic repeat/drop a real change in the number of samples
			// madVR receives instead of squeezing two samples into one source
			// interval. Content remains tied to capture/media time; its signed
			// phase is paid back only at scene boundaries and can remain overdue
			// until the next eligible boundary.
			bool sceneCadenceForSample = false;
			SceneCorrectionAction sceneCorrectionAction = SceneCorrectionAction::None;
			bool correctionAtSceneBoundary = false;
			bool sceneCorrectionCommitted = false;
			bool contentPhasePending = false;
			double secondsFromCorrectionDeadline = 0.0;
			// This is the nominal one-frame correction deadline.  It deliberately
			// does not authorize a correction by itself: Scene Mode may only alter
			// cadence while processing a detected scene boundary.
			constexpr long double kCorrectionDeadlineFrames = 0.98L;
			long double pendingContentPhaseFrames =
				sceneCadence.contentPhaseFrames;
			const double displayRateHz =
				m_sceneDisplayRefreshRateHz.load(std::memory_order_acquire);
			const double deliveryRateHz =
				m_sceneDeliveryRateHz.load(std::memory_order_acquire);
			sampleTimeHr = pSample->GetTime(&currentStart, &currentStop);
			const bool validSceneTiming =
				sceneEnabled &&
				sceneTimingReady &&
				convertedSceneTimingGeneration == currentSceneTimingGeneration &&
				SUCCEEDED(sampleTimeHr) &&
				currentStop > currentStart &&
				displayRateHz >= 10.0 && displayRateHz <= 240.0 &&
				deliveryRateHz >= 10.0 && deliveryRateHz <= 240.0;

			if (validSceneTiming)
			{
				if (!sceneCadence.active)
				{
					sceneCadence.active = true;
					sceneCadence.generation = currentSceneTimingGeneration;
					sceneCadence.queueEpoch = currentQueueEpoch;
					sceneCadence.displayRateHz = displayRateHz;
					sceneCadence.anchor = static_cast<long double>(currentStart);
					sceneCadence.nextOutputIndex = 0;
					const uint64_t warmupIntervals =
						m_sceneWarmupIntervals.load(std::memory_order_acquire);
					const long double warmupPhaseFrames =
						static_cast<long double>(warmupIntervals) *
						((static_cast<long double>(displayRateHz) /
							static_cast<long double>(deliveryRateHz)) - 1.0L);
					// Only the fractional phase is meaningful when correction starts.
					// Retaining whole frames accumulated during warm-up creates a
					// backlog which would require several scene-boundary corrections
					// to pay back.
					sceneCadence.contentPhaseFrames =
						std::fmod(warmupPhaseFrames, 1.0L);
					DebugLog::Log(
						"SCENE-AWARE CADENCE: started at %.6f Hz "
						"(delivery %.6f Hz, warmup=%llu, phase=%+.6Lf/%+.6Lf, epoch=%llu)",
						displayRateHz, deliveryRateHz, warmupIntervals,
						warmupPhaseFrames, sceneCadence.contentPhaseFrames,
						currentSceneTimingGeneration);
				}
				else
				{
					const double rateDeltaPpm =
						abs(displayRateHz - sceneCadence.displayRateHz) /
						sceneCadence.displayRateHz * 1000000.0;
					if (rateDeltaPpm >= 0.5)
					{
						// Preserve the next timestamp exactly while adopting the
						// refined long-window display-rate measurement.
						sceneCadence.anchor = static_cast<long double>(
							sceneOutputTime(sceneCadence.nextOutputIndex));
						sceneCadence.nextOutputIndex = 0;
						sceneCadence.displayRateHz = displayRateHz;
						DebugLog::Log(
							"SCENE-AWARE CADENCE: refined display rate to %.6f Hz (delta %.2f ppm)",
							displayRateHz, rateDeltaPpm);
					}
				}

				sceneCadenceForSample = true;
				pendingContentPhaseFrames =
					sceneCadence.contentPhaseFrames +
					(static_cast<long double>(displayRateHz) / deliveryRateHz) - 1.0L;
				contentPhasePending = true;
				m_scenePhasePpmUnits.store(
					static_cast<int64_t>(llround(
						pendingContentPhaseFrames * 1000000.0L)),
					std::memory_order_release);

				const DWORD correctionNow = GetTickCount();
				const bool correctionIntervalElapsed =
					(correctionNow -
						m_lastSceneAwareCorrectionTime.load(std::memory_order_relaxed)) >= 250;
				const bool newSceneEvent =
					isSafeCorrectionPoint &&
					sceneEventId != 0 &&
					sceneEventId !=
						m_lastCorrectedSceneEventId.load(std::memory_order_relaxed);

				// Seek a scene boundary late in the correction interval.  A fixed
				// 0.75-frame trigger used to make a 45-minute correction eligible
				// roughly ten minutes early.  Scene cuts may hide a correction up to
				// ten seconds early; for short cadences retain a proportional 25%
				// window so we still respond aggressively.
				const long double signedPhaseRate =
					static_cast<long double>(displayRateHz - deliveryRateHz);
				const long double phaseRateMagnitude = std::fabs(signedPhaseRate);
				bool correctionWindowOpen = false;
				if (phaseRateMagnitude > 0.000000001L)
				{
					const long double targetPhase = signedPhaseRate > 0.0L ?
						kCorrectionDeadlineFrames : -kCorrectionDeadlineFrames;
					const long double secondsUntilCorrection =
						(targetPhase - pendingContentPhaseFrames) / signedPhaseRate;
					secondsFromCorrectionDeadline =
						static_cast<double>(secondsUntilCorrection);
					const long double nominalIntervalSeconds =
						kCorrectionDeadlineFrames / phaseRateMagnitude;
					const long double correctionWindowSeconds = std::min(
						10.0L, nominalIntervalSeconds * 0.25L);
					correctionWindowOpen =
						secondsUntilCorrection <= correctionWindowSeconds;
					m_sceneSecondsUntilCorrection.store(
						static_cast<double>(secondsUntilCorrection),
						std::memory_order_release);
					m_sceneSecondsUntilPlan.store(
						static_cast<double>(std::max(
							0.0L,
							secondsUntilCorrection - correctionWindowSeconds)),
						std::memory_order_release);
					m_scenePredictedAction.store(
						signedPhaseRate > 0.0L ? 1 : -1,
						std::memory_order_release);
					m_sceneCorrectionPlanned.store(
						correctionWindowOpen, std::memory_order_release);
				}
				else
				{
					m_sceneSecondsUntilCorrection.store(0.0, std::memory_order_release);
					m_sceneSecondsUntilPlan.store(0.0, std::memory_order_release);
					m_scenePredictedAction.store(0, std::memory_order_release);
					m_sceneCorrectionPlanned.store(false, std::memory_order_release);
				}

				if (newSceneEvent && correctionIntervalElapsed && correctionWindowOpen)
				{
					sceneCorrectionAction = signedPhaseRate > 0.0L ?
						SceneCorrectionAction::Repeat : SceneCorrectionAction::Drop;
					correctionAtSceneBoundary = true;
				}

				if (sceneCorrectionAction == SceneCorrectionAction::Drop)
				{
					const long double phaseBefore =
						pendingContentPhaseFrames;
					if (m_sceneCorrectionUpstreamSample.load(
							std::memory_order_acquire))
					{
						// The source frame is intentionally omitted while the
						// presentation cadence remains contiguous. Keep subsequent
						// Advanced media times aligned with that output sequence.
						--advancedMediaTimeOffset;
					}
					sceneCadence.contentPhaseFrames =
						pendingContentPhaseFrames + 1.0L;
					sceneCorrectionCommitted = true;
					m_scenePhasePpmUnits.store(
						static_cast<int64_t>(llround(
							sceneCadence.contentPhaseFrames * 1000000.0L)),
						std::memory_order_release);
					m_lastSceneAwareCorrectionTime.store(
						correctionNow, std::memory_order_relaxed);
					m_sceneLastCorrectionAction.store(-1, std::memory_order_release);
					m_sceneLastCorrectionSecondsFromDeadline.store(
						secondsFromCorrectionDeadline, std::memory_order_release);
					m_sceneLastCorrectionTick.store(
						GetTickCount64(), std::memory_order_release);
					if (correctionAtSceneBoundary)
						m_lastCorrectedSceneEventId.store(
							sceneEventId, std::memory_order_relaxed);
					m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
					m_sceneAwareCorrectionDropCount.fetch_add(
						1, std::memory_order_relaxed);
					// This omission is the cadence correction itself. Adopt the next
					// source identity without turning it into another PTS hole.
					if (rationalSourceGapSlotsToSuppress !=
						(std::numeric_limits<uint64_t>::max)())
						++rationalSourceGapSlotsToSuppress;
					LiveOutputTraceRecord sceneDropTrace;
					sceneDropTrace.kind = LiveOutputTraceKind::PlannedDrop;
					sceneDropTrace.frameNumber = convertedSample.frameNumber;
					sceneDropTrace.pipelineEpoch = currentQueueEpoch;
					sceneDropTrace.captureTimestamp =
						convertedSample.captureTimestamp;
					sceneDropTrace.captureArrivalTick =
						convertedSample.captureArrivalTick;
					sceneDropTrace.eventTick = GetTickCount64();
					sceneDropTrace.rawQueueDepth = static_cast<uint32_t>(
						m_publishedRawQueueDepth.load(std::memory_order_acquire));
					sceneDropTrace.convertedQueueDepth = static_cast<uint32_t>(
						m_publishedConvertedQueueDepth.load(std::memory_order_acquire));
					sceneDropTrace.totalQueueDepth =
						sceneDropTrace.rawQueueDepth +
						sceneDropTrace.convertedQueueDepth;
					sceneDropTrace.timestampMethod = static_cast<uint8_t>(m_timestamp);
					sceneDropTrace.sceneBoundary = correctionAtSceneBoundary;
					sceneDropTrace.intentionalDrop = true;
					sceneDropTrace.sourceGapSuppressed = true;
					m_liveOutputTrace.Record(sceneDropTrace);
					DebugLog::Log(
						"SCENE-AWARE CORRECTION: output drop at %s "
						"(event=%llu, phase=%+.6Lf -> %+.6Lf frames, media-offset=%+lld)",
						"scene boundary",
						sceneEventId, phaseBefore,
						sceneCadence.contentPhaseFrames,
						advancedMediaTimeOffset);
					pSample->Release();
					continue;
				}
			}
			else if (sceneEnabled && sceneCadence.active)
			{
				DebugLog::Log(
					"SCENE-AWARE CADENCE: active timestamp became invalid; resetting segment");
				pSample->Release();
				RequestCoordinatedReset("cadence-invalid-timestamp");
				break;
			}

			HRESULT hr = S_OK;
			bool cadenceTimestampNeedsReset = false;
			bool scheduledPresentationGapRepeat = false;
			bool advancedRepeatCommitted = false;
			LONGLONG advancedMediaTimeOffsetForCurrent = advancedMediaTimeOffset;
			IMediaSample* deferredUpstreamRepeat = nullptr;
			if (sceneCadenceForSample)
			{
				// A repeat is represented by one empty presentation slot. The
				// renderer naturally holds the previous frame for that interval.
				// This avoids allocating/copying a 4K sample and avoids pushing
				// two Receive calls back-to-back into madVR.
				const bool repeatRequested =
					sceneCorrectionAction == SceneCorrectionAction::Repeat;
				const bool useUpstreamSample =
					repeatRequested &&
					m_sceneCorrectionUpstreamSample.load(
						std::memory_order_acquire);
				if (useUpstreamSample)
				{
					const REFERENCE_TIME repeatStart =
						sceneOutputTime(sceneCadence.nextOutputIndex + 1);
					const REFERENCE_TIME repeatStop =
						sceneOutputTime(sceneCadence.nextOutputIndex + 2);
					const HRESULT cloneHr = CloneSampleForUpstreamRepeat(
						pSample, repeatStart, repeatStop,
						&deferredUpstreamRepeat);
					if (FAILED(cloneHr) || !deferredUpstreamRepeat)
					{
						scheduledPresentationGapRepeat = true;
						DebugLog::Log(
							"SCENE-AWARE CORRECTION: upstream clone unavailable "
							"(hr=0x%08x); falling back to renderer gap",
								cloneHr);
					}
					else
					{
						// The duplicate occupies the current media-time slot. The
						// captured sample that follows it must occupy the next one.
						// Apply the current Advanced offset to the clone now; the
						// current sample is shifted after the duplicate is accepted.
						if (!applyAdvancedMediaTimeOffset(
								deferredUpstreamRepeat,
								advancedMediaTimeOffset))
						{
							DebugLog::Log(
								"SCENE-AWARE MEDIA TIME: failed to shift deferred "
								"upstream repeat by %+lld frame(s)",
								advancedMediaTimeOffset);
						}
						advancedRepeatCommitted = true;
						advancedMediaTimeOffsetForCurrent =
							advancedMediaTimeOffset + 1;
					}
				}
				else
				{
					scheduledPresentationGapRepeat = repeatRequested;
				}
				const uint64_t slotOffset =
					scheduledPresentationGapRepeat ? 1ULL : 0ULL;
				REFERENCE_TIME outputStart =
					sceneOutputTime(sceneCadence.nextOutputIndex + slotOffset);
				REFERENCE_TIME outputStop =
					sceneOutputTime(sceneCadence.nextOutputIndex + slotOffset + 1);
				if (m_timestamp !=
						DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL &&
					FAILED(pSample->SetTime(&outputStart, &outputStop)))
					cadenceTimestampNeedsReset = true;

				const bool advancedMediaTimeMode =
					m_sceneCorrectionUpstreamSample.load(
						std::memory_order_acquire);
				const LONGLONG mediaTimeOffsetForSample =
					advancedRepeatCommitted ?
						advancedMediaTimeOffsetForCurrent : advancedMediaTimeOffset;
				if (advancedMediaTimeMode &&
					m_timestamp !=
						DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL &&
					!applyAdvancedMediaTimeOffset(
						pSample, mediaTimeOffsetForSample))
				{
					DebugLog::Log(
						"SCENE-AWARE MEDIA TIME: failed to shift captured "
						"sample by %+lld frame(s)",
						mediaTimeOffsetForSample);
				}
			}

			if (cadenceTimestampNeedsReset)
			{
				if (deferredUpstreamRepeat)
					deferredUpstreamRepeat->Release();
				DebugLog::Log(
					"SCENE-AWARE CADENCE: SetTime failed; resetting segment");
				pSample->Release();
				RequestCoordinatedReset("cadence-set-time-failure");
				break;
			}

			// 4) DELIVER - Let madVR handle buffering and presentation.
			hr = deliverTracked(
				pSample,
				currentQueueEpoch,
				convertedSample.frameNumber,
				convertedSample.captureTimestamp,
				convertedSample.captureArrivalTick,
				convertedSample.processingDurationUs,
				correctionAtSceneBoundary,
				sceneCadenceForSample ? RationalLiveOutputCadence::Display :
					RationalLiveOutputCadence::Rational,
				sceneCadenceForSample ? sceneCadence.displayRateHz : 0.0,
				scheduledPresentationGapRepeat ? 1U : 0U,
				convertedSample.sourceDiscontinuity);

			if (hr == S_OK && sceneCadenceForSample &&
				deferredUpstreamRepeat)
			{
				// The current sample consumed its normal slot. Submit the cloned
				// repeat on the next capture wakeup rather than bursting two
				// Receive calls into madVR in this delivery iteration.
				++sceneCadence.nextOutputIndex;
				pendingUpstreamRepeat.sample = deferredUpstreamRepeat;
				pendingUpstreamRepeat.queueEpoch = currentQueueEpoch;
				pendingUpstreamRepeat.timingGeneration =
					currentSceneTimingGeneration;
				pendingUpstreamRepeat.frameNumber = convertedSample.frameNumber;
				pendingUpstreamRepeat.captureTimestamp =
					convertedSample.captureTimestamp;
				pendingUpstreamRepeat.captureArrivalTick =
					convertedSample.captureArrivalTick;
				pendingUpstreamRepeat.processingDurationUs =
					convertedSample.processingDurationUs;
				pendingUpstreamRepeat.sceneEventId = sceneEventId;
				pendingUpstreamRepeat.phaseBefore =
					pendingContentPhaseFrames;
				pendingUpstreamRepeat.secondsFromDeadline =
					secondsFromCorrectionDeadline;
				pendingUpstreamRepeat.atSceneBoundary =
					correctionAtSceneBoundary;
				deferredUpstreamRepeat = nullptr;
				advancedMediaTimeOffset = advancedMediaTimeOffsetForCurrent;

				// Do not reset this auto-reset event: doing so can discard the
				// only readiness notification for an already-populated converted
				// queue and let madVR drain its entire downstream pipeline.
				if (m_hConvertedAvailableEvent)
					SetEvent(m_hConvertedAvailableEvent);
			}
			else if (hr == S_OK && sceneCadenceForSample &&
				scheduledPresentationGapRepeat)
			{
				// One sample was delivered, but two presentation slots were
				// consumed: the deliberate hold/repeat gap and this sample.
				sceneCadence.nextOutputIndex += 2;
				const long double phaseBefore = pendingContentPhaseFrames;
				sceneCadence.contentPhaseFrames =
					pendingContentPhaseFrames - 1.0L;
				sceneCorrectionCommitted = true;
				m_scenePhasePpmUnits.store(
					static_cast<int64_t>(llround(
						sceneCadence.contentPhaseFrames * 1000000.0L)),
					std::memory_order_release);
				m_lastSceneAwareCorrectionTime.store(
					GetTickCount(), std::memory_order_relaxed);
				m_sceneLastCorrectionAction.store(1, std::memory_order_release);
				m_sceneLastCorrectionSecondsFromDeadline.store(
					secondsFromCorrectionDeadline, std::memory_order_release);
				m_sceneLastCorrectionTick.store(
					GetTickCount64(), std::memory_order_release);
				if (correctionAtSceneBoundary)
					m_lastCorrectedSceneEventId.store(
						sceneEventId, std::memory_order_relaxed);
				m_sceneAwareCorrectionRepeatCount.fetch_add(
					1, std::memory_order_relaxed);
				DebugLog::Log(
					"SCENE-AWARE CORRECTION: presentation-gap repeat at %s "
					"(phase=%+.6Lf -> %+.6Lf frames)",
					"scene boundary",
					pendingContentPhaseFrames,
					sceneCadence.contentPhaseFrames);
			}
			else if (hr == S_OK && sceneCadenceForSample &&
				contentPhasePending && !sceneCorrectionCommitted)
			{
				++sceneCadence.nextOutputIndex;
				sceneCadence.contentPhaseFrames = pendingContentPhaseFrames;
				m_scenePhasePpmUnits.store(
					static_cast<int64_t>(llround(
						sceneCadence.contentPhaseFrames * 1000000.0L)),
					std::memory_order_release);
			}
			else if (FAILED(hr) && sceneCadenceForSample &&
				!sceneCorrectionCommitted)
			{
				m_scenePhasePpmUnits.store(
					static_cast<int64_t>(llround(
						sceneCadence.contentPhaseFrames * 1000000.0L)),
					std::memory_order_release);
			}
			if (deferredUpstreamRepeat)
				deferredUpstreamRepeat->Release();

			if (hr == S_OK)
			{
				lastSuccessfullyDeliveredEpoch = currentQueueEpoch;
				lastSuccessfullyDeliveredFrameNumber =
					convertedSample.frameNumber;
			}

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

			pSample->Release();
			if (hr != S_OK)
				break;
			if (pendingUpstreamRepeat.sample)
				break;
		}
	}

	DebugLog::Log("DELIVERY THREAD: Exiting");
	if (pendingUpstreamRepeat.sample)
		pendingUpstreamRepeat.sample->Release();
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
	SceneDetector sceneDetector;
	uint64_t sceneDetectorGeneration = m_sceneDetectorGeneration.load(std::memory_order_acquire);
	// FrameProcessor survives a DirectShow graph reset, but its conversion
	// worker does not. Reset the worker-owned model unconditionally here so a
	// replacement worker cannot race the generation increment in Reset() and
	// inherit an already-stable model after the published rectangle was cleared.
	// With identical paused frames that stale model would otherwise emit no new
	// publication, leaving NLS in WAITING until the picture geometry changed.
	m_frameProcessor.ResetActivePicture();
	uint64_t activePictureDetectorGeneration =
		m_activePictureDetectorGeneration.load(std::memory_order_acquire);

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
				sceneDetector.Reset(currentSceneDetectorGeneration);
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
				currentConvertedSize = m_processedFrameQueue.Size();
			}
			const uint64_t currentActivePictureGeneration =
				m_activePictureDetectorGeneration.load(std::memory_order_acquire);
			if (currentActivePictureGeneration != activePictureDetectorGeneration)
			{
				m_frameProcessor.ResetActivePicture();
				activePictureDetectorGeneration = currentActivePictureGeneration;
			}

			const size_t queueMaxSize = DirectShowEpochPrimePolicy::PrimeTarget(
				m_frameQueueMaxSize.load(std::memory_order_relaxed),
				static_cast<size_t>(std::max<LONG>(
					0, GetNegotiatedAllocatorBufferCount())));
			const PipelineEpoch currentEpoch{
				m_queueEpoch.load(std::memory_order_acquire) };
			const LiveSteadyQueueDecision steadyQueueDecision =
				LiveSteadyQueuePolicy::Evaluate({
					m_steadyQueueEpoch.load(std::memory_order_acquire),
					currentEpoch.value,
					IsSteadyQueueTargetConfigured(),
					m_sceneAwareTimingCorrection.load(std::memory_order_acquire),
					GetConfiguredSteadyQueueTarget(),
					currentConvertedSize });
			const size_t conversionHighWater = steadyQueueDecision.active ?
				steadyQueueDecision.highWater : queueMaxSize;
			if (currentConvertedSize >= conversionHighWater)
			{
				++backpressureHits;
				break;  // Preserve cadence: wait for delivery instead of converting then discarding.
			}

			// Preserve origin/main's gentle back-pressure above half capacity, but
			// never sleep while holding a queue lock.
			if (currentConvertedSize >= (queueMaxSize * 3) / 4)
				Sleep(2);
			else if (currentConvertedSize >= queueMaxSize / 2)
				Sleep(1);

			// Pop one raw frame.
			VideoFrame videoFrame{};
			bool hasFrame = false;
			size_t rawQueueSize = 0;
			uint64_t frameQueueEpoch = 0;

			if (!m_isActive.load(std::memory_order_acquire))
			{
				DebugLog::Log("CONVERSION WORKER: Not active during raw frame check, returning");
				return 0;
			}
			hasFrame = m_captureFrameQueue.TryPopCurrent(currentEpoch, videoFrame);
			if (hasFrame)
			{
				frameQueueEpoch = currentEpoch.value;
				// Publish the replacement ownership before publishing the smaller
				// raw depth, so a concurrent snapshot cannot undercount the one
				// DeckLink-backed frame held by conversion.
				m_sourceBufferConversionCaptureArrivalTick.store(
					videoFrame.GetCaptureArrivalTick(), std::memory_order_release);
				m_sourceBufferConversionInFlight.store(
					true, std::memory_order_release);
			}
			const EpochBoundedQueueMetrics rawMetrics = m_captureFrameQueue.Metrics();
			rawQueueSize = rawMetrics.depth;
			m_publishedRawQueueDepth.store(rawQueueSize, std::memory_order_release);

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
				m_sourceBufferConversionInFlight.store(false, std::memory_order_release);
				m_sourceBufferConversionCaptureArrivalTick.store(0, std::memory_order_release);
				m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
				continue;
			}

			FrameProcessorResult processing = m_frameProcessor.Process({
				&videoFrame,
				pSample,
				{ frameQueueEpoch },
				videoFrame.GetCounter(),
				static_cast<uint64_t>(videoFrame.GetTimingTimestamp()),
				videoFrame.GetCaptureArrivalTick(),
				0 });
			hr = processing.result;
			const uint64_t convTimeUs = processing.processingDurationUs;

			m_totalConversionTimeUs.fetch_add(convTimeUs, std::memory_order_relaxed);
			++m_conversionFrameCount;
			m_lastConversionTick.store(
				GetTickCount64(), std::memory_order_release);
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
				m_sourceBufferConversionInFlight.store(false, std::memory_order_release);
				m_sourceBufferConversionCaptureArrivalTick.store(0, std::memory_order_release);
				pSample->Release();
				m_droppedFrameCount.fetch_add(1, std::memory_order_relaxed);
				continue;
			}

			// Release raw frame - we're done with it
			videoFrame.SourceBufferRelease();
			m_sourceBufferConversionInFlight.store(false, std::memory_order_release);
			m_sourceBufferConversionCaptureArrivalTick.store(0, std::memory_order_release);

			// A reset/recovery may have purged the queues while this expensive
			// conversion was in flight. Never publish that old sample into the
			// new queue epoch.
			if (frameQueueEpoch != m_queueEpoch.load(std::memory_order_acquire))
			{
				pSample->Release();
				continue;
			}

			bool isSafeCorrectionPoint = false;
			uint64_t sceneEventId = 0;
			uint8_t sceneEventFramesBack = 0;
			uint16_t sceneAverageLuma = 0;
			const bool sceneDetectionEnabled =
				m_sceneAwareTimingCorrection.load(std::memory_order_acquire);
			const bool subtitleRepositioningEnabled =
				m_subtitleRepositionMode.load(std::memory_order_acquire) !=
					SubtitleRepositionMode::DISABLED;
			if ((sceneDetectionEnabled || subtitleRepositioningEnabled) &&
				IsEqualGUID(m_mediaType.subtype, MEDIASUBTYPE_P010))
				isSafeCorrectionPoint = AnalyzeSceneDetector(
					pSample, sceneDetector, videoFrame.GetCounter(),
					videoFrame.GetTimingTimestamp(), currentSceneDetectorGeneration,
					sceneEventId, sceneEventFramesBack, sceneAverageLuma);

			// NLS/aspect-rule gating uses the same converted P010 image that reaches
			// madVR. Sparse sampling every few frames is negligible beside conversion.
			if (IsEqualGUID(m_mediaType.subtype, MEDIASUBTYPE_P010))
				UpdateActivePictureAspectRatio(pSample, videoFrame.GetCounter());

			// Analyze the unmodified frame first. Subtitle relocation changes a
			// small image region and must not become input to the cut detector.
			// Latch one neutral dark-gray panel level for the initial scene and
			// each confirmed scene change so the background never pumps.
			if (sceneDetectionEnabled && sceneAverageLuma > 0 &&
				(isSafeCorrectionPoint ||
					!m_subtitlePanelLumaInitialized.load(
						std::memory_order_acquire)))
			{
				m_subtitleSceneAverageLumaCode.store(
					sceneAverageLuma, std::memory_order_release);
				m_subtitlePanelLumaInitialized.store(
					true, std::memory_order_release);
				DebugLog::Log(
					"SUBTITLE BACKGROUND: latched scene average=%u event=%llu",
					sceneAverageLuma, sceneEventId);
			}
			if (subtitleRepositioningEnabled && isSafeCorrectionPoint &&
				sceneEventId != 0)
			{
				m_subtitleSceneEventId.store(
					sceneEventId, std::memory_order_release);
				// Preserve an active caption's dimensions across a shot cut.
				// Its burned-in text can legitimately span the transition.
				if (!m_subtitleTrackActive.load(
					std::memory_order_acquire))
				{
					m_subtitlePanelHalfWidthPixels.store(
						0, std::memory_order_release);
					m_subtitlePanelHeightPixels.store(
						0, std::memory_order_release);
					m_subtitleFastSignature.store(
						0, std::memory_order_release);
				}
			}
			// Scene analysis may run solely to size subtitle panels. Do not tag
			// samples for timing correction when Scene Detect itself is off.
			if (!sceneDetectionEnabled)
			{
				isSafeCorrectionPoint = false;
				sceneEventId = 0;
				sceneEventFramesBack = 0;
			}

			// Relocate only after scene measurement and before queueing.
			if (subtitleRepositioningEnabled)
				RelocateSubtitleInP010(pSample, videoFrame.GetCounter());
			const uint64_t sceneTimingGeneration =
				m_sceneTimingGeneration.load(std::memory_order_acquire);

			// Add converted sample to queue only after its timestamp history is
			// published. The delivery thread is then free to drain immediately.
			size_t convertedQueueDepth = 0;
				size_t steadyQueueDepthBefore = 0;
			bool sampleQueued = false;
			{
				CAutoLock convLock(&m_convertedQueueLock);
				if (frameQueueEpoch == m_queueEpoch.load(std::memory_order_acquire))
				{
					// Publish SMART timestamp history inside the same epoch check
					// as the queue insertion. This prevents a reset from rejecting
					// the sample while leaving its stale timestamp behind.
					if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
						m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2)
					{
						REFERENCE_TIME sampleStart = 0, sampleStop = 0;
						if (SUCCEEDED(pSample->GetTime(&sampleStart, &sampleStop)))
							RecordPendingTimestamp(sampleStart);
					}

					// Hard cuts are confirmed from one of the following frames.
					// Conversion normally runs ahead of delivery, so move the event
					// tag back to the actual first post-cut sample while it is still
					// buffered. If delivery already consumed it, retain the current
					// confirmation frame as a safe fallback.
					bool sceneEventMovedToBufferedFrame = false;
					if (isSafeCorrectionPoint && sceneEventFramesBack > 0)
					{
						(void)m_processedFrameQueue.TryMutateCurrentFromBack(
							{ frameQueueEpoch }, sceneEventFramesBack,
							[&](ProcessedFrame& cutSample)
							{
								if (cutSample.queueEpoch == frameQueueEpoch &&
									cutSample.sceneTimingGeneration == sceneTimingGeneration)
								{
									cutSample.isSafeCorrectionPoint = true;
									cutSample.sceneEventId = sceneEventId;
									sceneEventMovedToBufferedFrame = true;
								}
							});
						if (sceneEventMovedToBufferedFrame)
						{
							isSafeCorrectionPoint = false;
							sceneEventId = 0;
						}
					}

					const PipelineEpoch currentEpoch{
						m_queueEpoch.load(std::memory_order_acquire) };
					ProcessedFrame processedFrame = processing.frame;
					// VideoFrame is the epoch-owned source-gap authority. Never
					// infer current-frame semantics from an allocator sample flag:
					// IMediaSample instances are recycled and may retain flags from
					// an earlier use. Epoch-start discontinuity is applied by the
					// final delivery owner.
					processedFrame.sourceDiscontinuity =
						videoFrame.IsSourceDiscontinuity();
					processedFrame.isSafeCorrectionPoint = isSafeCorrectionPoint;
					processedFrame.sceneEventId = sceneEventId;
					processedFrame.sceneTimingGeneration = sceneTimingGeneration;
					steadyQueueDepthBefore = m_processedFrameQueue.Size();
					const EpochBoundedQueuePushResult pushResult =
						m_processedFrameQueue.Push(std::move(processedFrame),
							{ frameQueueEpoch }, currentEpoch);
					const EpochBoundedQueueMetrics processedMetrics =
						m_processedFrameQueue.Metrics();
					m_publishedConvertedQueueDepth.store(
						processedMetrics.depth, std::memory_order_release);
					convertedQueueDepth = processedMetrics.depth;
					sampleQueued =
						pushResult == EpochBoundedQueuePushResult::Accepted ||
						pushResult == EpochBoundedQueuePushResult::AcceptedAfterOverflowDiscard;
					pSample = nullptr; // Queue accepted or released the sample reference.
				}
			}

			if (pSample)
			{
				pSample->Release();
				continue;
			}

			if (sampleQueued)
			{
				LiveOutputTraceRecord conversionTrace;
				conversionTrace.kind = LiveOutputTraceKind::ConversionCompleted;
				conversionTrace.frameNumber = videoFrame.GetCounter();
				conversionTrace.pipelineEpoch = frameQueueEpoch;
				conversionTrace.captureTimestamp =
					static_cast<uint64_t>(videoFrame.GetTimingTimestamp());
				conversionTrace.captureArrivalTick =
					videoFrame.GetCaptureArrivalTick();
				conversionTrace.eventTick = GetTickCount64();
				conversionTrace.rawQueueDepth = static_cast<uint32_t>(
					m_publishedRawQueueDepth.load(std::memory_order_acquire));
				conversionTrace.convertedQueueDepth = static_cast<uint32_t>(
					convertedQueueDepth);
				conversionTrace.queueTarget = static_cast<uint32_t>(
					GetConfiguredSteadyQueueTarget());
				conversionTrace.queueDepthBefore = static_cast<uint32_t>(
					steadyQueueDepthBefore);
				conversionTrace.queueDepthAfter = static_cast<uint32_t>(
					convertedQueueDepth);
				conversionTrace.queueDiscarded = 0;
				conversionTrace.convertedQueueDiscarded = 0;
				conversionTrace.intentionalDrop = false;
				conversionTrace.processingDurationUs = static_cast<uint32_t>(
					std::min<uint64_t>(convTimeUs, std::numeric_limits<uint32_t>::max()));
				conversionTrace.sceneBoundary = isSafeCorrectionPoint;
				conversionTrace.sourceDiscontinuity =
					videoFrame.IsSourceDiscontinuity();
				m_liveOutputTrace.Record(conversionTrace);
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
			rawQueueSize = m_captureFrameQueue.Size();
			{
				CAutoLock convLock(&m_convertedQueueLock);
				convertedQueueSize = m_processedFrameQueue.Size();
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


HRESULT CBufferedLiveSourceVideoOutputPin::CloneSampleForUpstreamRepeat(
	IMediaSample* source,
	REFERENCE_TIME start,
	REFERENCE_TIME stop,
	IMediaSample** repeatSample)
{
	if (!source || !repeatSample)
		return E_POINTER;
	*repeatSample = nullptr;

	IMediaSample* duplicate = nullptr;
	// This optional correction must never block conversion or delivery waiting
	// for a large allocator buffer. The normal allocator has correction
	// headroom; if it is unavailable the caller falls back to RENDERER_GAP.
	HRESULT hr = GetDeliveryBuffer(&duplicate, nullptr, nullptr, AM_GBF_NOWAIT);
	if (FAILED(hr) || !duplicate)
		return FAILED(hr) ? hr : E_FAIL;

	BYTE* sourceData = nullptr;
	BYTE* duplicateData = nullptr;
	const long sourceLength = source->GetActualDataLength();
	if (sourceLength < 0 ||
		FAILED(source->GetPointer(&sourceData)) || !sourceData ||
		FAILED(duplicate->GetPointer(&duplicateData)) || !duplicateData ||
		sourceLength > duplicate->GetSize())
	{
		duplicate->Release();
		return E_FAIL;
	}

	if (sourceLength > 0)
		memcpy(duplicateData, sourceData, static_cast<size_t>(sourceLength));
	hr = duplicate->SetActualDataLength(sourceLength);
	if (SUCCEEDED(hr))
		hr = duplicate->SetTime(&start, &stop);
	if (SUCCEEDED(hr))
		hr = duplicate->SetSyncPoint(TRUE);
	if (SUCCEEDED(hr))
		hr = duplicate->SetPreroll(FALSE);
	if (SUCCEEDED(hr))
		hr = duplicate->SetDiscontinuity(FALSE);
	if (FAILED(hr))
	{
		duplicate->Release();
		return hr;
	}

	// This is a second presentation of the same captured frame. Start by copying
	// the source media-time identity; Advanced mode then applies its delivery
	// thread correction offset so the duplicate and following samples remain
	// monotonic in the output stream.
	LONGLONG mediaStart = 0;
	LONGLONG mediaStop = 0;
	if (SUCCEEDED(source->GetMediaTime(&mediaStart, &mediaStop)))
	{
		hr = duplicate->SetMediaTime(&mediaStart, &mediaStop);
		if (FAILED(hr))
		{
			duplicate->Release();
			return hr;
		}
	}

	// Preserve HDR metadata on the rare cloned sample. Side data is optional,
	// so an unsupported interface is not a correction failure.
	IMediaSideData* sourceSideData = nullptr;
	IMediaSideData* duplicateSideData = nullptr;
	if (SUCCEEDED(source->QueryInterface(
			__uuidof(IMediaSideData),
			reinterpret_cast<void**>(&sourceSideData))) &&
		SUCCEEDED(duplicate->QueryInterface(
			__uuidof(IMediaSideData),
			reinterpret_cast<void**>(&duplicateSideData))))
	{
		const GUID sideDataTypes[] =
		{
			IID_MediaSideDataHDRContentLightLevel,
			IID_MediaSideDataHDR
		};
		for (const GUID& type : sideDataTypes)
		{
			const BYTE* data = nullptr;
			size_t size = 0;
			if (SUCCEEDED(sourceSideData->GetSideData(
					type, &data, &size)) &&
				data && size > 0)
			{
				duplicateSideData->SetSideData(type, data, size);
			}
		}
	}
	if (duplicateSideData)
		duplicateSideData->Release();
	if (sourceSideData)
		sourceSideData->Release();

	*repeatSample = duplicate;
	return S_OK;
}


bool CBufferedLiveSourceVideoOutputPin::GetActivePictureAspectRatio(
	double& aspectRatio) const
{
	aspectRatio = m_activePictureAspectRatio.load(std::memory_order_acquire);
	return m_activePictureAspectStable.load(std::memory_order_acquire) &&
		aspectRatio > 0.0;
}


bool CBufferedLiveSourceVideoOutputPin::GetActivePictureRectangle(
	ActivePictureRectangle& rectangle) const
{
	std::lock_guard<std::mutex> lock(m_activePictureRectangleMutex);
	rectangle = m_activePictureRectangle;
	return rectangle.stable;
}


void CBufferedLiveSourceVideoOutputPin::UpdateActivePictureAspectRatio(
	IMediaSample* sample, uint64_t frameNumber)
{
	if (!sample)
		return;

	LONG width = 0;
	LONG signedHeight = 0;
	if (m_mediaType.pbFormat && IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo2) &&
		m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER2))
	{
		const auto* info = reinterpret_cast<const VIDEOINFOHEADER2*>(m_mediaType.pbFormat);
		width = info->bmiHeader.biWidth;
		signedHeight = info->bmiHeader.biHeight;
	}
	else if (m_mediaType.pbFormat && IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo) &&
		m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER))
	{
		const auto* info = reinterpret_cast<const VIDEOINFOHEADER*>(m_mediaType.pbFormat);
		width = info->bmiHeader.biWidth;
		signedHeight = info->bmiHeader.biHeight;
	}
	if (width <= 0 || signedHeight == 0)
		return;

	BYTE* bytes = nullptr;
	(void)sample->GetPointer(&bytes);
	const int height = signedHeight > 0 ? signedHeight : -signedHeight;
	const size_t pitch = static_cast<size_t>(width) * sizeof(uint16_t);
	double framesPerSecond = 60.0;
	if (m_timeScale > 0 && m_frameDurationTicks > 0)
		framesPerSecond = static_cast<double>(m_timeScale) / m_frameDurationTicks;
	const ActivePictureAnalyzerResult analysis = m_frameProcessor.AnalyzeActivePicture({
		{ bytes, static_cast<size_t>(std::max<LONG>(0, sample->GetActualDataLength())),
			static_cast<int>(width), height, pitch, pitch }, frameNumber, framesPerSecond });
	if (!analysis.analyzed)
		return;

	const ActivePictureTransitionDecision& decision = analysis.decision;
	const P010ActivePictureEvidence& evidence = analysis.evidence;
	if (decision.diagnostic)
	{
		if (!evidence.available)
			DebugLog::Log("ACTIVE PICTURE: state=unavailable frame=%llu candidate_matches=%u contradictions=%u reversals=%u confidence=%.2f reason=\"%s; %s\"",
				static_cast<unsigned long long>(frameNumber), decision.matchingCandidates,
				decision.contradictoryCandidates, decision.candidateReversals,
				decision.confidence, decision.reason.c_str(), evidence.reason.c_str());
		else
			DebugLog::Log("ACTIVE PICTURE: state=%s frame=%llu candidate=%d,%d-%d,%d stable=%d,%d-%d,%d stable_aspect=%.4f raster=%dx%d aspect=%.4f symmetric=%d matches=%u contradictions=%u reversals=%u confidence=%.2f classification=%d samples=%zu/%zu edge_trust=%d,%d,%d,%d edge_black=%.2f,%.2f,%.2f,%.2f edge_chroma=%.2f,%.2f,%.2f,%.2f edge_boundary=%.1f,%.1f,%.1f,%.1f edge_confidence=%.2f,%.2f,%.2f,%.2f first_contradiction=%llu latency_frames=%llu reason=\"%s\"",
				decision.state == ActivePictureTransitionState::STABLE ? "stable" : "candidate_transition",
				static_cast<unsigned long long>(frameNumber), decision.bounds.left, decision.bounds.top,
				decision.bounds.right, decision.bounds.bottom, decision.stableBounds.left,
				decision.stableBounds.top, decision.stableBounds.right, decision.stableBounds.bottom,
				decision.stableBounds.aspectRatio, decision.bounds.rasterWidth,
				decision.bounds.rasterHeight, decision.bounds.aspectRatio,
				decision.bounds.symmetricBars ? 1 : 0, decision.matchingCandidates,
				decision.contradictoryCandidates, decision.candidateReversals,
				decision.confidence, static_cast<int>(evidence.classification),
				evidence.lumaSamples, evidence.chromaSamples,
				evidence.left.trusted, evidence.top.trusted, evidence.right.trusted, evidence.bottom.trusted,
				evidence.left.blackFraction, evidence.top.blackFraction, evidence.right.blackFraction, evidence.bottom.blackFraction,
				evidence.left.neutralChromaFraction, evidence.top.neutralChromaFraction, evidence.right.neutralChromaFraction, evidence.bottom.neutralChromaFraction,
				evidence.left.innerBoundaryContrast, evidence.top.innerBoundaryContrast, evidence.right.innerBoundaryContrast, evidence.bottom.innerBoundaryContrast,
				evidence.left.confidence, evidence.top.confidence, evidence.right.confidence, evidence.bottom.confidence,
				static_cast<unsigned long long>(decision.firstContradictoryFrame),
				static_cast<unsigned long long>(decision.decisionLatencyFrames),
				(decision.reason + "; " + evidence.reason).c_str());
	}
	PublishActivePictureTransition(decision);
}


void CBufferedLiveSourceVideoOutputPin::PublishActivePictureTransition(
	const ActivePictureTransitionDecision& decision)
{
	if (!decision.publish)
		return;

	const uint64_t generation =
		m_activePictureRectangleGeneration.fetch_add(
			1, std::memory_order_acq_rel) + 1;
	if (!decision.stable)
	{
		m_activePictureAspectStable.store(false, std::memory_order_release);
		{
			std::lock_guard<std::mutex> lock(m_activePictureRectangleMutex);
			m_activePictureRectangle = {
				decision.bounds.left, decision.bounds.top,
				decision.bounds.right, decision.bounds.bottom,
				decision.bounds.rasterWidth, decision.bounds.rasterHeight,
				decision.bounds.aspectRatio, generation, false
			};
		}
		DebugLog::Log(
			"ACTIVE PICTURE: publication generation=%llu state=transitioning "
			"confidence=%.2f reason=\"%s\"",
			static_cast<unsigned long long>(generation),
			decision.confidence, decision.reason.c_str());
		return;
	}

	m_activePictureAspectRatio.store(
		decision.bounds.aspectRatio, std::memory_order_release);
	m_activePictureAspectStable.store(true, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(m_activePictureRectangleMutex);
		m_activePictureRectangle = {
			decision.bounds.left, decision.bounds.top,
			decision.bounds.right, decision.bounds.bottom,
			decision.bounds.rasterWidth, decision.bounds.rasterHeight,
			decision.bounds.aspectRatio, generation, true
		};
	}
	DebugLog::Log(
		"ACTIVE PICTURE: publication generation=%llu state=stable "
		"aspect=%.4f bounds=%d,%d-%d,%d raster=%dx%d "
		"confidence=%.2f latency_frames=%llu reason=\"%s\"",
		static_cast<unsigned long long>(generation),
		decision.bounds.aspectRatio,
		decision.bounds.left, decision.bounds.top,
		decision.bounds.right, decision.bounds.bottom,
		decision.bounds.rasterWidth, decision.bounds.rasterHeight,
		decision.confidence,
		static_cast<unsigned long long>(decision.decisionLatencyFrames),
		decision.reason.c_str());
}


bool CBufferedLiveSourceVideoOutputPin::AnalyzeSceneDetector(
	IMediaSample* sample,
	SceneDetector& detector,
	uint64_t sourceSequence,
	timingclocktime_t timestamp,
	uint64_t generation,
	uint64_t& sceneEventId,
	uint8_t& eventFramesBack,
	uint16_t& averageLuma)
{
	sceneEventId = 0;
	eventFramesBack = 0;
	averageLuma = 0;
	if (!sample || !IsEqualGUID(m_mediaType.subtype, MEDIASUBTYPE_P010) || !m_mediaType.pbFormat)
		return false;

	LONG width = 0;
	LONG height = 0;
	if (IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo2) &&
		m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER2))
	{
		const auto* videoInfo = reinterpret_cast<const VIDEOINFOHEADER2*>(m_mediaType.pbFormat);
		width = videoInfo->bmiHeader.biWidth;
		height = videoInfo->bmiHeader.biHeight;
	}
	else if (IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo) &&
		m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER))
	{
		const auto* videoInfo = reinterpret_cast<const VIDEOINFOHEADER*>(m_mediaType.pbFormat);
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

	const SceneAnalysisResult result = m_frameProcessor.AnalyzeScene({
		reinterpret_cast<const uint16_t*>(data), lumaWidth, lumaHeight,
		lumaWidth * sizeof(uint16_t), sourceSequence, timestamp, generation,
		static_cast<uint64_t>(std::max<REFERENCE_TIME>(1, m_frameDuration)), &detector });
	if (!result.validInput)
		return false;
	averageLuma = result.averageLuma;
	eventFramesBack = result.eventFramesBack;
	if (!result.safeBoundary)
		return false;
	// Preserve DirectShow's process-wide event sequencing and published count.
	sceneEventId = m_sceneEventSequence.fetch_add(1, std::memory_order_relaxed) + 1;
	m_sceneAwareDetectedCount.fetch_add(1, std::memory_order_relaxed);
	return true;
}


void CBufferedLiveSourceVideoOutputPin::StartSubtitleAnalysisWorker()
{
	std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
	if (m_subtitleAnalysisThread.joinable())
		return;

	m_subtitleWorkerStop = false;
	m_subtitleJobPending = false;
	m_subtitleGpuDetector.reset();
	m_subtitleAnalysisThread =
		std::thread(&CBufferedLiveSourceVideoOutputPin::SubtitleAnalysisWorker, this);
}


void CBufferedLiveSourceVideoOutputPin::StopSubtitleAnalysisWorker()
{
	std::shared_ptr<GpuSubtitleDetector> gpuDetector;
	{
		std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
		m_subtitleWorkerStop = true;
		m_subtitleJobPending = false;
		gpuDetector = m_subtitleGpuDetector;
	}
	if (gpuDetector)
		gpuDetector->Cancel();
	m_subtitleAnalysisCondition.notify_all();

	if (m_subtitleAnalysisThread.joinable())
	{
		DebugLog::Log("SUBTITLE ANALYZER: stopping worker");
		m_subtitleAnalysisThread.join();
		DebugLog::Log("SUBTITLE ANALYZER: worker stopped");
	}

	std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
	m_subtitleWorkerStop = false;
	m_subtitlePendingFrame = {};
	m_subtitleLatestResult = {};
	m_subtitleRecycledLuma.clear();
	m_subtitleGpuDetector.reset();
}


void CBufferedLiveSourceVideoOutputPin::ResetSubtitleAnalysis()
{
	m_subtitleAnalysisGeneration.fetch_add(1, std::memory_order_acq_rel);
	m_subtitleLastSubmitTick.store(0, std::memory_order_release);
	m_subtitleTrackActive.store(false, std::memory_order_release);
	m_subtitleSceneEventId.store(0, std::memory_order_release);
	m_subtitlePanelHalfWidthPixels.store(0, std::memory_order_release);
	m_subtitlePanelHeightPixels.store(0, std::memory_order_release);
	m_subtitlePictureTopPixels.store(0, std::memory_order_release);
	m_subtitlePictureBottomPixels.store(0, std::memory_order_release);
	m_subtitleFastSignature.store(0, std::memory_order_release);
	m_subtitleSignatureGeneration.fetch_add(1, std::memory_order_acq_rel);

	std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
	if (!m_subtitlePendingFrame.luma.empty() && m_subtitleRecycledLuma.empty())
		m_subtitleRecycledLuma.swap(m_subtitlePendingFrame.luma);
	m_subtitlePendingFrame = {};
	m_subtitleLatestResult = {};
	m_subtitleJobPending = false;
}


void CBufferedLiveSourceVideoOutputPin::SubmitSubtitleAnalysis(
	const uint16_t* yPlane,
	int width,
	int height,
	uint64_t frameNumber,
	uint16_t blackCode)
{
	if (!yPlane || width < 320 || height < 240)
		return;

	const uint64_t now = GetTickCount64();
	const uint64_t last = m_subtitleLastSubmitTick.load(std::memory_order_relaxed);
	// Acquire at roughly one video-frame interval, then reduce the cadence once
	// a caption is tracked. The latest-frame-only worker queue prevents OCR
	// latency from accumulating while the faster acquisition cadence removes
	// the former visible delay at caption onset.
	const bool advancedMode =
		m_subtitleRepositionMode.load(std::memory_order_acquire) ==
			SubtitleRepositionMode::ADVANCED;
	// Advanced is a geometry-only DirectML pass and is deliberately sampled at
	// video-frame cadence. BASIC retains its lower cadence because Windows OCR
	// is a CPU fallback and the latest-frame queue prevents latency buildup.
	const bool trackActive =
		m_subtitleTrackActive.load(std::memory_order_acquire);
	bool subtitleSignatureChanged = false;
	if (trackActive)
	{
		const int pictureBottom =
			m_subtitlePictureBottomPixels.load(std::memory_order_acquire);
		const int barDepth = pictureBottom > 0 && pictureBottom < height ?
			std::min(height - pictureBottom, std::max(8, height / 10)) : 0;
		if (barDepth >= 8)
		{
			uint64_t signature = 0;
			const int bandLeft = width / 5;
			const int bandWidth = width * 3 / 5;
			for (int bin = 0; bin < 32; ++bin)
			{
				int bright = 0;
				const int x0 = bandLeft + bandWidth * bin / 32;
				const int x1 = bandLeft + bandWidth * (bin + 1) / 32;
				for (int sy = 0; sy < 6; ++sy)
				{
					const int y = std::min(height - 1,
						pictureBottom + (barDepth - 1) * (sy + 1) / 7);
					for (int sx = 0; sx < 4; ++sx)
					{
						const int x = std::min(width - 1,
							x0 + std::max(1, x1 - x0 - 1) * (sx + 1) / 5);
						const uint16_t code = static_cast<uint16_t>(
							yPlane[static_cast<size_t>(y) * width + x] >> 6);
						bright += code > blackCode + 32;
					}
				}
				const uint64_t level =
					bright >= 6 ? 3u : (bright >= 3 ? 2u :
						(bright >= 1 ? 1u : 0u));
				signature |= level << (bin * 2);
			}
			const uint64_t previous =
				m_subtitleFastSignature.exchange(
					signature, std::memory_order_acq_rel);
			uint64_t changedGroups =
				(signature ^ previous);
			changedGroups =
				(changedGroups | (changedGroups >> 1)) &
				0x5555555555555555ULL;
			int changedBins = 0;
			while (changedGroups)
			{
				changedGroups &= changedGroups - 1;
				++changedBins;
			}
			subtitleSignatureChanged = previous == 0 || changedBins >= 2;
			if (subtitleSignatureChanged && previous != 0)
				m_subtitleSignatureGeneration.fetch_add(
					1, std::memory_order_acq_rel);
		}
	}
	const uint64_t minimumInterval = advancedMode ?
		(trackActive && !subtitleSignatureChanged ? 80 : 16) :
		// OCR is needed to acquire a caption and immediately after the cheap
		// subtitle-band signature changes.  Re-running it for every unchanged
		// frame only adds CPU load; a confirmed track supplies stable geometry
		// until the next signature change.
		(trackActive ? (subtitleSignatureChanged ? 16 : 150) : 35);
	if (last != 0 && now - last < minimumInterval)
		return;
	m_subtitleLastSubmitTick.store(now, std::memory_order_relaxed);

	SubtitleAnalysisFrame frame;
	frame.generation =
		m_subtitleAnalysisGeneration.load(std::memory_order_acquire);
	frame.frameNumber = frameNumber;
	frame.submittedTick = now;
	frame.sceneEventId =
		m_subtitleSceneEventId.load(std::memory_order_acquire);
	frame.subtitleSignatureChanged = subtitleSignatureChanged;
	frame.fullWidth = width;
	frame.fullHeight = height;
	frame.blackCode = blackCode;
	frame.scale = std::max(1, (width + 959) / 960);
	frame.width = (width + frame.scale - 1) / frame.scale;
	frame.height = (height + frame.scale - 1) / frame.scale;

	{
		std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
		if (!m_subtitleRecycledLuma.empty())
			frame.luma.swap(m_subtitleRecycledLuma);
	}
	frame.luma.resize(
		static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height));

	for (int y = 0; y < frame.height; ++y)
	{
		const int sourceY = std::min(height - 1, y * frame.scale);
		uint16_t* destination =
			frame.luma.data() + static_cast<size_t>(y) * frame.width;
		const uint16_t* source =
			yPlane + static_cast<size_t>(sourceY) * width;
		for (int x = 0; x < frame.width; ++x)
			destination[x] =
				static_cast<uint16_t>(source[std::min(width - 1, x * frame.scale)] >> 6);
	}

	{
		std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
		// Latest-frame-only backpressure: an analyzer that falls behind replaces
		// its pending job instead of building latency.
		if (m_subtitleJobPending && !m_subtitlePendingFrame.luma.empty() &&
			m_subtitleRecycledLuma.empty())
			m_subtitleRecycledLuma.swap(m_subtitlePendingFrame.luma);
		m_subtitlePendingFrame = std::move(frame);
		m_subtitleJobPending = true;
	}
	m_subtitleAnalysisCondition.notify_one();
}


void CBufferedLiveSourceVideoOutputPin::SubtitleAnalysisWorker()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	std::shared_ptr<GpuSubtitleDetector> gpuDetector;
	if (m_subtitleRepositionMode.load(std::memory_order_acquire) ==
		SubtitleRepositionMode::ADVANCED)
	{
		// Session initialization can compile DirectML kernels. Keep it off the
		// UI and delivery threads, then publish it so shutdown can cancel Run().
		gpuDetector = std::make_shared<GpuSubtitleDetector>();
		std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
		if (m_subtitleWorkerStop)
			return;
		m_subtitleGpuDetector = gpuDetector;
	}
	SubtitleTrackerState tracker;
	uint64_t trackerGeneration = std::numeric_limits<uint64_t>::max();
	uint64_t analysisCount = 0;
	uint64_t totalAnalysisUs = 0;
	uint64_t maximumAnalysisUs = 0;

	for (;;)
	{
		SubtitleAnalysisFrame frame;
		{
			std::unique_lock<std::mutex> lock(m_subtitleAnalysisMutex);
			m_subtitleAnalysisCondition.wait(lock, [this]() {
				return m_subtitleWorkerStop || m_subtitleJobPending;
				});
			if (m_subtitleWorkerStop)
				break;
			frame = std::move(m_subtitlePendingFrame);
			m_subtitlePendingFrame = {};
			m_subtitleJobPending = false;
		}

		if (frame.generation != trackerGeneration)
		{
			tracker = {};
			trackerGeneration = frame.generation;
		}

		const bool wasBarStable = tracker.barStable;
		const bool wasTracking = tracker.active;
		const auto started = GetWallClockTime();
		SubtitleAnalysisResult result = AnalyzeSubtitleFrame(
			frame, tracker, gpuDetector.get());
		const uint64_t analysisUs = (GetWallClockTime() - started) / 10;
		m_subtitleTrackActive.store(
			tracker.active, std::memory_order_release);
		if (result.barStable)
		{
			m_subtitlePictureTopPixels.store(
				result.pictureTop, std::memory_order_release);
			m_subtitlePictureBottomPixels.store(
				result.pictureBottom, std::memory_order_release);
		}
		++analysisCount;
		totalAnalysisUs += analysisUs;
		maximumAnalysisUs = std::max(maximumAnalysisUs, analysisUs);

		{
			std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
			if (frame.generation ==
				m_subtitleAnalysisGeneration.load(std::memory_order_acquire))
			{
				if (result.active || !tracker.active)
					m_subtitleLatestResult = result;
				else if (m_subtitleLatestResult.active)
				{
					// A single model miss must not erase a coherent track.
					// Preserve its geometry/mask while refreshing only its age;
					// the live compositor still requires plausible glyph pixels
					// in the current frame, so vanished captions are not drawn.
					m_subtitleLatestResult.producedTick =
						result.producedTick;
					m_subtitleLatestResult.frameNumber =
						result.frameNumber;
				}
			}
			if (m_subtitleRecycledLuma.empty())
				m_subtitleRecycledLuma.swap(frame.luma);
		}

		if (tracker.barStable != wasBarStable)
		{
			DebugLog::Log(
				"SUBTITLE ANALYZER: bottom black bar %s at row %d/%d",
				tracker.barStable ? "STABLE" : "LOST",
				tracker.stablePictureBottom, frame.height);
		}
		if (tracker.active != wasTracking)
		{
			if (!tracker.active)
			{
				// Panel dimensions belong to one coherent caption. Keeping them
				// after release made a single long/false detection permanently
				// widen every later subtitle until a graph reset.
				m_subtitlePanelHalfWidthPixels.store(
					0, std::memory_order_release);
				m_subtitlePanelHeightPixels.store(
					0, std::memory_order_release);
				m_subtitleFastSignature.store(
					0, std::memory_order_release);
				m_subtitleSignatureGeneration.fetch_add(
					1, std::memory_order_acq_rel);
			}
			DebugLog::Log(
				"SUBTITLE ANALYZER: subtitle track %s at frame %llu",
				tracker.active ? "ACQUIRED" : "RELEASED",
				frame.frameNumber);
		}

		if (analysisCount % 120 == 0)
		{
			DebugLog::Log(
				"SUBTITLE ANALYZER: 120-frame avg=%.3fms max=%.3fms active=%s confidence=%u",
				static_cast<double>(totalAnalysisUs) / 120000.0,
				static_cast<double>(maximumAnalysisUs) / 1000.0,
				result.active ? "YES" : "NO", result.confidence);
			totalAnalysisUs = 0;
			maximumAnalysisUs = 0;
		}
	}
	DebugLog::Log("SUBTITLE ANALYZER: worker exited");
}


bool CBufferedLiveSourceVideoOutputPin::DetectSubtitleWithWindowsOcr(
	const SubtitleAnalysisFrame& frame,
	int pictureTop,
	int pictureBottom,
	SubtitleRect& detected,
	int& score,
	bool& atTop,
	bool& textObserved,
	uint64_t& contentHash,
	int& lineCount,
	int& representativeLineHeight,
	std::vector<SubtitleRect>& words)
{
	contentHash = 0;
	lineCount = 0;
	representativeLineHeight = 0;
	const WindowsOcrSubtitleResult ocr = DetectWindowsOcrSubtitle(
		frame.luma.data(), frame.width, frame.height,
		pictureTop, pictureBottom);
	static std::atomic<bool> availabilityReported{ false };
	if (!availabilityReported.exchange(true, std::memory_order_relaxed))
		DebugLog::Log(
			"SUBTITLE ANALYZER: Windows OCR text-region detection %s",
			ocr.available ? "enabled" : "unavailable; using classical fallback");
	textObserved = ocr.textObserved;
	if (!ocr.detected)
		return false;
	detected = { ocr.left, ocr.top, ocr.right, ocr.bottom };
	score = ocr.score;
	atTop = ocr.atTop;
	contentHash = ocr.contentHash;
	lineCount = ocr.lineCount;
	representativeLineHeight = ocr.representativeLineHeight;
	words.clear();
	words.reserve(ocr.words.size());
	for (const WindowsOcrWordBox& word : ocr.words)
		words.push_back({ word.left, word.top, word.right, word.bottom });
	return true;
}


CBufferedLiveSourceVideoOutputPin::SubtitleAnalysisResult
CBufferedLiveSourceVideoOutputPin::AnalyzeSubtitleFrame(
	const SubtitleAnalysisFrame& frame,
	SubtitleTrackerState& tracker,
	GpuSubtitleDetector* gpuDetector)
{
	SubtitleAnalysisResult result;
	result.generation = frame.generation;
	result.frameNumber = frame.frameNumber;
	result.producedTick = GetTickCount64();
	result.fullWidth = frame.fullWidth;
	result.fullHeight = frame.fullHeight;
	result.blackCode = frame.blackCode;

	const int width = frame.width;
	const int height = frame.height;
	if (width < 80 || height < 60 ||
		frame.luma.size() < static_cast<size_t>(width) * height)
		return result;
	if (frame.sceneEventId != 0 &&
		frame.sceneEventId != tracker.lastSceneEventId)
	{
		tracker.lastSceneEventId = frame.sceneEventId;
		// Panel bounds are grow-only within a scene. A confirmed cut is the
		// deliberate boundary at which a shorter future caption may establish
		// a new width without causing frame-to-frame breathing.
		if (!tracker.active)
		{
			tracker.retainedPanel = {};
			tracker.styleSamples = 0;
			tracker.typicalLineHeight = 0;
		}
	}

	auto rectWidth = [](const SubtitleRect& r) { return std::max(0, r.right - r.left); };
	auto rectHeight = [](const SubtitleRect& r) { return std::max(0, r.bottom - r.top); };
	auto rectArea = [&](const SubtitleRect& r) {
		return static_cast<int64_t>(rectWidth(r)) * rectHeight(r);
		};
	auto intersectionOverUnion = [&](const SubtitleRect& a, const SubtitleRect& b) {
		SubtitleRect intersection{
			std::max(a.left, b.left), std::max(a.top, b.top),
			std::min(a.right, b.right), std::min(a.bottom, b.bottom)
		};
		const int64_t intersectionArea = rectArea(intersection);
		const int64_t unionArea = rectArea(a) + rectArea(b) - intersectionArea;
		return unionArea > 0 ?
			static_cast<double>(intersectionArea) / static_cast<double>(unionArea) : 0.0;
		};
	auto related = [&](const SubtitleRect& a, const SubtitleRect& b) {
		if (rectArea(a) == 0 || rectArea(b) == 0)
			return false;
		if (intersectionOverUnion(a, b) >= 0.12)
			return true;
		const int centerAx = a.left + rectWidth(a) / 2;
		const int centerAy = a.top + rectHeight(a) / 2;
		const int centerBx = b.left + rectWidth(b) / 2;
		const int centerBy = b.top + rectHeight(b) / 2;
		return std::abs(centerAx - centerBx) <=
			std::max(rectWidth(a), rectWidth(b)) / 4 &&
			std::abs(centerAy - centerBy) <=
			std::max(3, std::max(rectHeight(a), rectHeight(b))) &&
			std::max(rectHeight(a), rectHeight(b)) <=
			std::max(1, std::min(rectHeight(a), rectHeight(b))) * 3;
		};
	auto unionRect = [](const SubtitleRect& oldRect, const SubtitleRect& newRect) {
		if (oldRect.right <= oldRect.left || oldRect.bottom <= oldRect.top)
			return newRect;
		return SubtitleRect{
			std::min(oldRect.left, newRect.left),
			std::min(oldRect.top, newRect.top),
			std::max(oldRect.right, newRect.right),
			std::max(oldRect.bottom, newRect.bottom)
		};
		};

	// Establish the true black level from the bottom edge. This works for both
	// full and limited range and avoids hard-coding SDR values for HDR samples.
	std::vector<uint16_t> bottomSamples;
	const int edgeWidth = std::max(8, width / 5);
	const int bottomRows = std::max(3, std::min(12, height / 40));
	bottomSamples.reserve(static_cast<size_t>(edgeWidth) * bottomRows * 2);
	for (int y = height - bottomRows; y < height; ++y)
	{
		const uint16_t* row = frame.luma.data() + static_cast<size_t>(y) * width;
		for (int x = 0; x < edgeWidth; ++x)
			bottomSamples.push_back(row[x]);
		for (int x = width - edgeWidth; x < width; ++x)
			bottomSamples.push_back(row[x]);
	}
	const size_t medianIndex = bottomSamples.size() / 2;
	std::nth_element(bottomSamples.begin(), bottomSamples.begin() + medianIndex,
		bottomSamples.end());
	const uint16_t measuredBlack = bottomSamples[medianIndex];
	result.blackCode = measuredBlack;

	int candidatePictureTop = 0;
	int candidatePictureBottom = 0;
	const int allowedBlackDelta = 36;
	if (std::abs(static_cast<int>(measuredBlack) -
		static_cast<int>(frame.blackCode)) <= 96)
	{
		const int darkLimit = std::min(1023,
			static_cast<int>(measuredBlack) + allowedBlackDelta);
		int barRows = 0;
		for (int y = height - 1; y >= 0; --y)
		{
			const uint16_t* row =
				frame.luma.data() + static_cast<size_t>(y) * width;
			int dark = 0;
			int samples = 0;
			uint64_t sum = 0;
			for (int x = 0; x < edgeWidth; ++x)
			{
				dark += row[x] <= darkLimit;
				sum += row[x];
				++samples;
			}
			for (int x = width - edgeWidth; x < width; ++x)
			{
				dark += row[x] <= darkLimit;
				sum += row[x];
				++samples;
			}
			if (dark * 100 < samples * 84 ||
				sum > static_cast<uint64_t>(samples) *
				(static_cast<uint64_t>(measuredBlack) + 28))
				break;
			++barRows;
		}

		if (barRows >= std::max(3, height / 35) && barRows <= height / 3)
			candidatePictureBottom = height - barRows;

		int topBarRows = 0;
		for (int y = 0; y < height; ++y)
		{
			const uint16_t* row =
				frame.luma.data() + static_cast<size_t>(y) * width;
			int dark = 0;
			int samples = 0;
			uint64_t sum = 0;
			for (int x = 0; x < edgeWidth; ++x)
			{
				dark += row[x] <= darkLimit;
				sum += row[x];
				++samples;
			}
			for (int x = width - edgeWidth; x < width; ++x)
			{
				dark += row[x] <= darkLimit;
				sum += row[x];
				++samples;
			}
			if (dark * 100 < samples * 84 ||
				sum > static_cast<uint64_t>(samples) *
				(static_cast<uint64_t>(measuredBlack) + 28))
				break;
			++topBarRows;
		}
		if (topBarRows >= std::max(3, height / 35) &&
			topBarRows <= height / 3)
			candidatePictureTop = topBarRows;
	}

	const int barTolerance = std::max(2, height / 240);
	if (candidatePictureTop > 0)
	{
		if (tracker.pendingPictureTop > 0 &&
			std::abs(candidatePictureTop -
				tracker.pendingPictureTop) <= barTolerance)
		{
			tracker.pendingPictureTop =
				(tracker.pendingPictureTop * 3 + candidatePictureTop + 2) / 4;
			tracker.topBarHits =
				std::min<uint8_t>(8, tracker.topBarHits + 1);
		}
		else
		{
			tracker.pendingPictureTop = candidatePictureTop;
			tracker.topBarHits = 1;
		}
		tracker.topBarMisses = 0;
		const uint8_t requiredHits = tracker.topBarStable ? 8 : 2;
		if (tracker.topBarHits >= requiredHits)
		{
			if (!tracker.topBarStable ||
				std::abs(tracker.pendingPictureTop -
					tracker.stablePictureTop) > barTolerance)
			{
				tracker.stablePictureTop = tracker.pendingPictureTop;
				tracker.retainedPanel = {};
			}
			tracker.topBarStable = true;
		}
	}
	else
	{
		tracker.topBarHits = 0;
		tracker.topBarMisses =
			std::min<uint8_t>(60, tracker.topBarMisses + 1);
		// The edge probe can briefly fail while a subtitle, fade, or display
		// transition touches the letterbox boundary.  Keep the last confirmed
		// geometry for a bounded interval; dropping it immediately makes the
		// moved caption flash even though the bar has not actually changed.
		if (tracker.topBarMisses > 30)
		{
			tracker.topBarStable = false;
		}
	}

	if (candidatePictureBottom > 0)
	{
		if (tracker.pendingPictureBottom > 0 &&
			std::abs(candidatePictureBottom -
				tracker.pendingPictureBottom) <= barTolerance)
		{
			tracker.pendingPictureBottom =
				(tracker.pendingPictureBottom * 3 + candidatePictureBottom + 2) / 4;
			tracker.barHits = std::min<uint8_t>(8, tracker.barHits + 1);
		}
		else
		{
			tracker.pendingPictureBottom = candidatePictureBottom;
			tracker.barHits = 1;
		}
		tracker.barMisses = 0;
		const uint8_t requiredHits = tracker.barStable ? 8 : 2;
		if (tracker.barHits >= requiredHits)
		{
			if (!tracker.barStable ||
				std::abs(tracker.pendingPictureBottom -
					tracker.stablePictureBottom) > barTolerance)
			{
				tracker.stablePictureBottom = tracker.pendingPictureBottom;
				tracker.retainedPanel = {};
			}
			tracker.barStable = true;
		}
	}
	else
	{
		tracker.barHits = 0;
		tracker.barMisses = std::min<uint8_t>(60, tracker.barMisses + 1);
		// See the matching top-bar hold above.  The active caption has its own
		// shorter wall-clock expiry below, so retaining the bar state here does
		// not make a vanished caption persist indefinitely.
		if (tracker.barMisses > 30)
		{
			tracker.barStable = false;
		}
	}

	result.barStable = tracker.barStable;
	if (!tracker.barStable)
	{
		tracker.previousLuma = frame.luma;
		return result;
	}

	const int pictureBottom = tracker.stablePictureBottom;
	const int pictureTop = tracker.topBarStable ?
		tracker.stablePictureTop :
		std::max(0, height - pictureBottom);
	result.pictureTop =
		std::max(0, std::min(frame.fullHeight,
			pictureTop * frame.scale));
	result.pictureBottom =
		std::min(frame.fullHeight, pictureBottom * frame.scale);
	const int searchTop = std::max(1, pictureBottom - height / 5);
	const int roiHeight = height - searchTop;
	const bool advancedMode =
		m_subtitleRepositionMode.load(std::memory_order_acquire) ==
			SubtitleRepositionMode::ADVANCED;
	if (roiHeight < 8)
	{
		tracker.previousLuma = frame.luma;
		return result;
	}
	const bool havePreviousFrame =
		tracker.previousLuma.size() == frame.luma.size();
	if (!havePreviousFrame)
	{
		tracker.previousLuma = frame.luma;
		return result;
	}
	if (!advancedMode && tracker.active &&
		!frame.subtitleSignatureChanged && tracker.lastDetectionTick != 0)
	{
		// BASIC's Windows OCR is a recognition engine, not a real-time image
		// primitive.  A stable caption does not need recognition again: retain
		// its confirmed geometry and spend the CPU only when the inexpensive
		// boundary-band signature reports a likely text change.
		tracker.previousLuma = frame.luma;
		tracker.lastDetectionTick = result.producedTick;
		return result;
	}

	// Produce a local-contrast mask. Unlike the former global brightness
	// projection, this finds compact stroke-like regions and then groups them
	// into aligned text lines.
	std::vector<uint8_t> mask(
		static_cast<size_t>(width) * roiHeight, static_cast<uint8_t>(0));
	const int absoluteFloor = std::min(1023,
		static_cast<int>(measuredBlack) + 72);
	for (int localY = 1; localY + 1 < roiHeight; ++localY)
	{
		const int y = searchTop + localY;
		for (int x = 1; x + 1 < width; ++x)
		{
			const uint16_t value =
				frame.luma[static_cast<size_t>(y) * width + x];
			const uint16_t previousValue =
				tracker.previousLuma[static_cast<size_t>(y) * width + x];
			uint16_t localMinimum = 1023;
			uint16_t localMaximum = 0;
			for (int yy = y - 1; yy <= y + 1; ++yy)
				for (int xx = x - 1; xx <= x + 1; ++xx)
				{
					const uint16_t neighbor =
						frame.luma[static_cast<size_t>(yy) * width + xx];
					localMinimum = std::min(localMinimum, neighbor);
					localMaximum = std::max(localMaximum, neighbor);
				}
			if (value >= absoluteFloor &&
				std::abs(static_cast<int>(value) -
					static_cast<int>(previousValue)) <= 36 &&
				value >= static_cast<uint16_t>(
					std::min(1023, static_cast<int>(localMinimum) + 44)) &&
				localMaximum >= static_cast<uint16_t>(
					std::min(1023, static_cast<int>(localMinimum) + 60)))
				mask[static_cast<size_t>(localY) * width + x] = 1;
		}
	}
	tracker.previousLuma = frame.luma;

	GpuSubtitleDetectionResult gpuDetection;
	if (advancedMode && gpuDetector)
	{
		static thread_local bool availabilityLogged = false;
		gpuDetection = gpuDetector->Detect(
			frame.luma.data(),
			width,
			height,
			pictureTop,
			pictureBottom);
		if (!availabilityLogged)
		{
			if (gpuDetection.available)
				DebugLog::Log(
					"SUBTITLE ADVANCED DETECTOR: DirectML active on adapter %d",
					gpuDetector->SelectedDeviceId());
			else
				DebugLog::Log(
					"SUBTITLE ADVANCED DETECTOR: unavailable; using BASIC fallback");
			if (!gpuDetection.available)
				DebugLog::Log("SUBTITLE ADVANCED DETECTOR ERROR: %s",
					gpuDetector->LastError().c_str());
			availabilityLogged = true;
		}

		// DirectML identifies text regions. Keep actual P010 pixels only when
		// they also look like locally contrasting foreground, so the model's
		// filled probability polygons cannot copy video background into the
		// opaque relocated subtitle panel.
		if (gpuDetection.detected &&
			gpuDetection.textMask.size() ==
				static_cast<size_t>(width) * height &&
			!gpuDetection.atTop)
		{
			for (int y = std::max(searchTop + 1,
					gpuDetection.top - height / 80);
				y + 1 < std::min(height,
					gpuDetection.bottom + height / 80); ++y)
				for (int x = std::max(1,
						gpuDetection.left - width / 160);
					x + 1 < std::min(width,
						gpuDetection.right + width / 160); ++x)
				{
					if (!gpuDetection.textMask[
						static_cast<size_t>(y) * width + x])
						continue;
					const uint16_t value =
						frame.luma[static_cast<size_t>(y) * width + x];
					uint16_t localMinimum = value;
					uint16_t localMaximum = value;
					for (int yy = y - 1; yy <= y + 1; ++yy)
						for (int xx = x - 1; xx <= x + 1; ++xx)
						{
							const uint16_t neighbor =
								frame.luma[
									static_cast<size_t>(yy) * width + xx];
							localMinimum = std::min(localMinimum, neighbor);
							localMaximum = std::max(localMaximum, neighbor);
						}
					if (value >= std::min<int>(1023, measuredBlack + 24) &&
						value >= localMinimum + 20 &&
						localMaximum >= localMinimum + 28)
						mask[static_cast<size_t>(y - searchTop) *
							width + x] = 1;
				}
		}
	}
	const bool gpuFound =
		advancedMode && gpuDetection.available && gpuDetection.detected;

	SubtitleRect ocrDetected;
	int ocrScore = 0;
	bool ocrAtTop = false;
	bool ocrTextObserved = false;
	uint64_t ocrContentHash = 0;
	int ocrLineCount = 0;
	int ocrLineHeight = 0;
	std::vector<SubtitleRect> ocrWords;
	// BASIC is intentionally a CPU image-processing path: it uses the local
	// contrast, projection, component, and temporal-track logic below, not the
	// Windows text recognizer.  OCR recognition is far too expensive for a live
	// 60 fps capture pipeline (the latest run measured 45–75 ms passes).
	// Retain it only as a compatibility fallback when ADVANCED was requested
	// but DirectML is unavailable.
	bool ocrFound = false;
	if (advancedMode && !gpuDetection.available)
	{
		ocrFound = DetectSubtitleWithWindowsOcr(
			frame, pictureTop, pictureBottom,
			ocrDetected, ocrScore, ocrAtTop, ocrTextObserved,
			ocrContentHash, ocrLineCount, ocrLineHeight, ocrWords);
	}
	if (ocrFound && gpuFound && ocrAtTop == gpuDetection.atTop)
	{
		// OCR provides semantic classification while DirectML provides a more
		// stable whole-line extent. Union them so a partially recognized upper
		// line does not disappear from the extraction mask.
		ocrDetected = unionRect(
			ocrDetected,
			{ gpuDetection.left, gpuDetection.top,
				gpuDetection.right, gpuDetection.bottom });
		ocrScore += std::min(1000, gpuDetection.score);
		ocrLineCount = std::max(ocrLineCount, gpuDetection.lineCount);
		ocrLineHeight = std::max(
			ocrLineHeight, gpuDetection.representativeLineHeight);
		for (const GpuSubtitleRegion& region : gpuDetection.regions)
			ocrWords.push_back(
				{ region.left, region.top, region.right, region.bottom });
	}

	// Row projection complements connected components when a subtitle touches
	// the picture boundary and its strokes become connected to bright scene
	// detail. The projected rows still originate from local-contrast strokes,
	// not from an absolute "bright pixel" threshold.
	SubtitleRect projected = gpuFound ?
		SubtitleRect{ gpuDetection.left, gpuDetection.top,
			gpuDetection.right, gpuDetection.bottom } :
		SubtitleRect{};
	int projectedScore = gpuFound ? gpuDetection.score : 0;
	SubtitleRect currentProjection;
	int currentProjectionScore = 0;
	int projectionGap = 0;
	const int maximumProjectionGap = std::max(2, height / 120);
	auto finishProjection = [&]() {
		if (currentProjection.right > currentProjection.left &&
			currentProjection.bottom > pictureBottom &&
			currentProjection.bottom - currentProjection.top <= height / 6 &&
			currentProjectionScore > projectedScore)
		{
			projected = currentProjection;
			projectedScore = currentProjectionScore;
		}
		currentProjection = {};
		currentProjectionScore = 0;
		projectionGap = 0;
		};
	// Seed only from rows that are physically in the bar. This prevents a
	// bright lower-third or scene edge in the active picture from becoming the
	// starting point for relocation.
	const int firstBarLocalRow =
		std::max(0, pictureBottom - searchTop);
	for (int localY = firstBarLocalRow; localY < roiHeight; ++localY)
	{
		int count = 0;
		int left = width;
		int right = 0;
		const uint8_t* row =
			mask.data() + static_cast<size_t>(localY) * width;
		for (int x = 0; x < width; ++x)
			if (row[x])
			{
				++count;
				left = std::min(left, x);
				right = std::max(right, x + 1);
			}
		const bool textRow =
			count >= std::max(3, width / 400) &&
			right > left && right - left >= width / 28;
		if (textRow)
		{
			projectionGap = 0;
			if (currentProjection.right <= currentProjection.left)
				currentProjection = { left, searchTop + localY,
					right, searchTop + localY + 1 };
			else
			{
				currentProjection.left =
					std::min(currentProjection.left, left);
				currentProjection.right =
					std::max(currentProjection.right, right);
				currentProjection.bottom = searchTop + localY + 1;
			}
			currentProjectionScore += count;
		}
		else if (currentProjection.right > currentProjection.left &&
			++projectionGap > maximumProjectionGap)
			finishProjection();
	}
	finishProjection();

	// A subtitle can have another line entirely above the bar. Grow the
	// bar-seeded box upward only inside its horizontal footprint, with a
	// time-tested subtitle-height cap and enough gap tolerance for two lines.
	if (projected.right > projected.left)
	{
		const int seedWidth = projected.right - projected.left;
		const int scanLeft = std::max(0, projected.left - seedWidth / 12);
		const int scanRight = std::min(width, projected.right + seedWidth / 12);
		const int minimumTop =
			std::max(searchTop, projected.bottom - std::max(12, height / 7));
		const int upwardGapLimit = std::max(4, height / 30);
		int upwardGap = 0;
		for (int y = projected.top - 1; y >= minimumTop; --y)
		{
			int count = 0;
			int left = scanRight;
			int right = scanLeft;
			const uint8_t* row =
				mask.data() + static_cast<size_t>(y - searchTop) * width;
			for (int x = scanLeft; x < scanRight; ++x)
				if (row[x])
				{
					++count;
					left = std::min(left, x);
					right = std::max(right, x + 1);
				}
			const bool relatedTextRow =
				count >= std::max(3, seedWidth / 32) &&
				right > left && right - left >= seedWidth * 7 / 20;
			if (relatedTextRow)
			{
				projected.top = y;
				projectedScore += count;
				upwardGap = 0;
			}
			else if (++upwardGap > upwardGapLimit)
				break;
		}
	}

	// A one-pixel close reconnects antialiased character strokes but is too
	// small to join ordinary scene objects into subtitle-length regions.
	std::vector<uint8_t> closed(mask.size(), static_cast<uint8_t>(0));
	for (int y = 1; y + 1 < roiHeight; ++y)
		for (int x = 1; x + 1 < width; ++x)
		{
			bool set = false;
			for (int yy = y - 1; yy <= y + 1 && !set; ++yy)
				for (int xx = x - 1; xx <= x + 1; ++xx)
					set |= mask[static_cast<size_t>(yy) * width + xx] != 0;
			closed[static_cast<size_t>(y) * width + x] = set ? 1 : 0;
		}

	struct Component
	{
		SubtitleRect box;
		int pixels = 0;
	};
	std::vector<Component> components;
	std::vector<int> flood;
	flood.reserve(512);
	for (int localY = 1; localY + 1 < roiHeight; ++localY)
		for (int x = 1; x + 1 < width; ++x)
		{
			const int start = localY * width + x;
			if (!closed[start])
				continue;

			Component component;
			component.box = { x, searchTop + localY, x + 1, searchTop + localY + 1 };
			flood.clear();
			flood.push_back(start);
			closed[start] = 0;
			for (size_t index = 0; index < flood.size(); ++index)
			{
				const int position = flood[index];
				const int cy = position / width;
				const int cx = position - cy * width;
				++component.pixels;
				component.box.left = std::min(component.box.left, cx);
				component.box.right = std::max(component.box.right, cx + 1);
				component.box.top = std::min(component.box.top, searchTop + cy);
				component.box.bottom = std::max(component.box.bottom, searchTop + cy + 1);
				for (int yy = cy - 1; yy <= cy + 1; ++yy)
					for (int xx = cx - 1; xx <= cx + 1; ++xx)
					{
						if (xx <= 0 || xx + 1 >= width ||
							yy <= 0 || yy + 1 >= roiHeight)
							continue;
						const int neighbor = yy * width + xx;
						if (closed[neighbor])
						{
							closed[neighbor] = 0;
							flood.push_back(neighbor);
						}
					}
			}

			const int componentWidth = rectWidth(component.box);
			const int componentHeight = rectHeight(component.box);
			const int componentArea = componentWidth * componentHeight;
			if (component.pixels >= 3 && componentWidth >= 1 &&
				componentHeight >= 2 &&
				componentHeight <= std::max(8, height / 14) &&
				componentWidth <= width * 9 / 10 &&
				componentWidth <= std::max(8, componentHeight * 24) &&
				component.pixels * 100 >= componentArea * 6)
				components.push_back(component);
		}

	struct TextLine
	{
		SubtitleRect box;
		int components = 0;
		int pixels = 0;
	};
	std::sort(components.begin(), components.end(),
		[](const Component& a, const Component& b) {
			const int centerA = a.box.top + (a.box.bottom - a.box.top) / 2;
			const int centerB = b.box.top + (b.box.bottom - b.box.top) / 2;
			return centerA == centerB ? a.box.left < b.box.left : centerA < centerB;
		});
	std::vector<TextLine> lines;
	for (const Component& component : components)
	{
		const int componentCenter =
			component.box.top + rectHeight(component.box) / 2;
		TextLine* selected = nullptr;
		for (TextLine& line : lines)
		{
			const int lineCenter = line.box.top + rectHeight(line.box) / 2;
			const int verticalTolerance =
				std::max(3, std::max(rectHeight(line.box),
					rectHeight(component.box)) / 2);
			if (std::abs(componentCenter - lineCenter) <= verticalTolerance)
			{
				selected = &line;
				break;
			}
		}
		if (!selected)
		{
			lines.push_back({ component.box, 1, component.pixels });
			continue;
		}
		selected->box.left = std::min(selected->box.left, component.box.left);
		selected->box.top = std::min(selected->box.top, component.box.top);
		selected->box.right = std::max(selected->box.right, component.box.right);
		selected->box.bottom = std::max(selected->box.bottom, component.box.bottom);
		++selected->components;
		selected->pixels += component.pixels;
	}

	std::vector<TextLine> validLines;
	for (const TextLine& line : lines)
	{
		const int lineWidth = rectWidth(line.box);
		const int lineHeight = rectHeight(line.box);
		if (lineHeight >= 3 && lineHeight <= std::max(10, height / 11) &&
			lineWidth >= width / 28 && lineWidth <= width * 9 / 10 &&
			(line.components >= 2 || lineWidth >= width / 9))
			validLines.push_back(line);
	}

	const bool suppressClassicalFallback =
		!ocrFound && ocrTextObserved && !gpuFound;
	SubtitleRect detected = ocrFound ? ocrDetected :
		(suppressClassicalFallback ? SubtitleRect{} : projected);
	int detectedScore = ocrFound ? ocrScore :
		(suppressClassicalFallback ? 0 : projectedScore);
	for (const TextLine& line : validLines)
	{
		if (ocrFound || suppressClassicalFallback ||
			rectArea(projected) > 0)
			break;
		if (line.box.bottom <= pictureBottom)
			continue;
		SubtitleRect block = line.box;
		int score = line.components * 20 + rectWidth(line.box) + line.pixels / 4;
		for (const TextLine& other : validLines)
		{
			if (&other == &line)
				continue;
			const int verticalGap =
				other.box.bottom <= block.top ? block.top - other.box.bottom :
				(block.bottom <= other.box.top ? other.box.top - block.bottom : 0);
			const int centerDifference = std::abs(
				(other.box.left + other.box.right) -
				(block.left + block.right)) / 2;
			if (verticalGap <= std::max(5,
				std::max(rectHeight(other.box), rectHeight(block)) * 2) &&
				centerDifference <= width / 5)
			{
				block.left = std::min(block.left, other.box.left);
				block.top = std::min(block.top, other.box.top);
				block.right = std::max(block.right, other.box.right);
				block.bottom = std::max(block.bottom, other.box.bottom);
				score += other.components * 20 + rectWidth(other.box) / 2;
			}
		}
		if (rectHeight(block) <= height / 7 && score > detectedScore)
		{
			detected = block;
			detectedScore = score;
		}
	}

	// Subtitles are expected to occupy or overlap the central picture region.
	// Reject isolated side text and menu/navigation labels.
	const int detectedCenter =
		detected.left + rectWidth(detected) / 2;
	const bool centeredDetection =
		rectArea(detected) > 0 &&
		detectedCenter >= width * 3 / 10 &&
		detectedCenter <= width * 7 / 10 &&
		detected.right >= width * 2 / 5 &&
		detected.left <= width * 3 / 5;
	const bool sameTrackedCaption =
		ocrFound && ocrContentHash != 0 && tracker.active &&
		tracker.trackedFromOcr &&
		ocrContentHash == tracker.trackedContentHash;
	const int detectedLineHeight = ocrFound ? ocrLineHeight :
		(gpuFound ? gpuDetection.representativeLineHeight : 0);
	const bool consistentLineHeight =
		(!ocrFound && !gpuFound) || detectedLineHeight <= 0 ||
		tracker.styleSamples < 3 ||
		sameTrackedCaption ||
		(detectedLineHeight * 2 >= tracker.typicalLineHeight &&
			detectedLineHeight <= tracker.typicalLineHeight * 2);
	const bool validOcrMetrics =
		!ocrFound || (ocrLineCount >= 1 && ocrLineCount <= 3 &&
			ocrContentHash != 0);
	const bool validModelMetrics =
		!gpuFound ||
		(gpuDetection.score >= 100 &&
			rectWidth(detected) >= width / 80 &&
			// Caption blocks can be wide, but a box approaching the whole
			// picture is a model false-positive or menu, not a subtitle.  The
			// previous 80% ceiling admitted the oversized panels in the log.
			rectWidth(detected) <= width * 18 / 25 &&
			rectHeight(detected) >= std::max(3, height / 180) &&
			rectHeight(detected) <= height / 7 &&
			gpuDetection.lineCount >= 1 && gpuDetection.lineCount <= 3);
	const bool plausibleClassicalDetection =
		ocrFound || gpuFound ||
		(detectedScore >= 30 &&
			rectWidth(detected) >= width / 50 &&
			rectWidth(detected) <= width * 17 / 20 &&
			rectHeight(detected) <= height / 7);
	const bool found =
		centeredDetection && consistentLineHeight && validOcrMetrics &&
		validModelMetrics &&
		plausibleClassicalDetection;
	const bool detectionAtTop =
		ocrFound ? ocrAtTop : (gpuFound && gpuDetection.atTop);
	const bool detectionFromModel = gpuFound;
	auto mergeWordRects = [&](std::vector<SubtitleRect>& destination,
		const std::vector<SubtitleRect>& source) {
		for (const SubtitleRect& word : source)
		{
			bool merged = false;
			for (SubtitleRect& existing : destination)
				if (intersectionOverUnion(existing, word) >= 0.20)
				{
					existing = unionRect(existing, word);
					merged = true;
					break;
				}
			if (!merged && destination.size() < 64)
				destination.push_back(word);
		}
		};
	auto updateCaptionStyle = [&]() {
		if ((!ocrFound && !gpuFound) || detectedLineHeight <= 0)
			return;
		if (tracker.styleSamples == 0)
			tracker.typicalLineHeight = detectedLineHeight;
		else
			tracker.typicalLineHeight =
				(tracker.typicalLineHeight * 7 +
					detectedLineHeight + 4) / 8;
		tracker.styleSamples =
			std::min<uint8_t>(16, tracker.styleSamples + 1);
		};
	auto updateRetainedPanel = [&]() {
		if (rectArea(tracker.retainedPanel) == 0)
		{
			tracker.retainedPanel = tracker.tracked;
			return;
		}

		const int edgeTolerance = std::max(2, width / 160);
		const bool growsPanel =
			tracker.tracked.left <
				tracker.retainedPanel.left - edgeTolerance ||
			tracker.tracked.right >
				tracker.retainedPanel.right + edgeTolerance;
		if (growsPanel)
		{
			// Grow immediately, but never shrink within the current scene. This
			// keeps the destination panel fixed across short and long captions
			// and removes visible horizontal breathing. A confirmed scene cut
			// or queue/aspect reset starts fresh.
			tracker.retainedPanel =
				unionRect(tracker.retainedPanel, tracker.tracked);
		}
		};
	bool captionTransitionPending = false;
	// A live result is published only after the tracker confirms that its
	// geometry belongs to the current subtitle.  Keeping this separate from
	// `found` prevents a single weak GPU rectangle from replacing the last
	// coherent mask/panel while a new caption is being evaluated.
	bool confirmedTrackedThisPass = false;
	if (found)
	{
		const bool sameCandidateCaption =
			ocrFound && ocrContentHash != 0 &&
			ocrContentHash == tracker.candidateContentHash;
		if (rectArea(tracker.candidate) > 0 &&
			tracker.candidateAtTop == detectionAtTop &&
			((ocrFound && sameCandidateCaption) ||
				(!ocrFound && related(tracker.candidate, detected))))
		{
			// Expand immediately so an OCR result that discovers an initial or
			// final word can never produce the former slowly growing caption.
			tracker.candidate = unionRect(tracker.candidate, detected);
			tracker.candidateHits =
				std::min<uint8_t>(8, tracker.candidateHits + 1);
			if (ocrFound)
				mergeWordRects(tracker.candidateWords, ocrWords);
			tracker.candidateFromModel |= detectionFromModel;
		}
		else
		{
			tracker.candidate = detected;
			tracker.candidateWords = ocrFound ?
				ocrWords : std::vector<SubtitleRect>{};
			tracker.candidateHits = 1;
			tracker.candidateAtTop = detectionAtTop;
			tracker.candidateFromModel = detectionFromModel;
			tracker.candidateContentHash =
				ocrFound ? ocrContentHash : 0;
		}

		if (tracker.active && sameTrackedCaption &&
			tracker.trackedAtTop == detectionAtTop &&
			related(tracker.tracked, detected))
		{
			// Same caption: grow immediately, never contract from OCR jitter.
			tracker.tracked = unionRect(tracker.tracked, detected);
			mergeWordRects(tracker.trackedWords, ocrWords);
			tracker.trackedFromModel |= detectionFromModel;
			tracker.trackMisses = 0;
			tracker.lastDetectionTick = result.producedTick;
			tracker.confirmedScore = std::max(100, detectedScore);
			confirmedTrackedThisPass = true;
			updateCaptionStyle();
			updateRetainedPanel();
		}
		else if (tracker.candidateHits >= 2)
		{
			// Geometry alone is not enough to draw a caption. Require two
			// consistent analyses in both modes so a single GPU false-positive
			// cannot create a panel or seed an incomplete glyph snapshot.
			tracker.tracked = tracker.candidate;
			tracker.trackedAtTop = tracker.candidateAtTop;
			tracker.trackedFromOcr = ocrFound;
			tracker.trackedFromModel = tracker.candidateFromModel;
			tracker.trackedWords = ocrFound ?
				tracker.candidateWords : std::vector<SubtitleRect>{};
			tracker.trackedContentHash =
				ocrFound ? tracker.candidateContentHash : 0;
			tracker.trackMisses = 0;
			tracker.lastDetectionTick = result.producedTick;
			tracker.active = true;
			tracker.confirmedScore = std::max(100, detectedScore);
			confirmedTrackedThisPass = true;
			updateCaptionStyle();
			updateRetainedPanel();
		}
		else if (tracker.active)
		{
			captionTransitionPending = ocrFound &&
				tracker.trackedFromOcr && !sameTrackedCaption;
			tracker.trackMisses = std::min<uint8_t>(10, tracker.trackMisses + 1);
		}
	}
	else
	{
		tracker.candidate = {};
		tracker.candidateWords.clear();
		tracker.candidateContentHash = 0;
		tracker.candidateFromModel = false;
		tracker.candidateHits = 0;
		if (tracker.active)
			tracker.trackMisses = std::min<uint8_t>(10, tracker.trackMisses + 1);
	}

	// Hold through short fades and detector misses using wall time rather than
	// a cadence-dependent miss count. Advanced analysis is asynchronous and a
	// busy renderer can legitimately delay several consecutive passes.
	if (tracker.active &&
		(tracker.lastDetectionTick == 0 ||
			result.producedTick < tracker.lastDetectionTick ||
			result.producedTick - tracker.lastDetectionTick > 1500))
	{
		tracker.active = false;
		tracker.tracked = {};
		tracker.trackedWords.clear();
		tracker.trackedContentHash = 0;
		tracker.trackedFromModel = false;
		tracker.confirmedScore = 0;
	}

	// Let the worker retain the last coherent result during a short detector
	// gap or while a possible replacement subtitle accumulates confirmation.
	// This is intentionally before mask construction: rebuilding a mask from
	// unconfirmed geometry was the source of the confidence=1 and flickering
	// output observed in the latest run.
	if (tracker.active && !confirmedTrackedThisPass)
		return result;

	if (captionTransitionPending || !tracker.active ||
		(tracker.trackedAtTop ?
			tracker.tracked.top >= pictureTop :
			tracker.tracked.bottom <= pictureBottom))
		return result;
	// Never regenerate an OCR glyph mask from old word geometry when the
	// current OCR pass did not confirm text. The worker retains the preceding
	// coherent result briefly; current-frame validation decides whether it is
	// still safe to use.
	if ((tracker.trackedFromOcr || tracker.trackedFromModel) &&
		!ocrFound && !gpuFound)
		return result;

	const int paddingX =
		(tracker.trackedFromOcr || tracker.trackedFromModel) ?
		std::max(4, width / 120) :
		std::max(2, width / 320);
	const int paddingY =
		(tracker.trackedFromOcr || tracker.trackedFromModel) ?
		std::max(4, height / 100) :
		std::max(2, height / 240);
	result.source.left =
		std::max(0, (tracker.tracked.left - paddingX) * frame.scale);
	result.source.top =
		std::max(0, (tracker.tracked.top - paddingY) * frame.scale);
	result.source.right =
		std::min(frame.fullWidth,
			(tracker.tracked.right + paddingX) * frame.scale);
	result.source.bottom =
		std::min(frame.fullHeight,
			(tracker.tracked.bottom + paddingY) * frame.scale);
	const SubtitleRect panel = rectArea(tracker.retainedPanel) > 0 ?
		tracker.retainedPanel : tracker.tracked;
	result.panelLeft = std::max(0,
		(panel.left - paddingX) * frame.scale);
	result.panelRight = std::min(frame.fullWidth,
		(panel.right + paddingX) * frame.scale);
	result.analysisScale = frame.scale;
	result.maskLeft = std::max(0, tracker.tracked.left - paddingX);
	result.maskTop = std::max(0, tracker.tracked.top - paddingY);
	const int maskRight =
		std::min(width, tracker.tracked.right + paddingX);
	const int maskBottom =
		std::min(height, tracker.tracked.bottom + paddingY);
	result.maskWidth = std::max(0, maskRight - result.maskLeft);
	result.maskHeight = std::max(0, maskBottom - result.maskTop);
	if (result.maskWidth > 0 && result.maskHeight > 0)
	{
		auto textMask = std::make_shared<std::vector<uint8_t>>(
			static_cast<size_t>(result.maskWidth) * result.maskHeight,
			static_cast<uint8_t>(0));
		auto textReferenceLuma = std::make_shared<std::vector<uint16_t>>(
			static_cast<size_t>(result.maskWidth) * result.maskHeight,
			static_cast<uint16_t>(0));
		if (tracker.trackedFromOcr)
		{
			// OCR supplies geometry only; it never redraws text. Build a sparse
			// mask from actual high-contrast pixels in the analyzed P010 frame.
			// Filling whole OCR word rectangles admitted picture pixels and made
			// stale geometry look like fragmented, re-rendered captions.
			const int wordPaddingX = std::max(1, width / 640);
			const int wordPaddingY = std::max(1, height / 360);
			for (const SubtitleRect& word : tracker.trackedWords)
			{
				const int left = std::max(result.maskLeft,
					word.left - wordPaddingX);
				const int top = std::max(result.maskTop,
					word.top - wordPaddingY);
				const int right = std::min(maskRight,
					word.right + wordPaddingX);
				const int bottom = std::min(maskBottom,
					word.bottom + wordPaddingY);
				if (right <= left || bottom <= top)
					continue;
				for (int y = top; y < bottom; ++y)
					for (int x = left; x < right; ++x)
					{
						const uint16_t value =
							frame.luma[static_cast<size_t>(y) * width + x];
						uint16_t localMinimum = value;
						uint16_t localMaximum = value;
						for (int yy = std::max(0, y - 1);
							yy <= std::min(height - 1, y + 1); ++yy)
							for (int xx = std::max(0, x - 1);
								xx <= std::min(width - 1, x + 1); ++xx)
							{
								const uint16_t neighbor =
									frame.luma[static_cast<size_t>(yy) * width + xx];
								localMinimum = std::min(localMinimum, neighbor);
								localMaximum = std::max(localMaximum, neighbor);
							}
						if (value < std::min<int>(1023, measuredBlack + 24) ||
							localMaximum - localMinimum < 28 ||
							value < localMinimum + 20)
							continue;
						const size_t index =
							static_cast<size_t>(y - result.maskTop) *
								result.maskWidth + (x - result.maskLeft);
						(*textMask)[index] = 1;
						(*textReferenceLuma)[index] = value;
					}
			}
		}
		else
		{
			for (int y = 0; y < result.maskHeight; ++y)
			{
				const int maskY = result.maskTop + y - searchTop;
				if (maskY < 0 || maskY >= roiHeight)
					continue;
				std::copy_n(
					mask.data() + static_cast<size_t>(maskY) * width +
						result.maskLeft,
					result.maskWidth,
					textMask->data() + static_cast<size_t>(y) *
						result.maskWidth);
				for (int x = 0; x < result.maskWidth; ++x)
					if ((*textMask)[static_cast<size_t>(y) *
						result.maskWidth + x])
						(*textReferenceLuma)[static_cast<size_t>(y) *
							result.maskWidth + x] =
							frame.luma[static_cast<size_t>(
								result.maskTop + y) * width +
								result.maskLeft + x];
			}
		}
		const size_t maskPixels = static_cast<size_t>(std::count(
			textMask->begin(), textMask->end(), static_cast<uint8_t>(1)));
		if (maskPixels < 12)
			return result;
		result.textMask = std::move(textMask);
		result.textReferenceLuma = std::move(textReferenceLuma);
	}
	result.confidence = static_cast<uint16_t>(
		std::max(100, std::min(1000, tracker.confirmedScore)));
	result.sourceAtTop = tracker.trackedAtTop;
	result.ocrBased = tracker.trackedFromOcr;
	result.modelBased = tracker.trackedFromModel;
	result.active = rectArea(result.source) > 0 &&
		result.source.right > result.source.left &&
		result.source.bottom > result.source.top &&
		result.maskWidth > 0 && result.maskHeight > 0 &&
		result.textMask && result.textReferenceLuma;
	return result;
}


bool CBufferedLiveSourceVideoOutputPin::CompositeTrackedSubtitle(
	uint16_t* yPlane,
	uint16_t* uvPlane,
	int width,
	int height,
	uint16_t blackCode,
	const SubtitleAnalysisResult& result)
{
	static thread_local uint64_t rejectedFrames = 0;
	auto clearCachedRectangle = [&](int left, int top, int right, int bottom,
		uint16_t fillY) {
		left = std::max(0, std::min(width, left)) & ~1;
		right = std::max(left, std::min(width, right)) & ~1;
		top = std::max(0, std::min(height, top)) & ~1;
		bottom = std::max(top, std::min(height, bottom)) & ~1;
		const uint16_t neutralUV = static_cast<uint16_t>(512u << 6);
		for (int y = top; y < bottom; ++y)
			std::fill_n(yPlane +
				static_cast<size_t>(y) * width + left,
				right - left, fillY);
		for (int y = top / 2; y < bottom / 2; ++y)
			for (int x = left; x < right; x += 2)
			{
				const size_t uv =
					static_cast<size_t>(y) * width + x;
				uvPlane[uv] = neutralUV;
				uvPlane[uv + 1] = neutralUV;
			}
		};
	auto renderCachedSubtitle = [&]() {
		const uint64_t signatureGeneration =
			m_subtitleSignatureGeneration.load(std::memory_order_acquire);
		const size_t cachedWords =
			static_cast<size_t>(m_subtitleCachedSourceWidth) *
			m_subtitleCachedSourceHeight;
		if (!result.active || result.confidence == 0 ||
			result.source.right <= result.source.left ||
			result.source.bottom <= result.source.top ||
			m_subtitleTemporalSampleCount < 2 ||
			m_subtitleCachedGlyphCount == 0 ||
			m_subtitleCachedSignatureGeneration != signatureGeneration ||
			m_subtitleCachedFrameWidth != width ||
			m_subtitleCachedFrameHeight != height ||
			m_subtitleCachedSourceWidth <= 0 ||
			m_subtitleCachedSourceHeight <= 0 ||
			m_subtitleCachedGlyphMask.size() != cachedWords ||
			m_subtitleCachedGlyphY.size() != cachedWords)
			return false;

		const uint16_t blackY = static_cast<uint16_t>(
			std::min<int>(1023, blackCode) << 6);
		if (result.sourceAtTop)
			clearCachedRectangle(0, 0, width,
				result.pictureTop, blackY);
		else
			clearCachedRectangle(0, result.pictureBottom,
				width, height, blackY);
		clearCachedRectangle(
			m_subtitleCachedSourceClearLeft,
			m_subtitleCachedSourceClearTop,
			m_subtitleCachedSourceClearRight,
			m_subtitleCachedSourceClearBottom,
			blackY);
		clearCachedRectangle(
			m_subtitleCachedBackgroundLeft,
			m_subtitleCachedBackgroundTop,
			m_subtitleCachedBackgroundRight,
			m_subtitleCachedBackgroundBottom,
			m_subtitleCachedPanelY);
		for (int y = 0; y < m_subtitleCachedSourceHeight; ++y)
			for (int x = 0; x < m_subtitleCachedSourceWidth; ++x)
			{
				const size_t index =
					static_cast<size_t>(y) *
						m_subtitleCachedSourceWidth + x;
				if (!m_subtitleCachedGlyphMask[index])
					continue;
				const int destinationX =
					m_subtitleCachedSourceLeft + x;
				const int destinationY =
					m_subtitleCachedTargetTop + y;
				if (destinationX >= 0 && destinationX < width &&
					destinationY >= 0 && destinationY < height)
					yPlane[static_cast<size_t>(destinationY) *
						width + destinationX] =
						m_subtitleCachedGlyphY[index];
			}
		return true;
		};
	auto rejectCurrentFrame = [&](const char* reason) {
		++rejectedFrames;
		const bool usedCache = renderCachedSubtitle();
		if (rejectedFrames <= 5 || rejectedFrames % 120 == 0)
			DebugLog::Log(
				"SUBTITLE LIVE DETECTOR: rejected frame (%s%s), count=%llu",
				reason, usedCache ? "; cached glyphs retained" : "",
				rejectedFrames);
		return usedCache;
		};
	if (!yPlane || !uvPlane || !result.barStable || !result.active ||
		result.confidence == 0 ||
		result.source.right <= result.source.left ||
		result.source.bottom <= result.source.top ||
		result.pictureTop < 0 ||
		result.pictureBottom <= 0 || result.pictureBottom > height)
		return rejectCurrentFrame("invalid-state");

	// OCR remains useful for classification, but it is deliberately not the
	// frame-by-frame compositor gate. Search a fixed central band around the
	// stable picture edge in the current P010 frame so a new caption can move
	// on its first frame and OCR latency cannot make native/moved text flash.
	const bool sourceAtTop =
		result.active && result.sourceAtTop && result.pictureTop > 0;
	int sourceLeft = (width / 12) & ~1;
	int sourceRight = (width - width / 12) & ~1;
	// Keep the temporal crop fixed while a caption is active. The former crop
	// expanded and contracted with each asynchronous model result, constantly
	// invalidating the cache and moving copied pixels. One fifth of the frame
	// covers three subtitle lines while remaining a small processing region.
	int sourceTop = sourceAtTop ?
		std::max(0, result.pictureTop - height / 10) :
		std::max(0, result.pictureBottom - height / 5);
	int sourceBottom = sourceAtTop ?
		std::min(height, result.pictureTop + height / 5) :
		std::min(height, result.pictureBottom + height / 10);
	sourceLeft &= ~1;
	sourceRight &= ~1;
	sourceTop &= ~1;
	sourceBottom &= ~1;
	if (sourceRight <= sourceLeft || sourceBottom <= sourceTop)
		return rejectCurrentFrame("invalid-roi");

	const int sourceWidth = sourceRight - sourceLeft;
	const int sourceHeight = sourceBottom - sourceTop;
	const int activePictureHeight =
		std::max(height / 3, result.pictureBottom - result.pictureTop);
	const int lift = std::max(4, activePictureHeight / 20) & ~1;
	const int backgroundPaddingX = std::max(8, width / 160) & ~1;
	const int backgroundPaddingY = std::max(4, height / 270) & ~1;

	const size_t yWords =
		static_cast<size_t>(sourceWidth) * sourceHeight;
	const size_t uvWords =
		static_cast<size_t>(sourceWidth) * (sourceHeight / 2);
	m_subtitleScratchY.resize(yWords);
	m_subtitleScratchUV.resize(uvWords);
	m_subtitleGlyphMask.resize(yWords);
	m_subtitleGlyphCandidateMask.resize(yWords);
	m_subtitleGlyphGrowthMask.resize(yWords);

	const bool hasOcrTextMask =
		result.active && (result.ocrBased || result.modelBased) &&
		result.analysisScale > 0 &&
		result.maskWidth > 0 && result.maskHeight > 0 &&
		result.textMask &&
		result.textReferenceLuma &&
		result.textMask->size() ==
			static_cast<size_t>(result.maskWidth) * result.maskHeight &&
		result.textReferenceLuma->size() == result.textMask->size();
	auto ocrReferenceLumaAt = [&](int frameX, int frameY) {
		if (!hasOcrTextMask)
			return static_cast<uint16_t>(0);
		const int maskX =
			frameX / result.analysisScale - result.maskLeft;
		const int maskY =
			frameY / result.analysisScale - result.maskTop;
		// The analyzer samples one pixel per scale-sized cell. A one-cell
		// dilation restores full-resolution stroke edges and antialiasing
		// without admitting the surrounding picture or translucent box.
		uint16_t referenceLuma = 0;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int x = maskX + dx;
				const int y = maskY + dy;
				if (x < 0 || x >= result.maskWidth ||
					y < 0 || y >= result.maskHeight)
					continue;
				const size_t index = static_cast<size_t>(y) *
					result.maskWidth + x;
				if ((*result.textMask)[index])
					referenceLuma = std::max(referenceLuma,
						(*result.textReferenceLuma)[index]);
			}
		return referenceLuma;
		};

	for (int y = 0; y < sourceHeight; ++y)
		std::copy_n(yPlane +
			static_cast<size_t>(sourceTop + y) * width + sourceLeft,
			sourceWidth,
			m_subtitleScratchY.data() + static_cast<size_t>(y) * sourceWidth);
	for (int y = 0; y < sourceHeight / 2; ++y)
		std::copy_n(uvPlane +
			static_cast<size_t>(sourceTop / 2 + y) * width + sourceLeft,
			sourceWidth,
			m_subtitleScratchUV.data() + static_cast<size_t>(y) * sourceWidth);

	// Learn foreground directly from non-black pixels physically inside the
	// bar. This is alphabet-independent: accents, CJK glyphs, Arabic text,
	// musical notes, and other caption symbols are treated as image shapes.
	std::array<uint32_t, 1024> histogram{};
	size_t histogramWords = 0;
	for (int y = 0; y < sourceHeight; ++y)
	{
		const int frameY = sourceTop + y;
		const bool inBar = sourceAtTop ?
			frameY < result.pictureTop :
			frameY >= result.pictureBottom;
		if (!inBar)
			continue;
		for (int x = 0; x < sourceWidth; ++x)
		{
			const size_t chromaIndex =
				static_cast<size_t>(y / 2) * sourceWidth + (x & ~1);
			const int chromaU = static_cast<int>(
				m_subtitleScratchUV[chromaIndex] >> 6);
			const int chromaV = static_cast<int>(
				m_subtitleScratchUV[chromaIndex + 1] >> 6);
			if (std::abs(chromaU - 512) > 112 ||
				std::abs(chromaV - 512) > 112)
				continue;
			const uint16_t value = static_cast<uint16_t>(
				m_subtitleScratchY[
					static_cast<size_t>(y) * sourceWidth + x] >> 6);
			if (value <= blackCode + 12)
				continue;
			++histogram[std::min<size_t>(1023, value)];
			++histogramWords;
		}
	}
	if (histogramWords < 16 && hasOcrTextMask && result.active)
	{
		// Netflix-style captions can straddle the picture edge with only a
		// handful of antialiased pixels physically inside the black bar. Once
		// the model has established a boundary-crossing subtitle block, learn
		// its foreground from the confirmed mask across the whole block rather
		// than requiring an arbitrary number of pixels below the boundary.
		for (int y = 0; y < sourceHeight; ++y)
			for (int x = 0; x < sourceWidth; ++x)
			{
				const int frameX = sourceLeft + x;
				const int frameY = sourceTop + y;
				const uint16_t reference =
					ocrReferenceLumaAt(frameX, frameY);
				if (reference <= blackCode + 48)
					continue;
				const size_t chromaIndex =
					static_cast<size_t>(y / 2) * sourceWidth + (x & ~1);
				const int chromaU = static_cast<int>(
					m_subtitleScratchUV[chromaIndex] >> 6);
				const int chromaV = static_cast<int>(
					m_subtitleScratchUV[chromaIndex + 1] >> 6);
				if (std::abs(chromaU - 512) > 112 ||
					std::abs(chromaV - 512) > 112)
					continue;
				const uint16_t value = static_cast<uint16_t>(
					m_subtitleScratchY[
						static_cast<size_t>(y) * sourceWidth + x] >> 6);
				if (value <= blackCode + 20)
					continue;
				++histogram[std::min<size_t>(1023, value)];
				++histogramWords;
			}
	}
	if (histogramWords < 8)
		return rejectCurrentFrame("no-bar-foreground");

	auto percentile = [&](uint32_t numerator, uint32_t denominator) {
		const uint64_t target =
			std::max<uint64_t>(1,
				(static_cast<uint64_t>(histogramWords) * numerator) /
					denominator);
		uint64_t accumulated = 0;
		for (size_t value = 0; value < histogram.size(); ++value)
		{
			accumulated += histogram[value];
			if (accumulated >= target)
				return static_cast<uint16_t>(value);
		}
		return static_cast<uint16_t>(1023);
		};
	const uint16_t high = percentile(19, 20);
	if (high <= blackCode + 28)
		return rejectCurrentFrame("insufficient-contrast");
	const uint16_t hardThreshold = static_cast<uint16_t>(
		std::min<int>(high, std::max<int>(
			static_cast<int>(blackCode) + 40,
			static_cast<int>(percentile(3, 4)))));
	const uint16_t softThreshold = static_cast<uint16_t>(
		std::min<int>(1023, blackCode + 12));

	size_t hardSeedWords = 0;
	for (int y = 0; y < sourceHeight; ++y)
		for (int x = 0; x < sourceWidth; ++x)
		{
			const size_t index = static_cast<size_t>(y) * sourceWidth + x;
			const uint16_t value =
				static_cast<uint16_t>(m_subtitleScratchY[index] >> 6);
			const size_t chromaIndex =
				static_cast<size_t>(y / 2) * sourceWidth + (x & ~1);
			const int chromaU = static_cast<int>(
				m_subtitleScratchUV[chromaIndex] >> 6);
			const int chromaV = static_cast<int>(
				m_subtitleScratchUV[chromaIndex + 1] >> 6);
			const int frameX = sourceLeft + x;
			const int frameY = sourceTop + y;
			const uint16_t ocrReference = hasOcrTextMask ?
				ocrReferenceLumaAt(frameX, frameY) : 0;
			const bool confirmedStrong =
				hasOcrTextMask &&
				ocrReference > blackCode + 160 &&
				std::abs(chromaU - 512) <= 160 &&
				std::abs(chromaV - 512) <= 160 &&
				value >= std::max<int>(
					std::max<int>(
						static_cast<int>(blackCode) + 96,
						(static_cast<int>(hardThreshold) * 85 + 99) / 100),
					(static_cast<int>(ocrReference) * 60 + 99) / 100);
			const bool confirmedSoft =
				hasOcrTextMask &&
				ocrReference > blackCode + 128 &&
				std::abs(chromaU - 512) <= 192 &&
				std::abs(chromaV - 512) <= 192 &&
				value >= std::max<int>(
					std::max<int>(
						static_cast<int>(blackCode) + 48,
						(static_cast<int>(hardThreshold) * 35 + 99) / 100),
					(static_cast<int>(ocrReference) * 35 + 99) / 100);

			// A confirmed line crossing the lower bar is strong evidence that a
			// nearby centered line immediately above is part of the same caption.
			// Detect only bright neutral cores in that narrow companion zone.
			const int companionDistance = std::max(
				height / 10,
				2 * std::max(1,
					result.source.bottom - result.source.top));
			const bool inCompanionZone = hasOcrTextMask &&
				(sourceAtTop ?
					(frameY >= result.source.bottom &&
						frameY < result.source.bottom + companionDistance) :
					(frameY < result.source.top &&
						frameY >= result.source.top - companionDistance)) &&
				frameX >= std::max(0, result.source.left - width / 8) &&
				frameX < std::min(width, result.source.right + width / 8);
			const int companionStrongThreshold = std::max<int>(
				static_cast<int>(blackCode) + 160,
				(static_cast<int>(hardThreshold) * 85 + 99) / 100);
			const bool companionStrong =
				inCompanionZone &&
				std::abs(chromaU - 512) <= 112 &&
				std::abs(chromaV - 512) <= 112 &&
				value >= companionStrongThreshold;
			const bool companionSoft =
				inCompanionZone &&
				std::abs(chromaU - 512) <= 144 &&
				std::abs(chromaV - 512) <= 144 &&
				value >= std::max<int>(
					static_cast<int>(blackCode) + 80,
					companionStrongThreshold * 45 / 100);

			const bool conservativeLiveCandidate =
				!hasOcrTextMask &&
				std::abs(chromaU - 512) <= 112 &&
				std::abs(chromaV - 512) <= 112 &&
				value >= softThreshold;
			const bool strongCandidate = hasOcrTextMask ?
				(confirmedStrong || companionStrong) :
				conservativeLiveCandidate;
			const bool softCandidate = hasOcrTextMask ?
				(confirmedSoft || companionSoft) :
				conservativeLiveCandidate;
			m_subtitleGlyphCandidateMask[index] =
				softCandidate ? 1 : 0;
			m_subtitleGlyphMask[index] = strongCandidate ? 1 : 0;
			if (strongCandidate)
				++hardSeedWords;
		}

	if (hardSeedWords < 16)
		return rejectCurrentFrame("no-strong-glyph-seeds");

	// Recover antialiased edges only when connected to a confirmed bright core.
	// This preserves smooth glyphs without copying isolated source-video pixels
	// over the already opaque destination panel.
	if (hasOcrTextMask)
	{
		for (int iteration = 0; iteration < 2; ++iteration)
		{
			m_subtitleGlyphGrowthMask = m_subtitleGlyphMask;
			for (int y = 1; y + 1 < sourceHeight; ++y)
				for (int x = 1; x + 1 < sourceWidth; ++x)
				{
					const size_t index =
						static_cast<size_t>(y) * sourceWidth + x;
					if (!m_subtitleGlyphCandidateMask[index] ||
						m_subtitleGlyphMask[index])
						continue;
					bool connected = false;
					for (int dy = -1; dy <= 1 && !connected; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
							if ((dx != 0 || dy != 0) &&
								m_subtitleGlyphMask[
									static_cast<size_t>(y + dy) *
										sourceWidth + x + dx])
							{
								connected = true;
								break;
							}
					if (connected)
						m_subtitleGlyphGrowthMask[index] = 1;
				}
			m_subtitleGlyphMask.swap(m_subtitleGlyphGrowthMask);
		}
	}

	// Remove connected shapes that cannot plausibly be glyphs. Bright roof
	// lines, UI borders and picture edges can be locally neutral and can fall
	// inside a text model's coarse region; copying them caused the visible
	// background bleed. Real caption glyphs remain compact even when italic,
	// accented, CJK, or musical-note characters are used.
	std::fill(m_subtitleGlyphGrowthMask.begin(),
		m_subtitleGlyphGrowthMask.end(), static_cast<uint8_t>(0));
	m_subtitleGlyphFlood.clear();
	m_subtitleGlyphFlood.reserve(
		std::min<size_t>(yWords, static_cast<size_t>(8192)));
	for (int startY = 0; startY < sourceHeight; ++startY)
		for (int startX = 0; startX < sourceWidth; ++startX)
		{
			const int start = startY * sourceWidth + startX;
			if (!m_subtitleGlyphMask[start] ||
				m_subtitleGlyphGrowthMask[start])
				continue;
			m_subtitleGlyphFlood.clear();
			m_subtitleGlyphFlood.push_back(start);
			m_subtitleGlyphGrowthMask[start] = 1;
			int left = startX;
			int right = startX + 1;
			int top = startY;
			int bottom = startY + 1;
			for (size_t index = 0;
				index < m_subtitleGlyphFlood.size(); ++index)
			{
				const int position = m_subtitleGlyphFlood[index];
				const int y = position / sourceWidth;
				const int x = position - y * sourceWidth;
				left = std::min(left, x);
				right = std::max(right, x + 1);
				top = std::min(top, y);
				bottom = std::max(bottom, y + 1);
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (dx == 0 && dy == 0)
							continue;
						const int nextX = x + dx;
						const int nextY = y + dy;
						if (nextX < 0 || nextX >= sourceWidth ||
							nextY < 0 || nextY >= sourceHeight)
							continue;
						const int next = nextY * sourceWidth + nextX;
						if (m_subtitleGlyphMask[next] &&
							!m_subtitleGlyphGrowthMask[next])
						{
							m_subtitleGlyphGrowthMask[next] = 1;
							m_subtitleGlyphFlood.push_back(next);
						}
					}
			}
			const int componentWidth = right - left;
			const int componentHeight = bottom - top;
			const size_t componentPixels = m_subtitleGlyphFlood.size();
			const bool plausible =
				componentPixels >= 2 &&
				componentHeight >= std::max(3, height / 720) &&
				componentHeight <= std::max(80, height / 18) &&
				componentWidth <= std::max(40, componentHeight * 12) &&
				componentPixels <= static_cast<size_t>(
					std::max(1600, height * width / 300));
			if (!plausible)
				for (const int position : m_subtitleGlyphFlood)
					m_subtitleGlyphMask[position] = 0;
		}

	// Locate actual text-line bands inside the broader analysis rectangle.
	// Selecting the lowest two bands rejects neutral, stable picture detail
	// above a one- or two-line subtitle without attempting to blend it away.
	struct GlyphBand
	{
		int top = 0;
		int bottom = 0;
		int left = 0;
		int right = 0;
		size_t pixels = 0;
	};
	std::vector<GlyphBand> glyphBands;
	GlyphBand currentBand;
	bool bandActive = false;
	int bandGap = 0;
	auto finishGlyphBand = [&]() {
		if (bandActive &&
			currentBand.bottom - currentBand.top >= 3 &&
			currentBand.bottom - currentBand.top <= std::max(24, height / 18))
			glyphBands.push_back(currentBand);
		currentBand = {};
		bandActive = false;
		bandGap = 0;
		};
	for (int y = 0; y < sourceHeight; ++y)
	{
		int count = 0;
		int left = sourceWidth;
		int right = 0;
		for (int x = 0; x < sourceWidth; ++x)
			if (m_subtitleGlyphMask[
				static_cast<size_t>(y) * sourceWidth + x])
			{
				++count;
				left = std::min(left, x);
				right = std::max(right, x + 1);
			}
		const int span = std::max(0, right - left);
		const bool textRow =
			count >= std::max(4, sourceWidth / 160) &&
			span >= std::max(12, sourceWidth / 24) &&
			span <= sourceWidth * 9 / 10 &&
			count * 24 >= span;
		if (textRow)
		{
			if (!bandActive)
			{
				currentBand = { y, y + 1, left, right,
					static_cast<size_t>(count) };
				bandActive = true;
			}
			else
			{
				currentBand.bottom = y + 1;
				currentBand.left = std::min(currentBand.left, left);
				currentBand.right = std::max(currentBand.right, right);
				currentBand.pixels += count;
			}
			bandGap = 0;
		}
		else if (bandActive && ++bandGap > 2)
			finishGlyphBand();
	}
	finishGlyphBand();
	if (glyphBands.empty())
		return rejectCurrentFrame("no-text-line-band");

	int selectedTop = 0;
	int selectedBottom = 0;
	int selectedLeft = sourceWidth;
	int selectedRight = 0;
	if (sourceAtTop)
	{
		const GlyphBand& highestBand = glyphBands.front();
		selectedTop = highestBand.top;
		selectedBottom = highestBand.bottom;
		selectedLeft = highestBand.left;
		selectedRight = highestBand.right;
		if (glyphBands.size() >= 2)
		{
			const GlyphBand& followingBand = glyphBands[1];
			const int gap = followingBand.top - highestBand.bottom;
			const int maximumLineGap =
				std::max(height / 40,
					2 * std::max(highestBand.bottom - highestBand.top,
						followingBand.bottom - followingBand.top));
			const int centerDifference = std::abs(
				(followingBand.left + followingBand.right) -
				(highestBand.left + highestBand.right)) / 2;
			if (gap >= 0 && gap <= maximumLineGap &&
				centerDifference <= width / 5)
			{
				selectedBottom = followingBand.bottom;
				selectedLeft = std::min(selectedLeft, followingBand.left);
				selectedRight = std::max(selectedRight, followingBand.right);
			}
		}
	}
	else
	{
		const GlyphBand& lowestBand = glyphBands.back();
		selectedTop = lowestBand.top;
		selectedBottom = lowestBand.bottom;
		selectedLeft = lowestBand.left;
		selectedRight = lowestBand.right;
		if (glyphBands.size() >= 2)
		{
			const GlyphBand& precedingBand =
				glyphBands[glyphBands.size() - 2];
			const int gap = lowestBand.top - precedingBand.bottom;
			const int maximumLineGap =
				std::max(height / 40,
					2 * std::max(lowestBand.bottom - lowestBand.top,
						precedingBand.bottom - precedingBand.top));
			const int centerDifference = std::abs(
				(precedingBand.left + precedingBand.right) -
				(lowestBand.left + lowestBand.right)) / 2;
			if (gap >= 0 && gap <= maximumLineGap &&
				centerDifference <= width / 5)
			{
				selectedTop = precedingBand.top;
				selectedLeft = std::min(selectedLeft, precedingBand.left);
				selectedRight = std::max(selectedRight, precedingBand.right);
			}
		}
	}

	// At least one selected text band must physically enter the black bar.
	// This is the defining condition for repositioning and rejects bright
	// picture detail, menus, and ordinary on-screen graphics above the edge.
	const int boundaryInSource = (sourceAtTop ?
		result.pictureTop : result.pictureBottom) - sourceTop;
	const bool pixelsCrossBoundary = sourceAtTop ?
		selectedTop < boundaryInSource :
		selectedBottom > boundaryInSource;
	const bool ocrConfirmsBoundary =
		result.active && (sourceAtTop ?
			result.source.top < result.pictureTop :
			result.source.bottom > result.pictureBottom);
	if (!pixelsCrossBoundary && !ocrConfirmsBoundary)
		return rejectCurrentFrame("line-does-not-cross-bar");

	// Row-density detection intentionally ignores the faint first/last glyph
	// rows. Restore a small vertical safety margin before extraction so caps,
	// accents, descenders, and antialiasing cannot be clipped.
	const int verticalGlyphPadding = std::max(4, height / 180);
	selectedTop = std::max(0, selectedTop - verticalGlyphPadding);
	selectedBottom =
		std::min(sourceHeight, selectedBottom + verticalGlyphPadding);

	// Find the central horizontal text cluster using a column projection.
	// This excludes isolated picture pixels that previously inflated the
	// grow-only panel almost to full width.
	struct HorizontalTextCluster
	{
		int left = 0;
		int right = 0;
	};
	std::vector<HorizontalTextCluster> horizontalClusters;
	const int selectedHeight = selectedBottom - selectedTop;
	const int minimumColumnPixels = std::max(2, selectedHeight / 16);
	const int maximumCharacterGap = std::max(12, selectedHeight);
	int clusterLeft = -1;
	int lastActiveColumn = -1;
	for (int x = 0; x < sourceWidth; ++x)
	{
		int pixels = 0;
		for (int y = selectedTop; y < selectedBottom; ++y)
			pixels += m_subtitleGlyphMask[
				static_cast<size_t>(y) * sourceWidth + x] != 0;
		if (pixels < minimumColumnPixels)
			continue;
		if (clusterLeft < 0)
			clusterLeft = x;
		else if (lastActiveColumn >= 0 &&
			x - lastActiveColumn > maximumCharacterGap)
		{
			horizontalClusters.push_back(
				{ clusterLeft, lastActiveColumn + 1 });
			clusterLeft = x;
		}
		lastActiveColumn = x;
	}
	if (clusterLeft >= 0)
		horizontalClusters.push_back(
			{ clusterLeft, lastActiveColumn + 1 });
	if (horizontalClusters.empty())
		return rejectCurrentFrame("no-horizontal-text-cluster");

	const int sourceCenter = sourceWidth / 2;
	const HorizontalTextCluster* selectedCluster = nullptr;
	int bestClusterScore = std::numeric_limits<int>::min();
	for (const HorizontalTextCluster& cluster : horizontalClusters)
	{
		const int clusterWidth = cluster.right - cluster.left;
		if (clusterWidth < std::max(12, sourceWidth / 80))
			continue;
		const int clusterCenter =
			cluster.left + clusterWidth / 2;
		const int score =
			clusterWidth - std::abs(clusterCenter - sourceCenter) / 2;
		if (score > bestClusterScore)
		{
			bestClusterScore = score;
			selectedCluster = &cluster;
		}
	}
	if (!selectedCluster)
		return rejectCurrentFrame("no-central-text-cluster");
	selectedLeft = selectedCluster->left;
	selectedRight = selectedCluster->right;

	// Once OCR has acquired a subtitle track, use its word-union rectangle as
	// a padded extraction guard. OCR is not used to redraw anything; it only
	// prevents neutral picture edges at the letterbox boundary from entering
	// the live pixel mask. Taking the full padded OCR height also restores cap
	// and accent rows that a row-density band can otherwise trim.
	const bool hasOcrGuard =
		result.active &&
		result.source.right > result.source.left &&
		result.source.bottom > result.source.top;
	if (hasOcrGuard)
	{
		const int ocrHeight = result.source.bottom - result.source.top;
		const int ocrPaddingX =
			std::max(width / 200, ocrHeight / 2);
		const int ocrPaddingY =
			std::max(height / 360, ocrHeight / 4);
		const int ocrLeft = std::max(0,
			result.source.left - sourceLeft - ocrPaddingX);
		const int ocrRight = std::min(sourceWidth,
			result.source.right - sourceLeft + ocrPaddingX);
		const int ocrTop = std::max(0,
			result.source.top - sourceTop - ocrPaddingY);
		const int ocrBottom = std::min(sourceHeight,
			result.source.bottom - sourceTop + ocrPaddingY);
		// Preserve a strongly detected companion line outside OCR's current
		// word union. This is what keeps Netflix's upper line stable when only
		// the lower boundary-crossing line is recognized on a given pass.
		selectedLeft = std::min(selectedLeft, ocrLeft);
		selectedRight = std::max(selectedRight, ocrRight);
		selectedTop = std::min(selectedTop, ocrTop);
		selectedBottom = std::max(selectedBottom, ocrBottom);
		if (selectedRight <= selectedLeft ||
			selectedBottom <= selectedTop)
			return rejectCurrentFrame("invalid-ocr-guard");
	}

	size_t glyphPixels = 0;
	int glyphLeft = sourceWidth;
	int glyphTop = sourceHeight;
	int glyphRight = 0;
	int glyphBottom = 0;
	const int selectedHorizontalPadding = std::max(4, width / 200);
	for (int y = 0; y < sourceHeight; ++y)
		for (int x = 0; x < sourceWidth; ++x)
		{
			const size_t index = static_cast<size_t>(y) * sourceWidth + x;
			const bool insideSelectedLines =
				y >= selectedTop && y < selectedBottom &&
				x >= std::max(0, selectedLeft - selectedHorizontalPadding) &&
				x < std::min(sourceWidth,
					selectedRight + selectedHorizontalPadding);
			// Keep only strong cores plus connected antialiased pixels. Detached
			// punctuation and music notes have their own bright cores, while
			// isolated source-video pixels remain excluded.
			m_subtitleGlyphMask[index] =
				insideSelectedLines && m_subtitleGlyphMask[index] ? 1 : 0;
			if (!m_subtitleGlyphMask[index])
				continue;
			++glyphPixels;
			glyphLeft = std::min(glyphLeft, x);
			glyphTop = std::min(glyphTop, y);
			glyphRight = std::max(glyphRight, x + 1);
			glyphBottom = std::max(glyphBottom, y + 1);
		}

	if (glyphPixels < std::max<size_t>(24, yWords / 2000) ||
		glyphPixels > yWords * 3 / 5 ||
		glyphRight <= glyphLeft || glyphBottom <= glyphTop)
		return rejectCurrentFrame("implausible-glyph-area");

	const int absoluteGlyphLeft = sourceLeft + glyphLeft;
	const int absoluteGlyphRight = sourceLeft + glyphRight;
	const int glyphCenter = absoluteGlyphLeft +
		(absoluteGlyphRight - absoluteGlyphLeft) / 2;
	if (glyphCenter < width * 3 / 10 || glyphCenter > width * 7 / 10 ||
		absoluteGlyphRight - absoluteGlyphLeft > width * 9 / 10)
		return rejectCurrentFrame("off-center-or-too-wide");

	int targetTop = sourceAtTop ?
		((result.pictureTop + lift - glyphTop) & ~1) :
		((result.pictureBottom - lift - glyphBottom) & ~1);
	const uint64_t currentSignatureGeneration =
		m_subtitleSignatureGeneration.load(std::memory_order_acquire);
	const bool sameTemporalRegion =
		m_subtitleCachedSignatureGeneration == currentSignatureGeneration &&
		m_subtitleCachedFrameWidth == width &&
		m_subtitleCachedFrameHeight == height &&
		m_subtitleCachedSourceLeft == sourceLeft &&
		m_subtitleCachedSourceTop == sourceTop &&
		m_subtitleCachedSourceWidth == sourceWidth &&
		m_subtitleCachedSourceHeight == sourceHeight;
	if (sameTemporalRegion && m_subtitleTemporalSampleCount != 0)
		targetTop = m_subtitleCachedTargetTop;
	if (targetTop + glyphTop < result.pictureTop ||
		targetTop + glyphBottom > result.pictureBottom)
		return rejectCurrentFrame("invalid-target");
	// Streaming apps commonly burn a translucent rounded rectangle or shadow
	// outside OCR's word bounds. Clear a font-height-relative safety margin so
	// the original overlay cannot remain as a halo after relocation.
	const int glyphHeight = glyphBottom - glyphTop;
	const int overlayClearPaddingX =
		(std::max(backgroundPaddingX, glyphHeight / 2) + 1) & ~1;
	const int overlayClearPaddingY =
		(std::max(backgroundPaddingY, glyphHeight / 3) + 1) & ~1;
	const int sourceClearLeft =
		(std::max(0, absoluteGlyphLeft - overlayClearPaddingX) & ~1);
	const int sourceClearRight =
		(std::min(width,
			absoluteGlyphRight + overlayClearPaddingX + 1) & ~1);
	const int sourceClearTop =
		(std::max(0,
			sourceTop + glyphTop - overlayClearPaddingY) & ~1);
	const int sourceClearBottom =
		(std::min(height,
			sourceTop + glyphBottom + overlayClearPaddingY + 1) & ~1);

	// The panel is always centered. Retain only its maximum half-width so left
	// and right padding remain identical and shorter captions cannot make it
	// breathe. Reset/restart clears the retained width.
	const int frameCenter = width / 2;
	const int currentHalfWidth = std::min(width * 9 / 20,
		std::max(frameCenter - absoluteGlyphLeft,
			absoluteGlyphRight - frameCenter) + overlayClearPaddingX);
	int retainedHalfWidth =
		m_subtitlePanelHalfWidthPixels.load(std::memory_order_acquire);
	while (result.active && retainedHalfWidth < currentHalfWidth &&
		!m_subtitlePanelHalfWidthPixels.compare_exchange_weak(
			retainedHalfWidth, currentHalfWidth,
			std::memory_order_acq_rel, std::memory_order_acquire))
	{
	}
	const int displayedHalfWidth =
		std::max(retainedHalfWidth, currentHalfWidth);
	const int backgroundLeft =
		(std::max(0, frameCenter - displayedHalfWidth) & ~1);
	const int backgroundRight =
		(std::min(width, frameCenter + displayedHalfWidth + 1) & ~1);
	const int currentPanelHeight = sourceAtTop ? 0 :
		std::max(0, result.pictureBottom -
			(targetTop + glyphTop - backgroundPaddingY));
	int retainedPanelHeight =
		m_subtitlePanelHeightPixels.load(std::memory_order_acquire);
	while (!sourceAtTop && result.active &&
		retainedPanelHeight < currentPanelHeight &&
		!m_subtitlePanelHeightPixels.compare_exchange_weak(
			retainedPanelHeight, currentPanelHeight,
			std::memory_order_acq_rel, std::memory_order_acquire))
	{
	}
	const int displayedPanelHeight =
		std::max(retainedPanelHeight, currentPanelHeight);
	const int backgroundTop = sourceAtTop ?
		(std::max(0, result.pictureTop) & ~1) :
		(std::max(0,
			result.pictureBottom - displayedPanelHeight) & ~1);
	const int backgroundBottom = sourceAtTop ?
		(std::min(height,
			targetTop + glyphBottom + backgroundPaddingY + 1) & ~1) :
		(std::min(height, result.pictureBottom + 1) & ~1);

	const uint16_t blackY = static_cast<uint16_t>(
		std::min<int>(1023, blackCode) << 6);
	uint16_t panelCode = std::min<uint16_t>(1023, blackCode);
	if (m_sceneAwareTimingCorrection.load(std::memory_order_acquire) &&
		m_subtitlePanelLumaInitialized.load(std::memory_order_acquire))
	{
		const uint16_t sceneAverage =
			m_subtitleSceneAverageLumaCode.load(std::memory_order_acquire);
		const int aboveBlack = std::max(0,
			static_cast<int>(sceneAverage) - static_cast<int>(blackCode));
		// Preserve an opaque black/gray panel rather than matching APL
		// literally. Twenty percent of the scene-to-black distance gives the
		// Envy-style adaptation without turning the panel into a bright block.
		panelCode = static_cast<uint16_t>(std::min<int>(
			std::min<int>(1023, static_cast<int>(blackCode) + 192),
			static_cast<int>(blackCode) + aboveBlack / 5));
	}
	const uint16_t panelY = static_cast<uint16_t>(panelCode << 6);
	const uint16_t neutralUV = static_cast<uint16_t>(512u << 6);
	auto clearRectangle = [&](int left, int top, int right, int bottom,
		uint16_t fillY) {
		left = std::max(0, std::min(width, left)) & ~1;
		right = std::max(left, std::min(width, right)) & ~1;
		top = std::max(0, std::min(height, top)) & ~1;
		bottom = std::max(top, std::min(height, bottom)) & ~1;
		for (int y = top; y < bottom; ++y)
			std::fill_n(yPlane +
				static_cast<size_t>(y) * width + left,
				right - left, fillY);
		for (int y = top / 2; y < bottom / 2; ++y)
			for (int x = left; x < right; x += 2)
			{
				const size_t uv =
					static_cast<size_t>(y) * width + x;
				uvPlane[uv] = neutralUV;
				uvPlane[uv + 1] = neutralUV;
			}
		};

	// Build a coherent glyph snapshot from recurring pixels, not a permanent
	// union. A subtitle is stationary across adjacent frames while picture
	// detail moves; accepting a two-of-three temporal vote keeps glyph strokes
	// and rejects the background fragments visible in the recorded test.
	const uint64_t signatureGeneration = currentSignatureGeneration;
	const bool sameCachedGeometry =
		m_subtitleCachedSignatureGeneration == signatureGeneration &&
		m_subtitleCachedFrameWidth == width &&
		m_subtitleCachedFrameHeight == height &&
		m_subtitleCachedSourceLeft == sourceLeft &&
		m_subtitleCachedSourceTop == sourceTop &&
		m_subtitleCachedSourceWidth == sourceWidth &&
		m_subtitleCachedSourceHeight == sourceHeight &&
		m_subtitleCachedGlyphMask.size() == yWords &&
		m_subtitleCachedGlyphY.size() == yWords &&
		m_subtitlePreviousGlyphMask.size() == yWords &&
		m_subtitlePrevious2GlyphMask.size() == yWords;
	if (!sameCachedGeometry)
	{
		m_subtitleCachedGlyphMask.assign(yWords, 0);
		m_subtitleCachedGlyphY.assign(yWords, 0);
		m_subtitlePreviousGlyphMask.assign(
			m_subtitleGlyphMask.begin(), m_subtitleGlyphMask.end());
		m_subtitlePrevious2GlyphMask.assign(yWords, 0);
		m_subtitleCachedFrameWidth = width;
		m_subtitleCachedFrameHeight = height;
		m_subtitleCachedSourceLeft = sourceLeft;
		m_subtitleCachedSourceTop = sourceTop;
		m_subtitleCachedSourceWidth = sourceWidth;
		m_subtitleCachedSourceHeight = sourceHeight;
		m_subtitleCachedTargetTop = targetTop;
		m_subtitleCachedSignatureGeneration = signatureGeneration;
		m_subtitleCachedGlyphCount = 0;
		m_subtitleTemporalSampleCount = 1;
		return rejectCurrentFrame("temporal-warmup");
	}

	const bool hadStableGlyphs = m_subtitleCachedGlyphCount != 0;
	if (hadStableGlyphs)
	{
		size_t overlappingGlyphs = 0;
		for (size_t index = 0; index < yWords; ++index)
			overlappingGlyphs += m_subtitleGlyphMask[index] &&
				m_subtitleCachedGlyphMask[index];
		const size_t smallerGlyphSet =
			std::min(glyphPixels, m_subtitleCachedGlyphCount);
		if (smallerGlyphSet >= 24 &&
			overlappingGlyphs * 5 < smallerGlyphSet)
		{
			// The fast bar signature can miss a changed line that is mostly
			// inside the picture. Do not blend two captions together: warm a
			// fresh temporal state and let the native new caption pass for one
			// frame while it is confirmed.
			m_subtitleCachedGlyphMask.assign(yWords, 0);
			m_subtitleCachedGlyphY.assign(yWords, 0);
			m_subtitlePreviousGlyphMask.assign(
				m_subtitleGlyphMask.begin(), m_subtitleGlyphMask.end());
			m_subtitlePrevious2GlyphMask.assign(yWords, 0);
			m_subtitleCachedGlyphCount = 0;
			m_subtitleTemporalSampleCount = 1;
			m_subtitlePanelHalfWidthPixels.store(
				0, std::memory_order_release);
			m_subtitlePanelHeightPixels.store(
				0, std::memory_order_release);
			return rejectCurrentFrame("temporal-caption-change");
		}
	}
	size_t stableGlyphCount = 0;
	for (size_t index = 0; index < yWords; ++index)
	{
		const int observations =
			(m_subtitleGlyphMask[index] ? 1 : 0) +
			(m_subtitlePreviousGlyphMask[index] ? 1 : 0) +
			(m_subtitleTemporalSampleCount >= 2 &&
				m_subtitlePrevious2GlyphMask[index] ? 1 : 0);
		const bool keepStablePixel =
			observations >= 2 ||
			(m_subtitleCachedGlyphMask[index] && observations >= 1);
		m_subtitleCachedGlyphMask[index] =
			keepStablePixel ? 1 : 0;
		if (!keepStablePixel)
			continue;
		++stableGlyphCount;
		if (m_subtitleGlyphMask[index])
			m_subtitleCachedGlyphY[index] = m_subtitleScratchY[index];
	}
	m_subtitlePrevious2GlyphMask = m_subtitlePreviousGlyphMask;
	m_subtitlePreviousGlyphMask.assign(
		m_subtitleGlyphMask.begin(), m_subtitleGlyphMask.end());
	m_subtitleTemporalSampleCount = std::min<uint8_t>(
		3, static_cast<uint8_t>(m_subtitleTemporalSampleCount + 1));
	m_subtitleCachedGlyphCount = stableGlyphCount;
	if (stableGlyphCount == 0)
		return rejectCurrentFrame("temporal-no-consensus");

	m_subtitleCachedSourceClearLeft =
		!hadStableGlyphs ? sourceClearLeft :
			std::min(m_subtitleCachedSourceClearLeft, sourceClearLeft);
	m_subtitleCachedSourceClearTop =
		!hadStableGlyphs ? sourceClearTop :
			std::min(m_subtitleCachedSourceClearTop, sourceClearTop);
	m_subtitleCachedSourceClearRight =
		!hadStableGlyphs ? sourceClearRight :
			std::max(m_subtitleCachedSourceClearRight, sourceClearRight);
	m_subtitleCachedSourceClearBottom =
		!hadStableGlyphs ? sourceClearBottom :
			std::max(m_subtitleCachedSourceClearBottom, sourceClearBottom);
	m_subtitleCachedBackgroundLeft =
		!hadStableGlyphs ? backgroundLeft :
			std::min(m_subtitleCachedBackgroundLeft, backgroundLeft);
	m_subtitleCachedBackgroundTop =
		!hadStableGlyphs ? backgroundTop :
			std::min(m_subtitleCachedBackgroundTop, backgroundTop);
	m_subtitleCachedBackgroundRight =
		!hadStableGlyphs ? backgroundRight :
			std::max(m_subtitleCachedBackgroundRight, backgroundRight);
	m_subtitleCachedBackgroundBottom =
		!hadStableGlyphs ? backgroundBottom :
			std::max(m_subtitleCachedBackgroundBottom, backgroundBottom);
	m_subtitleCachedPanelY = panelY;
	m_subtitleCachedSceneEventId =
		m_subtitleSceneEventId.load(std::memory_order_acquire);

	// Keep this deliberately simple and deterministic: both the old subtitle
	// area and its new location are completely opaque rectangles. The source
	// is video black; the destination is scene-latched neutral black/gray when
	// Scene Detect is enabled, and video black otherwise.
	// The entire black bar is safe to clear and doing so guarantees that words
	// outside an imperfect OCR box cannot survive underneath the relocated
	// caption. The destination is drawn afterward and is always the top layer.
	if (sourceAtTop)
		clearRectangle(0, 0, width, result.pictureTop, blackY);
	else
		clearRectangle(0, result.pictureBottom, width, height, blackY);
	clearRectangle(sourceClearLeft, sourceClearTop,
		sourceClearRight, sourceClearBottom, blackY);
	clearRectangle(backgroundLeft, backgroundTop,
		backgroundRight, backgroundBottom, panelY);

	for (int y = 0; y < sourceHeight; ++y)
		for (int x = 0; x < sourceWidth; ++x)
		{
			const size_t source =
				static_cast<size_t>(y) * sourceWidth + x;
			if (m_subtitleCachedGlyphMask[source])
				yPlane[static_cast<size_t>(targetTop + y) * width +
					sourceLeft + x] = m_subtitleCachedGlyphY[source];
		}
	// Destination chroma remains neutral from clearRectangle(). Only glyph
	// luma is copied, preventing colored picture pixels or chroma fringes from
	// leaking into the relocated subtitle.
	return true;
}


bool CBufferedLiveSourceVideoOutputPin::RelocateSubtitleInP010(
	IMediaSample* sample,
	uint64_t frameNumber)
{
	if (m_subtitleRepositionMode.load(std::memory_order_acquire) ==
			SubtitleRepositionMode::DISABLED ||
		!sample ||
		!IsEqualGUID(m_mediaType.subtype, MEDIASUBTYPE_P010) || !m_mediaType.pbFormat)
		return false;

	LONG width = 0;
	LONG signedHeight = 0;
	uint16_t nominalBlackCode = 64;
	if (IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo2) &&
		m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER2))
	{
		const VIDEOINFOHEADER2* info =
			reinterpret_cast<const VIDEOINFOHEADER2*>(m_mediaType.pbFormat);
		width = info->bmiHeader.biWidth;
		signedHeight = info->bmiHeader.biHeight;
		const DXVA_ExtendedFormat* colorimetry =
			reinterpret_cast<const DXVA_ExtendedFormat*>(&info->dwControlFlags);
		if (colorimetry->NominalRange == DXVA_NominalRange_0_255)
			nominalBlackCode = 0;
		else if (colorimetry->NominalRange == DXVA_NominalRange_48_208)
			nominalBlackCode = 192;
	}
	else if (IsEqualGUID(m_mediaType.formattype, FORMAT_VideoInfo) &&
		m_mediaType.cbFormat >= sizeof(VIDEOINFOHEADER))
	{
		const VIDEOINFOHEADER* info =
			reinterpret_cast<const VIDEOINFOHEADER*>(m_mediaType.pbFormat);
		width = info->bmiHeader.biWidth;
		signedHeight = info->bmiHeader.biHeight;
	}
	if (width <= 0 || signedHeight == 0)
		return false;

	const int frameWidth = static_cast<int>(width);
	const int frameHeight =
		static_cast<int>(signedHeight > 0 ? signedHeight : -signedHeight);
	const size_t planeBytes =
		static_cast<size_t>(frameWidth) * frameHeight * sizeof(uint16_t);
	if (frameWidth < 320 || frameHeight < 240 ||
		sample->GetActualDataLength() < static_cast<LONG>(planeBytes * 3 / 2))
		return false;

	BYTE* bytes = nullptr;
	if (FAILED(sample->GetPointer(&bytes)) || !bytes)
		return false;
	uint16_t* yPlane = reinterpret_cast<uint16_t*>(bytes);
	uint16_t* uvPlane = reinterpret_cast<uint16_t*>(bytes + planeBytes);

	SubmitSubtitleAnalysis(yPlane, frameWidth, frameHeight,
		frameNumber, nominalBlackCode);

	SubtitleAnalysisResult result;
	{
		std::lock_guard<std::mutex> lock(m_subtitleAnalysisMutex);
		result = m_subtitleLatestResult;
	}
	const uint64_t now = GetTickCount64();
	if (!result.barStable ||
		result.generation !=
			m_subtitleAnalysisGeneration.load(std::memory_order_acquire) ||
		result.fullWidth != frameWidth || result.fullHeight != frameHeight ||
		now < result.producedTick || now - result.producedTick > 250)
		return false;

	if (!CompositeTrackedSubtitle(yPlane, uvPlane, frameWidth, frameHeight,
		result.blackCode, result))
		return false;

	const uint64_t relocationCount =
		m_subtitleRelocationCount.fetch_add(1, std::memory_order_relaxed) + 1;
	if (relocationCount <= 5 || relocationCount % 120 == 0)
	{
		DebugLog::Log(
			"SUBTITLE REPOSITION: frame=%llu relocation=%llu confidence=%u "
			"source=(%d,%d)-(%d,%d) edge=%s picture=%d-%d",
			frameNumber, relocationCount, result.confidence,
			result.source.left, result.source.top,
			result.source.right, result.source.bottom,
			result.sourceAtTop ? "TOP" : "BOTTOM",
			result.pictureTop, result.pictureBottom);
	}
	return true;
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

	const size_t configuredStartup =
		m_configuredStartupPrerollFrames.load(std::memory_order_acquire);
	if (configuredStartup > 0)
	{
		const size_t capacity =
			m_frameQueueMaxSize.load(std::memory_order_acquire);
		if (capacity > 0)
			frames = std::min(configuredStartup, capacity);
	}

	const size_t deliveryReserve = GetDeliveryReserve();
	const size_t normalTarget = std::max(frames, deliveryReserve);
	const uint64_t currentEpoch = m_queueEpoch.load(std::memory_order_acquire);
	const uint64_t primeEpoch =
		m_primeQueueEpoch.load(std::memory_order_acquire);
	const size_t primeTarget =
		m_primeTargetFrames.load(std::memory_order_acquire);
	const size_t queueCapacity =
		m_frameQueueMaxSize.load(std::memory_order_acquire);
	const size_t allocatorBuffers = static_cast<size_t>(std::max<LONG>(
		0, GetNegotiatedAllocatorBufferCount()));
	const size_t capacity = DirectShowEpochPrimePolicy::PrimeTarget(
		queueCapacity, allocatorBuffers);
	const size_t effectiveTarget =
		DirectShowEpochPrimePolicy::ResolveBufferingTarget(
			normalTarget, currentEpoch, primeEpoch, primeTarget, capacity);

	// Log the buffering target periodically
	static double lastLoggedFps = 0.0;
	if (abs(fps - lastLoggedFps) > 1.0)
	{
		DebugLog::Log("GetBufferingTarget(): fps=%.2f, nominalTarget=%zu, effectiveTarget=%zu deliveryReserve=%zu configuredStartup=%zu primeEpoch=%llu currentEpoch=%llu primeTarget=%zu", fps, nominalTarget, effectiveTarget, deliveryReserve, configuredStartup, static_cast<unsigned long long>(primeEpoch), static_cast<unsigned long long>(currentEpoch), primeTarget);
		lastLoggedFps = fps;
	}

	return effectiveTarget;
}


size_t CBufferedLiveSourceVideoOutputPin::GetDeliveryReserve() const
{
	const bool targetConfigured = IsSteadyQueueTargetConfigured();
	const size_t configuredTarget = GetConfiguredSteadyQueueTarget();
	if (targetConfigured)
		return configuredTarget > 0 ? configuredTarget - 1 : 0;
	const size_t readinessReserve =
		m_outputReadinessDeliveryReserve.load(std::memory_order_acquire);
	return readinessReserve > 0 ? readinessReserve : 1;
}


bool CBufferedLiveSourceVideoOutputPin::IsSteadyQueueTargetConfigured() const
{
	return m_configuredSteadyReserveExplicit.load(std::memory_order_acquire);
}


size_t CBufferedLiveSourceVideoOutputPin::GetConfiguredSteadyQueueTarget() const
{
	const size_t configuredTarget =
		m_configuredSteadyReserveFrames.load(std::memory_order_acquire);
	if (configuredTarget == 0)
		return 0;
	const size_t capacity =
		m_frameQueueMaxSize.load(std::memory_order_acquire);
	return capacity > 0 ? std::min(configuredTarget, capacity) : 0;
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
	RequestCoordinatedReset("bad-clock-smart-timestamp");
}

size_t CBufferedLiveSourceVideoOutputPin::GetConvertedQueueSize()
{
	CAutoLock convLock(&m_convertedQueueLock);
	return m_processedFrameQueue.Size();
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
