// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "TranscriptRecorder.h"
#include "CalibrationRun.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Calibration
{
	std::string EscapeTranscriptText(const std::string& text)
	{
		std::string out;
		for (const char c : text)
		{
			if (c == '\\') out += "\\\\";
			else if (c == '\x1b') out += "\\e";
			else if (c == '\r') out += "\\r";
			else if (c == '\t') out += "\\t";
			else if (c == '\n') continue;   // lines are split before escaping
			else out.push_back(c);
		}
		return out;
	}

	std::string FormatTranscriptScript(const std::vector<TranscriptEvent>& events, const std::vector<std::string>& header)
	{
		std::string script;
		for (const std::string& h : header) script += "# " + h + "\n";
		std::string pending;           // output not yet ended by a newline
		double lastKey = -1.0;
		bool sleepDue = false;
		auto flushLines = [&]()
		{
			size_t newline;
			while ((newline = pending.find('\n')) != std::string::npos)
			{
				std::string line = pending.substr(0, newline);
				if (!line.empty() && line.back() == '\r') line.pop_back();
				script += line.empty() ? "OUT\n" : "OUT " + EscapeTranscriptText(line) + "\n";
				pending.erase(0, newline + 1);
			}
		};
		for (const TranscriptEvent& e : events)
		{
			switch (e.kind)
			{
			case TranscriptEvent::Kind::Output:
				if (sleepDue)
				{
					const double ms = (e.seconds - lastKey) * 1000.0;
					if (ms >= 20.0)
						script += "SLEEP " + std::to_string(static_cast<long long>(std::lround((std::min)(ms, 600000.0)))) + "\n";
					sleepDue = false;
				}
				pending += e.text;
				flushLines();
				break;
			case TranscriptEvent::Kind::Key:
				if (!pending.empty())
				{
					script += "PROMPT " + EscapeTranscriptText(pending) + "\n";
					pending.clear();
				}
				for (size_t i = 0; i < e.text.size(); ++i) script += "KEY\n";
				lastKey = e.seconds;
				sleepDue = true;
				break;
			case TranscriptEvent::Kind::Exit:
				if (!pending.empty())
				{
					script += "PROMPT " + EscapeTranscriptText(pending) + "\n";
					pending.clear();
				}
				script += "EXIT " + std::to_string(e.exitCode) + "\n";
				break;
			}
		}
		if (!pending.empty()) script += "PROMPT " + EscapeTranscriptText(pending) + "\n";
		return script;
	}

	std::string TranscriptFileName(const std::string& meter, const std::string& stamp)
	{
		std::string name;
		for (const char c : meter)
		{
			if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') name.push_back(c);
			else if (c >= 'A' && c <= 'Z') name.push_back(static_cast<char>(c - 'A' + 'a'));
			else if (!name.empty() && name.back() != '_') name.push_back('_');
		}
		while (!name.empty() && name.back() == '_') name.pop_back();
		if (name.empty()) name = "meter";
		if (name.size() > 48) name.resize(48);
		return "recorded_" + name + "_" + stamp + ".txt";
	}

	RecordingTerminal::RecordingTerminal(std::unique_ptr<ITerminal> i, const std::string& p, std::vector<std::string> h,
		Clock c)
		: inner(std::move(i)), path(p), header(std::move(h)), clock(std::move(c))
	{
	}

	RecordingTerminal::~RecordingTerminal()
	{
		std::string error;
		if (!path.empty() && !saved && !Save(error))
			DebugLog::Log("Calibration: the spotread transcript could not be saved: %s", error.c_str());
	}

	double RecordingTerminal::Now() const
	{
		if (clock) return clock();
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	bool RecordingTerminal::Start(const std::wstring& program, const std::wstring& commandLine,
		const std::wstring& workingDirectory, std::string& error)
	{
		{
			std::lock_guard<std::mutex> g(mutex);
			std::string narrow;
			for (const wchar_t w : commandLine) narrow.push_back(w < 128 ? static_cast<char>(w) : '?');
			// The arguments only (the program's path is the user's install folder).
			std::string args;
			if (!narrow.empty() && narrow[0] == '"')
			{
				const size_t close = narrow.find('"', 1);
				if (close != std::string::npos) args = narrow.substr(close + 1);
			}
			else
			{
				const size_t space = narrow.find(' ');
				if (space != std::string::npos) args = narrow.substr(space + 1);
			}
			while (!args.empty() && args.front() == ' ') args.erase(args.begin());
			header.push_back("spotread arguments: " + args);
		}
		return inner && inner->Start(program, commandLine, workingDirectory, error);
	}

	bool RecordingTerminal::Write(const std::string& bytes)
	{
		const bool ok = inner && inner->Write(bytes);
		if (ok)
		{
			std::lock_guard<std::mutex> g(mutex);
			TranscriptEvent e;
			e.kind = TranscriptEvent::Kind::Key;
			e.seconds = Now();
			e.text = bytes;
			events.push_back(e);
		}
		return ok;
	}

	bool RecordingTerminal::Read(std::string& output, int timeoutMs)
	{
		const size_t before = output.size();
		const bool got = inner && inner->Read(output, timeoutMs);
		if (output.size() > before)
		{
			std::lock_guard<std::mutex> g(mutex);
			TranscriptEvent e;
			e.kind = TranscriptEvent::Kind::Output;
			e.seconds = Now();
			e.text = output.substr(before);
			events.push_back(e);
		}
		return got;
	}

	bool RecordingTerminal::Exited(unsigned long& exitCode)
	{
		const bool exited = inner && inner->Exited(exitCode);
		if (exited)
		{
			std::lock_guard<std::mutex> g(mutex);
			if (!exitRecorded)
			{
				exitRecorded = true;
				TranscriptEvent e;
				e.kind = TranscriptEvent::Kind::Exit;
				e.seconds = Now();
				e.exitCode = exitCode;
				events.push_back(e);
			}
		}
		return exited;
	}

	void RecordingTerminal::Terminate()
	{
		if (inner) inner->Terminate();
	}

	std::string RecordingTerminal::Script() const
	{
		std::lock_guard<std::mutex> g(mutex);
		return FormatTranscriptScript(events, header);
	}

	bool RecordingTerminal::Save(std::string& error)
	{
		saved = true;
		if (path.empty()) return true;
		return WriteTextFile(path, Script(), error);
	}
}
