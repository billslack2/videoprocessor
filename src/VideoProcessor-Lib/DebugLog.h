/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <mutex>
#include <Windows.h>
#include <string>


/**
 * Simple debug logger that writes to debug.log in the executable directory
 * Thread-safe file logging with timestamps
 */
class DebugLog
{
public:
	/**
	 * Log a message to debug.log with timestamp
	 * Thread-safe
	 */
	template<typename... Args>
	static void Log(const char* format, Args... args)
	{
		std::lock_guard<std::mutex> lock(GetMutex());
		
		std::string logPath = GetLogFilePath();
		std::ofstream file(logPath, std::ios::app);
		if (!file.is_open())
			return;

		// Get current time
		auto now = std::time(nullptr);
		struct tm tm;
		localtime_s(&tm, &now);
		
		// Write timestamp
		file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | ";
		
		// Write formatted message
		char buffer[4096];
		snprintf(buffer, sizeof(buffer), format, args...);
		file << buffer << "\n";
		
		file.close();
	}

	/**
	 * Clear the debug log file
	 */
	static void Clear()
	{
		std::lock_guard<std::mutex> lock(GetMutex());
		std::string logPath = GetLogFilePath();
		std::ofstream file(logPath, std::ios::trunc);
		file.close();
	}

	/**
	 * Get the full path to the log file for reference
	 */
	static std::string GetLogFilePath()
	{
		static std::string cachedPath;
		if (!cachedPath.empty())
			return cachedPath;

		// Get the executable path
		char exePath[MAX_PATH];
		DWORD result = GetModuleFileNameA(NULL, exePath, MAX_PATH);
		if (result == 0)
		{
			// Fallback to current directory
			cachedPath = "debug.log";
			return cachedPath;
		}

		// Get the directory containing the executable
		std::string exePathStr(exePath);
		size_t lastSlash = exePathStr.find_last_of("\\/");
		if (lastSlash != std::string::npos)
		{
			cachedPath = exePathStr.substr(0, lastSlash + 1) + "debug.log";
		}
		else
		{
			cachedPath = "debug.log";
		}

		return cachedPath;
	}

private:
	static std::mutex& GetMutex()
	{
		static std::mutex s_mutex;
		return s_mutex;
	}
};


// Convenient macro for logging
#define DEBUGLOG(format, ...) DebugLog::Log(format, __VA_ARGS__)
#define DEBUGLOG_SIMPLE(msg) DebugLog::Log("%s", msg)
#define DEBUGLOG_PATH() DebugLog::GetLogFilePath()
