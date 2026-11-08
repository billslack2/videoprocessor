// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration phase 2: recording real spotread sessions.
//
// With [calibration] record_transcripts on, VP's terminal to spotread is
// wrapped in a RecordingTerminal: everything spotread prints and every key VP
// sends are kept with their times, and when the meter closes the session is
// saved as a script in the test-data format that fake_spotread.exe and the
// CPU tests' ScriptedTerminal play (src/VP-V/modules/output/calibration_lut/tests/data/spotread,
// README.txt there):
//
//   OUT <line>      a complete output line (\e ESC, \r, \t, \\ escaped)
//   PROMPT <text>   output without a newline, before a key
//   KEY             a key VP sent ('q' ends the script)
//   SLEEP <ms>      the instrument's time between a key and its answer
//   EXIT <code>     spotread's exit code
//
// Files: <transcript_folder or <vp>\luts\calibration\transcripts>\
// recorded_<meter>_<yyyyMMdd-HHmmss>.txt. Commit one to data/spotread as
// recorded_<meter>.txt to replace the modelled transcripts.

#include "Terminal.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Calibration
{
	struct TranscriptEvent
	{
		enum class Kind { Output, Key, Exit } kind = Kind::Output;
		double seconds = 0.0;
		std::string text;     // output bytes, or the key
		unsigned long exitCode = 0;
	};

	// Escapes for an OUT / PROMPT line.
	std::string EscapeTranscriptText(const std::string& text);
	// The script for a recorded session; header lines become # comments.
	std::string FormatTranscriptScript(const std::vector<TranscriptEvent>& events, const std::vector<std::string>& header);
	// recorded_<meter>_<stamp>.txt (meter: letters, digits, '-', '_').
	std::string TranscriptFileName(const std::string& meter, const std::string& stamp);

	class RecordingTerminal final : public ITerminal
	{
	public:
		using Clock = std::function<double()>;
		// path: where the script is written when the terminal is destroyed
		// (empty = keep in memory only, Script() for tests).
		RecordingTerminal(std::unique_ptr<ITerminal> inner, const std::string& path, std::vector<std::string> header,
			Clock clock = Clock());
		~RecordingTerminal() override;

		bool Start(const std::wstring& program, const std::wstring& commandLine, const std::wstring& workingDirectory,
			std::string& error) override;
		bool Write(const std::string& bytes) override;
		bool Read(std::string& output, int timeoutMs) override;
		bool Exited(unsigned long& exitCode) override;
		void Terminate() override;

		std::string Script() const;
		// Writes the script now (also done by the destructor); false with error.
		bool Save(std::string& error);
		const std::string& Path() const { return path; }

	private:
		double Now() const;
		std::unique_ptr<ITerminal> inner;
		std::string path;
		std::vector<std::string> header;
		Clock clock;
		mutable std::mutex mutex;
		std::vector<TranscriptEvent> events;
		bool exitRecorded = false;
		bool saved = false;
	};
}
