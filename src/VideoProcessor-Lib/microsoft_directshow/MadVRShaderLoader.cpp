/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#include <pch.h>

#include <ConfigFile.h>
#include <DebugLog.h>
#include <AspectRatio.h>
#include <ActivePictureTransitionModel.h>
#include <microsoft_directshow/MadVRExternalPixelShaders.h>

#include "MadVRShaderLoader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>


namespace
{
constexpr const char* CONFIG_SECTION = "shaders";
constexpr size_t MAX_SHADER_BYTES = 4 * 1024 * 1024;

enum class SignalMatch
{
	ANY,
	SDR,
	HDR
};

enum class ShaderSourceBackend
{
	ANY,
	MADVR,
	LIBPLACEBO
};

struct ShaderEntry
{
	unsigned int order = 0;
	std::filesystem::path path;
	std::string displayName;
	std::map<std::string, std::string> parameters;
};

struct ShaderRule
{
	std::string name;
	std::string label;
	bool nls = false;
	bool explicitType = false;
	SignalMatch signal = SignalMatch::ANY;
	std::vector<int> nominalRates;
	bool none = false;
	bool manual = false;
	bool valid = true;
	ShaderSourceBackend sourceBackend = ShaderSourceBackend::ANY;
	std::string filename;
	std::map<std::string, std::string> parameters;
	unsigned long outputAspectRatioX = 0;
	unsigned long outputAspectRatioY = 0;
	// This is deliberately separate from outputAspectRatio.  madVR's screen
	// profile owns presentation; NLS only needs a geometric target.
	unsigned long nlsTargetAspectRatioX = 0;
	unsigned long nlsTargetAspectRatioY = 0;
	double aspectTolerancePercent = -1.0;
	double stableGeometryDeadbandPercent =
		ActivePictureTransitionModel::DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT;
	double activeAspectMinimum = 0.0;
	bool narrowerOnly = false;
	std::string inactiveRule;
	std::vector<ShaderEntry> preScale;
	std::vector<ShaderEntry> postScale;
};


bool RuleAppliesToBackend(const ShaderRule& rule,
	ShaderRendererBackend backend)
{
	if (rule.sourceBackend == ShaderSourceBackend::ANY)
		return true;
	return (backend == ShaderRendererBackend::MADVR &&
			rule.sourceBackend == ShaderSourceBackend::MADVR) ||
		(backend == ShaderRendererBackend::LIBPLACEBO &&
			rule.sourceBackend == ShaderSourceBackend::LIBPLACEBO);
}


ShaderSourceBackend SourceBackendForFilename(const std::string& filename)
{
	const std::string extension = ConfigFile::NormalizeName(
		std::filesystem::u8path(filename).extension().u8string());
	if (extension == ".glsl" || extension == ".hook")
		return ShaderSourceBackend::LIBPLACEBO;
	// Preserve the complete legacy contract: HLSL and unrecognized extensions
	// continue down the exact madVR validation and compilation path.
	return ShaderSourceBackend::MADVR;
}


bool ParseBoundedDouble(const std::string& raw, double minimum,
	double maximum, double& value)
{
	try
	{
		size_t parsed = 0;
		const double candidate = std::stod(ConfigFile::Trim(raw), &parsed);
		if (parsed != ConfigFile::Trim(raw).size() || !std::isfinite(candidate) ||
			candidate < minimum || candidate > maximum)
			return false;
		value = candidate;
		return true;
	}
	catch (...)
	{
		return false;
	}
}

MadVRShaderRuntimeState g_runtimeState;

double GetNlsTargetAspect(const ShaderRule& rule)
{
	const MadVRShaderRuntimeSnapshot runtime = g_runtimeState.GetSnapshot();
	if (runtime.nlsTargetAspect > 0.0)
		return runtime.nlsTargetAspect;
	if (rule.nlsTargetAspectRatioX > 0 && rule.nlsTargetAspectRatioY > 0)
		return static_cast<double>(rule.nlsTargetAspectRatioX) /
			rule.nlsTargetAspectRatioY;
	if (rule.nls)
		return 16.0 / 9.0;
	return 0.0;
}


bool IsSupportedProfile(const std::string& profile)
{
	return profile == "ps_2_0" || profile == "ps_2_a" ||
		profile == "ps_2_b" || profile == "ps_3_0";
}


std::string NormalizeProfile(const std::string& profile)
{
	return ConfigFile::NormalizeName(profile);
}


std::string CurrentExecutablePath()
{
	std::vector<wchar_t> buffer(32768);
	const DWORD length = GetModuleFileNameW(
		nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
	if (length == 0 || length >= buffer.size())
		return {};
	return std::filesystem::path(
		std::wstring(buffer.data(), length)).u8string();
}


std::filesystem::path ResolveShaderPath(const std::string& filename)
{
	std::string resolved;
	std::string error;
	if (!MadVRShaderLoader::ResolveShaderFilename(
		filename, CurrentExecutablePath(), resolved, error))
	{
		DebugLog::Log("Shaders: rejected shader filename \"%s\": %s",
			filename.c_str(), error.c_str());
		return {};
	}
	return std::filesystem::u8path(resolved);
}


std::vector<std::string> SplitList(const std::string& value)
{
	std::vector<std::string> values;
	std::istringstream stream(value);
	std::string item;
	while (std::getline(stream, item, ','))
	{
		item = ConfigFile::Trim(item);
		if (!item.empty())
			values.push_back(item);
	}
	return values;
}


bool RejectIndexedShaderEntries(const ConfigFile& config,
	const std::string& section)
{
	const auto* settings = config.GetSectionValues(section);
	if (!settings)
		return true;

	for (const auto& setting : *settings)
	{
		if (setting.first.compare(0, 11, "pre_resize_") == 0 ||
			setting.first.compare(0, 12, "post_resize_") == 0)
		{
			DebugLog::Log(
				"Shaders: [%s] key \"%s\" is unsupported; each effect must use one file and one stage",
				section.c_str(), setting.first.c_str());
			return false;
		}
	}
	return true;
}


bool IsParameterNameValid(const std::string& name)
{
	if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
		return false;
	return std::all_of(name.begin() + 1, name.end(), [](unsigned char character)
		{
			return std::isalnum(character) != 0 || character == '_';
		});
}


void LoadShaderParameters(const ConfigFile& config, const std::string& section,
	ShaderRule& rule)
{
	const auto* settings = config.GetSectionValues(section);
	if (!settings)
		return;

	for (const auto& setting : *settings)
	{
		if (setting.first.compare(0, 6, "param_") != 0)
			continue;

		const std::string name = setting.first.substr(6);
		if (!IsParameterNameValid(name))
		{
			DebugLog::Log("Shaders: rule \"%s\" has invalid parameter name \"%s\"",
				rule.name.c_str(), name.c_str());
			rule.valid = false;
			continue;
		}

		try
		{
			// Common user-facing shader enums are substituted as compile-time
			// scalars so shader templates remain numeric-only and safe.
			if (name == "quality")
			{
				const std::string quality = ConfigFile::NormalizeName(setting.second);
				if (quality == "low") rule.parameters[name] = "0";
				else if (quality == "medium") rule.parameters[name] = "1";
				else if (quality == "high") rule.parameters[name] = "2";
				else if (quality == "very high" || quality == "very_high" || quality == "veryhigh")
					rule.parameters[name] = "3";
				else throw std::invalid_argument("unknown quality");
				continue;
			}
			if (name == "geometry")
			{
				const std::string geometry = ConfigFile::NormalizeName(setting.second);
				if (geometry == "classic") rule.parameters[name] = "0";
				else if (geometry == "protected" || geometry == "center protected" ||
					geometry == "center_protected" || geometry == "centerprotected")
					rule.parameters[name] = "1";
				else throw std::invalid_argument("unknown geometry");
				continue;
			}
			size_t parsedLength = 0;
			const double value = std::stod(setting.second, &parsedLength);
			if (parsedLength != setting.second.size() || !std::isfinite(value))
				throw std::invalid_argument("not a finite scalar");
			std::ostringstream normalized;
			normalized.precision(17);
			normalized << value;
			rule.parameters[name] = normalized.str();
		}
		catch (...)
		{
			const char* acceptedValues = "";
			if (name == "quality")
				acceptedValues = " or LOW/MEDIUM/HIGH/VERY_HIGH";
			else if (name == "geometry")
				acceptedValues = " or CLASSIC/PROTECTED";
			DebugLog::Log("Shaders: rule \"%s\" parameter \"%s\" must be a finite number%s, got \"%s\"",
				rule.name.c_str(), name.c_str(),
				acceptedValues,
				setting.second.c_str());
			rule.valid = false;
		}
	}
}


bool NormalizeNlsSetting(const std::string& name,
	const std::string& rawValue, std::string& normalized)
{
	const std::string value = ConfigFile::NormalizeName(rawValue);
	if (name == "geometry")
	{
		if (value == "classic") normalized = "0";
		else if (value == "protected" || value == "center protected" ||
			value == "center_protected" || value == "centerprotected")
			normalized = "1";
		else return false;
		return true;
	}
	if (name == "quality")
	{
		if (value == "low") normalized = "0";
		else if (value == "medium") normalized = "1";
		else if (value == "high") normalized = "2";
		else if (value == "very high" || value == "very_high" ||
			value == "veryhigh") normalized = "3";
		else return false;
		return true;
	}

	double minimum = 0.0;
	double maximum = 0.0;
	if (name == "strength")
	{
		minimum = 0.0;
		maximum = 1.0;
	}
	else if (name == "center_protection")
	{
		minimum = 0.0;
		maximum = 0.45;
	}
	else if (name == "curve")
	{
		minimum = 0.5;
		maximum = 4.0;
	}
	else
	{
		return false;
	}

	double parsed = 0.0;
	if (!ParseBoundedDouble(rawValue, minimum, maximum, parsed))
		return false;
	std::ostringstream text;
	text.precision(17);
	text << parsed;
	normalized = text.str();
	return true;
}


void LoadTypedNlsSettings(const ConfigFile& config,
	const std::string& section, ShaderRule& rule)
{
	const auto* settings = config.GetSectionValues(section);
	if (!settings)
		return;

	const std::map<std::string, std::string> defaults = {
		{ "strength", "1" },
		{ "geometry", "0" },
		{ "center_protection", "0.35" },
		{ "curve", "2" },
		{ "quality", "1" },
		{ "stretch_ratio", "1" },
		{ "active_height_fraction", "1" },
		{ "active_left", "0" },
		{ "active_top", "0" },
		{ "active_right", "1" },
		{ "active_bottom", "1" },
		{ "warp_axis", "0" },
		{ "safe_fit", "0" },
		{ "safe_fit_axis", "0" },
		{ "safe_fit_fraction", "1" }
	};
	for (const auto& setting : defaults)
		rule.parameters.emplace(setting.first, setting.second);

	for (const char* rawName :
		{ "strength", "geometry", "center_protection", "curve", "quality" })
	{
		const std::string name(rawName);
		const std::string alias = "param_" + name;
		const auto typed = settings->find(name);
		const auto legacy = settings->find(alias);
		if (typed != settings->end() && legacy != settings->end())
		{
			DebugLog::Log(
				"Shaders: rule \"%s\" defines both \"%s\" and deprecated alias \"%s\"",
				rule.name.c_str(), name.c_str(), alias.c_str());
			rule.valid = false;
			continue;
		}

		const auto selected = typed != settings->end() ? typed : legacy;
		if (selected == settings->end())
			continue;
		std::string normalized;
		if (!NormalizeNlsSetting(name, selected->second, normalized))
		{
			DebugLog::Log(
				"Shaders: rule \"%s\" has invalid NLS %s \"%s\"",
				rule.name.c_str(), name.c_str(), selected->second.c_str());
			rule.valid = false;
			continue;
		}
		rule.parameters[name] = normalized;
		if (legacy != settings->end() && rule.explicitType)
			DebugLog::Log(
				"Shaders: rule \"%s\" key \"%s\" is deprecated; use \"%s\"",
				rule.name.c_str(), alias.c_str(), name.c_str());
	}

	// These values belong to the active viewport and detected picture, not to
	// user configuration. Continue reading them for old configurations, but
	// always overwrite them from the coherent runtime snapshot before compile.
	for (const char* derived :
		{ "stretch_ratio", "warp_axis", "active_height_fraction",
		  "active_left", "active_top", "active_right", "active_bottom",
		  "safe_fit", "safe_fit_axis", "safe_fit_fraction" })
	{
		const std::string alias = "param_" + std::string(derived);
		if (settings->find(alias) != settings->end() && rule.explicitType)
			DebugLog::Log(
				"Shaders: rule \"%s\" key \"%s\" is deprecated and runtime-derived",
				rule.name.c_str(), alias.c_str());
	}
}


bool ParseSignal(const std::string& rawValue, SignalMatch& signal)
{
	const std::string value = ConfigFile::NormalizeName(rawValue);
	if (value.empty() || value == "any")
		signal = SignalMatch::ANY;
	else if (value == "sdr")
		signal = SignalMatch::SDR;
	else if (value == "hdr")
		signal = SignalMatch::HDR;
	else
		return false;
	return true;
}


bool ParseNominalRates(const std::string& rawValue, std::vector<int>& rates)
{
	if (ConfigFile::NormalizeName(rawValue) == "any")
		return true;

	for (const std::string& item : SplitList(rawValue))
	{
		try
		{
			size_t parsedLength = 0;
			const int rate = std::stoi(item, &parsedLength);
			if (parsedLength != item.size() || rate < 1 || rate > 1000)
				return false;
			rates.push_back(rate);
		}
		catch (...)
		{
			return false;
		}
	}
	return !rates.empty();
}


bool ParseOutputAspectRatio(const std::string& rawValue, unsigned long& aspectX,
	unsigned long& aspectY)
{
	aspectX = 0;
	aspectY = 0;
	const std::string value = ConfigFile::Trim(rawValue);
	if (ConfigFile::NormalizeName(value) == "native")
		return true;

	AspectRatio parsed;
	std::string error;
	if (!AspectRatioParser::Parse(value, 1.0, 4.0, parsed, error) ||
		parsed.numerator > (std::numeric_limits<unsigned long>::max)() ||
		parsed.denominator > (std::numeric_limits<unsigned long>::max)())
		return false;
	aspectX = static_cast<unsigned long>(parsed.numerator);
	aspectY = static_cast<unsigned long>(parsed.denominator);
	return true;
}


bool IsHdr(EOTF eotf)
{
	return eotf == EOTF::HDR || eotf == EOTF::PQ || eotf == EOTF::HLG;
}


const char* SignalName(EOTF eotf)
{
	if (eotf == EOTF::SDR)
		return "SDR";
	if (IsHdr(eotf))
		return "HDR";
	return "UNKNOWN";
}


bool RuleMatches(const ShaderRule& rule, EOTF eotf, int nominalRate)
{
	if (rule.signal == SignalMatch::SDR && eotf != EOTF::SDR)
		return false;
	if (rule.signal == SignalMatch::HDR && !IsHdr(eotf))
		return false;
	return rule.nominalRates.empty() ||
		std::find(rule.nominalRates.begin(), rule.nominalRates.end(), nominalRate) !=
			rule.nominalRates.end();
}


ShaderRule LoadRule(const ConfigFile& config, const std::string& configuredName)
{
	ShaderRule rule;
	rule.name = ConfigFile::NormalizeName(configuredName);
	rule.label = ConfigFile::Trim(configuredName);
	const std::string section = std::string(CONFIG_SECTION) + "." + rule.name;
	if (!config.HasSection(section))
	{
		DebugLog::Log("Shaders: configured rule \"%s\" has no [%s] section",
			configuredName.c_str(), section.c_str());
		rule.valid = false;
		return rule;
	}

	std::string rawValue;
	if (config.TryGetString(section, "label", rawValue) && !ConfigFile::Trim(rawValue).empty())
		rule.label = ConfigFile::Trim(rawValue);

	if (config.TryGetString(section, "type", rawValue))
	{
		rule.explicitType = true;
		const std::string type = ConfigFile::NormalizeName(rawValue);
		if (type == "nls")
			rule.nls = true;
		else if (type != "custom" && type != "shader")
		{
			DebugLog::Log(
				"Shaders: rule \"%s\" has invalid type \"%s\"; expected NLS or CUSTOM",
				rule.name.c_str(), rawValue.c_str());
			rule.valid = false;
		}
	}

	if (config.TryGetString(section, "signal", rawValue) && !ParseSignal(rawValue, rule.signal))
	{
		DebugLog::Log("Shaders: rule \"%s\" has invalid signal \"%s\"; expected ANY, SDR, or HDR",
			rule.name.c_str(), rawValue.c_str());
		rule.valid = false;
	}

	if (config.TryGetString(section, "frame_rates", rawValue) &&
		!ParseNominalRates(rawValue, rule.nominalRates))
	{
		DebugLog::Log("Shaders: rule \"%s\" has invalid frame_rates \"%s\"; use comma-separated nominal integers or ANY",
			rule.name.c_str(), rawValue.c_str());
		rule.valid = false;
	}

	if (config.TryGetString(section, "none", rawValue) &&
		!config.TryGetBool(section, "none", rule.none))
	{
		DebugLog::Log("Shaders: rule \"%s\" has invalid none value \"%s\"",
			rule.name.c_str(), rawValue.c_str());
		rule.valid = false;
	}

	std::string shortcut;
	const bool hasShortcut =
		config.TryGetString(section, "shortcut", shortcut) &&
		!ConfigFile::Trim(shortcut).empty();
	rule.manual = hasShortcut;
	if (config.TryGetString(section, "manual", rawValue))
	{
		bool legacyManual = false;
		if (!config.TryGetBool(section, "manual", legacyManual))
		{
			DebugLog::Log("Shaders: rule \"%s\" has invalid manual value \"%s\"",
				rule.name.c_str(), rawValue.c_str());
			rule.valid = false;
		}
		else
		{
			DebugLog::Log(
				"Shaders: rule \"%s\" key \"manual\" is deprecated; a shortcut makes an effect manual automatically",
				rule.name.c_str());
			rule.manual = hasShortcut || legacyManual;
		}
	}

	if (config.TryGetString(section, "output_aspect_ratio", rawValue) &&
		!ParseOutputAspectRatio(rawValue, rule.outputAspectRatioX,
			rule.outputAspectRatioY))
	{
		DebugLog::Log("Shaders: rule \"%s\" has invalid output_aspect_ratio \"%s\"; use native, a decimal, or X:Y",
			rule.name.c_str(), rawValue.c_str());
		rule.valid = false;
	}
	if (config.TryGetString(section, "nls_target_aspect_ratio", rawValue) &&
		(!ParseOutputAspectRatio(rawValue, rule.nlsTargetAspectRatioX,
			rule.nlsTargetAspectRatioY) || rule.nlsTargetAspectRatioX == 0 ||
			rule.nlsTargetAspectRatioY == 0))
	{
		DebugLog::Log("Shaders: rule \"%s\" has invalid nls_target_aspect_ratio \"%s\"; use a decimal or X:Y",
			rule.name.c_str(), rawValue.c_str());
		rule.valid = false;
	}

	if (config.TryGetString(section, "aspect_tolerance_percent", rawValue) &&
		!ParseBoundedDouble(rawValue, 0.0, 50.0, rule.aspectTolerancePercent))
	{
		DebugLog::Log("Shaders: rule \"%s\" has invalid aspect_tolerance_percent \"%s\"",
			rule.name.c_str(), rawValue.c_str());
		rule.valid = false;
	}
	if (config.TryGetString(section, "tolerance_percent", rawValue))
	{
		if (config.GetSectionValues(section)->find(
			"aspect_tolerance_percent") !=
			config.GetSectionValues(section)->end())
		{
			DebugLog::Log(
				"Shaders: rule \"%s\" defines both \"tolerance_percent\" and deprecated alias \"aspect_tolerance_percent\"",
				rule.name.c_str());
			rule.valid = false;
		}
		else if (!ParseBoundedDouble(rawValue, 0.0, 50.0,
			rule.aspectTolerancePercent))
		{
			DebugLog::Log(
				"Shaders: rule \"%s\" has invalid tolerance_percent \"%s\"",
				rule.name.c_str(), rawValue.c_str());
			rule.valid = false;
		}
	}
	if (config.TryGetString(section, "active_aspect_min", rawValue) &&
		!ParseBoundedDouble(rawValue, 1.0, 4.0, rule.activeAspectMinimum))
	{
		DebugLog::Log("Shaders: rule \"%s\" has invalid active_aspect_min \"%s\"",
			rule.name.c_str(), rawValue.c_str());
		rule.valid = false;
	}
	if (config.TryGetString(section, "aspect_direction", rawValue))
	{
		const std::string direction = ConfigFile::NormalizeName(rawValue);
		if (direction == "narrower_only")
			rule.narrowerOnly = true;
		else if (direction != "any")
		{
			DebugLog::Log("Shaders: rule \"%s\" has invalid aspect_direction \"%s\"; use ANY or NARROWER_ONLY",
				rule.name.c_str(), rawValue.c_str());
			rule.valid = false;
		}
	}
	if (config.TryGetString(section, "inactive_rule", rawValue))
		rule.inactiveRule = ConfigFile::NormalizeName(rawValue);

	LoadShaderParameters(config, section, rule);
	if (!rule.explicitType && rule.nlsTargetAspectRatioX > 0 &&
		rule.nlsTargetAspectRatioY > 0 &&
		rule.parameters.find("stretch_ratio") != rule.parameters.end())
	{
		rule.nls = true;
	}
	if (rule.nls)
	{
		if (config.TryGetString(section,
			"stable_geometry_deadband_percent", rawValue) &&
			!ParseBoundedDouble(rawValue, 0.0,
				ActivePictureTransitionModel::
					MAX_STABLE_GEOMETRY_DEADBAND_PERCENT,
				rule.stableGeometryDeadbandPercent))
		{
			DebugLog::Log("Shaders: NLS rule \"%s\" has invalid stable_geometry_deadband_percent \"%s\"; use 0 through %.0f",
				rule.name.c_str(), rawValue.c_str(),
				ActivePictureTransitionModel::
					MAX_STABLE_GEOMETRY_DEADBAND_PERCENT);
			rule.valid = false;
		}
		if (rule.aspectTolerancePercent < 0.0)
			rule.aspectTolerancePercent = 5.0;
		LoadTypedNlsSettings(config, section, rule);
		if (rule.explicitType)
		{
			for (const char* derived :
				{ "output_aspect_ratio", "nls_target_aspect_ratio",
				  "active_aspect_min", "aspect_direction", "inactive_rule" })
			{
				if (config.GetSectionValues(section)->find(derived) !=
					config.GetSectionValues(section)->end())
				{
					DebugLog::Log(
						"Shaders: typed NLS rule \"%s\" key \"%s\" is deprecated and runtime-derived",
						rule.name.c_str(), derived);
				}
			}
			rule.outputAspectRatioX = 0;
			rule.outputAspectRatioY = 0;
			rule.nlsTargetAspectRatioX = 0;
			rule.nlsTargetAspectRatioY = 0;
			rule.activeAspectMinimum = 0.0;
			rule.narrowerOnly = false;
			rule.inactiveRule.clear();
		}
	}

	if (!RejectIndexedShaderEntries(config, section))
		rule.valid = false;
	std::string filename;
	if (config.TryGetString(section, "file", filename))
	{
		rule.filename = ConfigFile::Trim(filename);
		rule.sourceBackend = SourceBackendForFilename(rule.filename);
		std::string stage = "pre_resize";
		config.TryGetString(section, "stage", stage);
		stage = ConfigFile::NormalizeName(stage);
		const std::filesystem::path path =
			ResolveShaderPath(filename);
		if (path.empty())
			rule.valid = false;
		else if (stage == "pre_resize" || stage == "pre")
			rule.preScale.push_back({ 1, path });
		else if (stage == "post_resize" || stage == "post")
			rule.postScale.push_back({ 1, path });
		else
		{
			DebugLog::Log(
				"Shaders: rule \"%s\" has invalid stage \"%s\"; use PRE_RESIZE or POST_RESIZE",
				rule.name.c_str(), stage.c_str());
			rule.valid = false;
		}
	}
	else if (config.GetSectionValues(section)->find("stage") !=
		config.GetSectionValues(section)->end())
	{
		DebugLog::Log(
			"Shaders: rule \"%s\" defines stage without file",
			rule.name.c_str());
		rule.valid = false;
	}
	return rule;
}


bool LoadRuleSelectionForBackend(const ConfigFile& config,
	const std::string& selector, ShaderRendererBackend backend,
	std::vector<ShaderRule>& rules)
{
	rules.clear();
	std::set<std::string> seen;
	for (const std::string& name : SplitList(selector))
	{
		const std::string normalized = ConfigFile::NormalizeName(name);
		if (!seen.insert(normalized).second)
			continue;
		ShaderRule rule = LoadRule(config, normalized);
		if (!rule.valid)
			return false;
		if (!RuleAppliesToBackend(rule, backend))
			continue;
		rules.push_back(std::move(rule));
	}
	return !rules.empty();
}


bool LoadRuleSelection(const ConfigFile& config, const std::string& selector,
	std::vector<ShaderRule>& rules)
{
	return LoadRuleSelectionForBackend(config, selector,
		ShaderRendererBackend::MADVR, rules);
}


ShaderRule* FindNlsRule(std::vector<ShaderRule>& rules)
{
	ShaderRule* result = nullptr;
	for (ShaderRule& rule : rules)
	{
		if (!rule.nls)
			continue;
		if (result)
			return nullptr;
		result = &rule;
	}
	return result;
}


ConfiguredShaderRule ToConfiguredShaderRule(const ShaderRule& rule)
{
	ConfiguredShaderRule configured;
	configured.name = rule.name;
	configured.label = rule.label;
	configured.filename = rule.filename;
	configured.parameters = rule.parameters;
	configured.nls = rule.nls;
	configured.none = rule.none;
	configured.aspectTolerancePercent =
		std::max(0.0, rule.aspectTolerancePercent);
	configured.stableGeometryDeadbandPercent =
		rule.stableGeometryDeadbandPercent;
	configured.activeAspectMinimum = rule.activeAspectMinimum;
	configured.narrowerOnly = rule.narrowerOnly;
	return configured;
}


bool ApplyShaderParameters(std::string& source,
	const std::map<std::string, std::string>& parameters,
	const std::filesystem::path& path)
{
	for (const auto& parameter : parameters)
	{
		const std::string token = "{{" + parameter.first + "}}";
		size_t position = 0;
		while ((position = source.find(token, position)) != std::string::npos)
		{
			source.replace(position, token.size(), parameter.second);
			position += parameter.second.size();
		}
	}

	if (source.find("{{") != std::string::npos || source.find("}}") != std::string::npos)
	{
		DebugLog::Log("Shaders: unresolved {{parameter}} token in \"%s\"",
			path.u8string().c_str());
		return false;
	}
	return true;
}


bool ReadShader(const std::filesystem::path& path, std::string& source)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input.is_open())
	{
		DebugLog::Log("Shaders: cannot open shader file \"%s\"",
			path.u8string().c_str());
		return false;
	}

	const std::streamoff length = input.tellg();
	if (length <= 0 || static_cast<unsigned long long>(length) > MAX_SHADER_BYTES)
	{
		DebugLog::Log("Shaders: invalid shader size (%lld bytes) for \"%s\"",
			static_cast<long long>(length), path.u8string().c_str());
		return false;
	}

	input.seekg(0, std::ios::beg);
	source.resize(static_cast<size_t>(length));
	if (!input.read(&source[0], length))
	{
		DebugLog::Log("Shaders: failed reading shader file \"%s\"",
			path.u8string().c_str());
		source.clear();
		return false;
	}

	if (source.size() >= 3 &&
		static_cast<unsigned char>(source[0]) == 0xEF &&
		static_cast<unsigned char>(source[1]) == 0xBB &&
		static_cast<unsigned char>(source[2]) == 0xBF)
	{
		source.erase(0, 3);
	}
	return true;
}


std::string ShaderProfile(const std::string& source,
	const std::string& defaultProfile)
{
	static const std::string marker = "$MinimumShaderProfile:";
	std::istringstream stream(source);
	std::string line;
	for (int lineNumber = 0; lineNumber < 20 && std::getline(stream, line); ++lineNumber)
	{
		const size_t markerPos = line.find(marker);
		if (markerPos == std::string::npos)
			continue;

		const std::string profile = NormalizeProfile(
			line.substr(markerPos + marker.size()));
		return IsSupportedProfile(profile) ? profile : std::string();
	}
	return defaultProfile;
}


bool ApplyStage(IMadVRExternalPixelShaders* shaderInterface,
	const std::vector<ShaderEntry>& entries, int stage,
	const char* stageName, const std::string& defaultProfile,
	std::vector<ActiveMadVRShader>& activeShaders)
{
	if (entries.empty())
		return false;

	HRESULT hr = shaderInterface->ClearPixelShaders(stage);
	if (FAILED(hr))
	{
		DebugLog::Log("Shaders: failed to clear %s stage (HRESULT=0x%08lx)",
			stageName, static_cast<unsigned long>(hr));
		return false;
	}

	std::vector<ActiveMadVRShader> stageShaders;
	for (const ShaderEntry& entry : entries)
	{
		const auto totalStarted = std::chrono::steady_clock::now();
		std::string source;
		if (!ReadShader(entry.path, source))
		{
			shaderInterface->ClearPixelShaders(stage);
			DebugLog::Log("Shaders: %s stage disabled because shader #%u could not be loaded",
				stageName, entry.order);
			return false;
		}
		const auto readFinished = std::chrono::steady_clock::now();
		if (!ApplyShaderParameters(source, entry.parameters, entry.path))
		{
			shaderInterface->ClearPixelShaders(stage);
			return false;
		}

		const std::string profile = ShaderProfile(source, defaultProfile);
		if (profile.empty())
		{
			shaderInterface->ClearPixelShaders(stage);
			DebugLog::Log("Shaders: %s shader #%u has an unsupported D3D9 profile in \"%s\"",
				stageName, entry.order, entry.path.u8string().c_str());
			return false;
		}
		const auto prepareFinished = std::chrono::steady_clock::now();

		DebugLog::Log("Shaders: applying %s shader #%u \"%s\" (profile=%s)",
			stageName, entry.order, entry.path.u8string().c_str(), profile.c_str());
		hr = shaderInterface->AddPixelShader(source.c_str(), profile.c_str(), stage, nullptr);
		const auto installFinished = std::chrono::steady_clock::now();
		if (FAILED(hr))
		{
			shaderInterface->ClearPixelShaders(stage);
			DebugLog::Log("Shaders: compilation/install failed for %s shader #%u \"%s\" (HRESULT=0x%08lx); stage cleared",
				stageName, entry.order, entry.path.u8string().c_str(),
				static_cast<unsigned long>(hr));
			return false;
		}
		const auto milliseconds = [](const auto& start, const auto& finish)
		{
			return std::chrono::duration<double, std::milli>(
				finish - start).count();
		};
		DebugLog::Log(
			"Shaders: %s shader #%u timing read=%.3fms prepare=%.3fms install=%.3fms total=%.3fms",
			stageName, entry.order,
			milliseconds(totalStarted, readFinished),
			milliseconds(readFinished, prepareFinished),
			milliseconds(prepareFinished, installFinished),
			milliseconds(totalStarted, installFinished));

		std::string displayName = entry.path.stem().u8string();
		if (!entry.displayName.empty())
			displayName = entry.displayName + " (" + displayName + ")";
		stageShaders.push_back({ displayName,
			stage == MADVR_SHADER_STAGE_POST_SCALE });
	}

	DebugLog::Log("Shaders: %s stage ACTIVE with %u shader(s)",
		stageName, static_cast<unsigned int>(stageShaders.size()));
	activeShaders.insert(activeShaders.end(), stageShaders.begin(), stageShaders.end());
	return true;
}


void AppendLabel(std::string& combined, const std::string& value)
{
	if (value.empty())
		return;
	if (!combined.empty())
		combined += " + ";
	combined += value;
}


void AppendRuleEntries(const ShaderRule& rule,
	std::vector<ShaderEntry>& preScale, std::vector<ShaderEntry>& postScale)
{
	auto append = [&rule](const std::vector<ShaderEntry>& source,
		std::vector<ShaderEntry>& target)
	{
		for (ShaderEntry entry : source)
		{
			entry.order = static_cast<unsigned int>(target.size() + 1);
			entry.displayName = rule.label;
			entry.parameters = rule.parameters;
			target.push_back(std::move(entry));
		}
	};
	append(rule.preScale, preScale);
	append(rule.postScale, postScale);
}


bool ResolveNlsRuleForFrame(ShaderRule& rule,
	const MadVRShaderRuntimeSnapshot& runtime, const VideoState& videoState,
	unsigned long& outputAspectX, unsigned long& outputAspectY,
	bool& waiting)
{
	waiting = false;
	const double targetAspect = GetNlsTargetAspect(rule);
	if (!rule.nls || targetAspect <= 0.0)
		return true;

	double activeAspect = runtime.activeGeometry.aspectRatio;
	MadVRActivePictureGeometry activeGeometry = runtime.activeGeometry;
	const bool currentGeometry =
		MadVRNlsOutputContractIsPrepared(runtime);
	if (runtime.nlsMode == MadVRNlsMappingMode::WAITING ||
		!currentGeometry)
	{
		outputAspectX = 0;
		outputAspectY = 0;
		waiting = true;
		DebugLog::Log(
			"Shaders: NLS mapping waiting requested=%s effective=%s renderer_generation=%llu last_safe=%s",
			runtime.requestedRule.c_str(), runtime.effectiveRule.c_str(),
			static_cast<unsigned long long>(runtime.rendererGeneration),
			MadVRNlsMappingModeName(runtime.lastSafeNlsMode));
		return true;
	}
	// The target output contract is exposed only after the exact, source-owned
	// crop is current for this renderer generation. Merely arming NLS must not
	// cause madVR to fit the raster as though a mapping already exists.
	ResolveMadVRNlsOutputAspect(targetAspect, outputAspectX, outputAspectY);
	if (activeAspect <= 0.0 || !videoState.displayMode)
		return true;

	const double rasterAspect =
		static_cast<double>(videoState.displayMode->FrameWidth()) /
		std::max<long>(1, videoState.displayMode->FrameHeight());
	const double heightFraction =
		std::clamp(rasterAspect / activeAspect, 0.25, 1.0);
	const bool verticalWarp =
		runtime.nlsMode == MadVRNlsMappingMode::ACTIVE &&
		activeAspect > targetAspect;
	const double stretchRatio =
		runtime.nlsMode == MadVRNlsMappingMode::SCOPE_PASSTHROUGH ?
			1.0 : std::clamp(verticalWarp ?
				activeAspect / targetAspect : targetAspect / activeAspect,
				1.0, 1.5);
	const bool safeFit =
		runtime.nlsMode == MadVRNlsMappingMode::SAFE_FIT;
	const bool safeFitVertical = safeFit && activeAspect > targetAspect;
	const double safeFitFraction = safeFit ?
		std::clamp(std::min(activeAspect, targetAspect) /
			std::max(activeAspect, targetAspect), 0.01, 1.0) : 1.0;
	auto coordinateText = [](double value)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(8) << value;
		return text.str();
	};
	rule.parameters["active_height_fraction"] =
		coordinateText(heightFraction);
	rule.parameters["active_left"] = coordinateText(activeGeometry.left);
	rule.parameters["active_top"] = coordinateText(activeGeometry.top);
	rule.parameters["active_right"] = coordinateText(activeGeometry.right);
	rule.parameters["active_bottom"] = coordinateText(activeGeometry.bottom);
	rule.parameters["stretch_ratio"] = coordinateText(stretchRatio);
	rule.parameters["warp_axis"] = verticalWarp ? "1" : "0";
	rule.parameters["safe_fit"] = safeFit ? "1" : "0";
	rule.parameters["safe_fit_axis"] = safeFitVertical ? "1" : "0";
	rule.parameters["safe_fit_fraction"] =
		coordinateText(safeFitFraction);
	DebugLog::Log(
		"Shaders: NLS mapping=%s rect=%.5f,%.5f-%.5f,%.5f active_generation=%llu source=%.4f target=%.4f axis=%s stretch=%.5f safe_fit_fraction=%.5f renderer_generation=%llu",
		MadVRNlsMappingModeName(runtime.nlsMode),
		activeGeometry.left, activeGeometry.top,
		activeGeometry.right, activeGeometry.bottom,
		static_cast<unsigned long long>(activeGeometry.generation),
		activeAspect, targetAspect,
		verticalWarp ? "vertical" : "horizontal", stretchRatio,
		safeFitFraction,
		static_cast<unsigned long long>(runtime.rendererGeneration));
	return true;
}


void ApplyShaderEntries(IBaseFilter* renderer, const std::vector<ShaderEntry>& preScale,
	const std::vector<ShaderEntry>& postScale, const std::string& defaultProfile,
	MadVRShaderSelection& selection)
{
	CComQIPtr<IMadVRExternalPixelShaders> shaderInterface(renderer);
	if (!shaderInterface)
	{
		DebugLog::Log("Shaders: configured, but the selected renderer does not expose the required external shader interface");
		return;
	}

	// A runtime rule change replaces the complete VP-managed shader chain. This
	// also makes a none=true rule reliably turn a previously active shader off.
	shaderInterface->ClearPixelShaders(MADVR_SHADER_STAGE_PRE_SCALE);
	shaderInterface->ClearPixelShaders(MADVR_SHADER_STAGE_POST_SCALE);

	const bool preActive = ApplyStage(shaderInterface, preScale,
		MADVR_SHADER_STAGE_PRE_SCALE, "pre-resize", defaultProfile,
		selection.activeShaders);
	const bool postActive = ApplyStage(shaderInterface, postScale,
		MADVR_SHADER_STAGE_POST_SCALE, "post-resize", defaultProfile,
		selection.activeShaders);
	DebugLog::Log("Shaders: configuration complete (pre-resize=%s, post-resize=%s)",
		preActive ? "active" : "inactive", postActive ? "active" : "inactive");
}
}


MadVRShaderSelection MadVRShaderLoader::ApplyConfiguredShaders(IBaseFilter* renderer,
	const VideoState& videoState)
{
	MadVRShaderSelection selection;
	if (!renderer)
		return selection;
	ActivePictureTransitionModel::SetRuntimeStableGeometryDeadbandPercent(
		ActivePictureTransitionModel::DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT);

	ConfigFile config;
	if (!config.Load() || !config.HasSection(CONFIG_SECTION))
		return selection;

	bool enabled = false;
	std::string rawValue;
	if (config.TryGetString(CONFIG_SECTION, "enabled", rawValue) &&
		!config.TryGetBool(CONFIG_SECTION, "enabled", enabled))
	{
		DebugLog::Log("Shaders: invalid enabled value \"%s\"; shaders disabled",
			rawValue.c_str());
		return selection;
	}
	if (!enabled)
	{
		DebugLog::Log("Shaders: disabled by VideoProcessor.cfg");
		return selection;
	}

	std::string defaultProfile = "ps_3_0";
	std::string configuredProfile;
	if (!config.TryGetString(CONFIG_SECTION, "fallback_shader_model", configuredProfile))
		config.TryGetString(CONFIG_SECTION, "profile", configuredProfile);
	if (!configuredProfile.empty())
	{
		defaultProfile = NormalizeProfile(configuredProfile);
		if (!IsSupportedProfile(defaultProfile))
		{
			DebugLog::Log("Shaders: unsupported fallback shader model \"%s\"; expected ps_2_0, ps_2_a, ps_2_b, or ps_3_0",
				configuredProfile.c_str());
			return selection;
		}
	}

	std::string configuredDefault = "none";
	config.TryGetString(CONFIG_SECTION, "default", configuredDefault);
	if (ConfigFile::NormalizeName(configuredDefault) != "none")
	{
		DebugLog::Log("Shaders: unsupported default \"%s\"; only none is currently supported",
			configuredDefault.c_str());
		return selection;
	}

	const double refreshRate = videoState.displayMode ?
		videoState.displayMode->RefreshRateHz() : 0.0;
	const int nominalRate = refreshRate > 0.0 ?
		static_cast<int>(std::floor(refreshRate + 0.0001)) : 0;
	DebugLog::Log("Shaders: evaluating input signal=%s refresh=%.6f Hz nominal=%d",
		SignalName(videoState.eotf), refreshRate, nominalRate);

	std::string ruleList;
	const MadVRShaderRuntimeSnapshot runtime = g_runtimeState.GetSnapshot();
	const std::string runtimeRule = runtime.effectiveRule;
	if (!config.TryGetString(CONFIG_SECTION, "rules", ruleList))
	{
		DebugLog::Log(
			"Shaders: enabled, but no effect names were configured in rules");
		return selection;
	}

	std::vector<ShaderRule> availableRules;
	std::set<std::string> seenRules;
	for (const std::string& configuredName : SplitList(ruleList))
	{
		const std::string normalizedName = ConfigFile::NormalizeName(configuredName);
		if (!seenRules.insert(normalizedName).second)
		{
			DebugLog::Log("Shaders: ignoring duplicate rule \"%s\"", configuredName.c_str());
			continue;
		}

		ShaderRule rule = LoadRule(config, configuredName);
		if (!rule.valid)
			continue;
		availableRules.push_back(std::move(rule));
	}

	std::vector<ShaderRule> selectedRules;
	if (!runtimeRule.empty())
	{
		std::set<std::string> selectedNames;
		for (const std::string& selectedName : SplitList(runtimeRule))
		{
			const std::string normalized =
				ConfigFile::NormalizeName(selectedName);
			if (!selectedNames.insert(normalized).second)
				continue;
			const auto found = std::find_if(availableRules.begin(),
				availableRules.end(), [&normalized](const ShaderRule& rule)
				{
					return rule.name == normalized;
				});
			if (found == availableRules.end())
			{
				DebugLog::Log(
					"Shaders: selected effect \"%s\" is missing or invalid",
					selectedName.c_str());
				return selection;
			}
			if (RuleAppliesToBackend(*found,
				ShaderRendererBackend::MADVR))
			{
				selectedRules.push_back(*found);
			}
		}
	}
	else
	{
		const auto found = std::find_if(availableRules.begin(),
			availableRules.end(), [&videoState, nominalRate](
				const ShaderRule& rule)
			{
				return RuleAppliesToBackend(rule,
					ShaderRendererBackend::MADVR) &&
					!rule.manual &&
					RuleMatches(rule, videoState.eotf, nominalRate);
			});
		if (found != availableRules.end())
			selectedRules.push_back(*found);
	}

	if (selectedRules.empty())
	{
		DebugLog::Log(
			"Shaders: no effect matched signal=%s refresh=%.6f Hz nominal=%d; default=none",
			SignalName(videoState.eotf), refreshRate, nominalRate);
		return selection;
	}

	const size_t noneCount = static_cast<size_t>(std::count_if(
		selectedRules.begin(), selectedRules.end(),
		[](const ShaderRule& rule) { return rule.none; }));
	const size_t nlsCount = static_cast<size_t>(std::count_if(
		selectedRules.begin(), selectedRules.end(),
		[](const ShaderRule& rule) { return rule.nls; }));
	if ((noneCount > 0 && selectedRules.size() > 1) || nlsCount > 1)
	{
		DebugLog::Log(
			"Shaders: invalid effect group \"%s\"; NONE must be exclusive and only one NLS effect may be active",
			runtimeRule.c_str());
		selection.ruleLabel = "Invalid shader group";
		ApplyShaderEntries(renderer, {}, {}, defaultProfile, selection);
		return selection;
	}
	for (const ShaderRule& rule : selectedRules)
	{
		if (rule.nls)
		{
			ActivePictureTransitionModel::SetRuntimeStableGeometryDeadbandPercent(
				rule.stableGeometryDeadbandPercent);
			break;
		}
	}

	std::vector<ShaderEntry> preScale;
	std::vector<ShaderEntry> postScale;
	selection.ruleLabel.clear();
	for (ShaderRule& rule : selectedRules)
	{
		if (!selection.ruleName.empty())
			selection.ruleName += ",";
		selection.ruleName += rule.name;
		AppendLabel(selection.ruleLabel, rule.label);
		if (!rule.nls && !rule.none)
			AppendLabel(selection.companionRuleLabel, rule.label);

		unsigned long ruleAspectX = rule.outputAspectRatioX;
		unsigned long ruleAspectY = rule.outputAspectRatioY;
		bool waiting = false;
		ResolveNlsRuleForFrame(rule, runtime, videoState,
			ruleAspectX, ruleAspectY, waiting);
		if (ruleAspectX > 0 && ruleAspectY > 0)
		{
			if (selection.outputAspectRatioX > 0 &&
				selection.outputAspectRatioY > 0 &&
				static_cast<unsigned long long>(
					selection.outputAspectRatioX) * ruleAspectY !=
				static_cast<unsigned long long>(ruleAspectX) *
					selection.outputAspectRatioY)
			{
				DebugLog::Log(
					"Shaders: effect group \"%s\" requests conflicting output aspects",
					runtimeRule.c_str());
				selection.ruleLabel = "Invalid shader group";
				ApplyShaderEntries(renderer, {}, {}, defaultProfile,
					selection);
				return selection;
			}
			selection.outputAspectRatioX = ruleAspectX;
			selection.outputAspectRatioY = ruleAspectY;
		}

		DebugLog::Log(
			"Shaders: selected effect \"%s\" (%s) for signal=%s refresh=%.6f Hz nominal=%d",
			rule.name.c_str(), rule.label.c_str(), SignalName(videoState.eotf),
			refreshRate, nominalRate);
		if (!rule.none && !waiting)
			AppendRuleEntries(rule, preScale, postScale);
		if (waiting)
		{
			DebugLog::Log(
				"Shaders: effect \"%s\" is waiting; other effects in the group remain active",
				rule.name.c_str());
		}
	}

	if (noneCount > 0)
		DebugLog::Log("Shaders: selected effect explicitly requests no shaders");
	else if (preScale.empty() && postScale.empty())
		DebugLog::Log("Shaders: selected effect group contains no active shader files");
	else
		DebugLog::Log(
			"Shaders: loading selected effect group from \"%s\" (fallback shader model=%s)",
			config.GetLoadedPath().c_str(), defaultProfile.c_str());
	ApplyShaderEntries(renderer, preScale, postScale, defaultProfile, selection);
	return selection;
}


MadVRShaderSelection MadVRShaderLoader::ApplyConfiguredShaderRule(IBaseFilter* renderer,
	const VideoState& videoState, const std::string& ruleName,
	bool updateRuntimeRequest)
{
	if (updateRuntimeRequest)
	{
		const std::string normalizedRule = ConfigFile::NormalizeName(ruleName);
		g_runtimeState.SetRequestedRule(normalizedRule);
		g_runtimeState.SetEffectiveRule(normalizedRule);
		DebugLog::Log("Shaders: manual runtime request changed to \"%s\"",
			ruleName.empty() ? "automatic" : ruleName.c_str());
	}
	else
	{
		g_runtimeState.SetEffectiveRule(ConfigFile::NormalizeName(ruleName));
		DebugLog::Log("Shaders: applying temporary effective rule \"%s\" without changing manual request",
			ruleName.c_str());
	}
	return ApplyConfiguredShaders(renderer, videoState);
}

bool MadVRShaderLoader::GetRuntimeOutputAspectRatio(unsigned long& aspectX,
	unsigned long& aspectY)
{
	aspectX = 0;
	aspectY = 0;
	const auto runtime = g_runtimeState.GetSnapshot();
	const std::string runtimeRule = runtime.effectiveRule;
	if (runtimeRule.empty())
		return false;

	ConfigFile config;
	if (!config.Load())
		return false;
	std::vector<ShaderRule> rules;
	if (!LoadRuleSelection(config, runtimeRule, rules))
		return false;
	for (const ShaderRule& rule : rules)
	{
		unsigned long ruleX = rule.outputAspectRatioX;
		unsigned long ruleY = rule.outputAspectRatioY;
		const bool currentNlsGeometry =
			MadVRNlsOutputContractIsPrepared(runtime);
		if (rule.nls && GetNlsTargetAspect(rule) > 0.0)
		{
			if (currentNlsGeometry)
				ResolveMadVRNlsOutputAspect(
					GetNlsTargetAspect(rule), ruleX, ruleY);
			else
			{
				ruleX = 0;
				ruleY = 0;
			}
		}
		if (ruleX == 0 || ruleY == 0)
			continue;
		if (aspectX > 0 && aspectY > 0 &&
			static_cast<unsigned long long>(aspectX) * ruleY !=
			static_cast<unsigned long long>(ruleX) * aspectY)
		{
			aspectX = 0;
			aspectY = 0;
			return false;
		}
		aspectX = ruleX;
		aspectY = ruleY;
	}
	return aspectX > 0 && aspectY > 0;
}


bool MadVRShaderLoader::ValidateActivePictureAspect(const std::string& ruleName,
	bool aspectAvailable, double activeAspectRatio, std::string& reason)
{
	reason.clear();
	ConfigFile config;
	if (!config.Load())
	{
		reason = "configuration file is unavailable";
		return false;
	}
	std::vector<ShaderRule> rules;
	if (!LoadRuleSelection(config, ruleName, rules))
	{
		reason = "shader rule is invalid";
		return false;
	}
	const size_t nlsCount = static_cast<size_t>(std::count_if(
		rules.begin(), rules.end(),
		[](const ShaderRule& rule) { return rule.nls; }));
	ShaderRule* selected = FindNlsRule(rules);
	if (nlsCount > 1)
	{
		reason = "an effect group may contain only one NLS effect";
		return false;
	}
	const ShaderRule& rule = selected ? *selected : rules.front();
	if (rule.aspectTolerancePercent < 0.0)
		return true;
	if (!aspectAvailable || activeAspectRatio <= 0.0)
	{
		reason = "active picture aspect is not stable yet";
		return false;
	}
	const double target = GetNlsTargetAspect(rule);
	if (target <= 0.0)
	{
		reason = "aspect guard requires nls_target_aspect_ratio";
		return false;
	}
	if (rule.activeAspectMinimum > 0.0 &&
		activeAspectRatio < rule.activeAspectMinimum)
	{
		std::ostringstream message;
		message << "active picture " << activeAspectRatio <<
			" is below minimum " << rule.activeAspectMinimum;
		reason = message.str();
		return false;
	}
	const double signedDifferencePercent =
		(target - activeAspectRatio) * 100.0 / target;
	const bool allowed = rule.narrowerOnly ?
		signedDifferencePercent > rule.aspectTolerancePercent :
		std::abs(signedDifferencePercent) > rule.aspectTolerancePercent;
	if (!allowed)
	{
		std::ostringstream message;
		message << "active picture " << activeAspectRatio
			<< " is already within " << rule.aspectTolerancePercent
			<< "% of target " << target;
		if (rule.narrowerOnly)
			message << ", or is wider than that target";
		reason = message.str();
	}
	return allowed;
}


bool MadVRShaderLoader::EvaluateNlsMapping(const std::string& ruleName,
	bool aspectAvailable, double activeAspectRatio,
	MadVRNlsMappingDecision& decision, ShaderRendererBackend backend)
{
	decision = {};
	ConfigFile config;
	if (!config.Load())
	{
		decision.reason = "configuration file is unavailable";
		return false;
	}
	std::vector<ShaderRule> rules;
	if (!LoadRuleSelectionForBackend(config, ruleName, backend, rules))
	{
		decision.reason = "shader rule is invalid";
		return false;
	}
	const size_t nlsCount = static_cast<size_t>(std::count_if(
		rules.begin(), rules.end(),
		[](const ShaderRule& rule) { return rule.nls; }));
	ShaderRule* selected = FindNlsRule(rules);
	if (nlsCount != 1 || !selected)
	{
		decision.reason = nlsCount > 1 ?
			"an effect group may contain only one NLS effect" :
			"shader rule does not define NLS mapping";
		return false;
	}
	const ShaderRule& rule = *selected;
	const double target = GetNlsTargetAspect(rule);
	if (!rule.nls || target <= 0.0)
	{
		decision.reason = "shader rule does not define NLS mapping";
		return false;
	}
	decision = EvaluateMadVRNlsMapping(aspectAvailable, activeAspectRatio,
		target, std::max(0.0, rule.aspectTolerancePercent),
		rule.activeAspectMinimum, rule.narrowerOnly);
	g_runtimeState.SetNlsDecision(decision);
	return true;
}


bool MadVRShaderLoader::SetRuntimeActivePictureGeometry(
	const MadVRActivePictureGeometry& geometry)
{
	return g_runtimeState.SetActiveGeometry(geometry);
}

void MadVRShaderLoader::SetRuntimeNlsTargetAspect(double targetAspect)
{
	g_runtimeState.SetNlsTargetAspect(targetAspect);
}


void MadVRShaderLoader::SetRuntimeShaderSelection(
	const std::string& requestedRule, const std::string& effectiveRule,
	MadVRNlsMappingMode nlsMode)
{
	g_runtimeState.SetRuleSelection(
		ConfigFile::NormalizeName(requestedRule),
		ConfigFile::NormalizeName(effectiveRule), nlsMode);
}


MadVRShaderRuntimeSnapshot MadVRShaderLoader::GetRuntimeShaderState()
{
	return g_runtimeState.GetSnapshot();
}


uint64_t MadVRShaderLoader::BeginRendererGeneration()
{
	return g_runtimeState.BeginRendererGeneration();
}

bool MadVRShaderLoader::PrepareNlsOutputContractRendererReplacement()
{
	return g_runtimeState.PrepareNlsOutputContractRendererReplacement();
}


bool MadVRShaderLoader::GetRuleActivationInfo(const std::string& ruleName,
	std::string& label, std::string& inactiveRule, bool& nlsMapping,
	ShaderRendererBackend backend)
{
	label.clear();
	inactiveRule.clear();
	nlsMapping = false;
	ConfigFile config;
	if (!config.Load())
		return false;
	std::vector<ShaderRule> rules;
	if (!LoadRuleSelectionForBackend(config, ruleName, backend, rules))
		return false;
	const size_t noneCount = static_cast<size_t>(std::count_if(
		rules.begin(), rules.end(),
		[](const ShaderRule& rule) { return rule.none; }));
	const size_t nlsCount = static_cast<size_t>(std::count_if(
		rules.begin(), rules.end(),
		[](const ShaderRule& rule) { return rule.nls; }));
	if ((noneCount > 0 && rules.size() > 1) || nlsCount > 1)
		return false;
	for (const ShaderRule& rule : rules)
	{
		AppendLabel(label, rule.label);
		if (rule.nls)
		{
			inactiveRule = rule.inactiveRule;
			nlsMapping = GetNlsTargetAspect(rule) > 0.0;
		}
	}
	return true;
}


bool MadVRShaderLoader::GetConfiguredRuleSelection(
	const std::string& ruleName, ShaderRendererBackend backend,
	std::vector<ConfiguredShaderRule>& selection, std::string& reason)
{
	selection.clear();
	reason.clear();
	ConfigFile config;
	if (!config.Load())
	{
		reason = "configuration file is unavailable";
		return false;
	}

	std::vector<ShaderRule> rules;
	if (!LoadRuleSelectionForBackend(config, ruleName, backend, rules))
	{
		reason = "shader rule is invalid or not applicable to this renderer";
		return false;
	}
	const size_t noneCount = static_cast<size_t>(std::count_if(
		rules.begin(), rules.end(),
		[](const ShaderRule& rule) { return rule.none; }));
	const size_t nlsCount = static_cast<size_t>(std::count_if(
		rules.begin(), rules.end(),
		[](const ShaderRule& rule) { return rule.nls; }));
	if ((noneCount > 0 && rules.size() > 1) || nlsCount > 1)
	{
		reason =
			"NONE must be exclusive and only one NLS effect may be active";
		return false;
	}

	for (const ShaderRule& rule : rules)
		selection.push_back(ToConfiguredShaderRule(rule));
	return true;
}


bool MadVRShaderLoader::GetConfiguredNlsPrewarmRules(
	std::vector<ConfiguredShaderRule>& rules,
	std::string& reason)
{
	rules.clear();
	reason.clear();
	ConfigFile config;
	if (!config.Load() || !config.HasSection(CONFIG_SECTION))
	{
		reason = "shader configuration is unavailable";
		return false;
	}

	bool enabled = false;
	if (!config.TryGetBool(CONFIG_SECTION, "enabled", enabled) || !enabled)
	{
		reason = "shaders are disabled";
		return false;
	}

	std::string ruleList;
	if (!config.TryGetString(CONFIG_SECTION, "rules", ruleList))
	{
		reason = "no shader rules are configured";
		return false;
	}

	std::set<std::string> seen;
	for (const std::string& configuredName : SplitList(ruleList))
	{
		const std::string name = ConfigFile::NormalizeName(configuredName);
		if (!seen.insert(name).second)
			continue;
		ShaderRule rule = LoadRule(config, name);
		if (!rule.valid || !rule.nls ||
			!RuleAppliesToBackend(rule, ShaderRendererBackend::LIBPLACEBO))
		{
			continue;
		}
		rules.push_back(ToConfiguredShaderRule(rule));
	}
	if (rules.empty())
	{
		reason = "no Alpha-compatible NLS rules are configured";
		return false;
	}
	return true;
}


bool MadVRShaderLoader::IsShaderFilenameCompatible(
	const std::string& filename, ShaderRendererBackend backend)
{
	ShaderRule rule;
	rule.sourceBackend = filename.empty()
		? ShaderSourceBackend::ANY
		: SourceBackendForFilename(filename);
	return RuleAppliesToBackend(rule, backend);
}


bool MadVRShaderLoader::ResolveShaderFilename(
	const std::string& filename, const std::string& executablePath,
	std::string& resolvedPath, std::string& error)
{
	resolvedPath.clear();
	error.clear();
	const std::string trimmed = ConfigFile::Trim(filename);
	if (trimmed.empty())
	{
		error = "filename is empty";
		return false;
	}
	const std::filesystem::path configured =
		std::filesystem::u8path(trimmed);
	if (configured.is_absolute() || configured.has_root_path() ||
		configured.has_parent_path() ||
		configured.filename() != configured ||
		configured == "." || configured == ".." ||
		trimmed.find_first_of("<>:\"/\\|?*") != std::string::npos ||
		std::any_of(trimmed.begin(), trimmed.end(),
			[](unsigned char value) { return value < 32; }))
	{
		error = "use a filename only; VP always loads from the executable's shaders directory";
		return false;
	}
	const std::filesystem::path executable =
		std::filesystem::u8path(executablePath);
	if (executable.empty() || !executable.has_parent_path())
	{
		error = "executable directory is unavailable";
		return false;
	}
	resolvedPath = (executable.parent_path() / "shaders" /
		configured).lexically_normal().u8string();
	return true;
}
