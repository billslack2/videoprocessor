/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <cmath>  // For fabs()

#include <guid.h>
#include <IMediaSideData.h>

#include "ALiveSourceVideoOutputPin.h"


ALiveSourceVideoOutputPin::ALiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr):
	CBaseOutputPin(
		LIVE_SOURCE_FILTER_NAME, filter, pLock, phr,
		LIVE_SOURCE_FILTER_VIDEO_OUPUT_PIN_NAME)
{
	// All member initialization happens in the header with default values
}


void ALiveSourceVideoOutputPin::Initialize(
	IVideoFrameFormatter* const videoFrameFormatter,
	timestamp_t frameDuration,
	LONGLONG fpsNum,
	LONGLONG fpsDen,
	ITimingClock* const timingClock,
	DirectShowStartStopTimeMethod timestamp,
	const AM_MEDIA_TYPE& mediaType)
{
	if (!videoFrameFormatter)
		throw std::runtime_error("Cannot set null IVideoFrameFormatter");

	if (frameDuration <= 0)
		throw std::runtime_error("Duration must be > 0");
	assert(frameDuration > 50000LL); // 5ms frame is 200Hz, probably a reasonable upper bound
	assert(frameDuration < 10000000LL);  // 1Hz, reasonable lower bound

	m_videoFrameFormatter = videoFrameFormatter;
	m_frameDuration = frameDuration;
	m_fpsNum = fpsNum;
	m_fpsDen = fpsDen;
	m_timingClock = timingClock;
	m_timestamp = timestamp;
	m_mediaType = mediaType;
}


HRESULT ALiveSourceVideoOutputPin::GetMediaType(int iPosition, CMediaType* pmt)
{
	if (iPosition < 0)
		return E_INVALIDARG;
	if (iPosition > 0)
		return VFW_S_NO_MORE_ITEMS;

	pmt->Set(m_mediaType);

	return S_OK;
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

	HRESULT hr = NOERROR;
	*ppAlloc = nullptr;

	// get downstream prop request
	// the derived class may modify this in DecideBufferSize, but
	// we assume that he will consistently modify it the same way,
	// so we only get it once
	ALLOCATOR_PROPERTIES prop;
	ZeroMemory(&prop, sizeof(prop));

	pPin->GetAllocatorRequirements(&prop);

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


HRESULT ALiveSourceVideoOutputPin::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest)
{
	CheckPointer(pAlloc,E_POINTER);
	CheckPointer(ppropInputRequest,E_POINTER);

	// Safety check - this can be called before Initialize()
	if (!m_videoFrameFormatter)
	{
		DbgLog((LOG_ERROR, 1, TEXT("::DecideBufferSize(): m_videoFrameFormatter is null - called before Initialize()?")));
		return E_POINTER;
	}

	HRESULT hr = NOERROR;

	ppropInputRequest->cBuffers = 1;
	ppropInputRequest->cbBuffer = m_videoFrameFormatter->GetOutFrameSize();

	ASSERT(ppropInputRequest->cbBuffer);

	ALLOCATOR_PROPERTIES Actual;
	hr = pAlloc->SetProperties(ppropInputRequest,&Actual);
	if(FAILED(hr))
	{
		return hr;
	}

	if(Actual.cbBuffer < ppropInputRequest->cbBuffer)
	{
		return E_FAIL;
	}

	return S_OK;
}


//
// IAMPushSource
//


STDMETHODIMP ALiveSourceVideoOutputPin::GetMaxStreamOffset(REFERENCE_TIME *prtMaxOffset)
{
	*prtMaxOffset = 0;
	return S_OK;
}


STDMETHODIMP ALiveSourceVideoOutputPin::GetPushSourceFlags(ULONG *pFlags)
{
	// Return (* pFlags) = 0 [if this is a iAMPushSource]
	// https://docs.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-ireferenceclock
	*pFlags = 0;
	return S_OK;
}


STDMETHODIMP ALiveSourceVideoOutputPin::GetStreamOffset(REFERENCE_TIME *prtOffset)
{
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
	*prtLatency = 0;  // TODO: Just a guess
	return S_OK;
}


//
// IQualityControl
//


STDMETHODIMP ALiveSourceVideoOutputPin::Notify(IBaseFilter* pSender, Quality q)
{
	// TODO
	return S_OK;
}


HRESULT STDMETHODCALLTYPE ALiveSourceVideoOutputPin::SetSink(IQualityControl* piqc)
{
	// TODO
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

	m_hdrData = hdrData;
	m_hdrChanged = true;
}


void ALiveSourceVideoOutputPin::Reset()
{
	DbgLog((LOG_TRACE, 1, TEXT("ALiveSourceVideoOutputPin::Reset()")));

	// Only deliver flush if we're connected - prevents crashes during shutdown
	if (IsConnected())
	{
		if (FAILED(DeliverBeginFlush()))
		{
			DbgLog((LOG_ERROR, 1, TEXT("ALiveSourceVideoOutputPin::Reset(): DeliverBeginFlush failed")));
			// Don't throw - just log and continue with reset
		}
	}

	if (m_hdrData)
		m_hdrChanged = true;

	m_newSegment = true;

	m_frameCounter = 0;
	m_previousFrameCounter = 0;
	m_startTimeOffset = 0;
	m_frameCounterOffset = 0;
	m_previousTimeStop = 0;
	m_droppedFrameCount = 0;
	m_nextRationalTimeStart = 0;
	m_rationalRemainder = 0;  // Reset remainder accumulator

	// CRITICAL: Reset latency measurement to prevent stale values from affecting auto-tuning
	// After a reset (e.g., refresh rate change), the first few frames may have abnormal latency
	// Reset to 0 so auto-tuning doesn't react to stale measurements
	m_exitLatencyMs = 0.0;
	m_latencyMeasurementFrameCounter = 0;  // Mark measurement as stale

	// NOTE: We intentionally do NOT reset m_discontinuityCount and m_reAnchorCount here
	// These are cumulative diagnostic counters that should persist across resets
	// to help diagnose timeline stability issues over time.
	// Only reset the current drift value since we're starting fresh.
	m_timestampDriftMs = 0.0;

	// Only deliver flush if we're connected
	if (IsConnected())
	{
		if (FAILED(DeliverEndFlush()))
		{
			DbgLog((LOG_ERROR, 1, TEXT("ALiveSourceVideoOutputPin::Reset(): DeliverEndFlush failed")));
			// Don't throw - just log and continue
		}
	}
}


void ALiveSourceVideoOutputPin::UpdateFrameRate(LONGLONG fpsNum, LONGLONG fpsDen)
{
	DbgLog((LOG_TRACE, 1, TEXT("ALiveSourceVideoOutputPin::UpdateFrameRate(%lld/%lld)"),
		static_cast<LONGLONG>(fpsNum),
		static_cast<LONGLONG>(fpsDen)));

	if (fpsNum <= 0 || fpsDen <= 0)
		throw std::runtime_error("Invalid FPS parameters");

	// Simple atomic update - we don't need locks because:
	// 1. This is content switching, not VFR
	// 2. Brief interruption is acceptable
	// 3. LONGLONG writes are atomic on x64
	m_fpsNum = fpsNum;
	m_fpsDen = fpsDen;
	
	// Update the approximate frame duration (for fallback/logging)
	m_frameDuration = static_cast<timestamp_t>((10000000LL * fpsDen) / fpsNum);

	DbgLog((LOG_TRACE, 1, TEXT("ALiveSourceVideoOutputPin::UpdateFrameRate(): New frame duration = %lld (100ns units)"),
		static_cast<LONGLONG>(m_frameDuration)));
}


HRESULT ALiveSourceVideoOutputPin::RenderVideoFrameIntoSample(VideoFrame& videoFrame, IMediaSample* const pSample)
{
	// Defensive null checks - prevent access violation crashes
	if (!m_timingClock)
	{
		DbgLog((LOG_ERROR, 1, TEXT("::RenderVideoFrameIntoSample(): m_timingClock is null!")));
		return E_POINTER;
	}
	
	if (!m_videoFrameFormatter)
	{
		DbgLog((LOG_ERROR, 1, TEXT("::RenderVideoFrameIntoSample(): m_videoFrameFormatter is null!")));
		return E_POINTER;
	}

	assert(videoFrame.GetTimingTimestamp() > 0);
	assert(m_frameDuration > 0);
	assert(m_timingClock->TimingClockTicksPerSecond() > 0);

	++m_frameCounter;

	HRESULT hr;

	//
	// Media time
	//

	// Guarantee first frame to start counting at zero
	uint64_t streamFrameCounter = videoFrame.GetCounter();
	if (m_frameCounterOffset == 0)
		m_frameCounterOffset = streamFrameCounter;
	streamFrameCounter -= m_frameCounterOffset;

	// Set frame counter
	LONGLONG mediaTimeStart = streamFrameCounter;
	LONGLONG mediaTimeStop = mediaTimeStart + 1;
	hr = pSample->SetMediaTime(&mediaTimeStart, &mediaTimeStop);
	if (FAILED(hr))
		return hr;

	// Discontinuity check
	const bool isDiscontinuity =
		videoFrame.GetCounter() != (m_previousFrameCounter + 1) ||
		m_frameCounter == 1;
	if (isDiscontinuity)
	{
		// Track discontinuity for diagnostics
		if (m_frameCounter > 1)  // Don't count first frame
			++m_discontinuityCount;

		DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): Frame counter jumped from %I64u (stream frame %I64u), discontinuity detected"),
			videoFrame.GetCounter(), m_previousFrameCounter, streamFrameCounter));

		hr = pSample->SetDiscontinuity(TRUE);
		if (FAILED(hr))
			return hr;
	}

	m_previousFrameCounter = videoFrame.GetCounter();

	//
	// Setting the time
	//

	REFERENCE_TIME timeStart = REFERENCE_TIME_INVALID;
	REFERENCE_TIME timeStop = REFERENCE_TIME_INVALID;
	REFERENCE_TIME timeStartRaw = REFERENCE_TIME_INVALID;  // Raw timestamp before offset correction

	// Determine start time
	switch (m_timestamp)
	{
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE:

		// Get frame timestamp as reference time using integer arithmetic
		{
			const timingclocktime_t frameTicks = videoFrame.GetTimingTimestamp();
			const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();
			
			timeStartRaw = (REFERENCE_TIME)((frameTicks * 10000000LL) / ticksPerSecond);
			timeStart = timeStartRaw;
		}

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

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:

		// **CRITICAL: For CLOCK_RATIONAL, capture RAW hardware timestamp BEFORE any offset correction**
		// This ensures the rational timeline anchor is completely independent of frame offset
		{
			const timingclocktime_t frameTicks = videoFrame.GetTimingTimestamp();
			const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();
			
			// This is the RAW hardware timestamp - no offset applied yet
			timeStartRaw = (REFERENCE_TIME)((frameTicks * 10000000LL) / ticksPerSecond);
		}

		// On first frame, capture BOTH the raw anchor point AND the normalization offset
		if (m_startTimeOffset == 0)
		{
			m_startTimeOffset = timeStartRaw;  // Store for normalization

			DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_RATIONAL - captured raw anchor %I64d"),
				videoFrame.GetCounter(), m_startTimeOffset));
		}

		// Now normalize to start near zero (this is ONLY for DirectShow compatibility)
		// The rational timeline will use the raw anchor internally
		timeStart = timeStartRaw - m_startTimeOffset;
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
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:

		timeStop = NextFrameTimestamp();
		if (timeStop == REFERENCE_TIME_INVALID)
		{
			// Use rational arithmetic if available for exact frame duration
			if (m_fpsNum > 0 && m_fpsDen > 0)
			{
				// Calculate exact duration: (10,000,000 * fpsDen) / fpsNum
				const LONGLONG exactDuration = (10000000LL * m_fpsDen) / m_fpsNum;
				timeStop = timeStart + exactDuration;
			}
			else
			{
				timeStop = timeStart + m_frameDuration;
			}
		}
		else
		{
			assert(m_startTimeOffset > 0);
			timeStop -= m_startTimeOffset;
		}

		assert(timeStop > timeStart);
		break;

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO:

		// Use rational arithmetic if available for monotonic, exact timestamps
		if (m_fpsNum > 0 && m_fpsDen > 0)
		{
			// Calculate exact duration: (10,000,000 * fpsDen) / fpsNum
			const LONGLONG exactDuration = (10000000LL * m_fpsDen) / m_fpsNum;
			timeStop = timeStart + exactDuration;
		}
		else
		{
			timeStop = timeStart + m_frameDuration;
		}
		break;

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:

		// CLOCK_RATIONAL: Ultra-smooth drift correction without frame drops
		// 
		// DESIGN PRINCIPLES:
		// 1. Timestamps MUST be monotonically increasing (DirectShow requirement)
		// 2. Use EXACT rational arithmetic to eliminate integer truncation drift
		// 3. Apply gentle proportional correction for hardware clock drift
		// 4. Never re-anchor backwards (would violate monotonicity)
		//
		// Fallback to simple duration if FPS not available
		if (m_fpsNum <= 0 || m_fpsDen <= 0)
		{
			// No rational FPS available - use simple duration like other modes
			timeStop = timeStart + m_frameDuration;
			break;
		}

		{
			// EXACT RATIONAL ARITHMETIC
			// For 59.94Hz (60000/1001): duration = 10,000,000 * 1001 / 60000
			// Integer division loses the remainder, causing drift over time.
			// 
			// Solution: Track the remainder and add 1 unit when it accumulates.
			// This is called "Bresenham's line algorithm" approach.
			//
			// Example for 59.94Hz:
			//   numerator = 10,000,000 * 1001 = 10,010,000,000
			//   base = 10,010,000,000 / 60000 = 166,833
			//   remainder = 10,010,000,000 % 60000 = 20,000
			//   Every frame adds 20,000 to accumulator
			//   When accumulator >= 60000, add 1 to duration and subtract 60000
			//   This happens every 3 frames (60000/20000 = 3)
			
			const LONGLONG numerator = 10000000LL * m_fpsDen;
			const LONGLONG baseDuration = numerator / m_fpsNum;
			const LONGLONG remainder = numerator % m_fpsNum;
			
			// On first frame, anchor to hardware clock
			if (m_newSegment)
			{
				m_nextRationalTimeStart = timeStart;
				m_rationalRemainder = 0;  // Reset remainder accumulator
				m_newSegment = false;
				m_timestampDriftMs = 0.0;
				
				DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_RATIONAL initial anchor to %I64d (base=%I64d, rem=%I64d/%I64d)"),
					videoFrame.GetCounter(), m_nextRationalTimeStart, baseDuration, remainder, m_fpsNum));
					
				// Use base duration for first frame (no remainder yet)
				timeStart = m_nextRationalTimeStart;
				timeStop = timeStart + baseDuration;
				m_rationalRemainder = remainder;  // Start accumulating
				m_nextRationalTimeStart = timeStop;
			}
			else if (isDiscontinuity)
			{
				// For actual frame drops (discontinuity), handle carefully
				// CRITICAL: Never go backwards - only re-anchor if hardware is AHEAD
				++m_reAnchorCount;
				
				if (timeStart > m_nextRationalTimeStart)
				{
					// Hardware clock jumped forward - safe to follow
					m_nextRationalTimeStart = timeStart;
					m_rationalRemainder = 0;  // Reset remainder on re-anchor
					DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_RATIONAL discontinuity - re-anchor forward to %I64d"),
						videoFrame.GetCounter(), m_nextRationalTimeStart));
				}
				else
				{
					// Hardware clock is behind our timeline - maintain monotonicity
					DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_RATIONAL discontinuity - keeping at %I64d (hw=%I64d, diff=%.2fms)"),
						videoFrame.GetCounter(), m_nextRationalTimeStart, timeStart,
						static_cast<double>(m_nextRationalTimeStart - timeStart) / 10000.0));
				}
				m_timestampDriftMs = 0.0;
				
				// Use base duration after discontinuity
				timeStart = m_nextRationalTimeStart;
				timeStop = timeStart + baseDuration;
				m_rationalRemainder = remainder;
				m_nextRationalTimeStart = timeStop;
			}
			else
			{
				// Normal case: Use exact rational duration with remainder tracking
				
				// Calculate this frame's exact duration using Bresenham-style accumulation
				LONGLONG exactDuration = baseDuration;
				m_rationalRemainder += remainder;
				if (m_rationalRemainder >= m_fpsNum)
				{
					exactDuration += 1;  // Add one 100ns unit
					m_rationalRemainder -= m_fpsNum;
				}
				
				// Calculate current drift between our timeline and hardware
				// Positive drift = our timeline is ahead of hardware
				// Negative drift = our timeline is behind hardware
				const LONGLONG drift = m_nextRationalTimeStart - timeStart;
				m_timestampDriftMs = static_cast<double>(drift) / 10000.0;
				
				// GENTLE PROPORTIONAL CORRECTION for hardware clock drift
				// The remainder tracking eliminates mathematical drift, but the hardware
				// clock may still drift slightly. Apply tiny corrections.
				//
				// Correction rate: 0.5% of drift per frame (1/200)
				// This is faster than before (was 0.1%) to respond to real drift
				// At 60fps, this is ~30% correction per second
				
				LONGLONG correction = drift / 200;  // 0.5% of total drift
				
				// Clamp correction to max ±0.5ms per frame (prevents instability)
				const LONGLONG maxCorrection = 5000;  // 0.5ms in 100ns units
				if (correction > maxCorrection) correction = maxCorrection;
				if (correction < -maxCorrection) correction = -maxCorrection;
				
				// Apply correction to duration
				LONGLONG adjustedDuration = exactDuration - correction;
				
				// Sanity bounds: duration must be between 95% and 105% of nominal
				const LONGLONG minDuration = (baseDuration * 95) / 100;
				const LONGLONG maxDuration = (baseDuration * 105) / 100;
				if (adjustedDuration < minDuration) adjustedDuration = minDuration;
				if (adjustedDuration > maxDuration) adjustedDuration = maxDuration;
				
				// Set timestamps from our rational timeline
				timeStart = m_nextRationalTimeStart;
				timeStop = timeStart + adjustedDuration;
				
				// MONOTONICITY CHECK
				assert(timeStop > timeStart);
				
				// Advance timeline for next frame
				m_nextRationalTimeStart = timeStop;
				
				// Log every 5 seconds or on significant drift
				if (m_frameCounter % 300 == 0 || 
				    (m_frameCounter % 60 == 0 && (m_timestampDriftMs > 0.5 || m_timestampDriftMs < -0.5)))
				{
					DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_RATIONAL drift=%.3fms, correction=%.1fus, rem=%I64d/%I64d"),
						videoFrame.GetCounter(), m_timestampDriftMs, static_cast<double>(correction) / 10.0,
						m_rationalRemainder, m_fpsNum));
				}
			}
		}
		break;

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:

		timeStop = NextFrameTimestamp();
		assert(timeStop != REFERENCE_TIME_INVALID);
		assert(timeStop > timeStart);

		assert(m_startTimeOffset > 0);
		timeStop -= m_startTimeOffset;
		break;
	}

	// Set right amount of values
	switch (m_timestamp)
	{
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO:

		hr = pSample->SetTime(&timeStart, &timeStop);
		if (FAILED(hr))
			return hr;

#ifdef _DEBUG
		// Every n frames output a bunch of consecutive frames to check start/stop for all applicable formats
		if (m_frameCounter % 200 < 5)
		{
			const double durationMs = (timeStop - timeStart) / 10000.0;
			const double diffStopMs = (timeStart - m_previousTimeStop) / 10000.0;

			DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): StartTS: %I64d StopTS: %I64d, duration: %.02f, diffPrevStopStart: %.02f"),
				videoFrame.GetCounter(), timeStart, timeStop, durationMs, diffStopMs));

			m_previousTimeStop = timeStop;
		}
#endif // _DEBUG
		break;

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

	assert(pData);

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

	// Note: This can be updatedcalled from a different thread, can go wrong but never saw
	//       it happen so leaving this as-is.
	if (m_hdrData)
	{
		if ((streamFrameCounter % 100) == 1 || m_hdrChanged)
		{
			IMediaSideData* pMediaSideData = nullptr;
			if (FAILED(pSample->QueryInterface(&pMediaSideData)))
				throw std::runtime_error("Failed to get IMediaSideData");

			MediaSideDataHDRContentLightLevel hdrLightLevel;
			ZeroMemory(&hdrLightLevel, sizeof(hdrLightLevel));
			hdrLightLevel.MaxCLL = (unsigned int)round(m_hdrData->maxCll);
			hdrLightLevel.MaxFALL = (unsigned int)round(m_hdrData->maxFall);
			pMediaSideData->SetSideData(IID_MediaSideDataHDRContentLightLevel, (const BYTE*)&hdrLightLevel, sizeof(hdrLightLevel));

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
			pMediaSideData->SetSideData(IID_MediaSideDataHDR, (const BYTE*)&hdr, sizeof(hdr));

			pMediaSideData->Release();

			m_hdrChanged = false;
		}
	}

	if (m_frameCounter % 20 == 0)
	{
		//
		// Calculate the exit latency, which is right before we hand-off to the DirectShow
		// renderer.
		//

		const timingclocktime_t now = m_timingClock->TimingClockNow();

		m_exitLatencyMs = TimingClockDiffMs(
			videoFrame.GetTimingTimestamp(), now, m_timingClock->TimingClockTicksPerSecond());
		
		// Mark when this measurement was taken so auto-tuning can verify freshness
		m_latencyMeasurementFrameCounter = m_frameCounter;
	}

	return hr;
}
