#pragma once

#include "ConfigFile.h"
#include "ConfigSchema.h"
#include "MainConfigSchema.h"
#include "RendererConfigView.h"
#include "DisplayRuleExpression.h"
#include "AspectRatio.h"

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
	inline bool OwnsSection(const std::string& section)
	{
		return RendererConfigView::OwnsSection(section) ||
			section == "general" ||
			section == "profile_groups" ||
			section == "profiles.input" ||
			section == "profiles.scaling" ||
			section == "profiles.display" ||
			section == "profiles.viewport" ||
			section == "event_actions" ||
			section == "display_rules" ||
			section == "refresh_rate_commands" ||
			section.rfind("profile_groups.", 0) == 0 ||
			section.rfind("profiles.", 0) == 0 ||
			section.rfind("event_actions.", 0) == 0 ||
			section.rfind("display_rules.", 0) == 0;
	}

	inline std::string StatePath(const ConfigFile& config)
	{
		std::string path = config.GetLoadedPath();
		if (path.empty())
			path = ConfigFile::DEFAULT_FILENAME;
		const size_t separator = path.find_last_of("\\/");
		const size_t extension = path.find_last_of('.');
		if (extension != std::string::npos &&
			(separator == std::string::npos || extension > separator))
			path.resize(extension);
		return path + ".state";
	}

	struct Profile
	{
		std::string group;
		std::string name;
		std::string when;
		DisplayRuleExpression::Expression whenExpression;
		int priority = 0;
		std::map<std::string, std::string> settings;
	};

	struct Group
	{
		std::string name;
		std::vector<std::string> profiles;
		std::string defaultSelection;
		std::string resetWhen;
		DisplayRuleExpression::Expression resetExpression;
		bool persistSelection = true;
	};

	struct Model
	{
		bool persistSelection = true;
		std::vector<std::string> warnings;
		std::vector<Group> groups;
		std::map<std::string, Profile> profiles;
		int eventActionDelaySeconds = 5;
		struct EventAction
		{
			std::string name;
			std::vector<std::string> events;
			std::string when;
			DisplayRuleExpression::Expression whenExpression;
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

	struct ResolvedViewport
	{
		std::string group = "viewport";
		std::string profile = "default";
		AspectRatio screenAspect{ 16, 9, 16.0 / 9.0 };
		bool subtitleFit = false;
		uint64_t subtitleHoldMilliseconds = 2000;
		int subtitlePaddingPixels = 20;
		uint64_t generation = 0;
	};

	inline bool ValidateExpressionVariables(
		const DisplayRuleExpression::Expression& expression,
		const std::set<std::string>& allowed, const std::string& context,
		std::string& error)
	{
		for (const std::string& variable : expression.Variables())
			if (allowed.find(variable) == allowed.end())
			{
				error = context + " cannot use variable '$" + variable + "'";
				return false;
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

	inline bool ParseBoolean(const std::string& text, bool& value)
	{
		const std::string normalized = ConfigFile::NormalizeName(text);
		if (normalized == "1" || normalized == "true" ||
			normalized == "yes" || normalized == "on")
		{
			value = true;
			return true;
		}
		if (normalized == "0" || normalized == "false" ||
			normalized == "no" || normalized == "off")
		{
			value = false;
			return true;
		}
		return false;
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
		AspectRatio aspect;
		std::string error;
		return AspectRatioParser::Parse(
			text, minimum, maximum, aspect, error);
	}

	inline bool IsRegistrableKeyChord(const std::string& chord)
	{
		std::string key;
		size_t start = 0;
		while (start <= chord.size())
		{
			const size_t end = chord.find('+', start);
			const std::string token = ConfigFile::Trim(chord.substr(start, end - start));
			if (token.empty()) return false;
			const std::string normalized = ConfigFile::NormalizeName(token);
			if (normalized != "ctrl" && normalized != "control" &&
				normalized != "alt" && normalized != "shift")
			{
				if (!key.empty()) return false;
				key = token;
			}
			if (end == std::string::npos) break;
			start = end + 1;
		}
		if (key.empty()) return false;
		const std::string normalized = ConfigFile::NormalizeName(key);
		if (normalized == "escape" || normalized == "esc" ||
			normalized == "enter" || normalized == "return") return true;
		if (normalized.size() >= 2 && normalized[0] == 'f')
		{
			int number = 0;
			return ParseInteger(normalized.substr(1), 1, 24, number);
		}
		return key.size() == 1 &&
			std::isalnum(static_cast<unsigned char>(key.front())) != 0;
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
			if (key == "mode")
				return IsChoice(value, { "normal", "scope" });
			if (key == "screen_aspect" || key == "scope_screen_aspect")
				return IsAspectInRange(value, 1.0, 4.0);
			if (key == "subtitle_fit" || key == "scope_subtitle_fit")
				return IsBoolean(value);
			if (key == "subtitle_hold_seconds" ||
				key == "scope_subtitle_hold_seconds")
				return IsNumberInRange(value, 0.0, 30.0);
			if (key == "subtitle_padding_pixels" ||
				key == "scope_subtitle_padding_pixels")
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

	inline bool ValidateCanonicalDisplaySetting(
		const std::string& key, const std::string& value)
	{
		std::string ignored;
		for (const char* group : { "input", "scaling", "display" })
			if (ValidateProfileSetting(group, key, value, ignored)) return true;
		return key == "deband" &&
			IsChoice(value, { "auto", "on", "off" });
	}

	inline bool ValidateCanonicalRendererSections(
		const ConfigFile& config, std::string& error)
	{
		const std::vector<ConfigSchema::KeyRule> policyRules = {
			ConfigSchema::Boolean("switch_refresh_rate"),
			ConfigSchema::Boolean("output_diagnostics"),
			ConfigSchema::Boolean("diagnostic_disable_shader_cache")
		};
		if (!ConfigSchema::ValidateSection(config,
			RendererConfigView::GENERAL_SECTION, policyRules, error))
			return false;

		if (const auto* display = config.GetSectionValues(
			RendererConfigView::DISPLAY_SECTION))
			for (const auto& value : *display)
				if (!ValidateCanonicalDisplaySetting(
					value.first, value.second))
				{
					error = "[" +
						std::string(RendererConfigView::DISPLAY_SECTION) +
						"] key '" + value.first +
						"' is not a valid built-in renderer base setting";
					return false;
				}
		return true;
	}

	inline bool Read(const ConfigFile& config, Model& model, std::string& error)
	{
		model = {};
		error.clear();
		RendererConfigView rendererConfig(config);
		if (!rendererConfig.Validate(error, model.warnings) ||
			!ValidateCanonicalRendererSections(config, error))
			return false;
		if (!IsUnified(config))
		{
			if (config.HasSection("display_rules"))
				model.warnings.push_back(
					"[display_rules] is legacy-only; migrate to "
					"[profile_groups.display] and [profiles.display.*]");
			return true; // Legacy remains a separate, unchanged compatibility path.
		}

		if (!config.GetWarnings().empty())
		{
			error = "unified renderer configuration is not strict: " + config.GetWarnings().front();
			return false;
		}

		for (const char* legacySection : { "display_rules", "refresh_rate_commands" })
			if (config.HasSection(legacySection))
			{
				error = "unified renderer configuration cannot include legacy [" +
					std::string(legacySection) + "]";
				return false;
			}
		if (const auto* shortcuts = config.GetSectionValues("shortcuts"))
			for (const auto& shortcut : *shortcuts)
				if (shortcut.first == "screen_profile_normal" ||
					shortcut.first == "screen_profile_scope" ||
					shortcut.first == "display_rules_auto")
				{
					error = "unified renderer configuration cannot include "
						"legacy [shortcuts] key '" + shortcut.first + "'";
					return false;
				}

		std::string value;
		const std::vector<ConfigSchema::KeyRule> generalRules = {
			ConfigSchema::Boolean("persist_profile_selection"),
			ConfigSchema::Boolean("switch_refresh_rate"),
			ConfigSchema::Integer("event_action_delay_seconds", 0, 30),
			ConfigSchema::Boolean("output_diagnostics"),
			ConfigSchema::Boolean("diagnostic_disable_shader_cache")
		};
		if (!ConfigSchema::ValidateSection(config, "general", generalRules, error))
			return false;
		bool persist = true;
		config.TryGetBool("general", "persist_profile_selection", persist);
		model.persistSelection = persist;
		if (config.TryGetString("general", "event_action_delay_seconds", value))
			ParseInteger(value, 0, 30, model.eventActionDelaySeconds);
		for (const char* displaySection :
			{ RendererConfigView::LEGACY_DISPLAY_SECTION,
			  RendererConfigView::HISTORICAL_DISPLAY_SECTION })
		{
			const auto* display = config.GetSectionValues(displaySection);
			if (!display)
				continue;
			const std::set<std::string> baseKeys = {
				"sdr_target_nits", "sdr_black_nits", "switch_refresh_rate",
				"quality", "tone_mapping", "gamut_mapping", "peak_detection",
				"contrast_recovery", "upscaler", "downscaler", "deband",
				"deband_strength", "sigmoid", "dithering", "output_presentation",
				"output_range", "output_gamma",
				"sdr_target_primaries", "report_bt2020_to_display",
				"sdr_input_transfer", "output_diagnostics",
				"diagnostic_disable_shader_cache", "screen_aspect",
				"default_screen_profile", "subtitle_fit",
				"subtitle_hold_seconds", "subtitle_padding_pixels",
				"scope_screen_aspect", "scope_subtitle_fit",
				"scope_subtitle_hold_seconds", "scope_subtitle_padding_pixels"
			};
			std::vector<ConfigSchema::KeyRule> displayRules;
			for (const std::string& key : baseKeys)
				displayRules.push_back({
					key,
					[key](const std::string& value)
						{ return ValidateBaseSetting(key, value); },
					"a valid renderer value"
				});
			if (!ConfigSchema::ValidateSection(
				config, displaySection, displayRules, error))
				return false;
		}

		const std::vector<std::string> expectedGroups = { "input", "scaling", "display", "viewport" };
		for (const std::string& groupName : expectedGroups)
		{
			const std::string section = "profile_groups." + groupName;
			const std::string profilePrefix = "profiles." + groupName + ".";
			if (!config.HasSection(section))
			{
				for (const std::string& configured :
					config.GetSectionNames())
					if (configured.rfind(profilePrefix, 0) == 0)
					{
						error = "[" + configured +
							"] is orphaned because [" + section +
							"] is not declared";
						return false;
					}
				continue; // An undeclared group contributes no overrides.
			}
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
				if (!group.resetExpression.Compile(group.resetWhen, error, true) ||
					!ValidateExpressionVariables(group.resetExpression, { "key" },
						"[" + section + "] when=", error))
					return false;
			}
			group.persistSelection = model.persistSelection;
			if (config.TryGetString(section, "persist_profile_selection", value) &&
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
				const std::map<std::string, std::string> viewportAliases = {
					{ "scope_screen_aspect", "screen_aspect" },
					{ "scope_subtitle_fit", "subtitle_fit" },
					{ "scope_subtitle_hold_seconds", "subtitle_hold_seconds" },
					{ "scope_subtitle_padding_pixels", "subtitle_padding_pixels" }
				};
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
						std::string settingKey = value.first;
						std::string expected;
						if (groupName == "viewport")
						{
							if (settingKey == "mode")
							{
								if (!ValidateProfileSetting(groupName,
									settingKey, value.second, expected))
								{
									error = "[" + profileSection +
										"] key 'mode' has invalid value '" +
										value.second +
										"'; expected normal or scope";
									return false;
								}
								model.warnings.push_back(
									"[" + profileSection +
									"] key 'mode' is deprecated and ignored; use screen_aspect");
								continue;
							}
							const auto alias = viewportAliases.find(settingKey);
							if (alias != viewportAliases.end())
							{
								if (values->find(alias->second) != values->end())
								{
									error = "[" + profileSection + "] defines both deprecated '" +
										settingKey + "' and replacement '" + alias->second + "'";
									return false;
								}
								model.warnings.push_back(
									"[" + profileSection + "] key '" + settingKey +
									"' is deprecated; use '" + alias->second + "'");
								settingKey = alias->second;
							}
						}
						if (!ValidateProfileSetting(groupName, settingKey, value.second, expected))
						{
							error = "[" + profileSection + "] key '" + settingKey +
								"' has invalid value '" + value.second + "'; expected " + expected;
							return false;
						}
						profile.settings.emplace(settingKey, value.second);
					}
				}
				if (!profile.when.empty() &&
					(!profile.whenExpression.Compile(profile.when, error, true) ||
					 !ValidateExpressionVariables(profile.whenExpression,
						{ "eotf", "transfer", "colorspace", "primaries", "format",
						  "hdr_metadata", "interlaced", "scan", "source_rate",
						  "cadence", "width", "height", "resolution", "key" },
						"[" + profileSection + "] when=", error)))
					return false;
				model.profiles.emplace(groupName + "." + profileName, std::move(profile));
			}
			std::map<std::string, std::string> chordOwners;
			auto registerChords = [&](const DisplayRuleExpression::Expression& expression,
				const std::string& owner) -> bool
			{
				for (const std::string& chord : expression.KeyChords())
				{
					if (!IsRegistrableKeyChord(chord))
					{
						error = owner + " uses unregistrable key chord '" + chord + "'";
						return false;
					}
					const std::string canonical = ConfigFile::NormalizeName(chord);
					const auto existing = chordOwners.find(canonical);
					if (existing != chordOwners.end() && existing->second != owner)
					{
						error = "key chord '" + chord + "' selects both " +
							existing->second + " and " + owner + " in group '" +
							groupName + "'";
						return false;
					}
					chordOwners[canonical] = owner;
				}
				return true;
			};
			if (!group.resetWhen.empty() &&
				!registerChords(group.resetExpression, "[" + section + "] when="))
				return false;
			for (const std::string& profileName : group.profiles)
			{
				const Profile& profile = model.profiles.at(groupName + "." + profileName);
				if (!profile.when.empty() &&
					!registerChords(profile.whenExpression,
						"[profiles." + groupName + "." + profileName + "]"))
					return false;
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
					!action.whenExpression.Compile(action.when, error, true) ||
					!ValidateExpressionVariables(action.whenExpression,
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
			if (expectedSections.find(section) == expectedSections.end() &&
				!MainConfigSchema::OwnsSection(section) &&
				!RendererConfigView::OwnsSection(section))
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
				if (group.resetExpression.DeclaresKeyChord(canonicalKey))
				{
					int specificity = 0;
					const bool matchesReset = group.resetExpression.Matches(
						values, specificity, error);
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
				if (!profile.whenExpression.DeclaresKeyChord(canonicalKey)) continue;
				int specificity = 0;
				const bool matchesProfile = profile.whenExpression.Matches(
					values, specificity, error);
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

	inline bool ResolveViewport(const Model& model,
		const std::string& profileName, uint64_t generation,
		ResolvedViewport& viewport, std::string& error)
	{
		viewport = {};
		viewport.generation = generation;
		error.clear();
		if (profileName.empty() || profileName == "default")
			return true;

		const auto profile = model.profiles.find(
			"viewport." + ConfigFile::NormalizeName(profileName));
		if (profile == model.profiles.end())
		{
			error = "viewport profile '" + profileName + "' does not exist";
			return false;
		}
		viewport.profile = profile->second.name;
		const auto& settings = profile->second.settings;
		auto value = settings.find("screen_aspect");
		if (value != settings.end() &&
			!AspectRatioParser::Parse(value->second, 1.0, 4.0,
				viewport.screenAspect, error))
		{
			error = "[profiles.viewport." + viewport.profile +
				"] screen_aspect: " + error;
			return false;
		}
		value = settings.find("subtitle_fit");
		if (value != settings.end() &&
			!ParseBoolean(value->second, viewport.subtitleFit))
		{
			error = "[profiles.viewport." + viewport.profile +
				"] subtitle_fit is invalid";
			return false;
		}
		value = settings.find("subtitle_hold_seconds");
		if (value != settings.end())
		{
			double seconds = 0.0;
			if (!DisplayRuleExpression::ParseNumber(
				ConfigFile::Trim(value->second), seconds) ||
				seconds < 0.0 || seconds > 30.0)
			{
				error = "[profiles.viewport." + viewport.profile +
					"] subtitle_hold_seconds is invalid";
				return false;
			}
			viewport.subtitleHoldMilliseconds =
				static_cast<uint64_t>(std::llround(seconds * 1000.0));
		}
		value = settings.find("subtitle_padding_pixels");
		if (value != settings.end() &&
			!ParseInteger(value->second, 0, 500,
				viewport.subtitlePaddingPixels))
		{
			error = "[profiles.viewport." + viewport.profile +
				"] subtitle_padding_pixels is invalid";
			return false;
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
				const bool matches = profile.whenExpression.Matches(
					values, specificity, error);
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
		auto collect = [&chords](const DisplayRuleExpression::Expression& expression)
		{
			chords.insert(chords.end(), expression.KeyChords().begin(),
				expression.KeyChords().end());
		};
		for (const Group& group : model.groups)
		{
			if (!group.resetWhen.empty()) collect(group.resetExpression);
			for (const std::string& name : group.profiles)
			{
				const Profile& profile = model.profiles.at(group.name + "." + name);
				if (!profile.when.empty()) collect(profile.whenExpression);
			}
		}
		std::sort(chords.begin(), chords.end(), [](const std::string& left, const std::string& right)
			{ return ConfigFile::NormalizeName(left) < ConfigFile::NormalizeName(right); });
		chords.erase(std::unique(chords.begin(), chords.end(), [](const std::string& left, const std::string& right)
			{ return ConfigFile::NormalizeName(left) == ConfigFile::NormalizeName(right); }), chords.end());
		return true;
	}
}
