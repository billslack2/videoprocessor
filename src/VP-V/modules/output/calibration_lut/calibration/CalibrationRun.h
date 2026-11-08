// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: writing a run (design "Outputs", "Safety and
// config"). Files go to the run folder of CalibrationOutputs.h; the
// configuration is changed only on Apply, key by key, after byte-for-byte
// backups:
//   <cfg folder>\<cfg name>.before-calibration.<yyyyMMdd-HHmmss>.bak   (AGENTS.md:
//       a timestamped backup in the deployment folder before any edit)
//   <run>\before\<cfg name>                                            (the run's copy)
// then ConfigEditorCore validates the exact candidate and SaveSafely
// replaces the file atomically. Only calibration_lut_enabled and the
// calibration_lut_<slot> keys of one Color / Output profile change; every
// other byte (comments, unknown keys, order) is kept.

#include "CalibrationOutputs.h"
#include "DisplayModel.h"
#include "../pattern_lib/PatternCodes.h"

#include <string>
#include <vector>

namespace Calibration
{
	// One measured (or skipped) patch, as raw.jsonl stores it.
	struct JournalEntry
	{
		int index = 0;
		std::string phase;         // "measure", "verify", "placement"
		Patch patch;
		PatchCodes codes;
		Vec3 xyz = { 0, 0, 0 };
		int reads = 0;
		double spread = 0.0;
		bool floorLimited = false;
		double seconds = 0.0;
		std::string status;        // ok, skipped, unreadable
		std::string note;
	};

	std::string FormatJournalLine(const JournalEntry& entry);
	// Appends one line (creating folders as needed). False with error.
	bool AppendJournal(const std::string& path, const std::string& line, std::string& error);

	bool CreateFolders(const std::string& folder, std::string& error);
	bool WriteTextFile(const std::string& path, const std::string& text, std::string& error);

	// <config>.before-calibration.<stamp>.bak beside the configuration; the
	// path written, or empty with error. Never overwrites an existing file.
	std::string BackupConfigBeside(const std::string& configPath, const RunTimestamp& stamp, std::string& error);
	// A byte copy into the run's before\ folder.
	bool CopyConfigToRun(const RunFolder& folder, const std::string& configPath, std::string& copied, std::string& error);

	// The Color / Output profile section to write: `active` when the file has
	// it, else the first [vprenderer.color.*] section; empty when none.
	std::string ChooseColorProfileSection(const std::vector<std::string>& sections, const std::string& active);

	// The edits Apply makes (calibration_lut_enabled plus the slots written).
	std::vector<ConfigEdit> CalibrationApplyEdits(const std::string& colorProfileSection, const RunFolder& folder,
		const bool slotsWritten[3]);

	struct ApplyOutcome
	{
		bool ok = false;
		std::string error;
		std::string backupPath;       // beside the configuration
		std::string runCopyPath;      // in the run's before\ folder
		std::string section;
		ConfigDiff diff;
	};

	// Backs up, edits only the calibration keys, validates and saves.
	ApplyOutcome ApplyCalibrationToConfig(const std::string& configPath, const std::string& activeColorSection,
		const RunFolder& folder, const bool slotsWritten[3], const RunTimestamp& stamp);
	// The diff Apply would make, without writing anything (the review step).
	ConfigDiff PreviewCalibrationEdits(const std::string& configPath, const std::string& activeColorSection,
		const RunFolder& folder, const bool slotsWritten[3], std::string& section, std::string& error);

	struct IndexEntry
	{
		std::string display;
		std::string timestamp;
		std::string run;          // relative run folder
		std::string status;       // measured, applied, discarded, cancelled
		double verifyMean = -1.0, verifyMax = -1.0;
	};

	std::string FormatIndexEntry(const IndexEntry& entry);
	// Adds an entry to luts\calibration\index.json (a JSON array; created when absent).
	bool AppendIndex(const std::string& indexPath, const IndexEntry& entry, std::string& error);

	std::string FormatReportHtml(const std::string& displayName, const DisplayModel& model, const VerifyReport* verify,
		const std::vector<JournalEntry>& journal);
}
