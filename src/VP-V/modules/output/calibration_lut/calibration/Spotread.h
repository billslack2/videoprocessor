// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: ArgyllCMS spotread, the text side (no process here).
//
//  - TerminalText: a pseudo-console's output (VT sequences, cursor moves,
//    carriage-return redraws) turned into plain lines.
//  - SpotreadParser: incremental classification of spotread's output into
//    events - the "take a reading" prompt, a sensor-calibration prompt, a
//    reading (XYZ), a recoverable read failure, a fatal error.
//  - The command line VP runs and spotread's usage text (version, connected
//    instruments, display types), plus the install guidance shown when Argyll
//    is missing. VP never bundles or downloads Argyll.
//
// spotread's wording differs between Argyll versions; the parser keys on
// short, long-stable phrases ("Result is XYZ:", "any key to continue",
// "to take a reading" / "space to read", "Error -") and the unit tests run
// recorded transcripts of each case (src/VP-V/modules/output/calibration_lut/tests/data/spotread).

#include "../pattern_lib/ColorMath.h"

#include <string>
#include <vector>

namespace Calibration
{
	// ------------------------------------------------------------ terminal

	// Appends `chunk` to a VT-stripping line assembler. Escape sequences are
	// removed; cursor positioning (CSI H / f / E / F / d) and line feeds end a
	// line; a bare carriage return starts the line again (a redraw keeps the
	// last text); CSI C / X (cursor forward, erase characters: a pseudo-
	// console's compressed spaces) become spaces. Sequences split across
	// chunks are kept until complete.
	class TerminalText
	{
	public:
		void Append(const std::string& chunk);
		// Complete lines since the last call.
		std::vector<std::string> TakeLines();
		// The unfinished last line (a prompt waits there without a newline).
		const std::string& Pending() const { return current; }
		void Clear();

	private:
		void EndLine();
		std::string escape;      // an incomplete escape sequence
		std::string current;
		std::vector<std::string> lines;
		bool carriageReturn = false;
	};

	// Plain-text helpers (also used for lines that never went through a terminal).
	std::string StripVt(const std::string& text);
	std::string TrimText(const std::string& text);

	// -------------------------------------------------------------- parser

	enum class SpotreadEventKind
	{
		ReadyPrompt,          // waiting for a key to take a reading
		CalibrationPrompt,    // place the sensor / cap / tile, then any key
		CalibrationComplete,
		Reading,              // xyz valid
		ReadFailed,           // this reading failed (too dark, misread); spotread goes on
		NeedsCalibration,     // the instrument asks for a calibration before reading on
		FatalError,           // spotread cannot go on (no instrument, init failed, USB)
		Exited,               // set by the session when the process ends
		Timeout,              // set by the session
	};

	enum class SpotreadErrorKind
	{
		None,
		NoInstrument,
		InitFailed,
		Communication,
		Misread,
		TooDark,
		UserAbort,
		Usage,
		Other,
	};

	struct SpotreadEvent
	{
		SpotreadEventKind kind = SpotreadEventKind::ReadyPrompt;
		SpotreadErrorKind error = SpotreadErrorKind::None;
		Vec3 xyz = { 0.0, 0.0, 0.0 };
		std::string text;     // the instructions (calibration) or the message (errors)
	};

	const char* ToString(SpotreadEventKind kind);
	const char* ToString(SpotreadErrorKind kind);

	// Parses "Result is XYZ: X Y Z ..." (any Argyll 1.x-3.x layout: ", D50 Lab:",
	// ", Yxy:", trailing units). Falls back to "Yxy: Y x y". False when no
	// finite triplet is present. Locale-independent.
	bool ParseResultLine(const std::string& line, Vec3& xyz);
	// A locale-independent number at text[pos]; advances pos.
	bool ParseNumberAt(const std::string& text, size_t& pos, double& value);

	class SpotreadParser
	{
	public:
		// Feeds raw pseudo-console bytes; returns the events found, in order.
		std::vector<SpotreadEvent> Feed(const std::string& chunk);
		// Everything seen (plain text), bounded, for error reports and the log.
		const std::string& Transcript() const { return transcript; }
		void Reset();

	private:
		void ClassifyLine(const std::string& line, std::vector<SpotreadEvent>& out);
		void CheckPrompt(std::vector<SpotreadEvent>& out);
		void AppendTranscript(const std::string& line);

		TerminalText terminal;
		std::string transcript;
		std::vector<std::string> recent;   // lines since the last prompt (calibration instructions)
		bool calibrationPending = false;
		std::string promptLatched;         // the pending text a prompt event was raised for
	};

	// ------------------------------------------------------ command line

	struct SpotreadOptions
	{
		int port = 0;                       // -c n; 0 = spotread's default (the first instrument)
		std::string displayType;            // -y x
		std::string spectralCorrection;     // -X file (.ccss / .ccmx)
		int refreshMode = 0;                // 0 auto, 1 -Y r, 2 -Y n
		bool skipInitialCalibration = false;// -N (colorimeters only; spotread ignores it otherwise)
	};

	// The arguments after the program name. Always -e (emissive, absolute
	// cd/m2) and -x (Yxy) so the result line carries XYZ and Yxy.
	std::vector<std::string> SpotreadArguments(const SpotreadOptions& options);
	// A Windows command line: the quoted program path then the arguments,
	// each quoted by CommandLineToArgvW's rules.
	std::wstring BuildCommandLine(const std::wstring& program, const std::vector<std::string>& arguments);
	std::wstring QuoteArgument(const std::wstring& argument);

	// ------------------------------------------------------- usage text

	struct SpotreadInstrument
	{
		int port = 0;
		std::string description;   // "usb:/bus5/dev4/ (X-Rite i1 DisplayPro, ColorMunki Display)"
		std::string name;          // the part in parentheses when present
	};

	struct SpotreadDisplayType
	{
		std::string selector;      // "l", "c", "1" ...
		std::string description;
	};

	struct SpotreadUsage
	{
		bool recognised = false;   // looked like spotread's usage
		std::string version;       // "3.3.0"
		int versionMajor = 0, versionMinor = 0, versionPatch = 0;
		std::vector<SpotreadInstrument> instruments;
		std::vector<SpotreadDisplayType> displayTypes;
	};

	SpotreadUsage ParseSpotreadUsage(const std::string& text);
	// Argyll 3.0 or newer is recommended (older releases miss newer meters).
	bool VersionAtLeast(const SpotreadUsage& usage, int major, int minor, int patch = 0);

	// The text the wizard shows when spotread cannot be found or run.
	std::string ArgyllInstallGuidance(const std::string& searched);
}
