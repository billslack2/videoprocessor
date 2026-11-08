// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "ArgyllInstall.h"
#include "Terminal.h"

#include <algorithm>
#include <cwctype>

namespace Calibration
{
	namespace
	{
		std::wstring Join(const std::wstring& a, const std::wstring& b)
		{
			if (a.empty()) return b;
			const wchar_t last = a.back();
			return (last == L'\\' || last == L'/') ? a + b : a + L"\\" + b;
		}

		std::wstring TrimQuotes(std::wstring text)
		{
			while (!text.empty() && std::iswspace(text.back())) text.pop_back();
			size_t start = 0;
			while (start < text.size() && std::iswspace(text[start])) ++start;
			text = text.substr(start);
			if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
				text = text.substr(1, text.size() - 2);
			while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
			return text;
		}

		std::wstring LowerW(std::wstring text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return text;
		}

		std::string Narrow(const std::wstring& text)
		{
			if (text.empty()) return std::string();
			const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			if (n <= 0) return std::string();
			std::string out(static_cast<size_t>(n), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n, nullptr, nullptr);
			return out;
		}

		std::wstring EnvironmentValue(const wchar_t* name)
		{
			const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
			if (n == 0) return std::wstring();
			std::wstring value(n, L'\0');
			const DWORD got = GetEnvironmentVariableW(name, &value[0], n);
			value.resize(got);
			return value;
		}
	}

	std::vector<std::wstring> CandidatesForSetting(const std::wstring& rawSetting)
	{
		std::vector<std::wstring> out;
		const std::wstring setting = TrimQuotes(rawSetting);
		if (setting.empty()) return out;
		const std::wstring lower = LowerW(setting);
		if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".exe") == 0)
		{
			out.push_back(setting);
			return out;
		}
		out.push_back(Join(setting, L"spotread.exe"));
		out.push_back(Join(Join(setting, L"bin"), L"spotread.exe"));
		return out;
	}

	bool FolderVersion(const std::wstring& folderName, int version[3])
	{
		version[0] = version[1] = version[2] = 0;
		size_t i = 0;
		while (i < folderName.size() && !std::iswdigit(folderName[i])) ++i;
		if (i == folderName.size()) return false;
		for (int k = 0; k < 3 && i < folderName.size(); ++k)
		{
			int v = 0;
			bool any = false;
			while (i < folderName.size() && std::iswdigit(folderName[i]))
			{
				v = v * 10 + (folderName[i] - L'0');
				any = true;
				++i;
			}
			if (!any) break;
			version[k] = v;
			if (i < folderName.size() && folderName[i] == L'.') ++i;
			else break;
		}
		return true;
	}

	ArgyllSearchEnvironment SystemSearchEnvironment(const std::wstring& configured)
	{
		ArgyllSearchEnvironment e;
		e.configured = configured;
		e.environment = EnvironmentValue(L"ARGYLL_PATH");
		e.path = EnvironmentValue(L"PATH");
		for (const wchar_t* name : { L"ProgramFiles", L"ProgramW6432", L"ProgramFiles(x86)" })
		{
			const std::wstring root = EnvironmentValue(name);
			if (!root.empty()) e.installRoots.push_back(root);
		}
		const std::wstring local = EnvironmentValue(L"LOCALAPPDATA");
		if (!local.empty()) e.installRoots.push_back(Join(local, L"Programs"));
		const std::wstring profile = EnvironmentValue(L"USERPROFILE");
		if (!profile.empty())
		{
			e.installRoots.push_back(profile);
			e.installRoots.push_back(Join(profile, L"Downloads"));
		}
		const std::wstring systemDrive = EnvironmentValue(L"SystemDrive");
		e.installRoots.push_back(systemDrive.empty() ? std::wstring(L"C:\\") : systemDrive + L"\\");
		e.fileExists = [](const std::wstring& path)
		{
			const DWORD attributes = GetFileAttributesW(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
		};
		e.listArgyllFolders = [](const std::wstring& root)
		{
			std::vector<std::wstring> names;
			WIN32_FIND_DATAW found{};
			const HANDLE handle = FindFirstFileW(Join(root, L"Argyll*").c_str(), &found);
			if (handle == INVALID_HANDLE_VALUE) return names;
			do
			{
				if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && names.size() < 64)
					names.push_back(found.cFileName);
			} while (FindNextFileW(handle, &found));
			FindClose(handle);
			return names;
		};
		return e;
	}

	std::wstring FindSpotread(const ArgyllSearchEnvironment& e, std::string& searched)
	{
		searched.clear();
		auto tried = [&searched](const std::wstring& what)
		{
			if (searched.size() < 2000) searched += (searched.empty() ? "" : "; ") + Narrow(what);
		};
		auto exists = [&e](const std::wstring& path) { return e.fileExists && e.fileExists(path); };
		for (const std::wstring& candidate : CandidatesForSetting(e.configured))
		{
			tried(candidate);
			if (exists(candidate)) return candidate;
		}
		if (!TrimQuotes(e.configured).empty())
			return std::wstring();   // an explicit setting that is wrong is reported, not silently replaced
		for (const std::wstring& candidate : CandidatesForSetting(e.environment))
		{
			tried(candidate);
			if (exists(candidate)) return candidate;
		}
		size_t start = 0;
		bool pathTried = false;
		while (start <= e.path.size() && !e.path.empty())
		{
			size_t end = e.path.find(L';', start);
			if (end == std::wstring::npos) end = e.path.size();
			const std::wstring folder = TrimQuotes(e.path.substr(start, end - start));
			if (!folder.empty())
			{
				const std::wstring candidate = Join(folder, L"spotread.exe");
				if (exists(candidate)) return candidate;
				pathTried = true;
			}
			start = end + 1;
		}
		if (pathTried) tried(L"PATH");
		struct Found { std::wstring path; int version[3]; };
		std::vector<Found> found;
		for (const std::wstring& root : e.installRoots)
		{
			if (!e.listArgyllFolders) break;
			tried(Join(root, L"Argyll*"));
			for (const std::wstring& name : e.listArgyllFolders(root))
			{
				const std::wstring candidate = Join(Join(Join(root, name), L"bin"), L"spotread.exe");
				if (!exists(candidate)) continue;
				Found f;
				f.path = candidate;
				FolderVersion(name, f.version);
				found.push_back(f);
			}
		}
		if (found.empty()) return std::wstring();
		std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b)
		{
			if (a.version[0] != b.version[0]) return a.version[0] > b.version[0];
			if (a.version[1] != b.version[1]) return a.version[1] > b.version[1];
			return a.version[2] > b.version[2];
		});
		return found.front().path;
	}

	std::string ArgyllProbe::Summary() const
	{
		if (!found) return ArgyllInstallGuidance(searched);
		std::string text = "spotread: " + Narrow(spotread);
		if (!ran)
			return text + "\nIt could not be run: " + error;
		if (!usage.recognised)
			return text + "\nIt did not answer like ArgyllCMS's spotread. " + error;
		text += "\nArgyllCMS " + (usage.version.empty() ? std::string("(version not reported)") : usage.version);
		if (!usage.version.empty() && !VersionAtLeast(usage, 3, 0, 0))
			text += " - older than 3.0: install the current release for newer meters";
		if (usage.instruments.empty())
			text += "\nNo meter connected (plug it in, then Detect again).";
		else
			for (const SpotreadInstrument& instrument : usage.instruments)
				text += "\nMeter " + std::to_string(instrument.port) + ": " + instrument.name;
		return text;
	}

	ArgyllProbe ProbeArgyll(const std::wstring& configured, int timeoutMs)
	{
		ArgyllProbe probe;
		probe.spotread = FindSpotread(SystemSearchEnvironment(configured), probe.searched);
		probe.found = !probe.spotread.empty();
		if (!probe.found) return probe;
		std::string output;
		unsigned long exitCode = 0;
		const std::wstring commandLine = L"\"" + probe.spotread + L"\" -?";
		probe.ran = RunCaptured(probe.spotread, commandLine, timeoutMs, output, exitCode, probe.error);
		if (!probe.ran && !output.empty()) probe.ran = true;   // a timeout after the usage text still answers
		probe.usage = ParseSpotreadUsage(output);
		if (probe.ran && !probe.usage.recognised && probe.error.empty())
			probe.error = "Unexpected output: " + TrimText(StripVt(output)).substr(0, 300);
		return probe;
	}
}
