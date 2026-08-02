/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <atomic>
#include <cstdint>
#include <memory>
#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <microsoft_directshow/DirectShowRendererStartStopTimeMethod.h>
#include <microsoft_directshow/DirectShowVideoTimingAdapter.h>
#include <microsoft_directshow/DirectShowDefines.h>
#include <PPMCorrectionLoader.h>
#include <AutoPpmCalibrator.h>
#include <IntegerMath.h>
#include <RendererLiveness.h>
#include <RendererResetRequestLatch.h>
#include <SubtitleRepositionMode.h>

#include "CLiveSource.h"

// This is not an error by itself
static const HRESULT S_FRAME_NOT_RENDERED = 1;

// A coherent, temporally stable active-image result. Coordinates are expressed
// in the delivered raster and right/bottom are exclusive.
struct ActivePictureRectangle
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	int rasterWidth = 0;
	int rasterHeight = 0;
	double aspectRatio = 0.0;
	uint64_t generation = 0;
	bool stable = false;
};

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
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

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

	// Buffered pins override this.  Keeping the default no-op preserves the
	// unbuffered source behaviour and avoids changing the COM interface.
	virtual void SetSceneAwareTimingCorrection(bool) {}
	virtual void SetSceneCorrectionUpstreamSample(bool) {}
	virtual void SetSubtitleRepositioning(bool) {}
	virtual void SetSubtitleRepositioningMode(SubtitleRepositionMode mode)
	{
		SetSubtitleRepositioning(mode != SubtitleRepositionMode::DISABLED);
	}
	virtual void SetSceneTimingRates(double, double) {}
	virtual void SetSceneTimingReadiness(bool, uint64_t) {}
	virtual void SetSceneTimingPhase(int64_t, int64_t, int64_t) {}
	virtual void SetOutputReadinessDeliveryReserve(size_t) {}
	virtual void SetQueueFramePolicy(size_t, size_t, bool) {}
	// Returns a temporally stable estimate of the active picture (excluding
	// encoded black bars). Buffered P010 sources override this.
	virtual bool GetActivePictureAspectRatio(double& aspectRatio) const
	{
		aspectRatio = 0.0;
		return false;
	}
	virtual bool GetActivePictureRectangle(ActivePictureRectangle& rectangle) const
	{
		rectangle = {};
		return false;
	}
	// Queue an aspect-only VIDEOINFOHEADER2 update on the next delivered
	// sample. The downstream pin must explicitly accept the dynamic media
	// type; dimensions, subtype, frame rate, and allocator remain unchanged.
	bool RequestDynamicPictureAspectRatio(
		unsigned long aspectX, unsigned long aspectY);

	// Get the size of the queue.
	// Zero means no queueing going on.
	virtual size_t GetFrameQueueSize() = 0;
	virtual bool GetLivenessSnapshot(RendererLivenessSnapshot& snapshot) const
	{
		snapshot = {};
		return false;
	}
	virtual bool GetLatencySnapshot(RendererLatencySnapshot&) const
	{
		return false;
	}
	void SetResetRequestSink(
		std::shared_ptr<IRendererResetRequestSink> sink)
	{
		m_resetRequestLatch.SetSink(std::move(sink));
	}
	bool RequestSourceGapGraphReprime()
	{
		return RequestCoordinatedReset(
			"material-source-counter-gap",
			RendererResetReason::SourceGapRecovery);
	}

	// Reset the internal state and the video stream.
	virtual void Reset();

	// Set fixed pipeline offset for RATIONAL_RATIONAL mode (in 100ns units)
	// This compensates for processing delays by shifting the timeline forward
	void SetRationalPipelineOffset(REFERENCE_TIME offset) { m_rationalPipelineOffset = offset; }
	REFERENCE_TIME GetRationalPipelineOffset() const { return m_rationalPipelineOffset; }
	void SetPresentationLeadFrames(size_t frames, bool configured)
	{
		m_presentationLeadFrames = frames > 16 ? 16 : frames;
		m_presentationLeadFramesConfigured = configured;
	}

	//
	// Metrics
	//

	// Get the exit latency in ms, which the amount of time between the frame timestamp
	// and when the frame is delivered to the DirectShow renderer.
	// This is sampled.
	double ExitLatencyMs() const { return m_exitLatencyMs.load(std::memory_order_relaxed);  }

	// Get the amount of dropped frames due to queue actions
	uint64_t DroppedFrameCount() const { return m_droppedFrameCount.load(std::memory_order_relaxed); }

	// Buffered pins override this with intentional Scene Detect correction drops.
	virtual uint64_t SceneAwareCorrectionDropCount() const { return 0; }
	virtual uint64_t SceneAwareCorrectionRepeatCount() const { return 0; }
	virtual uint64_t SceneAwareDetectedCount() const { return 0; }
	virtual uint64_t SceneAwareLateCandidateCount() const { return 0; }
	virtual bool GetSceneTimingPrediction(double& secondsUntilCorrection,
		double& secondsUntilPlan, int& action, bool& planned) const
	{
		secondsUntilCorrection = 0.0;
		secondsUntilPlan = 0.0;
		action = 0;
		planned = false;
		return false;
	}
	virtual bool GetSceneTimingLastCorrection(int& action,
		double& secondsFromDeadline, uint64_t& correctionTick) const
	{
		action = 0;
		secondsFromDeadline = 0.0;
		correctionTick = 0;
		return false;
	}
	virtual bool SceneTimingRatesCompatible() const { return false; }

	// PPM correction information access
	// Get the current PPM correction value being applied
	// Returns the applied PPM value. It is added to the timestamp-trim
	// numerator: positive = slower delivery, negative = faster delivery.
	int GetCurrentPPMCorrection() const 
	{ 
		// m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR + ppmCorrection
		// So: ppmCorrection = m_currentRationalTrimNumerator - RATIONAL_TRIM_DENOMINATOR
		return (int)((m_currentRationalTrimNumerator - RATIONAL_TRIM_DENOMINATOR)); 
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
	
	// Auto-calibration status access
	bool IsAutoCalibrating() const { return m_useAutoCalibration; }
	AutoPpmCalibrator::CalibrationStats GetAutoCalibrationStats() const { return m_autoPpmCalibrator.GetStats(); }
	
	// Feed PPM measurement from renderer to auto-calibrator
	// This is the single source of truth for PPM - calculated by DirectShowVideoRenderer::UpdatePPMMeasurement()
	void FeedPPMToCalibrator(int measuredPpm)
	{
		CAutoLock timingLock(&m_timingStateLock);
		if (m_useAutoCalibration && m_autoPpmCalibrator.IsActive())
		{
			// Feed the raw measured PPM to the calibrator
			// The calibrator will calculate remaining drift internally
			m_autoPpmCalibrator.OnPPM(measuredPpm);
			
			// Update trim numerator from calibrator (may have been adjusted)
			int autoPpm = m_autoPpmCalibrator.GetTotalPpmCorrection();
			m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR + autoPpm;
		}
	}
	
	// Frame duration statistics tracking
	// Returns average frame duration in milliseconds
	double GetAverageFrameDurationMs() const { return m_avgFrameDurationMs; }
	double GetMinFrameDurationMs() const { return m_minFrameDurationMs; }
	double GetMaxFrameDurationMs() const { return m_maxFrameDurationMs; }
	// VP-0066-2 rational-timing shadow validation. The legacy path remains the
	// source of all sample timestamps until this counter stays clean in field
	// evidence; no queue, worker, or DirectShow behavior depends on it.
	uint64_t RationalTimingShadowComparisonCount() const
	{
		return m_rationalTimingShadowComparisons.load(std::memory_order_relaxed);
	}
	uint64_t RationalTimingShadowMismatchCount() const
	{
		return m_rationalTimingShadowMismatches.load(std::memory_order_relaxed);
	}
	uint64_t RationalTimingControllerAppliedCount() const
	{
		return m_rationalTimingControllerApplied.load(std::memory_order_relaxed);
	}

	// Get the converted queue size (buffered mode only)
	virtual size_t GetConvertedQueueSize() const { return 0; }

	// Bound allocator memory while leaving enough samples for the queue and
	// downstream renderer. Buffered pins override this from their queue size.
	virtual LONG GetAllocatorBufferCount() const { return 16; }
	virtual void SetDownstreamPrimeTarget(size_t) {}
	LONG GetNegotiatedAllocatorBufferCount() const
	{
		return m_negotiatedAllocatorBufferCount.load(std::memory_order_acquire);
	}

	// Delivered timestamp history for late-binding lookup
	// CLOCK_SMART/SMART2 need to look up "next frame" timestamps, but that frame
	// may have already been delivered. Keep a circular buffer of recent deliveries.
	struct DeliveredTimestamp {
		REFERENCE_TIME timeStart = 0;
		REFERENCE_TIME timeStop = 0;
		uint64_t deliveryOrder = 0;  // Monotonic counter to track delivery order
	};
	static const size_t DELIVERED_HISTORY_SIZE = 100;
	DeliveredTimestamp m_deliveredHistory[DELIVERED_HISTORY_SIZE] = {};
	size_t m_deliveredHistoryIndex = 0;
	uint64_t m_deliveryCounter = 0;  // Monotonic counter
	mutable std::mutex m_deliveredHistoryMutex;
	
	// Record a timestamp after delivery
	void RecordDeliveredTimestamp(REFERENCE_TIME timeStart, REFERENCE_TIME timeStop)
	{
		std::lock_guard<std::mutex> lock(m_deliveredHistoryMutex);
		m_deliveredHistory[m_deliveredHistoryIndex].timeStart = timeStart;
		m_deliveredHistory[m_deliveredHistoryIndex].timeStop = timeStop;
		m_deliveredHistory[m_deliveredHistoryIndex].deliveryOrder = m_deliveryCounter++;
		m_deliveredHistoryIndex = (m_deliveredHistoryIndex + 1) % DELIVERED_HISTORY_SIZE;
	}
	
	// Find a delivered timestamp near the target (for late-binding)
	bool FindDeliveredTimestampNear(REFERENCE_TIME target, REFERENCE_TIME tolerance, REFERENCE_TIME& outStart) const
	{
		std::lock_guard<std::mutex> lock(m_deliveredHistoryMutex);
		
		REFERENCE_TIME bestStart = REFERENCE_TIME_INVALID;
		REFERENCE_TIME bestDelta = REFERENCE_TIME_INVALID;
		
		for (size_t i = 0; i < DELIVERED_HISTORY_SIZE; i++)
		{
			const auto& record = m_deliveredHistory[i];
			if (record.timeStart == 0)
				continue;  // Uninitialized slot
			
			const REFERENCE_TIME delta = abs(record.timeStart - target);
			if (delta <= tolerance)
			{
				if (bestStart == REFERENCE_TIME_INVALID || delta < bestDelta)
				{
					bestStart = record.timeStart;
					bestDelta = delta;
				}
			}
		}
		
		if (bestStart != REFERENCE_TIME_INVALID)
		{
			outStart = bestStart;
			return true;
		}
		return false;
	}
protected:
	std::atomic<LONG> m_negotiatedAllocatorBufferCount{ 0 };
	uint64_t AttachPendingMediaType(IMediaSample* sample);
	void CompletePendingMediaType(
		uint64_t generation, HRESULT deliveryResult);

	// Reset timestamp/media-time state without sending flush or segment
	// messages. Buffered pins use this while holding their serialized delivery
	// gate; the public Reset() wraps it in the normal DirectShow flush sequence.
	void ResetTimingState();
	// Bind value-only timing decisions to the buffered pipeline's authoritative
	// queue epoch. This has no DirectShow side effect.
	void ResetTimingControllerToPipelineEpoch(uint64_t epoch);

	// Constants for CLOCK_SMART duration tracking
	static const size_t DURATION_HISTORY_SIZE = 100;  // Track last 100 frame durations
	static const int64_t REFERENCE_TIME_TICKS_PER_SECOND = 10000000LL;  // 100ns ticks per second

	// RATIONAL_RATIONAL timing trim constants - dynamically loaded from VideoProcessor.cfg
	static const uint64_t RATIONAL_TRIM_DENOMINATOR = 1000000ULL;  // PPM base (parts per million)
	// Note: RATIONAL_TRIM_PPM_FASTER and RATIONAL_TRIM_NUMERATOR are now calculated dynamically

	// PPM correction support
	PPMCorrectionLoader m_ppmCorrectionLoader;
	uint64_t m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;  // Default: no correction
	std::unique_ptr<DirectShowVideoTimingAdapter> m_rationalTimingShadow;
	std::atomic<uint64_t> m_rationalTimingShadowComparisons = 0;
	std::atomic<uint64_t> m_rationalTimingShadowMismatches = 0;
	std::atomic<uint64_t> m_rationalTimingControllerApplied = 0;
	
	// Auto-calibration support
	AutoPpmCalibrator m_autoPpmCalibrator;
	bool m_useAutoCalibration = false;  // True if using auto-calibration (no config value or AUTO specified)
	
	/**
	 * Load PPM corrections and calculate trim values for current refresh rate
	 * @param refreshRate Current refresh rate in Hz
	 */
	void LoadPPMCorrections(double refreshRate);
	
	/**
	 * Get the current rational trim numerator (calculated from PPM correction or auto-calibration)
	 */
	uint64_t GetRationalTrimNumerator() const { return m_currentRationalTrimNumerator; }

	std::atomic<uint64_t> m_droppedFrameCount = 0;
	
	// Flag to force discontinuity on next frame after timeline reset
	// This tells MadVR that the timeline was reset and it should resync
	bool m_forceDiscontinuity = false;
	
	// Flag to deliver new segment on next frame after timeline reset
	// This officially notifies MadVR of timeline restart (critical for RATIONAL_RATIONAL)
	std::atomic_bool m_deliverNewSegment = false;
	
	// Render function to render a videoFrame onto a IMediaSample.
	// Will not release the sample or dec videoframe nor do the Deliver()
	// Will return S_FRAME_NOT_RENDERED if frame could not be renderered, not an error per-se
	HRESULT RenderVideoFrameIntoSample(VideoFrame&, IMediaSample* const);

	// Establish the same timestamp origin used by the legacy live queue when
	// its initial preroll completed.  This is intentionally narrower than
	// ResetTimingState(): the surrounding buffered pin has already established
	// the DirectShow segment and owns the one-time startup transition.
	void RestartTimingOriginAfterPreroll();

	// Get the next frame timestamp. If it doesn't know it's invalid. Overridden by implementations
	virtual REFERENCE_TIME NextFrameTimestamp() const { return REFERENCE_TIME_INVALID; }

	// Smart duration calculation for CLOCK_SMART mode
	REFERENCE_TIME CalculateSmartFrameDuration() const;
	void UpdateFrameDurationHistory(REFERENCE_TIME actualDuration);

	// Integer math utilities for precise timing calculations with overflow protection
	// HIGH-PRECISION CONVERSION: Eliminates cumulative rounding errors at high refresh rates
	static REFERENCE_TIME ConvertTimingClockToReferenceTime(timingclocktime_t timestamp, timingclocktime_t ticksPerSecond)
	{
		if (ticksPerSecond == 0)
			return 0;

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
		
		// NORMAL PATH: High-precision conversion with exact U64_MulDiv to prevent duplicate timestamps
		// This eliminates cumulative precision loss at high refresh rates (120Hz+)
		//
		// Example at 120Hz: 8.333ms frame period
		// Old truncation: loses ~0.03µs per frame → 200µs drift per minute
		// New rounding: maintains <10µs precision indefinitely
		return U64_MulDiv(timestamp, REFERENCE_TIME_TICKS_PER_SECOND, ticksPerSecond);
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
	CCritSec m_mediaTypeLock;
	CMediaType m_pendingMediaType;
	uint64_t m_pendingMediaTypeGeneration = 0;
	bool m_hasPendingMediaType = false;
	unsigned long m_pendingAspectX = 0;
	unsigned long m_pendingAspectY = 0;
	bool m_useHDRData = false;

	// Duration tracking for CLOCK_SMART improvements
	// Circular buffer of actual frame durations (in 100ns units) for averaging
	REFERENCE_TIME m_durationHistory[DURATION_HISTORY_SIZE] = {};
	size_t m_durationHistoryIndex = 0;  // Current write position in circular buffer
	size_t m_durationHistoryCount = 0;  // Number of valid entries (up to DURATION_HISTORY_SIZE)
	REFERENCE_TIME m_durationHistorySum = 0; // Running sum for O(1) SMART2 averaging
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
	bool m_frameCounterOffsetValid = false;
	uint64_t m_frameCounter = 0;
	uint64_t m_previousFrameCounter = 0;

	HDRDataSharedPtr m_hdrData = nullptr;
	bool m_hdrChanged = false;

	std::atomic<double> m_exitLatencyMs = 0.0;
	
	// Smart timing statistics for CLOCK_SMART/SMART2 modes
	uint64_t m_smartHardwareTimestampCount = 0;
	uint64_t m_smartSyntheticTimestampCount = 0;
	uint64_t m_smartRejectedTimestampCount = 0;  // Track rejected bad timestamps
	
	// SIMPLE TIMESTAMP LOOKUP: Rolling history of recent hardware timestamps
	// Instead of complex queue synchronization, just record timestamps as they arrive
	// and search for the first one that's reasonably larger than current
	struct TimestampRecord {
		uint64_t frameCounter = 0;
		REFERENCE_TIME timestamp = 0;
	};
	static const size_t TIMESTAMP_HISTORY_SIZE = 16;  // Keep last 16 frames
	TimestampRecord m_timestampHistory[TIMESTAMP_HISTORY_SIZE] = {};
	size_t m_timestampHistoryIndex = 0;
	std::mutex m_timestampHistoryMutex;
	
	// Helper to record a timestamp
	void RecordHardwareTimestamp(uint64_t frameCounter, REFERENCE_TIME timestamp);
	
	// Helper to find next timestamp greater than current
	REFERENCE_TIME FindNextHardwareTimestamp(REFERENCE_TIME currentTimestamp) const;

	// Lead time configuration for frame delivery timing
	// This adds a buffer time to prevent late deliveries to MadVR
	// Can be ramped from 0 to target over a configurable duration for smooth
	// startup; ramping is disabled by default.
	size_t m_presentationLeadFrames = 0;
	bool m_presentationLeadFramesConfigured = false;
	
	// Lead ramp duration configuration (in milliseconds)
	// Specifies how long to ramp from 0 to the resolved target lead
	// SetLeadRampDurationMs(0) selects the 5000ms (5 second) ramp default.
	uint64_t m_leadRampDurationMs = 0;  // Configurable lead ramp duration; 0 disables ramping
	uint64_t m_leadRampStartTimeMs = 0;    // Timestamp when ramp started (for time-based calculation)
	bool m_leadRampActive = false;         // Track if ramp has been initialized
	
	// Configure lead ramp duration
	// @param durationMs Duration in milliseconds to ramp from 0 to full LEADTIME (e.g., 5000 for 5 seconds)
	void SetLeadRampDurationMs(uint64_t durationMs) 
	{ 
		m_leadRampDurationMs = (durationMs > 0) ? durationMs : 5000;  // Zero selects the 5s ramp default
	}
	
	uint64_t GetLeadRampDurationMs() const 
	{ 
		return m_leadRampDurationMs; 
	}
	
	REFERENCE_TIME GetRampedLeadTime();
	
	// Virtual method for bad timestamp recovery (overridden in buffered implementation)
	virtual void OnBadTimestampDetected() {}
	bool RequestCoordinatedReset(
		const char* reason,
		RendererResetReason resetReason =
			RendererResetReason::LivenessRecovery);
	bool CoordinatedResetRequested() const
	{
		return m_resetRequestLatch.Pending();
	}
	void CompleteCoordinatedReset()
	{
		m_resetRequestLatch.Complete();
	}
	RendererResetRequestLatch m_resetRequestLatch;

	// Frame duration statistics tracking
	// These track the actual tick rate of timeStart/timeStop intervals
	double m_avgFrameDurationMs = 0.0;          // Running average frame duration in milliseconds
	double m_minFrameDurationMs = 0.0;          // Minimum frame duration observed
	double m_maxFrameDurationMs = 0.0;          // Maximum frame duration observed
	uint64_t m_durationSampleCount = 0;         // Number of samples taken
	uint64_t m_lastDurationLogFrame = 0;        // Last frame number when we logged stats
	
	// Helper method to track and log frame duration statistics
	void TrackFrameDuration(REFERENCE_TIME timeStart, REFERENCE_TIME timeStop, uint64_t frameNumber);

	// Serializes timing-state mutation with frame conversion. Reset, HDR updates,
	// PPM updates, and RenderVideoFrameIntoSample all use this lock.
	CCritSec m_timingStateLock;

};
