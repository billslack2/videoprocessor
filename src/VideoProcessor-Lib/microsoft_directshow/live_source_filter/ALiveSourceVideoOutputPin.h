/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <microsoft_directshow/DirectShowRendererStartStopTimeMethod.h>
#include <microsoft_directshow/DirectShowDefines.h>

#include "CLiveSource.h"

// This is not an error by itself
static const HRESULT S_FRAME_NOT_RENDERED = 1;

/**
 * Abstract implementation of the video output pin
 */
class ALiveSourceVideoOutputPin:
	public CBaseOutputPin,
	public IAMPushSource,
	public IKsPropertySet
{
public:

	ALiveSourceVideoOutputPin(
		CLiveSource* filter,
		CCritSec* pLock,
		HRESULT* phr);
	virtual ~ALiveSourceVideoOutputPin() {}

	DECLARE_IUNKNOWN;

	void Initialize(
		IVideoFrameFormatter* const videoFrameFormatter,
		timestamp_t frameDuration,
		LONGLONG fpsNum,
		LONGLONG fpsDen,
		ITimingClock* const timingClock,
		DirectShowStartStopTimeMethod timestamp,
		const AM_MEDIA_TYPE& mediaType);

	// CBaseOutputPin overrides
	HRESULT GetMediaType(int iPosition, CMediaType* pmt);
	HRESULT CheckMediaType(const CMediaType *pmt);
	HRESULT DecideAllocator(IMemInputPin* pPin, IMemAllocator** pAlloc);
	HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest);

	// IAMPushSource
	STDMETHODIMP GetMaxStreamOffset(REFERENCE_TIME* prtMaxOffset) override;
	STDMETHODIMP GetPushSourceFlags(ULONG* pFlags) override;
	STDMETHODIMP GetStreamOffset(REFERENCE_TIME* prtOffset) override;
	STDMETHODIMP SetMaxStreamOffset(REFERENCE_TIME rtMaxOffset) override;
	STDMETHODIMP SetPushSourceFlags(ULONG Flags) override;
	STDMETHODIMP SetStreamOffset(REFERENCE_TIME rtOffset) override;
	STDMETHODIMP GetLatency(REFERENCE_TIME* prtLatency) override;

	// IQualityControl
	STDMETHODIMP Notify(IBaseFilter * pSender, Quality q) override;
	HRESULT STDMETHODCALLTYPE SetSink(IQualityControl* piqc) override;

	// IKsPropertySet
	HRESULT STDMETHODCALLTYPE Set(
		REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData,
		LPVOID pPropData, DWORD cbPropData) override;
	HRESULT STDMETHODCALLTYPE Get(
		REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData,
		DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData,
		DWORD* pcbReturned) override;
	HRESULT STDMETHODCALLTYPE QuerySupported(
		REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport) override;

	// Part of ILiveSource interface replicated here
	void OnHDRData(HDRDataSharedPtr&);
	virtual HRESULT OnVideoFrame(VideoFrame&) = 0;

	// Set the size of the queue.
	// Zero means no queueing, might not be legal
	virtual void SetFrameQueueMaxSize(size_t) = 0;

	// Get the size of the queue.
	// Zero means no queueing going on.
	virtual size_t GetFrameQueueSize() = 0;

	// Update frame rate parameters dynamically (for refresh rate changes)
	void UpdateFrameRate(LONGLONG fpsNum, LONGLONG fpsDen);

	// Reset the internal state and the video stream.
	virtual void Reset();

	//
	// Metrics
	//

	// Get the exit latency in ms, which the amount of time between the frame timestamp
	// and when the frame is delivered to the DirectShow renderer.
	// This is sampled every 20 frames.
	double ExitLatencyMs() const { return m_exitLatencyMs;  }

	// Get the frame counter at which the last latency measurement was taken.
	// Returns 0 if no measurement has been taken yet (e.g., after Reset()).
	// Auto-tuning should only use latency values when this counter is recent.
	uint64_t LatencyMeasurementFrameCounter() const { return m_latencyMeasurementFrameCounter; }

	// Get the current frame counter (frames processed since last Reset)
	uint64_t CurrentFrameCounter() const { return m_frameCounter; }

	// Get the amount of dropped frames due to queue actions
	uint64_t DroppedFrameCount() const { return m_droppedFrameCount; }

protected:

	uint64_t m_droppedFrameCount = 0;

	// Render function to render a videoFrame onto a IMediaSample.
	// Will not release the sample or dec videoframe nor do the Deliver()
	// Will return S_FRAME_NOT_RENDERED if frame could not be renderered, not an error per-se
	HRESULT RenderVideoFrameIntoSample(VideoFrame&, IMediaSample* const);

	// Get the next frame timestamp. If it doesn't know it's invalid. Overridden by implementations
	virtual REFERENCE_TIME NextFrameTimestamp() const { return REFERENCE_TIME_INVALID; }

	IVideoFrameFormatter* m_videoFrameFormatter;
	timestamp_t m_frameDuration;
	LONGLONG m_fpsNum = 0;  // Rational FPS numerator (e.g., 60000 for 59.94Hz) - atomic writes on x64
	LONGLONG m_fpsDen = 0;  // Rational FPS denominator (e.g., 1001 for 59.94Hz) - atomic writes on x64
	ITimingClock* m_timingClock;
	DirectShowStartStopTimeMethod m_timestamp;
	AM_MEDIA_TYPE m_mediaType;
	bool m_useHDRData = false;

	REFERENCE_TIME m_previousTimeStop = 0;
	timestamp_t m_startTimeOffset = 0;
	uint64_t m_frameCounterOffset = 0;
	uint64_t m_frameCounter = 0;
	uint64_t m_previousFrameCounter = 0;
	bool m_newSegment = false;

	// Hybrid CLOCK_RATIONAL mode state
	REFERENCE_TIME m_nextRationalTimeStart = 0;

	HDRDataSharedPtr m_hdrData = nullptr;
	bool m_hdrChanged = false;

	double m_exitLatencyMs = 0.0;
	uint64_t m_latencyMeasurementFrameCounter = 0;  // Frame counter when latency was last measured
};
