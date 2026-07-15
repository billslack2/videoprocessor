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
	void SetSceneAwareTimingCorrection(bool enabled) override;
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

	size_t m_frameQueueMaxSize = 8;

	//
	// QUEUE INFRASTRUCTURE (with dedicated locks)
	//
	
	// Raw frame queue (input from capture device)
	// Protected by: m_rawQueueLock
	std::deque<VideoFrame> m_videoFrameQueue;
	CCritSec m_rawQueueLock;  // Protects m_videoFrameQueue only
	
	// Pre-converted sample queue (output from conversion worker)
	// Protected by: m_convertedQueueLock
	struct ConvertedSample
	{
		IMediaSample* sample = nullptr;
		bool isSafeCorrectionPoint = false;
	};
	std::deque<ConvertedSample> m_convertedSampleQueue;
	CCritSec m_convertedQueueLock;  // Protects m_convertedSampleQueue only

	// This option is deliberately off by default.  When false, conversion does
	// no scene analysis and delivery follows the pre-existing path exactly.
	std::atomic_bool m_sceneAwareTimingCorrection = false;
	std::atomic<uint64_t> m_sceneDetectorGeneration = 0;
	DWORD m_lastSceneAwareCorrectionTime = 0;
	
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
		static constexpr size_t COLUMNS = 24;
		static constexpr size_t ROWS = 14;
		std::array<uint16_t, COLUMNS * ROWS> luma{};
		bool valid = false;
	};

	// Reads a sparse luma grid from P010 output.  It is intentionally called
	// only by the conversion worker and only while the feature is enabled.
	bool IsSafeSceneAwareCorrectionPoint(IMediaSample* sample, SceneSignature& previous);

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
