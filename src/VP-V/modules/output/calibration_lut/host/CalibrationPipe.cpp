// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>
#include "CalibrationPipe.h"

#include <DebugLog.h>

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <cstring>

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

		template <typename T>
		bool GetPod(const std::vector<uint8_t>& in, size_t& cursor, T& value)
		{
			if (in.size() < cursor + 4 || Get32(in.data() + cursor) != sizeof(T) || in.size() < cursor + 4 + sizeof(T))
				return false;
			std::memcpy(&value, in.data() + cursor + 4, sizeof(T));
			cursor += 4 + sizeof(T);
			return true;
		}

		void PutString(std::vector<uint8_t>& out, const std::string& text)
		{
			const std::string bounded = text.substr(0, 1024);
			Put32(out, static_cast<uint32_t>(bounded.size()));
			out.insert(out.end(), bounded.begin(), bounded.end());
		}

		bool GetString(const std::vector<uint8_t>& in, size_t& cursor, std::string& text)
		{
			if (in.size() < cursor + 4) return false;
			const uint32_t length = Get32(in.data() + cursor);
			if (length > 1024 || in.size() < cursor + 4 + length) return false;
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

	const wchar_t* DefaultPipeName() { return L"\\\\.\\pipe\\VideoProcessor-Calibration"; }
	const char* EnableVariable() { return "VP_CALIBRATION_PIPE"; }

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

	bool DecodeSetPatternRequest(const Message& message, Calibration::PatternRequest& request)
	{
		size_t cursor = 0;
		return message.op == static_cast<uint32_t>(Op::SetPattern) && GetPod(message.payload, cursor, request) &&
			cursor == message.payload.size();
	}

	bool DecodeOutputReply(const Message& message, bool& ok, Calibration::PatternOutputInfo& info, std::string& error)
	{
		if (ReplyError(message, error)) return false;
		size_t cursor = 4;
		if (message.op != static_cast<uint32_t>(Op::GetOutput) || message.payload.size() < 4 ||
			!GetPod(message.payload, cursor, info) || cursor != message.payload.size())
		{
			error = "malformed GetOutput reply (VP and the calibration tool must come from the same build)";
			return false;
		}
		info.reason[sizeof(info.reason) - 1] = '\0';
		info.toneFunction[sizeof(info.toneFunction) - 1] = '\0';
		ok = Get32(message.payload.data()) != 0;
		return true;
	}

	bool DecodePatternReply(const Message& message, bool& ok, std::string& reason, std::string& error)
	{
		if (ReplyError(message, error)) return false;
		size_t cursor = 4;
		if (message.op != static_cast<uint32_t>(Op::SetPattern) || message.payload.size() < 4 ||
			!GetString(message.payload, cursor, reason) || cursor != message.payload.size())
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
			return EncodeOutputReply(ok, info);
		}
		if (request.op == static_cast<uint32_t>(Op::SetPattern))
		{
			Calibration::PatternRequest pattern;
			if (!DecodeSetPatternRequest(request, pattern))
				return EncodeError(request.op, "malformed SetPattern request (VP and the calibration tool must come from the same build)");
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
		HANDLE handle = CreateNamedPipeW(name.c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			1, MaximumMessageBytes, MaximumMessageBytes, 0, nullptr);
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
			if (!read && GetLastError() == ERROR_IO_PENDING)
				read = waitIo(overlapped, 2000, bytes, stopped);
			if (read && bytes > 0)
			{
				lastRequest = Clock::now();
				const std::vector<uint8_t> reply = HandleRequest(buffer.data(), bytes, handlers, patternsActive);
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
		std::vector<uint8_t> buffer(MaximumMessageBytes);
		DWORD read = 0;
		if (!CallNamedPipeW(name.c_str(), const_cast<uint8_t*>(request.data()), static_cast<DWORD>(request.size()),
			buffer.data(), static_cast<DWORD>(buffer.size()), &read, timeoutMs))
		{
			const DWORD status = GetLastError();
			error = status == ERROR_FILE_NOT_FOUND ?
				std::string("VideoProcessor's calibration pipe is not there: start VideoProcessor.exe with ") +
					EnableVariable() + "=1 in its environment and a source rendering with the VP Renderer" :
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
