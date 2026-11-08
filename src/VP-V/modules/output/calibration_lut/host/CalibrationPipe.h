// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "../pattern_lib/CalibrationPattern.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The calibration contract between a calibration session (another process, e.g.
// VideoProcessor-Calibrate.exe) and the running VP renderer, over a local named pipe:
//
//   GetOutput(PatternOutputInfo&)                -> what the output is; presented generation counters
//   SetPattern(const PatternRequest&, reason&)   -> show a patch (active) or return to video
//
// The renderer DLL runs the server only when VP_CALIBRATION_PIPE=1 is set in VideoProcessor.exe's
// environment; without it nothing listens and nothing changes. The pipe accepts local clients
// only (PIPE_REJECT_REMOTE_CLIENTS) with the default pipe security (the same user, administrators
// and SYSTEM can write to it).
//
// One request per connection (CallNamedPipe on the client). Both ends are built from the same
// headers for the same architecture, so the two POD structs cross as raw bytes; the header
// carries their sizes and a mismatch is refused.
//
// Safety: if patches are on and no request arrives for idleSeconds (the client crashed or was
// killed), the server ends the patches itself and video returns. The CLI driver sends a
// GetOutput heartbeat every few seconds while it runs.
namespace CalibrationPipe
{
	constexpr uint32_t Magic = 0x4C435056u;   // "VPCL"
	constexpr uint32_t Version = 1;
	constexpr uint32_t HeaderBytes = 16;
	constexpr uint32_t MaximumMessageBytes = 16 * 1024;
	constexpr uint32_t ErrorFlag = 0x8000u;

	enum class Op : uint32_t
	{
		GetOutput = 1,
		SetPattern = 2,
	};

	const wchar_t* DefaultPipeName();          // \\.\pipe\VideoProcessor-Calibration
	const char* EnableVariable();              // VP_CALIBRATION_PIPE
	bool EnabledByEnvironment();               // VP_CALIBRATION_PIPE is "1"

	// ---- wire format (pure; CPU tests) ---------------------------------------------------------
	struct Message
	{
		uint32_t op = 0;                       // Op, | ErrorFlag on an error reply
		std::vector<uint8_t> payload;
	};
	std::vector<uint8_t> Encode(const Message& message);
	bool Decode(const uint8_t* bytes, size_t size, Message& message);

	std::vector<uint8_t> EncodeGetOutputRequest();
	std::vector<uint8_t> EncodeSetPatternRequest(const Calibration::PatternRequest& request);
	std::vector<uint8_t> EncodeOutputReply(bool ok, const Calibration::PatternOutputInfo& info);
	std::vector<uint8_t> EncodePatternReply(bool ok, const std::string& reason);
	std::vector<uint8_t> EncodeError(uint32_t op, const std::string& reason);
	bool DecodeSetPatternRequest(const Message& message, Calibration::PatternRequest& request);
	bool DecodeOutputReply(const Message& message, bool& ok, Calibration::PatternOutputInfo& info, std::string& error);
	bool DecodePatternReply(const Message& message, bool& ok, std::string& reason, std::string& error);

	// The server's answer to one request (pure apart from the handlers; CPU tests).
	struct Handlers
	{
		std::function<bool(Calibration::PatternOutputInfo&)> getOutput;
		std::function<bool(const Calibration::PatternRequest&, std::string&)> setPattern;
	};
	// patternsActive: set to the request's active flag after a successful SetPattern.
	std::vector<uint8_t> HandleRequest(const uint8_t* bytes, size_t size, const Handlers& handlers, bool& patternsActive);

	// ---- server (renderer side) ----------------------------------------------------------------
	class Server
	{
	public:
		Server(std::wstring name, Handlers handlers, double idleSeconds = 30.0);
		~Server();
		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		bool Start(std::string& error);
		void Stop();   // joins the thread (the owner of the patterns flag clears it)
		bool Running() const { return thread.joinable(); }

	private:
		void Run();
		void EndAbandonedPatterns(const char* why);

		std::wstring name;
		Handlers handlers;
		double idleSeconds;
		void* stopEvent = nullptr;
		void* pipe = nullptr;
		std::thread thread;
		bool patternsActive = false;   // server thread only (and Stop after the join)
	};

	// ---- client (session side) -----------------------------------------------------------------
	class Client
	{
	public:
		explicit Client(std::wstring name = DefaultPipeName(), uint32_t timeoutMs = 3000);
		// False with error when the pipe is not there or the reply is malformed; otherwise the
		// renderer's own answer (ok) and reason.
		bool GetOutput(Calibration::PatternOutputInfo& info, bool& ok, std::string& error) const;
		bool SetPattern(const Calibration::PatternRequest& request, bool& ok, std::string& reason, std::string& error) const;

	private:
		bool Call(const std::vector<uint8_t>& request, Message& reply, std::string& error) const;
		std::wstring name;
		uint32_t timeoutMs;
	};
}
