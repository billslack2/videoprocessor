/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <map>
#include <string>

/**
 * PPM (Parts Per Million) correction loader for refresh rate specific timing adjustments.
 * 
 * Loads correction values from VideoProcessor.cfg [ppm_correction] with format:
 * 60=5 59=5 50=5 30=5 24=0 23=0
 * 
 * Where left side is refresh rate (Hz) and right side is PPM adjustment value.
 * Positive values make the stream faster, negative values make it slower.
 * 
 * Special value "AUTO" enables automatic calibration:
 * 60=AUTO 59=AUTO 50=AUTO
 * 
 * The system will measure actual hardware drift and automatically adjust PPM over time.
 */
class PPMCorrectionLoader
{
public:
    PPMCorrectionLoader();
    ~PPMCorrectionLoader() = default;

    /**
     * Load PPM corrections from VideoProcessor.cfg
     * @return true if file was loaded successfully, false if file doesn't exist or has errors
     */
    bool LoadCorrectionFile();

    /**
     * Get PPM correction for a specific refresh rate
     * @param refreshRate The refresh rate in Hz (e.g., 60.0, 59.94, 50.0, etc.)
     * @return PPM correction value (0 if not found or file not loaded)
     */
    int GetPPMCorrection(double refreshRate) const;

    /**
     * Check if corrections were loaded successfully
     * @return true if corrections are available
     */
    bool HasCorrections() const { return !m_corrections.empty(); }

    /**
     * Get number of loaded corrections
     * @return count of correction entries
     */
    size_t GetCorrectionCount() const { return m_corrections.size(); }

    /**
     * Clear all loaded corrections
     */
    void Clear() { m_corrections.clear(); }

private:
    /**
     * Parse a single line from the configuration file
     * @param line Line to parse (e.g., "60=5")
     * @return true if line was parsed successfully
     */
    bool ParseConfigLine(const std::string& line);

    /**
     * Find the best matching refresh rate from available corrections
     * @param targetRate The desired refresh rate
     * @return matching rate key, or 0 if no suitable match found
     */
    int FindBestMatch(double targetRate) const;

    // Map of refresh rate (as integer) to PPM correction value
    std::map<int, int> m_corrections;
    
    // Tolerance for matching refresh rates (e.g., 59.94 matches 60)
    static constexpr double REFRESH_RATE_TOLERANCE = 0.5;
};
