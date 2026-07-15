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
#include <vector>


namespace
{
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
	candidates.push_back(filename);

	if (IsAbsolutePath(filename))
		return candidates;

	char modulePath[MAX_PATH] = {};
	const DWORD modulePathLength = GetModuleFileNameA(nullptr, modulePath, ARRAYSIZE(modulePath));
	if (modulePathLength == 0 || modulePathLength >= ARRAYSIZE(modulePath))
		return candidates;

	std::string directory = ParentDirectory(modulePath);
	for (int i = 0; i < 3 && !directory.empty(); ++i)
	{
		candidates.push_back(directory + "\\" + filename);
		directory = ParentDirectory(directory);
	}

	return candidates;
}
}


bool ConfigFile::Load(const std::string& filename)
{
	m_sections.clear();
	m_warnings.clear();
	m_loadedPath.clear();
	m_loaded = false;

	std::ifstream configFile;
	for (const auto& candidate : BuildConfigPathCandidates(filename))
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
		return false;

	std::string currentSection;
	std::string line;
	int lineNumber = 0;

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
			}
			else
			{
				m_warnings.push_back("Line " + std::to_string(lineNumber) + ": malformed section header");
			}
			continue;
		}

		const size_t equalPos = line.find('=');
		if (equalPos == std::string::npos)
		{
			m_warnings.push_back("Line " + std::to_string(lineNumber) + ": expected key=value");
			continue;
		}

		const std::string key = NormalizeName(line.substr(0, equalPos));
		const std::string value = Trim(line.substr(equalPos + 1));
		if (currentSection.empty())
		{
			m_warnings.push_back("Line " + std::to_string(lineNumber) + ": key outside a section");
		}
		else if (!key.empty())
		{
			m_sections[currentSection][key] = value;
		}
		else
		{
			m_warnings.push_back("Line " + std::to_string(lineNumber) + ": empty key");
		}
	}

	m_loaded = true;
	return true;
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
