/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <map>
#include <string>
#include <vector>


class ConfigFile
{
public:
	static constexpr const char* DEFAULT_FILENAME = "VideoProcessor.cfg";

	bool Load(const std::string& filename = DEFAULT_FILENAME);
	bool IsLoaded() const { return m_loaded; }
	const std::string& GetLoadedPath() const { return m_loadedPath; }
	const std::vector<std::string>& GetWarnings() const { return m_warnings; }
	bool HasSection(const std::string& section) const;
	bool TryGetString(const std::string& section, const std::string& key, std::string& value) const;
	bool TryGetBool(const std::string& section, const std::string& key, bool& value) const;
	const std::map<std::string, std::string>* GetSectionValues(const std::string& section) const;

	static std::string NormalizeName(const std::string& value);
	static std::string Trim(const std::string& value);

private:
	std::map<std::string, std::map<std::string, std::string>> m_sections;
	std::vector<std::string> m_warnings;
	std::string m_loadedPath;
	bool m_loaded = false;
};
