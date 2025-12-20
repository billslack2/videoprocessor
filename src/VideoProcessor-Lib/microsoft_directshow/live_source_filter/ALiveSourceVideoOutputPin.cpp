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

#include "ALiveSourceVideoOutputPin.h"


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

	// CRITICAL: Reset ALL timeline state to ensure clean restart
	// This is especially important for RATIONAL_RATIONAL which expects pristine mathematical state
	m_frameCounter = 0;
	m_previousFrameCounter = 0;
	m_startTimeOffset = 0;
	m_frameCounterOffset = 0;
	m_previousTimeStop = 0;
	m_droppedFrameCount = 0;

	DbgLog((LOG_TRACE, 1, TEXT("ALiveSourceVideoOutputPin::Reset() - Complete timeline reset for mode %d"), m_timestamp));

	if (FAILED(DeliverEndFlush()))
		throw std::runtime_error("Failed to deliver endflush");

	// Deliver new segment to establish timing baseline with DirectShow reference clock
	if (FAILED(DeliverNewSegment(0, MAXLONGLONG, 1.0)))
		throw std::runtime_error("Failed to deliver new segment");
		
	m_newSegment = false;  // Clear flag after successful delivery
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
		m_frameCounterOffset = streamFrameCounter;
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
		// Pure rational timing: Use exact integer math for perfectly evenly-spaced timestamps
		// starting from zero. This produces monotonic, jitter-free timestamps.
		
		// CRITICAL: Use streamFrameCounter (derived from the SOURCE), NOT m_frameCounter
		// m_frameCounter is just a local incrementer and can desync from the actual stream
		// streamFrameCounter comes directly from videoFrame.GetCounter() adjusted by offset
		
		const uint64_t frameNum = streamFrameCounter;  // Frame number relative to stream start (frame 0, 1, 2, ...)
		
		// Pure rational calculation with optional PLL drift compensation
		// timeStart = (frameNum / timeScale) * 10000000 in 100-nanosecond units
		const uint64_t referenceTimePerSecond = 10000000ULL;
		
		// Apply optional PLL hardware clock drift correction if enabled
		// The correction factor is measured by the DeckLink PLL and accounts for
		// slight hardware clock variations from the nominal rate
		uint64_t adjustedFrameDurationTicks = m_frameDurationTicks;
		if (m_applyPllCorrectionToRational && m_tickRateCorrectionFactor != 1.0)
		{
			adjustedFrameDurationTicks = (uint64_t)round((double)m_frameDurationTicks * m_tickRateCorrectionFactor);
			
			// Log correction on first frame only
			if (streamFrameCounter == 0 && m_tickRateCorrectionFactor != 1.0)
			{
				const double correctionPpm = ((m_tickRateCorrectionFactor - 1.0) * 1000000.0);
				DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): RATIONAL_RATIONAL PLL correction ENABLED - factor: %.8f (%+.2f PPM)"),
					videoFrame.GetCounter(), m_tickRateCorrectionFactor, correctionPpm));
			}
		}
		else if (streamFrameCounter == 0)
		{
			DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): RATIONAL_RATIONAL PLL correction DISABLED - using pure mathematical timing"),
				videoFrame.GetCounter()));
		}
		
		timeStart = (REFERENCE_TIME)((frameNum * referenceTimePerSecond * adjustedFrameDurationTicks) / m_timeScale);
		
		// SANITY CHECK: For RATIONAL_RATIONAL on first frame, frameNum should be 0
		if (streamFrameCounter == 0 && frameNum != 0)
		{
			DbgLog((LOG_ERROR, 1, TEXT("::FillBuffer(#%I64u): CRITICAL - First frame but frameNum=%I64u"), 
				videoFrame.GetCounter(), frameNum));
		}
		
		// MONOTONICITY GUARANTEE: This timestamp MUST be >= previous timestamp
		// With pure integer math and monotonic frame counter, this should always hold
		// If inversion occurs, it indicates a fundamental math error
		if (streamFrameCounter > 0 && timeStart <= m_previousTimeStop)
		{
			DbgLog((LOG_ERROR, 1, TEXT("::FillBuffer(#%I64u): CRITICAL - RATIONAL_RATIONAL timestamp inversion! Current: %I64d, Previous: %I64d, streamFrameNum: %I64u"),
				videoFrame.GetCounter(), timeStart, m_previousTimeStop, streamFrameCounter));
			
			DbgLog((LOG_ERROR, 1, TEXT("Timeline state - m_frameCounter=%I64u, streamFrameCounter=%I64u, frameCounterOffset=%I64u, timeScale=%u, frameDurationTicks=%u, correction=%.8f"),
				m_frameCounter, streamFrameCounter, m_frameCounterOffset, m_timeScale, m_frameDurationTicks, m_tickRateCorrectionFactor));
			
			DbgLog((LOG_ERROR, 1, TEXT("Calculation: frameNum=%I64u, refTime=%I64u, adjustedTicks=%I64u, timeScale=%u"),
				frameNum, referenceTimePerSecond, adjustedFrameDurationTicks, m_timeScale));
			
			// This should never happen if math is correct, but if it does, force monotonic progression
			timeStart = m_previousTimeStop + 1;
		}
		
		// Detailed logging on first frame for debugging
		if (streamFrameCounter == 0)
		{
			DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): RATIONAL_RATIONAL STARTED - timeScale=%u, frameDurationTicks=%u, rate=%.6f fps, pllCorrectionEnabled=%s"),
				videoFrame.GetCounter(), m_timeScale, m_frameDurationTicks, 
				(double)m_timeScale / (double)m_frameDurationTicks,
				m_applyPllCorrectionToRational ? TEXT("YES") : TEXT("NO")));
		}
		
		// Every 100 frames, log timing state for monitoring
		if (streamFrameCounter % 100 == 0 && streamFrameCounter > 0)
		{
			DbgLog((LOG_TRACE, 1, TEXT("::FillBuffer(#%I64u): RATIONAL_RATIONAL frame %I64u - timeStart=%I64d, timeStop will be %I64d, prevStop=%I64d, correction=%.8f"),
				videoFrame.GetCounter(), streamFrameCounter, timeStart, 
				(REFERENCE_TIME)(((streamFrameCounter + 1) * referenceTimePerSecond * adjustedFrameDurationTicks) / m_timeScale),
				m_previousTimeStop, m_tickRateCorrectionFactor));
		}
		
		break;
	}

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE:

		// Get frame timestamp as reference time
		timeStart =
			(REFERENCE_TIME)(
				videoFrame.GetTimingTimestamp() *
				(10000000.0 / m_timingClock->TimingClockTicksPerSecond()));

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
		// For rational timing, calculate the next frame's exact timestamp
		// Apply same PLL correction as start time for consistency
		const uint64_t nextFrameNum = streamFrameCounter + 1;
		const uint64_t referenceTimePerSecond = 10000000ULL;
		
		// Apply same correction as start time
		uint64_t adjustedFrameDurationTicks = m_frameDurationTicks;
		if (m_applyPllCorrectionToRational && m_tickRateCorrectionFactor != 1.0)
		{
			adjustedFrameDurationTicks = (uint64_t)round((double)m_frameDurationTicks * m_tickRateCorrectionFactor);
		}
		
		timeStop = (REFERENCE_TIME)((nextFrameNum * referenceTimePerSecond * adjustedFrameDurationTicks) / m_timeScale);
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

		assert(timeStop > timeStart);
		break;

	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO:

		timeStop = timeStart + m_frameDuration;
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
	case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
	case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
	case DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO:

		hr = pSample->SetTime(&timeStart, &timeStop);
		if (FAILED(hr))
			return hr;

		// Track previous stop time for RATIONAL_RATIONAL monotonicity checking
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
