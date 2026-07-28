#pragma once

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <Windows.h>

class DebugLogRetention
{
public:
	static constexpr size_t DEFAULT_COUNT = 10;
	static constexpr size_t MIN_COUNT = 1;
	static constexpr size_t MAX_COUNT = 100;

	struct Setting
	{
		size_t count = DEFAULT_COUNT;
		std::string diagnostic;
	};

	struct RotationResult
	{
		bool activeReady = false;
		size_t retainedCount = 0;
		std::vector<std::string> diagnostics;
	};

	static Setting ResolveSetting(
		const std::string* rawValue,
		bool duplicate,
		bool configUnreadable)
	{
		Setting setting;
		if (configUnreadable)
		{
			setting.diagnostic =
				"Debug log retention: configuration unreadable; using default 10 total files";
			return setting;
		}
		if (duplicate)
		{
			setting.diagnostic =
				"Debug log retention: duplicate setting rejected; using default 10 total files";
			return setting;
		}
		if (rawValue == nullptr)
		{
			setting.diagnostic =
				"Debug log retention: setting omitted; using default 10 total files";
			return setting;
		}

		const std::string value = Trim(*rawValue);
		if (value.empty() ||
			!std::all_of(value.begin(), value.end(),
				[](unsigned char c) { return std::isdigit(c) != 0; }))
		{
			setting.diagnostic =
				"Debug log retention: invalid value '" + *rawValue +
				"'; using default 10 total files (allowed 1-100)";
			return setting;
		}

		try
		{
			const unsigned long parsed = std::stoul(value);
			if (parsed < MIN_COUNT || parsed > MAX_COUNT)
				throw std::out_of_range("retention");
			setting.count = static_cast<size_t>(parsed);
			setting.diagnostic =
				"Debug log retention: configured " +
				std::to_string(setting.count) +
				" total file(s), including the active log (startup-only)";
		}
		catch (const std::exception&)
		{
			setting.diagnostic =
				"Debug log retention: invalid value '" + *rawValue +
				"'; using default 10 total files (allowed 1-100)";
		}
		return setting;
	}

	static RotationResult Rotate(
		const std::string& activePath,
		size_t retentionCount)
	{
		RotationResult result;
		if (retentionCount < MIN_COUNT || retentionCount > MAX_COUNT)
			retentionCount = DEFAULT_COUNT;

		const PathParts parts = SplitPath(activePath);
		bool archiveActive = false;
		WIN32_FILE_ATTRIBUTE_DATA activeData = {};
		if (GetFileAttributesExA(
			activePath.c_str(), GetFileExInfoStandard, &activeData))
		{
			const ULARGE_INTEGER size = {
				activeData.nFileSizeLow, activeData.nFileSizeHigh
			};
			archiveActive = size.QuadPart > 0 && retentionCount > 1;
		}

		if (!PruneAndShiftArchives(
			parts, retentionCount, archiveActive, result.diagnostics))
			archiveActive = false;

		if (archiveActive)
		{
			const std::string archivePath = Join(parts.directory, parts.filename + ".0");
			if (MoveFileExA(activePath.c_str(), archivePath.c_str(), MOVEFILE_WRITE_THROUGH))
			{
				result.diagnostics.push_back(
					"Debug log rotation: archived " + FileName(activePath) +
					" as " + FileName(archivePath));
			}
			else
			{
				result.diagnostics.push_back(
					"Debug log rotation: could not archive '" + activePath +
					"' as '" + archivePath + "' (Windows error " +
					std::to_string(GetLastError()) +
					"); continuing in the existing file");
			}
		}

		std::ofstream active(
			activePath, std::ios::out | std::ios::app);
		if (!active.is_open())
		{
			result.diagnostics.push_back(
				"Debug log rotation: cannot open active log '" + activePath + "'");
			return result;
		}
		active.close();
		result.activeReady = true;

		bool enumerationSucceeded = false;
		std::vector<Archive> archives = EnumerateArchives(parts, &enumerationSucceeded);
		if (!enumerationSucceeded)
		{
			result.diagnostics.push_back(
				"Debug log rotation: could not enumerate archives in '" +
				parts.directory + "' (Windows error " +
				std::to_string(GetLastError()) + ")");
			result.retainedCount = 1;
			return result;
		}
		result.retainedCount = 1 + EnumerateArchives(parts).size();
		result.diagnostics.push_back(
			"Debug log rotation: active path '" + activePath +
			"', retained " + std::to_string(result.retainedCount) +
			" of " + std::to_string(retentionCount) + " allowed file(s)");
		return result;
	}

	static bool IsArchiveFileName(
		const std::string& activeFileName,
		const std::string& candidateFileName)
	{
		const PathParts parts = SplitPath(activeFileName);
		Archive archive;
		return TryParseArchive(parts, candidateFileName, archive);
	}

private:
	struct PathParts
	{
		std::string directory;
		std::string filename;
	};

	struct Archive
	{
		std::string path;
		bool indexed = false;
		size_t index = 0;
		std::string legacyTimestamp;
	};

	static std::string Trim(const std::string& value)
	{
		size_t first = 0;
		while (first < value.size() &&
			std::isspace(static_cast<unsigned char>(value[first])))
			++first;
		size_t last = value.size();
		while (last > first &&
			std::isspace(static_cast<unsigned char>(value[last - 1])))
			--last;
		return value.substr(first, last - first);
	}

	static std::string FileName(const std::string& path)
	{
		const size_t slash = path.find_last_of("\\/");
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}

	static PathParts SplitPath(const std::string& path)
	{
		PathParts parts;
		const size_t slash = path.find_last_of("\\/");
		parts.directory =
			slash == std::string::npos ? "." : path.substr(0, slash);
		parts.filename =
			slash == std::string::npos ? path : path.substr(slash + 1);
		return parts;
	}

	static std::string Join(
		const std::string& directory,
		const std::string& filename)
	{
		if (directory.empty() || directory == ".")
			return filename;
		const char last = directory.back();
		return directory + ((last == '\\' || last == '/') ? "" : "\\") +
			filename;
	}

	static bool TryParseArchive(
		const PathParts& parts,
		const std::string& candidateFileName,
		Archive& archive)
	{
		const std::string prefix = parts.filename + ".";
		if (candidateFileName.size() > prefix.size() &&
			candidateFileName.compare(0, prefix.size(), prefix) == 0)
		{
			const std::string token = candidateFileName.substr(prefix.size());
			if (!std::all_of(token.begin(), token.end(),
				[](unsigned char c) { return std::isdigit(c) != 0; }))
				return false;
			try
			{
				const unsigned long long parsed = std::stoull(token);
				if (parsed > std::numeric_limits<size_t>::max())
					return false;
				archive.indexed = true;
				archive.index = static_cast<size_t>(parsed);
				return true;
			}
			catch (const std::exception&) { return false; }
		}

		// Legacy timestamp archives are recognized only in their former exact
		// format. They count toward retention and are pruned before indexed logs.
		const size_t extension = parts.filename.find_last_of('.');
		if (extension == std::string::npos)
			return false;
		const std::string legacyPrefix =
			parts.filename.substr(0, extension) + ".";
		const std::string legacyExtension = parts.filename.substr(extension);
		if (candidateFileName.size() <= legacyPrefix.size() + legacyExtension.size() ||
			candidateFileName.compare(0, legacyPrefix.size(), legacyPrefix) != 0 ||
			candidateFileName.compare(candidateFileName.size() - legacyExtension.size(),
				legacyExtension.size(), legacyExtension) != 0)
			return false;
		const std::string legacyToken = candidateFileName.substr(
			legacyPrefix.size(), candidateFileName.size() - legacyPrefix.size() - legacyExtension.size());
		const std::string& timestamp = legacyToken;
		if (timestamp.size() < 15 || timestamp[8] != '-')
			return false;
		for (size_t index = 0; index < 15; ++index)
		{
			if (index != 8 &&
				!std::isdigit(static_cast<unsigned char>(timestamp[index])))
				return false;
		}

		archive.legacyTimestamp = timestamp.substr(0, 15);
		const unsigned int month =
			static_cast<unsigned int>(std::stoul(timestamp.substr(4, 2)));
		const unsigned int day =
			static_cast<unsigned int>(std::stoul(timestamp.substr(6, 2)));
		const unsigned int hour =
			static_cast<unsigned int>(std::stoul(timestamp.substr(9, 2)));
		const unsigned int minute =
			static_cast<unsigned int>(std::stoul(timestamp.substr(11, 2)));
		const unsigned int second =
			static_cast<unsigned int>(std::stoul(timestamp.substr(13, 2)));
		if (month < 1 || month > 12 || day < 1 || day > 31 ||
			hour > 23 || minute > 59 || second > 59)
			return false;

		if (timestamp.size() > 15)
		{
			if (timestamp[15] != '.' || timestamp.size() == 16)
				return false;
			const std::string collision = timestamp.substr(16);
			if (!std::all_of(
				collision.begin(), collision.end(),
				[](unsigned char c) { return std::isdigit(c) != 0; }))
				return false;
			if (collision.front() == '0')
				return false;
			try
			{
				std::stoul(collision);
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
		return true;
	}

	static bool PruneAndShiftArchives(const PathParts& parts, size_t retentionCount,
		bool archiveActive, std::vector<std::string>& diagnostics)
	{
		bool enumerated = false;
		std::vector<Archive> archives = EnumerateArchives(parts, &enumerated);
		if (!enumerated)
		{
			diagnostics.push_back("Debug log rotation: could not enumerate archives in '" +
				parts.directory + "' (Windows error " + std::to_string(GetLastError()) + ")");
			return false;
		}
		const size_t limit = retentionCount - 1;
		const size_t target = archiveActive && limit > 0 ? limit - 1 : limit;
		std::sort(archives.begin(), archives.end(), [](const Archive& left, const Archive& right)
		{
			if (left.indexed != right.indexed)
				return !left.indexed; // legacy files predate indexed rotation
			if (left.indexed)
				return left.index > right.index; // older indexes prune first
			return left.legacyTimestamp < right.legacyTimestamp;
		});
		while (archives.size() > target)
		{
			const Archive& oldest = archives.front();
			if (!DeleteFileA(oldest.path.c_str()))
			{
				diagnostics.push_back("Debug log rotation: could not prune '" + oldest.path +
					"' (Windows error " + std::to_string(GetLastError()) + ")");
				return false;
			}
			diagnostics.push_back("Debug log rotation: pruned " + FileName(oldest.path));
			archives.erase(archives.begin());
		}
		if (!archiveActive)
			return true;

		std::vector<Archive> indexed;
		for (const Archive& archive : archives)
			if (archive.indexed && archive.index < limit)
				indexed.push_back(archive);
		std::sort(indexed.begin(), indexed.end(), [](const Archive& left, const Archive& right)
			{ return left.index > right.index; });
		for (const Archive& archive : indexed)
		{
			const std::string destination = Join(parts.directory,
				parts.filename + "." + std::to_string(archive.index + 1));
			if (!MoveFileExA(archive.path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
			{
				diagnostics.push_back("Debug log rotation: could not shift '" + archive.path +
					"' to '" + destination + "' (Windows error " + std::to_string(GetLastError()) + ")");
				return false;
			}
		}
		return true;
	}

	static std::vector<Archive> EnumerateArchives(
		const PathParts& parts,
		bool* succeeded = nullptr)
	{
		std::vector<Archive> archives;
		if (succeeded)
			*succeeded = false;
		WIN32_FIND_DATAA data = {};
		const std::string pattern = Join(parts.directory, "*");
		HANDLE find = FindFirstFileA(pattern.c_str(), &data);
		if (find == INVALID_HANDLE_VALUE)
			return archives;

		do
		{
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				continue;
			Archive archive;
			if (TryParseArchive(parts, data.cFileName, archive))
			{
				archive.path = Join(parts.directory, data.cFileName);
				archives.push_back(archive);
			}
		}
		while (FindNextFileA(find, &data));
		FindClose(find);
		if (succeeded)
			*succeeded = true;
		return archives;
	}
};
