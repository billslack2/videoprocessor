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

	// Initialize hardware timer frequency for CLOCK_PLL mode
	// QueryPerformanceFrequency returns ticks per second
	LARGE_INTEGER freq;
	if (QueryPerformanceFrequency(&freq))
	{
		m_pllClock.hwTimerFrequency = freq.QuadPart;
		DbgLog((LOG_TRACE, 1, TEXT("ALiveSourceVideoOutputPin::Initialize(): Hardware timer frequency = %I64d Hz"),
			m_pllClock.hwTimerFrequency));
	}
	else
	{
		m_pllClock.hwTimerFrequency = 0;
		DbgLog((LOG_ERROR, 1, TEXT("ALiveSourceVideoOutputPin::Initialize(): QueryPerformanceFrequency failed!")));
	}

	// Compute nominal frame period in 100ns units for CLOCK_PLL
	if (fpsNum > 0 && fpsDen > 0)
	{
		m_pllClock.nominalPeriod100ns = (10000000LL * fpsDen) / fpsNum;
		DbgLog((LOG_TRACE, 1, TEXT("ALiveSourceVideoOutputPin::Initialize(): Nominal period = %I64d (100ns units) for %I64d/%I64d fps"),
			m_pllClock.nominalPeriod100ns, fpsNum, fpsDen));
	}
	else
	{
		m_pllClock.nominalPeriod100ns = frameDuration;
	}
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

	// Reset CLOCK_PLL state
	m_pllClock.initialized = false;
	m_pllClock.estimatedPeriodDouble = static_cast<double>(m_pllClock.nominalPeriod100ns);
	m_pllClock.estimatedPeriod100ns = m_pllClock.nominalPeriod100ns;
	m_pllClock.baseTimestamp = 0;
	m_pllClock.baseFrameIndex = 0;
	m_pllClock.lastHwTimerValue = 0;
	m_pllClock.lastFrameIndex = 0;
	m_pllClock.phaseErrorAccum = 0.0;
	m_pllClock.lastGeneratedTimestamp = 0;
	m_pllClock.lastFrameTimestamp = 0;
	m_pllClock.measurementBaseTimestamp = 0;
	m_pllClock.measurementBaseFrameIndex = 0;
	m_pllClock.measurementFrameCount = 0;

	// CRITICAL: Reset latency measurement to prevent stale values from affecting auto-tuning
	// After a reset (e.g., refresh rate change), the first few frames may have abnormal latency
	// Reset to 0 so auto-tuning doesn't react to stale measurements
	m_exitLatencyMs = 0.0;
	m_latencyMeasurementFrameCounter = 0;  // Mark measurement as stale

	// Reset average frame rate tracking - will restart after warm-up period
	m_avgRateStartTime = 0;
	m_avgRateLastTime = 0;
	m_avgRateFrameCount = 0;

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

	// Average frame rate tracking - uses FRAME timestamps for accuracy
	// This measures the TRUE capture rate, not affected by GUI thread timing
	const uint64_t AVG_RATE_START_FRAME = 300;
	const timingclocktime_t frameTimestamp = videoFrame.GetTimingTimestamp();
	
	if (m_frameCounter == AVG_RATE_START_FRAME)
	{
		// Start measurement at frame 300
		m_avgRateStartTime = frameTimestamp;
		m_avgRateLastTime = frameTimestamp;
		m_avgRateFrameCount = 0;  // Will be incremented below
		
		DbgLog((LOG_TRACE, 1, TEXT("::RenderVideoFrameIntoSample(#%I64u): Starting average frame rate measurement at frame_ts=%I64d"),
			videoFrame.GetCounter(), frameTimestamp));
	}
	else if (m_frameCounter > AVG_RATE_START_FRAME)
	{
		// Update last frame timestamp and increment count
		m_avgRateLastTime = frameTimestamp;
		++m_avgRateFrameCount;
		
		// Periodically log for debugging
		if (m_frameCounter % 300 == 0)
		{
			const timingclocktime_t elapsedTicks = m_avgRateLastTime - m_avgRateStartTime;
			const double elapsedSeconds = static_cast<double>(elapsedTicks) / 1000000.0;  // DeckLink is microseconds
			const double avgRate = (elapsedSeconds > 0) ? 
				static_cast<double>(m_avgRateFrameCount) / elapsedSeconds : 0.0;
			
			DbgLog((LOG_TRACE, 1, TEXT("::RenderVideoFrameIntoSample(#%I64u): Avg Rate Debug - frames=%I64u, elapsed_ticks=%I64d, elapsed_sec=%.6f, avg_rate=%.6f Hz"),
				videoFrame.GetCounter(), m_avgRateFrameCount, elapsedTicks, elapsedSeconds, avgRate));
		}
	}

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
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_PLL:

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

		// CLOCK_SMART: Uses next frame's actual timestamp when available (from queue),
		// otherwise falls back to rational duration calculation with Bresenham remainder tracking.
		//
		// This combines the best of both worlds:
		// - When queue has next frame: uses actual hardware timestamps (most accurate)
		// - When queue is empty: uses Bresenham-style rational timing (no drift)
		
		timeStop = NextFrameTimestamp();
		if (timeStop == REFERENCE_TIME_INVALID)
		{
			// No next frame available - use rational arithmetic with remainder tracking
			if (m_fpsNum > 0 && m_fpsDen > 0)
			{
				// Bresenham-style exact duration calculation
				const LONGLONG numerator = 10000000LL * m_fpsDen;
				const LONGLONG baseDuration = numerator / m_fpsNum;
				const LONGLONG remainder = numerator % m_fpsNum;
				
				// Calculate exact duration for this frame
				LONGLONG exactDuration = baseDuration;
				m_rationalRemainder += remainder;
				if (m_rationalRemainder >= m_fpsNum)
				{
					exactDuration += 1;  // Add one 100ns unit
					m_rationalRemainder -= m_fpsNum;
				}
				
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
			
			// Reset remainder when we get actual timestamps (re-sync with hardware)
			m_rationalRemainder = 0;
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
			//
			// DECKLINK HARDWARE COMPENSATION: Apply 1.000000668x timing correction
			// to compensate for Decklink hardware timing drift.
			// Formula: numerator = (10,000,000 * fpsDen * 1000000668) / 1000000000
			
			const LONGLONG numerator = (10000000LL * m_fpsDen * 1000000668LL) / 1000000000LL;
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
				
				// ADAPTIVE PROPORTIONAL CORRECTION for hardware clock drift
				// Use aggressive correction for the first ~2 seconds (120 frames at 60Hz)
				// to quickly settle after refresh rate changes, then switch to gentle correction
				// for steady-state stability.
				//
				// First 120 frames: 5% correction per frame (~97% settled in 1 second)
				// After 120 frames: 0.5% correction per frame (gentle steady-state)
				
				LONGLONG correction;
				LONGLONG maxCorrection;
				
				if (m_frameCounter <= 120)
				{
					// FAST SETTLING: 5% per frame for first 2 seconds
					correction = drift / 20;  // 5% of total drift
					maxCorrection = 50000;    // 5ms max per frame during settling
				}
				else
				{
					// STEADY STATE: 0.5% per frame
					correction = drift / 200;  // 0.5% of total drift
					maxCorrection = 5000;      // 0.5ms max per frame
				}
				
				// Clamp correction
				if (correction > maxCorrection) correction = maxCorrection;
				if (correction < -maxCorrection) correction = -maxCorrection;
				
				// Apply correction to duration
				LONGLONG adjustedDuration = exactDuration - correction;
				
				// Sanity bounds: duration must be between 90% and 110% of nominal during settling,
				// tighter 95%-105% bounds in steady state
				LONGLONG minDuration, maxDuration;
				if (m_frameCounter <= 120)
				{
					minDuration = (baseDuration * 90) / 100;
					maxDuration = (baseDuration * 110) / 100;
				}
				else
				{
					minDuration = (baseDuration * 95) / 100;
					maxDuration = (baseDuration * 105) / 100;
				}
				if (adjustedDuration < minDuration) adjustedDuration = minDuration;
				if (adjustedDuration > maxDuration) adjustedDuration = maxDuration;
				
				// Set timestamps from our rational timeline
				timeStart = m_nextRationalTimeStart;
				timeStop = timeStart + adjustedDuration;
				
				// MONOTONICITY CHECK
				assert(timeStop > timeStart);
				
				// Advance timeline for next frame
				m_nextRationalTimeStart = timeStop;
				
				// Log during settling or on significant drift
				if (m_frameCounter <= 120 || m_frameCounter % 300 == 0 || 
				    (m_frameCounter % 60 == 0 && (m_timestampDriftMs > 0.5 || m_timestampDriftMs < -0.5)))
				{
					DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_RATIONAL drift=%.3fms, correction=%.1fus, rem=%I64d/%I64d%s"),
						videoFrame.GetCounter(), m_timestampDriftMs, static_cast<double>(correction) / 10.0,
						m_rationalRemainder, m_fpsNum,
						m_frameCounter <= 120 ? TEXT(" [SETTLING]") : TEXT("")));
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

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_PLL:

		// CLOCK_PLL: Phase-Locked Loop - dynamically estimate frame period from hardware frame timestamps
		// 
		// DESIGN PRINCIPLES:
		// 1. Track actual capture hardware clock rate using FRAME timestamps (not QPC!)
		// 2. Measure period over multiple frames (15) for stable readings
		// 3. Use exponential moving average to smooth period estimates
		// 4. Apply gentle phase correction to prevent long-term drift
		// 5. Clamp estimate within ±200 ppm of nominal to prevent runaway
		// 6. Ensure monotonically increasing timestamps
		//
		{
			const timingclocktime_t currentFrameTimestamp = videoFrame.GetTimingTimestamp();
			const uint64_t currentFrameIndex = videoFrame.GetCounter();
			const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();

			// Initialize on first frame
			if (!m_pllClock.initialized)
			{
				m_pllClock.initialized = true;
				m_pllClock.estimatedPeriodDouble = static_cast<double>(m_pllClock.nominalPeriod100ns);
				m_pllClock.estimatedPeriod100ns = m_pllClock.nominalPeriod100ns;
				m_pllClock.baseTimestamp = timeStart;
				m_pllClock.baseFrameIndex = currentFrameIndex;
				m_pllClock.lastFrameTimestamp = currentFrameTimestamp;
				m_pllClock.lastFrameIndex = currentFrameIndex;
				m_pllClock.phaseErrorAccum = 0.0;
				m_pllClock.lastGeneratedTimestamp = timeStart;
				
				// Initialize multi-frame measurement window
				m_pllClock.measurementBaseTimestamp = currentFrameTimestamp;
				m_pllClock.measurementBaseFrameIndex = currentFrameIndex;
				m_pllClock.measurementFrameCount = 0;

				DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_PLL initialized - nominal period=%I64d (100ns), base=%I64d"),
					videoFrame.GetCounter(), m_pllClock.nominalPeriod100ns, m_pllClock.baseTimestamp));

				// First frame: use nominal period
				timeStop = timeStart + m_pllClock.estimatedPeriod100ns;
			}
			else
			{
				// Increment measurement frame counter
				++m_pllClock.measurementFrameCount;

				// Only update period estimate every PLL_MEASUREMENT_FRAMES frames
				// This averages over ~250ms at 60Hz for stable readings
				if (m_pllClock.measurementFrameCount >= PLL_MEASUREMENT_FRAMES)
				{
					// Calculate deltas over the measurement window
					const timingclocktime_t timestampDelta = currentFrameTimestamp - m_pllClock.measurementBaseTimestamp;
					const int64_t frameIndexDelta = static_cast<int64_t>(currentFrameIndex) - 
					                                 static_cast<int64_t>(m_pllClock.measurementBaseFrameIndex);

					// Only process valid forward progress
					if (timestampDelta > 0 && frameIndexDelta > 0)
					{
						// Convert frame timestamp delta to 100ns units (as double for precision)
						// timestampDelta is in hardware clock ticks (microseconds for DeckLink)
						const double timerDelta100ns = (static_cast<double>(timestampDelta) * 10000000.0) / 
						                                static_cast<double>(ticksPerSecond);
						
						// Measured frame period in 100ns units - averaged over multiple frames!
						const double measuredPeriod = timerDelta100ns / static_cast<double>(frameIndexDelta);
						const double measuredHz = 10000000.0 / measuredPeriod;

						// Check if measurement is within acceptable range (outlier rejection)
						const double nominalPeriod = static_cast<double>(m_pllClock.nominalPeriod100ns);
						const double minPeriod = nominalPeriod * (1.0 - PLL_OUTLIER_THRESHOLD);
						const double maxPeriod = nominalPeriod * (1.0 + PLL_OUTLIER_THRESHOLD);

						if (measuredPeriod >= minPeriod && measuredPeriod <= maxPeriod)
						{
							// Valid measurement - apply exponential moving average
							const double alpha = PLL_ALPHA;
							const double newEstimate = (1.0 - alpha) * m_pllClock.estimatedPeriodDouble +
							                           alpha * measuredPeriod;

							// Limit maximum change per update (prevents jumps)
							const double maxChange = m_pllClock.estimatedPeriodDouble * PLL_MAX_PERIOD_CHANGE;
							double change = newEstimate - m_pllClock.estimatedPeriodDouble;
							if (change > maxChange) change = maxChange;
							if (change < -maxChange) change = -maxChange;

							// Update the double-precision estimate
							double updatedEstimate = m_pllClock.estimatedPeriodDouble + change;
							
							// CLAMP within ±PLL_MAX_DEVIATION_PPM of nominal
							// This prevents runaway estimates from bad measurements
							const double maxDeviationFactor = PLL_MAX_DEVIATION_PPM / 1000000.0;
							const double minAllowed = nominalPeriod * (1.0 - maxDeviationFactor);
							const double maxAllowed = nominalPeriod * (1.0 + maxDeviationFactor);
							if (updatedEstimate < minAllowed) updatedEstimate = minAllowed;
							if (updatedEstimate > maxAllowed) updatedEstimate = maxAllowed;
							
							m_pllClock.estimatedPeriodDouble = updatedEstimate;
							
							// Update the integer version for timestamp generation (rounded)
							m_pllClock.estimatedPeriod100ns = static_cast<LONGLONG>(m_pllClock.estimatedPeriodDouble + 0.5);

							// Log every measurement update
							const double estimatedHz = 10000000.0 / m_pllClock.estimatedPeriodDouble;
							const double nominalHz = 10000000.0 / nominalPeriod;
							const double driftPpm = ((estimatedHz - nominalHz) / nominalHz) * 1000000.0;

							DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_PLL update - measured=%.4fHz, filtered=%.4fHz, drift=%.1f ppm, period=%.2f (over %I64u frames)"),
								videoFrame.GetCounter(), measuredHz, estimatedHz, driftPpm, 
								m_pllClock.estimatedPeriodDouble, frameIndexDelta));
						}
						else
						{
							// Outlier - log but don't update estimate
							DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): CLOCK_PLL outlier rejected - measured=%.4fHz (%.2f period), range=[%.2f,%.2f]"),
								videoFrame.GetCounter(), measuredHz, measuredPeriod, minPeriod, maxPeriod));
						}
					}

					// Reset measurement window for next batch
					m_pllClock.measurementBaseTimestamp = currentFrameTimestamp;
					m_pllClock.measurementBaseFrameIndex = currentFrameIndex;
					m_pllClock.measurementFrameCount = 0;
				}

				// Compute phase error and apply gentle correction (every frame)
				// Phase error = predicted timestamp vs actual hardware time
				const uint64_t frameOffset = currentFrameIndex - m_pllClock.baseFrameIndex;
				const REFERENCE_TIME predictedTimestamp = m_pllClock.baseTimestamp + 
					static_cast<LONGLONG>(static_cast<double>(frameOffset) * m_pllClock.estimatedPeriodDouble);
				
				const double phaseError = static_cast<double>(predictedTimestamp - timeStart);

				// Accumulate phase error with very small correction factor
				m_pllClock.phaseErrorAccum = (1.0 - PLL_PHASE_ALPHA) * m_pllClock.phaseErrorAccum +
				                                   PLL_PHASE_ALPHA * phaseError;

				// Apply small phase correction to base timestamp (prevents long-term drift)
				const double phaseCorrection = m_pllClock.phaseErrorAccum * 0.01;  // 1% of accumulated error
				m_pllClock.baseTimestamp -= static_cast<LONGLONG>(phaseCorrection);

				// Update last frame tracking
				m_pllClock.lastFrameTimestamp = currentFrameTimestamp;
				m_pllClock.lastFrameIndex = currentFrameIndex;

				// Generate timestamp using integer period (for DirectShow compatibility)
				timeStop = timeStart + m_pllClock.estimatedPeriod100ns;

				// Track generated timestamp for monotonicity check next frame
				m_pllClock.lastGeneratedTimestamp = timeStart;

				// Track drift for diagnostics
				m_timestampDriftMs = static_cast<double>(timeStart - (timeStartRaw - m_startTimeOffset)) / 10000.0;
			}
		}
		break;
	}

	// Set right amount of values
	switch (m_timestamp)
	{
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_PLL:
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
