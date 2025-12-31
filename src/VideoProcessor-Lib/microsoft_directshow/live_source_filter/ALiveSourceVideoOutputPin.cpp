/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <guid.h>
#include <IMediaSideData.h>
#include <DebugLog.h>
#include <PPMCorrectionLoader.h>

#include "ALiveSourceVideoOutputPin.h"

#include <intrin.h>
#pragma intrinsic(_umul128)

#include <intrin.h>

static inline uint64_t U64_MulDiv(uint64_t a, uint64_t b, uint64_t div)
{
	if (div == 0) return 0;
	uint64_t hi = 0;
	uint64_t lo = _umul128(a, b, &hi);
	return _udiv128(hi, lo, div, nullptr);
}

// Computes:
// floor( frameIndex * 10,000,000 * frameDurationTicks / timeScale * trimNum / trimDen )
static inline uint64_t RationalTimestampTrimmed(
	uint64_t frameIndex,
	uint64_t frameDurationTicks,
	uint64_t timeScale,
	uint64_t trimNum,
	uint64_t trimDen)
{
	constexpr uint64_t ticksPerSec = 10000000ULL;

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

	if (frameDuration <= 0)
		throw std::runtime_error("Duration must be > 0");
	assert(frameDuration > 50000LL); // 5ms frame is 200Hz, probably a reasonable upper bound
	assert(frameDuration < 10000000LL);  // 1Hz, reasonable lower bound

	if (timeScale == 0)
		throw std::runtime_error("timeScale must be > 0");
	if (frameDurationTicks == 0)
		throw std::runtime_error("frameDurationTicks must be > 0");

	m_videoFrameFormatter = videoFrameFormatter;
	m_frameDuration = frameDuration;
	m_timeScale = timeScale;
	m_frameDurationTicks = frameDurationTicks;
	m_timingClock = timingClock;
	m_timestamp = timestamp;
	m_mediaType = mediaType;

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

	HRESULT hr = NOERROR;

	ppropInputRequest->cBuffers = 8;  // Your fix
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

	// ✅ Add this logging to verify the fix
	DbgLog((LOG_TRACE, 1, TEXT("DecideBufferSize: Requested %d buffers, got %d buffers, size %d bytes"), 
		ppropInputRequest->cBuffers, Actual.cBuffers, Actual.cbBuffer));

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
	if (FAILED(DeliverBeginFlush()))
		throw std::runtime_error("Failed to deliver beginflush");

	if (m_hdrData)
		m_hdrChanged = true;

	m_frameCounter = 0;
	m_previousFrameCounter = 0;
	m_frameCounterOffset = 0;
	m_previousTimeStop = 0;
	m_startTimeOffset = 0;
	m_droppedFrameCount = 0;

	// Reset hybrid timing state for DS_SSTM_HARDWARE_RATIONAL mode
	m_previousHardwareTimestamp = 0;
	m_hardwareTimingAnomalyCount = 0;
	m_rationalFrameDuration = 0;
	m_minFrameAdvance = 0;
	m_maxFrameAdvance = 0;

	// Reset CLOCK_SMART duration tracking
	memset(m_durationHistory, 0, sizeof(m_durationHistory));
	m_durationHistoryIndex = 0;
	m_durationHistoryCount = 0;
	m_lastHardwareTimestamp = 0;

	// Log timing mode information
	if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): ORIGINAL CLOCK_SMART mode active - will use:")));
		DbgLog((LOG_TRACE, 1, TEXT("  1) Hardware stop timestamps when available (from frame queue)")));
		DbgLog((LOG_TRACE, 1, TEXT("  2) Theoretical frame duration as fallback when no hardware stop time")));
		DbgLog((LOG_TRACE, 1, TEXT("  3) Simple and reliable for basic timing needs")));
	}
	else if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): ENHANCED CLOCK_SMART2 mode active - will use:")));
		DbgLog((LOG_TRACE, 1, TEXT("  1) Hardware stop timestamps when available (from frame queue)")));
		DbgLog((LOG_TRACE, 1, TEXT("  2) Average of last %d actual durations when no hardware stop time"), DURATION_HISTORY_SIZE));
		DbgLog((LOG_TRACE, 1, TEXT("  3) Integer-only math for monotonic timing")));
		DbgLog((LOG_TRACE, 1, TEXT("  4) Rational duration fallback when no history available")));
	}
	else if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): RATIONAL_RATIONAL mode active - using correction.cfg PPM:")));
		if (m_ppmCorrectionLoader.HasCorrections())
		{
			int ppmCorrection = RATIONAL_TRIM_DENOMINATOR - GetRationalTrimNumerator();
			DbgLog((LOG_TRACE, 1, TEXT("  PPM adjustment: %d (from correction.cfg)"), ppmCorrection));
			
			// Show trim ratio with context
			double trimPercentage = (100.0 * GetRationalTrimNumerator()) / RATIONAL_TRIM_DENOMINATOR;
			if (ppmCorrection > 0)
			{
				DbgLog((LOG_TRACE, 1, TEXT("  Effect: Stream runs %d PPM FASTER (trim %.6f%% = slight slowdown to compensate)"), 
					ppmCorrection, trimPercentage));
			}
			else if (ppmCorrection < 0)
			{
				DbgLog((LOG_TRACE, 1, TEXT("  Effect: Stream runs %d PPM SLOWER (trim %.6f%% = slight speedup to compensate)"), 
					ppmCorrection, trimPercentage));
			}
			else
			{
				DbgLog((LOG_TRACE, 1, TEXT("  Effect: No PPM correction (trim = 100.000000%)")));
			}
		}
		else
		{
			DbgLog((LOG_TRACE, 1, TEXT("  PPM adjustment: 0 (no correction.cfg found, using default)")));
		}
		DbgLog((LOG_TRACE, 1, TEXT("  Consistent across start/stop time calculations")));
		DbgLog((LOG_TRACE, 1, TEXT("  Pipeline offset: %I64d (100ns units)"), m_rationalPipelineOffset));
	}
	else if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Reset(): CLOCK_RATIONAL mode active - using correction.cfg PPM:")));
		if (m_ppmCorrectionLoader.HasCorrections())
		{
			int ppmCorrection = RATIONAL_TRIM_DENOMINATOR - GetRationalTrimNumerator();
			DbgLog((LOG_TRACE, 1, TEXT("  PPM adjustment: %d (from correction.cfg)"), ppmCorrection));
			
			// Show trim ratio with context
			double trimPercentage = (100.0 * GetRationalTrimNumerator()) / RATIONAL_TRIM_DENOMINATOR;
			if (ppmCorrection > 0)
			{
				DbgLog((LOG_TRACE, 1, TEXT("  Effect: Stream runs %d PPM FASTER (trim %.6f%% = slight slowdown to compensate)"), 
					ppmCorrection, trimPercentage));
			}
			else if (ppmCorrection < 0)
			{
				DbgLog((LOG_TRACE, 1, TEXT("  Effect: Stream runs %d PPM SLOWER (trim %.6f%% = slight speedup to compensate)"), 
					ppmCorrection, trimPercentage));
			}
			else
			{
				DbgLog((LOG_TRACE, 1, TEXT("  Effect: No PPM correction (trim = 100.000000%)")));
			}
		}
		else
		{
			DbgLog((LOG_TRACE, 1, TEXT("  PPM adjustment: 0 (no correction.cfg found, using default)")));
		}
		DbgLog((LOG_TRACE, 1, TEXT("  Hardware timestamps for start, rational duration with PPM trim for frame intervals")));
	}

	if (FAILED(DeliverEndFlush()))
		throw std::runtime_error("Failed to deliver endflush");

	if (FAILED(DeliverNewSegment(0, MAXLONGLONG, 1.0)))
		throw std::runtime_error("Failed to deliver new segment");
}


REFERENCE_TIME ALiveSourceVideoOutputPin::CalculateSmartFrameDuration() const
{
	// If we don't have enough history, fall back to rational duration
	if (m_durationHistoryCount == 0)
	{
		// Use rational math for theoretical duration (integer-only calculation)
		return (REFERENCE_TIME)((REFERENCE_TIME_TICKS_PER_SECOND * m_frameDurationTicks) / m_timeScale);
	}

	// Calculate average duration using integer math to avoid floating point precision issues
	int64_t totalDuration = 0;
	const size_t sampleCount = (m_durationHistoryCount < DURATION_HISTORY_SIZE) ? m_durationHistoryCount : DURATION_HISTORY_SIZE;
	
for (size_t i = 0; i < sampleCount; i++)
	{
		totalDuration += m_durationHistory[i];
	}

	// Use integer division for average (avoiding floating point)
	const REFERENCE_TIME averageDuration = totalDuration / sampleCount;

	DbgLog((LOG_TRACE, 1, TEXT("CalculateSmartFrameDuration(): Average of %zu samples = %I64d (%.3fms)"),
		sampleCount, averageDuration, averageDuration / 10000.0));

	return averageDuration;
}


void ALiveSourceVideoOutputPin::UpdateFrameDurationHistory(REFERENCE_TIME actualDuration)
{
	// Validate duration is reasonable (between 5ms and 1 second)
	if (actualDuration < 50000LL || actualDuration > 10000000LL)
	{
		DbgLog((LOG_WARNING, 1, TEXT("UpdateFrameDurationHistory(): Rejecting invalid duration %I64d (%.3fms) - outside range 5ms-1000ms"), 
			actualDuration, actualDuration / 10000.0));
		return;
	}

	// Store duration in circular buffer
	m_durationHistory[m_durationHistoryIndex] = actualDuration;
	m_durationHistoryIndex = (m_durationHistoryIndex + 1) % DURATION_HISTORY_SIZE;
	
	if (m_durationHistoryCount < DURATION_HISTORY_SIZE)
	{
		m_durationHistoryCount++;
	}

	// Log periodic statistics (every 50 frames for better visibility during testing)
	if (m_durationHistoryCount > 0 && (m_durationHistoryCount % 50) == 0)
	{
		const REFERENCE_TIME avgDuration = CalculateSmartFrameDuration();
		const REFERENCE_TIME theoreticalDuration = (REFERENCE_TIME)((REFERENCE_TIME_TICKS_PER_SECOND * m_frameDurationTicks) / m_timeScale);
		
		DbgLog((LOG_TRACE, 1, TEXT("CLOCK_SMART Duration Stats: %zu samples, average=%.3fms, theoretical=%.3fms, diff=%.3fms"),
			m_durationHistoryCount, 
			avgDuration / 10000.0,
			theoreticalDuration / 10000.0,
			(avgDuration - theoreticalDuration) / 10000.0));
	}
}


HRESULT ALiveSourceVideoOutputPin::RenderVideoFrameIntoSample(VideoFrame& videoFrame, IMediaSample* const pSample)
{
	assert(videoFrame.GetTimingTimestamp() > 0);
	assert(m_frameDuration > 0);
	assert(m_timingClock->TimingClockTicksPerSecond() > 0);

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
	}

	++m_frameCounter;

	//
	// Media time
	//

	// Guarantee first frame to start counting at zero
	uint64_t streamFrameCounter = videoFrame.GetCounter();
	if (m_frameCounterOffset == 0)
	{
		m_frameCounterOffset = streamFrameCounter;
		
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
		
	if (isDiscontinuity)
	{
		DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): Frame counter jumped from %I64u (stream frame %I64u), discontinuity detected%s"),
			videoFrame.GetCounter(), m_previousFrameCounter, streamFrameCounter,
			m_forceDiscontinuity ? TEXT(" (FORCED after timeline reset)") : TEXT("")));

		hr = pSample->SetDiscontinuity(TRUE);
		if (FAILED(hr))
			return hr;
			
		// Clear the force flag after setting discontinuity
		m_forceDiscontinuity = false;
	}
	
	// CRITICAL FOR RATIONAL_RATIONAL: Deliver new segment after timeline reset
	// This officially notifies MadVR that the timeline has restarted from 0
	// Without this, RATIONAL_RATIONAL's strict mathematical timing confuses MadVR
	if (m_deliverNewSegment)
	{
		DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): Delivering NEW SEGMENT to restart timeline (critical for RATIONAL_RATIONAL)"),
			videoFrame.GetCounter()));
			
		if (FAILED(DeliverNewSegment(0, MAXLONGLONG, 1.0)))
		{
			DbgLog((LOG_ERROR, 1, TEXT("::FillBuffer(#%I64u): Failed to deliver new segment!"), videoFrame.GetCounter()));
		}
		else
		{
			DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): New segment delivered successfully - MadVR timeline restarted"),
				videoFrame.GetCounter()));
		}
		
		m_deliverNewSegment = false;
	}

	m_previousFrameCounter = videoFrame.GetCounter();

	//
	// Setting the time
	//

	REFERENCE_TIME timeStart = REFERENCE_TIME_INVALID;
	REFERENCE_TIME timeStop = REFERENCE_TIME_INVALID;

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
		
		// Get raw hardware timestamp
		REFERENCE_TIME rawHardwareTime = (REFERENCE_TIME)(videoFrame.GetTimingTimestamp() * (10000000.0 / m_timingClock->TimingClockTicksPerSecond()));
		
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
			m_startTimeOffset = rawHardwareTime;
			m_previousHardwareTimestamp = rawHardwareTime;
			timeStart = 0;  // Start timeline at zero
			
			DbgLog((LOG_TRACE, 1, TEXT("::HardwareRational(#%I64u): First frame - baseline set to %I64d, timeline starts at 0"),
				videoFrame.GetCounter(), m_startTimeOffset));
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

		// Get frame timestamp as reference time using integer math utility
		timeStart = ConvertTimingClockToReferenceTime(
			videoFrame.GetTimingTimestamp(), 
			m_timingClock->TimingClockTicksPerSecond());

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
		
		// CRITICAL MONOTONIC ENFORCEMENT FOR START TIME (CLOCK_SMART2)
		// Even with free-running clock, ensure timeStart never goes backwards
		// This prevents invalid frame intervals where start > stop
		if (m_previousTimeStop > 0)
		{
			// Calculate what the minimum start time should be based on previous stop
			// Use theoretical frame duration as minimum progression
			const REFERENCE_TIME minStartTime = m_previousTimeStop - m_frameDuration;
			
			if (timeStart < minStartTime)
			{
				DbgLog((LOG_WARNING, 1, TEXT("CLOCK_SMART2(#%I64u): timeStart=%I64d < minStartTime=%I64d, enforcing monotonic (prevStop=%I64d)"), 
					videoFrame.GetCounter(), timeStart, minStartTime, m_previousTimeStop));
				timeStart = minStartTime;
			}
		}
		
		// Store current hardware timestamp for CLOCK_SMART/CLOCK_SMART2 duration tracking
		if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
		    m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2)
		{
			m_lastHardwareTimestamp = ConvertTimingClockToReferenceTime(
				videoFrame.GetTimingTimestamp(),
				m_timingClock->TimingClockTicksPerSecond());
		}
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

		timeStop = NextFrameTimestamp();
		if (timeStop == REFERENCE_TIME_INVALID)
		{
			timeStop = timeStart + m_frameDuration;
		}
		else
		{
			assert(m_startTimeOffset > 0);
			timeStop -= m_startTimeOffset;
		}
		break;

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2:

		timeStop = NextFrameTimestamp();
		if (timeStop == REFERENCE_TIME_INVALID)
		{
			// ENHANCED CLOCK_SMART2: Instead of using theoretical duration,
			// use average of last 100 actual frame durations for better accuracy
			const REFERENCE_TIME smartDuration = CalculateSmartFrameDuration();
			timeStop = timeStart + smartDuration;
			
			// Update duration history if we have previous hardware timestamp
			if (m_lastHardwareTimestamp > 0)
			{
				// Calculate actual duration from hardware timestamps (integer math)
				const REFERENCE_TIME currentHardwareTime = ConvertTimingClockToReferenceTime(
					videoFrame.GetTimingTimestamp(),
					m_timingClock->TimingClockTicksPerSecond());
				const REFERENCE_TIME measuredDuration = currentHardwareTime - m_lastHardwareTimestamp;
				
				UpdateFrameDurationHistory(measuredDuration);
			}
			
			// Ensure monotonic progression using utility function
			const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
			if (monotonicTimeStop != timeStop)
			{
				DbgLog((LOG_WARNING, 1, TEXT("CLOCK_SMART2(#%I64u): Enforced monotonic progression, adjusted stop time from %I64d to %I64d"), 
					videoFrame.GetCounter(), timeStop, monotonicTimeStop));
				timeStop = monotonicTimeStop;
			}
		}
		else
		{
			assert(m_startTimeOffset > 0);
			timeStop -= m_startTimeOffset;
			
			// Ensure hardware-based stop time is also monotonic using utility function
			const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
			if (monotonicTimeStop != timeStop)
			{
				DbgLog((LOG_WARNING, 1, TEXT("CLOCK_SMART2(#%I64u): Hardware stop time not monotonic, enforced progression from %I64d to %I64d"), 
					videoFrame.GetCounter(), timeStop, monotonicTimeStop));
				timeStop = monotonicTimeStop;
			}
		}

		assert(timeStop > timeStart);
		break;
	
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
		
		timeStop = timeStart + m_frameDuration;
		break;

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:

		// These modes use the next frame timestamp or theoretical duration
		// Implementation needed if these modes are used
		timeStop = timeStart + m_frameDuration;
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

		// FINAL MONOTONIC VALIDATION: Ensure frame interval is always valid
		// This is the last line of defense against any timing anomalies
		if (timeStop <= timeStart)
		{
			// Emergency correction: force minimum 1-tick interval
			timeStop = timeStart + 1;
			DbgLog((LOG_ERROR, 1, TEXT("::FillBuffer(#%I64u): CRITICAL - timeStop <= timeStart! Forced to %I64d (start=%I64d)"),
				videoFrame.GetCounter(), timeStop, timeStart));
		}
		
		hr = pSample->SetTime(&timeStart, &timeStop);
		if (FAILED(hr))
			return hr;

		// Track previous stop time for monotonicity checking (important for both rational modes)
		// This must be done for ALL builds, not just debug, to enable timeline validation
		m_previousTimeStop = timeStop;
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
	}

	return hr;
}

void ALiveSourceVideoOutputPin::LoadPPMCorrections(double refreshRate)
{
	// Attempt to load correction.cfg file
	bool loaded = m_ppmCorrectionLoader.LoadCorrectionFile();
	
	if (loaded)
	{
		// Get PPM correction for this refresh rate
		int ppmCorrection = m_ppmCorrectionLoader.GetPPMCorrection(refreshRate);
		
		// Calculate the trim numerator based on PPM correction
		// Positive PPM makes stream faster (smaller numerator), negative makes it slower (larger numerator)
		if (ppmCorrection == 0)
		{
			m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;  // No correction
			DbgLog((LOG_TRACE, 1, TEXT("LoadPPMCorrections: %.3f Hz - using default timing (0 PPM)"), refreshRate));
		}
		else
		{
			m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR - ppmCorrection;
			DbgLog((LOG_TRACE, 1, TEXT("LoadPPMCorrections: %.3f Hz - applying %d PPM correction (trim numerator: %llu/%llu = %.6f%%)"), 
				refreshRate, ppmCorrection, m_currentRationalTrimNumerator, RATIONAL_TRIM_DENOMINATOR,
				(100.0 * m_currentRationalTrimNumerator) / RATIONAL_TRIM_DENOMINATOR));
		}
	}
	else
	{
		// No correction file, use default (no correction)
		m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;
		DbgLog((LOG_TRACE, 1, TEXT("LoadPPMCorrections: %.3f Hz - no correction.cfg found, using default timing"), refreshRate));
	}
}
