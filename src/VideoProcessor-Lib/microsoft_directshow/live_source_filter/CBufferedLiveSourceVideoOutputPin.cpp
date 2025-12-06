/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "CBufferedLiveSourceVideoOutputPin.h"


CBufferedLiveSourceVideoOutputPin::CBufferedLiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr):
	ALiveSourceVideoOutputPin(filter, pLock, phr)
{
	// Create auto-reset event for efficient thread signaling
	m_hFrameAvailableEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!m_hFrameAvailableEvent)
	{
		DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Failed to create frame event, error: %d"), GetLastError()));
		if (phr)
			*phr = E_OUTOFMEMORY;
		return;
	}

	// Create manual-reset event for clean shutdown signaling
	m_hShutdownEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (!m_hShutdownEvent)
	{
		DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Failed to create shutdown event, error: %d"), GetLastError()));
		if (phr)
			*phr = E_OUTOFMEMORY;
		return;
	}
}


CBufferedLiveSourceVideoOutputPin::~CBufferedLiveSourceVideoOutputPin()
{
	if (m_hShutdownEvent)
	{
		CloseHandle(m_hShutdownEvent);
		m_hShutdownEvent = nullptr;
	}

	if (m_hFrameAvailableEvent)
	{
		CloseHandle(m_hFrameAvailableEvent);
		m_hFrameAvailableEvent = nullptr;
	}
	
	PurgeQueue();
}


HRESULT CBufferedLiveSourceVideoOutputPin::Active()
{
	if (m_frameQueueMaxSize == 0)
		throw std::runtime_error("Call SetFrameQueueMaxSize() before activating the graph");

	// CRITICAL: Verify events were created successfully in constructor
	if (!m_hFrameAvailableEvent || !m_hShutdownEvent)
	{
		DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Active(): Event handles not initialized")));
		return E_FAIL;
	}

	{
		CAutoLock lock(m_pLock);

		if (m_pFilter->IsActive())
			return S_FALSE;	// succeeded, but did not allocate resources (they already exist...)

		assert(IsConnected());
		assert(!m_isActive);

		HRESULT hr = ALiveSourceVideoOutputPin::Active();
		//if (FAILED(hr))
		//	return hr;

		assert(!ThreadExists());

		// CRITICAL: Reset the shutdown event before starting the thread!
		// It may still be signaled from a previous Inactive() call
		ResetEvent(m_hShutdownEvent);

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = true;
			
			// **RESET MONITORING**: Clear stats when starting fresh
			m_queueHighWaterMark = 0;
			m_queueFullBlockCount = 0;
			m_frameDeliveryCount = 0;
		}

		// start the thread
		if (!Create())
			return E_FAIL;

		return S_OK;
	}
}


HRESULT CBufferedLiveSourceVideoOutputPin::Inactive()
{
	{
		CAutoLock lock(m_pLock);

		// do nothing if not connected - its ok not to connect to all pins of a source filter
		if (!IsConnected())
			return NOERROR;

		HRESULT hr = ALiveSourceVideoOutputPin::Inactive();
		if (FAILED(hr))
			return hr;

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = false;

			PurgeQueue();
		}

		// Signal shutdown event to wake the thread immediately
		if (m_hShutdownEvent)
		{
			SetEvent(m_hShutdownEvent);
		}

		if (ThreadExists())
		{
			Close();
		}
	}

	return S_OK;
}


HRESULT CBufferedLiveSourceVideoOutputPin::OnVideoFrame(VideoFrame& videoFrame)
{
	// **HYBRID ASYNC/BLOCKING APPROACH WITH TIMESTAMP VALIDATION**
	// - Normally: Async delivery via queue (low latency)
	// - When queue full: Block capture thread (backpressure prevents drops)
	// - Timestamp validation: Detect and handle timing issues
	//
	// This gives best of both worlds:
	// 1. Low latency when renderer keeps up
	// 2. No dropped frames when renderer temporarily falls behind
	// 3. Self-regulating backpressure
	// 4. Robust timestamp handling

	// **TIMESTAMP VALIDATION**: Check for common timing issues
	const timingclocktime_t currentTimestamp = videoFrame.GetTimingTimestamp();
	
	// **CRITICAL**: Wait OUTSIDE the lock to avoid deadlock and race conditions
	// Check queue fullness, release lock, wait, then retry
	while (true)
	{
		bool needToWait = false;
		
		{
			CAutoLock lock(&m_filterCritSec);

			// Reject frames if not processing
			if (!m_isActive)
				return S_OK;

			// **DIAGNOSTIC**: Validate timestamp ordering
			// This helps detect timing issues from the capture device
			if (!m_videoFrameQueue.empty())
			{
				const VideoFrame& lastFrame = m_videoFrameQueue.back();
				const timingclocktime_t lastTimestamp = lastFrame.GetTimingTimestamp();
				
				// Check for timestamp going backwards (serious error)
				if (currentTimestamp <= lastTimestamp)
				{
					DbgLog((LOG_ERROR, 1, 
						TEXT("CBufferedLiveSourceVideoOutputPin::OnVideoFrame(): WARNING - Frame timestamp not advancing! Current=%I64d, Last=%I64d, Delta=%I64d"),
						currentTimestamp, lastTimestamp, (currentTimestamp - lastTimestamp)));
				}
				
				// Check for unusually large gaps (possible frame counter skip)
				if (m_timingClock)
				{
					const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();
					const timingclocktime_t delta = currentTimestamp - lastTimestamp;
					
					// More than 100ms gap is suspicious for video frames
					const timingclocktime_t maxGapTicks = (ticksPerSecond / 10); // 100ms
					if (delta > maxGapTicks)
					{
						DbgLog((LOG_TRACE, 1,
							TEXT("CBufferedLiveSourceVideoOutputPin::OnVideoFrame(): Large timestamp gap: %I64d ticks (%.1f ms)"),
							delta, (delta * 1000.0) / ticksPerSecond));
					}
				}
			}

			// Check if queue has space
			needToWait = (m_videoFrameQueue.size() >= m_frameQueueMaxSize);
			
			if (!needToWait)
			{
				// We have space - handle out-of-order frames
				// If this frame's timestamp is lower or equal to the one before it,
				// erase that earlier one
				while (!m_videoFrameQueue.empty())
				{
					VideoFrame lastFrame = m_videoFrameQueue.back();

					// Previous one was younger, nothing to do
					if (currentTimestamp > lastFrame.GetTimingTimestamp())
						break;

					// Previous one was older or equal, erase
					DbgLog((LOG_TRACE, 1,
						TEXT("CBufferedLiveSourceVideoOutputPin::OnVideoFrame(): Dropping out-of-order frame, Current=%I64d, Dropped=%I64d"),
						currentTimestamp, lastFrame.GetTimingTimestamp()));
					
					lastFrame.SourceBufferRelease();
					m_videoFrameQueue.pop_back();
					++m_droppedFrameCount;
				}

				// Add the frame (we know there's space)
				videoFrame.SourceBufferAddRef();
				m_videoFrameQueue.push_back(videoFrame);
				
				// **ADAPTIVE MONITORING**: Track queue depth statistics
				const size_t currentQueueSize = m_videoFrameQueue.size();
				if (currentQueueSize > m_queueHighWaterMark)
				{
					m_queueHighWaterMark = currentQueueSize;
					
					// Warn if queue is consistently filling up (>75% capacity)
					const size_t warningThreshold = (m_frameQueueMaxSize * 3) / 4;
					if (currentQueueSize >= warningThreshold)
					{
						DbgLog((LOG_TRACE, 1,
							TEXT("CBufferedLiveSourceVideoOutputPin::OnVideoFrame(): Queue high water mark: %zu/%zu (%.1f%% full) - consider increasing queue size"),
							currentQueueSize, m_frameQueueMaxSize, (currentQueueSize * 100.0) / m_frameQueueMaxSize));
					}
				}
				
				// Signal event EVERY time a frame is added (auto-reset event handles rest)
				if (m_hFrameAvailableEvent)
				{
					SetEvent(m_hFrameAvailableEvent);
				}
				
				return S_OK;
			}
			else
			{
				// **DIAGNOSTIC**: Log when we need to block (indicates potential bottleneck)
				++m_queueFullBlockCount;
				
				DbgLog((LOG_TRACE, 2,
					TEXT("CBufferedLiveSourceVideoOutputPin::OnVideoFrame(): Queue full (%zu/%zu), blocking capture thread (block count: %zu)"),
					m_videoFrameQueue.size(), m_frameQueueMaxSize, m_queueFullBlockCount));
			}
		}
		// Lock is released here before we block

		// **BLOCKING**: Queue is full - wait for worker thread to drain it
		// Use very short sleep (1ms) to minimize latency impact
		// CRITICAL: We're outside the lock here, so worker thread can process frames
		Sleep(1);  // 1ms = one DirectShow reference time unit
		
		// Loop back to recheck - with fresh lock acquisition
	}
}


void CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize(size_t frameQueueMaxSize)
{
	if (frameQueueMaxSize <= 0)
		throw std::runtime_error("Frame queue size must be > 0");

	{
		CAutoLock lock(&m_filterCritSec);

		m_frameQueueMaxSize = frameQueueMaxSize;

		// If full throw away oldest to make space if needed
		while (m_videoFrameQueue.size() >= m_frameQueueMaxSize)
		{
			m_videoFrameQueue.front().SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}
	}
}


size_t CBufferedLiveSourceVideoOutputPin::GetFrameQueueSize()
{
	{
		CAutoLock lock(&m_filterCritSec);

		return m_videoFrameQueue.size();
	}
}


void CBufferedLiveSourceVideoOutputPin::Reset()
{
	PurgeQueue();
	ALiveSourceVideoOutputPin::Reset();
}


DWORD CBufferedLiveSourceVideoOutputPin::ThreadProc()
{
	// ! WARNING: Runs in inner thread

	// Elevate thread priority to TIME_CRITICAL for low-latency video delivery
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
	{
		DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Failed to set thread priority, error: %d"), GetLastError()));
	}

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin worker thread starting at TIME_CRITICAL priority")));

	// CRITICAL: Check if events exist before using them
	if (!m_hFrameAvailableEvent || !m_hShutdownEvent)
	{
		DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Events not initialized, thread exiting")));
		return -100;
	}

	// Prepare array of events to wait on
	HANDLE waitHandles[2] = { m_hFrameAvailableEvent, m_hShutdownEvent };

	// **FRAME PACING**: Track delivery timing for smooth renderer feed
	LARGE_INTEGER qpcFreq, lastDeliveryTime;
	QueryPerformanceFrequency(&qpcFreq);
	QueryPerformanceCounter(&lastDeliveryTime);
	
	while (true)
	{
		// **REFRESH-RATE AWARE WAIT**: Use actual frame duration for timeout
		// m_frameDuration is in 100ns units, convert to milliseconds
		// Use half frame duration as timeout - responsive but not wasteful
		DWORD waitTimeoutMs = 16;  // Default fallback for 60Hz
		if (m_frameDuration > 0)
		{
			// Convert 100ns units to ms, use half frame duration for responsiveness
			waitTimeoutMs = (DWORD)(m_frameDuration / 20000);  // /10000 for ms, /2 for half
			if (waitTimeoutMs < 4) waitTimeoutMs = 4;    // Min 4ms to avoid busy-waiting
			if (waitTimeoutMs > 50) waitTimeoutMs = 50;  // Max 50ms for 24Hz content
		}
		
		DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, waitTimeoutMs);
		
		// Check if shutdown was signaled
		if (waitResult == WAIT_OBJECT_0 + 1)
		{
			DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Shutdown event signaled")));
			break;
		}

		if (waitResult == WAIT_FAILED)
		{
			DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: WaitForMultipleObjects failed, error: %d"), GetLastError()));
			break;
		}

		VideoFrame videoFrame;
		bool hasFrame = false;
		timingclocktime_t frameTimestamp = 0;
		REFERENCE_TIME frameRefTime = 0;

		{
			CAutoLock lock(&m_filterCritSec);

			// Double-check active state (important for timeout case)
			if (!m_isActive)
				break;

			// For most timing modes empty is really empty, however for CLOCK_CLOCK
			// we need to keep one frame in for timestamp calculation
			if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK)
			{
				hasFrame = (m_videoFrameQueue.size() > 1);
			}
			else
			{
				hasFrame = !m_videoFrameQueue.empty();
			}
			
			if (!hasFrame)
				continue;  // Spurious wake-up or timeout, retry

			// Peek at the front frame WITHOUT removing it yet
			// We need to check if it's time to deliver
			videoFrame = m_videoFrameQueue.front();
			frameTimestamp = videoFrame.GetTimingTimestamp();
			
			// Convert frame timestamp to REFERENCE_TIME (100ns units)
			// SAFETY: Only do this if we have a valid timing clock
			if (m_timingClock)
			{
				const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();
				if (ticksPerSecond > 0)
				{
					frameRefTime = (REFERENCE_TIME)((frameTimestamp * 10000000LL) / ticksPerSecond);
				}
			}
		}

		// **TIMESTAMP-GATED DELIVERY - DISABLED**
		// After testing, the gating logic was found to cause more issues than it solved.
		// The frame offset (e.g., 90ms) already handles the timing - frames are timestamped
		// in the future so MadVR knows when to present them. We should deliver frames
		// as soon as possible and let MadVR handle the presentation timing.
		//
		// The original problem (frame repeats) was about timestamp generation, not
		// delivery timing. CLOCK_RATIONAL's drift correction handles that.
		//
		// Keeping this code commented for reference in case gating is needed in the future.
		/*
		if (m_timingClock && m_frameDuration > 0 &&
		    (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL ||
		     m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART ||
		     m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO))
		{
			// Gating logic disabled - deliver frames immediately
		}
		*/

		// Now actually remove the frame from the queue
		{
			CAutoLock lock(&m_filterCritSec);
			
			if (!m_isActive)
				break;
				
			// Verify frame is still there (another thread might have purged)
			if (m_videoFrameQueue.empty())
				continue;
				
			// Remove the frame we peeked at
			m_videoFrameQueue.pop_front();

			// **DIAGNOSTIC**: Log queue depth to detect buffering issues
			if (m_videoFrameQueue.size() >= (m_frameQueueMaxSize - 1))
			{
				DbgLog((LOG_TRACE, 2,
					TEXT("CBufferedLiveSourceVideoOutputPin::ThreadProc(): Queue nearly full (%zu/%zu) - renderer may be slow"),
					m_videoFrameQueue.size() + 1, m_frameQueueMaxSize));
			}

			// Get the current front's start time for CLOCK_SMART and CLOCK_CLOCK modes
			switch (m_timestamp)
			{
			case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
				assert(!m_videoFrameQueue.empty());
				// break;  not here intentionally

			case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:

				if (!m_videoFrameQueue.empty())
				{
					// Use integer arithmetic to avoid floating point rounding errors
					const timingclocktime_t nextFrameTicks = m_videoFrameQueue.front().GetTimingTimestamp();
					
					// CRITICAL: Check if timing clock is valid before using it
					if (m_timingClock)
					{
						const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();
						if (ticksPerSecond > 0)
						{
							m_nextVideoFrameStartTime = (REFERENCE_TIME)((nextFrameTicks * 10000000LL) / ticksPerSecond);
						}
						else
						{
							m_nextVideoFrameStartTime = REFERENCE_TIME_INVALID;
						}
					}
					else
					{
						DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Timing clock is null, using INVALID for next frame")));
						m_nextVideoFrameStartTime = REFERENCE_TIME_INVALID;
					}
				}
				else
				{
					m_nextVideoFrameStartTime = REFERENCE_TIME_INVALID;
				}
				break;
			}
		}

		// **FRAME PACING**: Measure time between deliveries to detect stuttering
		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);
		const double deliveryIntervalMs = ((currentTime.QuadPart - lastDeliveryTime.QuadPart) * 1000.0) / qpcFreq.QuadPart;
		lastDeliveryTime = currentTime;
		
		// Log unusually long gaps (more than 2x expected frame time)
		// SAFETY: Only check if we have a valid frame duration
		if (m_frameDuration > 0)
		{
			const double expectedFrameMs = m_frameDuration / 10000.0;
			if (deliveryIntervalMs > (expectedFrameMs * 2.0))
			{
				DbgLog((LOG_TRACE, 2,
					TEXT("CBufferedLiveSourceVideoOutputPin::ThreadProc(): Long delivery gap: %.1f ms (expected ~%.1f ms)"),
					deliveryIntervalMs, expectedFrameMs));
			}
		}

		// Get buffer for sample
		IMediaSample* pSample = nullptr;
		HRESULT hr = this->GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
		if (FAILED(hr))
		{
			videoFrame.SourceBufferRelease();
			DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: GetDeliveryBuffer failed, error: %d"), hr));
			return -1;
		}

		// Convert
		hr = RenderVideoFrameIntoSample(videoFrame, pSample);
		if (FAILED(hr))
		{
			videoFrame.SourceBufferRelease();
			pSample->Release();
			DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: RenderVideoFrameIntoSample failed, error: %d"), hr));
			return -2;
		}
		if (hr == S_FRAME_NOT_RENDERED)
		{
			videoFrame.SourceBufferRelease();
			pSample->Release();
			continue;
		}

		// Deliver frame to renderer
		hr = this->Deliver(pSample);
		if (FAILED(hr))
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("::FillBuffer(#%I64u): Failed to deliver sample, error: %i"),
				videoFrame.GetCounter(), hr));

			videoFrame.SourceBufferRelease();
			pSample->Release();
			return -3;
		}

		// **SUCCESS TRACKING**: Count successful deliveries
		++m_frameDeliveryCount;

		videoFrame.SourceBufferRelease();
		pSample->Release();
	}

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin worker thread exiting")));

	return 0;
}


void CBufferedLiveSourceVideoOutputPin::PurgeQueue()
{
	{
		CAutoLock lock(&m_filterCritSec);

		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}
	}
}
