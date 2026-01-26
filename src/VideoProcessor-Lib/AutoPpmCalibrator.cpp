/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <DebugLog.h>

#include "AutoPpmCalibrator.h"

AutoPpmCalibrator::AutoPpmCalibrator()
{
    Reset();
}

void AutoPpmCalibrator::Initialize()
{
    m_isInitialized = true;
    m_trimTotalPpm = 0;  // Start with no correction
    m_measurementCount = 0;

    DEBUGLOG("AutoPpmCalibrator: Initialized to consume pre-calculated PPM values from renderer");
    DEBUGLOG("AutoPpmCalibrator: Will apply filtering, smoothing, and convergence logic");
}

void AutoPpmCalibrator::OnPPM(int measuredPpm)
{
    if (!m_isInitialized)
    {
        DEBUGLOG("AutoPpmCalibrator: OnPPM called but not initialized! Ignoring measurement.");
        return;
    }

    m_measurementCount++;
    
    // **CRITICAL FIX: Calculate remaining drift CORRECTLY**
    // measuredPpm is the RAW measurement from the renderer (e.g., +10 PPM hardware drift)
    // m_trimTotalPpm is the correction we've ALREADY applied (e.g., +9 PPM)
    // remainingPpm is what's LEFT to correct (e.g., +10 - 9 = +1 PPM)
    const int remainingPpm = measuredPpm - m_trimTotalPpm;

    DEBUGLOG("AutoPpmCalibrator: PPM measurement #%llu - measured=%d, currentCorrection=%d, remaining=%d, consistent=%u",
        m_measurementCount, measuredPpm, m_trimTotalPpm, remainingPpm, m_consistentCount);

    // **SPIKE DETECTION: Warn if remaining drift is unusually large after calibration started**
    if (m_hasAppliedFirstCorrection && abs(remainingPpm) > 30)
    {
        DEBUGLOG("AutoPpmCalibrator: WARNING - Large remaining drift detected (%d PPM) after calibration started!", remainingPpm);
    }

    // Analyze and potentially apply correction
    AnalyzeMeasurement(remainingPpm);
    
    // Log final state after analysis
    DEBUGLOG("AutoPpmCalibrator: After analysis - totalCorrection=%d, consistent=%u, firstApplied=%d",
        m_trimTotalPpm, m_consistentCount, m_hasAppliedFirstCorrection);
}

void AutoPpmCalibrator::AnalyzeMeasurement(int remainingPpm)
{
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
        // **IMPROVED LOGGING: Show actual thresholds being used**
        const uint32_t requiredCount = (abs(remainingPpm) >= APPLY_THRESHOLD_PPM) ? 
            CONSISTENT_COUNT_REQUIRED : FINE_TUNE_CONSISTENT_COUNT;
        
        DEBUGLOG("AutoPpmCalibrator: Remaining drift %d PPM, waiting for more consistent measurements (count=%u/%u)",
            remainingPpm, m_consistentCount, requiredCount);
    }
}

int AutoPpmCalibrator::CalculateIncrementalCorrection(int remainingPpm)
{
    int correction;

    if (!m_hasAppliedFirstCorrection)
    {
        // First correction: apply ~95% of remaining drift
        correction = (remainingPpm * FIRST_CORRECTION_PERCENT) / 100;
        m_hasAppliedFirstCorrection = true;
        
        DEBUGLOG("AutoPpmCalibrator: First correction - applying %d%% of %d PPM = %d PPM",
            FIRST_CORRECTION_PERCENT, remainingPpm, correction);
    }
    else
    {
        // Later corrections: apply ~25% of remaining drift (gentler adjustments)
        correction = (remainingPpm * LATER_CORRECTION_PERCENT) / 100;
        
        DEBUGLOG("AutoPpmCalibrator: Incremental correction - applying %d%% of %d PPM = %d PPM",
            LATER_CORRECTION_PERCENT, remainingPpm, correction);
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
    
    if (m_isFirstMeasurement)
    {
        // First measurement after reset/correction - automatically consistent
        m_lastRemainingPpm = remainingPpm;
        m_consistentCount = 1;
        m_isFirstMeasurement = false;
        DEBUGLOG("AutoPpmCalibrator: First measurement after reset/correction: %d PPM", remainingPpm);
        return true;
    }

    if (diff <= CONSISTENCY_BAND_PPM)
    {
        // Measurement is consistent with previous
        m_consistentCount++;
        m_lastRemainingPpm = remainingPpm;
        
        DEBUGLOG("AutoPpmCalibrator: Consistent measurement #%u (diff=%d PPM within %d PPM band)",
            m_consistentCount, diff, CONSISTENCY_BAND_PPM);
        
        return true;
    }
    else
    {
        // Measurement not consistent - reset counter
        DEBUGLOG("AutoPpmCalibrator: Inconsistent measurement (diff=%d PPM exceeds %d PPM band) - resetting consistency",
            diff, CONSISTENCY_BAND_PPM);
        
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
        DEBUGLOG("AutoPpmCalibrator: Correction would exceed max limit (%d > %d), clamping",
            newTotalPpm, MAX_PPM_LIMIT);
        newTotalPpm = MAX_PPM_LIMIT;
    }
    else if (newTotalPpm < -MAX_PPM_LIMIT)
    {
        DEBUGLOG("AutoPpmCalibrator: Correction would exceed min limit (%d < %d), clamping",
            newTotalPpm, -MAX_PPM_LIMIT);
        newTotalPpm = -MAX_PPM_LIMIT;
    }

    DEBUGLOG("AutoPpmCalibrator: Applying correction: %d PPM -> %d PPM (delta: %d PPM)",
        m_trimTotalPpm, newTotalPpm, correction);

    m_trimTotalPpm = newTotalPpm;
    
    // Reset consistency tracking after applying correction
    m_consistentCount = 0;
    m_lastRemainingPpm = 0;
    m_isFirstMeasurement = true;
}

void AutoPpmCalibrator::Reset()
{
    m_isInitialized = false;
    m_trimTotalPpm = 0;
    m_measurementCount = 0;
    m_lastRemainingPpm = 0;
    m_consistentCount = 0;
    m_oscillationCount = 0;
    m_lastSignOfRemaining = 0;
    m_hasAppliedFirstCorrection = false;
    m_isFirstMeasurement = true;
    
    DEBUGLOG("AutoPpmCalibrator: Complete state reset - ready for new PPM measurements");
}

AutoPpmCalibrator::CalibrationStats AutoPpmCalibrator::GetStats() const
{
    CalibrationStats stats;
    stats.measurementCount = m_measurementCount;
    stats.currentTotalPpm = m_trimTotalPpm;
    stats.lastRemainingPpm = m_lastRemainingPpm;
    stats.consistentCount = m_consistentCount;
    stats.oscillationCount = m_oscillationCount;
    stats.isCalibrating = IsActive();
    return stats;
}
