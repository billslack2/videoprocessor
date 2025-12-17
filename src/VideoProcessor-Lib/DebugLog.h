/*
 * Copyright(C) 2025 Bill Slack <bslack@gmail.com>
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
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>


/**
 * Async debug logger that writes to debug.log in the executable directory
 * Thread-safe file logging with timestamps - non-blocking for caller
 */
class DebugLog
{
public:
	/**
	 * Log a message to debug.log with timestamp (async, non-blocking)
	 * Thread-safe
	 */
	template<typename... Args>
	static void Log(const char* format, Args... args)
	{
		// Format message immediately (fast operation)
		char buffer[4096];
		snprintf(buffer, sizeof(buffer), format, args...);
		
		// Queue for background writing (non-blocking)
		QueueMessage(buffer);
	}

	/**
	 * Clear the debug log file (synchronous operation)
	 */
	static void Clear()
	{
		std::lock_guard<std::mutex> lock(GetQueueMutex());
		
		// Clear any pending messages
		std::queue<LogMessage> emptyQueue;
		std::swap(GetMessageQueue(), emptyQueue);
		
		// Clear the file
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

	/**
	 * Initialize the async logger (call once at startup)
	 */
	static void Initialize()
	{
		std::lock_guard<std::mutex> lock(GetQueueMutex());
		if (!GetWriterThread().joinable())
		{
			GetShutdownFlag() = false;
			GetWriterThread() = std::thread(WriterThreadProc);
		}
	}

	/**
	 * Shutdown the async logger (call once at cleanup)
	 */
	static void Shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(GetQueueMutex());
			GetShutdownFlag() = true;
		}
		GetConditionVariable().notify_one();

		if (GetWriterThread().joinable())
		{
			GetWriterThread().join();
		}
	}

private:
	struct LogMessage
	{
		std::string message;
		std::time_t timestamp;
		
		LogMessage(const std::string& msg) : message(msg), timestamp(std::time(nullptr)) {}
	};

	static void QueueMessage(const std::string& message)
	{
		{
			std::lock_guard<std::mutex> lock(GetQueueMutex());
			
			// Prevent queue from growing too large (drop oldest messages)
			if (GetMessageQueue().size() >= 1000)
			{
				GetMessageQueue().pop();
			}
			
			GetMessageQueue().emplace(message);
		}
		GetConditionVariable().notify_one();
	}

	static void WriterThreadProc()
	{
		std::string logPath = GetLogFilePath();
		
		while (true)
		{
			std::unique_lock<std::mutex> lock(GetQueueMutex());
			
			// Wait for messages or shutdown signal
			GetConditionVariable().wait(lock, []() {
				return !GetMessageQueue().empty() || GetShutdownFlag();
			});

			// Process all pending messages
			while (!GetMessageQueue().empty())
			{
				LogMessage msg = GetMessageQueue().front();
				GetMessageQueue().pop();
				
				// Release lock while writing to file
				lock.unlock();
				
				// Write to file
				std::ofstream file(logPath, std::ios::app);
				if (file.is_open())
				{
					struct tm tm;
					localtime_s(&tm, &msg.timestamp);
					
					file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | " << msg.message << "\n";
					file.close();
				}
				
				// Re-acquire lock for next iteration
				lock.lock();
			}

			// Exit if shutdown requested and queue is empty
			if (GetShutdownFlag() && GetMessageQueue().empty())
			{
				break;
			}
		}
	}

	// Static member accessors (function-local statics for initialization safety)
	static std::mutex& GetQueueMutex()
	{
		static std::mutex s_queueMutex;
		return s_queueMutex;
	}

	static std::queue<LogMessage>& GetMessageQueue()
	{
		static std::queue<LogMessage> s_messageQueue;
		return s_messageQueue;
	}

	static std::condition_variable& GetConditionVariable()
	{
		static std::condition_variable s_condVar;
		return s_condVar;
	}

	static std::thread& GetWriterThread()
	{
		static std::thread s_writerThread;
		return s_writerThread;
	}

	static std::atomic<bool>& GetShutdownFlag()
	{
		static std::atomic<bool> s_shutdownFlag(false);
		return s_shutdownFlag;
	}
};


// Convenient macros for logging
#define DEBUGLOG(format, ...) DebugLog::Log(format, __VA_ARGS__)
#define DEBUGLOG_SIMPLE(msg) DebugLog::Log("%s", msg)
#define DEBUGLOG_PATH() DebugLog::GetLogFilePath()
#define DEBUGLOG_INIT() DebugLog::Initialize()
#define DEBUGLOG_SHUTDOWN() DebugLog::Shutdown()
