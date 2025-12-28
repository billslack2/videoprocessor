/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <DebugLog.h>

#include "PPMCorrectionLoader.h"

PPMCorrectionLoader::PPMCorrectionLoader()
{
}

bool PPMCorrectionLoader::LoadCorrectionFile()
{
    // Clear any existing corrections
    Clear();

    // Try to open correction.cfg in the current directory
    std::ifstream configFile("correction.cfg");
    
    if (!configFile.is_open())
    {
        DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: correction.cfg not found - using default PPM values")));
        return false;
    }

    DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: Loading correction.cfg")));

    std::string line;
    int lineNumber = 0;
    int validEntries = 0;

    while (std::getline(configFile, line))
    {
        lineNumber++;
        
        // Skip empty lines and comments (lines starting with #)
        if (line.empty() || line[0] == '#')
            continue;

        if (ParseConfigLine(line))
        {
            validEntries++;
        }
        else
        {
            DbgLog((LOG_WARNING, 1, TEXT("PPMCorrectionLoader: Invalid line %d in correction.cfg: %S"), 
                lineNumber, line.c_str()));
        }
    }

    configFile.close();

    DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: Loaded %d PPM corrections from correction.cfg"), validEntries));

    // Log all loaded corrections
    for (const auto& correction : m_corrections)
    {
        DbgLog((LOG_TRACE, 1, TEXT("PPMCorrectionLoader: %d Hz = %d PPM"), 
            correction.first, correction.second));
    }

    return validEntries > 0;
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

    // Also try exact integer match (e.g., 59.94 -> 60)
    int roundedRate = static_cast<int>(std::round(targetRate));
    auto it = m_corrections.find(roundedRate);
    if (it != m_corrections.end())
    {
        double roundDistance = std::abs(targetRate - roundedRate);
        if (roundDistance < bestDistance)
        {
            bestMatch = roundedRate;
        }
    }

    return bestMatch;
}