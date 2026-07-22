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
 * 
 * OPTIMIZATION: Keeps file open in writer thread and batches writes to reduce I/O overhead
 * BEHAVIOR: Clears log file on Initialize() call (each restart)
 */
class DebugLog
{
public:
	using ExternalSink = void (__cdecl *)(const char* message);

	// A renderer plugin cannot share this header-only logger's module-local
	// queue. Let it forward complete messages to the host process instead.
	static void SetExternalSink(ExternalSink sink)
	{
		GetExternalSink().store(sink, std::memory_order_release);
	}

	/**
	 * Log a message to debug.log with timestamp (async, non-blocking)
	 * Thread-safe
	 */
	template<typename... Args>
	static void Log(const char* format, Args... args)
	{
			
		if (true) {  // Changed from false to enable logging during HDMI resync debugging
			// Format message immediately (fast operation)
			char buffer[4096];
			snprintf(buffer, sizeof(buffer), format, args...);
			ExternalSink sink = GetExternalSink().load(std::memory_order_acquire);
			if (sink)
			{
				sink(buffer);
				return;
			}

			// Queue for background writing (non-blocking)
			QueueMessage(buffer);
		}
	}

	/**
	 * Get the full path to the log file for reference
	 */
	static std::string GetLogFilePath()
	{

		if (true) {
			return "c:/logs/vp_debug.log";

		}
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
	 * This also clears the log file from any previous session
	 */
	static void Initialize()
	{
		std::lock_guard<std::mutex> lock(GetQueueMutex());
		if (!GetWriterThread().joinable())
		{
			// Clear log file (fresh start for this session)
			std::string logPath = GetLogFilePath();
			std::ofstream file(logPath, std::ios::trunc);
			if (file.is_open())
			{
				// Write session start marker
				struct tm tm;
				auto now = std::time(nullptr);
				localtime_s(&tm, &now);
				file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | === DEBUG LOG SESSION START ===\n";
				file.close();
			}

			// Clear any pending messages from previous run
			std::queue<LogMessage> emptyQueue;
			std::swap(GetMessageQueue(), emptyQueue);

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

	// Batch configuration
	static constexpr size_t BATCH_SIZE = 50;              // Flush after 50 messages
	static constexpr int FLUSH_INTERVAL_MS = 100;         // Or every 100ms, whichever comes first

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
		
		// Open file once for the entire session (append mode after initialization truncate)
		std::ofstream file(logPath, std::ios::app);
		if (!file.is_open())
		{
			// Can't write to log, just return
			return;
		}

		auto lastFlushTime = std::chrono::steady_clock::now();
		size_t messagesSinceFlush = 0;

		while (true)
		{
			std::unique_lock<std::mutex> lock(GetQueueMutex());
			
			// Wait for messages or shutdown signal with timeout for periodic flushing
			auto waitResult = GetConditionVariable().wait_for(
				lock,
				std::chrono::milliseconds(FLUSH_INTERVAL_MS),
				[]() {
					return !GetMessageQueue().empty() || GetShutdownFlag();
				}
			);

			// Process all pending messages
			while (!GetMessageQueue().empty())
			{
				LogMessage msg = GetMessageQueue().front();
				GetMessageQueue().pop();
				
				// Release lock while writing to file
				lock.unlock();
				
				// Write to file (file is already open)
				if (file.is_open())
				{
					struct tm tm;
					localtime_s(&tm, &msg.timestamp);
					
					file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | " << msg.message << "\n";
					messagesSinceFlush++;

					// Flush periodically to ensure data is written
					if (messagesSinceFlush >= BATCH_SIZE)
					{
						file.flush();
						messagesSinceFlush = 0;
						lastFlushTime = std::chrono::steady_clock::now();
					}
				}
				
				// Re-acquire lock for next iteration
				lock.lock();
			}

			// Flush if timeout occurred (periodic flush interval)
			auto now = std::chrono::steady_clock::now();
			if (file.is_open() && messagesSinceFlush > 0 &&
				std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlushTime).count() >= FLUSH_INTERVAL_MS)
			{
				file.flush();
				messagesSinceFlush = 0;
				lastFlushTime = now;
			}

			// Exit if shutdown requested and queue is empty
			if (GetShutdownFlag() && GetMessageQueue().empty())
			{
				// Final flush and close
				if (file.is_open())
				{
					file.flush();
					file.close();
				}
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

	static std::atomic<ExternalSink>& GetExternalSink()
	{
		static std::atomic<ExternalSink> s_externalSink(nullptr);
		return s_externalSink;
	}
};


// Convenient macros for logging
#define DEBUGLOG(format, ...) DebugLog::Log(format, __VA_ARGS__)
#define DEBUGLOG_SIMPLE(msg) DebugLog::Log("%s", msg)
#define DEBUGLOG_PATH() DebugLog::GetLogFilePath()
#define DEBUGLOG_INIT() DebugLog::Initialize()
#define DEBUGLOG_SHUTDOWN() DebugLog::Shutdown()
