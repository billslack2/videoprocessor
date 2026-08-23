/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "CUnbufferedLiveSourceVideoOutputPin.h"


CUnbufferedLiveSourceVideoOutputPin::CUnbufferedLiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr):
	ALiveSourceVideoOutputPin(filter, pLock, phr)
{
}


CUnbufferedLiveSourceVideoOutputPin::~CUnbufferedLiveSourceVideoOutputPin()
{

}


HRESULT CUnbufferedLiveSourceVideoOutputPin::OnVideoFrame(VideoFrame& videoFrame)
{
	if (TerminalFlushStarted() || CoordinatedResetRequested())
		return S_FALSE;

	BYTE* pData = nullptr;
	HRESULT hr;

	// Get buffer for sample
	// Note you can fill in start and stop time, but following the code shows that they are unused.
	IMediaSample* pSample = nullptr;
	hr = this->GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
	if (FAILED(hr))
	{
		return hr;
	}

	// Render
	hr = RenderVideoFrameIntoSample(videoFrame, pSample);
	if (FAILED(hr) || hr == S_FRAME_NOT_RENDERED)
	{
		pSample->Release();
		return hr;
	}

	if (m_deliverNewSegment.exchange(false, std::memory_order_acq_rel))
	{
		pSample->Release();
		RequestCoordinatedReset("unbuffered-new-segment");
		return S_FALSE;
	}

	// Deliver to downstream renderer (this will block). The unbuffered mode
	// has no delivery telemetry consumer, but it shares the exact media-type
	// attachment/completion contract with the buffered live path.
	const DirectShowDeliveryTicket deliveryTicket =
		m_directShowFrameDeliverer.Begin(
			pSample,
			[this](IMediaSample* deliverySample)
			{
				return AttachPendingMediaType(deliverySample);
			},
			[]()
			{
				return GetWallClockTime();
			});
	const DirectShowDeliveryResult deliveryResult =
		m_directShowFrameDeliverer.Complete(
			deliveryTicket,
			[this](IMediaSample* deliverySample)
			{
				return Deliver(deliverySample);
			},
			[this](uint64_t mediaTypeGeneration, HRESULT deliveryResult)
			{
				CompletePendingMediaType(mediaTypeGeneration, deliveryResult);
			},
			[]()
			{
				return GetWallClockTime();
			});
	hr = deliveryResult.result;
	pSample->Release();

	return hr;
}


void CUnbufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize(size_t frameBufferMaxSize)
{
	if (frameBufferMaxSize != 0)
		throw std::runtime_error("CUnbufferedLiveSourceVideoOutputPin can only accept zero frame buffers ");
}
