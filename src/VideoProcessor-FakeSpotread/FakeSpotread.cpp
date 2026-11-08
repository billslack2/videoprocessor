// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
// A stand-in for ArgyllCMS's spotread, for VideoProcessor's calibration tests
// (CalibrationMeterTests). It is never shipped or deployed: it builds into
// x64\<config>\test-tools\fake_spotread.exe and the tests copy it to a scratch
// folder as spotread.exe.
//
// Like spotread it reads single keys from its console (_getch), so it only
// works under a real console / pseudo-console - exactly what VP's ConPTY
// terminal gives it. It plays a transcript script:
//
//   FAKE_SPOTREAD_SCRIPT  the script file (required, except for -?)
//   FAKE_SPOTREAD_LOG     appended: "args: ..." and one "key: <code>" per key read
//   FAKE_SPOTREAD_USAGE   the text `-?` prints (default: a built-in usage text)
//
// Script lines:
//   # comment
//   OUT <text>          print text and a newline (\t, \e for ESC escapes)
//   PROMPT <text>       print text without a newline (a prompt)
//   KEY                 wait for a key; 'q' / 'Q' / Esc ends the program (exit 0)
//   READ X Y Z          print a result block for that XYZ
//   SLEEP <ms>
//   HANG                wait forever (a hung instrument; the test's timeout ends it)
//   REPEAT <n> ... ENDREPEAT   (one level)
//   EXIT <code>

#include <windows.h>
#include <conio.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	std::string Env(const char* name)
	{
		char buffer[4096];
		const DWORD n = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
		return n > 0 && n < sizeof(buffer) ? std::string(buffer, n) : std::string();
	}

	void Log(const std::string& line)
	{
		const std::string path = Env("FAKE_SPOTREAD_LOG");
		if (path.empty()) return;
		std::ofstream log(path, std::ios::app | std::ios::binary);
		log << line << "\n";
	}

	std::string Unescape(const std::string& text)
	{
		std::string out;
		for (size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '\\' && i + 1 < text.size())
			{
				const char c = text[++i];
				if (c == 't') out.push_back('\t');
				else if (c == 'e') out.push_back('\x1b');
				else if (c == 'r') out.push_back('\r');
				else out.push_back(c);
			}
			else
				out.push_back(text[i]);
		}
		return out;
	}

	void Print(const std::string& text)
	{
		std::fwrite(text.data(), 1, text.size(), stdout);
		std::fflush(stdout);
	}

	const char* kUsage =
		"Read Print Spot values, Version 3.5.0\n"
		"Author: Graeme W. Gill, licensed under the AGPL Version 3\n"
		"usage: spotread [-options] [logfile]\n"
		" -v                   Verbose mode\n"
		" -s                   Print spectrum for each reading\n"
		" -c listno            Set communication port from the following list (default 1)\n"
		"    1 = 'usb:/bus5/dev4/ (X-Rite i1 DisplayPro, ColorMunki Display)'\n"
		" -e                   Emissive for display\n"
		" -y c|l               Display type - instrument specific list to choose from:\n"
		"    l   LCD (generic)\n"
		"    c   CRT (generic)\n"
		"    r   Refresh (generic)\n"
		" -x                   Display Yxy instead of Lab\n";
}

int main(int argc, char** argv)
{
	std::string args;
	for (int i = 1; i < argc; ++i)
		args += (i > 1 ? " " : "") + std::string(argv[i]);
	Log("args: " + args);
	for (int i = 1; i < argc; ++i)
		if (std::string(argv[i]) == "-?" || std::string(argv[i]) == "-h")
		{
			const std::string usagePath = Env("FAKE_SPOTREAD_USAGE");
			std::string usage = kUsage;
			if (!usagePath.empty())
			{
				std::ifstream file(usagePath, std::ios::binary);
				std::stringstream text;
				text << file.rdbuf();
				usage = text.str();
			}
			std::fputs(usage.c_str(), stderr);
			return 1;
		}

	const std::string scriptPath = Env("FAKE_SPOTREAD_SCRIPT");
	std::ifstream file(scriptPath, std::ios::binary);
	if (!file)
	{
		Print("spotread: Error - FAKE_SPOTREAD_SCRIPT is not set or cannot be read\n");
		return 2;
	}
	std::vector<std::string> lines;
	for (std::string line; std::getline(file, line);)
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(line);
	}

	size_t repeatStart = 0;
	int repeatLeft = 0;
	for (size_t pc = 0; pc < lines.size(); ++pc)
	{
		const std::string& line = lines[pc];
		if (line.empty() || line[0] == '#') continue;
		const size_t space = line.find(' ');
		const std::string op = line.substr(0, space);
		const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
		if (op == "OUT") Print(Unescape(rest) + "\n");
		else if (op == "PROMPT") Print(Unescape(rest));
		else if (op == "KEY")
		{
			const int key = _getch();
			Log("key: " + std::to_string(key));
			if (key == 'q' || key == 'Q' || key == 27)
			{
				Print("\n");
				return 0;
			}
		}
		else if (op == "READ")
		{
			double x = 0, y = 0, z = 0;
			sscanf_s(rest.c_str(), "%lf %lf %lf", &x, &y, &z);
			const double sum = x + y + z;
			char buffer[512];
			std::snprintf(buffer, sizeof(buffer),
				"\n Result is XYZ: %f %f %f, Yxy: %f %f %f\n\n",
				x, y, z, y, sum > 0 ? x / sum : 0.0, sum > 0 ? y / sum : 0.0);
			Print(buffer);
		}
		else if (op == "SLEEP") Sleep(static_cast<DWORD>(std::atoi(rest.c_str())));
		else if (op == "HANG") for (;;) Sleep(1000);
		else if (op == "REPEAT")
		{
			repeatLeft = std::atoi(rest.c_str());
			repeatStart = pc;
		}
		else if (op == "ENDREPEAT")
		{
			if (--repeatLeft > 0) pc = repeatStart;
		}
		else if (op == "EXIT") return std::atoi(rest.c_str());
	}
	return 0;
}
