/*
 * Copyright(C) 2025 Bill Slack <bslack@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include "DebugLogRetention.h"

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
 * Async VP logger that writes to logs\vp.log beneath the executable
 * directory and creates that directory on first run
 * Thread-safe file logging with timestamps - non-blocking for caller
 * 
 * OPTIMIZATION: Keeps file open in writer thread and batches writes to reduce I/O overhead
 * BEHAVIOR: Archives the prior session and prunes only matching VP logs
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
	 * Log a message to vp.log with timestamp (async, non-blocking)
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
		return GetSelectedLogPath();
	}

	static std::string GetExecutableLogFilePath()
	{
		char exePath[MAX_PATH];
		DWORD result = GetModuleFileNameA(NULL, exePath, MAX_PATH);
		if (result == 0)
			return "logs\\vp.log";

		std::string exePathStr(exePath);
		size_t lastSlash = exePathStr.find_last_of("\\/");
		if (lastSlash != std::string::npos)
			return exePathStr.substr(0, lastSlash + 1) +
				"logs\\vp.log";
		return "logs\\vp.log";
	}

	/**
	 * Initialize the async logger (call once at startup)
	 * This archives the prior session and applies the startup-only retention
	 * count before producer threads can enqueue normal diagnostics.
	 */
	static void Initialize(
		size_t retentionCount = DebugLogRetention::DEFAULT_COUNT,
		const std::string& retentionDiagnostic =
			"VP log retention: using default 10 total files",
		bool enhancedLogging = false)
	{
		std::lock_guard<std::mutex> lock(GetQueueMutex());
		if (!GetWriterThread().joinable())
		{
			GetEnhancedLoggingEnabled().store(
				enhancedLogging, std::memory_order_release);
			std::vector<std::string> initializationDiagnostics;
			const std::string logPath = GetExecutableLogFilePath();
			GetSelectedLogPath() = logPath;
			bool activeReady = false;
			const bool logDirectoryReady =
				DebugLogRetention::EnsureParentDirectory(
					logPath, initializationDiagnostics);
			if (logDirectoryReady && enhancedLogging)
			{
				// Enhanced mode is intentionally forensic: leave the current log
				// and every previous trace artifact intact until the user cleans up.
				std::ofstream active(logPath, std::ios::out | std::ios::app);
				activeReady = active.is_open();
				if (activeReady)
					active.close();
				else
					initializationDiagnostics.push_back(
						"Enhanced logging: cannot open active log '" + logPath + "'");
			}
			else if (logDirectoryReady)
			{
				auto rotation = DebugLogRetention::Rotate(logPath, retentionCount);
				activeReady = rotation.activeReady;
				initializationDiagnostics.insert(
					initializationDiagnostics.end(),
					rotation.diagnostics.begin(), rotation.diagnostics.end());
			}
			if (!activeReady)
			{
				// Never redirect diagnostics into an unrelated working directory.
				GetLoggingEnabled().store(false, std::memory_order_release);
				return;
			}
			GetLoggingEnabled().store(true, std::memory_order_release);

			std::ofstream file(logPath, std::ios::app);
			if (file.is_open())
			{
				struct tm tm;
				auto now = std::time(nullptr);
				localtime_s(&tm, &now);
				file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | === VP LOG SESSION START ===\n";
				file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") <<
					" | " << retentionDiagnostic << "\n";
				if (enhancedLogging)
					file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") <<
						" | Enhanced logging enabled: rotation disabled; live telemetry files enabled\n";
				else
					file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") <<
						" | VP log rotation: indexed archives use vp.log.0 (newest), vp.log.1, and so on\n";
				for (const std::string& diagnostic : initializationDiagnostics)
					file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") <<
						" | " << diagnostic << "\n";
				file.close();
			}
			else
				GetLoggingEnabled().store(false, std::memory_order_release);

			// Clear any pending messages from previous run
			std::queue<LogMessage> emptyQueue;
			std::swap(GetMessageQueue(), emptyQueue);

			if (GetLoggingEnabled().load(std::memory_order_acquire))
			{
				GetShutdownFlag() = false;
				GetWriterThread() = std::thread(WriterThreadProc);
			}
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

	// Extra telemetry artifacts are intentionally opt-in. Normal logging writes
	// only vp.log and its bounded archives.
	static bool IsEnhancedLoggingEnabled()
	{
		return GetEnhancedLoggingEnabled().load(std::memory_order_acquire);
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
		if (!GetLoggingEnabled().load(std::memory_order_acquire))
			return;

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

	static std::string& GetSelectedLogPath()
	{
		static std::string s_selectedLogPath = GetExecutableLogFilePath();
		return s_selectedLogPath;
	}

	static std::atomic<bool>& GetLoggingEnabled()
	{
		static std::atomic<bool> s_loggingEnabled(false);
		return s_loggingEnabled;
	}

	static std::atomic<bool>& GetEnhancedLoggingEnabled()
	{
		static std::atomic<bool> s_enhancedLoggingEnabled(false);
		return s_enhancedLoggingEnabled;
	}
};


// Convenient macros for logging
#define DEBUGLOG(format, ...) DebugLog::Log(format, __VA_ARGS__)
#define DEBUGLOG_SIMPLE(msg) DebugLog::Log("%s", msg)
#define DEBUGLOG_PATH() DebugLog::GetLogFilePath()
#define DEBUGLOG_INIT(...) DebugLog::Initialize(__VA_ARGS__)
#define DEBUGLOG_SHUTDOWN() DebugLog::Shutdown()
