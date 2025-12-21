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
	// Create auto-reset event for frame availability signaling
	// Auto-reset: automatically resets to non-signaled after a waiting thread is released
	m_hFrameAvailableEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!m_hFrameAvailableEvent)
		throw std::runtime_error("Failed to create frame available event");
}


CBufferedLiveSourceVideoOutputPin::~CBufferedLiveSourceVideoOutputPin()
{
	PurgeQueue();
	
	if (m_hFrameAvailableEvent)
	{
		CloseHandle(m_hFrameAvailableEvent);
		m_hFrameAvailableEvent = nullptr;
	}
}


HRESULT CBufferedLiveSourceVideoOutputPin::Active()
{
	if (m_frameQueueMaxSize == 0)
		throw std::runtime_error("Call SetFrameQueueMaxSize() before activating the graph");

	{
		CAutoLock lock(m_pLock);

		if (m_pFilter->IsActive())
			return S_FALSE;	// succeeded, but did not allocate resources (they already exist...)

		assert(IsConnected());
		assert(!m_isActive);

		HRESULT hr = ALiveSourceVideoOutputPin::Active();
		if (FAILED(hr))
			return hr;

		assert(!ThreadExists());

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = true;
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

		if (ThreadExists())
		{
			Close();
		}
	}

	return S_OK;
}


HRESULT CBufferedLiveSourceVideoOutputPin::OnVideoFrame(VideoFrame& videoFrame)
{
	{
		CAutoLock lock(&m_filterCritSec);

		// Reject frames if not processing
		if (!m_isActive)
			return S_OK;

		// More tolerant timestamp handling for long-term stability
		// Allow small backwards timestamps (within 1/2 frame duration) to handle PLL jitter
		const timingclocktime_t currentTimestamp = videoFrame.GetTimingTimestamp();
		const timingclocktime_t toleranceThreshold = m_timingClock ? 
			(m_timingClock->TimingClockTicksPerSecond() / 120) : 8333;  // ~8ms tolerance at 1MHz, fallback to 8ms

		// Only drop frames with significantly older timestamps (more than tolerance)
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame lastFrame = m_videoFrameQueue.back();
			const timingclocktime_t lastTimestamp = lastFrame.GetTimingTimestamp();

			// If new frame is significantly newer, keep processing
			if (currentTimestamp > (lastTimestamp + toleranceThreshold))
				break;

			// If new frame is significantly older, drop it and keep the newer one
			if ((currentTimestamp + toleranceThreshold) < lastTimestamp)
			{
				// Drop the incoming frame (it's too old)
				DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Dropping old frame - current: %lld, last: %lld, diff: %lld"),
					currentTimestamp, lastTimestamp, lastTimestamp - currentTimestamp));
				return S_OK;
			}

			// Timestamps are very close - drop the older one in queue and accept new one
			lastFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_back();
			++m_droppedFrameCount;
		}

		// If full throw away oldest to make space
		if (m_videoFrameQueue.size() >= m_frameQueueMaxSize)
		{
			m_videoFrameQueue.front().SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}

		// Prevent from getting cleaned up and add to queue
		videoFrame.SourceBufferAddRef();
		m_videoFrameQueue.push_back(videoFrame);
	}

	// Signal that a frame is available (outside lock to prevent contention)
	// Event-driven: wakes up ThreadProc instead of polling with Sleep(1)
	SetEvent(m_hFrameAvailableEvent);

	return S_OK;
}


void CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize(size_t frameQueueMaxSize)
{
	if (frameQueueMaxSize <= 0)
		throw std::runtime_error("Frame queue size must be > 0");

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize() - Changing queue size from %zu to %zu"), 
		m_frameQueueMaxSize, frameQueueMaxSize));

	{
		CAutoLock lock(&m_filterCritSec);

		m_frameQueueMaxSize = frameQueueMaxSize;

		// CRITICAL: Queue size change indicates renderer state change (like MadVR quality settings)
		// We need to reset timeline state and signal MadVR properly
		
		// Purge all frames from queue
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}
		
		// Reset timeline state to force clean restart
		// This ensures RATIONAL_RATIONAL gets clean mathematical progression after MadVR changes
		m_frameCounter = 0;
		m_previousFrameCounter = 0;
		m_startTimeOffset = 0;
		m_frameCounterOffset = 0;
		m_previousTimeStop = 0;
		
		// CRITICAL FOR RATIONAL_RATIONAL: Signal both discontinuity AND new segment
		// THEO_THEO works with just discontinuity, but RATIONAL_RATIONAL needs the official segment restart
		m_forceDiscontinuity = true;
		m_deliverNewSegment = true;  // This is what the Reset button does that we were missing!
		
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Queue purged, timeline reset, will deliver NEW SEGMENT on next frame (RATIONAL_RATIONAL fix)")));
	}
	
	// CRITICAL: Wake the consumer thread after purge to ensure it's ready for new frames
	// Without this, if the event was already reset, new frames arriving won't wake the thread
	SetEvent(m_hFrameAvailableEvent);
	
	// CRITICAL FIX: If we're connected and active, we need to properly initialize the DirectShow timeline
	// This fixes the issue when starting directly in fullscreen mode where SetFrameQueueMaxSize is called
	// during initialization but the timeline never gets properly established with DirectShow
	if (IsConnected() && m_isActive)
	{
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Connected and active during queue resize - delivering timeline initialization")));
		
		// Deliver new segment to establish timing baseline with DirectShow reference clock
		// This is what was missing when starting directly in fullscreen mode
		if (FAILED(DeliverNewSegment(0, MAXLONGLONG, 1.0)))
		{
			DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Failed to deliver new segment during queue resize!")));
		}
		else
		{
			DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: New segment delivered successfully - DirectShow timeline properly initialized")));
			
			// Clear the flag since we just delivered it
			m_deliverNewSegment = false;
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

	// Set thread priority to above normal for time-critical frame delivery
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL))
	{
		DbgLog((LOG_WARNING, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Failed to set thread priority to above normal")));
	}
	else
	{
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Thread priority set to above normal")));
	}

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin worker thread starting (event-driven)")));

	// Track consecutive delivery failures to prevent burst retries
	int consecutiveFailures = 0;
	const int maxConsecutiveFailures = 3;

	while (true)
	{
		DWORD waitResult = WaitForSingleObject(m_hFrameAvailableEvent, 1);
		
		if (waitResult == WAIT_TIMEOUT)
		{
			if (!m_isActive)
				break;
			continue;  // Nothing signaled, keep waiting
		}
		else if (waitResult != WAIT_OBJECT_0)
		{
			DbgLog((LOG_ERROR, 1, TEXT("::ThreadProc: WaitForSingleObject failed with %d"), waitResult));
			break;
		}

		VideoFrame videoFrame;
		size_t currentQueueSize = 0;

		{
			CAutoLock lock(&m_filterCritSec);

			if (!m_isActive)
				break;

			// Safety check: if queue is empty after resolution change, keep waiting
			if (m_videoFrameQueue.empty())
				continue;

			currentQueueSize = m_videoFrameQueue.size();

			// IMPROVED BURST HANDLING: More gradual queue reduction
			if (currentQueueSize > 5 && consecutiveFailures < maxConsecutiveFailures)  // Raised from 3 to 5
			{
				// Drop only 1-2 old frames, not everything down to 2
				while (m_videoFrameQueue.size() > 4)  // Keep 4 frames instead of 2
				{
					VideoFrame oldFrame = m_videoFrameQueue.front();
					oldFrame.SourceBufferRelease();
					m_videoFrameQueue.pop_front();
					++m_droppedFrameCount;
				}
			}

			// Get the front frame (oldest remaining)
			videoFrame = m_videoFrameQueue.front();
			m_videoFrameQueue.pop_front();

			// Get the current front's start time
			switch (m_timestamp)
			{
			case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
				assert(!m_videoFrameQueue.empty());
				// break;  not here intentionally

			case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:

				if (!m_videoFrameQueue.empty())
				{
					m_nextVideoFrameStartTime =
						(REFERENCE_TIME)(
						m_videoFrameQueue.front().GetTimingTimestamp() *
						(10000000.0 / m_timingClock->TimingClockTicksPerSecond()));
				}
				else
				{
					m_nextVideoFrameStartTime = REFERENCE_TIME_INVALID;
				}
				break;
			}
		}

		// ADAPTIVE TIMEOUT: Use shorter timeout if queue is backing up
		//DWORD bufferTimeout = (currentQueueSize > 2) ? 2 : 5;  // 1ms when under pressure //TODO: Updated

		// Get buffer for sample (flags parameter, not timeout)
		IMediaSample* pSample = nullptr;
		HRESULT hr = this->GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
		if (FAILED(hr))
		{
			videoFrame.SourceBufferRelease();
			++m_droppedFrameCount;
			consecutiveFailures++;
			
			// If we're consistently failing, back off slightly to prevent tight retry loop
			if (consecutiveFailures >= maxConsecutiveFailures)
			{
				Sleep(1);  // 1ms backoff
			}
			continue;
		}

		// Convert
		hr = RenderVideoFrameIntoSample(videoFrame, pSample);
		if (FAILED(hr))
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("::ThreadProc(#%I64u): RenderVideoFrameIntoSample() failed (HRESULT=0x%08x)"),
				videoFrame.GetCounter(), hr));
			
			videoFrame.SourceBufferRelease();
			pSample->Release();
			++m_droppedFrameCount;
			consecutiveFailures++;
			continue;
		}
		if (hr == S_FRAME_NOT_RENDERED)
		{
			videoFrame.SourceBufferRelease();
			pSample->Release();
			consecutiveFailures++;
			continue;
		}

		// Deliver frame to renderer - this is the main blocking point
		hr = this->Deliver(pSample);
		if (FAILED(hr))
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("::ThreadProc(#%I64u): Deliver() failed (HRESULT=0x%08x) - dropping frame"),
				videoFrame.GetCounter(), hr));

			videoFrame.SourceBufferRelease();
			pSample->Release();
			++m_droppedFrameCount;
			consecutiveFailures++;
			continue;
		}

		// Success - reset failure counter
		consecutiveFailures = 0;

		// Success - clean up and continue
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
