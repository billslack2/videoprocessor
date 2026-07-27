#pragma once

#include "ConfigFile.h"
#include "DisplayRuleExpression.h"

#include <algorithm>
#include <climits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Parsed representation of the VP-0028 renderer-profile configuration.  This
// deliberately has no renderer, GUI, or Windows dependency so startup
// validation and selection can be tested before a renderer is created.
namespace RendererProfileConfig
{
	constexpr int LatestSchemaVersion = 2;

	struct Profile
	{
		std::string group;
		std::string name;
		std::string when;
		int priority = 0;
		std::map<std::string, std::string> settings;
	};

	struct Group
	{
		std::string name;
		std::vector<std::string> profiles;
		std::string defaultSelection;
		std::string resetWhen;
		bool persistSelection = true;
	};

	struct Model
	{
		int schemaVersion = LatestSchemaVersion;
		bool persistSelection = true;
		std::vector<Group> groups;
		std::map<std::string, Profile> profiles;
	};

	struct KeySelection
	{
		std::string group;
		std::string profile;
		bool resetToAutomatic = false;
	};

	inline bool IsUnified(const ConfigFile& config)
	{
		return config.HasSection("profile_groups") || config.HasSection("general") ||
			config.HasSection("event_actions") || config.HasSection("profiles.input") ||
			config.HasSection("profiles.scaling") || config.HasSection("profiles.display") ||
			config.HasSection("profiles.viewport");
	}

	inline std::vector<std::string> SplitNames(const std::string& text)
	{
		std::vector<std::string> names;
		std::istringstream input(text);
		std::string name;
		while (std::getline(input, name, ','))
		{
			name = ConfigFile::NormalizeName(name);
			if (!name.empty()) names.push_back(name);
		}
		return names;
	}

	inline bool ParseNonNegativeInteger(const std::string& text, int& value)
	{
		try
		{
			size_t consumed = 0;
			const long parsed = std::stol(ConfigFile::Trim(text), &consumed);
			if (consumed != ConfigFile::Trim(text).size() || parsed < 0 || parsed > INT_MAX)
				return false;
			value = static_cast<int>(parsed);
			return true;
		}
		catch (const std::exception&) { return false; }
	}

	inline bool Read(const ConfigFile& config, Model& model, std::string& error)
	{
		model = {};
		error.clear();
		if (!IsUnified(config))
			return true; // Legacy remains a separate, unchanged compatibility path.

		std::string version;
		if (config.TryGetString("general", "config_version", version) &&
			(!ParseNonNegativeInteger(version, model.schemaVersion) || model.schemaVersion != LatestSchemaVersion))
		{
			error = "unified renderer configuration requires config_version=" +
				std::to_string(LatestSchemaVersion);
			return false;
		}
		bool persist = true;
		if (config.TryGetString("general", "persist_profile_selection", version) &&
			!config.TryGetBool("general", "persist_profile_selection", persist))
		{
			error = "[general] persist_profile_selection must be true or false";
			return false;
		}
		model.persistSelection = persist;

		const std::vector<std::string> expectedGroups = { "input", "scaling", "display", "viewport" };
		for (const std::string& groupName : expectedGroups)
		{
			const std::string section = "profile_groups." + groupName;
			std::string profileList;
			if (!config.TryGetString(section, "profiles", profileList))
			{
				error = "unified configuration requires " + section + " profiles=";
				return false;
			}

			Group group;
			group.name = groupName;
			group.profiles = SplitNames(profileList);
			if (group.profiles.empty())
			{
				error = section + " profiles= must not be empty";
				return false;
			}
			std::set<std::string> seen;
			for (const std::string& profileName : group.profiles)
				if (!seen.insert(profileName).second)
				{
					error = section + " profiles= contains duplicate '" + profileName + "'";
					return false;
				}

			if (!config.TryGetString(section, "default", group.defaultSelection))
			{
				error = section + " requires default=";
				return false;
			}
			group.defaultSelection = ConfigFile::NormalizeName(group.defaultSelection);
			if (group.defaultSelection != "auto" && seen.find(group.defaultSelection) == seen.end())
			{
				error = section + " default= must be auto or a listed profile";
				return false;
			}
			config.TryGetString(section, "when", group.resetWhen);
			if (!group.resetWhen.empty() && !DisplayRuleExpression::Validate(group.resetWhen, error))
				return false;
			group.persistSelection = model.persistSelection;
			if (config.TryGetString(section, "persist_profile_selection", version) &&
				!config.TryGetBool(section, "persist_profile_selection", group.persistSelection))
			{
				error = section + " persist_profile_selection must be true or false";
				return false;
			}

			for (const std::string& profileName : group.profiles)
			{
				const std::string profileSection = "profiles." + groupName + "." + profileName;
				const auto* values = config.GetSectionValues(profileSection);
				if (values == nullptr)
				{
					error = section + " lists '" + profileName + "' but [" + profileSection + "] is missing";
					return false;
				}
				Profile profile;
				profile.group = groupName;
				profile.name = profileName;
				for (const auto& value : *values)
				{
					if (value.first == "when") profile.when = value.second;
					else if (value.first == "priority")
					{
						if (!ParseNonNegativeInteger(value.second, profile.priority))
						{
							error = "[" + profileSection + "] priority must be a non-negative integer";
							return false;
						}
					}
					else profile.settings.emplace(value.first, value.second);
				}
				if (!profile.when.empty() && !DisplayRuleExpression::Validate(profile.when, error))
					return false;
				model.profiles.emplace(groupName + "." + profileName, std::move(profile));
			}
			model.groups.push_back(std::move(group));
		}
		return true;
	}

	// A physical key event is deliberately resolved across every independent
	// group.  Thus one chord may select, for example, both a display profile and
	// a viewport profile.  Ambiguity is only an error within a single group.
	inline bool SelectForKey(const Model& model, const std::string& key,
		const DisplayRuleExpression::ValueLookup& sourceValues,
		std::vector<KeySelection>& selections, std::string& error)
	{
		selections.clear();
		error.clear();
		const std::string canonicalKey = ConfigFile::NormalizeName(key);
		const DisplayRuleExpression::ValueLookup values =
			[&](const std::string& name, std::string& value)
			{
				if (name == "key") { value = canonicalKey; return true; }
				return sourceValues(name, value);
			};

		for (const Group& group : model.groups)
		{
			if (!group.resetWhen.empty())
			{
				int specificity = 0;
				const bool matchesReset = DisplayRuleExpression::Matches(
					group.resetWhen, values, specificity, error);
				if (!matchesReset && !error.empty()) return false;
				if (matchesReset)
				{
					selections.push_back({ group.name, {}, true });
					continue;
				}
			}

			std::string selected;
			for (const std::string& profileName : group.profiles)
			{
				const Profile& profile = model.profiles.at(group.name + "." + profileName);
				if (profile.when.find("$key") == std::string::npos)
					continue;
				int specificity = 0;
				const bool matchesProfile = DisplayRuleExpression::Matches(
					profile.when, values, specificity, error);
				if (!matchesProfile && !error.empty()) return false;
				if (!matchesProfile)
					continue;
				if (!selected.empty())
				{
					error = "key '" + key + "' selects both '" + selected + "' and '" +
						profileName + "' in profile group '" + group.name + "'";
					return false;
				}
				selected = profileName;
			}
			if (!selected.empty())
				selections.push_back({ group.name, selected, false });
		}
		return true;
	}
}
