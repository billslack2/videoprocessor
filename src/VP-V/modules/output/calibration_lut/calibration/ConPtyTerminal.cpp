// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "Terminal.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// The pseudo-console API is loaded at run time (kernel32, Windows 10 1809+),
// so VP still starts on an older Windows and the wizard explains instead.
namespace
{
	typedef void* PseudoConsoleHandle;
	typedef HRESULT (WINAPI *CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, PseudoConsoleHandle*);
	typedef void (WINAPI *ClosePseudoConsoleFn)(PseudoConsoleHandle);

	constexpr DWORD_PTR kPseudoConsoleAttribute = 0x00020016;   // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE

	struct ConPtyApi
	{
		CreatePseudoConsoleFn create = nullptr;
		ClosePseudoConsoleFn close = nullptr;
	};

	const ConPtyApi& Api()
	{
		static const ConPtyApi api = []
		{
			ConPtyApi a;
			if (HMODULE kernel = GetModuleHandleW(L"kernel32.dll"))
			{
				a.create = reinterpret_cast<CreatePseudoConsoleFn>(GetProcAddress(kernel, "CreatePseudoConsole"));
				a.close = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
			}
			return a;
		}();
		return api;
	}

	std::string LastErrorText(const char* what)
	{
		const DWORD code = GetLastError();
		char buffer[256] = {};
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buffer,
			static_cast<DWORD>(sizeof(buffer) - 1), nullptr);
		std::string text(buffer);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
		return std::string(what) + " failed (" + std::to_string(code) + (text.empty() ? "" : ": " + text) + ")";
	}

	HANDLE CreateKillOnCloseJob()
	{
		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		if (!job) return nullptr;
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
		{
			CloseHandle(job);
			return nullptr;
		}
		return job;
	}

	class ConPtyTerminal final : public Calibration::ITerminal
	{
	public:
		ConPtyTerminal(short columns, short rows) : size{ columns, rows } {}
		~ConPtyTerminal() override { Shutdown(); }

		bool Start(const std::wstring& program, const std::wstring& commandLine,
			const std::wstring& workingDirectory, std::string& error) override
		{
			if (process) { error = "the terminal already runs a program"; return false; }
			const ConPtyApi& api = Api();
			if (!api.create || !api.close)
			{
				error = "this Windows has no pseudo-console (Windows 10 version 1809 or newer is needed)";
				return false;
			}
			HANDLE inputRead = nullptr, outputWrite = nullptr;
			if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0)) { error = LastErrorText("CreatePipe"); return false; }
			if (!CreatePipe(&outputRead, &outputWrite, nullptr, 0))
			{
				error = LastErrorText("CreatePipe");
				CloseHandle(inputRead);
				Shutdown();
				return false;
			}
			const HRESULT created = api.create(size, inputRead, outputWrite, 0, &console);
			// The pseudo-console holds its own copies.
			CloseHandle(inputRead);
			CloseHandle(outputWrite);
			if (FAILED(created))
			{
				error = "CreatePseudoConsole failed (0x" + std::to_string(static_cast<unsigned long>(created)) + ")";
				console = nullptr;
				Shutdown();
				return false;
			}
			reader = std::thread([this] { ReaderLoop(); });

			SIZE_T attributeBytes = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
			std::vector<unsigned char> attributeStorage(attributeBytes);
			auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
			if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes) ||
				!UpdateProcThreadAttribute(attributes, 0, kPseudoConsoleAttribute, console, sizeof(console), nullptr, nullptr))
			{
				error = LastErrorText("UpdateProcThreadAttribute");
				Shutdown();
				return false;
			}
			STARTUPINFOEXW startup{};
			startup.StartupInfo.cb = sizeof(startup);
			// Null standard handles: without this a parent whose own stdio is
			// redirected (a service, a test host) hands those pipes to the child,
			// which then writes past the pseudo-console.
			startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
			startup.StartupInfo.hStdInput = nullptr;
			startup.StartupInfo.hStdOutput = nullptr;
			startup.StartupInfo.hStdError = nullptr;
			startup.lpAttributeList = attributes;
			PROCESS_INFORMATION info{};
			std::wstring mutableLine = commandLine;
			job = CreateKillOnCloseJob();
			const BOOL ok = CreateProcessW(program.c_str(), &mutableLine[0], nullptr, nullptr, FALSE,
				EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr,
				workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup.StartupInfo, &info);
			DeleteProcThreadAttributeList(attributes);
			if (!ok)
			{
				error = LastErrorText("CreateProcess");
				Shutdown();
				return false;
			}
			if (job && !AssignProcessToJobObject(job, info.hProcess))
			{
				CloseHandle(job);
				job = nullptr;
			}
			ResumeThread(info.hThread);
			CloseHandle(info.hThread);
			process = info.hProcess;
			return true;
		}

		bool Write(const std::string& bytes) override
		{
			if (!inputWrite) return false;
			DWORD written = 0;
			return WriteFile(inputWrite, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
				written == bytes.size();
		}

		bool Read(std::string& output, int timeoutMs) override
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (buffer.empty() && !readerDone)
				changed.wait_for(lock, std::chrono::milliseconds((std::max)(timeoutMs, 0)),
					[this] { return !buffer.empty() || readerDone; });
			if (buffer.empty()) return false;
			output += buffer;
			buffer.clear();
			return true;
		}

		bool Exited(unsigned long& exitCode) override
		{
			if (!process) { exitCode = 0; return true; }
			if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
			DWORD code = 0;
			GetExitCodeProcess(process, &code);
			exitCode = code;
			return true;
		}

		void Terminate() override
		{
			if (process && WaitForSingleObject(process, 0) != WAIT_OBJECT_0)
			{
				TerminateProcess(process, 1);
				WaitForSingleObject(process, 2000);
			}
		}

	private:
		void ReaderLoop()
		{
			char chunk[4096];
			for (;;)
			{
				DWORD got = 0;
				if (!ReadFile(outputRead, chunk, sizeof(chunk), &got, nullptr) || got == 0) break;
				{
					std::lock_guard<std::mutex> guard(mutex);
					buffer.append(chunk, got);
					if (buffer.size() > 1024 * 1024) buffer.erase(0, buffer.size() - 1024 * 1024);
				}
				changed.notify_all();
			}
			{
				std::lock_guard<std::mutex> guard(mutex);
				readerDone = true;
			}
			changed.notify_all();
		}

		void Shutdown()
		{
			Terminate();
			if (console)
			{
				Api().close(console);   // ends the console host; the reader then sees the pipe close
				console = nullptr;
			}
			if (inputWrite) { CloseHandle(inputWrite); inputWrite = nullptr; }
			if (reader.joinable())
			{
				const HANDLE thread = reader.native_handle();
				if (WaitForSingleObject(thread, 3000) == WAIT_TIMEOUT)
					CancelSynchronousIo(thread);
				reader.join();
			}
			if (outputRead) { CloseHandle(outputRead); outputRead = nullptr; }
			if (process) { CloseHandle(process); process = nullptr; }
			if (job) { CloseHandle(job); job = nullptr; }
		}

		COORD size;
		PseudoConsoleHandle console = nullptr;
		HANDLE inputWrite = nullptr;
		HANDLE outputRead = nullptr;
		HANDLE process = nullptr;
		HANDLE job = nullptr;
		std::thread reader;
		std::mutex mutex;
		std::condition_variable changed;
		std::string buffer;
		bool readerDone = false;
	};
}

namespace Calibration
{
	std::unique_ptr<ITerminal> CreateConPtyTerminal(short columns, short rows)
	{
		return std::unique_ptr<ITerminal>(new ConPtyTerminal(columns, rows));
	}

	bool ConPtyAvailable()
	{
		return Api().create != nullptr && Api().close != nullptr;
	}

	bool RunCaptured(const std::wstring& program, const std::wstring& commandLine, int timeoutMs,
		std::string& output, unsigned long& exitCode, std::string& error)
	{
		output.clear();
		exitCode = 0;
		SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		HANDLE readEnd = nullptr, writeEnd = nullptr;
		if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) { error = LastErrorText("CreatePipe"); return false; }
		SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
		HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = nul;
		startup.hStdOutput = writeEnd;
		startup.hStdError = writeEnd;
		PROCESS_INFORMATION info{};
		std::wstring mutableLine = commandLine;
		HANDLE job = CreateKillOnCloseJob();
		const BOOL ok = CreateProcessW(program.c_str(), &mutableLine[0], nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info);
		CloseHandle(writeEnd);
		if (nul && nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
		if (!ok)
		{
			error = LastErrorText("CreateProcess");
			CloseHandle(readEnd);
			if (job) CloseHandle(job);
			return false;
		}
		if (job) AssignProcessToJobObject(job, info.hProcess);
		ResumeThread(info.hThread);
		CloseHandle(info.hThread);
		const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>((std::max)(timeoutMs, 0));
		bool timedOut = false;
		char chunk[4096];
		for (;;)
		{
			DWORD available = 0;
			while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available > 0)
			{
				DWORD got = 0;
				if (!ReadFile(readEnd, chunk, (std::min)(available, static_cast<DWORD>(sizeof(chunk))), &got, nullptr) || got == 0)
					break;
				output.append(chunk, got);
				if (output.size() > 1024 * 1024) break;
			}
			if (WaitForSingleObject(info.hProcess, 20) == WAIT_OBJECT_0)
			{
				// Drain what is left.
				DWORD got = 0;
				while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available > 0 &&
					ReadFile(readEnd, chunk, (std::min)(available, static_cast<DWORD>(sizeof(chunk))), &got, nullptr) && got > 0)
					output.append(chunk, got);
				break;
			}
			if (GetTickCount64() > deadline)
			{
				timedOut = true;
				TerminateProcess(info.hProcess, 1);   // the child this function started
				WaitForSingleObject(info.hProcess, 2000);
				break;
			}
		}
		DWORD code = 0;
		GetExitCodeProcess(info.hProcess, &code);
		exitCode = code;
		CloseHandle(info.hProcess);
		CloseHandle(readEnd);
		if (job) CloseHandle(job);
		if (timedOut) error = "the program did not finish within " + std::to_string(timeoutMs / 1000) + " s";
		return !timedOut;
	}
}
