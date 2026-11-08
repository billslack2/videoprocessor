// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>
#include "CalibrationPipe.h"

#include <DebugLog.h>

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "advapi32.lib")

namespace CalibrationPipe
{
	namespace
	{
		void Put32(std::vector<uint8_t>& out, uint32_t value)
		{
			const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
			out.insert(out.end(), p, p + 4);
		}

		uint32_t Get32(const uint8_t* bytes)
		{
			uint32_t value = 0;
			std::memcpy(&value, bytes, 4);
			return value;
		}

		template <typename T>
		void PutPod(std::vector<uint8_t>& out, const T& value)
		{
			Put32(out, static_cast<uint32_t>(sizeof(T)));
			const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
			out.insert(out.end(), p, p + sizeof(T));
		}

		// The size-prefixed raw bytes of a POD at `cursor`: exactly sizeof(T) and nothing after them.
		template <typename T>
		bool PodBytes(const std::vector<uint8_t>& in, size_t cursor, const uint8_t*& bytes)
		{
			if (in.size() < cursor + 4 || Get32(in.data() + cursor) != sizeof(T) || in.size() != cursor + 4 + sizeof(T))
				return false;
			bytes = in.data() + cursor + 4;
			return true;
		}

		bool BoolByte(const uint8_t* bytes, size_t offset)
		{
			return bytes[offset] == 0 || bytes[offset] == 1;
		}

		void PutString(std::vector<uint8_t>& out, const std::string& text)
		{
			const std::string bounded = text.substr(0, MaximumReasonBytes);
			Put32(out, static_cast<uint32_t>(bounded.size()));
			out.insert(out.end(), bounded.begin(), bounded.end());
		}

		bool GetString(const std::vector<uint8_t>& in, size_t& cursor, std::string& text)
		{
			if (in.size() < cursor + 4) return false;
			const uint32_t length = Get32(in.data() + cursor);
			if (length > MaximumReasonBytes || in.size() < cursor + 4 + length) return false;
			text.assign(reinterpret_cast<const char*>(in.data() + cursor + 4), length);
			cursor += 4 + length;
			return true;
		}

		Message Make(Op op)
		{
			Message message;
			message.op = static_cast<uint32_t>(op);
			return message;
		}

		bool ReplyError(const Message& message, std::string& error)
		{
			if (!(message.op & ErrorFlag)) return false;
			size_t cursor = 0;
			std::string reason;
			error = GetString(message.payload, cursor, reason) ? reason : std::string("the renderer refused the request");
			return true;
		}
	}

	std::wstring PipeNameForSession(uint32_t sessionId)
	{
		return L"\\\\.\\pipe\\VideoProcessor-Calibration-" + std::to_wstring(sessionId);
	}

	std::wstring DefaultPipeName()
	{
		DWORD session = 0;
		if (!ProcessIdToSessionId(GetCurrentProcessId(), &session))
			session = 0xFFFFFFFFu;   // never a real session: client and server then fail to meet, visibly
		return PipeNameForSession(session);
	}

	const char* EnableVariable() { return "VP_CALIBRATION_PIPE"; }

	namespace
	{
		// The user SID of a process's token, as raw bytes. False when the token cannot be read.
		bool ProcessUserSid(HANDLE process, std::vector<uint8_t>& sid)
		{
			HANDLE token = nullptr;
			if (!OpenProcessToken(process, TOKEN_QUERY, &token))
				return false;
			DWORD size = 0;
			GetTokenInformation(token, TokenUser, nullptr, 0, &size);
			std::vector<uint8_t> buffer(size > 0 && size < 4096 ? size : 0);
			bool ok = !buffer.empty() && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
			if (ok)
			{
				const PSID user = reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid;
				ok = IsValidSid(user) != FALSE;
				if (ok)
				{
					const uint8_t* bytes = static_cast<const uint8_t*>(user);
					sid.assign(bytes, bytes + GetLengthSid(user));
				}
			}
			CloseHandle(token);
			return ok;
		}

	}

	bool ThisProcessIsElevated()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			return false;
		TOKEN_ELEVATION elevation{};
		DWORD size = 0;
		const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
			elevation.TokenIsElevated != 0;
		CloseHandle(token);
		return elevated;
	}

	std::string ConnectErrorText(uint32_t status, bool clientElevated)
	{
		switch (status)
		{
		case ERROR_FILE_NOT_FOUND:
			return std::string("VideoProcessor's calibration pipe is not there: start VideoProcessor.exe with ") +
				EnableVariable() + "=1 in its environment and a source rendering with the VP Renderer (same Windows "
				"session as this tool)";
		case ERROR_ACCESS_DENIED:
			// The pipe admits only this user; with the same user, a refusal means VideoProcessor's pipe carries
			// the high-integrity label of an elevated process.
			return clientElevated ?
				std::string("the calibration pipe refused access: VideoProcessor runs as another user; run this tool "
					"as the same user as VideoProcessor") :
				std::string("VideoProcessor is running as administrator: run this tool as administrator too");
		case ERROR_PIPE_BUSY:
			return "the calibration pipe stayed busy";
		default:
			return "the calibration pipe could not be opened (" + std::to_string(status) + ")";
		}
	}

	SingleClientGuard::~SingleClientGuard()
	{
		if (mutex) CloseHandle(static_cast<HANDLE>(mutex));
	}

	bool SingleClientGuard::Acquire(const std::wstring& name, std::string& why)
	{
		if (mutex) return true;
		HANDLE handle = CreateMutexW(nullptr, FALSE, name.c_str());
		if (!handle)
		{
			why = "the single-instance lock could not be created (" + std::to_string(GetLastError()) + ")";
			return false;
		}
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			CloseHandle(handle);
			why = "another VideoProcessor-Calibrate is already running in this Windows session; finish or cancel it first";
			return false;
		}
		mutex = handle;
		return true;
	}

	std::wstring SingleClientGuard::DefaultName()
	{
		// Local\ is per session, like the pipe.
		return L"Local\\VideoProcessor-Calibrate";
	}

	bool PipeSecuritySddl(std::wstring& sddl, std::string& error)
	{
		std::vector<uint8_t> user;
		LPWSTR sid = nullptr;
		if (!ProcessUserSid(GetCurrentProcess(), user) || !ConvertSidToStringSidW(reinterpret_cast<PSID>(user.data()), &sid))
		{
			error = "the process token's user could not be read (" + std::to_string(GetLastError()) + ")";
			return false;
		}
		// D:P = protected DACL (no inherited entries); GA for the token user and LocalSystem only.
		sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")(A;;GA;;;SY)";
		LocalFree(sid);
		// An elevated VideoProcessor labels the pipe high integrity, no write up: a non-elevated process
		// of the same user cannot drive it.
		if (ThisProcessIsElevated())
			sddl += L"S:(ML;;NW;;;HI)";
		return true;
	}

	bool SameUserProcess(uint32_t processId, std::string& why)
	{
		std::vector<uint8_t> mine, theirs;
		if (!ProcessUserSid(GetCurrentProcess(), mine))
		{
			why = "this process's user could not be read";
			return false;
		}
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
		if (!process)
		{
			why = "the pipe server's process (" + std::to_string(processId) + ") cannot be inspected";
			return false;
		}
		const bool read = ProcessUserSid(process, theirs);
		CloseHandle(process);
		if (!read)
		{
			why = "the pipe server's user cannot be verified (run this tool with the same elevation as VideoProcessor)";
			return false;
		}
		if (mine.size() != theirs.size() || !EqualSid(reinterpret_cast<PSID>(mine.data()), reinterpret_cast<PSID>(theirs.data())))
		{
			why = "the pipe server (process " + std::to_string(processId) + ") runs as another user";
			return false;
		}
		return true;
	}

	// ---- field validation --------------------------------------------------------------------------

	bool ValidatePatternRequestBytes(const uint8_t* bytes, size_t size, Calibration::PatternRequest& request, std::string& why)
	{
		using Calibration::PatternRequest;
		if (!bytes || size != sizeof(PatternRequest)) { why = "PatternRequest has the wrong size"; return false; }
		if (!BoolByte(bytes, offsetof(PatternRequest, active))) { why = "PatternRequest.active is not a bool"; return false; }
		PatternRequest parsed;
		std::memcpy(&parsed, bytes, sizeof(parsed));
		for (int c = 0; c < 3; ++c)
			if (parsed.patch[c] > MaximumCode || parsed.background[c] > MaximumCode)
			{
				why = "a patch code is above 1023";
				return false;
			}
		if (!std::isfinite(parsed.windowAreaPercent) || !(parsed.windowAreaPercent > 0.0f) || parsed.windowAreaPercent > 100.0f)
		{
			why = "windowAreaPercent is outside (0, 100]";
			return false;
		}
		if ((parsed.flags & ~Calibration::PatternReadback) != 0) { why = "unknown PatternRequest flags"; return false; }
		request = parsed;
		return true;
	}

	bool ValidateOutputInfoBytes(const uint8_t* bytes, size_t size, Calibration::PatternOutputInfo& info, std::string& why)
	{
		using Calibration::PatternOutputInfo;
		if (!bytes || size != sizeof(PatternOutputInfo)) { why = "PatternOutputInfo has the wrong size"; return false; }
		if (!BoolByte(bytes, offsetof(PatternOutputInfo, available)) || !BoolByte(bytes, offsetof(PatternOutputInfo, limitedRange)) ||
			!BoolByte(bytes, offsetof(PatternOutputInfo, bt2020)) || !BoolByte(bytes, offsetof(PatternOutputInfo, toneMappingActive)))
		{
			why = "a PatternOutputInfo flag is not a bool";
			return false;
		}
		int32_t transfer = 0;
		std::memcpy(&transfer, bytes + offsetof(PatternOutputInfo, transfer), sizeof(transfer));
		if (transfer < 0 || transfer > 2) { why = "PatternOutputInfo.transfer is out of range"; return false; }
		PatternOutputInfo parsed;
		std::memcpy(&parsed, bytes, sizeof(parsed));
		parsed.reason[sizeof(parsed.reason) - 1] = '\0';
		parsed.toneFunction[sizeof(parsed.toneFunction) - 1] = '\0';
		const uint32_t container = parsed.containerBits;
		if (!(container == 0 || container == 8 || container == 10 || container == 16) || parsed.codeBits > container ||
			parsed.toneCurveCount > PatternOutputInfo::ToneCurveMax)
		{
			why = "PatternOutputInfo bit depths or tone-curve count are out of range";
			return false;
		}
		info = parsed;
		return true;
	}

	bool EnabledByEnvironment()
	{
		char value[8] = {};
		const DWORD length = GetEnvironmentVariableA(EnableVariable(), value, static_cast<DWORD>(sizeof(value)));
		return length == 1 && value[0] == '1';
	}

	// ---- wire format -------------------------------------------------------------------------------

	std::vector<uint8_t> Encode(const Message& message)
	{
		std::vector<uint8_t> out;
		out.reserve(HeaderBytes + message.payload.size());
		Put32(out, Magic);
		Put32(out, Version);
		Put32(out, message.op);
		Put32(out, static_cast<uint32_t>(message.payload.size()));
		out.insert(out.end(), message.payload.begin(), message.payload.end());
		return out;
	}

	bool Decode(const uint8_t* bytes, size_t size, Message& message)
	{
		if (!bytes || size < HeaderBytes || size > MaximumMessageBytes) return false;
		if (Get32(bytes) != Magic || Get32(bytes + 4) != Version) return false;
		const uint32_t payload = Get32(bytes + 12);
		if (static_cast<size_t>(payload) != size - HeaderBytes) return false;
		message.op = Get32(bytes + 8);
		message.payload.assign(bytes + HeaderBytes, bytes + size);
		return true;
	}

	std::vector<uint8_t> EncodeGetOutputRequest()
	{
		return Encode(Make(Op::GetOutput));
	}

	std::vector<uint8_t> EncodeSetPatternRequest(const Calibration::PatternRequest& request)
	{
		Message message = Make(Op::SetPattern);
		PutPod(message.payload, request);
		return Encode(message);
	}

	std::vector<uint8_t> EncodeOutputReply(bool ok, const Calibration::PatternOutputInfo& info)
	{
		Message message = Make(Op::GetOutput);
		Put32(message.payload, ok ? 1u : 0u);
		PutPod(message.payload, info);
		return Encode(message);
	}

	std::vector<uint8_t> EncodePatternReply(bool ok, const std::string& reason)
	{
		Message message = Make(Op::SetPattern);
		Put32(message.payload, ok ? 1u : 0u);
		PutString(message.payload, reason);
		return Encode(message);
	}

	std::vector<uint8_t> EncodeError(uint32_t op, const std::string& reason)
	{
		Message message;
		message.op = (op & ~ErrorFlag) | ErrorFlag;
		PutString(message.payload, reason);
		return Encode(message);
	}

	bool DecodeSetPatternRequest(const Message& message, Calibration::PatternRequest& request, std::string& why)
	{
		const uint8_t* bytes = nullptr;
		if (message.op != static_cast<uint32_t>(Op::SetPattern) ||
			!PodBytes<Calibration::PatternRequest>(message.payload, 0, bytes))
		{
			why = "malformed SetPattern request (VP and the calibration tool must come from the same build)";
			return false;
		}
		return ValidatePatternRequestBytes(bytes, sizeof(Calibration::PatternRequest), request, why);
	}

	bool DecodeOutputReply(const Message& message, bool& ok, Calibration::PatternOutputInfo& info, std::string& error)
	{
		if (ReplyError(message, error)) return false;
		const uint8_t* bytes = nullptr;
		if (message.op != static_cast<uint32_t>(Op::GetOutput) || message.payload.size() < 4 ||
			Get32(message.payload.data()) > 1 || !PodBytes<Calibration::PatternOutputInfo>(message.payload, 4, bytes))
		{
			error = "malformed GetOutput reply (VP and the calibration tool must come from the same build)";
			return false;
		}
		std::string why;
		if (!ValidateOutputInfoBytes(bytes, sizeof(Calibration::PatternOutputInfo), info, why))
		{
			error = "invalid GetOutput reply: " + why;
			return false;
		}
		ok = Get32(message.payload.data()) != 0;
		return true;
	}

	bool DecodePatternReply(const Message& message, bool& ok, std::string& reason, std::string& error)
	{
		if (ReplyError(message, error)) return false;
		size_t cursor = 4;
		if (message.op != static_cast<uint32_t>(Op::SetPattern) || message.payload.size() < 4 ||
			Get32(message.payload.data()) > 1 || !GetString(message.payload, cursor, reason) || cursor != message.payload.size())
		{
			error = "malformed SetPattern reply (VP and the calibration tool must come from the same build)";
			return false;
		}
		ok = Get32(message.payload.data()) != 0;
		return true;
	}

	std::vector<uint8_t> HandleRequest(const uint8_t* bytes, size_t size, const Handlers& handlers, bool& patternsActive)
	{
		Message request;
		if (!Decode(bytes, size, request))
			return EncodeError(0, "malformed request (VP and the calibration tool must come from the same build)");
		if (request.op == static_cast<uint32_t>(Op::GetOutput) && request.payload.empty())
		{
			Calibration::PatternOutputInfo info;
			const bool ok = handlers.getOutput ? handlers.getOutput(info) : false;
			if (!handlers.getOutput)
				strncpy_s(info.reason, sizeof(info.reason), "no renderer", _TRUNCATE);
			info.reason[sizeof(info.reason) - 1] = '\0';
			info.toneFunction[sizeof(info.toneFunction) - 1] = '\0';
			return EncodeOutputReply(ok, info);
		}
		if (request.op == static_cast<uint32_t>(Op::SetPattern))
		{
			Calibration::PatternRequest pattern;
			std::string why;
			if (!DecodeSetPatternRequest(request, pattern, why))
				return EncodeError(request.op, why);
			std::string reason;
			const bool ok = handlers.setPattern ? handlers.setPattern(pattern, reason) : false;
			if (!handlers.setPattern) reason = "no renderer";
			if (ok) patternsActive = pattern.active;
			return EncodePatternReply(ok, reason);
		}
		return EncodeError(request.op, "unknown calibration request");
	}

	// ---- server ------------------------------------------------------------------------------------

	Server::Server(std::wstring n, Handlers h, double idle) : name(std::move(n)), handlers(std::move(h)), idleSeconds(idle) {}

	Server::~Server()
	{
		Stop();
	}

	bool Server::Start(std::string& error)
	{
		if (thread.joinable()) return true;
		std::wstring sddl;
		if (!PipeSecuritySddl(sddl, error))
			return false;
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
		{
			error = "the pipe's security descriptor could not be built (" + std::to_string(GetLastError()) + ")";
			return false;
		}
		SECURITY_ATTRIBUTES attributes{};
		attributes.nLength = sizeof(attributes);
		attributes.lpSecurityDescriptor = descriptor;
		attributes.bInheritHandle = FALSE;
		HANDLE handle = CreateNamedPipeW(name.c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			1, MaximumMessageBytes, MaximumMessageBytes, 0, &attributes);
		LocalFree(descriptor);
		if (handle == INVALID_HANDLE_VALUE)
		{
			error = "CreateNamedPipe failed (" + std::to_string(GetLastError()) +
				"); is another VP renderer already serving calibration?";
			return false;
		}
		HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!stop)
		{
			CloseHandle(handle);
			error = "CreateEvent failed";
			return false;
		}
		pipe = handle;
		stopEvent = stop;
		patternsActive = false;
		thread = std::thread([this]() { Run(); });
		return true;
	}

	void Server::Stop()
	{
		if (!thread.joinable()) return;
		SetEvent(static_cast<HANDLE>(stopEvent));
		thread.join();
		CloseHandle(static_cast<HANDLE>(pipe));
		CloseHandle(static_cast<HANDLE>(stopEvent));
		pipe = nullptr;
		stopEvent = nullptr;
		// The owner of the patterns flag clears it (the renderer: Adapter::Retire); calling the
		// handler here could take the renderer's queue lock under its render lock.
		patternsActive = false;
	}

	void Server::EndAbandonedPatterns(const char* why)
	{
		if (!patternsActive) return;
		Calibration::PatternRequest off;
		off.active = false;
		std::string reason;
		if (handlers.setPattern) handlers.setPattern(off, reason);
		patternsActive = false;
		DebugLog::Log("Calibration pipe: patterns ended by the renderer (%s)", why);
	}

	void Server::Run()
	{
		HANDLE handle = static_cast<HANDLE>(pipe);
		HANDLE stop = static_cast<HANDLE>(stopEvent);
		HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!ioEvent) return;
		DebugLog::Log("Calibration pipe: listening (%s=1)", EnableVariable());
		using Clock = std::chrono::steady_clock;
		Clock::time_point lastRequest = Clock::now();

		// Waits for an overlapped operation: true = completed; false = stop requested or timed out
		// (the operation is cancelled).
		const auto waitIo = [&](OVERLAPPED& overlapped, DWORD timeoutMs, DWORD& bytes, bool& stopped) -> bool
		{
			HANDLE handles[2] = { ioEvent, stop };
			const DWORD result = WaitForMultipleObjects(2, handles, FALSE, timeoutMs);
			stopped = result == WAIT_OBJECT_0 + 1;
			if (result != WAIT_OBJECT_0)
			{
				CancelIoEx(handle, &overlapped);
				GetOverlappedResult(handle, &overlapped, &bytes, TRUE);
				return false;
			}
			return GetOverlappedResult(handle, &overlapped, &bytes, FALSE) != FALSE;
		};

		std::vector<uint8_t> buffer(MaximumMessageBytes);
		for (;;)
		{
			OVERLAPPED overlapped{};
			overlapped.hEvent = ioEvent;
			ResetEvent(ioEvent);
			bool connected = ConnectNamedPipe(handle, &overlapped) != FALSE;
			DWORD bytes = 0;
			bool stopped = false;
			if (!connected)
			{
				const DWORD status = GetLastError();
				if (status == ERROR_PIPE_CONNECTED)
					connected = true;
				else if (status == ERROR_IO_PENDING)
				{
					// Wake once a second for the abandoned-patterns check.
					connected = waitIo(overlapped, 1000, bytes, stopped);
					if (stopped) break;
				}
				else
				{
					if (WaitForSingleObject(stop, 100) == WAIT_OBJECT_0) break;
				}
			}
			if (!connected)
			{
				const double idle = std::chrono::duration<double>(Clock::now() - lastRequest).count();
				if (patternsActive && idle > idleSeconds)
					EndAbandonedPatterns("no calibration request for the idle limit; the client is gone");
				continue;
			}

			// One request, one reply, then wait for the client to close its end.
			ResetEvent(ioEvent);
			overlapped = OVERLAPPED{};
			overlapped.hEvent = ioEvent;
			bytes = 0;
			bool read = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, &overlapped) != FALSE;
			bool oversized = !read && GetLastError() == ERROR_MORE_DATA;
			if (!read && !oversized && GetLastError() == ERROR_IO_PENDING)
			{
				read = waitIo(overlapped, 2000, bytes, stopped);
				oversized = !read && !stopped && GetLastError() == ERROR_MORE_DATA;
			}
			if ((read && bytes > 0) || oversized)
			{
				// A message over MaximumMessageBytes is refused unread; only a well-formed request
				// counts as the client being alive.
				Message probe;
				if (read && Decode(buffer.data(), bytes, probe))
					lastRequest = Clock::now();
				const std::vector<uint8_t> reply = oversized ?
					EncodeError(0, "message too large") : HandleRequest(buffer.data(), bytes, handlers, patternsActive);
				ResetEvent(ioEvent);
				overlapped = OVERLAPPED{};
				overlapped.hEvent = ioEvent;
				DWORD written = 0;
				bool wrote = WriteFile(handle, reply.data(), static_cast<DWORD>(reply.size()), &written, &overlapped) != FALSE;
				if (!wrote && GetLastError() == ERROR_IO_PENDING)
					wrote = waitIo(overlapped, 2000, written, stopped);
				if (wrote && !stopped)
				{
					// The client reads the reply and closes: the read below ends with a broken pipe.
					ResetEvent(ioEvent);
					overlapped = OVERLAPPED{};
					overlapped.hEvent = ioEvent;
					uint8_t ignored[16];
					DWORD drained = 0;
					if (!ReadFile(handle, ignored, sizeof(ignored), &drained, &overlapped) && GetLastError() == ERROR_IO_PENDING)
						waitIo(overlapped, 2000, drained, stopped);
				}
			}
			DisconnectNamedPipe(handle);
			if (stopped) break;
		}
		CloseHandle(ioEvent);
		DebugLog::Log("Calibration pipe: stopped");
	}

	// ---- client ------------------------------------------------------------------------------------

	Client::Client(std::wstring n, uint32_t timeout) : name(std::move(n)), timeoutMs(timeout) {}

	bool Client::Call(const std::vector<uint8_t>& request, Message& reply, std::string& error) const
	{
		// Connect with identification-level impersonation only (the server cannot act as this user),
		// then verify that the server process runs as this user before sending anything.
		const ULONGLONG deadline = GetTickCount64() + timeoutMs;
		HANDLE pipe = INVALID_HANDLE_VALUE;
		for (;;)
		{
			pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
			if (pipe != INVALID_HANDLE_VALUE)
				break;
			const DWORD status = GetLastError();
			const ULONGLONG now = GetTickCount64();
			if (status == ERROR_PIPE_BUSY && now < deadline)
			{
				WaitNamedPipeW(name.c_str(), static_cast<DWORD>((std::min)(deadline - now, static_cast<ULONGLONG>(1000))));
				continue;
			}
			error = ConnectErrorText(status, ThisProcessIsElevated());
			return false;
		}
		struct HandleCloser { HANDLE handle; ~HandleCloser() { CloseHandle(handle); } } pipeCloser{ pipe };

		ULONG serverProcess = 0;
		std::string why;
		if (!GetNamedPipeServerProcessId(pipe, &serverProcess) || !SameUserProcess(serverProcess, why))
		{
			error = "refusing the calibration pipe: " + (why.empty() ? std::string("its server process is unknown") : why);
			return false;
		}
		DWORD mode = PIPE_READMODE_MESSAGE;
		if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
		{
			error = "the calibration pipe could not be set to message mode (" + std::to_string(GetLastError()) + ")";
			return false;
		}

		HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!done)
		{
			error = "CreateEvent failed";
			return false;
		}
		struct HandleCloser eventCloser{ done };
		std::vector<uint8_t> buffer(MaximumMessageBytes);
		OVERLAPPED overlapped{};
		overlapped.hEvent = done;
		DWORD read = 0;
		BOOL ok = TransactNamedPipe(pipe, const_cast<uint8_t*>(request.data()), static_cast<DWORD>(request.size()),
			buffer.data(), static_cast<DWORD>(buffer.size()), &read, &overlapped);
		if (!ok && GetLastError() == ERROR_IO_PENDING)
		{
			const ULONGLONG now = GetTickCount64();
			const DWORD wait = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
			if (WaitForSingleObject(done, (std::max)(wait, static_cast<DWORD>(500))) != WAIT_OBJECT_0)
			{
				CancelIoEx(pipe, &overlapped);
				GetOverlappedResult(pipe, &overlapped, &read, TRUE);
				error = "VideoProcessor did not answer on the calibration pipe in time";
				return false;
			}
			ok = GetOverlappedResult(pipe, &overlapped, &read, FALSE);
		}
		if (!ok)
		{
			const DWORD status = GetLastError();
			error = status == ERROR_MORE_DATA ? std::string("the reply from VideoProcessor is too large") :
				"the calibration pipe call failed (" + std::to_string(status) + ")";
			return false;
		}
		if (!Decode(buffer.data(), read, reply))
		{
			error = "malformed reply from VideoProcessor (VP and the calibration tool must come from the same build)";
			return false;
		}
		return true;
	}

	bool Client::GetOutput(Calibration::PatternOutputInfo& info, bool& ok, std::string& error) const
	{
		Message reply;
		return Call(EncodeGetOutputRequest(), reply, error) && DecodeOutputReply(reply, ok, info, error);
	}

	bool Client::SetPattern(const Calibration::PatternRequest& request, bool& ok, std::string& reason, std::string& error) const
	{
		Message reply;
		return Call(EncodeSetPatternRequest(request), reply, error) && DecodePatternReply(reply, ok, reason, error);
	}
}
