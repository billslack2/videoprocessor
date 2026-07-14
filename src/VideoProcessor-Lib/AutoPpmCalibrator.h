/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <pch.h>

/**
 * Automatic PPM (Parts Per Million) calibrator for hardware timestamp drift correction.
 * 
 * Consumes PRE-CALCULATED PPM values from DirectShowVideoRenderer::UpdatePPMMeasurement()
 * and applies filtering, smoothing, and convergence logic to automatically adjust timing.
 * 
 * This eliminates duplicate PPM calculation - the renderer's 5-second rolling window
 * measurement is the single source of truth, and this class just filters/applies it.
 * 
 * CALIBRATION STRATEGY:
 * - Measurement window: Every PPM update (~5 seconds from renderer)
 * - Initial convergence: 2 consistent measurements
 * - First correction: Applies 95% of detected drift
 * - Fine-tuning: 6 consistent measurements for remaining <2 PPM drift
 * - Later corrections: Apply 25% of remaining drift to avoid oscillation
 * - Maximum correction: ±100 PPM
 */
class AutoPpmCalibrator
{
public:
    AutoPpmCalibrator();
    ~AutoPpmCalibrator() = default;

    /**
     * Initialize the calibrator (now simpler - no timing parameters needed)
     */
    void Initialize();

    /**
     * Feed a pre-calculated PPM measurement into the calibrator
     * @param measuredPpm Current PPM deviation from renderer (positive = faster, negative = slower)
     * 
     * This replaces the old OnFrame() method. The PPM calculation is now done externally
     * by DirectShowVideoRenderer::UpdatePPMMeasurement() using a 5-second rolling window.
     */
    void OnPPM(int measuredPpm);

    /**
     * Get the current total PPM correction value
     * @return Current PPM adjustment (positive = faster, negative = slower)
     */
    int GetTotalPpmCorrection() const { return m_trimTotalPpm; }

    /**
     * Check if calibrator is active (has been initialized and collecting data)
     * @return true if calibrator is actively measuring
     */
    bool IsActive() const { return m_isInitialized; }  // Fixed: Don't require measurements - they come AFTER initialization

    /**
     * Reset the calibrator to initial state (clears all measurements)
     */
    void Reset();

    /**
     * Get measurement statistics for debugging
     */
    struct CalibrationStats
    {
        uint64_t measurementCount;        // Number of PPM measurements received
        int currentTotalPpm;               // Current total PPM correction
        int lastRemainingPpm;              // Last measured remaining drift
        uint32_t consistentCount;          // Consecutive consistent measurements
        uint32_t oscillationCount;         // Oscillation detection counter
        bool isCalibrating;                // Currently in calibration mode
    };
    
    CalibrationStats GetStats() const;

private:
    // Calibration constants
    static const int64_t CONSISTENCY_BAND_PPM = 2;              // ±2 PPM consistency tolerance
    static const int64_t APPLY_THRESHOLD_PPM = 3;               // Apply correction if |remaining| >= 3 PPM
    static const int64_t FINE_TUNE_THRESHOLD_PPM = 2;           // Fine-tune threshold for final convergence
    static const uint32_t CONSISTENT_COUNT_REQUIRED = 2;        // 2 consistent measurements to apply correction
    static const uint32_t FINE_TUNE_CONSISTENT_COUNT = 6;       // Need 6 consistent for fine-tuning
    static const int64_t FLIP_THRESHOLD_PPM = 5;                // Oscillation detection threshold
    static const int MAX_PPM_LIMIT = 100;                       // Maximum allowed PPM correction (±100)
    static const int FIRST_CORRECTION_PERCENT = 95;             // First correction applies 95% of drift
    static const int LATER_CORRECTION_PERCENT = 25;             // Later corrections apply 25% of drift

    // Core state
    bool m_isInitialized = false;
    int m_trimTotalPpm = 0;  // Total accumulated PPM correction
    uint64_t m_measurementCount = 0;  // Number of PPM samples received

    // Control state
    int m_lastRemainingPpm = 0;        // Previous remaining drift measurement
    uint32_t m_consistentCount = 0;    // Consecutive consistent measurements
    uint32_t m_oscillationCount = 0;   // Count of sign flips for oscillation detection
    int m_lastSignOfRemaining = 0;     // Sign of last remaining PPM (+1, -1, or 0)
    bool m_hasAppliedFirstCorrection = false;  // Track if we've done initial 95% correction
    bool m_isFirstMeasurement = true;  // Proper flag for first measurement after reset/correction

    // Helper methods
    void AnalyzeMeasurement(int measuredPpm);
    int CalculateIncrementalCorrection(int remainingPpm);
    bool CheckConsistency(int remainingPpm);
    bool CheckOscillation(int remainingPpm);
    void ApplyCorrection(int correction);
};
