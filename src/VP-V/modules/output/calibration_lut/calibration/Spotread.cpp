// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "Spotread.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace Calibration
{
	namespace
	{
		constexpr size_t kMaxTranscript = 64 * 1024;
		constexpr size_t kMaxLine = 4096;

		std::string Lower(const std::string& text)
		{
			std::string out(text);
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		bool Contains(const std::string& haystack, const char* needle)
		{
			return haystack.find(needle) != std::string::npos;
		}

		bool ContainsAny(const std::string& haystack, std::initializer_list<const char*> needles)
		{
			for (const char* n : needles)
				if (Contains(haystack, n)) return true;
			return false;
		}

		int CsiParameter(const std::string& sequence, int fallback)
		{
			// sequence: ESC [ params final
			int value = 0;
			bool any = false;
			for (size_t i = 2; i + 1 < sequence.size(); ++i)
			{
				const char c = sequence[i];
				if (c >= '0' && c <= '9')
				{
					value = value * 10 + (c - '0');
					any = true;
					if (value > 100000) break;
				}
				else if (c == ';')
					break;   // the first parameter only
				else if (c == '?' || c == '>' || c == '=')
					continue;
				else
					break;
			}
			return any ? value : fallback;
		}
	}

	// ------------------------------------------------------------ terminal

	void TerminalText::Clear()
	{
		escape.clear();
		current.clear();
		lines.clear();
		carriageReturn = false;
	}

	void TerminalText::EndLine()
	{
		const std::string trimmed = TrimText(current);
		if (!trimmed.empty()) lines.push_back(current);
		current.clear();
		carriageReturn = false;
	}

	void TerminalText::Append(const std::string& chunk)
	{
		for (const char raw : chunk)
		{
			const unsigned char c = static_cast<unsigned char>(raw);
			if (!escape.empty())
			{
				escape.push_back(raw);
				bool complete = false;
				if (escape.size() == 2)
				{
					if (raw == '[' || raw == ']') continue;              // CSI / OSC
					if (raw == '(' || raw == ')' || raw == '*' || raw == '+') continue;   // charset: one more
					complete = true;                                     // two-byte sequence
				}
				else if (escape[1] == '[')
				{
					if (c >= 0x40 && c <= 0x7E)
					{
						complete = true;
						const int n = CsiParameter(escape, 1);
						switch (raw)
						{
						case 'H': case 'f': case 'd': case 'E': case 'F':
							EndLine();
							break;
						case 'C': case 'X':
							current.append(static_cast<size_t>((std::min)((std::max)(n, 1), 200)), ' ');
							break;
						case 'G':
						{
							const size_t column = static_cast<size_t>((std::min)((std::max)(n, 1), 400));
							if (column <= 1) { if (!current.empty()) carriageReturn = true; }
							else if (current.size() < column - 1) current.append(column - 1 - current.size(), ' ');
							break;
						}
						default:
							break;   // colours, erase, modes, cursor visibility
						}
					}
				}
				else if (escape[1] == ']')
				{
					if (raw == '\a') complete = true;
					else if (escape.size() >= 2 && raw == '\\' && escape[escape.size() - 2] == '\x1b') complete = true;
				}
				else
				{
					complete = true;   // charset designation: ESC ( B
				}
				if (escape.size() > 256) complete = true;   // malformed: drop
				if (complete) escape.clear();
				continue;
			}
			if (c == 0x1B)
			{
				escape.assign(1, raw);
				continue;
			}
			if (c == '\n')
			{
				EndLine();
				continue;
			}
			if (c == '\r')
			{
				carriageReturn = true;
				continue;
			}
			if (carriageReturn)
			{
				// A bare carriage return: the text that follows redraws the line.
				current.clear();
				carriageReturn = false;
			}
			if (c == '\b')
			{
				if (!current.empty()) current.pop_back();
				continue;
			}
			if (c == '\t')
			{
				current.push_back(' ');
				continue;
			}
			if (c < 0x20 || c == 0x7F) continue;
			current.push_back(raw);
			if (current.size() > kMaxLine) EndLine();
		}
	}

	std::vector<std::string> TerminalText::TakeLines()
	{
		std::vector<std::string> out;
		out.swap(lines);
		return out;
	}

	std::string StripVt(const std::string& text)
	{
		TerminalText t;
		t.Append(text);
		t.Append("\n");
		std::string out;
		for (const std::string& line : t.TakeLines())
		{
			if (!out.empty()) out += "\n";
			out += line;
		}
		return out;
	}

	std::string TrimText(const std::string& text)
	{
		size_t a = 0, b = text.size();
		while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
		while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
		return text.substr(a, b - a);
	}

	// -------------------------------------------------------------- parser

	const char* ToString(SpotreadEventKind kind)
	{
		switch (kind)
		{
		case SpotreadEventKind::ReadyPrompt: return "ready";
		case SpotreadEventKind::CalibrationPrompt: return "calibration_prompt";
		case SpotreadEventKind::CalibrationComplete: return "calibration_complete";
		case SpotreadEventKind::Reading: return "reading";
		case SpotreadEventKind::ReadFailed: return "read_failed";
		case SpotreadEventKind::NeedsCalibration: return "needs_calibration";
		case SpotreadEventKind::FatalError: return "fatal";
		case SpotreadEventKind::Exited: return "exited";
		case SpotreadEventKind::Timeout: return "timeout";
		}
		return "unknown";
	}

	const char* ToString(SpotreadErrorKind kind)
	{
		switch (kind)
		{
		case SpotreadErrorKind::None: return "none";
		case SpotreadErrorKind::NoInstrument: return "no_instrument";
		case SpotreadErrorKind::InitFailed: return "init_failed";
		case SpotreadErrorKind::Communication: return "communication";
		case SpotreadErrorKind::Misread: return "misread";
		case SpotreadErrorKind::TooDark: return "too_dark";
		case SpotreadErrorKind::UserAbort: return "user_abort";
		case SpotreadErrorKind::Usage: return "usage";
		case SpotreadErrorKind::Other: return "other";
		}
		return "unknown";
	}

	bool ParseNumberAt(const std::string& text, size_t& pos, double& value)
	{
		size_t i = pos;
		bool negative = false;
		if (i < text.size() && (text[i] == '-' || text[i] == '+'))
		{
			negative = text[i] == '-';
			++i;
		}
		unsigned long long mantissa = 0;
		int digits = 0, scale = 0;
		bool anyDigit = false;
		while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
		{
			if (digits < 18) { mantissa = mantissa * 10 + static_cast<unsigned>(text[i] - '0'); ++digits; }
			else ++scale;
			anyDigit = true;
			++i;
		}
		if (i < text.size() && text[i] == '.')
		{
			++i;
			while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
			{
				if (digits < 18) { mantissa = mantissa * 10 + static_cast<unsigned>(text[i] - '0'); ++digits; --scale; }
				anyDigit = true;
				++i;
			}
		}
		if (!anyDigit) return false;
		if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
		{
			size_t j = i + 1;
			bool expNegative = false;
			if (j < text.size() && (text[j] == '-' || text[j] == '+')) { expNegative = text[j] == '-'; ++j; }
			int exponent = 0;
			bool anyExp = false;
			while (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j])))
			{
				if (exponent < 10000) exponent = exponent * 10 + (text[j] - '0');
				anyExp = true;
				++j;
			}
			if (anyExp)
			{
				scale += expNegative ? -exponent : exponent;
				i = j;
			}
		}
		double v = static_cast<double>(mantissa) * std::pow(10.0, scale);
		if (negative) v = -v;
		if (!std::isfinite(v)) return false;
		value = v;
		pos = i;
		return true;
	}

	namespace
	{
		bool ParseTriplet(const std::string& text, size_t pos, double out[3])
		{
			for (int k = 0; k < 3; ++k)
			{
				while (pos < text.size() && (text[pos] == ' ' || text[pos] == ',' || text[pos] == '\t')) ++pos;
				if (!ParseNumberAt(text, pos, out[k])) return false;
			}
			return true;
		}
	}

	bool ParseResultLine(const std::string& line, Vec3& xyz)
	{
		const size_t x = line.find("XYZ:");
		if (x != std::string::npos)
		{
			double v[3];
			if (ParseTriplet(line, x + 4, v))
			{
				xyz = { v[0], v[1], v[2] };
				return true;
			}
		}
		const size_t y = line.find("Yxy:");
		if (y != std::string::npos)
		{
			double v[3];
			if (ParseTriplet(line, y + 4, v) && v[2] > 0.0)
			{
				const double Y = v[0], cx = v[1], cy = v[2];
				xyz = { cx * Y / cy, Y, (1.0 - cx - cy) * Y / cy };
				return true;
			}
		}
		return false;
	}

	void SpotreadParser::Reset()
	{
		terminal.Clear();
		transcript.clear();
		recent.clear();
		calibrationPending = false;
		promptLatched.clear();
	}

	void SpotreadParser::AppendTranscript(const std::string& line)
	{
		transcript += line;
		transcript += "\n";
		if (transcript.size() > kMaxTranscript)
			transcript.erase(0, transcript.size() - kMaxTranscript);
	}

	namespace
	{
		enum class PromptKind { None, Ready, Calibration };

		PromptKind DetectPrompt(const std::string& lower)
		{
			if (ContainsAny(lower, { "any key to continue", "key to continue," }))
				return PromptKind::Calibration;
			if (ContainsAny(lower, { "to take a reading", "space to read", "key to read", "spot to be measured",
				"] to read", "to read," }))
				return PromptKind::Ready;
			return PromptKind::None;
		}
	}

	void SpotreadParser::ClassifyLine(const std::string& rawLine, std::vector<SpotreadEvent>& out)
	{
		const std::string line = TrimText(rawLine);
		if (line.empty()) return;
		AppendTranscript(line);
		const std::string lower = Lower(line);

		SpotreadEvent e;
		e.text = line;
		Vec3 xyz;
		if (Contains(lower, "result is") && ParseResultLine(line, xyz))
		{
			e.kind = SpotreadEventKind::Reading;
			e.xyz = xyz;
			out.push_back(e);
			promptLatched.clear();
			recent.clear();
			return;
		}
		// Fatal errors first (a "read failed due to a USB error" is fatal).
		if (ContainsAny(lower, { "no instrument", "no instruments", "failed to find instrument", "instrument not found",
			"no such instrument", "couldn't find", "could not find instrument" }))
		{
			e.kind = SpotreadEventKind::FatalError;
			e.error = SpotreadErrorKind::NoInstrument;
			out.push_back(e);
			return;
		}
		if (ContainsAny(lower, { "initialisation failed", "initialization failed", "failed to initialise",
			"failed to initialize", "instrument access failed", "opening instrument failed" }))
		{
			e.kind = SpotreadEventKind::FatalError;
			e.error = SpotreadErrorKind::InitFailed;
			out.push_back(e);
			return;
		}
		if (ContainsAny(lower, { "communications failure", "communication failure", "usb error", "usb read",
			"usb write", "serial communications", "device disconnected", "lost communication" }))
		{
			e.kind = SpotreadEventKind::FatalError;
			e.error = SpotreadErrorKind::Communication;
			out.push_back(e);
			return;
		}
		if (ContainsAny(lower, { "user aborted", "user hit quit", "user hit abort" }))
		{
			e.kind = SpotreadEventKind::FatalError;
			e.error = SpotreadErrorKind::UserAbort;
			out.push_back(e);
			return;
		}
		if (ContainsAny(lower, { "calibration complete", "calibration done", "calibration succeeded" }))
		{
			e.kind = SpotreadEventKind::CalibrationComplete;
			out.push_back(e);
			calibrationPending = false;
			promptLatched.clear();
			recent.clear();
			return;
		}
		if (ContainsAny(lower, { "needs a calibration", "need a calibration", "needs calibration", "calibration is needed",
			"requires calibration", "calibration needed", "calibration is required" }))
		{
			e.kind = SpotreadEventKind::NeedsCalibration;
			out.push_back(e);
			calibrationPending = true;
			return;
		}
		if (ContainsAny(lower, { "read failed", "spot read failed", "misread", "reading failed", "calibration failed",
			"measurement failed" }))
		{
			e.kind = SpotreadEventKind::ReadFailed;
			e.error = ContainsAny(lower, { "too dark", "not enough light", "low light", "too low" }) ?
				SpotreadErrorKind::TooDark : SpotreadErrorKind::Misread;
			out.push_back(e);
			promptLatched.clear();
			return;
		}
		if (Contains(lower, "usage:") || Contains(lower, "read print spot values") || Contains(lower, "-c listno"))
		{
			// The usage text alone (no "Error -") means a bad argument too.
			e.kind = SpotreadEventKind::FatalError;
			e.error = SpotreadErrorKind::Usage;
			out.push_back(e);
			return;
		}
		if (Contains(lower, "error -") || lower.rfind("error:", 0) == 0 || lower.rfind("fatal", 0) == 0)
		{
			e.kind = SpotreadEventKind::FatalError;
			e.error = SpotreadErrorKind::Other;
			out.push_back(e);
			return;
		}
		recent.push_back(line);
		if (recent.size() > 12) recent.erase(recent.begin());
	}

	void SpotreadParser::CheckPrompt(std::vector<SpotreadEvent>& out)
	{
		// The pending (unterminated) line plus the last complete lines.
		std::string window;
		for (const std::string& line : recent)
			window += Lower(line) + "\n";
		window += Lower(terminal.Pending());
		const PromptKind kind = DetectPrompt(window);
		if (kind == PromptKind::None) return;
		const std::string latch = kind == PromptKind::Ready ? "ready" : "calibration";
		if (promptLatched == latch) return;
		promptLatched = latch;
		SpotreadEvent e;
		if (kind == PromptKind::Calibration)
		{
			e.kind = SpotreadEventKind::CalibrationPrompt;
			std::string text;
			for (const std::string& line : recent)
				text += (text.empty() ? "" : "\n") + line;
			const std::string pending = TrimText(terminal.Pending());
			if (!pending.empty()) text += (text.empty() ? "" : "\n") + pending;
			e.text = text;
			calibrationPending = true;
		}
		else
		{
			e.kind = SpotreadEventKind::ReadyPrompt;
			calibrationPending = false;
		}
		recent.clear();
		out.push_back(e);
	}

	std::vector<SpotreadEvent> SpotreadParser::Feed(const std::string& chunk)
	{
		std::vector<SpotreadEvent> events;
		terminal.Append(chunk);
		for (const std::string& line : terminal.TakeLines())
		{
			ClassifyLine(line, events);
			// A prompt may end on a complete line too.
			if (!events.empty() && events.back().kind == SpotreadEventKind::FatalError) continue;
			CheckPrompt(events);
		}
		CheckPrompt(events);
		return events;
	}

	// ------------------------------------------------------ command line

	std::vector<std::string> SpotreadArguments(const SpotreadOptions& o)
	{
		std::vector<std::string> args;
		if (o.port > 0)
		{
			args.push_back("-c");
			args.push_back(std::to_string(o.port));
		}
		args.push_back("-e");
		args.push_back("-x");
		if (!o.displayType.empty())
		{
			args.push_back("-y");
			args.push_back(o.displayType);
		}
		if (o.refreshMode == 1) { args.push_back("-Y"); args.push_back("r"); }
		else if (o.refreshMode == 2) { args.push_back("-Y"); args.push_back("n"); }
		if (!o.spectralCorrection.empty())
		{
			args.push_back("-X");
			args.push_back(o.spectralCorrection);
		}
		if (o.skipInitialCalibration) args.push_back("-N");
		return args;
	}

	std::wstring QuoteArgument(const std::wstring& arg)
	{
		if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
		std::wstring out = L"\"";
		size_t backslashes = 0;
		for (const wchar_t c : arg)
		{
			if (c == L'\\') { ++backslashes; continue; }
			if (c == L'"')
			{
				out.append(backslashes * 2 + 1, L'\\');
				out.push_back(L'"');
			}
			else
			{
				out.append(backslashes, L'\\');
				out.push_back(c);
			}
			backslashes = 0;
		}
		out.append(backslashes * 2, L'\\');
		out.push_back(L'"');
		return out;
	}

	namespace
	{
		std::wstring Widen(const std::string& text)
		{
			if (text.empty()) return std::wstring();
			const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			if (n <= 0) return std::wstring(text.begin(), text.end());
			std::wstring out(static_cast<size_t>(n), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n);
			return out;
		}
	}

	std::wstring BuildCommandLine(const std::wstring& program, const std::vector<std::string>& arguments)
	{
		std::wstring line = L"\"" + program + L"\"";
		for (const std::string& a : arguments)
			line += L" " + QuoteArgument(Widen(a));
		return line;
	}

	// ------------------------------------------------------- usage text

	namespace
	{
		bool IsOptionLine(const std::string& line)
		{
			size_t i = 0;
			while (i < line.size() && line[i] == ' ') ++i;
			return i < line.size() && i <= 2 && line[i] == '-' && i + 1 < line.size() &&
				std::isalpha(static_cast<unsigned char>(line[i + 1]));
		}

		void ParseVersion(const std::string& version, SpotreadUsage& usage)
		{
			usage.version = version;
			int parts[3] = { 0, 0, 0 };
			size_t pos = 0;
			for (int k = 0; k < 3 && pos < version.size(); ++k)
			{
				int v = 0;
				bool any = false;
				while (pos < version.size() && std::isdigit(static_cast<unsigned char>(version[pos])))
				{
					v = v * 10 + (version[pos] - '0');
					any = true;
					++pos;
				}
				if (!any) break;
				parts[k] = v;
				if (pos < version.size() && version[pos] == '.') ++pos;
				else break;
			}
			usage.versionMajor = parts[0];
			usage.versionMinor = parts[1];
			usage.versionPatch = parts[2];
		}
	}

	SpotreadUsage ParseSpotreadUsage(const std::string& raw)
	{
		SpotreadUsage usage;
		const std::string text = StripVt(raw);
		std::istringstream stream(text);
		std::string line;
		enum class Block { None, Ports, DisplayTypes } block = Block::None;
		while (std::getline(stream, line))
		{
			const std::string lower = Lower(line);
			if (Contains(lower, "read print spot values") || Contains(lower, "spotread"))
				usage.recognised = true;
			if (usage.version.empty())
			{
				const size_t v = lower.find("version ");
				if (v != std::string::npos)
				{
					size_t p = v + 8;
					std::string digits;
					while (p < line.size() && (std::isdigit(static_cast<unsigned char>(line[p])) || line[p] == '.'))
						digits.push_back(line[p++]);
					while (!digits.empty() && digits.back() == '.') digits.pop_back();
					if (!digits.empty()) ParseVersion(digits, usage);
				}
			}
			if (IsOptionLine(line))
			{
				block = Block::None;
				if (Contains(lower, "-c listno") || Contains(lower, "communication port"))
				{
					block = Block::Ports;
					usage.recognised = true;
				}
				else if (Contains(lower, "-y ") && Contains(lower, "display type"))
					block = Block::DisplayTypes;
				continue;
			}
			if (block == Block::Ports)
			{
				// "    1 = 'usb:/bus5/dev4/ (X-Rite i1 DisplayPro, ColorMunki Display)'"
				size_t p = 0;
				while (p < line.size() && line[p] == ' ') ++p;
				int port = 0;
				bool any = false;
				while (p < line.size() && std::isdigit(static_cast<unsigned char>(line[p])))
				{
					port = port * 10 + (line[p] - '0');
					any = true;
					++p;
				}
				while (p < line.size() && line[p] == ' ') ++p;
				if (!any || p >= line.size() || line[p] != '=') continue;
				std::string description = TrimText(line.substr(p + 1));
				if (description.size() >= 2 && description.front() == '\'' && description.back() == '\'')
					description = description.substr(1, description.size() - 2);
				SpotreadInstrument instrument;
				instrument.port = port;
				instrument.description = description;
				const size_t open = description.rfind('(');
				const size_t close = description.rfind(')');
				instrument.name = open != std::string::npos && close != std::string::npos && close > open ?
					description.substr(open + 1, close - open - 1) : description;
				usage.instruments.push_back(instrument);
			}
			else if (block == Block::DisplayTypes)
			{
				const std::string trimmed = TrimText(line);
				if (trimmed.empty() || trimmed.front() == '[') continue;
				size_t p = 0;
				while (p < trimmed.size() && std::isalnum(static_cast<unsigned char>(trimmed[p]))) ++p;
				if (p == 0 || p > 3 || p >= trimmed.size() || trimmed[p] != ' ') continue;
				SpotreadDisplayType type;
				type.selector = trimmed.substr(0, p);
				type.description = TrimText(trimmed.substr(p));
				if (!type.description.empty()) usage.displayTypes.push_back(type);
			}
		}
		return usage;
	}

	bool VersionAtLeast(const SpotreadUsage& u, int major, int minor, int patch)
	{
		if (u.versionMajor != major) return u.versionMajor > major;
		if (u.versionMinor != minor) return u.versionMinor > minor;
		return u.versionPatch >= patch;
	}

	std::string ArgyllInstallGuidance(const std::string& searched)
	{
		std::string text =
			"ArgyllCMS was not found. VP measures through ArgyllCMS's spotread, which you install yourself "
			"(VP does not include or download it):\n"
			"1. Download the Windows 64-bit ArgyllCMS executables from the official site, argyllcms.com "
			"(current 3.x release).\n"
			"2. Unzip it to a folder of your choice, for example C:\\Program Files\\ArgyllCMS.\n"
			"3. Meters: i1Display / Calibrite, ColorMunki Display, Spyder and JETI work without a driver. "
			"i1Pro, ColorMunki Photo and some older meters need Argyll's USB driver - see Argyll's "
			"'Installing the software on Microsoft Windows' page.\n"
			"4. In Config > Calibration, set the Argyll folder (the folder that holds bin\\spotread.exe), "
			"or leave it empty to search the usual places, then press Detect.";
		if (!searched.empty())
			text += "\nSearched: " + searched;
		return text;
	}
}
