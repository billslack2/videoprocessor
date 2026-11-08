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
// environment; without it nothing listens and nothing changes.
//
// Access (a pipe that can put patterns on screen is locked down):
//  - local clients only (PIPE_REJECT_REMOTE_CLIENTS);
//  - an explicit protected DACL: the user running VideoProcessor and SYSTEM only (no
//    administrators, no other users, no inherited entries);
//  - a per-session name, \\.\pipe\VideoProcessor-Calibration-<session id>, so a process in
//    another logon session does not even find it;
//  - one instance (FILE_FLAG_FIRST_PIPE_INSTANCE: nobody can pre-create the name to intercept it);
//  - an elevated VideoProcessor adds a high-integrity, no-write-up label;
//  - the client connects with identification-level impersonation only and refuses a server
//    process that does not run as its own user (GetNamedPipeServerProcessId, token user SID).
//
// One request per connection (TransactNamedPipe on the client). Both ends are built from the same
// headers for the same architecture, so the two POD structs cross as raw bytes; the header
// carries their sizes and a mismatch is refused. Every message is capped at MaximumMessageBytes
// and every field is validated before use (bool bytes 0 / 1, enum ranges, codes within 10 bits,
// a finite window share in (0, 100], known flags only, bounded strings); nothing is allocated
// from a length read off the pipe.
//
// Safety: if patches are on and no request arrives for idleSeconds (the client crashed or was
// killed), the server ends the patches itself and video returns. The CLI driver sends a
// GetOutput heartbeat every few seconds while it runs.
namespace CalibrationPipe
{
	constexpr uint32_t Magic = 0x4C435056u;   // "VPCL"
	constexpr uint32_t Version = 1;
	constexpr uint32_t HeaderBytes = 16;
	constexpr uint32_t MaximumMessageBytes = 4096;
	constexpr uint32_t MaximumReasonBytes = 255;
	constexpr uint32_t MaximumCode = 1023;      // the widest integer container (10-bit)
	constexpr uint32_t ErrorFlag = 0x8000u;
	static_assert(HeaderBytes + 8 + sizeof(Calibration::PatternOutputInfo) <= MaximumMessageBytes,
		"the GetOutput reply must fit one message");

	enum class Op : uint32_t
	{
		GetOutput = 1,
		SetPattern = 2,
	};

	// \\.\pipe\VideoProcessor-Calibration-<session id of this process>
	std::wstring DefaultPipeName();
	std::wstring PipeNameForSession(uint32_t sessionId);
	const char* EnableVariable();              // VP_CALIBRATION_PIPE
	bool EnabledByEnvironment();               // VP_CALIBRATION_PIPE is "1"

	// The pipe's security descriptor in SDDL: protected DACL, full access for the process's token
	// user and SYSTEM only; an elevated process adds S:(ML;;NW;;;HI). False with error when the
	// token cannot be read.
	bool PipeSecuritySddl(std::wstring& sddl, std::string& error);
	// The client's server check: true when process `processId` runs as this process's user.
	bool SameUserProcess(uint32_t processId, std::string& why);
	bool ThisProcessIsElevated();
	// The client's message for a failed connect (pure; CPU tests). Access denied for a non-elevated
	// client means VideoProcessor is elevated (its pipe has the high-integrity label): the message
	// says so explicitly, never a silent lockout.
	std::string ConnectErrorText(uint32_t status, bool clientElevated);

	// One calibration client per Windows session (a named mutex in Local\): a second
	// VideoProcessor-Calibrate refuses to start instead of interleaving patches with the first.
	class SingleClientGuard
	{
	public:
		SingleClientGuard() = default;
		~SingleClientGuard();
		SingleClientGuard(const SingleClientGuard&) = delete;
		SingleClientGuard& operator=(const SingleClientGuard&) = delete;
		bool Acquire(const std::wstring& name, std::string& why);
		static std::wstring DefaultName();

	private:
		void* mutex = nullptr;
	};

	// ---- field validation (pure; CPU tests) -----------------------------------------------------
	// Raw bytes of a PatternRequest as they came off the pipe: exact size, bool byte 0 / 1, codes
	// <= MaximumCode, a finite window share in (0, 100], known flags only.
	bool ValidatePatternRequestBytes(const uint8_t* bytes, size_t size, Calibration::PatternRequest& request, std::string& why);
	// Raw bytes of a PatternOutputInfo (the client side): exact size, bool bytes, the transfer enum,
	// container / code bits, the tone-curve count; strings are terminated.
	bool ValidateOutputInfoBytes(const uint8_t* bytes, size_t size, Calibration::PatternOutputInfo& info, std::string& why);

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
	bool DecodeSetPatternRequest(const Message& message, Calibration::PatternRequest& request, std::string& why);
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
