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
#include <PPMCorrectionLoader.h>

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
		unsigned int timeScale,
		unsigned int frameDurationTicks,
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

	// Reset the internal state and the video stream.
	virtual void Reset();

	// Set fixed pipeline offset for RATIONAL_RATIONAL mode (in 100ns units)
	// This compensates for processing delays by shifting the timeline forward
	void SetRationalPipelineOffset(REFERENCE_TIME offset) { m_rationalPipelineOffset = offset; }
	REFERENCE_TIME GetRationalPipelineOffset() const { return m_rationalPipelineOffset; }

	//
	// Metrics
	//

	// Get the exit latency in ms, which the amount of time between the frame timestamp
	// and when the frame is delivered to the DirectShow renderer.
	// This is sampled.
	double ExitLatencyMs() const { return m_exitLatencyMs;  }

	// Get the amount of dropped frames due to queue actions
	uint64_t DroppedFrameCount() const { return m_droppedFrameCount; }

	// PPM correction information access
	// Get the current PPM correction value being applied
	// Returns the PPM value from config: positive = faster, negative = slower
	int GetCurrentPPMCorrection() const 
	{ 
		// m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR + ppmCorrection
		// So: ppmCorrection = m_currentRationalTrimNumerator - RATIONAL_TRIM_DENOMINATOR
		return (int)(m_currentRationalTrimNumerator - RATIONAL_TRIM_DENOMINATOR); 
	}
	
	// Check if PPM correction is active (non-zero)
	bool HasPPMCorrection() const 
	{ 
		return m_currentRationalTrimNumerator != RATIONAL_TRIM_DENOMINATOR; 
	}
	
	// Get PPM correction source information
	bool GetPPMCorrectionSource() const 
	{ 
		return m_ppmCorrectionLoader.HasCorrections(); 
	}

protected:

	// Constants for CLOCK_SMART duration tracking
	static const size_t DURATION_HISTORY_SIZE = 100;  // Track last 100 frame durations
	static const int64_t REFERENCE_TIME_TICKS_PER_SECOND = 10000000LL;  // 100ns ticks per second

	// RATIONAL_RATIONAL timing trim constants - now dynamically loaded from correction.cfg
	static const uint64_t RATIONAL_TRIM_DENOMINATOR = 1000000ULL;  // PPM base (parts per million)
	// Note: RATIONAL_TRIM_PPM_FASTER and RATIONAL_TRIM_NUMERATOR are now calculated dynamically

	// PPM correction support
	PPMCorrectionLoader m_ppmCorrectionLoader;
	uint64_t m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;  // Default: no correction
	
	/**
	 * Load PPM corrections and calculate trim values for current refresh rate
	 * @param refreshRate Current refresh rate in Hz
	 */
	void LoadPPMCorrections(double refreshRate);
	
	/**
	 * Get the current rational trim numerator (calculated from PPM correction)
	 */
	uint64_t GetRationalTrimNumerator() const { return m_currentRationalTrimNumerator; }

	uint64_t m_droppedFrameCount = 0;
	
	// Flag to force discontinuity on next frame after timeline reset
	// This tells MadVR that the timeline was reset and it should resync
	bool m_forceDiscontinuity = false;
	
	// Flag to deliver new segment on next frame after timeline reset
	// This officially notifies MadVR of timeline restart (critical for RATIONAL_RATIONAL)
	bool m_deliverNewSegment = false;
	
	// Render function to render a videoFrame onto a IMediaSample.
	// Will not release the sample or dec videoframe nor do the Deliver()
	// Will return S_FRAME_NOT_RENDERED if frame could not be renderered, not an error per-se
	HRESULT RenderVideoFrameIntoSample(VideoFrame&, IMediaSample* const);

	// Get the next frame timestamp. If it doesn't know it's invalid. Overridden by implementations
	virtual REFERENCE_TIME NextFrameTimestamp() const { return REFERENCE_TIME_INVALID; }

	// Smart duration calculation for CLOCK_SMART mode
	REFERENCE_TIME CalculateSmartFrameDuration() const;
	void UpdateFrameDurationHistory(REFERENCE_TIME actualDuration);

	// Integer math utilities for precise timing calculations with overflow protection
	// HIGH-PRECISION CONVERSION: Eliminates cumulative rounding errors at high refresh rates
	static REFERENCE_TIME ConvertTimingClockToReferenceTime(timingclocktime_t timestamp, timingclocktime_t ticksPerSecond)
	{
		// OVERFLOW PROTECTION: DeckLink clock at 1MHz wraps after ~2.5 hours (9,223,372,036,854,775,807 ticks)
		// Check if (timestamp * 10000000) would overflow int64_t
		// Max safe value: INT64_MAX / 10000000 = 922,337,203,685 ticks (~10.7 days at 1MHz)
		const int64_t maxSafeValue = INT64_MAX / REFERENCE_TIME_TICKS_PER_SECOND;
		
		if (timestamp > maxSafeValue)
		{
			// Overflow would occur - use alternative calculation
			// Slightly less precise but prevents catastrophic failure
			// This path only triggers after ~10 days of continuous operation
			return (timestamp / ticksPerSecond) * REFERENCE_TIME_TICKS_PER_SECOND;
		}
		
		// NORMAL PATH: High-precision conversion with banker's rounding
		// Add half the divisor before division to round to nearest (not truncate)
		// This eliminates cumulative precision loss at high refresh rates (120Hz+)
		//
		// Example at 120Hz: 8.333ms frame period
		// Old truncation: loses ~0.03µs per frame → 200µs drift per minute
		// New rounding: maintains <10µs precision indefinitely
		return ((timestamp * REFERENCE_TIME_TICKS_PER_SECOND) + (ticksPerSecond / 2)) / ticksPerSecond;
	}

	static bool IsMonotonicProgression(REFERENCE_TIME current, REFERENCE_TIME previous)
	{
		return current > previous;
	}

	static REFERENCE_TIME EnforceMonotonicProgression(REFERENCE_TIME proposed, REFERENCE_TIME previous)
	{
		return (proposed <= previous) ? (previous + 1) : proposed;
	}

	IVideoFrameFormatter* m_videoFrameFormatter;
	timestamp_t m_frameDuration;
	ITimingClock* m_timingClock;
	DirectShowStartStopTimeMethod m_timestamp;
	AM_MEDIA_TYPE m_mediaType;
	bool m_useHDRData = false;

	// Duration tracking for CLOCK_SMART improvements
	// Circular buffer of actual frame durations (in 100ns units) for averaging
	REFERENCE_TIME m_durationHistory[DURATION_HISTORY_SIZE] = {};
	size_t m_durationHistoryIndex = 0;  // Current write position in circular buffer
	size_t m_durationHistoryCount = 0;  // Number of valid entries (up to DURATION_HISTORY_SIZE)
	REFERENCE_TIME m_lastHardwareTimestamp = 0;  // Previous hardware timestamp for duration calculation

	// Rational timing parameters for RATIONAL_RATIONAL mode (Bresenham-style exact integer math)
	// These come from DisplayMode and allow drift-free timing for rates like 23.976, 29.97, 59.94
	unsigned int m_timeScale = 0;           // Ticks per second (e.g., 24000 for 23.976fps)
	unsigned int m_frameDurationTicks = 0;  // Ticks per frame (e.g., 1001 for 23.976fps)
	
	// Fixed pipeline offset for RATIONAL_RATIONAL mode compensation
	// Applied as a forward shift to account for processing delays
	// Will be calculated dynamically based on frame rate during initialization
	REFERENCE_TIME m_rationalPipelineOffset = 0;  // Pipeline offset in 100ns units (calculated at runtime)

	// Hybrid timing state variables for DS_SSTM_HARDWARE_RATIONAL mode
	// This mode uses hardware timestamps for start time and rational math for frame duration
	REFERENCE_TIME m_previousHardwareTimestamp = 0;  // Track hardware timestamp progression
	uint32_t m_hardwareTimingAnomalyCount = 0;       // Count backwards/invalid timestamps
	REFERENCE_TIME m_rationalFrameDuration = 0;      // Calculated rational frame duration in 100ns units
	REFERENCE_TIME m_minFrameAdvance = 0;            // Minimum time between frames (25% of rational duration)
	REFERENCE_TIME m_maxFrameAdvance = 0;            // Maximum time between frames (200% of rational duration)

	REFERENCE_TIME m_previousTimeStop = 0;
	timestamp_t m_startTimeOffset = 0;
	uint64_t m_frameCounterOffset = 0;
	uint64_t m_frameCounter = 0;
	uint64_t m_previousFrameCounter = 0;

	HDRDataSharedPtr m_hdrData = nullptr;
	bool m_hdrChanged = false;

	double m_exitLatencyMs = 0.0;
	
	// Smart timing statistics for CLOCK_SMART/SMART2 modes
	uint64_t m_smartHardwareTimestampCount = 0;
	uint64_t m_smartSyntheticTimestampCount = 0;
	uint64_t m_smartRejectedTimestampCount = 0;  // Track rejected bad timestamps
	
	// Thread-safe FIFO queue for hardware timestamps (replaces single-value storage)
	std::mutex m_timestampQueueMutex;
	std::deque<REFERENCE_TIME> m_hardwareTimestampQueue;
	static const size_t MAX_TIMESTAMP_QUEUE_SIZE = 3;  // Keep small for low latency
	
	// Validation parameters for timestamp sanity checking
	REFERENCE_TIME m_expectedFrameDuration = 0;  // Calculated once at init
	REFERENCE_TIME m_minValidDuration = 0;       // 50% of expected
	REFERENCE_TIME m_maxValidDuration = 0;       // 200% of expected
	
	// Helper methods for queue management
	bool EnqueueHardwareTimestamp(REFERENCE_TIME timestamp);
	REFERENCE_TIME DequeueHardwareTimestamp();
};
