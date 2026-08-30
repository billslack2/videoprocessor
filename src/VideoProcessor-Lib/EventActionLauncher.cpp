#include <pch.h>

#include "EventActionLauncher.h"

#include <ConfigFile.h>
#include <DebugLog.h>

#include <utility>
#include <windows.h>


namespace
{
	std::string ParentPath(const std::string& path)
	{
		const size_t separator = path.find_last_of("\\/");
		return separator == std::string::npos ? std::string() :
			path.substr(0, separator);
	}


	bool IsAbsoluteWindowsPath(const std::string& path)
	{
		return path.size() >= 3 &&
			std::isalpha(static_cast<unsigned char>(path[0])) &&
			path[1] == ':' && (path[2] == '\\' || path[2] == '/');
	}


	std::wstring Utf8ToWide(const std::string& value)
	{
		if (value.empty())
			return std::wstring();
		const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			value.c_str(), -1, nullptr, 0);
		if (length <= 0)
			return std::wstring();
		std::wstring result(static_cast<size_t>(length), L'\0');
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
			-1, &result[0], length) <= 0)
			return std::wstring();
		result.resize(static_cast<size_t>(length - 1));
		return result;
	}
}


namespace EventActionLauncher
{
	bool ExpandArgumentVariables(
		const RendererProfileConfig::Model::EventAction& action,
		const ActionValueLookup& values,
		RendererProfileConfig::Model::EventAction& expanded,
		std::string& error)
	{
		expanded = action;
		error.clear();
		std::string result;
		result.reserve(action.arguments.size());
		size_t cursor = 0;
		while (cursor < action.arguments.size())
		{
			const size_t open = action.arguments.find("${", cursor);
			if (open == std::string::npos)
			{
				result.append(action.arguments, cursor, std::string::npos);
				break;
			}
			result.append(action.arguments, cursor, open - cursor);
			const size_t close = action.arguments.find('}', open + 2);
			if (close == std::string::npos)
			{
				error = "unterminated ${variable} reference";
				return false;
			}
			const std::string variable = ConfigFile::NormalizeName(
				action.arguments.substr(open + 2, close - open - 2));
			std::string value;
			if (variable.empty() || !values || !values(variable, value))
			{
				error = "argument variable '${" + variable +
					"}' is unavailable for this event";
				return false;
			}
			result += value;
			cursor = close + 1;
		}
		expanded.arguments = std::move(result);
		return true;
	}

	void Launch(const RendererProfileConfig::Model::EventAction& action,
		const std::string& configPath, bool waitForExit,
		uintptr_t cancellationEvent)
	{
		const std::string configDirectory = ParentPath(configPath);
		std::string program = ConfigFile::Trim(action.program);
		if (!IsAbsoluteWindowsPath(program) && !configDirectory.empty())
			program = configDirectory + "\\" + program;
		std::string workingDirectory = ConfigFile::Trim(action.workingDirectory);
		if (workingDirectory.empty())
			workingDirectory = configDirectory;
		else if (!IsAbsoluteWindowsPath(workingDirectory) &&
			!configDirectory.empty())
			workingDirectory = configDirectory + "\\" + workingDirectory;

		const std::wstring wideProgram = Utf8ToWide(program);
		const std::wstring wideArguments = Utf8ToWide(action.arguments);
		const std::wstring wideWorkingDirectory = Utf8ToWide(workingDirectory);
		if (wideProgram.empty())
		{
			DebugLog::Log("event action '%s' rejected: program is not valid UTF-8",
				action.name.c_str());
			return;
		}

		const std::string normalizedProgram = ConfigFile::NormalizeName(program);
		const bool batch = normalizedProgram.size() >= 4 &&
			(normalizedProgram.substr(normalizedProgram.size() - 4) == ".bat" ||
				normalizedProgram.substr(normalizedProgram.size() - 4) == ".cmd");
		std::wstring application = wideProgram;
		std::wstring commandLine;
		if (batch)
		{
			wchar_t commandProcessor[MAX_PATH]{};
			if (GetEnvironmentVariableW(L"ComSpec", commandProcessor,
				static_cast<DWORD>(_countof(commandProcessor))) == 0)
			{
				DebugLog::Log("event action '%s' rejected: ComSpec is unavailable",
					action.name.c_str());
				return;
			}
			application = commandProcessor;
			commandLine = L"\"" + application + L"\" /d /s /c \"\"" +
				wideProgram + L"\"" +
				(wideArguments.empty() ? L"" : L" " + wideArguments) + L"\"";
		}
		else
		{
			commandLine = L"\"" + wideProgram + L"\"" +
				(wideArguments.empty() ? L"" : L" " + wideArguments);
		}

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESHOWWINDOW;
		startupInfo.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION processInfo{};
		if (!CreateProcessW(application.c_str(), &commandLine[0], nullptr,
			nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			wideWorkingDirectory.empty() ? nullptr :
			wideWorkingDirectory.c_str(), &startupInfo, &processInfo))
		{
			DebugLog::Log(
				"event action process creation failed: action='%s' role=%s "
				"program=%s error=%lu",
				action.name.c_str(), ActionIdentity(action).c_str(),
				program.c_str(), GetLastError());
			return;
		}
		const DWORD processId = processInfo.dwProcessId;
		CloseHandle(processInfo.hThread);
		DebugLog::Log(
			"event action process created: action='%s' role=%s pid=%lu program=%s",
			action.name.c_str(), ActionIdentity(action).c_str(), processId,
			program.c_str());
		if (!waitForExit)
		{
			CloseHandle(processInfo.hProcess);
			return;
		}

		const HANDLE cancel = reinterpret_cast<HANDLE>(cancellationEvent);
		const HANDLE waitHandles[] = { processInfo.hProcess, cancel };
		const DWORD waitCount = cancel ? 2 : 1;
		const DWORD waitResult = WaitForMultipleObjects(waitCount, waitHandles,
			FALSE, INFINITE);
		if (waitResult == WAIT_OBJECT_0)
		{
			DWORD exitCode = STILL_ACTIVE;
			GetExitCodeProcess(processInfo.hProcess, &exitCode);
			DebugLog::Log(
				"event action serialized completion: action='%s' role=%s pid=%lu exit=%lu",
				action.name.c_str(), ActionIdentity(action).c_str(), processId,
				exitCode);
		}
		else if (cancel && waitResult == WAIT_OBJECT_0 + 1)
		{
			DebugLog::Log(
				"event action serialized wait released by shutdown: action='%s' role=%s pid=%lu",
				action.name.c_str(), ActionIdentity(action).c_str(), processId);
		}
		else
		{
			DebugLog::Log(
				"event action serialized wait failed: action='%s' role=%s pid=%lu error=%lu",
				action.name.c_str(), ActionIdentity(action).c_str(), processId,
				GetLastError());
		}
		CloseHandle(processInfo.hProcess);
	}
}
