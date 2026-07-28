#pragma once

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
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
		size_t retentionCount,
		std::time_t timestamp = std::time(nullptr))
	{
		RotationResult result;
		if (retentionCount < MIN_COUNT || retentionCount > MAX_COUNT)
			retentionCount = DEFAULT_COUNT;

		const PathParts parts = SplitPath(activePath);
		bool appendExisting = false;
		WIN32_FILE_ATTRIBUTE_DATA activeData = {};
		if (GetFileAttributesExA(
			activePath.c_str(), GetFileExInfoStandard, &activeData))
		{
			const ULARGE_INTEGER size = {
				activeData.nFileSizeLow, activeData.nFileSizeHigh
			};
			if (size.QuadPart > 0)
			{
				const std::string archivePath =
					FindAvailableArchivePath(parts, timestamp);
				if (!archivePath.empty() &&
					MoveFileExA(activePath.c_str(), archivePath.c_str(),
						MOVEFILE_WRITE_THROUGH))
				{
					result.diagnostics.push_back(
						"Debug log rotation: archived previous session as " +
						FileName(archivePath));
				}
				else
				{
					appendExisting = true;
					result.diagnostics.push_back(
						"Debug log rotation: could not archive active log '" +
						activePath + "' (Windows error " +
						std::to_string(GetLastError()) +
						"); continuing in the existing file");
				}
			}
		}

		std::ofstream active(
			activePath,
			std::ios::out | (appendExisting ? std::ios::app : std::ios::trunc));
		if (!active.is_open())
		{
			result.diagnostics.push_back(
				"Debug log rotation: cannot open active log '" + activePath + "'");
			return result;
		}
		active.close();
		result.activeReady = true;

		bool enumerationSucceeded = false;
		std::vector<Archive> archives =
			EnumerateArchives(parts, &enumerationSucceeded);
		if (!enumerationSucceeded)
		{
			result.diagnostics.push_back(
				"Debug log rotation: could not enumerate archives in '" +
				parts.directory + "' (Windows error " +
				std::to_string(GetLastError()) + ")");
			result.retainedCount = 1;
			return result;
		}
		std::sort(
			archives.begin(), archives.end(),
			[](const Archive& left, const Archive& right)
			{
				if (left.timestamp != right.timestamp)
					return left.timestamp < right.timestamp;
				return left.collision < right.collision;
			});

		const size_t archiveLimit = retentionCount - 1;
		size_t pruneCount =
			archives.size() > archiveLimit ? archives.size() - archiveLimit : 0;
		for (size_t index = 0; index < pruneCount; ++index)
		{
			if (DeleteFileA(archives[index].path.c_str()))
			{
				result.diagnostics.push_back(
					"Debug log rotation: pruned " +
					FileName(archives[index].path));
			}
			else
			{
				result.diagnostics.push_back(
					"Debug log rotation: could not prune '" +
					archives[index].path + "' (Windows error " +
					std::to_string(GetLastError()) + ")");
			}
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
		std::string stem;
		std::string extension;
	};

	struct Archive
	{
		std::string path;
		std::string timestamp;
		unsigned long collision = 0;
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
		const std::string filename =
			slash == std::string::npos ? path : path.substr(slash + 1);
		const size_t dot = filename.find_last_of('.');
		if (dot == std::string::npos || dot == 0)
			parts.stem = filename;
		else
		{
			parts.stem = filename.substr(0, dot);
			parts.extension = filename.substr(dot);
		}
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

	static std::string FormatTimestamp(std::time_t timestamp)
	{
		struct tm local = {};
		localtime_s(&local, &timestamp);
		std::ostringstream formatted;
		formatted << std::put_time(&local, "%Y%m%d-%H%M%S");
		return formatted.str();
	}

	static std::string FindAvailableArchivePath(
		const PathParts& parts,
		std::time_t timestamp)
	{
		const std::string timestampText = FormatTimestamp(timestamp);
		for (unsigned long collision = 0; collision < 10000; ++collision)
		{
			const std::string filename =
				parts.stem + "." + timestampText +
				(collision == 0 ? "" : "." + std::to_string(collision)) +
				parts.extension;
			const std::string path = Join(parts.directory, filename);
			if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
				return path;
		}
		return {};
	}

	static bool TryParseArchive(
		const PathParts& parts,
		const std::string& candidateFileName,
		Archive& archive)
	{
		const std::string prefix = parts.stem + ".";
		if (candidateFileName.size() <= prefix.size() + parts.extension.size() ||
			candidateFileName.compare(0, prefix.size(), prefix) != 0 ||
			candidateFileName.compare(
				candidateFileName.size() - parts.extension.size(),
				parts.extension.size(), parts.extension) != 0)
			return false;

		const size_t tokenLength =
			candidateFileName.size() - prefix.size() - parts.extension.size();
		const std::string token =
			candidateFileName.substr(prefix.size(), tokenLength);
		if (token.size() < 15 || token[8] != '-')
			return false;
		for (size_t index = 0; index < 15; ++index)
		{
			if (index != 8 &&
				!std::isdigit(static_cast<unsigned char>(token[index])))
				return false;
		}

		archive.timestamp = token.substr(0, 15);
		const unsigned int month =
			static_cast<unsigned int>(std::stoul(token.substr(4, 2)));
		const unsigned int day =
			static_cast<unsigned int>(std::stoul(token.substr(6, 2)));
		const unsigned int hour =
			static_cast<unsigned int>(std::stoul(token.substr(9, 2)));
		const unsigned int minute =
			static_cast<unsigned int>(std::stoul(token.substr(11, 2)));
		const unsigned int second =
			static_cast<unsigned int>(std::stoul(token.substr(13, 2)));
		if (month < 1 || month > 12 || day < 1 || day > 31 ||
			hour > 23 || minute > 59 || second > 59)
			return false;

		archive.collision = 0;
		if (token.size() > 15)
		{
			if (token[15] != '.' || token.size() == 16)
				return false;
			const std::string collision = token.substr(16);
			if (!std::all_of(
				collision.begin(), collision.end(),
				[](unsigned char c) { return std::isdigit(c) != 0; }))
				return false;
			if (collision.front() == '0')
				return false;
			try
			{
				archive.collision = std::stoul(collision);
			}
			catch (const std::exception&)
			{
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
