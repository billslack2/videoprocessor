/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "ConfigFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <shellapi.h>
#include <vector>


namespace
{
std::mutex g_rendererConfigurationPathMutex;
std::string g_rendererConfigurationPath;

std::string StripComment(const std::string& value)
{
	for (size_t i = 0; i < value.size(); ++i)
	{
		const char c = value[i];
		if ((c == '#' || c == ';') && (i == 0 || std::isspace(static_cast<unsigned char>(value[i - 1]))))
			return value.substr(0, i);
	}

	return value;
}

bool IsAbsolutePath(const std::string& filename)
{
	return filename.size() >= 3 && std::isalpha(static_cast<unsigned char>(filename[0])) && filename[1] == ':' &&
		(filename[2] == '\\' || filename[2] == '/');
}

std::string ParentDirectory(const std::string& path)
{
	const size_t slashPos = path.find_last_of("\\/");
	if (slashPos == std::string::npos)
		return {};

	return path.substr(0, slashPos);
}

std::vector<std::string> BuildConfigPathCandidates(const std::string& filename)
{
	std::vector<std::string> candidates;

	if (IsAbsolutePath(filename))
	{
		candidates.push_back(filename);
		return candidates;
	}

	char modulePath[MAX_PATH] = {};
	const DWORD modulePathLength = GetModuleFileNameA(nullptr, modulePath, ARRAYSIZE(modulePath));
	if (modulePathLength != 0 && modulePathLength < ARRAYSIZE(modulePath))
	{
		// The deployed configuration belongs to the executable.  This avoids a
		// shortcut's working directory silently selecting an unrelated config.
		std::string directory = ParentDirectory(modulePath);
		for (int i = 0; i < 3 && !directory.empty(); ++i)
		{
			candidates.push_back(directory + "\\" + filename);
			directory = ParentDirectory(directory);
		}
	}

	// Keep the working directory as a fallback for portable/manual launches.
	candidates.push_back(filename);

	return candidates;
}

std::string WideToNarrowPath(const wchar_t* value)
{
	if (value == nullptr || value[0] == L'\0')
		return {};

	const int required = WideCharToMultiByte(
		CP_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr);
	if (required <= 1)
		return {};

	std::string result(static_cast<size_t>(required), '\0');
	WideCharToMultiByte(
		CP_ACP, 0, value, -1, &result[0], required, nullptr, nullptr);
	result.resize(static_cast<size_t>(required - 1));
	return result;
}

std::vector<std::string> GetProcessCommandLineArguments()
{
	int argumentCount = 0;
	LPWSTR* wideArguments =
		CommandLineToArgvW(GetCommandLineW(), &argumentCount);
	if (wideArguments == nullptr)
		return {};

	std::vector<std::string> arguments;
	arguments.reserve(static_cast<size_t>(argumentCount));
	for (int index = 0; index < argumentCount; ++index)
		arguments.push_back(WideToNarrowPath(wideArguments[index]));

	LocalFree(wideArguments);
	return arguments;
}

const char* ConfigOverrideOption(const std::string& filename)
{
	if (_stricmp(filename.c_str(), ConfigFile::DEFAULT_FILENAME) == 0)
		return "--config";
	if (_stricmp(filename.c_str(), ConfigFile::RENDERER_FILENAME) == 0)
		return "--vr_config";
	return nullptr;
}
}


bool ConfigFile::Load(const std::string& filename)
{
	m_sections.clear();
	m_sectionOrder.clear();
	m_warnings.clear();
	m_loadedPath.clear();
	m_loaded = false;

	std::ifstream configFile;
	const bool rendererRequest =
		_stricmp(filename.c_str(), ConfigFile::RENDERER_FILENAME) == 0;
	const char* overrideOption = ConfigOverrideOption(filename);
	std::string selectedFilename = filename;
	std::string overridePath;
	std::string overrideError;
	bool hasOverride = false;
	if (rendererRequest)
	{
		{
			std::lock_guard<std::mutex> guard(
				g_rendererConfigurationPathMutex);
			if (!g_rendererConfigurationPath.empty())
			{
				overridePath = g_rendererConfigurationPath;
				hasOverride = true;
				overrideOption = nullptr;
			}
		}
		if (!hasOverride)
		{
			bool explicitSelection = false;
			bool compatibilityOverride = false;
			if (!TryResolveRendererConfigSelection(
				GetProcessCommandLineArguments(),
				selectedFilename,
				explicitSelection,
				compatibilityOverride,
				overrideError))
			{
				m_warnings.push_back(overrideError);
				return false;
			}
			hasOverride = explicitSelection;
			if (hasOverride)
			{
				overridePath = selectedFilename;
				overrideOption = compatibilityOverride ?
					"--vr_config" : "--config";
			}
		}
	}
	else
	{
		hasOverride = overrideOption != nullptr &&
			TryParseCommandLineOption(
				GetProcessCommandLineArguments(),
				overrideOption,
				overridePath,
				overrideError);
	}

	if (!overrideError.empty())
	{
		m_warnings.push_back(overrideError);
		return false;
	}

	const std::vector<std::string> candidates = hasOverride ?
		std::vector<std::string>{ overridePath } :
		BuildConfigPathCandidates(selectedFilename);
	for (const auto& candidate : candidates)
	{
		configFile.clear();
		configFile.open(candidate);
		if (configFile.is_open())
		{
			m_loadedPath = candidate;
			break;
		}
	}

	if (!configFile.is_open())
	{
		if (hasOverride)
		{
			m_loadedPath = overridePath;
			m_warnings.push_back(
				std::string("Cannot open ") +
				(overrideOption ? overrideOption :
					"resolved renderer configuration") +
				" file: " + overridePath);
		}
		return false;
	}

	std::string currentSection;
	std::string line;
	int lineNumber = 0;
	std::map<std::string, std::map<std::string, int>> valueLineNumbers;
	std::map<std::string, int> sectionLineNumbers;

	while (std::getline(configFile, line))
	{
		++lineNumber;
		if (lineNumber == 1 && line.size() >= 3 &&
			static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB &&
			static_cast<unsigned char>(line[2]) == 0xBF)
		{
			line.erase(0, 3);
		}

		line = Trim(StripComment(line));
		if (line.empty())
			continue;

		if (line.front() == '[' || line.back() == ']')
		{
			if (line.front() == '[' && line.back() == ']' && line.size() >= 2)
			{
				currentSection = NormalizeName(line.substr(1, line.size() - 2));
				if (currentSection.empty())
					m_warnings.push_back("Line " + std::to_string(lineNumber) + ": empty section name");
				else
				{
					const auto previous = sectionLineNumbers.find(currentSection);
					if (previous != sectionLineNumbers.end())
						m_warnings.push_back(
							"Line " + std::to_string(lineNumber) + ": duplicate section [" +
							currentSection + "] continues section from line " +
							std::to_string(previous->second));
					else
					{
						sectionLineNumbers[currentSection] = lineNumber;
						m_sectionOrder.push_back(currentSection);
					}
					// Preserve intentionally empty sections. Unified default-only
					// profiles are valid and must still participate in graph validation.
					m_sections[currentSection];
				}
			}
			else
			{
				m_warnings.push_back("Line " + std::to_string(lineNumber) + ": malformed section header");
			}
			continue;
		}

		const size_t colonPos = line.find(':');
		const size_t equalPos = line.find('=');
		const size_t separatorPos =
			colonPos == std::string::npos ? equalPos :
			(equalPos == std::string::npos ? colonPos :
				std::min(colonPos, equalPos));
		if (separatorPos == std::string::npos)
		{
			m_warnings.push_back(
				"Line " + std::to_string(lineNumber) + ": expected key: value");
			continue;
		}

		const std::string key = NormalizeName(line.substr(0, separatorPos));
		const std::string value = Trim(line.substr(separatorPos + 1));
		if (currentSection.empty())
		{
			m_warnings.push_back("Line " + std::to_string(lineNumber) + ": key outside a section");
		}
		else if (!key.empty())
		{
			auto& sectionLineNumbers = valueLineNumbers[currentSection];
			const auto previous = sectionLineNumbers.find(key);
			if (previous != sectionLineNumbers.end())
			{
				m_warnings.push_back(
					"Line " + std::to_string(lineNumber) + ": duplicate [" +
					currentSection + "] key '" + key + "' overrides line " +
					std::to_string(previous->second));
			}
			m_sections[currentSection][key] = value;
			sectionLineNumbers[key] = lineNumber;
		}
		else
		{
			m_warnings.push_back("Line " + std::to_string(lineNumber) + ": empty key");
		}
	}

	m_loaded = true;
	return true;
}


bool ConfigFile::TryResolveRendererConfigSelection(
	const std::vector<std::string>& arguments,
	std::string& filename,
	bool& explicitSelection,
	bool& compatibilityOverride,
	std::string& error)
{
	filename = DEFAULT_FILENAME;
	explicitSelection = false;
	compatibilityOverride = false;
	error.clear();

	std::string selected;
	if (TryParseCommandLineOption(
		arguments, "--vr_config", selected, error))
	{
		filename = selected;
		explicitSelection = true;
		compatibilityOverride = true;
		return true;
	}
	if (!error.empty())
		return false;

	if (TryParseCommandLineOption(
		arguments, "--config", selected, error))
	{
		filename = selected;
		explicitSelection = true;
		return true;
	}
	return error.empty();
}


void ConfigFile::SetRendererConfigurationPath(const std::string& path)
{
	std::lock_guard<std::mutex> guard(g_rendererConfigurationPathMutex);
	g_rendererConfigurationPath = path;
}


bool ConfigFile::HasSection(const std::string& section) const
{
	return m_sections.find(NormalizeName(section)) != m_sections.end();
}


bool ConfigFile::TryGetString(const std::string& section, const std::string& key, std::string& value) const
{
	auto sectionIt = m_sections.find(NormalizeName(section));
	if (sectionIt == m_sections.end())
		return false;

	auto valueIt = sectionIt->second.find(NormalizeName(key));
	if (valueIt == sectionIt->second.end())
		return false;

	value = valueIt->second;
	return true;
}


bool ConfigFile::TryGetBool(const std::string& section, const std::string& key, bool& value) const
{
	std::string rawValue;
	if (!TryGetString(section, key, rawValue))
		return false;

	const std::string normalizedValue = NormalizeName(rawValue);
	if (normalizedValue == "1" || normalizedValue == "true" || normalizedValue == "yes" || normalizedValue == "on")
	{
		value = true;
		return true;
	}

	if (normalizedValue == "0" || normalizedValue == "false" || normalizedValue == "no" || normalizedValue == "off")
	{
		value = false;
		return true;
	}

	return false;
}


const std::map<std::string, std::string>* ConfigFile::GetSectionValues(const std::string& section) const
{
	auto sectionIt = m_sections.find(NormalizeName(section));
	if (sectionIt == m_sections.end())
		return nullptr;

	return &sectionIt->second;
}


std::vector<std::string> ConfigFile::GetSectionNames() const
{
	return m_sectionOrder;
}


std::string ConfigFile::NormalizeName(const std::string& value)
{
	std::string normalized = Trim(value);
	while (!normalized.empty() && (normalized.front() == '/' || normalized.front() == '-'))
		normalized.erase(normalized.begin());

	std::transform(normalized.begin(), normalized.end(), normalized.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	return normalized;
}


std::string ConfigFile::Trim(const std::string& value)
{
	const auto first = std::find_if_not(value.begin(), value.end(),
		[](unsigned char c) { return std::isspace(c); });
	if (first == value.end())
		return {};

	const auto last = std::find_if_not(value.rbegin(), value.rend(),
		[](unsigned char c) { return std::isspace(c); }).base();

	return std::string(first, last);
}


bool ConfigFile::TryParseIndexedKey(
	const std::string& key,
	const std::string& prefix,
	unsigned int& oneBasedIndex)
{
	const std::string normalizedKey = NormalizeName(key);
	const std::string normalizedPrefix = NormalizeName(prefix) + ".";
	if (normalizedKey.compare(0, normalizedPrefix.size(), normalizedPrefix) != 0)
		return false;

	const std::string indexToken = normalizedKey.substr(normalizedPrefix.size());
	if (indexToken.empty() ||
		!std::all_of(indexToken.begin(), indexToken.end(),
			[](unsigned char character) { return std::isdigit(character) != 0; }))
		return false;

	try
	{
		const unsigned long parsed = std::stoul(indexToken);
		if (parsed == 0 || parsed > UINT_MAX)
			return false;
		oneBasedIndex = static_cast<unsigned int>(parsed);
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}


bool ConfigFile::TryParseCommandLineOption(
	const std::vector<std::string>& arguments,
	const std::string& option,
	std::string& value,
	std::string& error)
{
	value.clear();
	error.clear();

	const std::string normalizedOption = NormalizeName(option);
	bool found = false;
	for (size_t index = 0; index < arguments.size(); ++index)
	{
		const std::string& argument = arguments[index];
		if (argument.empty() ||
			(argument[0] != '-' && argument[0] != '/'))
			continue;

		const size_t equalPos = argument.find('=');
		const std::string argumentName =
			equalPos == std::string::npos ?
			argument :
			argument.substr(0, equalPos);
		if (NormalizeName(argumentName) != normalizedOption)
			continue;

		if (found)
		{
			error = "Duplicate command-line option: " + option;
			return false;
		}

		std::string parsedValue;
		if (equalPos != std::string::npos)
		{
			parsedValue = argument.substr(equalPos + 1);
		}
		else
		{
			if (index + 1 >= arguments.size() ||
				(!arguments[index + 1].empty() &&
				 (arguments[index + 1][0] == '-' ||
				  arguments[index + 1][0] == '/')))
			{
				error = "Missing value for command-line option: " + option;
				return false;
			}
			parsedValue = arguments[++index];
		}

		if (parsedValue.empty())
		{
			error = "Missing value for command-line option: " + option;
			return false;
		}

		value = parsedValue;
		found = true;
	}

	return found;
}
