/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <DebugLog.h>
#include <IntegerMath.h>

#include "AutoPpmCalibrator.h"

AutoPpmCalibrator::AutoPpmCalibrator()
{
    Reset();
}

void AutoPpmCalibrator::Initialize(
    uint64_t frameDurationTicks,
    uint64_t timeScale,
    uint64_t hwTicksPerSec)
{
    if (timeScale == 0 || hwTicksPerSec == 0)
    {
        DbgLog((LOG_ERROR, 1, TEXT("AutoPpmCalibrator::Initialize: Invalid parameters (zero timeScale or hwTicksPerSec)")));
        return;
    }

    m_frameDurationTicks = frameDurationTicks;
    m_timeScale = timeScale;
    m_hwTicksPerSec = hwTicksPerSec;
    m_isInitialized = true;
    m_trimTotalPpm = 0;  // Start with no correction

    // Calculate expected frame rate and frame period for verification
    const double frameRate = (double)timeScale / frameDurationTicks;
    const double framePeriodMs = 1000.0 / frameRate;
    const double hwFramePeriodTicks = (double)hwTicksPerSec / frameRate;

    DEBUGLOG("AutoPpmCalibrator: Initialized for %llu/%llu ticks/sec (%.6f Hz, %.3f ms/frame)",frameDurationTicks, timeScale, frameRate, framePeriodMs);
    DEBUGLOG("AutoPpmCalibrator: Hardware clock %llu Hz (%.3f ticks/frame)",hwTicksPerSec, hwFramePeriodTicks);
    DEBUGLOG("AutoPpmCalibrator: Expected measurement: 600 frames = %.3f seconds = %llu hardware ticks", 600.0 / frameRate, (uint64_t)(hwFramePeriodTicks * 600));
    DEBUGLOG("AutoPpmCalibrator: IMPORTANT: Measuring hardware timestamp PROGRESSION rate, not clock accuracy");
    DEBUGLOG("AutoPpmCalibrator: Positive drift = timestamps advancing FASTER than theoretical frame rate");
    DEBUGLOG("AutoPpmCalibrator: Negative drift = timestamps advancing SLOWER than theoretical frame rate");
}

void AutoPpmCalibrator::OnFrame(uint64_t frameCounter, uint64_t hwTimestamp)
{
    if (!m_isInitialized)
        return;

    // Initialize measurement window on first frame
    if (m_measurementWindowStartFrame == 0)
    {
        ResetMeasurementWindow(frameCounter, hwTimestamp);
        return;
    }

    // Update end of measurement window
    m_measurementWindowEndFrame = frameCounter;
    m_measurementWindowEndTimestamp = hwTimestamp;

    // Check if we've reached measurement interval
    const uint64_t framesInWindow = m_measurementWindowEndFrame - m_measurementWindowStartFrame;
    
    if (framesInWindow >= MEASUREMENT_INTERVAL_FRAMES)
    {
        // Analyze the measurement window and potentially apply correction
        AnalyzeMeasurementWindow();
        
        // Reset window to start a new measurement interval
        ResetMeasurementWindow(frameCounter, hwTimestamp);
    }
}

void AutoPpmCalibrator::AnalyzeMeasurementWindow()
{
    const uint64_t intervals = m_measurementWindowEndFrame - m_measurementWindowStartFrame;
    
    if (intervals < MIN_MEASUREMENT_FRAMES)
    {
        DEBUGLOG("AutoPpmCalibrator: Insufficient frames (%llu) for analysis", intervals);
        return;
    }

    // DIAGNOSTIC: Check current state at entry
    DEBUGLOG("AutoPpmCalibrator: AnalyzeMeasurementWindow ENTRY - m_trimTotalPpm=%d, m_hasAppliedFirstCorrection=%d",
        m_trimTotalPpm, m_hasAppliedFirstCorrection ? 1 : 0);

    // Calculate actual hardware ticks elapsed
    const uint64_t actualTicks = m_measurementWindowEndTimestamp - m_measurementWindowStartTimestamp;
    const double measuredTicksPerFrame = (double)actualTicks / intervals;

    // Calculate theoretical ticks per frame from configured rational rate
    // theoreticalTicksPerFrame = (frameDurationTicks / timeScale) * hwTicksPerSec
    const double theoreticalTicksPerFrame = 
        ((double)m_frameDurationTicks / (double)m_timeScale) * (double)m_hwTicksPerSec;

    // Calculate drift: (measured - theoretical) / theoretical
    // This gives us the hardware clock drift relative to the configured rational rate
    const double driftRatio = (measuredTicksPerFrame - theoreticalTicksPerFrame) / theoreticalTicksPerFrame;
    const double driftPpmFloat = driftRatio * 1000000.0;
    
    // Round to nearest integer instead of truncating
    int64_t rawDriftPpm = (int64_t)(driftPpmFloat + (driftPpmFloat >= 0 ? 0.5 : -0.5));

    // Apply current correction to get remaining drift
    const int remainingPpm = (int)(rawDriftPpm - m_trimTotalPpm);

    // DIAGNOSTIC LOGGING
    DEBUGLOG("AutoPpmCalibrator: === DIAGNOSTIC ===");
    DEBUGLOG("  Measurement window: frames %llu to %llu (%llu intervals)",
        m_measurementWindowStartFrame, m_measurementWindowEndFrame, intervals);
    DEBUGLOG("  Hardware timestamps: %llu to %llu (%llu ticks elapsed)",
        m_measurementWindowStartTimestamp, m_measurementWindowEndTimestamp, actualTicks);
    DEBUGLOG("  Measured ticks/frame: %.9f (%.9f Hz)",
        measuredTicksPerFrame, m_hwTicksPerSec / measuredTicksPerFrame);
    DEBUGLOG("  Theoretical ticks/frame: %.9f (%.9f Hz)",
        theoreticalTicksPerFrame, m_hwTicksPerSec / theoreticalTicksPerFrame);
    DEBUGLOG("  Difference: %.9f ticks/frame", measuredTicksPerFrame - theoreticalTicksPerFrame);
    DEBUGLOG("  Drift ratio: %.12f", driftRatio);
    DEBUGLOG("  Drift PPM (float): %.3f", driftPpmFloat);
    DEBUGLOG("  Raw drift (rounded): %I64d PPM", rawDriftPpm);
    DEBUGLOG("  Current correction: %d PPM", m_trimTotalPpm);
    DEBUGLOG("  Remaining correction needed: %d PPM", remainingPpm);
    DEBUGLOG("AutoPpmCalibrator: === END DIAGNOSTIC ===");

    // Check consistency
    if (!CheckConsistency(remainingPpm))
    {
        DEBUGLOG("AutoPpmCalibrator: Measurement not consistent (remaining=%d, last=%d, diff=%d)",
            remainingPpm, m_lastRemainingPpm, abs(remainingPpm - m_lastRemainingPpm));
        return;
    }

    // Check for oscillation
    if (CheckOscillation(remainingPpm))
    {
        DEBUGLOG("AutoPpmCalibrator: Oscillation detected - skipping correction");
        m_consistentCount = 0;
        return;
    }

    // Check if correction should be applied
    if (abs(remainingPpm) >= APPLY_THRESHOLD_PPM && m_consistentCount >= CONSISTENT_COUNT_REQUIRED)
    {
        // Regular correction for larger drift
        const int correction = CalculateIncrementalCorrection(remainingPpm);
        
        if (correction != 0)
        {
            ApplyCorrection(correction);
        }
        else
        {
            DEBUGLOG("AutoPpmCalibrator: Correction rounded to zero, no adjustment applied");
        }
    }
    else if (abs(remainingPpm) >= FINE_TUNE_THRESHOLD_PPM && m_consistentCount >= FINE_TUNE_CONSISTENT_COUNT)
    {
        // Fine-tuning: Apply remaining drift if consistently measured over longer period
        DEBUGLOG("AutoPpmCalibrator: Fine-tuning mode - applying 100%% of remaining %d PPM after %u consistent measurements",
            remainingPpm, m_consistentCount);
        ApplyCorrection(remainingPpm);  // Apply full remaining drift
    }
    else if (abs(remainingPpm) < FINE_TUNE_THRESHOLD_PPM)
    {
        DEBUGLOG("AutoPpmCalibrator: Remaining drift (%d PPM) below fine-tune threshold (%d PPM) - calibration stable",
            remainingPpm, FINE_TUNE_THRESHOLD_PPM);
    }
    else
    {
        DEBUGLOG("AutoPpmCalibrator: Remaining drift %d PPM, waiting for more consistent measurements (count=%u, need=%u for fine-tune)",
            remainingPpm, m_consistentCount, 
            abs(remainingPpm) >= APPLY_THRESHOLD_PPM ? CONSISTENT_COUNT_REQUIRED : FINE_TUNE_CONSISTENT_COUNT);
    }
}

int64_t AutoPpmCalibrator::CalculateRawDriftPpm() const
{
    const uint64_t intervals = m_measurementWindowEndFrame - m_measurementWindowStartFrame;
    
    // Calculate expected hardware ticks
    uint64_t expectedTicks = U64_MulDiv(intervals, m_frameDurationTicks, m_timeScale);
    expectedTicks = U64_MulDiv(expectedTicks, m_hwTicksPerSec, 1ULL);

    // Calculate actual hardware ticks
    const uint64_t actualTicks = m_measurementWindowEndTimestamp - m_measurementWindowStartTimestamp;

    // Calculate drift: (actual - expected) / expected * 1,000,000
    // Use signed arithmetic for proper negative drift handling
    const int64_t diff = (int64_t)actualTicks - (int64_t)expectedTicks;
    
    // Avoid division by zero
    if (expectedTicks == 0)
        return 0;

    // Calculate PPM with proper rounding for both positive and negative values
    // PPM = (diff * 1000000) / expected
    // For positive diff: add half divisor to round up
    // For negative diff: subtract half divisor to round down (more negative)
    int64_t ppm;
    if (diff >= 0)
    {
        // Hardware running faster than expected (positive drift)
        ppm = ((diff * 1000000LL) + ((int64_t)expectedTicks / 2)) / (int64_t)expectedTicks;
    }
    else
    {
        // Hardware running slower than expected (negative drift)
        // Subtract half to round away from zero (more negative)
        ppm = ((diff * 1000000LL) - ((int64_t)expectedTicks / 2)) / (int64_t)expectedTicks;
    }

    return ppm;
}

int AutoPpmCalibrator::CalculateIncrementalCorrection(int remainingPpm)
{
    int correction;

    if (!m_hasAppliedFirstCorrection)
    {
        // First correction: apply ~95% of remaining drift
        correction = (remainingPpm * FIRST_CORRECTION_PERCENT) / 100;
        m_hasAppliedFirstCorrection = true;
        
        DEBUGLOG("AutoPpmCalibrator: First correction - applying %d%% of %d PPM = %d PPM",FIRST_CORRECTION_PERCENT, remainingPpm, correction);
    }
    else
    {
        // Later corrections: apply ~25% of remaining drift (gentler adjustments)
        correction = (remainingPpm * LATER_CORRECTION_PERCENT) / 100;
        
        DEBUGLOG("AutoPpmCalibrator: Incremental correction - applying %d%% of %d PPM = %d PPM",LATER_CORRECTION_PERCENT, remainingPpm, correction);
    }

    // Round to nearest integer (already done by integer division, but ensure non-zero if meaningful)
    if (correction == 0 && abs(remainingPpm) >= APPLY_THRESHOLD_PPM)
    {
        // Round up to at least ±1 if drift is significant
        correction = (remainingPpm > 0) ? 1 : -1;
    }

    return correction;
}

bool AutoPpmCalibrator::CheckConsistency(int remainingPpm)
{
    const int diff = abs(remainingPpm - m_lastRemainingPpm);
    
    if (m_lastRemainingPpm == 0)
    {
        // First measurement - automatically consistent
        m_lastRemainingPpm = remainingPpm;
        m_consistentCount = 1;
        return true;
    }

    if (diff < CONSISTENCY_BAND_PPM)
    {
        // Measurement is consistent with previous
        m_consistentCount++;
        m_lastRemainingPpm = remainingPpm;
        
        DEBUGLOG("AutoPpmCalibrator: Consistent measurement #%u (diff=%d PPM within %d PPM band)", m_consistentCount, diff, CONSISTENCY_BAND_PPM);
        
        return true;
    }
    else
    {
        // Measurement not consistent - reset counter
        DEBUGLOG("AutoPpmCalibrator: Inconsistent measurement (diff=%d PPM exceeds %d PPM band) - resetting consistency",diff, CONSISTENCY_BAND_PPM);
        
        m_consistentCount = 1;
        m_lastRemainingPpm = remainingPpm;
        return false;
    }
}

bool AutoPpmCalibrator::CheckOscillation(int remainingPpm)
{
    // Determine sign of remaining drift
    int currentSign = 0;
    if (remainingPpm > FLIP_THRESHOLD_PPM)
        currentSign = 1;
    else if (remainingPpm < -FLIP_THRESHOLD_PPM)
        currentSign = -1;

    if (m_lastSignOfRemaining == 0)
    {
        // First significant measurement
        m_lastSignOfRemaining = currentSign;
        m_oscillationCount = 0;
        return false;
    }

    // Check for sign flip
    if (currentSign != 0 && currentSign != m_lastSignOfRemaining)
    {
        m_oscillationCount++;
        m_lastSignOfRemaining = currentSign;
        
        DEBUGLOG("AutoPpmCalibrator: Sign flip detected (oscillation count=%u)", m_oscillationCount);
        
        // Detect oscillation pattern
        if (m_oscillationCount >= 2)
        {
            DEBUGLOG("AutoPpmCalibrator: Oscillation pattern detected - correction unstable");
            return true;
        }
    }
    else if (currentSign == m_lastSignOfRemaining)
    {
        // Same sign - reset oscillation counter
        m_oscillationCount = 0;
    }

    return false;
}

void AutoPpmCalibrator::ApplyCorrection(int correction)
{
    // Calculate new total PPM
    int newTotalPpm = m_trimTotalPpm + correction;
    
    // Clamp to maximum allowed PPM
    if (newTotalPpm > MAX_PPM_LIMIT)
    {
        DEBUGLOG("AutoPpmCalibrator: Correction would exceed max limit (%d > %d), clamping", newTotalPpm, MAX_PPM_LIMIT);
        newTotalPpm = MAX_PPM_LIMIT;
    }
    else if (newTotalPpm < -MAX_PPM_LIMIT)
    {
        DEBUGLOG("AutoPpmCalibrator: Correction would exceed min limit (%d < %d), clamping",newTotalPpm, -MAX_PPM_LIMIT);
        newTotalPpm = -MAX_PPM_LIMIT;
    }

    DEBUGLOG("AutoPpmCalibrator: Applying correction: %d PPM -> %d PPM (delta: %d PPM)",m_trimTotalPpm, newTotalPpm, correction);

    m_trimTotalPpm = newTotalPpm;
    
    // Reset consistency tracking after applying correction
    m_consistentCount = 0;
    m_lastRemainingPpm = 0;
}

void AutoPpmCalibrator::ResetMeasurementWindow(uint64_t frameCounter, uint64_t hwTimestamp)
{
    m_measurementWindowStartFrame = frameCounter;
    m_measurementWindowEndFrame = frameCounter;
    m_measurementWindowStartTimestamp = hwTimestamp;
    m_measurementWindowEndTimestamp = hwTimestamp;
}

void AutoPpmCalibrator::Reset()
{
    m_isInitialized = false;
    m_trimTotalPpm = 0;
    m_measurementWindowStartFrame = 0;
    m_measurementWindowEndFrame = 0;
    m_measurementWindowStartTimestamp = 0;
    m_measurementWindowEndTimestamp = 0;
    m_lastRemainingPpm = 0;
    m_consistentCount = 0;
    m_oscillationCount = 0;
    m_lastSignOfRemaining = 0;
    m_hasAppliedFirstCorrection = false;
    m_frameDurationTicks = 0;
    m_timeScale = 0;
    m_hwTicksPerSec = 0;
}

AutoPpmCalibrator::CalibrationStats AutoPpmCalibrator::GetStats() const
{
    CalibrationStats stats;
    stats.measurementCount = m_measurementWindowEndFrame - m_measurementWindowStartFrame;
    stats.currentTotalPpm = m_trimTotalPpm;
    stats.lastRemainingPpm = m_lastRemainingPpm;
    stats.consistentCount = m_consistentCount;
    stats.oscillationCount = m_oscillationCount;
    stats.isCalibrating = IsActive();
    return stats;
}
