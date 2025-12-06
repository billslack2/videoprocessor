/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "CBufferedLiveSourceVideoOutputPin.h"
#include "CUnbufferedLiveSourceVideoOutputPin.h"


#include "CLiveSource.h"


CLiveSource::CLiveSource(
	LPUNKNOWN pUnk,
	HRESULT* phr):
	CBaseFilter(LIVE_SOURCE_FILTER_NAME, pUnk, &m_critSec, CLSID_CLiveSource)
{
}


CLiveSource::~CLiveSource()
{
	assert(!m_videoOutputPin);  // Didn't call Destroy()
}


CUnknown* WINAPI CLiveSource::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr)
{
	CUnknown* liveSource = new CLiveSource(pUnk, phr);

	if (phr)
	{
		if (!liveSource)
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}

	return liveSource;
}


STDMETHODIMP CLiveSource::Initialize(
	IVideoFrameFormatter* videoFrameFormatter,
	const AM_MEDIA_TYPE& mediaType,
	timestamp_t frameDuration,
	LONGLONG fpsNum,
	LONGLONG fpsDen,
	ITimingClock* timingClock,
	DirectShowStartStopTimeMethod timestamp,
	bool useFrameQueue,
	size_t frameQueueMaxSize)
{
	assert(!m_videoOutputPin);
	assert(videoFrameFormatter);
	assert(mediaType.majortype.Data1 > 0);
	assert(frameDuration > 0);

	HRESULT hr = S_OK;

	if (useFrameQueue)
	{
		m_videoOutputPin = new CBufferedLiveSourceVideoOutputPin(
			this,
			&m_critSec,
			&hr);
	}
	else
	{
		m_videoOutputPin = new CUnbufferedLiveSourceVideoOutputPin(
			this,
			&m_critSec,
			&hr);
	}

	if (!m_videoOutputPin || hr != S_OK)
		throw std::runtime_error("Failed to construct pin");

	if (m_videoOutputPin)
		m_videoOutputPin->AddRef();

	m_videoOutputPin->Initialize(
		videoFrameFormatter,
		frameDuration,
		fpsNum,
		fpsDen,
		timingClock,
		timestamp,
		mediaType);

	if (useFrameQueue)
		m_videoOutputPin->SetFrameQueueMaxSize(frameQueueMaxSize);

	return S_OK;
}


STDMETHODIMP CLiveSource::Destroy()
{
	if (m_videoOutputPin)
	{
		ULONG refCount = m_videoOutputPin->Release();
		delete m_videoOutputPin;  // Pin's release() does not delete at last one
		m_videoOutputPin = nullptr;
	}

	return S_OK;
}


STDMETHODIMP CLiveSource::OnHDRData(HDRDataSharedPtr& hdrData)
{
	if (!m_videoOutputPin)
		return E_POINTER;
	
	m_videoOutputPin->OnHDRData(hdrData);
	return S_OK;
}


STDMETHODIMP CLiveSource::OnVideoFrame(VideoFrame& videoFrame)
{
	if (!m_videoOutputPin)
		return E_POINTER;
	
	return m_videoOutputPin->OnVideoFrame(videoFrame);
}


STDMETHODIMP CLiveSource::SetFrameQueueMaxSize(size_t frameQueueMaxSize)
{
	if (frameQueueMaxSize < 0)
		throw std::runtime_error("Queue must be >= 0");

	if (!m_videoOutputPin)
		return E_POINTER;

	m_videoOutputPin->SetFrameQueueMaxSize(frameQueueMaxSize);
	return S_OK;
}


STDMETHODIMP CLiveSource::UpdateFrameRate(LONGLONG fpsNum, LONGLONG fpsDen)
{
	if (!m_videoOutputPin)
		throw std::runtime_error("Cannot update frame rate before Initialize()");

	if (fpsNum <= 0 || fpsDen <= 0)
		throw std::runtime_error("Invalid FPS parameters");

	m_videoOutputPin->UpdateFrameRate(fpsNum, fpsDen);
	return S_OK;
}


STDMETHODIMP CLiveSource::Reset()
{
	// Safety check - Reset can be called during shutdown or before full initialization
	if (!m_videoOutputPin)
	{
		DbgLog((LOG_TRACE, 1, TEXT("CLiveSource::Reset(): m_videoOutputPin is null, skipping")));
		return S_OK;
	}

	m_videoOutputPin->Reset();
	return S_OK;
}


STDMETHODIMP CLiveSource::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	if (riid == IID_ILiveSource)
		return GetInterface((ILiveSource*)this, ppv);

	else if (riid == IID_IAMFilterMiscFlags)
		return GetInterface((IAMFilterMiscFlags*)this, ppv);

	else
		return CBaseFilter::NonDelegatingQueryInterface(riid, ppv);
}


int CLiveSource::GetPinCount()
{
	return 1;
}


CBasePin* CLiveSource::GetPin(int n)
{
	if (n != 0)
		throw std::runtime_error("CLiveSource only has 1 pin");

	return m_videoOutputPin;
}


ULONG CLiveSource::GetMiscFlags()
{
	return AM_FILTER_MISC_FLAGS_IS_SOURCE;
}


int CLiveSource::GetFrameQueueSize()
{
	if (!m_videoOutputPin)
		return 0;
	
	return (int)m_videoOutputPin->GetFrameQueueSize();
}


double CLiveSource::ExitLatencyMs() const
{
	if (!m_videoOutputPin)
		return 0.0;
	
	return m_videoOutputPin->ExitLatencyMs();
}


uint64_t CLiveSource::LatencyMeasurementFrameCounter() const
{
	if (!m_videoOutputPin)
		return 0;
	
	return m_videoOutputPin->LatencyMeasurementFrameCounter();
}


uint64_t CLiveSource::DroppedFrameCount() const
{
	if (!m_videoOutputPin)
		return 0;
	
	return m_videoOutputPin->DroppedFrameCount();
}


uint64_t CLiveSource::DiscontinuityCount() const
{
	if (!m_videoOutputPin)
		return 0;
	
	return m_videoOutputPin->DiscontinuityCount();
}


uint64_t CLiveSource::ReAnchorCount() const
{
	if (!m_videoOutputPin)
		return 0;
	
	return m_videoOutputPin->ReAnchorCount();
}


double CLiveSource::TimestampDriftMs() const
{
	if (!m_videoOutputPin)
		return 0.0;
	
	return m_videoOutputPin->TimestampDriftMs();
}
