// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration, the outputs of a run:
// the versioned run folder, display_profile.cfg and key-only config edits.
//
//   <vp>\luts\calibration\index.json                 every run
//   <vp>\luts\calibration\<display>\<timestamp>\
//       raw.jsonl  display_profile.cfg  calibration_<slot>.cube (x3)
//       verify.json  report.html  before\ (byte copies of the config files)
//
// Paths only: nothing here creates folders or files except the text the
// caller writes. Config edits change only the named keys through
// ConfigEditorCore (SetExisting, else SetKnown); every other line is kept
// byte for byte, and the result is a diff for the user to confirm before
// SaveSafely.

#include "DisplayModel.h"
#include "LedCharacterize.h"
#include <DisplayProfile.h>

#include <string>
#include <vector>

namespace ConfigEditorCore
{
	struct ConfigDocument;
}

namespace Calibration
{
	enum class LutSlot
	{
		Bt709 = 0,
		P3D65 = 1,
		Bt2020 = 2,
	};

	const char* SlotConfigKey(LutSlot slot);   // calibration_lut_bt709 / _p3_d65 / _bt2020
	const char* SlotFileName(LutSlot slot);    // calibration_bt709.cube / ...
	Primaries SlotPrimaries(LutSlot slot);

	struct RunTimestamp
	{
		int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
		std::string Format() const;   // yyyyMMdd-HHmmss
	};

	struct RunFolder
	{
		std::string calibrationRoot;   // <vp>\luts\calibration
		std::string indexJson;         // <vp>\luts\calibration\index.json
		std::string displayDir;
		std::string runDir;
		std::string relativeRunDir;    // luts/calibration/<display>/<timestamp> (config form)
		std::string rawJournal;
		std::string displayProfile;
		std::string cube[3];
		std::string cubeConfigValue[3];   // what calibration_lut_<slot> is set to
		std::string verifyJson;
		std::string reportHtml;
		std::string beforeDir;
	};

	// Letters, digits, '-', '_' and '.' kept; anything else becomes '_'.
	std::string SanitizeDisplayName(const std::string& name);
	RunFolder BuildRunFolder(const std::string& vpRoot, const std::string& displayName, const RunTimestamp& timestamp);
	// before\<file name of configPath>
	std::string BeforeCopyPath(const RunFolder& folder, const std::string& configPath);

	struct DisplayProfileValues
	{
		double peakNits = 0.0;
		double blackNits = 0.0;
		DisplayEotf eotf = DisplayEotf::Unknown;
		DisplayGamut gamut = DisplayGamut::Unknown;
		int linkBitDepth = 0;
		bool dynamicMapping = false;
		std::string outputFingerprint;
	};

	// Effective power of the greyscale (log-log fit, 10-90%) above black.
	double EffectiveGamma(const DisplayModel& model);
	// The nearest of BT.1886 (with the measured black) / gamma 2.2 / 2.4 / sRGB.
	DisplayEotf ClassifyEotf(const DisplayModel& model);
	// The standard gamut whose primaries are nearest the measured ones.
	DisplayGamut ClassifyGamut(const DisplayModel& model);
	DisplayProfileValues ProfileFromModel(const DisplayModel& model, const CalibrationTarget& target, int linkBitDepth,
		const std::string& outputFingerprint);
	// display_profile.cfg: the reserved [display.<name>] keys (DisplayProfile.h)
	// plus the output fingerprint.
	std::string FormatDisplayProfile(const std::string& displayName, const DisplayProfileValues& values);

	struct ConfigEdit
	{
		std::string section;
		std::string key;
		std::string value;
	};

	struct ConfigDiffEntry
	{
		std::string section;
		std::string key;
		std::string oldValue;
		std::string newValue;
		bool existed = false;
		bool applied = false;
	};

	struct ConfigDiff
	{
		bool ok = true;
		std::vector<ConfigDiffEntry> entries;
		bool Changed() const;
		// One line per edit: "[section] key: old -> new" ("(absent)" when new).
		std::string Format() const;
	};

	ConfigDiff ApplyKeyOnlyEdits(ConfigEditorCore::ConfigDocument& document, const std::vector<ConfigEdit>& edits);

	// calibration_lut_* in the active colour profile for the slots written.
	std::vector<ConfigEdit> LutSlotEdits(const std::string& colorProfileSection, const RunFolder& folder,
		const bool slotsWritten[3]);

	struct LedCalibrationResults
	{
		const FluxFit* flux = nullptr;
		const LatencySummary* led = nullptr;
		const LatencySummary* cntr = nullptr;
		const CntrFit* cntrCurve = nullptr;
		double compensationEotf = 0.0;   // 0 = leave unchanged
	};

	// [projector.default] flux_table, led_latency_ms, led_settle_ms,
	// cntr_latency_ms, cntr_settle_ms, cntr_max_usable, dmd_clip_cntr and
	// [led_drive.default] compensation_eotf, for the results present.
	std::vector<ConfigEdit> LedEdits(const LedCalibrationResults& results,
		const std::string& projectorSection = "projector.default",
		const std::string& ledDriveSection = "led_drive.default");
}
