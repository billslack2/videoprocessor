#pragma once

#include "ConfigFile.h"
#include "DisplayRuleExpression.h"

#include <algorithm>
#include <climits>
#include <cmath>
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
		int eventActionDelaySeconds = 5;
		struct EventAction
		{
			std::string name;
			std::vector<std::string> events;
			std::string when;
			std::string program;
			std::string arguments;
			std::string workingDirectory;
			int delaySeconds = 5;
		};
		std::vector<EventAction> actions;
	};

	struct KeySelection
	{
		std::string group;
		std::string profile;
		bool resetToAutomatic = false;
	};

	struct AutomaticSelection
	{
		std::string group;
		std::string profile;
		bool configuredDefault = false;
	};

	inline bool ExpressionDeclaresKeyChord(const std::string& expression,
		const std::string& key, bool& declares, std::string& error)
	{
		declares = false;
		const std::string canonicalKey = ConfigFile::NormalizeName(key);
		for (size_t position = 0; position < expression.size(); ++position)
		{
			if (expression[position] != '$') continue;
			const size_t nameStart = ++position;
			while (position < expression.size() &&
				(std::isalnum(static_cast<unsigned char>(expression[position])) || expression[position] == '_')) ++position;
			if (ConfigFile::NormalizeName(expression.substr(nameStart, position - nameStart)) != "key") continue;
			while (position < expression.size() && std::isspace(static_cast<unsigned char>(expression[position]))) ++position;
			if (position + 1 >= expression.size() || expression[position] != '=' || expression[position + 1] != '=')
			{
				error = "$key must use == with a quoted chord";
				return false;
			}
			position += 2;
			while (position < expression.size() && std::isspace(static_cast<unsigned char>(expression[position]))) ++position;
			if (position >= expression.size() || (expression[position] != '\'' && expression[position] != '"'))
			{
				error = "$key chord must be quoted";
				return false;
			}
			const char quote = expression[position++];
			const size_t chordStart = position;
			while (position < expression.size() && expression[position] != quote) ++position;
			if (position >= expression.size() || position == chordStart)
			{
				error = "$key chord is unterminated or empty";
				return false;
			}
			if (ConfigFile::NormalizeName(expression.substr(chordStart, position - chordStart)) == canonicalKey)
				declares = true;
		}
		return true;
	}

	inline bool ValidateExpressionVariables(const std::string& expression,
		const std::set<std::string>& allowed, const std::string& context,
		std::string& error)
	{
		for (size_t position = 0; position < expression.size(); ++position)
		{
			if (expression[position] != '$') continue;
			const size_t start = ++position;
			while (position < expression.size() &&
				(std::isalnum(static_cast<unsigned char>(expression[position])) ||
				 expression[position] == '_')) ++position;
			const std::string variable =
				ConfigFile::NormalizeName(expression.substr(start, position - start));
			if (allowed.find(variable) == allowed.end())
			{
				error = context + " cannot use variable '$" + variable + "'";
				return false;
			}
		}
		return true;
	}

	inline bool IsUnified(const ConfigFile& config)
	{
		if (config.HasSection("profile_groups") || config.HasSection("general") ||
			config.HasSection("event_actions") || config.HasSection("profiles.input") ||
			config.HasSection("profiles.scaling") || config.HasSection("profiles.display") ||
			config.HasSection("profiles.viewport"))
			return true;
		for (const std::string& section : config.GetSectionNames())
			if (section.rfind("profile_groups.", 0) == 0 ||
				section.rfind("profiles.", 0) == 0 ||
				section.rfind("event_actions.", 0) == 0)
				return true;
		return false;
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

	inline bool ParseInteger(const std::string& text, int minimum, int maximum, int& value)
	{
		try
		{
			const std::string trimmed = ConfigFile::Trim(text);
			size_t consumed = 0;
			const long parsed = std::stol(trimmed, &consumed);
			if (consumed != trimmed.size() || parsed < minimum || parsed > maximum) return false;
			value = static_cast<int>(parsed);
			return true;
		}
		catch (const std::exception&) { return false; }
	}

	inline bool IsIdentifier(const std::string& value)
	{
		if (value.empty() || value.size() > 64 ||
			!std::isalpha(static_cast<unsigned char>(value.front()))) return false;
		return std::all_of(value.begin() + 1, value.end(), [](unsigned char c)
			{ return std::isalnum(c) || c == '_' || c == '-'; });
	}

	inline bool IsChoice(const std::string& value,
		std::initializer_list<const char*> choices)
	{
		const std::string normalized = ConfigFile::NormalizeName(value);
		for (const char* choice : choices)
			if (normalized == choice) return true;
		return false;
	}

	inline bool IsBoolean(const std::string& value)
	{
		return IsChoice(value, { "1", "0", "true", "false", "yes", "no", "on", "off" });
	}

	inline bool IsNumberInRange(const std::string& text, double minimum,
		double maximum, bool maximumInclusive = true)
	{
		double value = 0.0;
		if (!DisplayRuleExpression::ParseNumber(ConfigFile::Trim(text), value)) return false;
		return value >= minimum && (maximumInclusive ? value <= maximum : value < maximum);
	}

	inline bool IsAspectInRange(const std::string& text, double minimum, double maximum)
	{
		const std::string value = ConfigFile::Trim(text);
		const size_t colon = value.find(':');
		if (colon == std::string::npos) return IsNumberInRange(value, minimum, maximum);
		double numerator = 0.0, denominator = 0.0;
		return DisplayRuleExpression::ParseNumber(value.substr(0, colon), numerator) &&
			DisplayRuleExpression::ParseNumber(value.substr(colon + 1), denominator) &&
			denominator > 0.0 && numerator / denominator >= minimum &&
			numerator / denominator <= maximum;
	}

	inline bool ValidateProfileSetting(const std::string& group, const std::string& key,
		const std::string& value, std::string& expected)
	{
		if (group == "input")
		{
			if (key == "tone_mapping") return IsChoice(value, { "auto", "spline", "bt2390", "st2094-40", "reinhard" });
			if (key == "gamut_mapping") return IsChoice(value, { "auto", "perceptual", "softclip", "relative", "desaturate" });
			if (key == "peak_detection") return IsChoice(value, { "auto", "off", "default", "high_quality", "on" });
			if (key == "contrast_recovery") return IsChoice(value, { "auto" }) || IsNumberInRange(value, 0.0, 1.0);
			if (key == "sdr_input_transfer") return IsChoice(value, { "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
			expected = "an input-owned setting"; return false;
		}
		if (group == "scaling")
		{
			if (key == "quality") return IsChoice(value, { "fast", "balanced", "high" });
			if (key == "upscaler") return IsChoice(value, { "auto", "ewa_lanczossharp", "ewa_lanczos", "bicubic", "bilinear" });
			if (key == "downscaler") return IsChoice(value, { "auto", "ewa_lanczos", "bicubic", "bilinear" });
			if (key == "sigmoid" || key == "dithering") return IsChoice(value, { "auto", "on", "off" });
			if (key == "deband_strength") return IsChoice(value, { "off", "light", "default" });
			expected = "a scaling-owned setting"; return false;
		}
		if (group == "display")
		{
			if (key == "sdr_target_nits" || key == "lut_reference_nits") return IsNumberInRange(value, 40.0, 500.0);
			if (key == "sdr_black_nits") return IsChoice(value, { "auto" }) || IsNumberInRange(value, 0.0, 500.0, false);
			if (key == "output_presentation") return IsChoice(value, { "auto", "composed", "direct" });
			if (key == "output_range" || key == "lut_reference_range") return IsChoice(value, { "auto", "full", "limited" });
			if (key == "output_gamma" || key == "lut_reference_transfer") return IsChoice(value, { "auto", "bt1886", "srgb", "1.8", "2.0", "2.2", "2.4", "2.6", "2.8" });
			if (key == "sdr_target_primaries" || key == "lut_reference_primaries") return IsChoice(value, { "rec709", "bt2020" });
			if (key == "report_bt2020_to_display") return IsBoolean(value);
			if (key == "lut")
			{
				const std::string normalized = ConfigFile::NormalizeName(value);
				return normalized.empty() || (normalized.size() > 5 && normalized.substr(normalized.size() - 5) == ".cube");
			}
			expected = "a display-owned setting"; return false;
		}
		if (group == "viewport")
		{
			if (key == "mode") return IsChoice(value, { "normal", "scope" });
			if (key == "scope_screen_aspect") return IsAspectInRange(value, 1.5, 4.0);
			if (key == "scope_subtitle_fit") return IsBoolean(value);
			if (key == "scope_subtitle_hold_seconds") return IsNumberInRange(value, 0.0, 30.0);
			if (key == "scope_subtitle_padding_pixels")
			{
				int parsed = 0; return ParseInteger(value, 0, 500, parsed);
			}
			expected = "a viewport-owned setting"; return false;
		}
		expected = "a known setting"; return false;
	}

	inline bool ValidateBaseSetting(const std::string& key, const std::string& value)
	{
		std::string ignored;
		for (const char* group : { "input", "scaling", "display", "viewport" })
			if (ValidateProfileSetting(group, key, value, ignored)) return true;
		if (key == "switch_refresh_rate" || key == "output_diagnostics" ||
			key == "diagnostic_disable_shader_cache") return IsBoolean(value);
		if (key == "deband") return IsChoice(value, { "auto", "on", "off" });
		if (key == "default_screen_profile") return IsChoice(value, { "normal", "scope" });
		return false;
	}

	inline bool Read(const ConfigFile& config, Model& model, std::string& error)
	{
		model = {};
		error.clear();
		if (!IsUnified(config))
			return true; // Legacy remains a separate, unchanged compatibility path.

		if (!config.GetWarnings().empty())
		{
			error = "unified renderer configuration is not strict: " + config.GetWarnings().front();
			return false;
		}

		for (const char* legacySection : { "display_rules", "shortcuts", "refresh_rate_commands" })
			if (config.HasSection(legacySection))
			{
				error = "unified renderer configuration cannot include legacy [" +
					std::string(legacySection) + "]";
				return false;
			}

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
		for (const char* booleanKey : {
			"switch_refresh_rate", "output_diagnostics",
			"diagnostic_disable_shader_cache" })
		{
			bool ignored = false;
			if (config.TryGetString("general", booleanKey, version) &&
				!config.TryGetBool("general", booleanKey, ignored))
			{
				error = "[general] " + std::string(booleanKey) +
					" must be true or false";
				return false;
			}
		}
		if (config.TryGetString("general", "event_action_delay_seconds", version) &&
			!ParseInteger(version, 0, 30, model.eventActionDelaySeconds))
		{
			error = "[general] event_action_delay_seconds must be a whole number from 0 to 30";
			return false;
		}
		if (const auto* general = config.GetSectionValues("general"))
			for (const auto& value : *general)
				if (value.first != "config_version" && value.first != "persist_profile_selection" &&
					value.first != "switch_refresh_rate" && value.first != "event_action_delay_seconds" &&
					value.first != "output_diagnostics" && value.first != "diagnostic_disable_shader_cache")
				{
					error = "[general] unknown key '" + value.first + "'";
					return false;
				}
		if (const auto* display = config.GetSectionValues("display"))
		{
			const std::set<std::string> baseKeys = {
				"sdr_target_nits", "sdr_black_nits", "switch_refresh_rate",
				"quality", "tone_mapping", "gamut_mapping", "peak_detection",
				"contrast_recovery", "upscaler", "downscaler", "deband",
				"deband_strength", "sigmoid", "dithering", "output_presentation",
				"output_range", "output_gamma",
				"sdr_target_primaries", "report_bt2020_to_display",
				"sdr_input_transfer", "output_diagnostics",
				"diagnostic_disable_shader_cache", "scope_screen_aspect",
				"default_screen_profile", "scope_subtitle_fit",
				"scope_subtitle_hold_seconds", "scope_subtitle_padding_pixels"
			};
			for (const auto& value : *display)
				if (baseKeys.find(value.first) == baseKeys.end())
				{
					error = "[display] unknown key '" + value.first + "'";
					return false;
				}
				else if (!ValidateBaseSetting(value.first, value.second))
				{
					error = "[display] key '" + value.first +
						"' has invalid value '" + value.second + "'";
					return false;
				}
		}

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
			{
				if (!IsIdentifier(profileName) || profileName == "auto" || profileName == "none" ||
					profileName == "default")
				{
					error = section + " contains invalid or reserved profile identifier '" + profileName + "'";
					return false;
				}
				if (!seen.insert(profileName).second)
				{
					error = section + " profiles= contains duplicate '" + profileName + "'";
					return false;
				}
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
			if (!group.resetWhen.empty())
			{
				if (!DisplayRuleExpression::Validate(group.resetWhen, error) ||
					!ValidateExpressionVariables(group.resetWhen, { "key" },
						"[" + section + "] when=", error))
					return false;
			}
			group.persistSelection = model.persistSelection;
			if (config.TryGetString(section, "persist_profile_selection", version) &&
				!config.TryGetBool(section, "persist_profile_selection", group.persistSelection))
			{
				error = section + " persist_profile_selection must be true or false";
				return false;
			}
			if (const auto* groupValues = config.GetSectionValues(section))
				for (const auto& value : *groupValues)
					if (value.first != "profiles" && value.first != "default" &&
						value.first != "when" && value.first != "persist_profile_selection")
					{
						error = "[" + section + "] unknown key '" + value.first + "'";
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
						if (!ParseInteger(value.second, -100000, 100000, profile.priority))
						{
							error = "[" + profileSection + "] priority must be an integer from -100000 to 100000";
							return false;
						}
					}
					else
					{
						std::string expected;
						if (!ValidateProfileSetting(groupName, value.first, value.second, expected))
						{
							error = "[" + profileSection + "] key '" + value.first +
								"' has invalid value '" + value.second + "'; expected " + expected;
							return false;
						}
						profile.settings.emplace(value.first, value.second);
					}
				}
				if (!profile.when.empty() &&
					(!DisplayRuleExpression::Validate(profile.when, error) ||
					 !ValidateExpressionVariables(profile.when,
						{ "eotf", "transfer", "colorspace", "primaries", "format",
						  "hdr_metadata", "interlaced", "source_rate", "width",
						  "height", "resolution", "key" },
						"[" + profileSection + "] when=", error)))
					return false;
				model.profiles.emplace(groupName + "." + profileName, std::move(profile));
			}
			model.groups.push_back(std::move(group));
		}

		std::set<std::string> expectedSections = { "display", "general", "event_actions" };
		for (const Group& group : model.groups)
		{
			expectedSections.insert("profile_groups." + group.name);
			for (const std::string& profile : group.profiles)
				expectedSections.insert("profiles." + group.name + "." + profile);
		}

		std::string actionList;
		if (config.TryGetString("event_actions", "actions", actionList))
		{
			const std::vector<std::string> names = SplitNames(actionList);
			std::set<std::string> seenActions;
			for (const std::string& name : names)
			{
				if (!IsIdentifier(name) || !seenActions.insert(name).second)
				{
					error = "[event_actions] contains invalid or duplicate action '" + name + "'";
					return false;
				}
				const std::string section = "event_actions." + name;
				expectedSections.insert(section);
				const auto* values = config.GetSectionValues(section);
				if (!values) { error = "[" + section + "] is missing"; return false; }
				Model::EventAction action;
				action.name = name;
				action.delaySeconds = model.eventActionDelaySeconds;
				std::string events;
				if (!config.TryGetString(section, "on", events) ||
					(action.events = SplitNames(events)).empty())
				{
					error = "[" + section + "] requires non-empty on="; return false;
				}
				for (const std::string& event : action.events)
					if (event != "refresh.applied" && event != "refresh.confirmed" &&
						event != "refresh.restored")
					{
						error = "[" + section + "] unsupported event '" + event + "'"; return false;
					}
				if (!config.TryGetString(section, "when", action.when) ||
					!DisplayRuleExpression::Validate(action.when, error) ||
					!ValidateExpressionVariables(action.when,
						{ "actual_refresh", "requested_refresh", "previous_refresh" },
						"[" + section + "] when=", error))
				{
					if (error.empty()) error = "[" + section + "] requires when="; return false;
				}
				if (!config.TryGetString(section, "program", action.program) ||
					ConfigFile::Trim(action.program).empty())
				{
					error = "[" + section + "] requires program="; return false;
				}
				const std::string program = ConfigFile::NormalizeName(action.program);
				if (program.size() < 4 ||
					(program.substr(program.size() - 4) != ".exe" &&
					 program.substr(program.size() - 4) != ".bat" &&
					 program.substr(program.size() - 4) != ".cmd"))
				{
					error = "[" + section + "] program must end in .exe, .bat, or .cmd"; return false;
				}
				config.TryGetString(section, "arguments", action.arguments);
				config.TryGetString(section, "working_directory", action.workingDirectory);
				std::string delay;
				if (config.TryGetString(section, "delay_seconds", delay) &&
					!ParseInteger(delay, 0, 30, action.delaySeconds))
				{
					error = "[" + section + "] delay_seconds must be a whole number from 0 to 30"; return false;
				}
				for (const auto& value : *values)
					if (value.first != "on" && value.first != "when" && value.first != "program" &&
						value.first != "arguments" && value.first != "working_directory" &&
						value.first != "delay_seconds")
					{
						error = "[" + section + "] unknown key '" + value.first + "'"; return false;
					}
				model.actions.push_back(std::move(action));
			}
		}
		if (const auto* eventValues = config.GetSectionValues("event_actions"))
			for (const auto& value : *eventValues)
				if (value.first != "actions")
				{
					error = "[event_actions] unknown key '" + value.first + "'"; return false;
				}
		for (const std::string& section : config.GetSectionNames())
			if (expectedSections.find(section) == expectedSections.end())
			{
				error = "unified renderer configuration has unknown or orphan section [" + section + "]";
				return false;
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
				bool declaresKey = false;
				if (!ExpressionDeclaresKeyChord(group.resetWhen, canonicalKey, declaresKey, error)) return false;
				if (declaresKey)
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
			}

			std::string selected;
			for (const std::string& profileName : group.profiles)
			{
				const Profile& profile = model.profiles.at(group.name + "." + profileName);
				bool declaresKey = false;
				if (!ExpressionDeclaresKeyChord(profile.when, canonicalKey, declaresKey, error)) return false;
				if (!declaresKey) continue;
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

	// Select every group from one immutable source snapshot. A matching profile
	// wins by priority, then comparison specificity, then its declared list
	// order. If nothing matches, a named default is selected; default=auto
	// intentionally leaves that group without an override.
	inline bool SelectAutomatic(const Model& model,
		const DisplayRuleExpression::ValueLookup& sourceValues,
		std::vector<AutomaticSelection>& selections, std::string& error)
	{
		selections.clear();
		error.clear();
		const DisplayRuleExpression::ValueLookup values =
			[&](const std::string& name, std::string& value)
			{
				if (name == "key") { value = "none"; return true; }
				return sourceValues(name, value);
			};

		for (const Group& group : model.groups)
		{
			const Profile* selected = nullptr;
			int selectedSpecificity = 0;
			for (const std::string& profileName : group.profiles)
			{
				const Profile& profile = model.profiles.at(group.name + "." + profileName);
				if (profile.when.empty()) continue;
				int specificity = 0;
				const bool matches = DisplayRuleExpression::Matches(
					profile.when, values, specificity, error);
				if (!matches && !error.empty()) return false;
				if (!matches) continue;
				if (!selected || profile.priority > selected->priority ||
					(profile.priority == selected->priority && specificity > selectedSpecificity))
				{
					selected = &profile;
					selectedSpecificity = specificity;
				}
			}
			if (selected)
			{
				selections.push_back({ group.name, selected->name, false });
			}
			else if (group.defaultSelection != "auto")
			{
				selections.push_back({ group.name, group.defaultSelection, true });
			}
		}
		return true;
	}

	// Extract application-local accelerators from the same configuration
	// expressions used for selection. `$key` is deliberately restricted to a
	// quoted equality literal so registration is finite and deterministic.
	inline bool CollectKeyChords(const Model& model, std::vector<std::string>& chords,
		std::string& error)
	{
		chords.clear();
		error.clear();
		auto collect = [&chords, &error](const std::string& expression) -> bool
		{
			for (size_t position = 0; position < expression.size(); ++position)
			{
				if (expression[position] != '$') continue;
				const size_t nameStart = ++position;
				while (position < expression.size() &&
					(std::isalnum(static_cast<unsigned char>(expression[position])) || expression[position] == '_'))
					++position;
				if (ConfigFile::NormalizeName(expression.substr(nameStart, position - nameStart)) != "key")
					continue;
				while (position < expression.size() && std::isspace(static_cast<unsigned char>(expression[position]))) ++position;
				if (position + 1 >= expression.size() || expression[position] != '=' || expression[position + 1] != '=')
				{
					error = "$key must use == with a quoted chord";
					return false;
				}
				position += 2;
				while (position < expression.size() && std::isspace(static_cast<unsigned char>(expression[position]))) ++position;
				if (position >= expression.size() || (expression[position] != '\'' && expression[position] != '"'))
				{
					error = "$key chord must be quoted";
					return false;
				}
				const char quote = expression[position++];
				const size_t chordStart = position;
				while (position < expression.size() && expression[position] != quote) ++position;
				if (position >= expression.size() || position == chordStart)
				{
					error = "$key chord is unterminated or empty";
					return false;
				}
				chords.push_back(expression.substr(chordStart, position - chordStart));
			}
			return true;
		};
		for (const Group& group : model.groups)
		{
			if (!group.resetWhen.empty() && !collect(group.resetWhen)) return false;
			for (const std::string& name : group.profiles)
			{
				const Profile& profile = model.profiles.at(group.name + "." + name);
				if (!profile.when.empty() && !collect(profile.when)) return false;
			}
		}
		std::sort(chords.begin(), chords.end(), [](const std::string& left, const std::string& right)
			{ return ConfigFile::NormalizeName(left) < ConfigFile::NormalizeName(right); });
		chords.erase(std::unique(chords.begin(), chords.end(), [](const std::string& left, const std::string& right)
			{ return ConfigFile::NormalizeName(left) == ConfigFile::NormalizeName(right); }), chords.end());
		return true;
	}
}
