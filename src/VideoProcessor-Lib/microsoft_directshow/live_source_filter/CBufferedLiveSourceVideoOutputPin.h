/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <ActivePictureTransitionModel.h>
#include <CaptureFrameQueue.h>
#include <DirectShowFrameDeliverer.h>
#include <DirectShowSegmentTransition.h>
#include <FrameProcessor.h>
#include <LiveOutputTrace.h>
#include <ProcessedFrameQueue.h>
#include <microsoft_directshow/DirectShowDefines.h>
#include "ALiveSourceVideoOutputPin.h"

#include "CLiveSource.h"

class GpuSubtitleDetector;


/**
 * This is an buffered output pin, any presented frame will be buffered first
 * and then a separate thread will deliver the buffers to the renderer.
 *
 * ASYNC CONVERSION ARCHITECTURE:
 * Raw frames ? Conversion Worker Thread ? Pre-Converted Samples ? Delivery Thread ? Renderer
 * 
 * This removes conversion time from the critical rendering path.
 *
 * THREAD SAFETY:
 * - m_captureFrameQueue: Owns raw frames from the capture device
 * - m_convertedQueueLock: Serializes converted-frame publication, historical
 *   scene tagging, and flush against the processed-frame transport boundary
 * - m_stateLock: Protects shared state variables (m_isBuffering, m_lastSeenFrameCounter, etc.)
 * 
 * Lock ordering (to prevent deadlock): rawQueueLock ? convertedQueueLock ? stateLock
 */
class CBufferedLiveSourceVideoOutputPin:
	public ALiveSourceVideoOutputPin,
	public CAMThread
{
public:

	CBufferedLiveSourceVideoOutputPin(
		CLiveSource* filter,
		CCritSec* pLock,
		HRESULT* phr);
	virtual ~CBufferedLiveSourceVideoOutputPin();

	// CBaseOutputPin
	HRESULT Active() override;
	HRESULT Inactive() override;

	// ALiveSourceVideoOutputPin
	HRESULT OnVideoFrame(VideoFrame&) override;
	void SetFrameQueueMaxSize(size_t) override;
	LONG GetAllocatorBufferCount() const override;
	void SetSceneAwareTimingCorrection(bool enabled) override;
	void SetSceneCorrectionUpstreamSample(bool enabled) override;
	void SetSubtitleRepositioning(bool enabled) override;
	void SetSubtitleRepositioningMode(SubtitleRepositionMode mode) override;
	void SetSceneTimingRates(double displayRefreshRateHz, double deliveryRateHz) override;
	void SetSceneTimingReadiness(bool ready, uint64_t intervalsObserved) override;
	void SetSceneTimingPhase(int64_t vblankQpc, int64_t refreshPeriodQpc, int64_t qpcFrequency) override;
	uint64_t SceneAwareCorrectionDropCount() const override { return m_sceneAwareCorrectionDropCount.load(std::memory_order_relaxed); }
	uint64_t SceneAwareCorrectionRepeatCount() const override { return m_sceneAwareCorrectionRepeatCount.load(std::memory_order_relaxed); }
	uint64_t SceneAwareDetectedCount() const override { return m_sceneAwareDetectedCount.load(std::memory_order_relaxed); }
	uint64_t SceneAwareLateCandidateCount() const override { return m_sceneAwareLateCandidateCount.load(std::memory_order_relaxed); }
	bool GetSceneTimingPrediction(double& secondsUntilCorrection,
		double& secondsUntilPlan, int& action, bool& planned) const override;
	bool GetSceneTimingLastCorrection(int& action,
		double& secondsFromDeadline, uint64_t& correctionTick) const override;
	bool SceneTimingRatesCompatible() const override
	{
		return m_sceneTimingRatesCompatible.load(std::memory_order_acquire);
	}
	bool GetActivePictureAspectRatio(double& aspectRatio) const override;
	bool GetActivePictureRectangle(ActivePictureRectangle& rectangle) const override;
	size_t GetFrameQueueSize() override;
	bool GetLivenessSnapshot(RendererLivenessSnapshot& snapshot) const override;
	void Reset() override;
	REFERENCE_TIME NextFrameTimestamp() const override;
	void OnBadTimestampDetected() override;

	// Add this public method declaration in the public section
	size_t GetConvertedQueueSize();

	REFERENCE_TIME NowStreamTime(CBaseFilter* f);


private:

	HANDLE m_hConvertedAvailableEvent = nullptr;  // Auto-reset event: signaled when converted samples are available
	HANDLE m_hConvertedSemaphore = nullptr;  // Semaphore: count of converted samples available

	std::atomic<size_t> m_frameQueueMaxSize = 8;

	//
	// QUEUE INFRASTRUCTURE (with dedicated locks)
	//
	
	// Raw frame queue (input from capture device). The queue owns the captured
	// source-buffer reference until the conversion worker takes the frame.
	CaptureFrameQueue m_captureFrameQueue{ 32 };
	
	// Pre-converted sample queue (output from conversion worker)
	// Protected by: m_convertedQueueLock
	enum class SceneCorrectionAction : uint8_t
	{
		None,
		Drop,
		Repeat
	};

	ProcessedFrameQueue m_processedFrameQueue{ 32 };
	CCritSec m_convertedQueueLock;
	FrameProcessor m_frameProcessor;
	DirectShowFrameDeliverer m_directShowFrameDeliverer;
	DirectShowSegmentTransition m_directShowSegmentTransition;
	// The trace is a VP-only, bounded diagnostic snapshot. It has no renderer
	// queue state and never performs file I/O from a worker or callback.
	LiveOutputTrace m_liveOutputTrace;
	// One record per second, kept separately so per-frame events cannot evict
	// the long-run OSD-equivalent queue history.
	LiveOutputTrace m_liveOutputMetricsTrace;
	std::atomic<uint64_t> m_liveOutputTraceExportOrdinal{ 0 };
	uint64_t m_liveOutputTraceRunId = 0;

	// This option is deliberately off by default.  When false, conversion does
	// no scene analysis and delivery follows the pre-existing path exactly.
	std::atomic_bool m_sceneAwareTimingCorrection = false;
	std::atomic_bool m_sceneCorrectionUpstreamSample = false;
	// Disabled by default. When enabled, the conversion worker may relocate
	// tracked subtitle glyphs crossing a stable bottom black bar in P010 output.
	std::atomic<SubtitleRepositionMode> m_subtitleRepositionMode =
		SubtitleRepositionMode::DISABLED;
	std::atomic<uint64_t> m_subtitleRelocationCount = 0;
	std::atomic<uint64_t> m_subtitleAnalysisGeneration = 0;
	std::atomic<uint64_t> m_subtitleLastSubmitTick = 0;

	struct SubtitleRect
	{
		int left = 0;
		int top = 0;
		int right = 0;
		int bottom = 0;
	};

	struct SubtitleAnalysisFrame
	{
		uint64_t generation = 0;
		uint64_t frameNumber = 0;
		uint64_t submittedTick = 0;
		uint64_t sceneEventId = 0;
		// A cheap bar-band signature from the delivery thread.  BASIC uses it
		// to avoid re-running Windows OCR while a confirmed caption is unchanged.
		bool subtitleSignatureChanged = false;
		int fullWidth = 0;
		int fullHeight = 0;
		int scale = 1;
		int width = 0;
		int height = 0;
		uint16_t blackCode = 64;
		std::vector<uint16_t> luma;
	};

	struct SubtitleAnalysisResult
	{
		uint64_t generation = 0;
		uint64_t frameNumber = 0;
		uint64_t producedTick = 0;
		int fullWidth = 0;
		int fullHeight = 0;
		int pictureTop = 0;
		int pictureBottom = 0;
		int panelLeft = 0;
		int panelRight = 0;
		SubtitleRect source;
		uint16_t blackCode = 64;
		uint16_t confidence = 0;
		int analysisScale = 1;
		int maskLeft = 0;
		int maskTop = 0;
		int maskWidth = 0;
		int maskHeight = 0;
		std::shared_ptr<const std::vector<uint8_t>> textMask;
		std::shared_ptr<const std::vector<uint16_t>> textReferenceLuma;
		bool barStable = false;
		bool sourceAtTop = false;
		bool ocrBased = false;
		bool modelBased = false;
		bool active = false;
	};

	struct SubtitleTrackerState
	{
		int pendingPictureTop = 0;
		int stablePictureTop = 0;
		int pendingPictureBottom = 0;
		int stablePictureBottom = 0;
		uint8_t topBarHits = 0;
		uint8_t topBarMisses = 0;
		uint8_t barHits = 0;
		uint8_t barMisses = 0;
		SubtitleRect candidate;
		SubtitleRect tracked;
		SubtitleRect retainedPanel;
		std::vector<SubtitleRect> candidateWords;
		std::vector<SubtitleRect> trackedWords;
		uint64_t candidateContentHash = 0;
		uint64_t trackedContentHash = 0;
		uint64_t lastDetectionTick = 0;
		uint64_t lastSceneEventId = 0;
		// Score from the last geometry that was confirmed by the tracker.  A
		// one-frame model observation must never replace a coherent subtitle
		// with a nominal (or zero) confidence result.
		int confirmedScore = 0;
		int typicalLineHeight = 0;
		uint8_t candidateHits = 0;
		uint8_t trackMisses = 0;
		uint8_t styleSamples = 0;
		std::vector<uint16_t> previousLuma;
		bool topBarStable = false;
		bool candidateAtTop = false;
		bool trackedAtTop = false;
		bool trackedFromOcr = false;
		bool candidateFromModel = false;
		bool trackedFromModel = false;
		bool barStable = false;
		bool active = false;
	};

	std::thread m_subtitleAnalysisThread;
	std::mutex m_subtitleAnalysisMutex;
	std::condition_variable m_subtitleAnalysisCondition;
	bool m_subtitleWorkerStop = false;
	bool m_subtitleJobPending = false;
	SubtitleAnalysisFrame m_subtitlePendingFrame;
	SubtitleAnalysisResult m_subtitleLatestResult;
	std::vector<uint16_t> m_subtitleRecycledLuma;
	// Created by the background worker in ADVANCED mode. Shutdown keeps a
	// shared reference so it can request cancellation before joining the worker.
	std::shared_ptr<GpuSubtitleDetector> m_subtitleGpuDetector;

	// Conversion-worker-owned scratch storage. Reused so enabled processing
	// does not allocate on every delivered frame.
	std::vector<uint16_t> m_subtitleScratchY;
	std::vector<uint16_t> m_subtitleScratchUV;
	std::vector<uint8_t> m_subtitleGlyphMask;
	std::vector<uint8_t> m_subtitleGlyphCandidateMask;
	std::vector<uint8_t> m_subtitleGlyphGrowthMask;
	std::vector<int> m_subtitleGlyphFlood;
	// Conversion-thread-owned temporal glyph state. The detector supplies
	// caption geometry; actual glyph pixels are accepted only when they recur
	// across adjacent live frames. This rejects moving picture detail without
	// OCR-redrawing the subtitle.
	std::vector<uint16_t> m_subtitleCachedGlyphY;
	std::vector<uint8_t> m_subtitleCachedGlyphMask;
	std::vector<uint8_t> m_subtitlePreviousGlyphMask;
	std::vector<uint8_t> m_subtitlePrevious2GlyphMask;
	int m_subtitleCachedFrameWidth = 0;
	int m_subtitleCachedFrameHeight = 0;
	int m_subtitleCachedSourceLeft = 0;
	int m_subtitleCachedSourceTop = 0;
	int m_subtitleCachedSourceWidth = 0;
	int m_subtitleCachedSourceHeight = 0;
	int m_subtitleCachedTargetTop = 0;
	int m_subtitleCachedSourceClearLeft = 0;
	int m_subtitleCachedSourceClearTop = 0;
	int m_subtitleCachedSourceClearRight = 0;
	int m_subtitleCachedSourceClearBottom = 0;
	int m_subtitleCachedBackgroundLeft = 0;
	int m_subtitleCachedBackgroundTop = 0;
	int m_subtitleCachedBackgroundRight = 0;
	int m_subtitleCachedBackgroundBottom = 0;
	uint16_t m_subtitleCachedPanelY = 0;
	uint64_t m_subtitleCachedSceneEventId = 0;
	uint64_t m_subtitleCachedSignatureGeneration = 0;
	size_t m_subtitleCachedGlyphCount = 0;
	uint8_t m_subtitleTemporalSampleCount = 0;
	// Opaque subtitle-panel luma is latched per scene. It is consulted only
	// while Scene Detect is enabled; otherwise panels use video black.
	std::atomic<uint16_t> m_subtitleSceneAverageLumaCode = 64;
	std::atomic_bool m_subtitlePanelLumaInitialized = false;
	std::atomic_bool m_subtitleTrackActive = false;
	std::atomic<uint64_t> m_subtitleSceneEventId = 0;
	std::atomic<int> m_subtitlePanelHalfWidthPixels = 0;
	std::atomic<int> m_subtitlePanelHeightPixels = 0;
	std::atomic<int> m_subtitlePictureTopPixels = 0;
	std::atomic<int> m_subtitlePictureBottomPixels = 0;
	std::atomic<uint64_t> m_subtitleFastSignature = 0;
	std::atomic<uint64_t> m_subtitleSignatureGeneration = 0;
	std::atomic<uint64_t> m_sceneDetectorGeneration = 0;
	// Invalidates queued scene decisions and the delivery-owned output cadence
	// whenever the graph, display timing, or feature state changes.
	std::atomic<uint64_t> m_sceneTimingGeneration = 0;
	std::atomic<uint64_t> m_sceneEventSequence = 0;
	std::atomic<uint64_t> m_sceneAwareDetectedCount = 0;
	std::atomic<uint64_t> m_sceneAwareLateCandidateCount = 0;
	std::atomic<uint64_t> m_sceneAwareCorrectionDropCount = 0;
	std::atomic<uint64_t> m_sceneAwareCorrectionRepeatCount = 0;
	// Delivery-thread-owned display-versus-capture content phase, published for
	// diagnostics in microframes. Positive means the display needs an extra
	// output sample (repeat); negative means capture has supplied an extra
	// content frame (drop).
	std::atomic<int64_t> m_scenePhasePpmUnits = 0;
	// Published by the delivery thread for the OSD.  The prediction follows the
	// signed accumulated phase, not a nominal fixed cadence.
	std::atomic<double> m_sceneSecondsUntilCorrection = 0.0;
	std::atomic<double> m_sceneSecondsUntilPlan = 0.0;
	std::atomic<int> m_scenePredictedAction = 0; // +1 repeat, -1 drop
	std::atomic_bool m_sceneCorrectionPlanned = false;
	std::atomic<int> m_sceneLastCorrectionAction = 0; // +1 repeat, -1 drop
	std::atomic<double> m_sceneLastCorrectionSecondsFromDeadline = 0.0;
	std::atomic<uint64_t> m_sceneLastCorrectionTick = 0;
	std::atomic<double> m_sceneDisplayRefreshRateHz = 0.0;
	std::atomic<double> m_sceneDeliveryRateHz = 0.0;
	std::atomic_bool m_sceneTimingRatesCompatible = false;
	std::atomic<bool> m_sceneTimingReady = false;
	std::atomic<uint64_t> m_sceneWarmupIntervals = 0;
	// Latest physical-vblank snapshot from the UI's display sampler.  It is
	// deliberately separate from the rate estimate: rate predicts *when* an
	// event is due, while this phase identifies the exact display interval.
	std::atomic<int64_t> m_sceneLastVBlankQpc = 0;
	std::atomic<int64_t> m_sceneRefreshPeriodQpc = 0;
	std::atomic<int64_t> m_sceneQpcFrequency = 0;
	// Delivery and reset can run concurrently.  Keep correction history atomic
	// so a resync cannot race the delivery thread or require another queue lock.
	std::atomic<DWORD> m_lastSceneAwareCorrectionTime = 0;
	std::atomic<uint64_t> m_lastCorrectedSceneEventId = 0;
	
	//
	// SHARED STATE (protected by m_stateLock)
	//
	CCritSec m_stateLock;  // Protects shared state variables below
	
	std::atomic_bool m_isActive = false;
	std::atomic_bool m_stopping = false;
	// Serializes the entire downstream flush transaction. This is distinct
	// from m_deliveryGate because BeginFlush must be sent before waiting for a
	// Deliver/Receive call that may currently be blocked in the renderer.
	CCritSec m_resetTransactionGate;
	// Serializes downstream Deliver calls with flush/NewSegment during Reset.
	// BeginFlush is sent before Reset takes this gate so a blocked renderer can
	// return; no old-epoch sample can then cross the segment boundary.
	CCritSec m_deliveryGate;
	std::atomic_bool m_deliveryFlushing = false;
	// Identifies the current queue epoch. A conversion that began before a
	// reset/recovery must not publish its sample into the new epoch.
	std::atomic<uint64_t> m_queueEpoch = 0;
	
	std::atomic_bool m_isBuffering = false; // gate delivery until converted queue is primed
	uint64_t m_lastSeenFrameCounter = 0;    // Track frame counter for discontinuity detection
	DWORD m_lastAutoPurgeTime = 0;          // Last time we auto-purged the converted queue
	DWORD m_bufferingExitTime = 0;          // When we last exited buffering mode (for grace period)
	CCritSec m_rawDiagnosticsLock;
	uint64_t m_rawOverflowLogCount = 0;      // Protected by m_rawDiagnosticsLock
	DWORD m_lastRawOverflowLogTime = 0;      // Protected by m_rawDiagnosticsLock

	// Core proactive frame management
	HANDLE m_hFrameAvailableEvent = nullptr;  // Event signaled when frames are added to the queue
	HANDLE m_hShutdownEvent = nullptr;        // Event signaled when thread should exit

	
	// Async conversion infrastructure
	HANDLE m_hConversionThread = nullptr;             // Conversion worker thread handle
	HANDLE m_hConversionShutdownEvent = nullptr;      // Event signaled when conversion thread should exit
	DWORD m_conversionThreadId = 0;                   // Conversion thread ID
	std::atomic<uint64_t> m_totalConversionTimeUs = 0;  // Total conversion time for metrics
	std::atomic<uint64_t> m_conversionFrameCount = 0;   // Number of frames converted

	// VP-0054 liveness evidence. These atomics keep diagnostics from taking a
	// queue or delivery lock while an external renderer is stalled.
	std::atomic<uint32_t> m_captureThreadId = 0;
	std::atomic<uint32_t> m_deliveryThreadId = 0;
	std::atomic<uint64_t> m_inputFrameCount = 0;
	std::atomic<uint64_t> m_dequeueCount = 0;
	std::atomic<uint64_t> m_deliveryAttemptCount = 0;
	std::atomic<uint64_t> m_deliverySuccessCount = 0;
	std::atomic<uint64_t> m_currentEpochDeliverySuccessCount = 0;
	std::atomic<uint64_t> m_lastDeliverySuccessQueueEpoch = 0;
	std::atomic<uint64_t> m_lastInputTick = 0;
	std::atomic<uint64_t> m_lastConversionTick = 0;
	std::atomic<uint64_t> m_lastDequeueTick = 0;
	std::atomic<uint64_t> m_lastDeliveryStartTick = 0;
	std::atomic<uint64_t> m_lastDeliverySuccessTick = 0;
	std::atomic<size_t> m_publishedRawQueueDepth = 0;
	std::atomic<size_t> m_publishedConvertedQueueDepth = 0;
	std::atomic_bool m_deliveryInProgress = false;
	std::atomic_bool m_resetInProgress = false;
	
	// Essential metrics for proactive decisions (simplified)
	std::atomic<uint32_t> m_recentDeliveryFailures = 0;   // Simple failure counter (reset periodically)
	DWORD m_lastQueueWarning = 0;                         // Throttle warnings only

	// Helper to get effective buffering target (half of queue size, at least 3 frames)
	size_t GetBufferingTarget();

	// Thread function, upon return thread exist.
	// Return codes > 0 indicate an error occured
	DWORD ThreadProc();
	
	// Conversion worker thread function
	static DWORD WINAPI ConversionThreadProc(LPVOID lpParameter);
	DWORD ConversionWorker();

	struct ActivePictureDetectorState
	{
		ActivePictureTransitionModel transition;
	};

	// Published lock-free for UI/renderer shortcut handling. Detection itself
	// is conversion-worker-owned and samples only sparse P010 luma positions.
	std::atomic<double> m_activePictureAspectRatio = 0.0;
	std::atomic_bool m_activePictureAspectStable = false;
	std::atomic<uint64_t> m_activePictureDetectorGeneration = 0;
	std::atomic<uint64_t> m_activePictureRectangleGeneration = 0;
	mutable std::mutex m_activePictureRectangleMutex;
	ActivePictureRectangle m_activePictureRectangle;

	// Adapts the DirectShow P010 sample to the renderer-neutral detector.
	bool AnalyzeSceneDetector(IMediaSample* sample, class SceneDetector& detector,
		uint64_t sourceSequence, timingclocktime_t timestamp, uint64_t generation,
		uint64_t& sceneEventId, uint8_t& eventFramesBack, uint16_t& averageLuma);
	void UpdateActivePictureAspectRatio(IMediaSample* sample, uint64_t frameNumber,
		ActivePictureDetectorState& state);
	void PublishActivePictureTransition(
		const ActivePictureTransitionDecision& decision);
	bool RelocateSubtitleInP010(IMediaSample* sample, uint64_t frameNumber);
	void StartSubtitleAnalysisWorker();
	void StopSubtitleAnalysisWorker();
	void ResetSubtitleAnalysis();
	void SubmitSubtitleAnalysis(const uint16_t* yPlane, int width, int height,
		uint64_t frameNumber, uint16_t blackCode);
	void SubtitleAnalysisWorker();
	SubtitleAnalysisResult AnalyzeSubtitleFrame(const SubtitleAnalysisFrame& frame,
		SubtitleTrackerState& tracker, GpuSubtitleDetector* gpuDetector);
	bool DetectSubtitleWithWindowsOcr(const SubtitleAnalysisFrame& frame,
		int pictureTop, int pictureBottom, SubtitleRect& detected,
		int& score, bool& atTop, bool& textObserved,
		uint64_t& contentHash, int& lineCount,
		int& representativeLineHeight, std::vector<SubtitleRect>& words);
	bool CompositeTrackedSubtitle(uint16_t* yPlane, uint16_t* uvPlane,
		int width, int height, uint16_t blackCode,
		const SubtitleAnalysisResult& result);
	HRESULT CloneSampleForUpstreamRepeat(IMediaSample* source,
		REFERENCE_TIME start, REFERENCE_TIME stop, IMediaSample** repeatSample);

	// Remove all raw frames and return the number released.
	size_t PurgeQueue();
	
	// Purge converted sample queue
	// CALLER MUST HOLD m_convertedQueueLock
	void PurgeConvertedQueue();
	void WriteLiveOutputTrace(const char* boundary);

	// Calculate next frame timestamp with enhanced logic for CLOCK_SMART
	REFERENCE_TIME CalculateEnhancedNextTimestamp() const;
	
	// Simplified proactive frame management
	size_t GetProactiveQueueTarget() const;
	bool ShouldProactivelyDrop() const;

	// Simple health monitoring for proactive management
	struct ProactiveQueueMetrics
	{
		size_t currentSize;
		size_t maxSize;
		size_t proactiveTarget;
		uint64_t totalDropped;
		uint32_t recentFailures;
		bool isHealthy;
		
		// Async conversion metrics
		size_t convertedQueueSize;
		uint64_t avgConversionTimeUs;
	};
	
	ProactiveQueueMetrics GetProactiveMetrics() const;

	//
	// PENDING TIMESTAMP HISTORY for late-binding
	// Records timestamps as samples are added to converted queue.
	// Allows late-binding to find "next frame" timestamps even when queue is empty.
	// This is the key fix for CLOCK_SMART/SMART2 at low frame rates (23.976Hz).
	//
	struct PendingTimestamp {
		REFERENCE_TIME timeStart = 0;
		uint64_t sequenceNumber = 0;  // Monotonic to track ordering
	};
	static const size_t PENDING_TIMESTAMP_SIZE = 64;  // Keep last 64 pending timestamps
	PendingTimestamp m_pendingTimestamps[PENDING_TIMESTAMP_SIZE] = {};
	size_t m_pendingTimestampIndex = 0;
	uint64_t m_pendingSequenceCounter = 0;
	mutable std::mutex m_pendingTimestampMutex;
	
	// Record a pending timestamp when sample is added to converted queue
	void RecordPendingTimestamp(REFERENCE_TIME timeStart);
	
	// Find the next pending timestamp after currentStart that's close to theoreticalStop within tolerance
	REFERENCE_TIME FindNextPendingTimestamp(REFERENCE_TIME currentStart, REFERENCE_TIME theoreticalStop, REFERENCE_TIME tolerance) const;
	
	// Clear pending timestamp history (called on reset/discontinuity)
	void ClearPendingTimestamps();
};
