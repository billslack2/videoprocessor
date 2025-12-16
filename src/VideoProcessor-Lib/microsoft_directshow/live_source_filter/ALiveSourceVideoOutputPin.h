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

	// CLOCK_RATIONAL Diagnostics - track timeline health
	uint64_t DiscontinuityCount() const { return m_discontinuityCount; }
	uint64_t ReAnchorCount() const { return m_reAnchorCount; }
	double TimestampDriftMs() const { return m_timestampDriftMs; }

	// CLOCK_PLL Diagnostics - track estimated period
	double EstimatedPeriodDriftPpm() const 
	{
		if (m_pllClock.nominalPeriod100ns <= 0 || m_pllClock.estimatedPeriodDouble <= 0.0)
			return 0.0;
		const double nominalHz = 10000000.0 / m_pllClock.nominalPeriod100ns;
		const double estimatedHz = 10000000.0 / m_pllClock.estimatedPeriodDouble;
		return ((estimatedHz - nominalHz) / nominalHz) * 1000000.0;
	}
	double EstimatedFrameRateHz() const 
	{
		if (m_pllClock.estimatedPeriodDouble <= 0.0)
			return 0.0;
		// Use the double-precision estimate for accurate display
		// This captures sub-integer changes that would be lost with integer truncation
		return 10000000.0 / m_pllClock.estimatedPeriodDouble;
	}
	
	double PhaseErrorMs() const
	{
		// Convert phase error from 100ns units to milliseconds
		return m_pllClock.phaseErrorAccum / 10000.0;
	}

	// CLOCK_PLL average frame rate calculation
	// Returns -1.0 to indicate "still calculating" (warm-up period)
	double AverageFrameRateHz() const
	{
		// Need at least 300 frames (5 seconds at 60Hz) for stable average
		// This avoids initial settling artifacts and PLL convergence period
		const uint64_t MIN_FRAMES_FOR_AVERAGE = 300;
		
		if (m_frameCounter < MIN_FRAMES_FOR_AVERAGE)
			return -1.0;  // Still in warm-up period
		
		// We use frame-based timing, not wall-clock timing
		// m_avgRateStartTime is the FRAME timestamp at measurement start
		// m_avgRateLastTime is the FRAME timestamp at latest frame
		if (m_avgRateStartTime == 0 || m_avgRateLastTime == 0)
			return -1.0;  // Not yet initialized
		
		if (m_avgRateLastTime <= m_avgRateStartTime)
			return -1.0;  // Invalid state
		
		const timingclocktime_t elapsedTicks = m_avgRateLastTime - m_avgRateStartTime;
		
		if (elapsedTicks <= 0)
			return -1.0;
		
		// Calculate elapsed seconds using the timing clock's frequency
		// This gives us the TRUE elapsed time between frames, not wall-clock time
		// DeckLink clock is 1,000,000 ticks/second (microseconds)
		const double elapsedSeconds = static_cast<double>(elapsedTicks) / 1000000.0;
		
		if (elapsedSeconds <= 0.0)
			return -1.0;
		
		// Calculate average: frames counted / elapsed frame time
		const uint64_t framesCounted = m_avgRateFrameCount;
		
		if (framesCounted == 0)
			return -1.0;
		
		return static_cast<double>(framesCounted) / elapsedSeconds;
	}

protected:

	uint64_t m_droppedFrameCount = 0;

	// CLOCK_RATIONAL diagnostic counters
	uint64_t m_discontinuityCount = 0;  // Tracks frame counter jumps
	uint64_t m_reAnchorCount = 0;       // Tracks timeline re-anchoring events  
	double m_timestampDriftMs = 0.0;    // Drift between rational timeline and hardware clock

	// Render function to render a videoFrame onto a IMediaSample.
	// Will not release the sample or dec videoframe nor do the Deliver()
	// Will return S_FRAME_NOT_RENDERED if frame could not be renderered, not an error per-se
	HRESULT RenderVideoFrameIntoSample(VideoFrame&, IMediaSample* const);

	// Get the next frame timestamp. If it doesn't know it's invalid. Overridden by implementations
	virtual REFERENCE_TIME NextFrameTimestamp() const { return REFERENCE_TIME_INVALID; }

	IVideoFrameFormatter* m_videoFrameFormatter = nullptr;
	timestamp_t m_frameDuration = 0;
	LONGLONG m_fpsNum = 0;  // Rational FPS numerator (e.g., 60000 for 59.94Hz) - atomic writes on x64
	LONGLONG m_fpsDen = 0;  // Rational FPS denominator (e.g., 1001 for 59.94Hz) - atomic writes on x64
	ITimingClock* m_timingClock = nullptr;
	DirectShowStartStopTimeMethod m_timestamp = DirectShowStartStopTimeMethod::DS_SSTM_NONE;
	AM_MEDIA_TYPE m_mediaType = {};
	bool m_useHDRData = false;

	REFERENCE_TIME m_previousTimeStop = 0;
	timestamp_t m_startTimeOffset = 0;
	uint64_t m_frameCounterOffset = 0;
	uint64_t m_frameCounter = 0;
	uint64_t m_previousFrameCounter = 0;
	bool m_newSegment = true;  // Start true so first frame anchors the timeline

	// Hybrid CLOCK_RATIONAL mode state
	REFERENCE_TIME m_nextRationalTimeStart = 0;
	LONGLONG m_rationalRemainder = 0;  // Bresenham-style remainder for exact rational timing

	// CLOCK_PLL mode state - tracks actual hardware capture clock rate using Phase-Locked Loop
	struct PLLClockState {
		bool initialized = false;           // Whether the PLL has been initialized
		LONGLONG nominalPeriod100ns = 0;    // Nominal frame period in 100ns units (from rational FPS)
		double estimatedPeriodDouble = 0.0; // Current estimated frame period in 100ns units (DOUBLE for sub-integer precision!)
		LONGLONG estimatedPeriod100ns = 0;  // Integer version for timestamp generation (derived from estimatedPeriodDouble)
		REFERENCE_TIME baseTimestamp = 0;   // Anchor timestamp for timeline
		uint64_t baseFrameIndex = 0;        // Frame index at anchor point
		LONGLONG lastHwTimerValue = 0;      // Last hardware timer sample (QPC units) - DEPRECATED, use lastFrameTimestamp
		uint64_t lastFrameIndex = 0;        // Frame index of last measurement
		LONGLONG hwTimerFrequency = 0;      // Hardware timer frequency (ticks/second)
		double phaseErrorAccum = 0.0;       // Accumulated phase error for gentle correction
		REFERENCE_TIME lastGeneratedTimestamp = 0;  // Last timestamp generated (for monotonicity)
		timingclocktime_t lastFrameTimestamp = 0;   // Last frame's hardware timestamp (DeckLink clock)
		
		// Multi-frame measurement state - measure over N frames for stability
		timingclocktime_t measurementBaseTimestamp = 0;  // Timestamp at start of measurement window
		uint64_t measurementBaseFrameIndex = 0;          // Frame index at start of measurement window
		uint64_t measurementFrameCount = 0;              // Frames since last measurement update
	} m_pllClock;

	// CLOCK_PLL configuration constants
	static constexpr double PLL_ALPHA = 0.02;               // Period smoothing factor (2% per measurement)
	static constexpr double PLL_PHASE_ALPHA = 0.001;        // Phase correction factor (0.1% per frame)
	static constexpr double PLL_MAX_PERIOD_CHANGE = 0.0005; // Max 0.05% change per update (50 ppm)
	static constexpr double PLL_OUTLIER_THRESHOLD = 0.01;   // Reject measurements >1% from nominal
	static constexpr double PLL_MAX_DEVIATION_PPM = 200.0;  // Clamp estimate within ±200 ppm of nominal
	static constexpr uint64_t PLL_MEASUREMENT_FRAMES = 15;  // Measure period over 15 frames (~250ms at 60Hz)

	HDRDataSharedPtr m_hdrData = nullptr;
	bool m_hdrChanged = false;

	double m_exitLatencyMs = 0.0;
	uint64_t m_latencyMeasurementFrameCounter = 0;  // Frame counter when latency was last measured
	
	// Average frame rate tracking - uses FRAME timestamps, not wall-clock time
	// This ensures we measure actual capture rate regardless of when UI queries
	timingclocktime_t m_avgRateStartTime = 0;       // Frame timestamp when avg measurement started (hardware clock ticks)
	timingclocktime_t m_avgRateLastTime = 0;        // Frame timestamp of the latest frame (hardware clock ticks)
	uint64_t m_avgRateFrameCount = 0;               // Number of frames counted since measurement started
};
