// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "CalibrationRun.h"

#include "Spotread.h"
#include <VideoProcessor-ConfigCore/ConfigEditorCore.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace Calibration
{
	namespace
	{
		std::wstring Wide(const std::string& utf8)
		{
			if (utf8.empty()) return std::wstring();
			const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
			if (n <= 0)
			{
				// Not UTF-8: the ANSI code page.
				const int m = MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
				std::wstring out(static_cast<size_t>((std::max)(m, 0)), L'\0');
				if (m > 0) MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], m);
				return out;
			}
			std::wstring out(static_cast<size_t>(n), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], n);
			return out;
		}

		std::string Narrow(const std::wstring& text)
		{
			if (text.empty()) return std::string();
			const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<size_t>((std::max)(n, 0)), '\0');
			if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n, nullptr, nullptr);
			return out;
		}

		std::string JsonString(const std::string& text)
		{
			std::string out = "\"";
			for (const char c : text)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
						out += buffer;
					}
					else
						out.push_back(c);
				}
			}
			return out + "\"";
		}

		std::string Number(double v, int digits = 6)
		{
			if (!std::isfinite(v)) return "null";
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.*g", digits, v);
			return buffer;
		}

		std::string Html(const std::string& text)
		{
			std::string out;
			for (const char c : text)
			{
				if (c == '<') out += "&lt;";
				else if (c == '>') out += "&gt;";
				else if (c == '&') out += "&amp;";
				else if (c == '"') out += "&quot;";
				else out.push_back(c);
			}
			return out;
		}

		std::string FolderOf(const std::string& path)
		{
			const size_t slash = path.find_last_of("\\/");
			return slash == std::string::npos ? std::string() : path.substr(0, slash);
		}

		std::string FileNameOf(const std::string& path)
		{
			const size_t slash = path.find_last_of("\\/");
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}
	}

	std::string FormatJournalLine(const JournalEntry& e)
	{
		std::ostringstream out;
		out << "{\"i\":" << e.index << ",\"phase\":" << JsonString(e.phase)
			<< ",\"kind\":" << JsonString(ToString(e.patch.kind))
			<< ",\"label\":" << JsonString(e.patch.label)
			<< ",\"space\":" << JsonString(e.patch.space == PatchSpace::Device ? "device" : "target")
			<< ",\"signal\":[" << Number(e.patch.rgb[0]) << "," << Number(e.patch.rgb[1]) << "," << Number(e.patch.rgb[2]) << "]"
			<< ",\"codes\":[" << e.codes.code[0] << "," << e.codes.code[1] << "," << e.codes.code[2] << "]"
			<< ",\"container\":[" << e.codes.container[0] << "," << e.codes.container[1] << "," << e.codes.container[2] << "]"
			<< ",\"device\":[" << Number(e.codes.device[0], 9) << "," << Number(e.codes.device[1], 9) << ","
			<< Number(e.codes.device[2], 9) << "]"
			<< ",\"xyz\":[" << Number(e.xyz[0], 8) << "," << Number(e.xyz[1], 8) << "," << Number(e.xyz[2], 8) << "]"
			<< ",\"reads\":" << e.reads << ",\"spread\":" << Number(e.spread, 4)
			<< ",\"floor_limited\":" << (e.floorLimited ? "true" : "false")
			<< ",\"seconds\":" << Number(e.seconds, 4)
			<< ",\"status\":" << JsonString(e.status);
		if (!e.note.empty()) out << ",\"note\":" << JsonString(e.note);
		out << "}";
		return out.str();
	}

	bool CreateFolders(const std::string& folder, std::string& error)
	{
		if (folder.empty()) return true;
		const std::wstring wide = Wide(folder);
		const DWORD attributes = GetFileAttributesW(wide.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return true;
		const std::string parent = FolderOf(folder);
		if (!parent.empty() && parent != folder && parent.back() != ':' && !CreateFolders(parent, error)) return false;
		if (!CreateDirectoryW(wide.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
		{
			error = "could not create " + folder + " (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		return true;
	}

	bool WriteTextFile(const std::string& path, const std::string& text, std::string& error)
	{
		if (!CreateFolders(FolderOf(path), error)) return false;
		std::ofstream file(Wide(path).c_str(), std::ios::binary | std::ios::trunc);
		if (!file) { error = "could not write " + path; return false; }
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!file) { error = "could not write " + path; return false; }
		return true;
	}

	bool AppendJournal(const std::string& path, const std::string& line, std::string& error)
	{
		if (!CreateFolders(FolderOf(path), error)) return false;
		std::ofstream file(Wide(path).c_str(), std::ios::binary | std::ios::app);
		if (!file) { error = "could not write " + path; return false; }
		file << line << "\n";
		return static_cast<bool>(file);
	}

	std::string BackupConfigBeside(const std::string& configPath, const RunTimestamp& stamp, std::string& error)
	{
		std::string backup = configPath + ".before-calibration." + stamp.Format() + ".bak";
		for (int n = 2; GetFileAttributesW(Wide(backup).c_str()) != INVALID_FILE_ATTRIBUTES && n < 100; ++n)
			backup = configPath + ".before-calibration." + stamp.Format() + "-" + std::to_string(n) + ".bak";
		if (!CopyFileW(Wide(configPath).c_str(), Wide(backup).c_str(), TRUE))
		{
			error = "could not back up " + configPath + " (error " + std::to_string(GetLastError()) + ")";
			return std::string();
		}
		return backup;
	}

	bool CopyConfigToRun(const RunFolder& folder, const std::string& configPath, std::string& copied, std::string& error)
	{
		if (!CreateFolders(folder.beforeDir, error)) return false;
		copied = BeforeCopyPath(folder, configPath);
		if (!CopyFileW(Wide(configPath).c_str(), Wide(copied).c_str(), TRUE))
		{
			error = "could not copy " + configPath + " to " + copied + " (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		return true;
	}

	std::string ChooseColorProfileSection(const std::vector<std::string>& sections, const std::string& active)
	{
		auto lower = [](std::string s)
		{
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		};
		if (!active.empty())
		{
			std::string name = active;
			if (lower(name).rfind("vprenderer.color.", 0) == 0) name = name.substr(17);
			else if (lower(name).rfind("color.", 0) == 0) name = name.substr(6);
			const std::string wanted = lower("vprenderer.color." + name);
			for (const std::string& s : sections)
				if (lower(s) == wanted) return s;
		}
		for (const std::string& s : sections)
			if (lower(s).rfind("vprenderer.color.", 0) == 0) return s;
		return std::string();
	}

	std::vector<ConfigEdit> CalibrationApplyEdits(const std::string& section, const RunFolder& folder,
		const bool slotsWritten[3])
	{
		std::vector<ConfigEdit> edits;
		edits.push_back({ section, "calibration_lut_enabled", "true" });
		for (const ConfigEdit& e : LutSlotEdits(section, folder, slotsWritten))
			edits.push_back(e);
		return edits;
	}

	ConfigDiff PreviewCalibrationEdits(const std::string& configPath, const std::string& active,
		const RunFolder& folder, const bool slotsWritten[3], std::string& section, std::string& error)
	{
		ConfigDiff diff;
		ConfigEditorCore::ConfigDocument document;
		std::wstring loadError;
		if (!document.Load(Wide(configPath), loadError))
		{
			error = "could not read the configuration: " + Narrow(loadError);
			diff.ok = false;
			return diff;
		}
		section = ChooseColorProfileSection(document.SectionNames(), active);
		if (section.empty())
		{
			error = "the configuration has no Color / Output profile ([vprenderer.color.<name>]) to attach the LUT to";
			diff.ok = false;
			return diff;
		}
		return ApplyKeyOnlyEdits(document, CalibrationApplyEdits(section, folder, slotsWritten));
	}

	ApplyOutcome ApplyCalibrationToConfig(const std::string& configPath, const std::string& active,
		const RunFolder& folder, const bool slotsWritten[3], const RunTimestamp& stamp)
	{
		ApplyOutcome o;
		ConfigEditorCore::ConfigDocument document;
		std::wstring loadError;
		if (!document.Load(Wide(configPath), loadError))
		{
			o.error = "could not read the configuration: " + Narrow(loadError);
			return o;
		}
		o.section = ChooseColorProfileSection(document.SectionNames(), active);
		if (o.section.empty())
		{
			o.error = "the configuration has no Color / Output profile ([vprenderer.color.<name>]) to attach the LUT to";
			return o;
		}
		o.diff = ApplyKeyOnlyEdits(document, CalibrationApplyEdits(o.section, folder, slotsWritten));
		if (!o.diff.ok)
		{
			o.error = "the calibration keys could not be set";
			return o;
		}
		std::wstring validation;
		if (!ConfigEditorCore::ValidateCandidate(document, validation))
		{
			o.error = "VP would reject the edited configuration: " + Narrow(validation);
			return o;
		}
		// Backups before the first byte changes.
		o.backupPath = BackupConfigBeside(configPath, stamp, o.error);
		if (o.backupPath.empty()) return o;
		if (!CopyConfigToRun(folder, configPath, o.runCopyPath, o.error)) return o;
		if (!o.diff.Changed())
		{
			o.ok = true;   // already set to this run (nothing to write)
			return o;
		}
		ConfigEditorCore::SaveResult saved;
		std::wstring saveError;
		if (!ConfigEditorCore::SaveSafely(document, saved, saveError))
		{
			o.error = "the configuration could not be saved: " + Narrow(saveError);
			return o;
		}
		o.ok = true;
		return o;
	}

	std::string FormatIndexEntry(const IndexEntry& e)
	{
		std::ostringstream out;
		out << "{\"display\":" << JsonString(e.display) << ",\"timestamp\":" << JsonString(e.timestamp)
			<< ",\"run\":" << JsonString(e.run) << ",\"status\":" << JsonString(e.status);
		if (e.verifyMean >= 0.0) out << ",\"verify_mean_de2000\":" << Number(e.verifyMean, 4);
		if (e.verifyMax >= 0.0) out << ",\"verify_max_de2000\":" << Number(e.verifyMax, 4);
		out << "}";
		return out.str();
	}

	bool AppendIndex(const std::string& indexPath, const IndexEntry& entry, std::string& error)
	{
		std::string text;
		{
			std::ifstream file(Wide(indexPath).c_str(), std::ios::binary);
			if (file)
				text.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		}
		const std::string item = FormatIndexEntry(entry);
		const size_t close = text.find_last_of(']');
		const size_t open = text.find('[');
		if (open == std::string::npos || close == std::string::npos || close < open)
			text = "[\n  " + item + "\n]\n";
		else
		{
			const std::string inside = TrimText(text.substr(open + 1, close - open - 1));
			text = text.substr(0, close);
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
			text += (inside.empty() ? "\n  " : ",\n  ") + item + "\n]\n";
		}
		return WriteTextFile(indexPath, text, error);
	}

	std::string FormatReportHtml(const std::string& displayName, const DisplayModel& model, const VerifyReport* verify,
		const std::vector<JournalEntry>& journal)
	{
		std::ostringstream out;
		out << "<!doctype html><html><head><meta charset=\"utf-8\"><title>VP calibration - " << Html(displayName)
			<< "</title><style>body{font-family:Segoe UI,Arial,sans-serif;margin:24px;color:#222}"
			<< "table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:3px 8px;text-align:right}"
			<< "th{background:#eee}.fail{color:#b00}.pass{color:#070}</style></head><body>";
		out << "<h1>VP calibration: " << Html(displayName) << "</h1>";
		const Vec3 white = model.NativeWhite();
		const Chromaticity wxy = XyzToXy(white);
		out << "<p>Native white " << Number(white[1], 4) << " cd/m&sup2; (x " << Number(wxy.x, 4) << ", y "
			<< Number(wxy.y, 4) << "), black " << Number(model.Black()[1], 4) << " cd/m&sup2;, additivity error "
			<< Number(model.AdditivityError() * 100.0, 3) << "%.</p>";
		if (verify)
		{
			out << "<h2>Verification</h2><p class=\"" << (verify->pass ? "pass" : "fail") << "\">dE2000 mean "
				<< Number(verify->meanDeltaE, 3) << ", 95th percentile " << Number(verify->p95DeltaE, 3) << ", max "
				<< Number(verify->maxDeltaE, 3) << " (" << Html(verify->worstLabel) << ") - "
				<< (verify->pass ? "pass" : "outside the target (mean &lt; 1.5, max &lt; 3)") << "</p>";
			out << "<table><tr><th>Patch</th><th>dE2000</th><th>Target Y</th><th>Measured Y</th></tr>";
			for (const VerifyEntry& e : verify->entries)
				out << "<tr><td style=\"text-align:left\">" << Html(e.label) << "</td><td>" << Number(e.deltaE, 3)
					<< "</td><td>" << Number(e.targetXyz[1], 4) << "</td><td>" << Number(e.measuredXyz[1], 4) << "</td></tr>";
			out << "</table>";
		}
		int measured = 0, skipped = 0, floorLimited = 0;
		for (const JournalEntry& e : journal)
		{
			if (e.phase != "measure") continue;
			if (e.status == "ok") ++measured; else ++skipped;
			if (e.floorLimited) ++floorLimited;
		}
		out << "<h2>Measurement</h2><p>" << measured << " patches measured, " << skipped << " skipped or unreadable, "
			<< floorLimited << " below the meter's low-light floor (averaged).</p>";
		out << "</body></html>\n";
		return out.str();
	}
}
