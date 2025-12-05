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
		if (FAILED(hr))
			return hr;

		assert(!ThreadExists());

		// CRITICAL: Reset the shutdown event before starting the thread!
		// It may still be signaled from a previous Inactive() call
		ResetEvent(m_hShutdownEvent);

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
	{
		CAutoLock lock(&m_filterCritSec);

		// Reject frames if not processing
		if (!m_isActive)
			return S_OK;

		// If this frame's timestamp is lower or equal to the one before it,
		// erase that earlier one
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame lastFrame = m_videoFrameQueue.back();

			// Previous one was younger, nothing to do
			if (videoFrame.GetTimingTimestamp() > lastFrame.GetTimingTimestamp())
				break;

			// Previous one was older or equal, erase
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

	// Signal event EVERY time a frame is added (auto-reset event handles rest)
	if (m_hFrameAvailableEvent)
	{
		SetEvent(m_hFrameAvailableEvent);
	}

	return S_OK;
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

	while (true)
	{
		// **CLOCK_SMART COMPATIBILITY**: Keep 100ms timeout for modes that need to peek at next frame
		// CLOCK_RATIONAL doesn't have this limitation and could use INFINITE, but we use 100ms for all modes for simplicity
		DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, 100);
		
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

		{
			CAutoLock lock(&m_filterCritSec);

			// Double-check active state (important for timeout case)
			if (!m_isActive)
				break;

			// For most timing empty is really empty, however for the clock-to-clock
			// we need to keep one frame in.
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

			// Get the front frame (oldest)
			videoFrame = m_videoFrameQueue.front();
			m_videoFrameQueue.pop_front();

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
					// This can happen during shutdown when the graph is being torn down
					if (m_timingClock)
					{
						const timingclocktime_t ticksPerSecond = m_timingClock->TimingClockTicksPerSecond();
						m_nextVideoFrameStartTime = (REFERENCE_TIME)((nextFrameTicks * 10000000LL) / ticksPerSecond);
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

		// Get buffer for sample
		// Note you can fill in start and stop time, but following the code shows that they are unused.
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
