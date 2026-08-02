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
#include <DebugLog.h>
#include <microsoft_directshow/live_source_filter/CLiveSource.h>
#include <microsoft_directshow/live_source_filter/ALiveSourceVideoOutputPin.h>
#include <microsoft_directshow/DIrectShowTranslations.h>
#include <microsoft_directshow/MadVRRuntimeInterfaces.h>

#include "DirectShowVideoRenderer.h"


DirectShowVideoRenderer::DirectShowVideoRenderer(
	IRendererCallback& callback,
	HWND videoHwnd,
	HWND eventHwnd,
	UINT eventMsg,
	ITimingClock* timingClock,
	DirectShowStartStopTimeMethod timestamp,
	bool useFrameQueue,
	size_t frameQueueMaxSize,
	VideoConversionOverride videoConversionOverride):
	m_callback(callback),
	m_videoHwnd(videoHwnd),
	m_eventHwnd(eventHwnd),
	m_eventMsg(eventMsg),
	m_timingClock(timingClock),
	m_timestamp(timestamp),
	m_useFrameQueue(useFrameQueue),
	m_frameQueueMaxSize(frameQueueMaxSize),
	m_videoConversionOverride(videoConversionOverride)
{
	if (!videoHwnd)
		throw std::runtime_error("Invalid videoHwnd");
	if (!eventHwnd)
		throw std::runtime_error("Invalid eventHwnd");
	if (!eventMsg)
		throw std::runtime_error("Invalid eventMsg");

	if (timingClock && timingClock->TimingClockTicksPerSecond() < 1000LL)
		throw std::runtime_error("TimingClock needs resolution of at least millisecond level");

	if (!useFrameQueue && timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK)
		throw std::runtime_error("No queue cannot be used with clock-clock, pick another mode and restart");

	// RATIONAL_RATIONAL mode timing clock will be created when video state is available
	// We need the exact rational frame rate from DisplayMode to create the perfect mathematical clock

	ZeroMemory(&m_pmt, sizeof(AM_MEDIA_TYPE));
}


DirectShowVideoRenderer::~DirectShowVideoRenderer()
{
	Retire();
}


void DirectShowVideoRenderer::Retire() noexcept
{
	if (m_retired.exchange(true, std::memory_order_acq_rel))
		return;
	DebugLog::Log(
		"DirectShow renderer retirement started: worker_thread=%lu graph_complete=%d",
		GetCurrentThreadId(),
		m_graphTeardownComplete.load(std::memory_order_acquire) ? 1 : 0);
	if (m_graphTeardownComplete.load(std::memory_order_acquire))
	{
		// STOPPED is terminal, but a final DirectShow notification can enqueue
		// event-drain work before the UI releases its lifetime pin. Discard
		// such post-teardown work and only join the already-clean owner.
		m_graphExecutor.CancelPendingAndShutdown({});
		DebugLog::Log(
			"DirectShow renderer retirement completed: worker_thread=%lu mode=join-only",
			GetCurrentThreadId());
		return;
	}
	m_graphExecutor.CancelPendingAndShutdown([this]()
		{
			GraphTeardownNoThrow();
		});
	DebugLog::Log(
		"DirectShow renderer retirement completed: worker_thread=%lu mode=forced-cleanup",
		GetCurrentThreadId());
}


bool DirectShowVideoRenderer::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("null video state is invalid");

	if (!IsGraphThread())
	{
		{
			std::lock_guard<std::mutex> lock(m_videoStateAdmissionMutex);
			if (m_admissionVideoState &&
				(videoState->valid == false ||
					videoState->colorspace !=
						m_admissionVideoState->colorspace ||
					videoState->eotf != m_admissionVideoState->eotf ||
					*(videoState->displayMode) !=
						*(m_admissionVideoState->displayMode) ||
					videoState->videoFrameEncoding !=
						m_admissionVideoState->videoFrameEncoding))
				return false;
			m_admissionVideoState = videoState;
		}
		const VideoStateComPtr acceptedState = videoState;
		PostCoalescedGraphCommand(GRAPH_COMMAND_VIDEO_STATE,
			[this, acceptedState]()
			{
				// The graph contract is immutable for this renderer
				// generation. Compatible runtime HDR changes are applied by
				// the owner-side HDR command without replacing the shared
				// pointer read by the capture path.
				if (!m_videoState)
					m_videoState = acceptedState;
			});
		return true;
	}

	if (m_videoState)
	{
		if (videoState->valid == false ||
			videoState->colorspace != m_videoState->colorspace ||
			videoState->eotf != m_videoState->eotf ||
			*(videoState->displayMode) != *(m_videoState->displayMode) ||
			videoState->videoFrameEncoding != m_videoState->videoFrameEncoding)
			return false;
	}
	m_videoState = videoState;

	// All good, continue
	return true;
}


void DirectShowVideoRenderer::OnVideoFrame(VideoFrame& videoFrame)
{
	// Called from some unknown thread, but with promise that Start() has completed

	assert(m_state.load(std::memory_order_acquire) ==
		RendererState::RENDERSTATE_RENDERING);
	assert(m_videoState);
	assert(videoFrame.GetTimingTimestamp() > 0);

	const timingclocktime_t frameTime = videoFrame.GetTimingTimestamp();
	const LiveFrameCounterDecision counterDecision =
		m_captureFrameCounterTracker.Observe(videoFrame.GetCounter());
	ALiveSourceVideoOutputPin* outputPin =
		m_liveSource ? m_liveSource->GetVideoOutputPin() : nullptr;
	RendererLivenessSnapshot downstreamSnapshot;
	const bool hasDownstreamSnapshot = outputPin &&
		outputPin->GetLivenessSnapshot(downstreamSnapshot) &&
		downstreamSnapshot.supported;
	const ULONGLONG now = GetTickCount64();
	const bool hasCurrentEpochDownstreamDelivery =
		hasDownstreamSnapshot &&
		HasCurrentEpochDownstreamDelivery(downstreamSnapshot);
	const bool recentDownstreamDelivery =
		hasCurrentEpochDownstreamDelivery &&
		downstreamSnapshot.lastDeliverySuccessTick <= now &&
		now - downstreamSnapshot.lastDeliverySuccessTick <= 500;
	const bool downstreamBelowCapacity =
		!hasDownstreamSnapshot || downstreamSnapshot.queueCapacity == 0 ||
		(downstreamSnapshot.rawQueueDepth < downstreamSnapshot.queueCapacity &&
			downstreamSnapshot.convertedQueueDepth <
				downstreamSnapshot.queueCapacity);
	const bool downstreamDeliveryBlocked =
		hasDownstreamSnapshot && downstreamSnapshot.deliveryInProgress &&
		downstreamSnapshot.lastDeliveryStartTick != 0 &&
		downstreamSnapshot.lastDeliveryStartTick <= now &&
		now - downstreamSnapshot.lastDeliveryStartTick > 500;
	const bool downstreamHealthy = !hasDownstreamSnapshot ||
		(hasCurrentEpochDownstreamDelivery && downstreamBelowCapacity &&
			(counterDecision.IsDiscontinuity() || recentDownstreamDelivery) &&
			!downstreamDeliveryBlocked);
	const LiveSourceGapRecoveryDecision gapRecovery =
		m_sourceGapRecoveryPolicy.Observe(
			counterDecision,
			m_videoState->displayMode->TimeScale(),
			m_videoState->displayMode->FrameDuration(),
			downstreamHealthy);
	if (counterDecision.IsDiscontinuity())
	{
		// Receiving a new live frame proves capture resumed. Rebase cadence and
		// carry an explicit source discontinuity through the delivery sequencer.
		// A material gap additionally asks the serialized owner to re-prime the
		// opaque madVR queues; never control the graph from this callback.
		videoFrame.SetSourceDiscontinuity(true);
		ResetPPMMeasurement();
		bool resetPublished = false;
		if (gapRecovery.action ==
			LiveSourceGapRecoveryAction::RequestGraphReprime)
		{
			resetPublished = outputPin &&
				outputPin->RequestSourceGapGraphReprime();
		}
		const char* action = "continue-local";
		if (gapRecovery.action ==
			LiveSourceGapRecoveryAction::RequestGraphReprime)
		{
			action = resetPublished ?
				"request-graph-reprime" : "coalesce-graph-reprime";
		}
		else if (gapRecovery.action ==
			LiveSourceGapRecoveryAction::SuppressedUntilHealthy)
		{
			action = "suppress-until-healthy";
		}
		DebugLog::Log(
			"DirectShow source counter discontinuity: transition=%s "
			"previous=%llu current=%llu missing=%llu "
			"material_threshold=%llu action=%s ppm=rebaseline "
			"graph_reset=%d healthy=%llu/%llu downstream_healthy=%d "
			"downstream_snapshot=%d downstream_epoch=%llu "
			"downstream_success=%llu queue=%zu/%zu+%zu/%zu",
			ToString(counterDecision.transition),
			static_cast<unsigned long long>(counterDecision.previous),
			static_cast<unsigned long long>(counterDecision.current),
			static_cast<unsigned long long>(counterDecision.missingFrames),
			static_cast<unsigned long long>(gapRecovery.materialGapFrames),
			action, resetPublished ? 1 : 0,
			static_cast<unsigned long long>(
				gapRecovery.healthyIntervalsObserved),
			static_cast<unsigned long long>(
				gapRecovery.healthyIntervalsRequired),
			downstreamHealthy ? 1 : 0,
			hasDownstreamSnapshot ? 1 : 0,
			static_cast<unsigned long long>(
				downstreamSnapshot.queueEpoch),
			static_cast<unsigned long long>(
				downstreamSnapshot.currentEpochDeliverySuccessCount),
			downstreamSnapshot.rawQueueDepth,
			downstreamSnapshot.queueCapacity,
			downstreamSnapshot.convertedQueueDepth,
			downstreamSnapshot.queueCapacity);
	}

	// Update PPM measurement with each frame
	UpdatePPMMeasurement(frameTime);
	MaybeScheduleMadVRRuntimeTelemetry();

	// Get delay until now once in a while
	if (m_frameCounter % 20 == 0)
	{
		const timingclocktime_t clockTime = m_timingClock->TimingClockNow();

		m_frameLatencyEntry = TimingClockDiffMs(frameTime, clockTime, m_timingClock->TimingClockTicksPerSecond());
		
		// Diagnostic: Check DirectShow clock synchronization (every 10 seconds)
		if (m_frameCounter % 600 == 0 && m_referenceClock)
		{
			REFERENCE_TIME dsClockTime = 0;
			if (SUCCEEDED(m_referenceClock->GetTime(&dsClockTime)))
			{
				// HIGH-PRECISION CONVERSION: Use the same banker's rounding as everywhere else
				// Convert hardware clock to DirectShow time for comparison
				const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();
				const REFERENCE_TIME hardwareAsDsTime = ((frameTime * 10000000LL) + (ticksPerSecond / 2)) / ticksPerSecond;
				const REFERENCE_TIME clockDiff = dsClockTime - hardwareAsDsTime;
				const double clockDiffMs = clockDiff / 10000.0;
				
				DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer: Clock sync check - DS clock: %I64d, HW clock: %I64d, diff: %.2f ms"),
					dsClockTime, hardwareAsDsTime, clockDiffMs));
			}
		}
	}

	const HRESULT deliveryResult = m_liveSource->OnVideoFrame(videoFrame);
	if (FAILED(deliveryResult))
	{
		DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer::OnVideoFrame(): Failed to deliver frame #%I64u"), m_frameCounter));
	}
	else if (!m_useFrameQueue && deliveryResult == S_OK)
	{
		m_unbufferedDeliverySuccessCount.fetch_add(
			1, std::memory_order_acq_rel);
	}

	++m_frameCounter;
}


HRESULT DirectShowVideoRenderer::OnWindowsEvent(LONG_PTR, LONG_PTR)
{
	// A previous owner-side drain can have published a state transition. Only
	// deliver it here, on the window/UI thread.
	PublishPendingStateCallback();

	const bool accepted = PostCoalescedGraphCommand(
		GRAPH_COMMAND_EVENT_DRAIN, [this]()
		{
			AssertGraphThread();
			OnWindowsEventOnGraphThread();

			// SetState deliberately does not call UI code from the graph owner.
			// Wake the HWND so the next UI-side entry publishes the completion.
			bool hasCompletion = false;
			{
				std::lock_guard<std::mutex> lock(m_completionMutex);
				hasCompletion = !m_pendingStateCompletions.empty();
			}
			if (hasCompletion)
			{
				WakeForOwnerCompletion();
			}
		});
	return accepted ? S_OK : VFW_E_WRONG_STATE;
}


HRESULT DirectShowVideoRenderer::OnWindowsEventOnGraphThread()
{
	// ! Do not tear down graph here

	if (!m_pEvent)
		throw std::runtime_error("No pevent");

	long evCode = 0;
	LONG_PTR param1 = 0, param2 = 0;

	HRESULT hr = S_OK;

	// Get the events from the queue.
	while (SUCCEEDED(m_pEvent->GetEvent(&evCode, &param1, &param2, 0)))
	{
		// Invoke the callback.
		OnGraphEvent(evCode, param1, param2);

		// Free the event data.
		hr = m_pEvent->FreeEventParams(evCode, param1, param2);
		if (FAILED(hr))
		{
			break;
		}
	}

	return hr;
}


void DirectShowVideoRenderer::Build()
{
	if (IsGraphThread())
	{
		GraphBuild();
		return;
	}
	m_graphExecutor.Post([this]()
		{
			try
			{
				GraphBuild();
			}
			catch (const std::exception& error)
			{
				DebugLog::Log(
					"DirectShow graph build failed asynchronously: %s",
					error.what());
				GraphTeardownNoThrow();
				SetState(RendererState::RENDERSTATE_FAILED);
			}
			WakeForOwnerCompletion();
		});
}


void DirectShowVideoRenderer::Start()
{
	m_unbufferedDeliverySuccessCount.store(0, std::memory_order_release);
	m_resetReadyForReveal.store(false, std::memory_order_release);
	if (IsGraphThread())
	{
		GraphRun();
		m_resetReadyForReveal.store(true, std::memory_order_release);
		return;
	}
	m_graphExecutor.Post([this]()
		{
			if (m_state.load(std::memory_order_acquire) !=
				RendererState::RENDERSTATE_READY)
				return;
			try
			{
				GraphRun();
				m_resetReadyForReveal.store(true, std::memory_order_release);
			}
			catch (const std::exception& error)
			{
				DebugLog::Log(
					"DirectShow graph start failed asynchronously: %s",
					error.what());
				GraphTeardownNoThrow();
				SetState(RendererState::RENDERSTATE_FAILED);
			}
			WakeForOwnerCompletion();
		});
}


void DirectShowVideoRenderer::Stop()
{
	StopWithIngressDrain({});
}


void DirectShowVideoRenderer::StopWithIngressDrain(
	const std::function<void()>& drainAfterGraphStop)
{
	if (IsGraphThread())
	{
		GraphStop();
		if (drainAfterGraphStop)
			drainAfterGraphStop();
		GraphTeardownNoThrow();
		if (m_graphTeardownComplete.load(std::memory_order_acquire))
			SetState(RendererState::RENDERSTATE_STOPPED);
		else
			SetState(RendererState::RENDERSTATE_FAILED);
		return;
	}
	m_graphExecutor.PostWithCompletion([this, drainAfterGraphStop]()
		{
			try
			{
				GraphStop();
			}
			catch (const std::exception& error)
			{
				DebugLog::Log(
					"DirectShow graph stop failed asynchronously: %s",
					error.what());
			}
			if (drainAfterGraphStop)
				drainAfterGraphStop();
			GraphTeardownNoThrow();
			if (m_graphTeardownComplete.load(std::memory_order_acquire))
				SetState(RendererState::RENDERSTATE_STOPPED);
			else
				SetState(RendererState::RENDERSTATE_FAILED);
		}, [eventHwnd = m_eventHwnd, eventMsg = m_eventMsg]()
		{
			PostMessage(eventHwnd, eventMsg, 0, 0);
		});
}


void DirectShowVideoRenderer::Reset()
{
	ResetWithIngressDrain({});
}


void DirectShowVideoRenderer::ResetWithIngressDrain(
	const std::function<void()>& drainAfterGraphStop)
{
	if (!IsGraphThread())
	{
		InvokeOnGraphThread([this, drainAfterGraphStop]()
			{
				ResetWithIngressDrain(drainAfterGraphStop);
			});
		return;
	}
	AssertGraphThread();
	m_unbufferedDeliverySuccessCount.store(0, std::memory_order_release);
	m_resetReadyForReveal.store(false, std::memory_order_release);
	DebugLog::Log("DirectShowVideoRenderer::Reset() called, m_liveSource=%p", m_liveSource);
	
	if (!m_liveSource)
	{
		DebugLog::Log("DirectShowVideoRenderer::Reset() - m_liveSource is NULL, returning");
		return;
	}
	RefreshDownstreamPrimeTarget();
	
	// This is an in-place graph re-prime. It deliberately retains the madVR
	// filter instance; a renderer restart is the separate full-recreation tier.
	// Activate the stopped graph into Pause before sending flush/NewSegment.
	// Sending that transaction while every downstream filter is stopped leaves
	// some madVR queue configurations cued but never consuming after Run.
	
	DebugLog::Log("DirectShowVideoRenderer::Reset() - Stopping graph for complete restart");
	
	if (m_pControl)
	{
		const auto logGraphState = [this](
			const char* phase, HRESULT transitionResult)
		{
			OAFilterState state = static_cast<OAFilterState>(-1);
			const HRESULT stateResult = m_pControl->GetState(0, &state);
			REFERENCE_TIME referenceTime = 0;
			const HRESULT clockResult = m_referenceClock ?
				m_referenceClock->GetTime(&referenceTime) : E_NOINTERFACE;
			DebugLog::Log(
				"DirectShow reset lifecycle: phase=%s transition_hr=0x%08lx "
				"get_state_hr=0x%08lx state=%ld clock_hr=0x%08lx "
				"reference_time=%lld",
				phase,
				static_cast<unsigned long>(transitionResult),
				static_cast<unsigned long>(stateResult),
				static_cast<long>(state),
				static_cast<unsigned long>(clockResult),
				static_cast<long long>(referenceTime));
		};

		HRESULT hr = m_pControl->Stop();
		if (FAILED(hr))
		{
			DebugLog::Log("DirectShowVideoRenderer::Reset() - Stop failed, hr=0x%x", hr);
			throw std::runtime_error("DirectShow graph Stop failed");
		}
		else
		{
			logGraphState("stopped", hr);

			// Capture admission is already closed. Stop/flush releases a
			// callback blocked in downstream Receive; now prove every admitted
			// callback has left before resetting source-owned queues.
			if (drainAfterGraphStop)
				drainAfterGraphStop();
			
			// Preserve the proven stop-settle boundary before reactivation.
			Sleep(100);

			// DirectShow transitions a stopped graph through Pause before Run. Do
			// that explicitly so the downstream pins are active when the source
			// publishes its serialized flush and fresh segment. S_FALSE is a valid
			// asynchronous/intermediate transition for this live graph.
			hr = m_pControl->Pause();
			if (FAILED(hr))
			{
				DebugLog::Log(
					"DirectShowVideoRenderer::Reset() - Pause failed, hr=0x%x",
					hr);
				throw std::runtime_error("DirectShow graph Pause failed");
			}
			logGraphState("paused-before-segment", hr);

			DebugLog::Log(
				"DirectShowVideoRenderer::Reset() - Resetting source while graph is active/paused");
			m_liveSource->Reset();
			logGraphState("segment-reset", S_OK);
			
			// Restart the graph
			hr = m_pControl->Run();
			if (FAILED(hr))
			{
				DebugLog::Log("DirectShowVideoRenderer::Reset() - Run failed, hr=0x%x", hr);
				throw std::runtime_error("DirectShow graph Run failed");
			}
			else
			{
				logGraphState("run-requested", hr);
			}
		}
	}
	else
	{
		// Fallback if no graph control
		DebugLog::Log("DirectShowVideoRenderer::Reset() - No pControl, just resetting source");
		if (drainAfterGraphStop)
			drainAfterGraphStop();
		m_liveSource->Reset();
	}
	
	m_frameCounter = 0;
	m_captureFrameCounterTracker.Reset();
	m_sourceGapRecoveryPolicy.OnGraphReset();
	ResetPPMMeasurement();
	m_unbufferedDeliverySuccessCount.store(0, std::memory_order_release);
	m_resetReadyForReveal.store(true, std::memory_order_release);
	DebugLog::Log("DirectShowVideoRenderer::Reset() - complete");
}


bool DirectShowVideoRenderer::RetargetWindowWithIngressDrain(
	uintptr_t targetWindow,
	const std::function<void()>& drainAfterGraphStop)
{
	const HWND targetHwnd = reinterpret_cast<HWND>(targetWindow);
	if (!targetHwnd || !IsWindow(targetHwnd))
		return false;
	if (!IsGraphThread())
	{
		return InvokeOnGraphThread(
			[this, targetWindow, drainAfterGraphStop]()
			{
				return RetargetWindowWithIngressDrain(
					targetWindow, drainAfterGraphStop);
			});
	}

	AssertGraphThread();
	if (!m_pControl || !m_videoWindow || !m_liveSource ||
		!m_videoHwnd || !IsWindow(m_videoHwnd))
	{
		return false;
	}

	const ULONGLONG operationStart = GetTickCount64();
	const HWND oldHwnd = m_videoHwnd;
	ULONGLONG stopMs = 0;
	ULONGLONG drainMs = 0;
	ULONGLONG resetMs = 0;
	ULONGLONG rebindMs = 0;
	ULONGLONG pauseMs = 0;
	ULONGLONG runMs = 0;
	HRESULT pauseHr = E_UNEXPECTED;
	try
	{
		m_unbufferedDeliverySuccessCount.store(
			0, std::memory_order_release);
		m_resetReadyForReveal.store(false, std::memory_order_release);

		ULONGLONG phaseStart = GetTickCount64();
		HRESULT hr = m_pControl->Stop();
		stopMs = GetTickCount64() - phaseStart;
		if (FAILED(hr))
			throw std::runtime_error(
				"DirectShow retarget graph Stop failed");

		phaseStart = GetTickCount64();
		if (drainAfterGraphStop)
			drainAfterGraphStop();
		drainMs = GetTickCount64() - phaseStart;

		// Preserve the proven madVR stop-settle boundary used by Reset().
		Sleep(100);

		phaseStart = GetTickCount64();
		WindowTeardown();
		OAHWND detachedOwner = 0;
		hr = m_videoWindow->get_Owner(&detachedOwner);
		if (FAILED(hr) || detachedOwner != 0)
			throw std::runtime_error(
				"DirectShow retarget failed to detach old window owner");
		m_videoHwnd = targetHwnd;
		RECT rectWindow = {};
		if (!GetWindowRect(m_videoHwnd, &rectWindow))
			throw std::runtime_error(
				"DirectShow retarget target rectangle unavailable");
		m_renderBoxWidth = rectWindow.right - rectWindow.left;
		m_renderBoxHeight = rectWindow.bottom - rectWindow.top;
		if (m_renderBoxWidth <= 0 || m_renderBoxHeight <= 0)
			throw std::runtime_error(
				"DirectShow retarget target rectangle is empty");
		WindowSetup();
		OAHWND attachedOwner = 0;
		hr = m_videoWindow->get_Owner(&attachedOwner);
		if (FAILED(hr) || attachedOwner != (OAHWND)targetHwnd)
			throw std::runtime_error(
				"DirectShow retarget failed to verify new window owner");
		rebindMs = GetTickCount64() - phaseStart;

		// Match ResetWithIngressDrain's proven lifecycle. A flush/NewSegment
		// transaction sent while the graph is stopped can leave madVR cued but
		// not consuming after Run, eventually blocking Receive.
		phaseStart = GetTickCount64();
		pauseHr = m_pControl->Pause();
		pauseMs = GetTickCount64() - phaseStart;
		if (FAILED(pauseHr))
			throw std::runtime_error(
				"DirectShow retarget graph Pause failed");

		phaseStart = GetTickCount64();
		RefreshDownstreamPrimeTarget();
		m_liveSource->Reset();
		resetMs = GetTickCount64() - phaseStart;

		phaseStart = GetTickCount64();
		hr = m_pControl->Run();
		if (SUCCEEDED(hr))
			hr = m_videoWindow->put_Visible(OATRUE);
		runMs = GetTickCount64() - phaseStart;
		if (FAILED(hr))
			throw std::runtime_error(
				"DirectShow retarget graph Run/visible failed");

		m_frameCounter = 0;
		m_captureFrameCounterTracker.Reset();
		m_sourceGapRecoveryPolicy.OnGraphReset();
		ResetPPMMeasurement();
		m_unbufferedDeliverySuccessCount.store(
			0, std::memory_order_release);
		m_resetReadyForReveal.store(true, std::memory_order_release);
		DebugLog::Log(
			"DirectShow window retarget completed: old=%p new=%p "
			"stop_ms=%llu drain_ms=%llu settle_ms=100 rebind_ms=%llu "
			"pause_hr=0x%08lx pause_ms=%llu reset_ms=%llu run_ms=%llu "
			"total_ms=%llu",
			oldHwnd, targetHwnd,
			static_cast<unsigned long long>(stopMs),
			static_cast<unsigned long long>(drainMs),
			static_cast<unsigned long long>(rebindMs),
			static_cast<unsigned long>(pauseHr),
			static_cast<unsigned long long>(pauseMs),
			static_cast<unsigned long long>(resetMs),
			static_cast<unsigned long long>(runMs),
			static_cast<unsigned long long>(
				GetTickCount64() - operationStart));
		return true;
	}
	catch (const std::exception& error)
	{
		DebugLog::Log(
			"DirectShow window retarget failed: old=%p new=%p "
			"elapsed_ms=%llu error=%s action=rollback",
			oldHwnd, targetHwnd,
			static_cast<unsigned long long>(
				GetTickCount64() - operationStart),
			error.what());
		try
		{
			m_pControl->Stop();
			WindowTeardown();
			OAHWND detachedOwner = 0;
			if (FAILED(m_videoWindow->get_Owner(&detachedOwner)) ||
				detachedOwner != 0)
			{
				throw std::runtime_error(
					"rollback failed to detach window owner");
			}
			m_videoHwnd = oldHwnd;
			RECT rectWindow = {};
			if (!GetWindowRect(m_videoHwnd, &rectWindow))
				throw std::runtime_error(
					"rollback target rectangle unavailable");
			m_renderBoxWidth = rectWindow.right - rectWindow.left;
			m_renderBoxHeight = rectWindow.bottom - rectWindow.top;
			WindowSetup();
			OAHWND attachedOwner = 0;
			if (FAILED(m_videoWindow->get_Owner(&attachedOwner)) ||
				attachedOwner != (OAHWND)oldHwnd)
			{
				throw std::runtime_error(
					"rollback failed to verify window owner");
			}
			const HRESULT rollbackPauseHr = m_pControl->Pause();
			if (FAILED(rollbackPauseHr))
				throw std::runtime_error(
					"rollback graph Pause failed");
			RefreshDownstreamPrimeTarget();
			m_liveSource->Reset();
			if (FAILED(m_pControl->Run()) ||
				FAILED(m_videoWindow->put_Visible(OATRUE)))
			{
				throw std::runtime_error(
					"rollback graph Run/visible failed");
			}
			m_resetReadyForReveal.store(
				true, std::memory_order_release);
			DebugLog::Log(
				"DirectShow window retarget rollback completed: target=%p",
				oldHwnd);
		}
		catch (const std::exception& rollbackError)
		{
			DebugLog::Log(
				"DirectShow window retarget rollback failed: target=%p "
				"error=%s",
				oldHwnd, rollbackError.what());
		}
		throw;
	}
}


void DirectShowVideoRenderer::ResetLiveQueue()
{
	if (!IsGraphThread())
	{
		InvokeOnGraphThread([this]()
			{
				ResetLiveQueue();
			});
		return;
	}
	AssertGraphThread();
	m_unbufferedDeliverySuccessCount.store(0, std::memory_order_release);
	m_resetReadyForReveal.store(false, std::memory_order_release);
	if (!m_liveSource)
	{
		DebugLog::Log("DirectShowVideoRenderer::ResetLiveQueue() - m_liveSource is NULL, returning");
		return;
	}

	// CLiveSource::Reset performs a serialized BeginFlush/EndFlush/NewSegment
	// transaction and purges both live queues. Unlike Reset(), it deliberately
	// leaves madVR and the DirectShow graph running.
	DebugLog::Log("DirectShowVideoRenderer::ResetLiveQueue() - flushing live source queue only");
	RefreshDownstreamPrimeTarget();
	m_liveSource->Reset();
	m_unbufferedDeliverySuccessCount.store(0, std::memory_order_release);
	m_resetReadyForReveal.store(true, std::memory_order_release);
	DebugLog::Log("DirectShowVideoRenderer::ResetLiveQueue() - complete");
}


void DirectShowVideoRenderer::SetOutputReadinessDeliveryReserve(
	size_t reserveFrames)
{
	// This is an atomic transport policy publication, not a graph operation.
	// Do not queue it behind graph work: the UI must publish the reserve before
	// its serialized post-ready reset begins.
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	if (m_liveSource && m_liveSource->GetVideoOutputPin())
	{
		m_liveSource->GetVideoOutputPin()->
			SetOutputReadinessDeliveryReserve(reserveFrames);
	}
}


void DirectShowVideoRenderer::SetQueueFramePolicy(
	size_t startupPrerollFrames, size_t steadyReserveFrames,
	bool steadyReserveConfigured)
{
	// Build() is asynchronous.  Retain the policy first so publishing it before
	// CLiveSource exists is not silently lost; LiveSourceBuildAndConnect() will
	// apply the retained values when it creates the output pin.
	m_queueStartupPrerollFrames.store(
		startupPrerollFrames, std::memory_order_release);
	m_queueSteadyTargetFrames.store(
		steadyReserveFrames, std::memory_order_release);
	m_queueSteadyTargetConfigured.store(
		steadyReserveConfigured, std::memory_order_release);

	// The lifetime lock keeps an already-live source valid while the pin
	// receives an update.  A fresh graph receives the retained values below.
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	const bool sourceReady =
		m_liveSource && m_liveSource->GetVideoOutputPin();
	if (m_liveSource && m_liveSource->GetVideoOutputPin())
	{
		m_liveSource->GetVideoOutputPin()->SetQueueFramePolicy(
			startupPrerollFrames, steadyReserveFrames,
			steadyReserveConfigured);
	}
	DebugLog::Log(
		"DirectShow queue policy retained: startup=%zu steady-target=%zu steady-explicit=%d source-ready=%d",
		startupPrerollFrames, steadyReserveFrames,
		steadyReserveConfigured ? 1 : 0, sourceReady ? 1 : 0);
}


void DirectShowVideoRenderer::SetResetRequestSink(
	std::shared_ptr<IRendererResetRequestSink> sink)
{
	m_resetRequestSink = std::move(sink);
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	if (m_liveSource)
		m_liveSource->SetResetRequestSink(m_resetRequestSink);
}


bool DirectShowVideoRenderer::GetLivenessSnapshot(
	RendererLivenessSnapshot& snapshot) const
{
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin())
	{
		snapshot = {};
		return false;
	}

	return m_liveSource->GetVideoOutputPin()->GetLivenessSnapshot(snapshot);
}


void DirectShowVideoRenderer::SetPresentationLeadFrames(
	size_t frames, bool configured)
{
	const size_t boundedFrames = (std::min)(frames, size_t{ 16 });
	m_presentationLeadFrames.store(
		boundedFrames, std::memory_order_release);
	m_presentationLeadFramesConfigured.store(
		configured, std::memory_order_release);

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	const bool sourceReady =
		m_liveSource && m_liveSource->GetVideoOutputPin();
	if (sourceReady)
	{
		m_liveSource->GetVideoOutputPin()->SetPresentationLeadFrames(
			boundedFrames, configured);
	}
	DebugLog::Log(
		"DirectShow presentation lead retained: frames=%zu explicit=%d source-ready=%d",
		boundedFrames, configured ? 1 : 0, sourceReady ? 1 : 0);
}


bool DirectShowVideoRenderer::GetLatencySnapshot(
	RendererLatencySnapshot& snapshot) const
{
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin())
	{
		snapshot = {};
		return false;
	}

	return m_liveSource->GetVideoOutputPin()->GetLatencySnapshot(snapshot);
}


void DirectShowVideoRenderer::OnSize()
{
	if (!IsGraphThread())
	{
		PostCoalescedGraphCommand(GRAPH_COMMAND_RESIZE, [this]()
			{
				OnSize();
			});
		return;
	}
	AssertGraphThread();
	if (!m_videoWindow)
		return;

	// Get window size
	RECT rectWindow;
	if (!GetWindowRect(m_videoHwnd, &rectWindow))
		throw std::runtime_error("Failed to get window rectangle");

	m_renderBoxWidth = rectWindow.right - rectWindow.left;
	m_renderBoxHeight = rectWindow.bottom - rectWindow.top;

	// TODO: Saw this blow up when resizing when the renderer is changing
	if (FAILED(m_videoWindow->SetWindowPosition(0, 0, m_renderBoxWidth, m_renderBoxHeight)))
		throw std::runtime_error("Failed to SetWindowPosition");
}


void DirectShowVideoRenderer::SetFrameQueueMaxSize(size_t frameMaxQueueSize)
{
	if (!IsGraphThread())
	{
		PostCoalescedGraphCommand(GRAPH_COMMAND_FRAME_QUEUE_SIZE,
			[this, frameMaxQueueSize]()
			{
				SetFrameQueueMaxSize(frameMaxQueueSize);
			});
		return;
	}
	if (!m_liveSource)
		return;
	m_liveSource->SetFrameQueueMaxSize(frameMaxQueueSize);
}


void DirectShowVideoRenderer::SetSceneAwareTimingCorrection(bool enabled)
{
	if (!IsGraphThread())
	{
		PostCoalescedGraphCommand(GRAPH_COMMAND_SCENE_CORRECTION,
			[this, enabled]()
			{
				SetSceneAwareTimingCorrection(enabled);
			});
		return;
	}
	if (!m_liveSource)
		return;

	m_liveSource->GetVideoOutputPin()->SetSceneAwareTimingCorrection(enabled);
}

void DirectShowVideoRenderer::SetSceneCorrectionUpstreamSample(bool enabled)
{
	if (!IsGraphThread())
	{
		PostCoalescedGraphCommand(GRAPH_COMMAND_SCENE_SAMPLE,
			[this, enabled]()
			{
				SetSceneCorrectionUpstreamSample(enabled);
			});
		return;
	}
	if (!m_liveSource)
		return;

	m_liveSource->GetVideoOutputPin()->SetSceneCorrectionUpstreamSample(enabled);
}

void DirectShowVideoRenderer::SetSubtitleRepositioning(bool enabled)
{
	SetSubtitleRepositioningMode(enabled ?
		SubtitleRepositionMode::BASIC :
		SubtitleRepositionMode::DISABLED);
}

void DirectShowVideoRenderer::SetSubtitleRepositioningMode(
	SubtitleRepositionMode mode)
{
	if (!IsGraphThread())
	{
		PostCoalescedGraphCommand(GRAPH_COMMAND_SUBTITLE_MODE,
			[this, mode]()
			{
				SetSubtitleRepositioningMode(mode);
			});
		return;
	}
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin())
		return;

	m_liveSource->GetVideoOutputPin()->SetSubtitleRepositioningMode(mode);
}

void DirectShowVideoRenderer::SetSceneTimingRates(
	double displayRefreshRateHz,
	double measuredCaptureRateHz)
{
	if (!IsGraphThread())
	{
		PostCoalescedGraphCommand(GRAPH_COMMAND_SCENE_RATES,
			[this, displayRefreshRateHz, measuredCaptureRateHz]()
			{
				SetSceneTimingRates(
					displayRefreshRateHz, measuredCaptureRateHz);
			});
		return;
	}
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin() || !m_videoState ||
		!m_videoState->displayMode)
		return;

	const double nominalRateHz = m_videoState->displayMode->RefreshRateHz();
	double deliveryRateHz = measuredCaptureRateHz > 0.0 ?
		measuredCaptureRateHz : nominalRateHz;

	// Rational-Rational timestamps are generated from the nominal rate and the
	// applied trim, not directly from hardware arrival time.  Pass the rate the
	// sink actually sees in sample timestamps.
	if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL)
	{
		const int appliedPpm = m_liveSource->GetCurrentPPMCorrection();
		const double durationScale = 1.0 + static_cast<double>(appliedPpm) / 1000000.0;
		if (durationScale > 0.0)
			deliveryRateHz = nominalRateHz / durationScale;
	}
	else if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO ||
		m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_THEO_NONE)
	{
		deliveryRateHz = nominalRateHz;
	}

	m_liveSource->GetVideoOutputPin()->SetSceneTimingRates(
		displayRefreshRateHz, deliveryRateHz);
}

void DirectShowVideoRenderer::SetSceneTimingReadiness(
	bool ready,
	uint64_t intervalsObserved)
{
	if (!IsGraphThread())
	{
		PostCoalescedGraphCommand(GRAPH_COMMAND_SCENE_READINESS,
			[this, ready, intervalsObserved]()
			{
				SetSceneTimingReadiness(ready, intervalsObserved);
			});
		return;
	}
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin())
		return;

	m_liveSource->GetVideoOutputPin()->SetSceneTimingReadiness(
		ready, intervalsObserved);
}

void DirectShowVideoRenderer::SetSceneTimingPhase(
	int64_t vblankQpc,
	int64_t refreshPeriodQpc,
	int64_t qpcFrequency)
{
	if (!IsGraphThread())
	{
		PostCoalescedGraphCommand(GRAPH_COMMAND_SCENE_PHASE,
			[this, vblankQpc, refreshPeriodQpc, qpcFrequency]()
			{
				SetSceneTimingPhase(
					vblankQpc, refreshPeriodQpc, qpcFrequency);
			});
		return;
	}
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin())
		return;

	m_liveSource->GetVideoOutputPin()->SetSceneTimingPhase(
		vblankQpc, refreshPeriodQpc, qpcFrequency);
}


size_t DirectShowVideoRenderer::GetFrameQueueSize()
{
	if (m_state.load(std::memory_order_acquire) !=
		RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	if (!m_liveSource ||
		m_state.load(std::memory_order_acquire) !=
			RendererState::RENDERSTATE_RENDERING)
		return 0;
	return m_liveSource->GetFrameQueueSize();
}


double DirectShowVideoRenderer::EntryLatencyMs() const
{
	if (m_state.load(std::memory_order_acquire) !=
		RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	return m_frameLatencyEntry;
}


double DirectShowVideoRenderer::ExitLatencyMs() const
{
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource ? m_liveSource->ExitLatencyMs() : 0.0;
}


uint64_t DirectShowVideoRenderer::DroppedFrameCount() const
{
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource ? m_liveSource->DroppedFrameCount() : 0;
}

uint64_t DirectShowVideoRenderer::SceneAwareCorrectionDropCount() const
{
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource && m_liveSource->GetVideoOutputPin() ?
		m_liveSource->GetVideoOutputPin()->SceneAwareCorrectionDropCount() : 0;
}

uint64_t DirectShowVideoRenderer::SceneAwareCorrectionRepeatCount() const
{
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource && m_liveSource->GetVideoOutputPin() ?
		m_liveSource->GetVideoOutputPin()->SceneAwareCorrectionRepeatCount() : 0;
}

bool DirectShowVideoRenderer::GetSceneTimingPrediction(
	double& secondsUntilCorrection, double& secondsUntilPlan,
	int& action, bool& planned) const
{
	secondsUntilCorrection = 0.0;
	secondsUntilPlan = 0.0;
	action = 0;
	planned = false;
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		return false;
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin() ||
		m_state != RendererState::RENDERSTATE_RENDERING)
		return false;

	return m_liveSource->GetVideoOutputPin()->GetSceneTimingPrediction(
		secondsUntilCorrection, secondsUntilPlan, action, planned);
}

bool DirectShowVideoRenderer::GetSceneTimingLastCorrection(
	int& action, double& secondsFromDeadline, uint64_t& correctionTick) const
{
	action = 0;
	secondsFromDeadline = 0.0;
	correctionTick = 0;
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		return false;
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin() ||
		m_state != RendererState::RENDERSTATE_RENDERING)
		return false;

	return m_liveSource->GetVideoOutputPin()->GetSceneTimingLastCorrection(
		action, secondsFromDeadline, correctionTick);
}

bool DirectShowVideoRenderer::SceneTimingRatesCompatible() const
{
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_state == RendererState::RENDERSTATE_RENDERING &&
		m_liveSource && m_liveSource->GetVideoOutputPin() &&
		m_liveSource->GetVideoOutputPin()->SceneTimingRatesCompatible();
}


uint64_t DirectShowVideoRenderer::SceneAwareDetectedCount() const
{
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource ? m_liveSource->SceneAwareDetectedCount() : 0;
}


uint64_t DirectShowVideoRenderer::SceneAwareLateCandidateCount() const
{
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource ? m_liveSource->SceneAwareLateCandidateCount() : 0;
}


void DirectShowVideoRenderer::OnGraphEvent(long evCode, LONG_PTR param1, LONG_PTR param2)
{
	// ! Do not tear down graph here
	// https://docs.microsoft.com/en-us/windows/win32/directshow/responding-to-events

	switch (evCode)
	{
	case EC_USERABORT:
	case EC_ERRORABORT:
	case EC_COMPLETE:
	if (m_state.load(std::memory_order_acquire) ==
		RendererState::RENDERSTATE_RENDERING)
		{
			// The UI owns ingress admission. Report a terminal failure so it
			// closes admission before forced owner teardown instead of
			// publishing STOPPED into RenderRemove with callbacks still live.
			SetState(RendererState::RENDERSTATE_FAILED);
		}
		break;
	}
}


void DirectShowVideoRenderer::SetState(RendererState state)
{
	AssertGraphThread();
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer::SetState(): %s"), ToString(state)));

	assert(state != RendererState::RENDERSTATE_UNKNOWN);
	assert(m_state.load(std::memory_order_acquire) != state);

	m_state.store(state, std::memory_order_release);
	std::lock_guard<std::mutex> lock(m_completionMutex);
	m_pendingStateCompletions.push_back(state);
}


void DirectShowVideoRenderer::PublishPendingStateCallback()
{
	assert(!IsGraphThread());
	std::deque<RendererState> completions;
	{
		std::lock_guard<std::mutex> lock(m_completionMutex);
		completions.swap(m_pendingStateCompletions);
	}
	for (const RendererState state : completions)
		m_callback.OnRendererState(state);
	if (m_pendingRendererRestart.exchange(false, std::memory_order_acq_rel))
		m_callback.OnRendererRestartRequired();
}


void DirectShowVideoRenderer::WakeForOwnerCompletion() const
{
	PostMessage(m_eventHwnd, m_eventMsg, 0, 0);
}


void DirectShowVideoRenderer::GraphBuild()
{
	AssertGraphThread();
	m_madVRDetectedRefreshRateHz.store(0.0, std::memory_order_release);
	const ULONGLONG buildStart = GetTickCount64();
	ULONGLONG phaseStart = buildStart;
	ULONGLONG filterGraphMs = 0;
	ULONGLONG clockMs = 0;
	ULONGLONG mediaTypeMs = 0;
	ULONGLONG liveSourceMs = 0;
	ULONGLONG rendererCreateMs = 0;
	ULONGLONG rendererConnectMs = 0;
	ULONGLONG windowSetupMs = 0;
	m_graphTeardownComplete.store(false, std::memory_order_release);
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer::GraphBuild(): Begin")));

	assert(m_videoState);

	//
	// Window setup
	//

	// Get window size
	RECT rectWindow;
	if (!GetWindowRect(m_videoHwnd, &rectWindow))
		throw std::runtime_error("Failed to get window rectangle");

	m_renderBoxWidth = rectWindow.right - rectWindow.left;
	m_renderBoxHeight = rectWindow.bottom - rectWindow.top;

	//
	// Directshow graph
	//

	FilterGraphBuild();
	filterGraphMs = GetTickCount64() - phaseStart;
	phaseStart = GetTickCount64();

	//
	// Clock
	//

	if (m_timingClock)
	{
		HRESULT clockResult = S_OK;
		m_referenceClock = new DirectShowTimingClock(*m_timingClock, clockResult);
		if (FAILED(clockResult))
		{
			delete static_cast<DirectShowTimingClock*>(m_referenceClock);
			m_referenceClock = nullptr;
			throw std::runtime_error("Failed to create DirectShow reference clock");
		}
		m_referenceClock->AddRef();

		if (FAILED(m_mediaFilter->SetSyncSource(m_referenceClock)))
			throw std::runtime_error("Failed to set sync source to our reference clock");

		// This is a video-only live graph; audio travels outside this process.
		// Keep the graph on the source/reference-clock timeline.  Enabling graph
		// stream offsets would require IAMPushSource stream-offset mutation and
		// provides no A/V synchronization benefit here.
	}
	clockMs = GetTickCount64() - phaseStart;

	//
	// Build conversion dependent stuff and media type
	//

	phaseStart = GetTickCount64();
	MediaTypeGenerate();
	mediaTypeMs = GetTickCount64() - phaseStart;

	//
	// Live source filter
	//

	phaseStart = GetTickCount64();
	LiveSourceBuildAndConnect();
	liveSourceMs = GetTickCount64() - phaseStart;

	//
	// Renderer
	//

	phaseStart = GetTickCount64();
	RendererBuild();
	rendererCreateMs = GetTickCount64() - phaseStart;

	if (!m_pRenderer)
		throw std::runtime_error("Created renderer instance wes nullptr");

	phaseStart = GetTickCount64();
	RendererConnect();
	rendererConnectMs = GetTickCount64() - phaseStart;
	RefreshDownstreamPrimeTarget();

	//
	// Window setup
	//

	phaseStart = GetTickCount64();
	WindowSetup();
	windowSetupMs = GetTickCount64() - phaseStart;

	//
	// Set up event notification.
	//

	if (FAILED(m_pEvent->SetNotifyWindow((OAHWND)m_eventHwnd, m_eventMsg, NULL)))
		throw std::runtime_error("Failed to setup event notification");

	SetState(RendererState::RENDERSTATE_READY);

	DebugLog::Log(
		"DirectShow graph build completed: target=%p "
		"filter_graph_ms=%llu clock_ms=%llu media_type_ms=%llu "
		"live_source_ms=%llu "
		"renderer_create_ms=%llu renderer_connect_ms=%llu "
		"window_setup_ms=%llu total_ms=%llu",
		m_videoHwnd,
		static_cast<unsigned long long>(filterGraphMs),
		static_cast<unsigned long long>(clockMs),
		static_cast<unsigned long long>(mediaTypeMs),
		static_cast<unsigned long long>(liveSourceMs),
		static_cast<unsigned long long>(rendererCreateMs),
		static_cast<unsigned long long>(rendererConnectMs),
		static_cast<unsigned long long>(windowSetupMs),
		static_cast<unsigned long long>(GetTickCount64() - buildStart));
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer::GraphBuild(): End")));
}


void DirectShowVideoRenderer::GraphTeardown()
{
	AssertGraphThread();
	// Details of how to clean up here https://docs.microsoft.com/en-us/windows/win32/directshow/using-windowed-mode

	DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer::GraphTeardown(): Begin")));

	//
	// Stop sending event notifications
	//
	// https://docs.microsoft.com/en-us/windows/win32/directshow/responding-to-events for notes on cleanup
	if (m_pEvent)
	{
		m_pEvent->SetNotifyWindow((OAHWND)nullptr, NULL, NULL);
		m_pEvent->Release();
		m_pEvent = nullptr;
	}

	//
	// Teardown window
	//

	WindowTeardown();

	//
	// Disonnect
	//

	LiveSourceDisconnect();

	//
	// Free
	//

	FilterGraphDestroy();

	if (m_referenceClock)
	{
		m_referenceClock->Release();
		m_referenceClock = nullptr;
	}

	LiveSourceDestroy();

	RendererDestroy();

	if (m_videoFramFormatter)
	{
		std::unique_lock<std::shared_mutex> lock(
			m_liveSourceLifetimeMutex);
		delete m_videoFramFormatter;
		m_videoFramFormatter = nullptr;
	}

	if (m_pmt.pbFormat)
	{
		CoTaskMemFree(m_pmt.pbFormat);
		m_pmt.pbFormat = nullptr;
	}

	DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer::GraphTeardown(): End")));
	m_graphTeardownComplete.store(
		GraphResourcesReleased(), std::memory_order_release);
}


void DirectShowVideoRenderer::GraphTeardownNoThrow() noexcept
{
	AssertGraphThread();
	try
	{
		if (m_pEvent)
			m_pEvent->SetNotifyWindow((OAHWND)nullptr, NULL, NULL);
	}
	catch (...)
	{
	}
	try
	{
		WindowTeardown();
	}
	catch (...)
	{
	}
	try
	{
		FilterGraphDestroy();
	}
	catch (...)
	{
	}

	if (m_referenceClock)
	{
		m_referenceClock->Release();
		m_referenceClock = nullptr;
	}
	try
	{
		LiveSourceDestroy();
	}
	catch (...)
	{
	}
	try
	{
		RendererDestroy();
	}
	catch (...)
	{
	}
	if (m_videoFramFormatter)
	{
		std::unique_lock<std::shared_mutex> lock(
			m_liveSourceLifetimeMutex);
		delete m_videoFramFormatter;
		m_videoFramFormatter = nullptr;
	}
	if (m_pmt.pbFormat)
	{
		CoTaskMemFree(m_pmt.pbFormat);
		m_pmt.pbFormat = nullptr;
	}
	m_graphTeardownComplete.store(
		GraphResourcesReleased(), std::memory_order_release);
}


bool DirectShowVideoRenderer::GraphResourcesReleased() const noexcept
{
	return !m_pGraph && !m_pControl && !m_pEvent && !m_videoWindow &&
		!m_pGraph2 && !m_mediaFilter && !m_amGraphStreams &&
		!m_referenceClock && !m_videoFramFormatter && !m_liveSource &&
		!m_pRenderer && !m_pmt.pbFormat;
}


void DirectShowVideoRenderer::GraphRun()
{
	AssertGraphThread();
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer::GraphRun()")));

	assert(m_pGraph);
	assert(m_pControl);

	if (FAILED(m_pControl->Run()))
		throw std::runtime_error("Failed to Run() graph");

	SetState(RendererState::RENDERSTATE_RENDERING);
	
	// NOTE: DO NOT call m_liveSource->Reset() here!
	// The Active() method has already done a complete reset of all state
	// before the threads were started. Calling Reset() here races with
	// the active conversion/delivery threads and causes timeline corruption.
	// Let the threads work with the clean state provided by Active();
}


void DirectShowVideoRenderer::FilterGraphBuild()
{
	AssertGraphThread();
	//
	// Directshow graph, note that we're not using a capture graph but DIY one
	// - https://docs.microsoft.com/en-us/windows/win32/directshow/about-the-capture-graph-builder
	// - https://www.codeproject.com/Articles/158053/DirectShow-Filters-Development-Part-2-Live-Source
	//

	if (FAILED(CoCreateInstance(
		CLSID_FilterGraph,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IGraphBuilder,
		(void**)&m_pGraph)))
		throw std::runtime_error("Failed to CoCreateInstance CLSID_FilterGraph");

	// Query for graph interfaces.
	if (FAILED(m_pGraph->QueryInterface(IID_IMediaControl, (void**)&m_pControl)))
		throw std::runtime_error("Failed to get IID_IMediaControl interface");

	if (FAILED(m_pGraph->QueryInterface(IID_IMediaEventEx, (void**)&m_pEvent)))
		throw std::runtime_error("Failed to get IID_IMediaEventEx interface");

	if (FAILED(m_pGraph->QueryInterface(IID_IVideoWindow, (void**)&m_videoWindow)))
		throw std::runtime_error("Failed to get IID_IVideoWindow interface");

	if (FAILED(m_pGraph->QueryInterface(IID_IFilterGraph2, (void**)&m_pGraph2)))
		throw std::runtime_error("Failed to get IID_IFilterGraph2 interface");

	if (FAILED(m_pGraph->QueryInterface(IID_IMediaFilter, (void**)&m_mediaFilter)))
		throw std::runtime_error("Failed to get IID_IMediaFilter interface");

	if (FAILED(m_pGraph->QueryInterface(IID_IAMGraphStreams, (void**)&m_amGraphStreams)))
		throw std::runtime_error("Failed to get IID_IAMGraphStreams interface");
}


void DirectShowVideoRenderer::FilterGraphDestroy()
{
	AssertGraphThread();
	if (m_pControl)
	{
		m_pControl->Release();
		m_pControl = nullptr;
	}

	if (m_pEvent)
	{
		m_pEvent->Release();
		m_pEvent = nullptr;
	}

	if (m_videoWindow)
	{
		m_videoWindow->Release();
		m_videoWindow = nullptr;
	}

	if (m_pGraph2)
	{
		m_pGraph2->Release();
		m_pGraph2 = nullptr;
	}

	if (m_mediaFilter)
	{
		m_mediaFilter->Release();
		m_mediaFilter = nullptr;
	}

	if (m_amGraphStreams)
	{
		m_amGraphStreams->Release();
		m_amGraphStreams = nullptr;
	}

	if (m_pGraph)
	{
		m_pGraph->Release();
		m_pGraph = nullptr;
	}
}


void DirectShowVideoRenderer::GraphStop()
{
	AssertGraphThread();
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowVideoRenderer::GraphStop()")));

	assert(m_pGraph);
	assert(m_pControl);

	// This is not sent to the outside world but it's used internally to guarantee that we're not
	// mis-handling events coming out of the DirectShow framework
	m_state.store(
		RendererState::RENDERSTATE_STOPPING, std::memory_order_release);

	// Stop directshow graph
	if (FAILED(m_pControl->Stop()))
		throw std::runtime_error("Failed to Stop() graph");

	// NOTE: Do NOT call m_liveSource->Reset() here
	// Reset is handled by DirectShowVideoRenderer::Reset() when needed
	// Calling it here causes duplicate resets on fullscreen transitions

	// Check if filter really stopped
	OAFilterState filterState = -1;  // Known invalid state
	if (FAILED(m_pControl->GetState(500, &filterState)))
		throw std::runtime_error("Failed to get filter state");

	if((FILTER_STATE)filterState != FILTER_STATE::State_Stopped)
		throw std::runtime_error("Filter graph was not stopped");

	assert(m_liveSource->GetFrameQueueSize() == 0);

}


void DirectShowVideoRenderer::WindowSetup()
{
	AssertGraphThread();
	// https://docs.microsoft.com/en-us/windows/win32/directshow/using-windowed-mode

	assert(m_videoHwnd);
	assert(m_renderBoxWidth > 0);
	assert(m_renderBoxHeight > 0);

	if (FAILED(m_videoWindow->put_Owner((OAHWND)m_videoHwnd)))
		throw std::runtime_error("Failed to set owner of video window");

	if (FAILED(m_videoWindow->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS)))
		throw std::runtime_error("Failed to set window style in video window");

	if (FAILED(m_videoWindow->SetWindowPosition(0, 0, m_renderBoxWidth, m_renderBoxHeight)))
		throw std::runtime_error("Failed to SetWindowPosition in video window");

	if (FAILED(m_videoWindow->HideCursor(OATRUE)))
		throw std::runtime_error("Failed to HideCursor in video window");
}


void DirectShowVideoRenderer::WindowTeardown()
{
	AssertGraphThread();
	// These can fail if we are terminating and there is no visible window anymore, no problem
	if (m_videoWindow)
	{
		m_videoWindow->put_Visible(OAFALSE);
		m_videoWindow->put_Owner(NULL);
		m_videoWindow->HideCursor(OAFALSE);
	}
}


void DirectShowVideoRenderer::LiveSourceBuildAndConnect()
{
	AssertGraphThread();
	assert(!m_liveSource);

	m_liveSource = dynamic_cast<CLiveSource*>(CLiveSource::CreateInstance(nullptr, nullptr));
	if (!m_liveSource)
		throw std::runtime_error("Failed to build a CLiveSource");

	m_liveSource->AddRef();
	m_liveSource->SetResetRequestSink(m_resetRequestSink);

	// Get the exact rational timing values from the DisplayMode
	// These are used for Bresenham-style exact integer math in RATIONAL_RATIONAL mode
	const unsigned int timeScale = m_videoState->displayMode->TimeScale();
	const unsigned int frameDurationTicks = m_videoState->displayMode->FrameDuration();

	// Calculate frame duration in 100ns units (for backward compatibility with other timing modes)
	const timestamp_t frameDuration100ns =
		(timestamp_t)round((1.0 / m_videoState->displayMode->RefreshRateHz()) * UNITS);

	m_liveSource->Initialize(
		m_videoFramFormatter,
		m_pmt,
		frameDuration100ns,
		timeScale,
		frameDurationTicks,
		m_timingClock,
		m_timestamp,
		m_useFrameQueue,
		m_frameQueueMaxSize);

	// Build() commonly receives the dialog's queue policy before this live
	// source exists.  Apply the retained policy immediately after Initialize(),
	// before the graph can run and accept any frames.
	const size_t startupPrerollFrames = m_queueStartupPrerollFrames.load(
		std::memory_order_acquire);
	const size_t steadyTargetFrames = m_queueSteadyTargetFrames.load(
		std::memory_order_acquire);
	const bool steadyTargetConfigured = m_queueSteadyTargetConfigured.load(
		std::memory_order_acquire);
	if (ALiveSourceVideoOutputPin* outputPin =
		m_liveSource->GetVideoOutputPin())
	{
		outputPin->SetQueueFramePolicy(
			startupPrerollFrames, steadyTargetFrames,
			steadyTargetConfigured);
		DebugLog::Log(
			"DirectShow queue policy applied to fresh graph: startup=%zu steady-target=%zu steady-explicit=%d",
			startupPrerollFrames, steadyTargetFrames,
			steadyTargetConfigured ? 1 : 0);
		const size_t presentationLeadFrames =
			m_presentationLeadFrames.load(std::memory_order_acquire);
		const bool presentationLeadConfigured =
			m_presentationLeadFramesConfigured.load(std::memory_order_acquire);
		outputPin->SetPresentationLeadFrames(
			presentationLeadFrames, presentationLeadConfigured);
		DebugLog::Log(
			"DirectShow presentation lead applied to fresh graph: frames=%zu explicit=%d",
			presentationLeadFrames,
			presentationLeadConfigured ? 1 : 0);
	}

	if (m_pGraph->AddFilter(m_liveSource, L"LiveSource") != S_OK)
	{
		m_liveSource->Release();
		throw std::runtime_error("Failed to add LiveSource to the graph");
	}
}


void DirectShowVideoRenderer::LiveSourceDisconnect()
{
	AssertGraphThread();
	if (m_liveSource)
	{
		IEnumPins* pEnum = nullptr;
		IPin* pLiveSourceOutputPin = nullptr;

		if (FAILED(m_liveSource->EnumPins(&pEnum)))
			throw std::runtime_error("Failed to get livesource pin enumerator");

		if (pEnum->Next(1, &pLiveSourceOutputPin, nullptr) != S_OK)
			throw std::runtime_error("Failed to run next on livesource pin");

		pEnum->Release();
		pEnum = nullptr;

		if (FAILED(m_pGraph->Disconnect(pLiveSourceOutputPin)))
			throw std::runtime_error("Failed to disconnect pins");

		pLiveSourceOutputPin->Release();
	}
}


void DirectShowVideoRenderer::LiveSourceDestroy()
{
	AssertGraphThread();
	std::unique_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	if (m_liveSource)
	{
		m_liveSource->Destroy();
		m_liveSource->Release();
		m_liveSource = nullptr;
	}
}


void DirectShowVideoRenderer::RefreshDownstreamPrimeTarget(
	const char* telemetrySource)
{
	AssertGraphThread();
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin())
		return;

	size_t targetFrames = 0;
	int cpuQueue = 0;
	int gpuQueue = 0;
	int windowedPresent = 0;
	int exclusivePresent = 0;
	int windowedBackbuffers = 0;
	int exclusiveBackbuffers = 0;
	BOOL delayUntilFull = FALSE;
	BOOL presentThread = FALSE;
	LONGLONG settingsRevision = 0;
	bool complete = false;
	IMadVRSettings* settings = nullptr;
	if (m_pRenderer && SUCCEEDED(m_pRenderer->QueryInterface(
		__uuidof(IMadVRSettings), reinterpret_cast<void**>(&settings))) && settings)
	{
		const bool revisionKnown = !!settings->SettingsGetRevision(&settingsRevision);
		// Unqualified stable IDs intentionally resolve against madVR's currently
		// active profile, including rule-selected profiles.
		const bool cpuKnown = !!settings->SettingsGetInteger(
			L"cpuQueueSize", &cpuQueue);
		const bool gpuKnown = !!settings->SettingsGetInteger(
			L"gpuQueueSize", &gpuQueue);
		const bool windowedKnown = !!settings->SettingsGetInteger(
			L"preRenderFramesWindowed",
			&windowedPresent);
		const bool exclusiveKnown = !!settings->SettingsGetInteger(
			L"preRenderFrames",
			&exclusivePresent);
		const bool windowedBackbuffersKnown = !!settings->SettingsGetInteger(
			L"backbufferCount", &windowedBackbuffers);
		const bool exclusiveBackbuffersKnown = !!settings->SettingsGetInteger(
			L"backbufferCountExcl", &exclusiveBackbuffers);
		const bool delayKnown = !!settings->SettingsGetBoolean(
			L"delayPlaybackStart2",
			&delayUntilFull);
		const bool presentThreadKnown = !!settings->SettingsGetBoolean(
			L"presentThread", &presentThread);
		wchar_t flushWindowed[96] = {};
		wchar_t flushExclusive[96] = {};
		int flushWindowedChars = static_cast<int>(_countof(flushWindowed));
		int flushExclusiveChars = static_cast<int>(_countof(flushExclusive));
		const bool flushWindowedKnown = !!settings->SettingsGetString(
			L"flushAfterPresent", flushWindowed, &flushWindowedChars);
		const bool flushExclusiveKnown = !!settings->SettingsGetString(
			L"flushAfterPresentExcl", flushExclusive, &flushExclusiveChars);
		char flushWindowedUtf8[192] = {};
		char flushExclusiveUtf8[192] = {};
		if (flushWindowedKnown)
			WideCharToMultiByte(CP_UTF8, 0, flushWindowed, -1,
				flushWindowedUtf8, static_cast<int>(_countof(flushWindowedUtf8)),
				nullptr, nullptr);
		if (flushExclusiveKnown)
			WideCharToMultiByte(CP_UTF8, 0, flushExclusive, -1,
				flushExclusiveUtf8, static_cast<int>(_countof(flushExclusiveUtf8)),
				nullptr, nullptr);
		complete = cpuKnown && gpuKnown && windowedKnown && exclusiveKnown &&
			cpuQueue >= 4 && cpuQueue <= 32 &&
			gpuQueue >= 4 && gpuQueue <= 24 &&
			windowedPresent >= 1 && windowedPresent <= 16 &&
			exclusivePresent >= 1 && exclusivePresent <= 16;
		if (complete)
		{
			// This is a diagnostic demand estimate, never live occupancy and never a
			// cap on VP's prime. Active-profile selection can change only after a new
			// media type reaches madVR, so a pre-reset snapshot may lag that change.
			targetFrames = static_cast<size_t>(cpuQueue) +
				(2u * static_cast<size_t>(gpuQueue)) +
				static_cast<size_t>(std::max(
					windowedPresent, exclusivePresent));
		}
		DebugLog::Log(
			"madVR effective configuration: source=%s revision=%lld revision_known=%d cpu=%d cpu_known=%d gpu=%d gpu_known=%d pre_render_windowed=%d windowed_known=%d pre_render_exclusive=%d exclusive_known=%d backbuffers_windowed=%d backbuffers_windowed_known=%d backbuffers_exclusive=%d backbuffers_exclusive_known=%d delay_until_full=%d delay_known=%d present_thread=%d present_thread_known=%d flush_after_present_windowed='%s' flush_windowed_known=%d flush_after_present_exclusive='%s' flush_exclusive_known=%d estimated_pipeline_frames=%zu estimate_valid=%d occupancy=unobservable",
			telemetrySource,
			static_cast<long long>(settingsRevision), revisionKnown ? 1 : 0,
			cpuQueue, cpuKnown ? 1 : 0, gpuQueue, gpuKnown ? 1 : 0,
			windowedPresent, windowedKnown ? 1 : 0,
			exclusivePresent, exclusiveKnown ? 1 : 0,
			windowedBackbuffers, windowedBackbuffersKnown ? 1 : 0,
			exclusiveBackbuffers, exclusiveBackbuffersKnown ? 1 : 0,
			delayUntilFull ? 1 : 0, delayKnown ? 1 : 0,
			presentThread ? 1 : 0, presentThreadKnown ? 1 : 0,
			flushWindowedUtf8, flushWindowedKnown ? 1 : 0,
			flushExclusiveUtf8, flushExclusiveKnown ? 1 : 0,
			targetFrames, complete ? 1 : 0);
		settings->Release();
	}

	LogMadVRRuntimeInfo(telemetrySource, false);

	// The aggregate remains diagnostic. Fresh-epoch priming always uses VP's
	// configurable physical reservoir and allocator headroom; this publication
	// is retained for per-epoch telemetry and future validated policy work.
	m_liveSource->GetVideoOutputPin()->SetDownstreamPrimeTarget(targetFrames);
}


void DirectShowVideoRenderer::MaybeScheduleMadVRRuntimeTelemetry()
{
	// This runs on the capture callback, where it must stay lock-free and must
	// never touch the renderer COM object. The actual query is serialized on the
	// graph owner below. A 30 second period is diagnostic-only; it does not
	// influence rate selection, queue control, reset policy, or delivery.
	constexpr ULONGLONG kTelemetryPeriodMs = 30000;
	const ULONGLONG now = GetTickCount64();
	ULONGLONG previous = m_lastMadVRRuntimeTelemetryTick.load(
		std::memory_order_acquire);
	if (previous != 0 && now >= previous &&
		now - previous < kTelemetryPeriodMs)
	{
		return;
	}
	if (!m_lastMadVRRuntimeTelemetryTick.compare_exchange_strong(
		previous, now, std::memory_order_acq_rel,
		std::memory_order_acquire))
	{
		return;
	}

	if (!PostCoalescedGraphCommand(GRAPH_COMMAND_MADVR_RUNTIME_TELEMETRY,
		[this]()
		{
			AssertGraphThread();
			// Sample both the active-profile configuration and renderer runtime
			// state together.  A settings revision/configuration change is
			// evidence only: polling must never restart a healthy live graph.
			RefreshDownstreamPrimeTarget("periodic-30s");
		}))
	{
		// Graph retirement can reject a post. Permit a new graph to sample
		// immediately rather than incorrectly suppressing it for 30 seconds.
		m_lastMadVRRuntimeTelemetryTick.store(0, std::memory_order_release);
	}
}


void DirectShowVideoRenderer::LogMadVRRuntimeInfo(
	const char* source, const bool requireAnyKnownValue)
{
	AssertGraphThread();
	IMadVRInfo* info = nullptr;
	if (!m_pRenderer || FAILED(m_pRenderer->QueryInterface(
		__uuidof(IMadVRInfo), reinterpret_cast<void**>(&info))) || !info)
	{
		m_madVRDetectedRefreshRateHz.store(0.0, std::memory_order_release);
		if (!requireAnyKnownValue)
		{
			DebugLog::Log(
				"madVR runtime info: source=%s unavailable renderer=%p occupancy=unobservable",
				source, m_pRenderer);
		}
		return;
	}

	double refreshRate = 0.0;
	ULONGLONG frameDuration = 0;
	SIZE displayMode = {};
	bool hdrOutput = false;
	bool exclusiveMode = false;
	bool dxvaDecode = false;
	bool dxvaDeinterlace = false;
	bool dxvaScaling = false;
	bool ivtc = false;
	int osdLatencyMs = 0;
	SIZE originalVideo = {};
	SIZE arAdjustedVideo = {};
	RECT videoCrop = {};
	RECT videoOutput = {};
	RECT croppedVideoOutput = {};
	LPWSTR madvrVersion = nullptr;
	LPWSTR yuvMatrix = nullptr;
	int madvrVersionChars = 0;
	int yuvMatrixChars = 0;
	const bool refreshKnown = SUCCEEDED(info->GetDouble("refreshRate", &refreshRate)) &&
		refreshRate > 0.0;
	m_madVRDetectedRefreshRateHz.store(
		refreshKnown ? refreshRate : 0.0, std::memory_order_release);
	const bool frameRateKnown = SUCCEEDED(info->GetUlonglong(
		"frameRate", &frameDuration)) && frameDuration > 0;
	const bool displayModeKnown = SUCCEEDED(info->GetSize(
		"displayModeSize", &displayMode));
	const bool hdrKnown = SUCCEEDED(info->GetBool("hdrOutput", &hdrOutput));
	const bool exclusiveKnown = SUCCEEDED(info->GetBool(
		"exclusiveModeActive", &exclusiveMode));
	const bool osdLatencyKnown = SUCCEEDED(info->GetInt(
		"osdLatency", &osdLatencyMs));
	const bool originalVideoKnown = SUCCEEDED(info->GetSize(
		"originalVideoSize", &originalVideo));
	const bool arAdjustedVideoKnown = SUCCEEDED(info->GetSize(
		"arAdjustedVideoSize", &arAdjustedVideo));
	const bool videoCropKnown = SUCCEEDED(info->GetRect(
		"videoCropRect", &videoCrop));
	const bool videoOutputKnown = SUCCEEDED(info->GetRect(
		"videoOutputRect", &videoOutput));
	const bool croppedVideoOutputKnown = SUCCEEDED(info->GetRect(
		"croppedVideoOutputRect", &croppedVideoOutput));
	const bool versionKnown = SUCCEEDED(info->GetString(
		"version", &madvrVersion, &madvrVersionChars)) && madvrVersion;
	const bool yuvMatrixKnown = SUCCEEDED(info->GetString(
		"yuvMatrix", &yuvMatrix, &yuvMatrixChars)) && yuvMatrix;
	(void)info->GetBool("dxvaDecodingActive", &dxvaDecode);
	(void)info->GetBool("dxvaDeinterlacingActive", &dxvaDeinterlace);
	(void)info->GetBool("dxvaScalingActive", &dxvaScaling);
	(void)info->GetBool("ivtcActive", &ivtc);
	const double postDeinterlaceFps = frameRateKnown ?
		10000000.0 / static_cast<double>(frameDuration) : 0.0;
	const bool anyKnownValue = refreshKnown || frameRateKnown ||
		displayModeKnown || hdrKnown || exclusiveKnown || osdLatencyKnown ||
		originalVideoKnown || arAdjustedVideoKnown || videoCropKnown ||
		videoOutputKnown || croppedVideoOutputKnown || versionKnown || yuvMatrixKnown;
	if (!requireAnyKnownValue || anyKnownValue)
	{
		DebugLog::Log(
			"madVR runtime info: source=%s version='%ls' version_known=%d detected_refresh_hz=%.6f refresh_known=%d post_deinterlace_fps=%.6f frame_rate_known=%d display_mode=%ldx%ld display_mode_known=%d hdr_output=%d hdr_known=%d exclusive=%d exclusive_known=%d yuv_matrix='%ls' yuv_matrix_known=%d original_video=%ldx%ld original_video_known=%d ar_adjusted_video=%ldx%ld ar_adjusted_video_known=%d video_crop=%ld,%ld,%ld,%ld video_crop_known=%d video_output=%ld,%ld,%ld,%ld video_output_known=%d cropped_video_output=%ld,%ld,%ld,%ld cropped_video_output_known=%d osd_latency_ms=%d osd_latency_known=%d dxva_decode=%d dxva_deinterlace=%d dxva_scaling=%d ivtc=%d occupancy=unobservable",
			source, madvrVersion ? madvrVersion : L"", versionKnown ? 1 : 0,
			refreshRate, refreshKnown ? 1 : 0,
			postDeinterlaceFps, frameRateKnown ? 1 : 0,
			static_cast<long>(displayMode.cx), static_cast<long>(displayMode.cy),
			displayModeKnown ? 1 : 0, hdrOutput ? 1 : 0, hdrKnown ? 1 : 0,
			exclusiveMode ? 1 : 0, exclusiveKnown ? 1 : 0,
			yuvMatrix ? yuvMatrix : L"", yuvMatrixKnown ? 1 : 0,
			static_cast<long>(originalVideo.cx), static_cast<long>(originalVideo.cy),
			originalVideoKnown ? 1 : 0,
			static_cast<long>(arAdjustedVideo.cx), static_cast<long>(arAdjustedVideo.cy),
			arAdjustedVideoKnown ? 1 : 0,
			static_cast<long>(videoCrop.left), static_cast<long>(videoCrop.top),
			static_cast<long>(videoCrop.right), static_cast<long>(videoCrop.bottom),
			videoCropKnown ? 1 : 0,
			static_cast<long>(videoOutput.left), static_cast<long>(videoOutput.top),
			static_cast<long>(videoOutput.right), static_cast<long>(videoOutput.bottom),
			videoOutputKnown ? 1 : 0,
			static_cast<long>(croppedVideoOutput.left), static_cast<long>(croppedVideoOutput.top),
			static_cast<long>(croppedVideoOutput.right), static_cast<long>(croppedVideoOutput.bottom),
			croppedVideoOutputKnown ? 1 : 0,
			osdLatencyMs, osdLatencyKnown ? 1 : 0,
			dxvaDecode ? 1 : 0, dxvaDeinterlace ? 1 : 0,
			dxvaScaling ? 1 : 0, ivtc ? 1 : 0);
	}
	if (madvrVersion)
		LocalFree(madvrVersion);
	if (yuvMatrix)
		LocalFree(yuvMatrix);
	info->Release();
}


void DirectShowVideoRenderer::RendererDestroy()
{
	AssertGraphThread();
	m_madVRDetectedRefreshRateHz.store(0.0, std::memory_order_release);
	if (m_pRenderer)
	{
		m_pRenderer->Release();
		m_pRenderer = nullptr;
	}
}


// Get current PPM correction information (override for RATIONAL_RATIONAL and CLOCK_RATIONAL support)
bool DirectShowVideoRenderer::GetPPMCorrectionInfo(int& ppmValue, bool& hasCorrection, CString& source) const
{
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	// Both RATIONAL_RATIONAL and CLOCK_RATIONAL use PPM corrections
	if ((m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL || 
	     m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL) && m_liveSource)
	{
		// Get PPM correction info directly from CLiveSource
		ppmValue = m_liveSource->GetCurrentPPMCorrection();
		hasCorrection = m_liveSource->HasPPMCorrection();
		source = m_liveSource->GetPPMCorrectionSource() ? TEXT("VideoProcessor.cfg") : TEXT("default");
		return true;
	}
	
	ppmValue = 0;
	hasCorrection = false;
	source = TEXT("N/A");
	return false;
}

// Get frame rate measurement and PPM deviation (for timing diagnostics)
bool DirectShowVideoRenderer::GetFrameRateAndPPM(double& measuredFps, int& ppmDeviation) const
{
	if (!m_hasPPMData.load(std::memory_order_acquire))
	{
		measuredFps = 0.0;
		ppmDeviation = 0;
		return false;
	}

	measuredFps = m_measuredFrameRate.load(std::memory_order_relaxed);
	ppmDeviation = m_ppmDeviation.load(std::memory_order_relaxed);
	return true;
}


bool DirectShowVideoRenderer::GetDetectedDisplayRefreshRate(
	double& refreshRateHz) const
{
	refreshRateHz = m_madVRDetectedRefreshRateHz.load(std::memory_order_acquire);
	return refreshRateHz >= 10.0 && refreshRateHz <= 240.0;
}

void DirectShowVideoRenderer::UpdatePPMMeasurement(timingclocktime_t frameTime) const
{
	// Initialize the cumulative measurement on the first frame after a full
	// renderer restart.
	if (m_firstFrameTime == 0)
	{
		m_firstFrameTime = frameTime;
		m_lastFrameTime = frameTime;
		m_frameCountForPPM = 1;
		m_lastPpmMeasurementPublishTime = frameTime;
		return;
	}

	m_lastFrameTime = frameTime;
	m_frameCountForPPM++;

	// Publish the all-time-since-restart estimate every five seconds. The
	// cadence estimate itself remains cumulative, which prevents short-window
	// noise from alternately predicting repeat and drop corrections.
	const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();
	const timingclocktime_t fiveSecondTicks = ticksPerSecond * 5;  // 5 seconds in ticks
	
	if ((frameTime - m_lastPpmMeasurementPublishTime) >= fiveSecondTicks)
	{
		const timingclocktime_t measurementElapsedTicks = frameTime - m_firstFrameTime;
		
		if (measurementElapsedTicks > 0 && m_frameCountForPPM > 1)
		{
			const double measurementElapsedSeconds =
				static_cast<double>(measurementElapsedTicks) /
				static_cast<double>(ticksPerSecond);
			const double measuredFps =
				static_cast<double>(m_frameCountForPPM - 1) / measurementElapsedSeconds;
			
			// Get theoretical refresh rate
			const double theoreticalFps = m_videoState->displayMode->RefreshRateHz();
			
			// Calculate PPM deviation: (measured - theoretical) * 1e6 / theoretical
			const double deviation = (measuredFps - theoreticalFps) / theoreticalFps;
			const int measuredPpmDeviation =
				(static_cast<int>(round(deviation * 1e6))) * -1;
			m_ppmDeviation.store(measuredPpmDeviation, std::memory_order_relaxed);
			m_measuredFrameRate.store(measuredFps, std::memory_order_relaxed);
			m_hasPPMData.store(true, std::memory_order_release);
			
			// **NEW: Feed PPM measurement to auto-calibrator if active**
			if (m_liveSource)
			{
				// Get the output pin to check if auto-calibration is active
				ALiveSourceVideoOutputPin* outputPin = m_liveSource->GetVideoOutputPin();
				
				DebugLog::Log("DirectShow: cumulative PPM estimate published - outputPin=%p, deviation=%d PPM",
					outputPin, measuredPpmDeviation);
				
				if (outputPin && outputPin->IsAutoCalibrating())
				{
					DebugLog::Log("DirectShow: Auto-calibration ACTIVE - feeding %d PPM to calibrator", measuredPpmDeviation);
					
					// Feed the PPM deviation to the calibrator
					// The calibrator will handle filtering, smoothing, and applying corrections
					outputPin->FeedPPMToCalibrator(measuredPpmDeviation);
					
					DebugLog::Log("DirectShow: PPM fed to calibrator successfully");
				}
				else
				{
					//DebugLog::Log("DirectShow: Auto-calibration INACTIVE - outputPin=%p, IsAutoCalibrating=%d", outputPin, outputPin ? outputPin->IsAutoCalibrating() : -1);
				}
			}
			else
			{
				DebugLog::Log("DirectShow: m_liveSource is NULL - cannot feed PPM");
			}
		}
		
		m_lastPpmMeasurementPublishTime = frameTime;
	}
}

void DirectShowVideoRenderer::ResetPPMMeasurement() const
{
	m_firstFrameTime = 0;
	m_lastFrameTime = 0;
	m_frameCountForPPM = 0;
	m_lastPpmMeasurementPublishTime = 0;
	m_measuredFrameRate.store(0.0, std::memory_order_release);
	m_ppmDeviation.store(0, std::memory_order_release);
	m_hasPPMData.store(false, std::memory_order_release);
}

size_t DirectShowVideoRenderer::GetConvertedQueueSize()
{
	if (m_state != RendererState::RENDERSTATE_RENDERING)
		throw std::runtime_error("Invalid state, can only be called while rendering");

	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource ? m_liveSource->GetConvertedQueueSize() : 0;
}


bool DirectShowVideoRenderer::GetActivePictureAspectRatio(double& aspectRatio) const
{
	aspectRatio = 0.0;
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource && m_liveSource->GetVideoOutputPin() &&
		m_liveSource->GetVideoOutputPin()->GetActivePictureAspectRatio(aspectRatio);
}


bool DirectShowVideoRenderer::GetActivePictureRectangle(ActivePictureRectangle& rectangle) const
{
	rectangle = {};
	std::shared_lock<std::shared_mutex> lock(m_liveSourceLifetimeMutex);
	return m_liveSource && m_liveSource->GetVideoOutputPin() &&
		m_liveSource->GetVideoOutputPin()->GetActivePictureRectangle(rectangle);
}
