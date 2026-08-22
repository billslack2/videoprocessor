#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ConfigEditorCore.h"

#include <ConfigFile.h>
#include <MainConfigSchema.h>
#include <RendererProfileConfig.h>
#include <ShaderConfigValidation.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace
{
	std::string ToNarrow(const std::wstring& value)
	{
		if (value.empty()) return {};
		const int size = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1,
			nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(size > 0 ? size : 0), '\0');
		if (size > 1)
			WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, &result[0],
				size, nullptr, nullptr);
		if (!result.empty()) result.pop_back();
		return result;
	}

	std::wstring ToWide(const std::string& value)
	{
		if (value.empty()) return {};
		const int size = MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1,
			nullptr, 0);
		std::wstring result(static_cast<size_t>(size > 0 ? size : 0), L'\0');
		if (size > 1)
			MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, &result[0],
				size);
		if (!result.empty()) result.pop_back();
		return result;
	}

	std::wstring Timestamp()
	{
		SYSTEMTIME time = {};
		GetLocalTime(&time);
		wchar_t value[32] = {};
		swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u", time.wYear,
			time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		return value;
	}

	std::wstring UniqueBackupPath(const std::wstring& path)
	{
		const std::wstring base = path + L".backup-" + Timestamp();
		if (GetFileAttributesW(base.c_str()) == INVALID_FILE_ATTRIBUTES)
			return base;
		for (unsigned int suffix = 2; suffix < 10000; ++suffix)
		{
			const std::wstring candidate = base + L"-" + std::to_wstring(suffix);
			if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES)
				return candidate;
		}
		return {};
	}

	bool WriteBytes(const std::wstring& path, const std::string& text)
	{
		std::ofstream output(ToNarrow(path),
			std::ios::binary | std::ios::trunc);
		if (!output) return false;
		output.write(text.data(), static_cast<std::streamsize>(text.size()));
		return static_cast<bool>(output);
	}

	bool ReadBytes(const std::wstring& path, std::string& text)
	{
		std::ifstream input(ToNarrow(path), std::ios::binary);
		if (!input) return false;
		text.assign(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
		return static_cast<bool>(input) || input.eof();
	}

	bool ParentDirectoryExists(const std::wstring& path)
	{
		const size_t separator = path.find_last_of(L"\\/");
		if (separator == std::wstring::npos) return true;
		const std::wstring parent = path.substr(0, separator);
		const DWORD attributes = GetFileAttributesW(parent.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	class DeleteOnExit
	{
	public:
		explicit DeleteOnExit(const std::wstring& target) : m_target(target) {}
		~DeleteOnExit()
		{
			if (!m_target.empty()) DeleteFileW(m_target.c_str());
		}

	private:
		std::wstring m_target;
	};
}

namespace ConfigEditorCore
{
	bool ConfigDocument::Load(const std::wstring& input, std::wstring& error)
	{
		path = input;
		std::ifstream inputFile(ToNarrow(path), std::ios::binary);
		if (!inputFile)
		{
			const DWORD attributes = GetFileAttributesW(path.c_str());
			const DWORD failure = attributes == INVALID_FILE_ATTRIBUTES ?
				GetLastError() : ERROR_SUCCESS;
			if (attributes == INVALID_FILE_ATTRIBUTES &&
				(failure == ERROR_FILE_NOT_FOUND ||
					failure == ERROR_PATH_NOT_FOUND) &&
				ParentDirectoryExists(path))
			{
				lines.clear();
				loadedBytes.clear();
				lineEnding = "\r\n";
				hasTerminalLineEnding = true;
				existedAtLoad = false;
				return true;
			}
			error = L"Cannot open " + path;
			return false;
		}

		const std::string text((std::istreambuf_iterator<char>(inputFile)),
			std::istreambuf_iterator<char>());
		loadedBytes = text;
		existedAtLoad = true;
		lineEnding = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
		hasTerminalLineEnding = !text.empty() &&
			(text.back() == '\r' || text.back() == '\n');
		lines.clear();
		size_t start = 0;
		while (start < text.size())
		{
			const size_t end = text.find_first_of("\r\n", start);
			lines.push_back(text.substr(start, end == std::string::npos ?
				std::string::npos : end - start));
			if (end == std::string::npos) break;
			start = end + 1;
			if (text[end] == '\r' && start < text.size() &&
				text[start] == '\n')
				++start;
		}
		return true;
	}

	std::string ConfigDocument::StripComment(const std::string& value)
	{
		for (size_t index = 0; index < value.size(); ++index)
			if ((value[index] == '#' || value[index] == ';') &&
				(index == 0 || std::isspace(
					static_cast<unsigned char>(value[index - 1]))))
				return value.substr(0, index);
		return value;
	}

	bool ConfigDocument::Find(const std::string& wantedSection,
		const std::string& wantedKey, size_t& lineIndex, size_t& valueStart,
		size_t& valueEnd) const
	{
		const std::string normalizedSection =
			ConfigFile::NormalizeName(wantedSection);
		const std::string normalizedKey = ConfigFile::NormalizeName(wantedKey);
		std::string section;
		for (size_t index = 0; index < lines.size(); ++index)
		{
			const std::string& raw = lines[index];
			const std::string text = ConfigFile::Trim(StripComment(raw));
			if (text.size() >= 2 && text.front() == '[' && text.back() == ']')
			{
				section = ConfigFile::NormalizeName(
					text.substr(1, text.size() - 2));
				continue;
			}
			if (section != normalizedSection) continue;
			const size_t colon = raw.find(':');
			const size_t equal = raw.find('=');
			const size_t separator = colon == std::string::npos ? equal :
				(equal == std::string::npos ? colon : std::min(colon, equal));
			if (separator == std::string::npos ||
				ConfigFile::NormalizeName(raw.substr(0, separator)) != normalizedKey)
				continue;
			const size_t comment = StripComment(raw).size();
			valueStart = separator + 1;
			while (valueStart < comment && std::isspace(
				static_cast<unsigned char>(raw[valueStart])))
				++valueStart;
			valueEnd = comment;
			while (valueEnd > valueStart && std::isspace(
				static_cast<unsigned char>(raw[valueEnd - 1])))
				--valueEnd;
			lineIndex = index;
			return true;
		}
		return false;
	}

	std::string ConfigDocument::Get(const char* section, const char* key) const
	{
		size_t line = 0, start = 0, end = 0;
		return Find(section, key, line, start, end) ?
			lines[line].substr(start, end - start) : std::string();
	}

	bool ConfigDocument::SetExisting(const char* section, const char* key,
		const std::string& value)
	{
		size_t line = 0, start = 0, end = 0;
		if (!Find(section, key, line, start, end)) return false;
		lines[line].replace(start, end - start, value);
		return true;
	}

	bool ConfigDocument::SetKnown(const std::string& wantedSection,
		const char* key, const std::string& value)
	{
		size_t line = 0, start = 0, end = 0;
		if (Find(wantedSection, key, line, start, end))
		{
			lines[line].replace(start, end - start, value);
			return true;
		}

		const std::string normalized = ConfigFile::NormalizeName(wantedSection);
		for (size_t index = 0; index < lines.size(); ++index)
		{
			const std::string text = ConfigFile::Trim(StripComment(lines[index]));
			if (text.size() < 2 || text.front() != '[' || text.back() != ']')
				continue;
			if (ConfigFile::NormalizeName(text.substr(1, text.size() - 2)) !=
				normalized)
				continue;
			size_t insertAt = index + 1;
			while (insertAt < lines.size())
			{
				const std::string next = ConfigFile::Trim(
					StripComment(lines[insertAt]));
				if (next.size() >= 2 && next.front() == '[' &&
					next.back() == ']')
					break;
				++insertAt;
			}
			lines.insert(lines.begin() + insertAt,
				std::string(key) + ": " + value);
			return true;
		}
		return false;
	}

	bool ConfigDocument::RemoveKnown(const std::string& wantedSection,
		const char* key)
	{
		size_t line = 0, start = 0, end = 0;
		if (!Find(wantedSection, key, line, start, end)) return false;
		lines.erase(lines.begin() + line);
		return true;
	}

	bool ConfigDocument::AddSection(const std::string& section)
	{
		const std::string normalized = ConfigFile::NormalizeName(section);
		for (const std::string& existing : SectionNamesWithPrefix(section))
			if (ConfigFile::NormalizeName(existing) == normalized) return false;
		if (!lines.empty() && !lines.back().empty()) lines.push_back({});
		lines.push_back("[" + section + "]");
		return true;
	}

	bool ConfigDocument::RemoveSection(const std::string& wantedSection)
	{
		const std::string normalized = ConfigFile::NormalizeName(wantedSection);
		for (size_t index = 0; index < lines.size(); ++index)
		{
			const std::string text = ConfigFile::Trim(StripComment(lines[index]));
			if (text.size() < 2 || text.front() != '[' || text.back() != ']')
				continue;
			if (ConfigFile::NormalizeName(text.substr(1, text.size() - 2)) !=
				normalized)
				continue;
			size_t end = index + 1;
			while (end < lines.size())
			{
				const std::string next = ConfigFile::Trim(
					StripComment(lines[end]));
				if (next.size() >= 2 && next.front() == '[' &&
					next.back() == ']')
					break;
				++end;
			}
			if (end < lines.size() && end > index + 1 &&
				lines[end - 1].empty())
				--end;
			lines.erase(lines.begin() + index, lines.begin() + end);
			return true;
		}
		return false;
	}

	bool ConfigDocument::FindSectionHeader(const std::string& wantedSection,
		size_t& lineIndex, size_t& nameStart, size_t& nameEnd) const
	{
		const std::string normalized = ConfigFile::NormalizeName(wantedSection);
		for (size_t index = 0; index < lines.size(); ++index)
		{
			const std::string text = ConfigFile::Trim(StripComment(lines[index]));
			if (text.size() < 2 || text.front() != '[' || text.back() != ']')
				continue;
			if (ConfigFile::NormalizeName(text.substr(1, text.size() - 2)) !=
				normalized)
				continue;
			const size_t open = lines[index].find('[');
			const size_t close = lines[index].find(']', open);
			if (open == std::string::npos || close == std::string::npos)
				return false;
			lineIndex = index;
			nameStart = open + 1;
			nameEnd = close;
			return true;
		}
		return false;
	}

	bool ConfigDocument::RenameSection(const std::string& oldSection,
		const std::string& newSection)
	{
		const std::string oldName = ConfigFile::NormalizeName(oldSection);
		const std::string newName = ConfigFile::NormalizeName(newSection);
		if (oldName.empty() || newName.empty()) return false;
		if (oldName != newName)
			for (const std::string& existing : SectionNamesWithPrefix(newName))
				if (ConfigFile::NormalizeName(existing) == newName) return false;
		size_t line = 0, start = 0, end = 0;
		if (!FindSectionHeader(oldName, line, start, end)) return false;
		lines[line].replace(start, end - start, newSection);
		return true;
	}

	bool ConfigDocument::SwapSectionHeaders(const std::string& first,
		const std::string& second)
	{
		if (ConfigFile::NormalizeName(first) == ConfigFile::NormalizeName(second))
			return true;
		size_t firstLine = 0, firstStart = 0, firstEnd = 0;
		size_t secondLine = 0, secondStart = 0, secondEnd = 0;
		if (!FindSectionHeader(first, firstLine, firstStart, firstEnd) ||
			!FindSectionHeader(second, secondLine, secondStart, secondEnd))
			return false;
		lines[firstLine].replace(firstStart, firstEnd - firstStart, second);
		lines[secondLine].replace(secondStart, secondEnd - secondStart, first);
		return true;
	}

	bool ConfigDocument::MoveSectionBefore(const std::string& wantedSection,
		const std::string& beforeSection)
	{
		if (ConfigFile::NormalizeName(wantedSection) ==
			ConfigFile::NormalizeName(beforeSection))
			return true;
		size_t start = 0, nameStart = 0, nameEnd = 0;
		if (!FindSectionHeader(wantedSection, start, nameStart, nameEnd))
			return false;
		size_t beforeLine = 0;
		if (!FindSectionHeader(beforeSection, beforeLine, nameStart, nameEnd))
			return false;
		size_t end = start + 1;
		while (end < lines.size())
		{
			const std::string next = ConfigFile::Trim(StripComment(lines[end]));
			if (next.size() >= 2 && next.front() == '[' && next.back() == ']')
				break;
			++end;
		}
		std::vector<std::string> block(lines.begin() + start,
			lines.begin() + end);
		if (beforeLine > start) beforeLine -= end - start;
		lines.erase(lines.begin() + start, lines.begin() + end);
		lines.insert(lines.begin() + beforeLine, block.begin(), block.end());
		return true;
	}

	bool ConfigDocument::MoveSectionAfter(const std::string& wantedSection,
		const std::string& afterSection)
	{
		if (ConfigFile::NormalizeName(wantedSection) ==
			ConfigFile::NormalizeName(afterSection))
			return true;
		size_t start = 0, nameStart = 0, nameEnd = 0;
		size_t afterLine = 0, afterNameStart = 0, afterNameEnd = 0;
		if (!FindSectionHeader(wantedSection, start, nameStart, nameEnd) ||
			!FindSectionHeader(afterSection, afterLine, afterNameStart,
				afterNameEnd))
			return false;
		size_t end = start + 1;
		while (end < lines.size())
		{
			const std::string next = ConfigFile::Trim(StripComment(lines[end]));
			if (next.size() >= 2 && next.front() == '[' && next.back() == ']')
				break;
			++end;
		}
		std::vector<std::string> block(lines.begin() + start,
			lines.begin() + end);
		lines.erase(lines.begin() + start, lines.begin() + end);

		afterLine = 0;
		if (!FindSectionHeader(afterSection, afterLine, nameStart, nameEnd))
			return false;
		size_t insertAt = afterLine + 1;
		while (insertAt < lines.size())
		{
			const std::string next = ConfigFile::Trim(
				StripComment(lines[insertAt]));
			if (next.size() >= 2 && next.front() == '[' && next.back() == ']')
				break;
			++insertAt;
		}
		lines.insert(lines.begin() + insertAt, block.begin(), block.end());
		return true;
	}

	std::vector<std::string> ConfigDocument::SectionNamesWithPrefix(
		const std::string& prefix) const
	{
		std::vector<std::string> result;
		const std::string normalizedPrefix = ConfigFile::NormalizeName(prefix);
		for (const std::string& raw : lines)
		{
			const std::string text = ConfigFile::Trim(StripComment(raw));
			if (text.size() < 2 || text.front() != '[' || text.back() != ']')
				continue;
			const std::string section = text.substr(1, text.size() - 2);
			const std::string normalizedSection =
				ConfigFile::NormalizeName(section);
			if (normalizedSection == normalizedPrefix ||
				normalizedSection.rfind(normalizedPrefix + ".", 0) == 0)
				result.push_back(section);
		}
		return result;
	}

	std::vector<std::string> ConfigDocument::SectionNames() const
	{
		std::vector<std::string> result;
		for (const std::string& raw : lines)
		{
			const std::string text = ConfigFile::Trim(StripComment(raw));
			if (text.size() >= 2 && text.front() == '[' && text.back() == ']')
				result.push_back(text.substr(1, text.size() - 2));
		}
		return result;
	}

	std::vector<std::pair<std::string, std::string>>
	ConfigDocument::SectionSettings(const std::string& wantedSection) const
	{
		std::vector<std::pair<std::string, std::string>> result;
		bool inside = false;
		for (const std::string& raw : lines)
		{
			const std::string text = ConfigFile::Trim(StripComment(raw));
			if (text.size() >= 2 && text.front() == '[' && text.back() == ']')
			{
				inside = ConfigFile::NormalizeName(
					text.substr(1, text.size() - 2)) ==
					ConfigFile::NormalizeName(wantedSection);
				continue;
			}
			if (!inside || text.empty()) continue;
			const size_t colon = text.find(':');
			if (colon == std::string::npos) continue;
			const std::string key = ConfigFile::Trim(text.substr(0, colon));
			if (!key.empty())
				result.emplace_back(key,
					ConfigFile::Trim(text.substr(colon + 1)));
		}
		return result;
	}

	std::string ConfigDocument::Serialize() const
	{
		std::ostringstream output;
		for (size_t index = 0; index < lines.size(); ++index)
		{
			output << lines[index];
			if (index + 1 < lines.size() || hasTerminalLineEnding)
				output << lineEnding;
		}
		return output.str();
	}

	bool ValidateCandidate(const ConfigDocument& document, std::wstring& error)
	{
		if (document.path.empty())
		{
			error = L"A configuration path is required.";
			return false;
		}
		const std::wstring temporary = document.path + L".vpconfig-validate.tmp";
		DeleteOnExit removeTemporary(temporary);
		if (!WriteBytes(temporary, document.Serialize()))
		{
			error = L"Cannot create a validation copy beside the configuration.";
			return false;
		}

		ConfigFile config;
		if (!config.Load(ToNarrow(temporary)))
		{
			error = L"The candidate configuration could not be read.";
			return false;
		}
		if (!config.GetWarnings().empty())
		{
			error = ToWide(config.GetWarnings().front());
			return false;
		}
		std::string schemaError;
		RendererProfileConfig::Model rendererModel;
		if (!MainConfigSchema::Validate(config, schemaError) ||
			!RendererProfileConfig::Read(config, rendererModel, schemaError) ||
			!ShaderConfigValidation::Validate(config, schemaError))
		{
			error = ToWide(schemaError);
			return false;
		}
		return true;
	}

	bool SaveSafely(ConfigDocument& document, SaveResult& result,
		std::wstring& error, bool overwriteExternalChanges)
	{
		result = {};
		if (!ValidateCandidate(document, error)) return false;
		bool creatingConfiguration = !document.existedAtLoad;
		auto backupExistingConfiguration = [&document, &result, &error]()
		{
			const std::wstring backup = UniqueBackupPath(document.path);
			if (backup.empty())
			{
				error = L"Could not choose a unique backup name. The configuration "
					L"was not changed.";
				return false;
			}
			if (!CopyFileW(document.path.c_str(), backup.c_str(), TRUE))
			{
				error = L"Could not create a backup. The configuration was not changed.";
				return false;
			}
			result.backupPath = backup;
			return true;
		};

		if (document.existedAtLoad)
		{
			std::string currentBytes;
			if (!ReadBytes(document.path, currentBytes))
			{
				error = L"Could not re-read the configuration before saving. "
					L"The configuration was not changed.";
				return false;
			}
			if (currentBytes != document.loadedBytes && !overwriteExternalChanges)
			{
				error = L"The configuration changed outside this editor after it was "
					L"loaded. Reload and review those changes before saving.";
				return false;
			}

			if (!backupExistingConfiguration()) return false;
		}
		else
		{
			const DWORD attributes = GetFileAttributesW(document.path.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES)
			{
				if (!overwriteExternalChanges)
				{
					error = L"The configuration was created outside this editor after it "
						L"was opened. Reload and review it before saving.";
					return false;
				}
				creatingConfiguration = false;
				if (!backupExistingConfiguration()) return false;
			}
			const DWORD failure = GetLastError();
			if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND)
			{
				error = L"Could not verify that the new configuration path is unused.";
				return false;
			}
		}

		const std::wstring temporary = document.path + L".vpconfig-write.tmp";
		DeleteOnExit removeTemporary(temporary);
		if (!WriteBytes(temporary, document.Serialize()))
		{
			error = L"Could not write the temporary configuration. The backup was retained.";
			return false;
		}
		if (!MoveFileExW(temporary.c_str(), document.path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			error = creatingConfiguration ?
				L"Could not create the configuration from the validated temporary file." :
				L"Could not replace the configuration. Backup retained at:\n" +
					result.backupPath;
			return false;
		}
		document.loadedBytes = document.Serialize();
		document.existedAtLoad = true;
		return true;
	}
}
