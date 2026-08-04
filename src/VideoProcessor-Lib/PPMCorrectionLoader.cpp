/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <DebugLog.h>
#include <ConfigFile.h>

#include "PPMCorrectionLoader.h"

PPMCorrectionLoader::PPMCorrectionLoader()
{
}

bool PPMCorrectionLoader::LoadCorrectionFile()
{
    // Clear any existing corrections
    Clear();

    ConfigFile unifiedConfig;
    if (unifiedConfig.Load())
    {
        if (!unifiedConfig.GetWarnings().empty())
        {
            for (const auto& warning : unifiedConfig.GetWarnings())
            {
                DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: Invalid VideoProcessor.cfg syntax: %S"), warning.c_str()));
            }

            DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: VideoProcessor.cfg has syntax errors - using default PPM values")));
            return false;
        }

        if (unifiedConfig.HasSection("directshow.ppm"))
        {
            std::string rawPpm;
			if (!unifiedConfig.TryGetString("directshow.ppm", "ppm", rawPpm))
            {
                DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: VideoProcessor.cfg [directshow.ppm] requires ppm")));
				return false;
			}
			if (ConfigFile::NormalizeName(rawPpm) == "auto")
			{
				// Preserve the established sentinel consumed by the timing path.
				// A unified value applies the same automatic policy at every cadence.
				for (int rate = 1; rate <= 1000; ++rate)
					m_corrections[rate] = 999999;
				DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: Loaded [directshow.ppm] ppm=AUTO for all source rates")));
				return true;
			}
			try
            {
                size_t consumed = 0;
                const int ppm = std::stoi(ConfigFile::Trim(rawPpm), &consumed);
                if (consumed != ConfigFile::Trim(rawPpm).size() ||
                    ppm < -1000000 || ppm > 1000000)
                    throw std::out_of_range("ppm");
                // The VP-0079 form is source-timing policy, not a brittle
                // nominal-refresh lookup. Populate the legacy lookup domain
                // so every actual cadence receives this one correction.
                for (int rate = 1; rate <= 1000; ++rate)
                    m_corrections[rate] = ppm;
                DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: Loaded [directshow.ppm] ppm=%d for all source rates"), ppm));
                return true;
            }
            catch (const std::exception&)
            {
                DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: Invalid [directshow.ppm] ppm: %S"), rawPpm.c_str()));
                return false;
            }
        }

        if (!unifiedConfig.HasSection("ppm_correction"))
        {
            DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: VideoProcessor.cfg [ppm_correction] not found - using default PPM values")));
            return false;
        }

        DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: Loading VideoProcessor.cfg [ppm_correction]")));

        int validEntries = 0;
        const auto* ppmCorrections = unifiedConfig.GetSectionValues("ppm_correction");
        if (ppmCorrections)
        {
            for (const auto& correction : *ppmCorrections)
            {
                if (ParseConfigLine(correction.first + "=" + correction.second))
                    validEntries++;
                else
                    DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: Invalid VideoProcessor.cfg ppm_correction entry: %S=%S"),
                        correction.first.c_str(), correction.second.c_str()));
            }
        }

        if (validEntries > 0)
        {
            DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: Loaded %d PPM corrections from VideoProcessor.cfg"), validEntries));

            for (const auto& correction : m_corrections)
            {
                DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: %d Hz = %d PPM"),
                    correction.first, correction.second));
            }

            return true;
        }

        Clear();
        DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: VideoProcessor.cfg [ppm_correction] had no valid entries - using default PPM values")));
        return false;
    }

    DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: VideoProcessor.cfg not found - using default PPM values")));
    return false;
}

bool PPMCorrectionLoader::ParseConfigLine(const std::string& line)
{
    // Remove any whitespace
    std::string cleanLine = line;
    cleanLine.erase(std::remove_if(cleanLine.begin(), cleanLine.end(), ::isspace), cleanLine.end());
    
    if (cleanLine.empty())
        return true; // Empty line after cleanup is OK

    // Split the line into tokens by space (for multiple entries on one line)
    std::istringstream lineStream(line);
    std::string token;
    bool parsedAny = false;

    while (lineStream >> token)
    {
        // Find the '=' separator
        size_t equalPos = token.find('=');
        if (equalPos == std::string::npos)
            continue;

        // Extract refresh rate and PPM value
        std::string refreshRateStr = token.substr(0, equalPos);
        std::string ppmStr = token.substr(equalPos + 1);

        try
        {
            // Parse refresh rate (integer)
            int refreshRate = std::stoi(refreshRateStr);
            if (refreshRate <= 0 || refreshRate > 1000)
            {
                DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: Invalid refresh rate %d (must be 1-1000)"), refreshRate));
                continue;
            }

            // Check for AUTO keyword
            if (ConfigFile::NormalizeName(ppmStr) == "auto")
            {
                // Store 999999 as sentinel value for AUTO mode
                m_corrections[refreshRate] = 999999;
                parsedAny = true;
                DbgLog((LOG_TRACE, 2, TEXT("PPMCorrectionLoader: Parsed %d Hz = AUTO (auto-calibration)"), refreshRate));
                continue;
            }

            // Parse PPM value (can be negative)
            int ppmValue = std::stoi(ppmStr);
            if (ppmValue < -1000000 || ppmValue > 1000000)
            {
                DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: Invalid PPM value %d (must be -1000000 to 1000000)"), ppmValue));
                continue;
            }

            // Store the correction
            m_corrections[refreshRate] = ppmValue;
            parsedAny = true;

            DbgLog((LOG_TRACE, 2, TEXT("PPMCorrectionLoader: Parsed %d Hz = %d PPM"), refreshRate, ppmValue));
        }
        catch (const std::exception&)
        {
            DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: Failed to parse token: %S"), token.c_str()));
        }
    }

    return parsedAny;
}

int PPMCorrectionLoader::GetPPMCorrection(double refreshRate) const
{
    if (m_corrections.empty())
        return 0;

    // Find the best matching refresh rate
    int bestMatch = FindBestMatch(refreshRate);
    
    if (bestMatch == 0)
        return 0;

    auto it = m_corrections.find(bestMatch);
    if (it != m_corrections.end())
    {
        DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: Using %d PPM correction for %.3f Hz (matched %d Hz)"), 
            it->second, refreshRate, bestMatch));
        return it->second;
    }

    return 0;
}

int PPMCorrectionLoader::FindBestMatch(double targetRate) const
{
    if (m_corrections.empty())
        return 0;

    int bestMatch = 0;
    double bestDistance = 1000000.0; // Large initial value

    // FIRST: Try truncated rate (first two digits before decimal)
    // e.g., 59.94 Hz ? 59 (not 60)
    // e.g., 29.97 Hz ? 29 (not 30)
    int truncatedRate = static_cast<int>(targetRate);
    auto it = m_corrections.find(truncatedRate);
    if (it != m_corrections.end())
    {
        double truncDistance = std::abs(targetRate - truncatedRate);
        if (truncDistance < bestDistance)
        {
            bestMatch = truncatedRate;
            bestDistance = truncDistance;
        }
    }

    // SECOND: Fall back to tolerance-based matching if truncation didn't find anything
    // Only use tolerance matching if we haven't already found a truncated match
    if (bestMatch == 0)
    {
        for (const auto& correction : m_corrections)
        {
            double distance = std::abs(targetRate - correction.first);
            
            // Accept matches within tolerance
            if (distance <= REFRESH_RATE_TOLERANCE && distance < bestDistance)
            {
                bestMatch = correction.first;
                bestDistance = distance;
            }
        }
    }

    return bestMatch;
}
