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

		// If queue is full, drop the oldest frame to make space
		if (m_videoFrameQueue.size() >= m_frameQueueMaxSize)
		{
			VideoFrame oldFrame = m_videoFrameQueue.front();
			oldFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}

		// Add the new frame to the queue
		videoFrame.SourceBufferAddRef();
		m_videoFrameQueue.push_back(videoFrame);
	}

	// Signal that a frame is available (outside lock to prevent contention)
	SetEvent(m_hFrameAvailableEvent);

	return S_OK;
}


void CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize(size_t frameQueueMaxSize)
{
	if (frameQueueMaxSize <= 0)
		throw std::runtime_error("Frame queue size must be > 0");

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize() - Changing from %zu to %zu"), 
		m_frameQueueMaxSize, frameQueueMaxSize));

	{
		CAutoLock lock(&m_filterCritSec);

		m_frameQueueMaxSize = frameQueueMaxSize;

		// Purge all frames
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}
		
		// Zero out timeline state for fresh start
		m_frameCounter = 0;
		m_previousFrameCounter = 0;
		m_frameCounterOffset = 0;
		m_previousTimeStop = 0;
		m_startTimeOffset = 0;
		
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize() - Queue purged, timeline zeroed")));
	}
	
	SetEvent(m_hFrameAvailableEvent);
	
	// Deliver new segment if active
	if (IsConnected() && m_isActive)
	{
		if (FAILED(DeliverNewSegment(0, MAXLONGLONG, 1.0)))
		{
			DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize() - Failed to deliver new segment")));
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
	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Reset() - Purging queue and resetting timeline")));
	
	{
		// Hold lock while purging queue and resetting state
		CAutoLock lock(&m_filterCritSec);
		
		// Purge all frames
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}
		
		// Zero out timeline state - fresh start
		m_frameCounter = 0;
		m_previousFrameCounter = 0;
		m_frameCounterOffset = 0;
		m_previousTimeStop = 0;
		m_startTimeOffset = 0;
		
		DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Reset() - Queue purged, timeline zeroed")));
	}
	
	// Call base Reset() for DirectShow signaling
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
		{
			CAutoLock lock(&m_filterCritSec);

			if (!m_isActive)
				break;

			// Safety check: if queue is empty, keep waiting
			if (m_videoFrameQueue.empty())
				continue;

			// Get and remove the front frame (oldest) - ATOMIC within lock
			videoFrame = m_videoFrameQueue.front();
			m_videoFrameQueue.pop_front();

			// Update next frame timestamp for CLOCK_SMART/CLOCK_CLOCK modes
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
		}  // ← UNLOCK before expensive operations

		// Get buffer for sample (outside lock - can block)
		IMediaSample* pSample = nullptr;
		HRESULT hr = this->GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
		if (FAILED(hr))
		{
			DbgLog((LOG_TRACE, 1, TEXT("::ThreadProc(#%I64u): GetDeliveryBuffer() failed (HRESULT=0x%08x)"),
				videoFrame.GetCounter(), hr));
			videoFrame.SourceBufferRelease();
			++m_droppedFrameCount;
			continue;
		}

		// Render frame into sample
		hr = RenderVideoFrameIntoSample(videoFrame, pSample);
		if (FAILED(hr) || hr == S_FRAME_NOT_RENDERED)
		{
			if (FAILED(hr))
			{
				DbgLog((LOG_TRACE, 1,
					TEXT("::ThreadProc(#%I64u): RenderVideoFrameIntoSample() failed (HRESULT=0x%08x)"),
					videoFrame.GetCounter(), hr));
			}
			videoFrame.SourceBufferRelease();
			pSample->Release();
			++m_droppedFrameCount;
			continue;
		}

		// Deliver frame to renderer
		hr = this->Deliver(pSample);
		if (FAILED(hr))
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("::ThreadProc(#%I64u): Deliver() failed (HRESULT=0x%08x)"),
				videoFrame.GetCounter(), hr));
			videoFrame.SourceBufferRelease();
			pSample->Release();
			++m_droppedFrameCount;
			continue;
		}

		// Success - clean up and continue to next frame
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
