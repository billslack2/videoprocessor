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
 * Measures the drift between theoretical frame timing and actual hardware timestamps,
 * then automatically adjusts the PPM correction value to compensate for the detected drift.
 * 
 * This is used when no manual correction.cfg value is present, or when the config specifies "AUTO".
 */
class AutoPpmCalibrator
{
public:
    AutoPpmCalibrator();
    ~AutoPpmCalibrator() = default;

    /**
     * Initialize the calibrator with timing parameters
     * @param frameDurationTicks Frame duration in timing clock ticks (numerator)
     * @param timeScale Timing clock ticks per second (denominator)
     * @param hwTicksPerSec Hardware clock ticks per second (e.g., 1000000 for DeckLink)
     */
    void Initialize(
        uint64_t frameDurationTicks,
        uint64_t timeScale,
        uint64_t hwTicksPerSec);

    /**
     * Feed a frame measurement into the calibrator
     * @param frameCounter Current frame counter (sequential)
     * @param hwTimestamp Raw hardware timestamp (in hardware clock ticks)
     */
    void OnFrame(uint64_t frameCounter, uint64_t hwTimestamp);

    /**
     * Get the current total PPM correction value
     * @return Current PPM adjustment (positive = faster, negative = slower)
     */
    int GetTotalPpmCorrection() const { return m_trimTotalPpm; }

    /**
     * Check if calibrator is active (has been initialized and collecting data)
     * @return true if calibrator is actively measuring
     */
    bool IsActive() const { return m_isInitialized && m_measurementWindowStartFrame > 0; }

    /**
     * Reset the calibrator to initial state (clears all measurements)
     */
    void Reset();

    /**
     * Get measurement statistics for debugging
     */
    struct CalibrationStats
    {
        uint64_t measurementCount;        // Number of frames measured
        int currentTotalPpm;               // Current total PPM correction
        int lastRemainingPpm;              // Last measured remaining drift
        uint32_t consistentCount;          // Consecutive consistent measurements
        uint32_t oscillationCount;         // Oscillation detection counter
        bool isCalibrating;                // Currently in calibration mode
    };
    
    CalibrationStats GetStats() const;

private:
    // Calibration constants
    static const uint64_t MEASUREMENT_INTERVAL_FRAMES = 600;    // Measure every 600 frames (~10 sec at 60fps)
    static const uint64_t MIN_MEASUREMENT_FRAMES = 300;         // Minimum frames before first measurement
    static const int64_t CONSISTENCY_BAND_PPM = 2;              // ±2 PPM consistency tolerance
    static const int64_t APPLY_THRESHOLD_PPM = 3;               // Apply correction if |remaining| >= 3 PPM
    static const int64_t FINE_TUNE_THRESHOLD_PPM = 2;           // Fine-tune threshold for final convergence
    static const uint32_t CONSISTENT_COUNT_REQUIRED = 3;        // Need 3 consistent measurements
    static const uint32_t FINE_TUNE_CONSISTENT_COUNT = 6;       // Need 6 consistent for fine-tuning
    static const int64_t FLIP_THRESHOLD_PPM = 5;                // Oscillation detection threshold
    static const int MAX_PPM_LIMIT = 100;                       // Maximum allowed PPM correction (±100)
    static const int FIRST_CORRECTION_PERCENT = 95;             // First correction applies 95% of drift
    static const int LATER_CORRECTION_PERCENT = 25;             // Later corrections apply 25% of drift
    static const int FINE_TUNE_CORRECTION_PERCENT = 100;        // Fine-tune applies 100% of remaining drift

    // Core state
    bool m_isInitialized = false;
    int m_trimTotalPpm = 0;  // Total accumulated PPM correction

    // Measurement window (tracks one interval of frames)
    uint64_t m_measurementWindowStartFrame = 0;
    uint64_t m_measurementWindowEndFrame = 0;
    uint64_t m_measurementWindowStartTimestamp = 0;
    uint64_t m_measurementWindowEndTimestamp = 0;

    // Control state
    int m_lastRemainingPpm = 0;        // Previous remaining drift measurement
    uint32_t m_consistentCount = 0;    // Consecutive consistent measurements
    uint32_t m_oscillationCount = 0;   // Count of sign flips for oscillation detection
    int m_lastSignOfRemaining = 0;     // Sign of last remaining PPM (+1, -1, or 0)
    bool m_hasAppliedFirstCorrection = false;  // Track if we've done initial 95% correction

    // Timing parameters (set during Initialize)
    uint64_t m_frameDurationTicks = 0;
    uint64_t m_timeScale = 0;
    uint64_t m_hwTicksPerSec = 0;

    // Helper methods
    void AnalyzeMeasurementWindow();
    int64_t CalculateRawDriftPpm() const;
    int CalculateIncrementalCorrection(int remainingPpm);
    bool CheckConsistency(int remainingPpm);
    bool CheckOscillation(int remainingPpm);
    void ApplyCorrection(int correction);
    void ResetMeasurementWindow(uint64_t frameCounter, uint64_t hwTimestamp);
};
