// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration test support: the spotread transcript scripts
// (data/spotread), an in-process ITerminal that plays them (the session's
// CPU tests), and paths to the fake spotread executable (the ConPTY tests).

#include <VP-V/modules/output/calibration_lut/pattern_lib/ColorMath.h>
#include <VP-V/modules/output/calibration_lut/calibration/Terminal.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace CalibrationTest
{
	inline std::string RepositoryRoot()
	{
		std::string path = __FILE__;
		for (char& c : path) if (c == '/') c = '\\';
		const size_t src = path.rfind("\\src\\");
		return src == std::string::npos ? std::string(".") : path.substr(0, src);
	}

	inline std::string TranscriptPath(const std::string& name)
	{
		return RepositoryRoot() + "\\src\\VP-V\\modules\\output\\calibration_lut\\tests\\data\\spotread\\" + name;
	}

	inline std::string FakeSpotreadPath()
	{
		return RepositoryRoot() + "\\x64\\Release\\test-tools\\fake_spotread.exe";
	}

	inline std::vector<std::string> ReadLines(const std::string& path)
	{
		std::vector<std::string> lines;
		std::ifstream file(path, std::ios::binary);
		for (std::string line; std::getline(file, line);)
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			lines.push_back(line);
		}
		return lines;
	}

	inline std::string Unescape(const std::string& text)
	{
		std::string out;
		for (size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '\\' && i + 1 < text.size())
			{
				const char c = text[++i];
				out.push_back(c == 't' ? '\t' : c == 'e' ? '\x1b' : c == 'r' ? '\r' : c);
			}
			else
				out.push_back(text[i]);
		}
		return out;
	}

	inline std::string ResultBlock(const std::string& rest)
	{
		double x = 0, y = 0, z = 0;
		sscanf_s(rest.c_str(), "%lf %lf %lf", &x, &y, &z);
		const double sum = x + y + z;
		char buffer[512];
		std::snprintf(buffer, sizeof(buffer), "\r\n Result is XYZ: %f %f %f, Yxy: %f %f %f\r\n\r\n",
			x, y, z, y, sum > 0 ? x / sum : 0.0, sum > 0 ? y / sum : 0.0);
		return buffer;
	}

	// The whole output of a script as if every KEY were answered (parser tests).
	inline std::string PlayAll(const std::string& scriptName)
	{
		std::string out;
		for (const std::string& line : ReadLines(TranscriptPath(scriptName)))
		{
			if (line.empty() || line[0] == '#') continue;
			const size_t space = line.find(' ');
			const std::string op = line.substr(0, space);
			const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
			if (op == "OUT") out += Unescape(rest) + "\r\n";
			else if (op == "PROMPT") out += Unescape(rest);
			else if (op == "READ") out += ResultBlock(rest);
		}
		return out;
	}

	// An ITerminal that runs a transcript script in-process, deterministically:
	// output up to the next KEY that has no key yet; SLEEP and empty reads
	// advance `now` (the session's clock) instead of waiting.
	class ScriptedTerminal final : public Calibration::ITerminal
	{
	public:
		ScriptedTerminal(const std::string& scriptName, double* now) : lines(ReadLines(TranscriptPath(scriptName))), clock(now) {}
		// A script from its lines (a recorded transcript in a scratch folder).
		ScriptedTerminal(const std::vector<std::string>& scriptLines, double* now) : lines(scriptLines), clock(now) {}

		bool Start(const std::wstring& program, const std::wstring& commandLine, const std::wstring&,
			std::string& error) override
		{
			if (program.empty()) { error = "no program"; return false; }
			started = true;
			startedCommandLine = commandLine;
			return true;
		}
		bool Write(const std::string& bytes) override
		{
			if (exited) return false;
			keys += bytes;
			pendingKeys += bytes;
			return true;
		}
		bool Read(std::string& output, int timeoutMs) override
		{
			Run();
			if (!buffer.empty())
			{
				output += buffer;
				buffer.clear();
				return true;
			}
			if (clock) *clock += timeoutMs / 1000.0;
			return false;
		}
		bool Exited(unsigned long& code) override
		{
			Run();
			code = exitCode;
			return exited;
		}
		void Terminate() override
		{
			terminated = true;
			exited = true;
			exitCode = 1;
		}

		std::string keys;
		std::wstring startedCommandLine;
		bool started = false;
		bool terminated = false;

	private:
		void Run()
		{
			while (!exited && pc < lines.size())
			{
				const std::string& line = lines[pc];
				if (line.empty() || line[0] == '#') { ++pc; continue; }
				const size_t space = line.find(' ');
				const std::string op = line.substr(0, space);
				const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
				if (op == "OUT") buffer += Unescape(rest) + "\r\n";
				else if (op == "PROMPT") buffer += Unescape(rest);
				else if (op == "READ") buffer += ResultBlock(rest);
				else if (op == "SLEEP") { if (clock) *clock += std::atoi(rest.c_str()) / 1000.0; }
				else if (op == "HANG") return;   // never advances
				else if (op == "EXIT") { exited = true; exitCode = static_cast<unsigned long>(std::atoi(rest.c_str())); return; }
				else if (op == "KEY")
				{
					if (pendingKeys.empty()) return;
					const char key = pendingKeys[0];
					pendingKeys.erase(0, 1);
					if (key == 'q' || key == 'Q' || key == 27) { exited = true; exitCode = 0; ++pc; return; }
				}
				++pc;
			}
			if (pc >= lines.size()) exited = true;
		}

		std::vector<std::string> lines;
		size_t pc = 0;
		double* clock;
		std::string buffer;
		std::string pendingKeys;
		bool exited = false;
		unsigned long exitCode = 0;
	};
}
