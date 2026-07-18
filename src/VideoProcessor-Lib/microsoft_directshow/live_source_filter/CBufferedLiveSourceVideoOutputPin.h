/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <deque>
#include <array>

#include <microsoft_directshow/DirectShowDefines.h>
#include "ALiveSourceVideoOutputPin.h"

#include "CLiveSource.h"


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
 * - m_rawQueueLock: Protects m_videoFrameQueue (raw frames from capture device)
 * - m_convertedQueueLock: Protects m_convertedSampleQueue (converted samples for delivery)
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
	void SetSceneTimingRates(double displayRefreshRateHz, double deliveryRateHz) override;
	void SetSceneTimingPhase(int64_t vblankQpc, int64_t refreshPeriodQpc, int64_t qpcFrequency) override;
	uint64_t SceneAwareCorrectionDropCount() const override { return m_sceneAwareCorrectionDropCount.load(std::memory_order_relaxed); }
	uint64_t SceneAwareCorrectionRepeatCount() const override { return m_sceneAwareCorrectionRepeatCount.load(std::memory_order_relaxed); }
	uint64_t SceneAwareDetectedCount() const override { return m_sceneAwareDetectedCount.load(std::memory_order_relaxed); }
	uint64_t SceneAwareLateCandidateCount() const override { return m_sceneAwareLateCandidateCount.load(std::memory_order_relaxed); }
	size_t GetFrameQueueSize() override;
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
	
	// Raw frame queue (input from capture device)
	// Protected by: m_rawQueueLock
	std::deque<VideoFrame> m_videoFrameQueue;
	CCritSec m_rawQueueLock;  // Protects m_videoFrameQueue only
	
	// Pre-converted sample queue (output from conversion worker)
	// Protected by: m_convertedQueueLock
	enum class SceneCorrectionAction : uint8_t
	{
		None,
		Drop,
		Repeat
	};

	struct ConvertedSample
	{
		IMediaSample* sample = nullptr;
		bool isSafeCorrectionPoint = false;
		uint64_t sceneEventId = 0;
		SceneCorrectionAction sceneCorrectionAction = SceneCorrectionAction::None;
	};
	std::deque<ConvertedSample> m_convertedSampleQueue;
	CCritSec m_convertedQueueLock;  // Protects m_convertedSampleQueue only

	// This option is deliberately off by default.  When false, conversion does
	// no scene analysis and delivery follows the pre-existing path exactly.
	std::atomic_bool m_sceneAwareTimingCorrection = false;
	std::atomic<uint64_t> m_sceneDetectorGeneration = 0;
	std::atomic<uint64_t> m_sceneEventSequence = 0;
	std::atomic<uint64_t> m_sceneAwareDetectedCount = 0;
	std::atomic<uint64_t> m_sceneAwareLateCandidateCount = 0;
	std::atomic<uint64_t> m_sceneAwareCorrectionDropCount = 0;
	std::atomic<uint64_t> m_sceneAwareCorrectionRepeatCount = 0;
	// Signed display-versus-delivery phase error in microframes. Positive means
	// the display has accumulated an extra refresh slot (sink repeat expected);
	// negative means delivery has accumulated an extra frame (sink drop expected).
	std::atomic<int64_t> m_scenePhasePpmUnits = 0;
	std::atomic<double> m_sceneDisplayRefreshRateHz = 0.0;
	std::atomic<double> m_sceneDeliveryRateHz = 0.0;
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
	
	std::atomic_bool m_isBuffering = false; // gate delivery until converted queue is primed
	uint64_t m_lastSeenFrameCounter = 0;    // Track frame counter for discontinuity detection
	DWORD m_lastAutoPurgeTime = 0;          // Last time we auto-purged the converted queue
	DWORD m_bufferingExitTime = 0;          // When we last exited buffering mode (for grace period)

	// Core proactive frame management
	HANDLE m_hFrameAvailableEvent = nullptr;  // Event signaled when frames are added to the queue
	HANDLE m_hShutdownEvent = nullptr;        // Event signaled when thread should exit

	
	// Async conversion infrastructure
	HANDLE m_hConversionThread = nullptr;             // Conversion worker thread handle
	HANDLE m_hConversionShutdownEvent = nullptr;      // Event signaled when conversion thread should exit
	DWORD m_conversionThreadId = 0;                   // Conversion thread ID
	std::atomic<uint64_t> m_totalConversionTimeUs = 0;  // Total conversion time for metrics
	std::atomic<uint64_t> m_conversionFrameCount = 0;   // Number of frames converted
	
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

	struct SceneSignature
	{
		static constexpr size_t COLUMNS = 32;
		static constexpr size_t ROWS = 18;
		static constexpr size_t HISTOGRAM_BINS = 16;
		std::array<uint16_t, COLUMNS * ROWS> luma{};
		std::array<uint16_t, HISTOGRAM_BINS> histogram{};
		uint32_t averageLuma = 0;
		bool valid = false;
	};

	struct SceneDetectorState
	{
		SceneSignature previous;
		SceneSignature pendingHardCut;
		uint8_t pendingHardCutFrames = 0;
		uint32_t pendingInitialAverageLumaDifference = 0;
		uint32_t pendingInitialChangedSampleCount = 0;
		uint32_t framesUntilNextEvent = 0;
		bool pendingHardCutValid = false;
		bool previousNearBlack = false;
	};

	// Reads a sparse luma grid from P010 output.  It is intentionally called
	// only by the conversion worker and only while the feature is enabled.
	bool IsSafeSceneAwareCorrectionPoint(IMediaSample* sample, SceneDetectorState& state,
		uint64_t& sceneEventId, SceneCorrectionAction& correctionAction);
	HRESULT CreateSyntheticRepeatSample(IMediaSample* source, REFERENCE_TIME start,
		REFERENCE_TIME stop, IMediaSample** repeatSample);

	// Remove all items from the videoFrameQueue
	// CALLER MUST HOLD m_rawQueueLock
	void PurgeQueue();
	
	// Purge converted sample queue
	// CALLER MUST HOLD m_convertedQueueLock
	void PurgeConvertedQueue();

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
