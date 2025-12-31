/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "DirectShowTimingClock.h"
#include <cmath>
#include <algorithm>

DirectShowTimingClock::DirectShowTimingClock(ITimingClock& timingClock)
	: CBaseReferenceClock(DIRECTSHOW_TIMING_CLOCK_NAME, nullptr, nullptr, nullptr),
	  m_timingClock(timingClock),
	  m_ticksPerSecond(m_timingClock.TimingClockTicksPerSecond())
{
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock::DirectShowTimingClock() - Professional timing clock initializing")));
	assert(m_ticksPerSecond > 0);
	
	// Initialize critical section for thread-safe statistics
	InitializeCriticalSection(&m_statisticsLock);
	
	// Boost clock thread priority for minimal latency
	HANDLE clockThread = GetCurrentThread();
	if (!SetThreadPriority(clockThread, THREAD_PRIORITY_TIME_CRITICAL))
	{
		DbgLog((LOG_WARNING, 1, TEXT("DirectShowTimingClock: Failed to set TIME_CRITICAL priority (error %d)"), GetLastError()));
	}
	else
	{
		DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock: Enhanced professional timing clock ready")));
	}
}

DirectShowTimingClock::~DirectShowTimingClock()
{
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock::~DirectShowTimingClock()")));
	DeleteCriticalSection(&m_statisticsLock);
}

REFERENCE_TIME DirectShowTimingClock::GetPrivateTime()
{
	const timingclocktime_t now = m_timingClock.TimingClockNow();
	
	// HIGH-PRECISION CONVERSION with overflow protection
	REFERENCE_TIME rt;
	if (now > (INT64_MAX / 10000000LL))
	{
		// Overflow protection - less precise but prevents catastrophic failure
		rt = (now / m_ticksPerSecond) * 10000000LL;
	}
	else
	{
		// Normal path: High-precision conversion with banker's rounding
		rt = ((now * 10000000LL) + (m_ticksPerSecond / 2)) / m_ticksPerSecond;
	}
	
	// Store raw time for statistics
	const REFERENCE_TIME rawTime = rt;
	bool monotonicCorrected = false;
	
	// PROFESSIONAL MONOTONIC ENFORCEMENT
	if (rt <= m_lastReturnedTime)
	{
		// Calculate expected progression for drift analysis
		if (m_expectedClockProgression > 0)
		{
			const REFERENCE_TIME expectedTime = m_lastReturnedTime + m_expectedClockProgression;
			const REFERENCE_TIME drift = expectedTime - rt;
			
			// Use expected progression if reasonable, otherwise minimal increment
			if (drift < 100000)  // Less than 10ms drift
			{
				rt = expectedTime;
			}
			else
			{
				rt = m_lastReturnedTime + 1;
			}
		}
		else
		{
			rt = m_lastReturnedTime + 1;
		}
		monotonicCorrected = true;
	}
	
	// Update expected progression based on frame rate detection
	if (!monotonicCorrected && m_lastReturnedTime > 0)
	{
		const REFERENCE_TIME actualProgression = rt - m_lastReturnedTime;
		
		// Smooth progression estimate for stable expectations
		if (m_expectedClockProgression == 0)
		{
			m_expectedClockProgression = actualProgression;
		}
		else
		{
			// Exponential moving average (87.5% old + 12.5% new)
			m_expectedClockProgression = (m_expectedClockProgression * 7 + actualProgression) / 8;
		}
	}
	
	// Update professional statistics
	UpdateStatistics(rt, rawTime, monotonicCorrected);
	
	m_lastReturnedTime = rt;
	return rt;
}

void DirectShowTimingClock::UpdateStatistics(REFERENCE_TIME currentTime, REFERENCE_TIME rawTime, bool wasMonotonicCorrected) const
{
	EnterCriticalSection(&m_statisticsLock);
	
	m_statistics.totalQueries++;
	
	if (wasMonotonicCorrected)
	{
		m_statistics.clockResetCount++;
	}
	
	// Calculate clock drift
	if (m_statistics.totalQueries > 1)
	{
		const double driftUs = (double)(rawTime - currentTime) / 10.0;  // Convert to microseconds
		m_clockDriftAccumulator += driftUs;
		m_statistics.averageClockDrift = m_clockDriftAccumulator / m_statistics.totalQueries;
		
		// Track maximum jitter
		const double absoluteDrift = fabs(driftUs);
		if (absoluteDrift > m_statistics.maxClockJitter)
		{
			m_statistics.maxClockJitter = absoluteDrift;
		}
	}
	
	// Update frame rate detection every 16ms (~60fps)
	if ((currentTime - m_lastFrameRateUpdate) >= 166667)  // 16.67ms in 100ns units
	{
		UpdateFrameRateDetection(currentTime);
		UpdateGenlockDetection();
		m_lastFrameRateUpdate = currentTime;
	}
	
	m_statistics.lastUpdateTime = currentTime;
	
	LeaveCriticalSection(&m_statisticsLock);
}

void DirectShowTimingClock::UpdateFrameRateDetection(REFERENCE_TIME currentTime) const
{
	// Store frame timing in circular buffer
	m_frameRateHistory[m_frameRateHistoryIndex] = currentTime;
	m_frameRateHistoryIndex = (m_frameRateHistoryIndex + 1) % FRAME_RATE_HISTORY_SIZE;
	
	if (m_frameRateHistoryCount < FRAME_RATE_HISTORY_SIZE)
	{
		m_frameRateHistoryCount++;
	}
	
	// Calculate frame rate from recent history
	m_statistics.detectedFrameRate = CalculateCurrentFrameRate();
}

void DirectShowTimingClock::UpdateGenlockDetection() const
{
	// Genlock detection based on clock stability
	// If clock drift is very low and frame rate is stable, likely genlocked
	const bool lowDrift = (fabs(m_statistics.averageClockDrift) < 0.5);  // Less than 0.5us average drift
	const bool stableFrameRate = (m_statistics.detectedFrameRate > 0) && 
		                         (m_statistics.maxClockJitter < 50.0);     // Less than 50us jitter
	
	m_statistics.isGenlocked = lowDrift && stableFrameRate && (m_statistics.totalQueries > 1000);
}

double DirectShowTimingClock::CalculateCurrentFrameRate() const
{
	if (m_frameRateHistoryCount < 10)  // Need at least 10 samples
		return 0.0;
	
	// Calculate average frame interval from recent history
	const int samplesBack = (std::min)(60, m_frameRateHistoryCount - 1);  // Last 1 second at 60fps
	const int oldIndex = (m_frameRateHistoryIndex - samplesBack + FRAME_RATE_HISTORY_SIZE) % FRAME_RATE_HISTORY_SIZE;
	const int newIndex = (m_frameRateHistoryIndex - 1 + FRAME_RATE_HISTORY_SIZE) % FRAME_RATE_HISTORY_SIZE;
	
	const REFERENCE_TIME totalTime = m_frameRateHistory[newIndex] - m_frameRateHistory[oldIndex];
	
	if (totalTime <= 0)
		return 0.0;
	
	const double averageFrameInterval = (double)totalTime / samplesBack;  // In 100ns units
	const double frameRate = 10000000.0 / averageFrameInterval;  // Convert to Hz
	
	// Sanity check: reasonable video frame rates (5-300 Hz)
	return (frameRate >= 5.0 && frameRate <= 300.0) ? frameRate : 0.0;
}

DirectShowTimingClock::TimingStatistics DirectShowTimingClock::GetTimingStatistics() const
{
	EnterCriticalSection(&m_statisticsLock);
	TimingStatistics stats = m_statistics;
	LeaveCriticalSection(&m_statisticsLock);
	return stats;
}

void DirectShowTimingClock::ResetStatistics()
{
	EnterCriticalSection(&m_statisticsLock);
	
	m_statistics = {};
	m_clockDriftAccumulator = 0.0;
	m_expectedClockProgression = 0;
	m_stabilityCheckCount = 0;
	m_frameRateHistoryIndex = 0;
	m_frameRateHistoryCount = 0;
	m_lastFrameRateUpdate = 0;
	
	DbgLog((LOG_TRACE, 1, TEXT("DirectShowTimingClock: Statistics reset")));
	
	LeaveCriticalSection(&m_statisticsLock);
}
