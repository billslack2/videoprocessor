// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: finding the user's ArgyllCMS install. VP never ships
// or downloads Argyll; when spotread is not found the wizard shows
// ArgyllInstallGuidance (Spotread.h).
//
// Search order:
//  1. [calibration] argyll_path: the spotread.exe itself, its bin folder, or
//     the install folder that holds bin\spotread.exe;
//  2. the ARGYLL_PATH environment variable (same forms);
//  3. every folder on PATH;
//  4. the usual unzip places: %ProgramFiles%, %ProgramFiles(x86)%,
//     %LOCALAPPDATA%\Programs, the user profile and drive roots, each for a
//     folder named Argyll* (Argyll_V3.5.0, ArgyllCMS ...), the highest version
//     first.

#include "Spotread.h"

#include <functional>
#include <string>
#include <vector>

namespace Calibration
{
	struct ArgyllProbe
	{
		bool found = false;
		std::wstring spotread;        // full path of spotread.exe
		std::string searched;         // what was tried (for the guidance text)
		bool ran = false;             // spotread -? could be run
		SpotreadUsage usage;
		std::string error;            // why it could not be run / understood
		std::string Summary() const;  // one paragraph for the wizard
	};

	// The candidate spotread.exe paths for a configured value (empty = none).
	std::vector<std::wstring> CandidatesForSetting(const std::wstring& setting);
	// The version encoded in an install folder name ("Argyll_V3.5.0" -> 3,5,0);
	// false when there is none.
	bool FolderVersion(const std::wstring& folderName, int version[3]);

	struct ArgyllSearchEnvironment
	{
		std::wstring configured;                        // argyll_path
		std::wstring environment;                       // ARGYLL_PATH
		std::wstring path;                              // PATH
		std::vector<std::wstring> installRoots;         // folders that may hold Argyll* folders
		std::function<bool(const std::wstring&)> fileExists;
		// Lists sub-folder names of a root that match "Argyll*".
		std::function<std::vector<std::wstring>(const std::wstring&)> listArgyllFolders;
	};

	// The real environment (registry-free: environment variables and folders).
	ArgyllSearchEnvironment SystemSearchEnvironment(const std::wstring& configured);
	// The spotread.exe found, or empty; `searched` lists what was tried.
	std::wstring FindSpotread(const ArgyllSearchEnvironment& environment, std::string& searched);
	// Finds spotread and runs `spotread -?` (bounded) to read its version and
	// connected instruments.
	ArgyllProbe ProbeArgyll(const std::wstring& configured, int timeoutMs = 20000);
}
