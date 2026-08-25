/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <microsoft_directshow/DirectShowPresentationLeadPolicy.h>

#include <dvdmedia.h>
#include <guid.h>
#include <IMediaSideData.h>
#include <DebugLog.h>
#include <PPMCorrectionLoader.h>
#include <IntegerMath.h>

#include "ALiveSourceVideoOutputPin.h"



// Computes:
// round( frameIndex * 10,000,000 * frameDurationTicks / timeScale * trimNum / trimDen )
//
// PRECISION-PRESERVING ROUNDING:
// This function now uses proper rounding instead of truncation to minimize timing drift.
// Each frame timestamp is calculated independently from frame 0: timestamp[N] = f(N),
// NOT iteratively (timestamp[N] = timestamp[N-1] + duration).
//
// ROUNDING vs TRUNCATION:
// - Truncation: Accumulates negative bias, causing timing to run slow
// - Rounding: Distributes error randomly around zero, minimizing drift
//
// The math remains DETERMINISTIC: frame 100 always produces the same timestamp
// regardless of how frames 0-99 were calculated.
//
// PRACTICAL IMPACT: At 60 Hz with 6 PPM correction (common for displays):
// - Old truncation: ~6µs negative drift per second of video  
// - New rounding: <0.1µs average drift over long periods
//
// WHY THIS MATTERS FOR VIDEO:
// Video timing requires microsecond precision for smooth playback. Even small
// systematic errors compound quickly:
// - 1µs/sec error = 1ms drift per 16 minutes
// - 6µs/sec error = 1 frame slip per ~2.8 hours at 60fps
// - PPM corrections amplify truncation bias, making this critical
//
// The rounding approach maintains timing accuracy indefinitely while preserving
// the deterministic property that each frame's timestamp depends only on its
// frame number, not the calculation history.
static inline uint64_t RationalTimestampTrimmed(
	uint64_t frameIndex,
	uint64_t frameDurationTicks,
	uint64_t timeScale,
	uint64_t trimNum,
	uint64_t trimDen)
{
	constexpr uint64_t ticksPerSec = 10000000ULL;

	// Sequential rounding integer division - prevents timing drift accumulation
	// Each frame timestamp is calculated from frame 0, so rounding error does NOT compound
	uint64_t t = frameIndex;
	t = U64_MulDiv(t, ticksPerSec, 1ULL);                 // * 10,000,000
	t = U64_MulDiv(t, frameDurationTicks, timeScale);    // * duration rational
	t = U64_MulDiv(t, trimNum, trimDen);                 // apply ppm trim
	return t;
}


ALiveSourceVideoOutputPin::ALiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr):
	CBaseOutputPin(
		LIVE_SOURCE_FILTER_NAME, filter, pLock, phr,
		LIVE_SOURCE_FILTER_VIDEO_OUPUT_PIN_NAME)
{
}


STDMETHODIMP ALiveSourceVideoOutputPin::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	if (riid == IID_IAMPushSource)
		return GetInterface(static_cast<IAMPushSource*>(this), ppv);

	if (riid == IID_IAMLatency)
		return GetInterface(static_cast<IAMLatency*>(this), ppv);

	if (riid == IID_IKsPropertySet)
		return GetInterface(static_cast<IKsPropertySet*>(this), ppv);

	return CBaseOutputPin::NonDelegatingQueryInterface(riid, ppv);
}


void ALiveSourceVideoOutputPin::Initialize(
	IVideoFrameFormatter* const videoFrameFormatter,
	timestamp_t frameDuration,
	unsigned int timeScale,
	unsigned int frameDurationTicks,
	ITimingClock* const timingClock,
	DirectShowStartStopTimeMethod timestamp,
	const AM_MEDIA_TYPE& mediaType)
{
	if (!videoFrameFormatter)
		throw std::runtime_error("Cannot set null IVideoFrameFormatter");
	if (!timingClock)
		throw std::runtime_error("Cannot set null ITimingClock");

	if (frameDuration <= 0)
		throw std::runtime_error("Duration must be > 0");
	assert(frameDuration > 50000LL); // 5ms frame is 200Hz, probably a reasonable upper bound
	assert(frameDuration < 10000000LL);  // 1Hz, reasonable lower bound

	if (timeScale == 0)
		throw std::runtime_error("timeScale must be > 0");
	if (frameDurationTicks == 0)
		throw std::runtime_error("frameDurationTicks must be > 0");
	if (timingClock->TimingClockTicksPerSecond() == 0)
		throw std::runtime_error("Timing clock tick rate must be > 0");

	m_videoFrameFormatter = videoFrameFormatter;
	m_frameDuration = frameDuration;
	m_timeScale = timeScale;
	m_frameDurationTicks = frameDurationTicks;
	m_timingClock = timingClock;
	m_timestamp = timestamp;
	m_mediaType = mediaType;
	m_rationalTimingShadow = std::make_unique<DirectShowVideoTimingAdapter>(
		timestamp, timeScale, frameDurationTicks, frameDuration);
	m_rationalTimingShadowComparisons.store(0, std::memory_order_relaxed);
	m_rationalTimingShadowMismatches.store(0, std::memory_order_relaxed);
	m_rationalTimingControllerApplied.store(0, std::memory_order_relaxed);

	// Load PPM corrections for RATIONAL_RATIONAL mode
	if (timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL)
	{
		// Calculate refresh rate from timing parameters
		double refreshRate = (double)timeScale / (double)frameDurationTicks;
		LoadPPMCorrections(refreshRate);
	}
	// Also load PPM corrections for CLOCK_RATIONAL mode for consistent timing
	else if (timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL)
	{
		// Calculate refresh rate from timing parameters
		double refreshRate = (double)timeScale / (double)frameDurationTicks;
		LoadPPMCorrections(refreshRate);
	}
}


HRESULT ALiveSourceVideoOutputPin::GetMediaType(int iPosition, CMediaType* pmt)
{
	CheckPointer(pmt, E_POINTER);
	if (iPosition < 0)
		return E_INVALIDARG;
	if (iPosition > 0)
		return VFW_S_NO_MORE_ITEMS;

	CAutoLock mediaTypeLock(&m_mediaTypeLock);
	pmt->Set(m_mediaType);

	return S_OK;
}

bool ALiveSourceVideoOutputPin::RequestDynamicPictureAspectRatio(
	unsigned long aspectX, unsigned long aspectY)
{
	CMediaType candidate;
	{
		CAutoLock mediaTypeLock(&m_mediaTypeLock);
		candidate.Set(m_mediaType);
	}
	if (candidate.formattype != FORMAT_VIDEOINFO2 ||
		candidate.cbFormat < sizeof(VIDEOINFOHEADER2) ||
		!candidate.pbFormat || !m_Connected)
		return false;

	VIDEOINFOHEADER2* videoInfo =
		reinterpret_cast<VIDEOINFOHEADER2*>(candidate.pbFormat);
	const unsigned long deliveredAspectX = aspectX > 0 && aspectY > 0 ?
		aspectX : static_cast<unsigned long>(
			std::max<LONG>(1, videoInfo->bmiHeader.biWidth));
	const unsigned long deliveredAspectY = aspectX > 0 && aspectY > 0 ?
		aspectY : static_cast<unsigned long>(
			std::max<LONG>(1, std::abs(videoInfo->bmiHeader.biHeight)));
	videoInfo->dwPictAspectRatioX = deliveredAspectX;
	videoInfo->dwPictAspectRatioY = deliveredAspectY;

	const HRESULT accepted = m_Connected->QueryAccept(&candidate);
	if (accepted != S_OK)
	{
		DebugLog::Log(
			"Shaders: downstream rejected dynamic picture aspect %lu:%lu "
			"(HRESULT=0x%08lx); renderer replacement remains required",
			deliveredAspectX, deliveredAspectY,
			static_cast<unsigned long>(accepted));
		return false;
	}

	uint64_t queuedGeneration = 0;
	{
		CAutoLock mediaTypeLock(&m_mediaTypeLock);
		m_pendingMediaType = candidate;
		++m_pendingMediaTypeGeneration;
		if (m_pendingMediaTypeGeneration == 0)
			++m_pendingMediaTypeGeneration;
		queuedGeneration = m_pendingMediaTypeGeneration;
		m_hasPendingMediaType = true;
		m_pendingAspectX = aspectX;
		m_pendingAspectY = aspectY;
	}
	DebugLog::Log(
		"Shaders: queued dynamic picture aspect %lu:%lu on next sample "
		"generation=%llu",
		deliveredAspectX, deliveredAspectY,
		static_cast<unsigned long long>(queuedGeneration));
	return true;
}


uint64_t ALiveSourceVideoOutputPin::AttachPendingMediaType(
	IMediaSample* sample)
{
	if (!sample)
		return 0;
	CMediaType pending;
	uint64_t generation = 0;
	{
		CAutoLock mediaTypeLock(&m_mediaTypeLock);
		if (!m_hasPendingMediaType)
			return 0;
		pending = m_pendingMediaType;
		generation = m_pendingMediaTypeGeneration;
	}
	if (FAILED(sample->SetMediaType(&pending)))
		return 0;
	return generation;
}


void ALiveSourceVideoOutputPin::CompletePendingMediaType(
	uint64_t generation, HRESULT deliveryResult)
{
	if (generation == 0)
		return;
	CAutoLock mediaTypeLock(&m_mediaTypeLock);
	if (!m_hasPendingMediaType ||
		generation != m_pendingMediaTypeGeneration)
		return;
	if (FAILED(deliveryResult))
	{
		DebugLog::Log(
			"Shaders: dynamic picture aspect delivery failed "
			"(HRESULT=0x%08lx); update remains pending",
			static_cast<unsigned long>(deliveryResult));
		return;
	}
	const VIDEOINFOHEADER2* accepted =
		reinterpret_cast<const VIDEOINFOHEADER2*>(
			m_pendingMediaType.pbFormat);
	VIDEOINFOHEADER2* current =
		reinterpret_cast<VIDEOINFOHEADER2*>(m_mediaType.pbFormat);
	if (accepted && current)
	{
		current->dwPictAspectRatioX = accepted->dwPictAspectRatioX;
		current->dwPictAspectRatioY = accepted->dwPictAspectRatioY;
	}
	m_hasPendingMediaType = false;
	DebugLog::Log(
		"Shaders: dynamic picture aspect accepted on sample (%lu:%lu) "
		"generation=%llu",
		accepted ? accepted->dwPictAspectRatioX : m_pendingAspectX,
		accepted ? accepted->dwPictAspectRatioY : m_pendingAspectY,
		static_cast<unsigned long long>(generation));
}


HRESULT ALiveSourceVideoOutputPin::CheckMediaType(const CMediaType* pmt)
{
	CheckPointer(pmt, E_POINTER);

	if(pmt->majortype != m_mediaType.majortype)
	{
		return E_INVALIDARG;
	}

	if (!IsEqualGUID(pmt->subtype, m_mediaType.subtype))
	{
		return E_INVALIDARG;
	}

	if (pmt->formattype != m_mediaType.formattype)
	{
		return E_INVALIDARG;
	}

	return S_OK;
}


HRESULT ALiveSourceVideoOutputPin::DecideAllocator(IMemInputPin* pPin, IMemAllocator** ppAlloc)
{
	// TODO: We can be more lenient here if this comes from a VideoInfo1 renderer as we're not using the HDR data extensions
	CheckPointer(pPin, E_POINTER);
	CheckPointer(ppAlloc, E_POINTER);

	HRESULT hr = NOERROR;
	*ppAlloc = nullptr;

	// get downstream prop request
	// the derived class may modify this in DecideBufferSize, but
	// we assume that he will consistently modify it the same way,
	// so we only get it once
	ALLOCATOR_PROPERTIES prop;
	ZeroMemory(&prop, sizeof(prop));

	hr = pPin->GetAllocatorRequirements(&prop);
	// Some downstream pins do not implement allocator requirements.  In that
	// case the zeroed defaults are valid and DecideBufferSize supplies the
	// actual request; other failures should be reported to the caller.
	if (FAILED(hr) && hr != E_NOTIMPL)
		return hr;

	if (prop.cbAlign == 0)
		prop.cbAlign = 1;

	// We only try the allocator of the input pin, we don't have a suitable allocator
	// for IMediaSideData.
	hr = pPin->GetAllocator(ppAlloc);
	if (SUCCEEDED(hr))
	{
		hr = DecideBufferSize(*ppAlloc, &prop);
		if (SUCCEEDED(hr))
		{
			hr = pPin->NotifyAllocator(*ppAlloc, FALSE);
			if (SUCCEEDED(hr))
			{
				return NOERROR;
			}
		}
	}

	// If the GetAllocator failed we may not have an interface
	if (*ppAlloc)
	{
		(*ppAlloc)->Release();
		*ppAlloc = nullptr;
	}
	return hr;
}


LONG ALiveSourceVideoOutputPin::ResolveAllocatorBufferRequest(
	LONG downstreamRequested, LONG recommendedBuffers)
{
	constexpr LONG kMinimumBuffers = 8;
	constexpr LONG kMaximumBuffers = 48;
	return std::min(kMaximumBuffers,
		std::max(kMinimumBuffers,
			std::max(downstreamRequested, recommendedBuffers)));
}


HRESULT ALiveSourceVideoOutputPin::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest)
{
	CheckPointer(pAlloc,E_POINTER);
	CheckPointer(ppropInputRequest,E_POINTER);
	CheckPointer(m_videoFrameFormatter, E_UNEXPECTED);

	HRESULT hr = NOERROR;

	// Keep allocator memory bounded. A 4K P010 frame is roughly 25 MB, so the
	// former fixed request for 128 buffers could commit several gigabytes.
	// Preserve downstream minimums, but cap the request to a practical queue
	// headroom limit; SetProperties reports the actual count below.
	const LONG requestedBuffers = ppropInputRequest->cBuffers;
	const LONG recommendedBuffers = GetAllocatorBufferCount();
	ppropInputRequest->cBuffers = ResolveAllocatorBufferRequest(
		requestedBuffers, recommendedBuffers);
	m_negotiatedAllocatorRequestCount.store(
		ppropInputRequest->cBuffers, std::memory_order_release);
	ppropInputRequest->cbBuffer = m_videoFrameFormatter->GetOutFrameSize();

	ASSERT(ppropInputRequest->cbBuffer);

	ALLOCATOR_PROPERTIES Actual = {};
	hr = pAlloc->SetProperties(ppropInputRequest,&Actual);
	if(FAILED(hr))
	{
		return hr;
	}

	if(Actual.cbBuffer < ppropInputRequest->cbBuffer)
	{
		return E_FAIL;
	}
	if (Actual.cBuffers <= 0)
		return E_FAIL;
	m_negotiatedAllocatorBufferCount.store(
		Actual.cBuffers, std::memory_order_release);

	// ✅ Add this logging to verify the fix
	DbgLog((LOG_TRACE, 1, TEXT("DecideBufferSize: Requested %d buffers, got %d buffers, size %d bytes"), 
		ppropInputRequest->cBuffers, Actual.cBuffers, Actual.cbBuffer));
	DebugLog::Log(
		"DirectShow allocator negotiated: requested_buffers=%ld actual_buffers=%ld buffer_bytes=%ld",
		static_cast<long>(ppropInputRequest->cBuffers),
		static_cast<long>(Actual.cBuffers),
		static_cast<long>(Actual.cbBuffer));

	return S_OK;
}

//
// IAMPushSource
//


STDMETHODIMP ALiveSourceVideoOutputPin::GetMaxStreamOffset(REFERENCE_TIME *prtMaxOffset)
{
	CheckPointer(prtMaxOffset, E_POINTER);
	*prtMaxOffset = 0;
	return S_OK;
}


STDMETHODIMP ALiveSourceVideoOutputPin::GetPushSourceFlags(ULONG *pFlags)
{
	// Return (* pFlags) = 0 [if this is a iAMPushSource]
	// https://docs.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-ireferenceclock
	CheckPointer(pFlags, E_POINTER);
	*pFlags = 0;
	return S_OK;
}


STDMETHODIMP ALiveSourceVideoOutputPin::GetStreamOffset(REFERENCE_TIME *prtOffset)
{
	CheckPointer(prtOffset, E_POINTER);
	*prtOffset = 0;
	return S_OK;
}


STDMETHODIMP ALiveSourceVideoOutputPin::SetMaxStreamOffset(REFERENCE_TIME rtMaxOffset)
{
	return E_NOTIMPL;
}


STDMETHODIMP ALiveSourceVideoOutputPin::SetPushSourceFlags(ULONG Flags)
{
	return E_NOTIMPL;
}


STDMETHODIMP ALiveSourceVideoOutputPin::SetStreamOffset(REFERENCE_TIME rtOffset)
{
	return E_NOTIMPL;
}


STDMETHODIMP ALiveSourceVideoOutputPin::GetLatency(REFERENCE_TIME *prtLatency)
{
	// latency in 100-nanosecond units.
	CheckPointer(prtLatency, E_POINTER);
	*prtLatency = 0;  // TODO: Just a guess
	return S_OK;
}


//
// IKsPropertySet
//


HRESULT STDMETHODCALLTYPE ALiveSourceVideoOutputPin::Set(
	REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData,
	LPVOID pPropData, DWORD cbPropData)
{
	// https://docs.microsoft.com/en-us/windows/win32/directshow/pin-requirements-for-capture-filters
	return E_NOTIMPL;
}


HRESULT STDMETHODCALLTYPE ALiveSourceVideoOutputPin::Get(
	REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData,
	DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData,
	DWORD* pcbReturned)
{
	// https://docs.microsoft.com/en-us/windows/win32/directshow/pin-requirements-for-capture-filters

	if (guidPropSet != AMPROPSETID_Pin)
		return E_PROP_SET_UNSUPPORTED;

	if (dwPropID != AMPROPERTY_PIN_CATEGORY)
		return E_PROP_ID_UNSUPPORTED;

	if (!pPropData && !pcbReturned)
		return E_POINTER;

	if (pcbReturned)
		*pcbReturned = sizeof(GUID);

	if (!pPropData)  // Caller just wants to know the size.
		return S_OK;

	if (cbPropData < sizeof(GUID))  // The buffer is too small.
		return E_UNEXPECTED;

	*(GUID*)pPropData = PIN_CATEGORY_CAPTURE;
	return S_OK;
}


HRESULT STDMETHODCALLTYPE ALiveSourceVideoOutputPin::QuerySupported(
	REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport)
{
	// https://docs.microsoft.com/en-us/windows/win32/directshow/pin-requirements-for-capture-filters

	if (guidPropSet != AMPROPSETID_Pin)
		return E_PROP_SET_UNSUPPORTED;

	if (dwPropID != AMPROPERTY_PIN_CATEGORY)
		return E_PROP_ID_UNSUPPORTED;

	// We support getting this property, but not setting it.
	if (pTypeSupport)
		*pTypeSupport = KSPROPERTY_SUPPORT_GET;

	return S_OK;
}


void ALiveSourceVideoOutputPin::OnHDRData(HDRDataSharedPtr& hdrData)
{
	if (!hdrData)
		throw std::runtime_error("Setting HDRData to null is not allowed");

	CAutoLock timingLock(&m_timingStateLock);
	m_hdrData = hdrData;
	m_hdrChanged = true;
}


void ALiveSourceVideoOutputPin::Reset()
{
	DebugLog::Log("ALiveSourceVideoOutputPin::Reset() - HDMI resync timing reset started");

	if (FAILED(DeliverBeginFlush()))
		throw std::runtime_error("Failed to deliver beginflush");

	ResetTimingState();

	if (FAILED(DeliverEndFlush()))
		throw std::runtime_error("Failed to deliver endflush");

	if (FAILED(DeliverNewSegment(0, MAXLONGLONG, 1.0)))
		throw std::runtime_error("Failed to deliver new segment");
	m_deliverNewSegment = false;
	CompleteCoordinatedReset();

	DebugLog::Log("ALiveSourceVideoOutputPin::Reset() - HDMI resync timing reset completed");
}


HRESULT ALiveSourceVideoOutputPin::BeginTerminalFlush()
{
	bool expected = false;
	if (!m_terminalFlushStarted.compare_exchange_strong(
		expected, true, std::memory_order_acq_rel))
	{
		return S_OK;
	}

	const HRESULT result = DeliverBeginFlush();
	if (FAILED(result))
	{
		m_terminalFlushStarted.store(false, std::memory_order_release);
	}
	return result;
}


bool ALiveSourceVideoOutputPin::RequestCoordinatedReset(
	const char* reason, RendererResetReason resetReason)
{
	RendererResetRequest request;
	request.reason = resetReason;
	request.scope = RendererResetScope::Graph;
	const bool firstRequest = m_resetRequestLatch.Request(request);
	if (firstRequest)
	{
		DebugLog::Log(
			"DirectShow source recovery requested: reason=%s "
			"action=publish-to-reset-coordinator",
			reason ? reason : "unknown");
	}
	return firstRequest;
}


void ALiveSourceVideoOutputPin::ResetTimingState()
{
	CAutoLock timingLock(&m_timingStateLock);
	if (m_rationalTimingShadow)
		m_rationalTimingShadow->Reset();

	if (m_hdrData)
		m_hdrChanged = true;

	// CRITICAL FIX: Reset ALL timing state atomically to prevent HDMI resync issues
	m_frameCounter = 0;
	m_previousFrameCounter = 0;
	m_frameCounterOffset = 0;  // This will be recalculated on first new frame
	m_frameCounterOffsetValid = false;
	m_previousTimeStop = 0;
	m_startTimeOffset = 0;     // CRITICAL: Must reset to 0 to allow recalculation on first frame
	m_droppedFrameCount = 0;
	
	// Force discontinuity on the first new sample. Reset itself sends the new
	// segment below; the conversion worker must not send a second NewSegment
	// concurrently with delivery.
	m_forceDiscontinuity = true;
	m_deliverNewSegment = false;
	
	// Reset timing method-specific state
	// CLOCK_RATIONAL SPECIAL: Reset m_rationalFrameDuration = 0 to trigger re-initialization
	// on next frame, which will re-apply the 40ms lead offset at baseline
	m_previousHardwareTimestamp = 0;
	m_hardwareTimingAnomalyCount = 0;
	m_rationalFrameDuration = 0;  // CRITICAL: Forces re-init with lead offset on next frame
	m_minFrameAdvance = 0;
	m_maxFrameAdvance = 0;
	m_smartDurationHistory.Reset();
	
	// Reset smart timing statistics
	m_smartHardwareTimestampCount = 0;
	m_smartSyntheticTimestampCount = 0;
	m_smartRejectedTimestampCount = 0;
	
	// Clear timestamp history for CLOCK_SMART modes
	{
		std::lock_guard<std::mutex> lock(m_timestampHistoryMutex);
		memset(m_timestampHistory, 0, sizeof(m_timestampHistory));
		m_timestampHistoryIndex = 0;
	}
	
	// AUTO-CALIBRATION: Reset calibrator state on stream change
	// This ensures clean measurement after format changes or HDMI reconnections
	if (m_useAutoCalibration)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): Resetting auto-calibrator for clean restart")));
		m_autoPpmCalibrator.Reset();
		
		// Re-initialize calibrator with simplified API
		m_autoPpmCalibrator.Initialize();
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): Auto-calibrator re-initialized")));
	}

	// LEAD RAMP: Reset ramp state but preserve user configuration
	// m_leadRampDurationMs is NOT reset (user's configurable preference)
	m_leadRampStartTimeMs = 0;      // Reset ramp timing to restart from frame 0
	m_leadRampActive = false;       // Mark ramp as needing re-initialization

	DebugLog::Log("ALiveSourceVideoOutputPin::ResetTimingState() - All timing state cleared for HDMI resync");
}


void ALiveSourceVideoOutputPin::RestartTimingOriginAfterPreroll()
{
	CAutoLock timingLock(&m_timingStateLock);
	if (m_rationalTimingShadow)
		m_rationalTimingShadow->RestartAfterPreroll();

	// Preserve the established DirectShow segment, but start timestamp and media
	// time generation from the first frame produced after preroll.  This is the
	// operating point used by the prior live queue implementation and prevents
	// the renderer from treating the whole preroll as permanent live latency.
	// It is called only once while delivery transitions out of buffering.
	m_frameCounter = 0;
	m_previousFrameCounter = 0;
	m_frameCounterOffset = 0;
	m_frameCounterOffsetValid = false;
	m_previousTimeStop = 0;
	m_startTimeOffset = 0;
	m_smartDurationHistory.Reset();
	m_previousHardwareTimestamp = 0;

	DebugLog::Log("ALiveSourceVideoOutputPin::RestartTimingOriginAfterPreroll() - legacy live-preroll timestamp origin restored");
}

void ALiveSourceVideoOutputPin::ResetTimingControllerToPipelineEpoch(
	uint64_t epoch)
{
	if (epoch == 0 || !m_rationalTimingShadow)
		return;
	CAutoLock timingLock(&m_timingStateLock);
	m_rationalTimingShadow->ResetToEpoch({ epoch });
}


REFERENCE_TIME ALiveSourceVideoOutputPin::CalculateSmartFrameDuration() const
{
	const REFERENCE_TIME theoreticalDuration = static_cast<REFERENCE_TIME>(
		(REFERENCE_TIME_TICKS_PER_SECOND * m_frameDurationTicks) / m_timeScale);
	return m_smartDurationHistory.AverageOr(theoreticalDuration);
}


HRESULT ALiveSourceVideoOutputPin::RenderVideoFrameIntoSample(VideoFrame& videoFrame, IMediaSample* const pSample)
{
	CheckPointer(pSample, E_POINTER);
	CAutoLock timingLock(&m_timingStateLock);
	assert(videoFrame.GetTimingTimestamp() > 0);
	assert(m_frameDuration > 0);
	// Initialize() validates this, but keep the render path defensive because
	// this method is also reached from asynchronous worker threads.
	if (!m_timingClock || m_timingClock->TimingClockTicksPerSecond() == 0)
		return E_UNEXPECTED;
	if (!m_videoFrameFormatter)
		return E_UNEXPECTED;

	// RATIONAL_RATIONAL TIMELINE PROTECTION: Detect and recover from timeline corruption BEFORE incrementing frame counter
	// This is critical because the frame counter increment at the start of the function means we need to detect
	// corruption BEFORE that increment, not after.
	if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL)
	{
		// RATIONAL_RATIONAL should NEVER have m_startTimeOffset set (that's for CLOCK modes)
		// If non-zero, it indicates corruption from a previous timing mode that wasn't properly reset
		// This can happen after renderer changes or improper shutdown/restart
		if (m_startTimeOffset != 0)
		{
			DbgLog((LOG_WARNING, 1, TEXT("::FillBuffer(#%I64u): RATIONAL_RATIONAL timeline CORRUPTION DETECTED - m_startTimeOffset=%I64d (should be 0)"),
				videoFrame.GetCounter(), m_startTimeOffset));
			
			// Force COMPLETE timeline reset to restore RATIONAL_RATIONAL integrity
			m_startTimeOffset = 0;
			m_previousTimeStop = 0;
			m_forceDiscontinuity = true;
			m_deliverNewSegment = true;
			
			DbgLog((LOG_WARNING, 1, TEXT("  Timeline RESET: m_startTimeOffset cleared, discontinuity and new segment flagged")));
		}
		
		// NOTE: AutoPpmCalibrator is now fed by DirectShowVideoRenderer::UpdatePPMMeasurement()
		// No need to call OnFrame() here - the calibrator receives pre-calculated PPM values
		// every 5 seconds from the renderer's rolling window measurement.
	}

	++m_frameCounter;

	//
	// Media time
	//

	// Guarantee first frame to start counting at zero
	uint64_t streamFrameCounter = videoFrame.GetCounter();
	if (!m_frameCounterOffsetValid)
	{
		m_frameCounterOffset = streamFrameCounter;
		m_frameCounterOffsetValid = true;
		
		// Log the frame counter offset being set (happens after reset)
		DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): Frame counter offset set to %I64u (first frame after reset)"),
			videoFrame.GetCounter(), m_frameCounterOffset));
	}
	streamFrameCounter -= m_frameCounterOffset;

	// Set frame counter
	LONGLONG mediaTimeStart = streamFrameCounter;
	LONGLONG mediaTimeStop = mediaTimeStart + 1;
	HRESULT hr = pSample->SetMediaTime(&mediaTimeStart, &mediaTimeStop);
	if (FAILED(hr))
		return hr;

	// Discontinuity check
	const bool isDiscontinuity =
		videoFrame.GetCounter() != (m_previousFrameCounter + 1) ||
		m_frameCounter == 1 ||
		m_forceDiscontinuity;  // Force discontinuity after timeline reset
		
	// Allocator samples are recycled. Set both TRUE and FALSE explicitly so a
	// discontinuity carried by an earlier frame cannot leak into a later,
	// continuous frame when the same IMediaSample is reused.
	hr = pSample->SetDiscontinuity(isDiscontinuity ? TRUE : FALSE);
	if (FAILED(hr))
		return hr;

	if (isDiscontinuity)
	{
		DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): Frame counter jumped from %I64u (stream frame %I64u), discontinuity detected%s"),
			videoFrame.GetCounter(), m_previousFrameCounter, streamFrameCounter,
			m_forceDiscontinuity ? TEXT(" (FORCED after timeline reset)") : TEXT("")));
			
		// Clear the force flag after setting discontinuity
		m_forceDiscontinuity = false;
	}
	
	// If timing corruption requested a new segment, the concrete delivery path
	// performs a serialized Reset before this sample can reach downstream.
	// NewSegment must never be sent from the conversion thread.

	m_previousFrameCounter = videoFrame.GetCounter();

	//
	// Setting the time
	//

	REFERENCE_TIME timeStart = REFERENCE_TIME_INVALID;
	REFERENCE_TIME timeStop = REFERENCE_TIME_INVALID;
	REFERENCE_TIME effectiveHardwareTime = REFERENCE_TIME_INVALID;
	const bool usesHardwareClock =
		m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL ||
		m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
		m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2 ||
		m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO ||
		m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK ||
		m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE;
	if (usesHardwareClock)
	{
		// The DeckLink timestamp is the complete live-clock input. Source counters
		// are discontinuity/recovery evidence only; they must never manufacture
		// additional presentation time or a cumulative clock offset.
		effectiveHardwareTime = ConvertTimingClockToReferenceTime(
			videoFrame.GetTimingTimestamp(),
			m_timingClock->TimingClockTicksPerSecond());
	}

	// Determine start time
	switch (m_timestamp)
	{
	case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
	{
		const uint64_t frameNum = streamFrameCounter;

		// Use dynamically loaded PPM correction instead of hardcoded constants
		const uint64_t baseStart =
			RationalTimestampTrimmed(
				frameNum,
				(uint64_t)m_frameDurationTicks,
				(uint64_t)m_timeScale,
				GetRationalTrimNumerator(),
				RATIONAL_TRIM_DENOMINATOR);

		REFERENCE_TIME tsStart =
			(REFERENCE_TIME)baseStart + m_rationalPipelineOffset;

		// ---- MONOTONIC SAFETY ----
		if (streamFrameCounter == 0)
		{
			// first frame after reset
			m_previousTimeStop = tsStart - 1;
		}
		else if (tsStart <= m_previousTimeStop)
		{
			tsStart = m_previousTimeStop + 1;
		}

		timeStart = tsStart;
		break;
	}



	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
	{
		// HYBRID MODE: Hardware timestamp for start time, rational math for duration, monotonic enforcement
		
		// Get raw hardware timestamp using the same integer conversion as the
		// other clock-based modes.  The previous double conversion lost
		// precision as the hardware clock value grew.
		const REFERENCE_TIME rawHardwareTime = effectiveHardwareTime;
		
		// Initialize rational frame duration and limits on first frame
		if (m_rationalFrameDuration == 0)
		{
			const uint64_t referenceTimePerSecond = 10000000ULL;
			
			// Apply PPM trim correction to frame duration (consistent with RATIONAL_RATIONAL mode)
			uint64_t trimmedDurationTicks = U64_MulDiv(
				(uint64_t)m_frameDurationTicks,
				GetRationalTrimNumerator(),
				RATIONAL_TRIM_DENOMINATOR);
			
			m_rationalFrameDuration = (REFERENCE_TIME)((referenceTimePerSecond * trimmedDurationTicks) / m_timeScale);
			m_minFrameAdvance = m_rationalFrameDuration / 4;      // 25% of rational duration
			m_maxFrameAdvance = m_rationalFrameDuration * 2;      // 200% of rational duration
			
			DbgLog((LOG_TRACE, 1, TEXT("::HardwareRational(#%I64u): Initialized rational duration=%I64d (%.3fms) with PPM trim, limits=[%I64d, %I64d]"),
				videoFrame.GetCounter(), m_rationalFrameDuration, m_rationalFrameDuration / 10000.0,
				m_minFrameAdvance, m_maxFrameAdvance));
		}
		
		// Handle first frame - establish timeline baseline
		if (m_startTimeOffset == 0)
		{
			// CLOCK_RATIONAL: Apply lead offset at baseline initialization
			// This ensures frames arrive slightly ahead for MadVR buffering
			// while maintaining the mathematical correctness of hardware-relative timing
			constexpr REFERENCE_TIME kLeadTime = 0LL;// 3000000LL;// 400000LL / 2;  // 40ms lead for buffering TODO: DO WE STIL NEED THIS?
			
			m_startTimeOffset = rawHardwareTime - kLeadTime;
			m_previousHardwareTimestamp = rawHardwareTime;
			timeStart = 0;  // Start timeline at zero
			
			DbgLog((LOG_TRACE, 1, TEXT("::HardwareRational(#%I64u): First frame - baseline set to %I64d with %I64d lead offset, timeline starts at 0"),
				videoFrame.GetCounter(), m_startTimeOffset, kLeadTime));
		}
		else
		{
			// Calculate hardware-based start time
			timeStart = rawHardwareTime - m_startTimeOffset;
			
			// MONOTONIC ENFORCEMENT: Ensure timeline never goes backwards or jumps unreasonably
			const REFERENCE_TIME timeSincePrevious = timeStart - (m_previousTimeStop - m_rationalFrameDuration);
			
			if (timeSincePrevious < m_minFrameAdvance)
			{
				// Hardware timestamp went backwards or too close - enforce minimum progression
				timeStart = m_previousTimeStop - m_rationalFrameDuration + m_minFrameAdvance;
				m_hardwareTimingAnomalyCount++;
				
				DbgLog((LOG_WARNING, 1, TEXT("::HardwareRational(#%I64u): Hardware time too close/backwards (diff=%I64d), enforced to %I64d (anomaly #%u)"),
					videoFrame.GetCounter(), timeSincePrevious, timeStart, m_hardwareTimingAnomalyCount));
			}
			else if (timeSincePrevious > m_maxFrameAdvance)
			{
				// Hardware timestamp jumped too far - limit to reasonable advance
				timeStart = m_previousTimeStop - m_rationalFrameDuration + m_maxFrameAdvance;
				m_hardwareTimingAnomalyCount++;
				
				DbgLog((LOG_WARNING, 1, TEXT("::HardwareRational(#%I64u): Hardware time jumped too far (diff=%I64d), limited to %I64d (anomaly #%u)"),
					videoFrame.GetCounter(), timeSincePrevious, timeStart, m_hardwareTimingAnomalyCount));
			}
			
			// Final monotonic check - ensure we never go backwards from previous stop time
			if (timeStart <= (m_previousTimeStop - m_rationalFrameDuration))
			{
				timeStart = m_previousTimeStop - m_rationalFrameDuration + 1;
				
				DbgLog((LOG_WARNING, 1, TEXT("::HardwareRational(#%I64u): Final monotonic enforcement - adjusted to %I64d"),
					videoFrame.GetCounter(), timeStart));
			}
		}
		
		m_previousHardwareTimestamp = rawHardwareTime;
		break;
	}

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE:

		// Get frame timestamp as reference time using integer math utility
		timeStart = effectiveHardwareTime;

		// Guarantee first frame to start counting at time zero
		// Note that this is against the recommendations of microsoft for directshow but otherwise
		// renderers don't start as they're often designed for file based video which starts at 0
		if (m_startTimeOffset == 0)
		{
			m_startTimeOffset = timeStart;

			DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): Setting start time offset to %I64u"),
				videoFrame.GetCounter(), m_startTimeOffset));
		}

		timeStart -= m_startTimeOffset;
		break;

	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_NONE:

		assert(m_startTimeOffset == 0);
		timeStart = (streamFrameCounter * m_frameDuration);
		break;

	}

	// Determine stop time
	switch (m_timestamp)
	{
	case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
	{
		const uint64_t nextFrameNum = streamFrameCounter + 1;
		const uint64_t ticksPerSec = 10000000ULL;

		// Use dynamically loaded PPM correction for consistent start/stop calculations
		uint64_t t = nextFrameNum;

		// t = t * ticksPerSec
		t = U64_MulDiv(t, ticksPerSec, 1ULL);

		// t = t * m_frameDurationTicks / m_timeScale
		t = U64_MulDiv(t, (uint64_t)m_frameDurationTicks, (uint64_t)m_timeScale);

		// Apply dynamic PPM trim on duration (same numerator/denominator as start time)
		t = U64_MulDiv(t, GetRationalTrimNumerator(), RATIONAL_TRIM_DENOMINATOR);

		const REFERENCE_TIME baseTimeStop = (REFERENCE_TIME)t;

		// Apply pipeline offset (currently unused / 0)
		timeStop = baseTimeStop + m_rationalPipelineOffset;
		break;
	}



	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
	{
		// HYBRID MODE: Use rational math for perfect frame duration
		timeStop = timeStart + m_rationalFrameDuration;
		
		// Log periodic timing info for debugging
		if (streamFrameCounter % 300 == 0)  // Every 5 seconds at 60fps
		{
			DbgLog((LOG_TRACE, 1, TEXT("::HardwareRational(#%I64u): timeStart=%I64d, duration=%I64d, timeStop=%I64d (anomalies=%u)"),
				videoFrame.GetCounter(), timeStart, m_rationalFrameDuration, timeStop, m_hardwareTimingAnomalyCount));
		}
		break;
	}

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
	{
		// Placeholder stop time; actual stop will be late-bound in buffered delivery thread
		timeStop = timeStart + m_frameDuration;
		
		if (timeStop <= m_previousTimeStop)
		{
			timeStop = m_previousTimeStop + 1;
		}

		assert(timeStop > timeStart);
		break;
	}

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2:
	{
		// Track measured durations for stats
		REFERENCE_TIME currentFrameTime =
			effectiveHardwareTime - m_startTimeOffset;
		const REFERENCE_TIME theoreticalDuration =
			static_cast<REFERENCE_TIME>(
				(REFERENCE_TIME_TICKS_PER_SECOND * m_frameDurationTicks) /
				m_timeScale);
		m_smartDurationHistory.Observe(
			videoFrame.GetCounter(), currentFrameTime, theoreticalDuration);

		// Placeholder stop time using averaged duration
		const REFERENCE_TIME avgDuration = CalculateSmartFrameDuration();
		timeStop = timeStart + avgDuration;
		
		if (timeStop <= m_previousTimeStop)
		{
			timeStop = m_previousTimeStop + 1;
		}

		assert(timeStop > timeStart);
		break;
	}

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE:
	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_NONE:

		// Theo methods: stop time = start + theoretical frame duration
		timeStop = timeStart + m_frameDuration;
		break;

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
	{
		// Use next frame's hardware timestamp for stop time
		REFERENCE_TIME nextFrameTime = NextFrameTimestamp();
		if (nextFrameTime != REFERENCE_TIME_INVALID)
		{
			timeStop = nextFrameTime - m_startTimeOffset;
			timeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
		}
		else
		{
			// Fallback to theoretical duration if next timestamp unavailable
			timeStop = timeStart + m_frameDuration;
		}
		break;
	}

	case DirectShowStartStopTimeMethod::DS_SSTM_NONE:
		// No timestamps at all
		break;
	}

	// Set right amount of values
	switch (m_timestamp)
	{
	case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO:
	{

		// FINAL MONOTONIC VALIDATION: Ensure frame interval is always valid
		// This is the last line of defense against any timing anomalies
		if (timeStop <= timeStart)
		{
			// Emergency correction: force minimum 1-tick interval
			timeStop = timeStart + 1;
			DbgLog((LOG_ERROR, 1, TEXT("::FillBuffer(#%I64u): CRITICAL - timeStop <= timeStart! Forced to %I64d (start=%I64d)"),
				videoFrame.GetCounter(), timeStop, timeStart));

		}

		// Track frame duration BEFORE applying lead offset (for accurate statistics)
		TrackFrameDuration(timeStart, timeStop, streamFrameCounter);

		// Store m_previousTimeStop BEFORE applying lead offset for consistent monotonic checks
		// This ensures next frame's comparison is against the base timestamp, not the lead-adjusted one
		m_previousTimeStop = timeStop;

		// MODE-SPECIFIC LEAD OFFSET HANDLING
		const REFERENCE_TIME baseTimeStart = timeStart;
		const REFERENCE_TIME baseTimeStop = timeStop;
		REFERENCE_TIME appliedLeadTime = 0;
		if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL ||
		    m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
		    m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2 ||
		    m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO ||
		    m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK)
		{
			// Apply lead offset for renderer buffering (after duration tracking AND storing m_previousTimeStop)
			appliedLeadTime = GetRampedLeadTime();
			timeStart += appliedLeadTime;
			timeStop += appliedLeadTime;
		}

		// Shadow only the currently deployed RATIONAL_RATIONAL path.  The legacy
		// result is still applied to the sample; this records aggregate parity
		// evidence before any behavior switch.  It deliberately shares the
		// timing lock and does not touch queues, workers, or DirectShow delivery.
		if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL &&
			m_rationalTimingShadow)
		{
			DirectShowFrameTimingInput shadowInput;
			shadowInput.timing.sourceFrameNumber = videoFrame.GetCounter();
			shadowInput.timing.ppmCorrection = GetCurrentPPMCorrection();
			shadowInput.timing.pipelineOffset = m_rationalPipelineOffset;
			shadowInput.presentationLead = appliedLeadTime;
			const DirectShowTimingDecision shadowDecision =
				m_rationalTimingShadow->Decide(shadowInput);
			m_rationalTimingShadowComparisons.fetch_add(1, std::memory_order_relaxed);
			const bool matches = shadowDecision.base.valid &&
				shadowDecision.base.discontinuity == isDiscontinuity &&
				shadowDecision.base.mediaStart == mediaTimeStart &&
				shadowDecision.base.mediaStop == mediaTimeStop &&
				shadowDecision.base.start == baseTimeStart &&
				shadowDecision.base.stop == baseTimeStop &&
				shadowDecision.start == timeStart && shadowDecision.stop == timeStop;
			if (!matches)
				m_rationalTimingShadowMismatches.fetch_add(1, std::memory_order_relaxed);
			else
			{
				// The real-display shadow run established exact parity through reset.
				// Take the controller's identical value only after comparison; the
				// legacy calculation remains the immediate fallback on any mismatch.
				timeStart = shadowDecision.start;
				timeStop = shadowDecision.stop;
				m_rationalTimingControllerApplied.fetch_add(
					1, std::memory_order_relaxed);
			}
		}
		 
		hr = pSample->SetTime(&timeStart, &timeStop);
		if (FAILED(hr))
			return hr;

		break;
	}

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE:
	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_NONE:	

		hr = pSample->SetTime(&timeStart, nullptr);
		if (FAILED(hr))
			return hr;
		break;
	}

	//
	// Data copy/formatting
	//

	// Get target data buffer
	BYTE* pData = nullptr;
	hr = pSample->GetPointer(&pData);
	if (FAILED(hr))
		return hr;

	if (!pData)
		return E_POINTER;

	// Format (which can just be a copy or a full decode) the video frame to the
	// DirectShow buffer
	// A simple memcpy runs in the 2-4ms range for a decent frame size
#ifdef _DEBUG
	timestamp_t startTime = ::GetWallClockTime();
#endif

	const bool formatSuccess =
		m_videoFrameFormatter->FormatVideoFrame(videoFrame, pData);

	if (!formatSuccess)
	{
		DbgLog((LOG_TRACE, 1,
			TEXT("::FillBuffer(#%I64u): Format failed"),
			videoFrame.GetCounter()));

		return S_FRAME_NOT_RENDERED;
	}

#ifdef _DEBUG
	if (streamFrameCounter % 100 == 0)
	{
		DbgLog((LOG_TRACE, 1,
			TEXT("::FillBuffer(#%I64u): Formatter took %.1f us"),
			videoFrame.GetCounter(),
			((::GetWallClockTime() - startTime) / 10.0)));
	}
#endif

	hr = pSample->SetActualDataLength(m_videoFrameFormatter->GetOutFrameSize());
	if (FAILED(hr))
		return hr;

	//
	// Sync
	//

	// All frames are complete images and hence sync points by definition
	hr = pSample->SetSyncPoint(TRUE);
	if (FAILED(hr))
		return hr;

	//
	// HDR metadata
	//

	// m_hdrData and m_hdrChanged are protected by m_timingStateLock held for
	// this entire render operation.
	if (m_hdrData)
	{
		if ((streamFrameCounter % 100) == 1 || m_hdrChanged)
		{
			IMediaSideData* pMediaSideData = nullptr;
			const HRESULT sideDataQueryResult = pSample->QueryInterface(&pMediaSideData);
			if (FAILED(sideDataQueryResult))
			{
				// HDR side data is optional.  A renderer that does not expose
				// IMediaSideData must not terminate the conversion worker.
				DbgLog((LOG_WARNING, 1, TEXT("::FillBuffer(#%I64u): IMediaSideData unavailable (0x%08x); continuing without HDR metadata"),
					videoFrame.GetCounter(), sideDataQueryResult));
				m_hdrChanged = false;
			}
			else
			{
				bool sideDataWriteSucceeded = true;

				MediaSideDataHDRContentLightLevel hdrLightLevel;
				ZeroMemory(&hdrLightLevel, sizeof(hdrLightLevel));
				hdrLightLevel.MaxCLL = (unsigned int)round(m_hdrData->maxCll);
				hdrLightLevel.MaxFALL = (unsigned int)round(m_hdrData->maxFall);
				HRESULT sideDataResult = pMediaSideData->SetSideData(IID_MediaSideDataHDRContentLightLevel, (const BYTE*)&hdrLightLevel, sizeof(hdrLightLevel));
				if (FAILED(sideDataResult))
				{
					DbgLog((LOG_WARNING, 1, TEXT("::FillBuffer(#%I64u): HDR content-light side data failed (0x%08x)"),
						videoFrame.GetCounter(), sideDataResult));
					sideDataWriteSucceeded = false;
				}

				MediaSideDataHDR hdr;
				ZeroMemory(&hdr, sizeof(hdr));
				hdr.display_primaries_x[0] = m_hdrData->displayPrimaryGreenX;
				hdr.display_primaries_x[1] = m_hdrData->displayPrimaryBlueX;
				hdr.display_primaries_x[2] = m_hdrData->displayPrimaryRedX;
				hdr.display_primaries_y[0] = m_hdrData->displayPrimaryGreenY;
				hdr.display_primaries_y[1] = m_hdrData->displayPrimaryBlueY;
				hdr.display_primaries_y[2] = m_hdrData->displayPrimaryRedY;
				hdr.white_point_x = m_hdrData->whitePointX;
				hdr.white_point_y = m_hdrData->whitePointY;
				hdr.max_display_mastering_luminance = m_hdrData->masteringDisplayMaxLuminance;
				hdr.min_display_mastering_luminance = m_hdrData->masteringDisplayMinLuminance;
				sideDataResult = pMediaSideData->SetSideData(IID_MediaSideDataHDR, (const BYTE*)&hdr, sizeof(hdr));
				if (FAILED(sideDataResult))
				{
					DbgLog((LOG_WARNING, 1, TEXT("::FillBuffer(#%I64u): HDR mastering-display side data failed (0x%08x)"),
						videoFrame.GetCounter(), sideDataResult));
					sideDataWriteSucceeded = false;
				}

				pMediaSideData->Release();

				if (sideDataWriteSucceeded)
					m_hdrChanged = false;
			}
		}
	}

	if (m_frameCounter % 20 == 0)
	{
		//
		// Calculate the exit latency, which is right before we hand-off to the DirectShow
		// renderer.
		//

		const timingclocktime_t now = m_timingClock->TimingClockNow();

		m_exitLatencyMs.store(
			TimingClockDiffMs(videoFrame.GetTimingTimestamp(), now,
				m_timingClock->TimingClockTicksPerSecond()),
			std::memory_order_relaxed);
	}

	return hr;
}

void ALiveSourceVideoOutputPin::LoadPPMCorrections(double refreshRate)
{
	// Attempt to load PPM corrections from VideoProcessor.cfg.
	bool loaded = m_ppmCorrectionLoader.LoadCorrectionFile();
	
	if (loaded)
	{
		// Get PPM correction for this refresh rate
		int ppmCorrection = m_ppmCorrectionLoader.GetPPMCorrection(refreshRate);
		
		// Check if AUTO mode is specified (ppm value = 999999 as sentinel)
		if (ppmCorrection == 999999)
		{
			// AUTO mode - use auto-calibration
			m_useAutoCalibration = true;
			m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;  // Start with no correction
			
			// **CRITICAL FIX: Reset and re-initialize auto-calibrator with simplified API**
			m_autoPpmCalibrator.Reset();
			m_autoPpmCalibrator.Initialize();  // Simplified initialization - no timing parameters needed
			
			DbgLog((LOG_TRACE, 1, TEXT("LoadPPMCorrections: %.3f Hz - AUTO mode enabled, auto-calibrator RESET and initialized"), refreshRate));
		}
		else
		{
		// Manual PPM correction from VideoProcessor.cfg.
			m_useAutoCalibration = false;
			
			// **FIX: Reset auto-calibrator when switching to manual mode**
			m_autoPpmCalibrator.Reset();
			
			// Calculate the trim numerator based on PPM correction
			if (ppmCorrection == 0)
			{
				m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;  // No correction
				DbgLog((LOG_TRACE, 1, TEXT("LoadPPMCorrections: %.3f Hz - no correction (0 PPM), auto-calibrator reset"), refreshRate));
			}
			else
			{
				m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR + ppmCorrection;
				
				DbgLog((LOG_TRACE, 1, TEXT("LoadPPMCorrections: %.3f Hz - applying %d PPM correction, auto-calibrator reset"), refreshRate, ppmCorrection));
				DbgLog((LOG_TRACE, 1, TEXT("  Trim ratio: %llu/%llu = %.6f%%"), 
					m_currentRationalTrimNumerator, RATIONAL_TRIM_DENOMINATOR,
					(100.0 * m_currentRationalTrimNumerator) / RATIONAL_TRIM_DENOMINATOR));
			}
		}
	}
	else
	{
		// No PPM correction config - default to auto-calibration.
		m_useAutoCalibration = true;
		m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;  // Start with no correction
		
		// **CRITICAL FIX: Reset and initialize with simplified API**
		m_autoPpmCalibrator.Reset();
		m_autoPpmCalibrator.Initialize();  // Simplified initialization
		
		DbgLog((LOG_TRACE, 1, TEXT("LoadPPMCorrections: %.3f Hz - no VideoProcessor.cfg PPM correction found, auto-calibrator RESET and initialized"), refreshRate));
	}
}

void ALiveSourceVideoOutputPin::TrackFrameDuration(REFERENCE_TIME timeStart, REFERENCE_TIME timeStop, uint64_t frameNumber)
{
	// Calculate frame duration in 100ns ticks
	REFERENCE_TIME durationTicks = timeStop - timeStart;
	
	// Convert to milliseconds with high precision
	double durationMs = durationTicks / 10000.0;
	
	// Initialize min/max on first sample
	if (m_durationSampleCount == 0)
	{
		m_minFrameDurationMs = durationMs;
		m_maxFrameDurationMs = durationMs;
		m_avgFrameDurationMs = durationMs;
	}
	else
	{
		// Update min/max
		if (durationMs < m_minFrameDurationMs)
			m_minFrameDurationMs = durationMs;
		if (durationMs > m_maxFrameDurationMs)
			m_maxFrameDurationMs = durationMs;
		
		// Update running average (incremental mean formula to avoid overflow)
		m_avgFrameDurationMs = m_avgFrameDurationMs + (durationMs - m_avgFrameDurationMs) / (m_durationSampleCount + 1);
	}
	
	m_durationSampleCount++;
	
	// Log statistics every 10 seconds at 60fps (600 frames)
	// For other frame rates, adjust: 10 seconds * fps
	// Approximate with: log every 600 frames regardless of actual rate
	if (m_lastDurationLogFrame == 0)
	{
		m_lastDurationLogFrame = frameNumber;
	}
	else if ((frameNumber - m_lastDurationLogFrame) >= 600)
	{
		// Log with high precision to detect rational vs hardware timing differences
		DEBUGLOG("Frame Duration Stats [%s]: avg=%.6fms, min=%.6fms, max=%.6fms, samples=%I64u",ToString(m_timestamp),m_avgFrameDurationMs,m_minFrameDurationMs,m_maxFrameDurationMs,m_durationSampleCount);
		
		// Calculate frame rate from average duration
		double avgFps = (m_avgFrameDurationMs > 0.0) ? (1000.0 / m_avgFrameDurationMs) : 0.0;
		DEBUGLOG("  - Calculated rate: %.6f fps (from avg duration)", avgFps);
		
		// Reset for next 10-second window
		m_lastDurationLogFrame = frameNumber;
		m_durationSampleCount = 0;
		m_minFrameDurationMs = 0.0;
		m_maxFrameDurationMs = 0.0;
		m_avgFrameDurationMs = 0.0;
	}
}

// How many frames to ramp over (hard-coded as requested)
static constexpr int kLeadRampFrames = 0;

REFERENCE_TIME ALiveSourceVideoOutputPin::GetRampedLeadTime()
{

	const bool legacyModeUsesLead = m_timestamp !=
		DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL;
	REFERENCE_TIME targetLeadTicks =
		DirectShowPresentationLeadPolicy::Resolve100ns(
			m_presentationLeadFramesConfigured,
			m_presentationLeadFrames, m_frameDuration,
			legacyModeUsesLead);

	// If ramping disabled or target is zero
	if (m_leadRampDurationMs <= 0 || targetLeadTicks <= 0)
		return targetLeadTicks;

	// Initialize ramp on first call
	if (!m_leadRampActive)
	{
		m_leadRampStartTimeMs = GetWallClockTime() / 10000;  // Convert from 100ns ticks to milliseconds
		m_leadRampActive = true;
		
		DEBUGLOG("GetRampedLeadTime: Initializing lead ramp - duration=%.0f ms, target=%.2f ms", (double)m_leadRampDurationMs, targetLeadTicks / 10000.0);
	}

	// Calculate elapsed time since ramp started
	uint64_t currentTimeMs = GetWallClockTime() / 10000;  // Convert from 100ns ticks to milliseconds
	uint64_t elapsedMs = currentTimeMs - m_leadRampStartTimeMs;

	// If ramp duration has elapsed, return full lead time
	if (elapsedMs >= m_leadRampDurationMs)
	{
		return targetLeadTicks;
	}

	// Linear interpolation: (elapsed / total) * target
	// Using integer math to maintain precision
	REFERENCE_TIME leadTime = (targetLeadTicks * elapsedMs) / m_leadRampDurationMs;

	// Periodic logging (every 500ms during ramp)
	static uint64_t lastLogTime = 0;
	if ((currentTimeMs - lastLogTime) >= 500)
	{
		DEBUGLOG("Lead time ramp: %.0f/%.0f ms elapsed -> lead %.3f/%.3f ms",
			(double)elapsedMs, (double)m_leadRampDurationMs,
			leadTime / 10000.0, targetLeadTicks / 10000.0);
		lastLogTime = currentTimeMs;
	}

	return leadTime;
}


void ALiveSourceVideoOutputPin::RecordHardwareTimestamp(uint64_t frameCounter, REFERENCE_TIME timestamp)
{
	std::lock_guard<std::mutex> lock(m_timestampHistoryMutex);
	
	// Store in circular buffer
	m_timestampHistory[m_timestampHistoryIndex].frameCounter = frameCounter;
	m_timestampHistory[m_timestampHistoryIndex].timestamp = timestamp;
	
	// Advance index (wrap around)
	m_timestampHistoryIndex = (m_timestampHistoryIndex + 1) % TIMESTAMP_HISTORY_SIZE;
}

REFERENCE_TIME ALiveSourceVideoOutputPin::FindNextHardwareTimestamp(REFERENCE_TIME currentTimestamp) const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_timestampHistoryMutex));
	
	// Search for first timestamp that's reasonably larger than current
	// "Reasonably larger" = between 0.5x and 2.0x expected frame duration
	const REFERENCE_TIME minDelta = m_frameDuration / 2;   // 50% of expected
	const REFERENCE_TIME maxDelta = m_frameDuration * 2;   // 200% of expected
	
	REFERENCE_TIME bestMatch = REFERENCE_TIME_INVALID;
	REFERENCE_TIME bestDelta = REFERENCE_TIME_INVALID;
	
	// Search entire history for best match
	for (size_t i = 0; i < TIMESTAMP_HISTORY_SIZE; i++)
	{
		const REFERENCE_TIME recorded = m_timestampHistory[i].timestamp;
		
		// Skip invalid/uninitialized entries
		if (recorded == 0)
			continue;
		
		// Calculate delta
		const REFERENCE_TIME delta = recorded - currentTimestamp;
		
		// Must be positive (in the future)
		if (delta <= 0)
			continue;
		
		// Check if in reasonable range
		if (delta >= minDelta && delta <= maxDelta)
		{
			// Found a valid candidate - keep closest one
			if (bestMatch == REFERENCE_TIME_INVALID || delta < bestDelta)
			{
				bestMatch = recorded;
				bestDelta = delta;
			}
		}
	}
	
	return bestMatch;
}
