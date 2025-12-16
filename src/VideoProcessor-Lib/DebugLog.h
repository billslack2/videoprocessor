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


/**
 * Simple debug logger that writes to a local debug.log file
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
		std::lock_guard<std::mutex> lock(s_mutex);
		
		std::ofstream file("debug.log", std::ios::app);
		if (!file.is_open())
			return;

		// Get current time
		auto now = std::time(nullptr);
		auto tm = *std::localtime(&now);
		
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
		std::lock_guard<std::mutex> lock(s_mutex);
		std::ofstream file("debug.log", std::ios::trunc);
		file.close();
	}

private:
	static std::mutex s_mutex;
};

// Define static mutex in header (inline)
inline std::mutex DebugLog::s_mutex;


// Convenient macro for logging
#define DEBUGLOG(format, ...) DebugLog::Log(format, __VA_ARGS__)
#define DEBUGLOG_SIMPLE(msg) DebugLog::Log("%s", msg)
