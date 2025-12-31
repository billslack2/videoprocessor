/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <refclock.h>
#include <ITimingClock.h>

#define DIRECTSHOW_TIMING_CLOCK_NAME TEXT("TimingClock")

/**
 * Professional DirectShow timing clock with enhanced features for broadcast video.
 * 
 * Features:
 * - Genlock detection and frame-rate synchronization
 * - Adaptive clock stability monitoring
 * - Professional timing statistics
 * - Multi-threaded safety for high-frequency queries
 */
class DirectShowTimingClock : public CBaseReferenceClock
{
public:
	DirectShowTimingClock(ITimingClock& timingClock);
	virtual ~DirectShowTimingClock();

	// CBaseReferenceClock override
	REFERENCE_TIME GetPrivateTime() override;
	
	// Professional video timing extensions
	struct TimingStatistics {
		double averageClockDrift;     // Average drift in microseconds
		double maxClockJitter;        // Maximum jitter observed (us)
		uint32_t clockResetCount;     // Number of monotonic corrections
		uint32_t totalQueries;        // Total clock queries
		bool isGenlocked;             // Detected genlock status
		double detectedFrameRate;     // Measured frame rate (Hz)
		REFERENCE_TIME lastUpdateTime; // Last statistics update
	};
	
	TimingStatistics GetTimingStatistics() const;
	void ResetStatistics();

private:
	ITimingClock& m_timingClock;
	const timingclocktime_t m_ticksPerSecond;
	REFERENCE_TIME m_lastReturnedTime = 0;
	
	// Enhanced professional timing features
	mutable CRITICAL_SECTION m_statisticsLock;
	mutable TimingStatistics m_statistics = {};
	
	// Frame rate detection and genlock analysis
	static const int FRAME_RATE_HISTORY_SIZE = 120;  // 2 seconds at 60fps
	mutable REFERENCE_TIME m_frameRateHistory[FRAME_RATE_HISTORY_SIZE] = {};
	mutable int m_frameRateHistoryIndex = 0;
	mutable int m_frameRateHistoryCount = 0;
	mutable REFERENCE_TIME m_lastFrameRateUpdate = 0;
	
	// Clock stability monitoring
	mutable double m_clockDriftAccumulator = 0.0;
	mutable REFERENCE_TIME m_expectedClockProgression = 0;
	mutable uint32_t m_stabilityCheckCount = 0;
	
	void UpdateStatistics(REFERENCE_TIME currentTime, REFERENCE_TIME rawTime, bool wasMonotonicCorrected) const;
	void UpdateFrameRateDetection(REFERENCE_TIME currentTime) const;
	void UpdateGenlockDetection() const;
	double CalculateCurrentFrameRate() const;
};
