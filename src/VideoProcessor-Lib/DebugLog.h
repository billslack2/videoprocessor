/*
 * Copyright(C) 2025 Bill Slack <bslack@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include "DebugLogRetention.h"
#include "DebugLogAsync.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <mutex>
#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <cstdio>


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
		ExternalSink sink = GetExternalSink().load(std::memory_order_acquire);
		if (!sink && !GetLoggingEnabled().load(std::memory_order_acquire))
			return;
		// Preserve printf and plugin callback lifetimes: pointer arguments cannot
		// be deferred safely. Formatted output is capped at 4096 bytes; timestamp
		// formatting and every file operation belong to the writer thread.
		char buffer[4096];
		const int length = std::snprintf(buffer, sizeof(buffer), format, args...);
		buffer[sizeof(buffer) - 1] = '\0';
		if (length < 0) buffer[0] = '\0';
		if (sink)
		{
			sink(buffer);
			return;
		}
		QueueMessage(buffer, length >= static_cast<int>(sizeof(buffer)));
	}

	using Metrics = DebugLogAsync::Metrics;
	static Metrics GetMetrics() { return GetTransport().Snapshot(); }

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
		std::lock_guard<std::mutex> lock(GetLifecycleMutex());
		if (!GetWriterThread().joinable())
		{
			GetLoggingEnabled().store(false, std::memory_order_release);
			// Allocate/initialize bounded storage during startup, before producers
			// can observe logging enabled and before any session file I/O.
			auto& transport = GetTransport();
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
				return;

			transport.Open();
			GetLoggingEnabled().store(true, std::memory_order_release);
			try
			{
				GetWriterThread() = std::thread(WriterThreadProc);
			}
			catch (...)
			{
				GetLoggingEnabled().store(false, std::memory_order_release);
				transport.Close();
				throw;
			}
		}
	}

	/**
	 * Shutdown the async logger (call once at cleanup)
	 */
	static void Shutdown()
	{
		// Lifecycle callers may wait for disk completion. Producers never take
		// this mutex, and closing admission guarantees every accepted record is
		// drained before the writer exits.
		std::lock_guard<std::mutex> lock(GetLifecycleMutex());
		GetLoggingEnabled().store(false, std::memory_order_release);
		GetTransport().Close();
		if (GetWriterThread().joinable()) GetWriterThread().join();
	}

	// Extra telemetry artifacts are intentionally opt-in. Normal logging writes
	// only vp.log and its bounded archives.
	static bool IsEnhancedLoggingEnabled()
	{
		return GetEnhancedLoggingEnabled().load(std::memory_order_acquire);
	}

private:
	using Transport = DebugLogAsync::Transport<>;

	static void QueueMessage(const char* message, bool truncated)
	{
		if (!GetLoggingEnabled().load(std::memory_order_acquire)) return;
		GetTransport().TryPublish(message, std::time(nullptr), truncated);
	}

	struct FileSink
	{
		std::ofstream& file;
		void Write(const Transport::Record& record)
		{
			struct tm local{};
			localtime_s(&local, &record.timestamp);
			file << std::put_time(&local, "%Y-%m-%d %H:%M:%S") <<
				" | " << record.message << "\n";
		}
		void Flush() { file.flush(); }
		void Report(const Metrics& metrics)
		{
			Transport::Record record;
			record.timestamp = std::time(nullptr);
			std::snprintf(record.message, sizeof(record.message),
				"VP async log telemetry: enqueued=%llu processed=%llu dropped_full=%llu dropped_contended=%llu truncated=%llu queue=%llu high_water=%llu capacity=1000 flushes=%llu flush_total_us=%llu flush_peak_us=%llu overflow=drop_oldest contention=drop_newest",
				static_cast<unsigned long long>(metrics.enqueued),
				static_cast<unsigned long long>(metrics.processed),
				static_cast<unsigned long long>(metrics.droppedFull),
				static_cast<unsigned long long>(metrics.producerContentions),
				static_cast<unsigned long long>(metrics.truncated),
				static_cast<unsigned long long>(metrics.queueDepth),
				static_cast<unsigned long long>(metrics.highWater),
				static_cast<unsigned long long>(metrics.flushCount),
				static_cast<unsigned long long>(metrics.flushTotalUs),
				static_cast<unsigned long long>(metrics.flushPeakUs));
			Write(record);
		}
	};

	static void WriterThreadProc()
	{
		std::ofstream file(GetLogFilePath(), std::ios::app);
		if (!file.is_open())
		{
			GetLoggingEnabled().store(false, std::memory_order_release);
			GetTransport().Close();
			return;
		}
		FileSink sink{file};
		GetTransport().Run(sink);
		file.close(); // Also outside every producer bookkeeping section.
	}

	static Transport& GetTransport()
	{
		static Transport transport;
		return transport;
	}

	static std::mutex& GetLifecycleMutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	static std::thread& GetWriterThread()
	{
		static std::thread writer;
		return writer;
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
