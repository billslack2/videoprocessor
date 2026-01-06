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
		
		// DirectShow-specific timing quality metrics
		double timingQuality;         // Overall timing quality (0.0-1.0)
		uint32_t discontinuityCount;  // Number of timing discontinuities detected
		double jitterReductionRatio;  // Effectiveness of jitter reduction (0.0-1.0)
	};
	
	TimingStatistics GetTimingStatistics() const;
	void ResetStatistics();
	
	// DirectShow IReferenceClock quality feedback (industry standard)
	double GetTimingQuality() const;
	bool IsHighQualityTiming() const;
	
	// DirectShow graph debugging support (professional tools integration)
	CString GetTimingDebugInfo() const;
	void LogTimingStatistics() const;
	
	// CRITICAL DIAGNOSTIC: Emergency timing debugging for massive deviations
	void LogCriticalTimingIssue(REFERENCE_TIME rawTime, REFERENCE_TIME processedTime, const char* reason) const;

private:
	ITimingClock& m_timingClock;
	const timingclocktime_t m_ticksPerSecond;
	REFERENCE_TIME m_lastReturnedTime = 0;
	
	// Enhanced professional timing features
	mutable CRITICAL_SECTION m_statisticsLock;
	mutable TimingStatistics m_statistics = {};
	
	// DirectShow-standard jitter buffer for timing stabilization
	static const int JITTER_BUFFER_SIZE = 16;  // Standard DirectShow jitter buffer depth
	mutable REFERENCE_TIME m_jitterBuffer[JITTER_BUFFER_SIZE] = {};
	mutable int m_jitterBufferIndex = 0;
	mutable int m_jitterBufferCount = 0;
	mutable REFERENCE_TIME m_smoothedTime = 0;
	
	// DirectShow timing quality metrics
	mutable REFERENCE_TIME m_lastQualityTime = 0;
	mutable double m_timingQuality = 1.0;  // 1.0 = perfect, 0.0 = terrible
	
	// DirectShow discontinuity detection (industry standard)
	mutable REFERENCE_TIME m_lastRawTime = 0;
	mutable bool m_discontinuityDetected = false;
	
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
	void UpdateStatisticsInternal(REFERENCE_TIME currentTime, REFERENCE_TIME rawTime, bool wasMonotonicCorrected) const;
	void UpdateFrameRateDetection(REFERENCE_TIME currentTime) const;
	void UpdateGenlockDetection() const;
	double CalculateCurrentFrameRate() const;
	
	// DirectShow-standard timing methods
	REFERENCE_TIME ApplyDirectShowJitterReduction(REFERENCE_TIME rawTime) const;
	REFERENCE_TIME ApplyLightJitterReduction(REFERENCE_TIME rawTime) const;  // CRITICAL FIX: New light jitter reduction
	void UpdateDirectShowTimingQuality(REFERENCE_TIME currentTime, REFERENCE_TIME rawTime) const;
	bool DetectTimingDiscontinuity(REFERENCE_TIME rawTime) const;
};
