// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>
#include "CalibrationOutputs.h"

#include <VideoProcessor-ConfigCore/ConfigEditorCore.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace Calibration
{
	const char* SlotConfigKey(LutSlot slot)
	{
		switch (slot)
		{
		case LutSlot::P3D65: return "calibration_lut_p3_d65";
		case LutSlot::Bt2020: return "calibration_lut_bt2020";
		default: return "calibration_lut_bt709";
		}
	}

	const char* SlotFileName(LutSlot slot)
	{
		switch (slot)
		{
		case LutSlot::P3D65: return "calibration_p3_d65.cube";
		case LutSlot::Bt2020: return "calibration_bt2020.cube";
		default: return "calibration_bt709.cube";
		}
	}

	Primaries SlotPrimaries(LutSlot slot)
	{
		switch (slot)
		{
		case LutSlot::P3D65: return Gamuts::P3D65();
		case LutSlot::Bt2020: return Gamuts::Bt2020();
		default: return Gamuts::Bt709();
		}
	}

	std::string RunTimestamp::Format() const
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%04d%02d%02d-%02d%02d%02d", year, month, day, hour, minute, second);
		return text;
	}

	std::string SanitizeDisplayName(const std::string& name)
	{
		std::string out;
		for (const char c : name)
		{
			const unsigned char u = static_cast<unsigned char>(c);
			out += (u < 0x80 && (std::isalnum(u) || c == '-' || c == '_' || c == '.')) ? c : '_';
		}
		// No leading / trailing dots or underscores (no "..", no hidden names).
		while (!out.empty() && (out.front() == '.' || out.front() == '_')) out.erase(out.begin());
		while (!out.empty() && (out.back() == '.' || out.back() == '_')) out.pop_back();
		return out.empty() ? std::string("display") : out;
	}

	namespace
	{
		std::string Join(const std::string& a, const std::string& b)
		{
			if (a.empty()) return b;
			const char last = a.back();
			return (last == '\\' || last == '/') ? a + b : a + "\\" + b;
		}
	}

	RunFolder BuildRunFolder(const std::string& vpRoot, const std::string& displayName, const RunTimestamp& timestamp)
	{
		RunFolder f;
		const std::string display = SanitizeDisplayName(displayName);
		const std::string stamp = timestamp.Format();
		f.calibrationRoot = Join(Join(vpRoot, "luts"), "calibration");
		f.indexJson = Join(f.calibrationRoot, "index.json");
		f.displayDir = Join(f.calibrationRoot, display);
		f.runDir = Join(f.displayDir, stamp);
		f.relativeRunDir = "luts/calibration/" + display + "/" + stamp;
		f.rawJournal = Join(f.runDir, "raw.jsonl");
		f.displayProfile = Join(f.runDir, "display_profile.cfg");
		for (int s = 0; s < 3; ++s)
		{
			f.cube[s] = Join(f.runDir, SlotFileName(static_cast<LutSlot>(s)));
			f.cubeConfigValue[s] = f.relativeRunDir + "/" + SlotFileName(static_cast<LutSlot>(s));
		}
		f.verifyJson = Join(f.runDir, "verify.json");
		f.reportHtml = Join(f.runDir, "report.html");
		f.beforeDir = Join(f.runDir, "before");
		return f;
	}

	std::string BeforeCopyPath(const RunFolder& folder, const std::string& configPath)
	{
		const size_t slash = configPath.find_last_of("\\/");
		const std::string name = slash == std::string::npos ? configPath : configPath.substr(slash + 1);
		return Join(folder.beforeDir, SanitizeDisplayName(name));
	}

	// ------------------------------------------------------- display profile

	double EffectiveGamma(const DisplayModel& model)
	{
		const double black = model.Black()[1];
		const double white = model.Forward({ 1, 1, 1 })[1];
		double sx = 0, sy = 0, sxx = 0, sxy = 0;
		int n = 0;
		for (int i = 1; i <= 9; ++i)
		{
			const double v = i / 10.0;
			const double rel = (model.Forward({ v, v, v })[1] - black) / (white - black);
			if (!(rel > 0.0)) continue;
			const double x = std::log(v), y = std::log(rel);
			sx += x; sy += y; sxx += x * x; sxy += x * y;
			++n;
		}
		if (n < 2) return 0.0;
		const double den = n * sxx - sx * sx;
		return den != 0.0 ? (n * sxy - sx * sy) / den : 0.0;
	}

	DisplayEotf ClassifyEotf(const DisplayModel& model)
	{
		const double lb = (std::max)(model.Black()[1], 0.0);
		const double lw = model.Forward({ 1, 1, 1 })[1];
		if (!(lw > lb)) return DisplayEotf::Unknown;
		const double q = std::pow(lw, 1.0 / 2.4) - std::pow(lb, 1.0 / 2.4);
		const double a = std::pow(q, 2.4), b = std::pow(lb, 1.0 / 2.4) / q;
		auto srgb = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
		double error[4] = { 0, 0, 0, 0 };
		for (int i = 1; i <= 19; ++i)
		{
			const double v = i / 20.0;
			const double y = (std::max)(model.Forward({ v, v, v })[1], 1e-9);
			const double candidates[4] = {
				a * std::pow(v + b, 2.4),
				lb + (lw - lb) * std::pow(v, 2.2),
				lb + (lw - lb) * std::pow(v, 2.4),
				lb + (lw - lb) * srgb(v),
			};
			for (int c = 0; c < 4; ++c)
			{
				const double e = std::log((std::max)(candidates[c], 1e-9) / y);
				error[c] += e * e;
			}
		}
		const DisplayEotf kinds[4] = { DisplayEotf::Bt1886, DisplayEotf::Gamma22, DisplayEotf::Gamma24, DisplayEotf::Srgb };
		int best = 0;
		for (int c = 1; c < 4; ++c) if (error[c] < error[best]) best = c;
		return kinds[best];
	}

	DisplayGamut ClassifyGamut(const DisplayModel& model)
	{
		const Chromaticity measured[3] = { XyzToXy(model.MeasuredPrimary(0)), XyzToXy(model.MeasuredPrimary(1)),
			XyzToXy(model.MeasuredPrimary(2)) };
		const Primaries standards[3] = { Gamuts::Bt709(), Gamuts::P3D65(), Gamuts::Bt2020() };
		const DisplayGamut kinds[3] = { DisplayGamut::Bt709, DisplayGamut::DisplayP3, DisplayGamut::Bt2020 };
		int best = 0;
		double bestDistance = 1e9;
		for (int s = 0; s < 3; ++s)
		{
			const Chromaticity p[3] = { standards[s].red, standards[s].green, standards[s].blue };
			double d = 0.0;
			for (int c = 0; c < 3; ++c)
				d += std::hypot(measured[c].x - p[c].x, measured[c].y - p[c].y);
			if (d < bestDistance) { bestDistance = d; best = s; }
		}
		return kinds[best];
	}

	DisplayProfileValues ProfileFromModel(const DisplayModel& model, const CalibrationTarget& target, int linkBitDepth,
		const std::string& outputFingerprint)
	{
		DisplayProfileValues v;
		v.peakNits = target.White()[1];
		v.blackNits = model.Black()[1];
		v.eotf = ClassifyEotf(model);
		v.gamut = ClassifyGamut(model);
		v.linkBitDepth = linkBitDepth;
		v.dynamicMapping = false;
		v.outputFingerprint = outputFingerprint;
		return v;
	}

	std::string FormatDisplayProfile(const std::string& displayName, const DisplayProfileValues& v)
	{
		std::ostringstream out;
		out.precision(6);
		out << "# VideoProcessor display profile, written by automatic calibration.\n"
			<< "# The reserved [display.<name>] keys (DisplayProfile.h) plus the output fingerprint\n"
			<< "# the calibration was made through (a mismatch at startup means: calibrate again).\n"
			<< "[display." << SanitizeDisplayName(displayName) << "]\n"
			<< "peak_nits: " << v.peakNits << "\n"
			<< "black_nits: " << v.blackNits << "\n"
			<< "eotf: " << ::ToString(v.eotf) << "\n"
			<< "gamut: " << ::ToString(v.gamut) << "\n";
		if (v.linkBitDepth > 0) out << "link_bit_depth: " << v.linkBitDepth << "\n";
		out << "dynamic_mapping: " << (v.dynamicMapping ? "on" : "off") << "\n";
		if (!v.outputFingerprint.empty())
		{
			std::string fingerprint = v.outputFingerprint;
			std::replace(fingerprint.begin(), fingerprint.end(), '\n', ' ');
			std::replace(fingerprint.begin(), fingerprint.end(), '\r', ' ');
			out << "output_fingerprint: " << fingerprint << "\n";
		}
		return out.str();
	}

	// ---------------------------------------------------------- config edits

	bool ConfigDiff::Changed() const
	{
		for (const ConfigDiffEntry& e : entries)
			if (e.applied && (!e.existed || e.oldValue != e.newValue)) return true;
		return false;
	}

	std::string ConfigDiff::Format() const
	{
		std::string out;
		for (const ConfigDiffEntry& e : entries)
		{
			out += "[" + e.section + "] " + e.key + ": " + (e.existed ? e.oldValue : std::string("(absent)")) + " -> " +
				e.newValue;
			if (!e.applied) out += "  (not applied)";
			out += "\n";
		}
		return out;
	}

	ConfigDiff ApplyKeyOnlyEdits(ConfigEditorCore::ConfigDocument& document, const std::vector<ConfigEdit>& edits)
	{
		ConfigDiff diff;
		for (const ConfigEdit& edit : edits)
		{
			ConfigDiffEntry e;
			e.section = edit.section;
			e.key = edit.key;
			e.newValue = edit.value;
			size_t line = 0, start = 0, end = 0;
			e.existed = document.Find(edit.section, edit.key, line, start, end);
			if (e.existed) e.oldValue = document.Get(edit.section.c_str(), edit.key.c_str());
			if (e.existed && e.oldValue == edit.value)
				e.applied = true;   // nothing to change: the line stays as it is
			else
				e.applied = document.SetExisting(edit.section.c_str(), edit.key.c_str(), edit.value) ||
					document.SetKnown(edit.section, edit.key.c_str(), edit.value);
			if (!e.applied) diff.ok = false;
			diff.entries.push_back(e);
		}
		return diff;
	}

	std::vector<ConfigEdit> LutSlotEdits(const std::string& colorProfileSection, const RunFolder& folder,
		const bool slotsWritten[3])
	{
		std::vector<ConfigEdit> edits;
		for (int s = 0; s < 3; ++s)
			if (slotsWritten[s])
				edits.push_back({ colorProfileSection, SlotConfigKey(static_cast<LutSlot>(s)), folder.cubeConfigValue[s] });
		return edits;
	}

	std::vector<ConfigEdit> LedEdits(const LedCalibrationResults& r, const std::string& projector,
		const std::string& ledDrive)
	{
		std::vector<ConfigEdit> edits;
		if (r.flux && r.flux->ok && r.flux->table.size() >= 2)
			edits.push_back({ projector, "flux_table", r.flux->ConfigValue() });
		if (r.led && r.led->ok)
		{
			edits.push_back({ projector, "led_latency_ms", std::to_string((std::min)((std::max)(r.led->latencyMs, 0), 2000)) });
			edits.push_back({ projector, "led_settle_ms", std::to_string((std::min)((std::max)(r.led->settleMs, 0), 5000)) });
		}
		if (r.cntr && r.cntr->ok)
		{
			edits.push_back({ projector, "cntr_latency_ms", std::to_string((std::min)((std::max)(r.cntr->latencyMs, 0), 2000)) });
			edits.push_back({ projector, "cntr_settle_ms", std::to_string((std::min)((std::max)(r.cntr->settleMs, 0), 5000)) });
		}
		if (r.cntrCurve && r.cntrCurve->ok)
		{
			edits.push_back({ projector, "cntr_max_usable", std::to_string(r.cntrCurve->cntrMaxUsable) });
			edits.push_back({ projector, "dmd_clip_cntr", std::to_string(r.cntrCurve->dmdClipCntr) });
		}
		if (r.compensationEotf >= 1.0 && r.compensationEotf <= 3.0)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.2f", r.compensationEotf);
			edits.push_back({ ledDrive, "compensation_eotf", text });
		}
		return edits;
	}
}
