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
#include <microsoft_directshow/MadVRExternalPixelShaders.h>

#include "MadVRShaderLoader.h"

#include <algorithm>
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

struct ShaderEntry
{
	unsigned int order = 0;
	std::filesystem::path path;
};

struct ShaderRule
{
	std::string name;
	std::string label;
	SignalMatch signal = SignalMatch::ANY;
	std::vector<int> nominalRates;
	bool none = false;
	bool manual = false;
	bool valid = true;
	std::map<std::string, std::string> parameters;
	unsigned long outputAspectRatioX = 0;
	unsigned long outputAspectRatioY = 0;
	// This is deliberately separate from outputAspectRatio.  madVR's screen
	// profile owns presentation; NLS only needs a geometric target.
	unsigned long nlsTargetAspectRatioX = 0;
	unsigned long nlsTargetAspectRatioY = 0;
	double aspectTolerancePercent = -1.0;
	double activeAspectMinimum = 0.0;
	bool narrowerOnly = false;
	std::string inactiveRule;
	std::vector<ShaderEntry> preScale;
	std::vector<ShaderEntry> postScale;
};


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


std::string ParentDirectory(const std::string& path)
{
	const size_t slashPos = path.find_last_of("\\/");
	return slashPos == std::string::npos ? std::string() : path.substr(0, slashPos);
}


std::filesystem::path ResolveShaderPath(const std::string& configuredPath,
	const std::string& configPath)
{
	std::filesystem::path path = std::filesystem::u8path(configuredPath);
	if (path.is_absolute())
		return path.lexically_normal();

	const std::string configDirectory = ParentDirectory(configPath);
	if (!configDirectory.empty())
		return (std::filesystem::u8path(configDirectory) / path).lexically_normal();

	return path.lexically_normal();
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


bool ParseOrderedKey(const std::string& key, const char* prefix,
	unsigned int& order)
{
	const std::string prefixString(prefix);
	if (key.compare(0, prefixString.size(), prefixString) != 0)
		return false;

	const std::string suffix = key.substr(prefixString.size());
	if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(),
		[](unsigned char c) { return std::isdigit(c) != 0; }))
	{
		return false;
	}

	try
	{
		const unsigned long parsed = std::stoul(suffix);
		if (parsed == 0 || parsed > std::numeric_limits<unsigned int>::max())
			return false;
		order = static_cast<unsigned int>(parsed);
		return true;
	}
	catch (...)
	{
		return false;
	}
}


void SortShaderEntries(std::vector<ShaderEntry>& entries)
{
	std::sort(entries.begin(), entries.end(),
		[](const ShaderEntry& left, const ShaderEntry& right)
		{
			return left.order < right.order;
		});
}


void LoadShaderEntries(const ConfigFile& config, const std::string& section,
	const std::set<std::string>& nonShaderKeys, std::vector<ShaderEntry>& preScale,
	std::vector<ShaderEntry>& postScale)
{
	const auto* settings = config.GetSectionValues(section);
	if (!settings)
		return;

	for (const auto& setting : *settings)
	{
		if (nonShaderKeys.find(setting.first) != nonShaderKeys.end())
			continue;
		if (setting.first.compare(0, 6, "param_") == 0)
			continue;

		unsigned int order = 0;
		std::vector<ShaderEntry>* target = nullptr;
		if (ParseOrderedKey(setting.first, "pre_resize_", order))
			target = &preScale;
		else if (ParseOrderedKey(setting.first, "post_resize_", order))
			target = &postScale;
		else
		{
			DebugLog::Log("Shaders: ignoring unknown [%s] key \"%s\"",
				section.c_str(), setting.first.c_str());
			continue;
		}

		if (setting.second.empty())
		{
			DebugLog::Log("Shaders: ignoring empty [%s] key \"%s\"",
				section.c_str(), setting.first.c_str());
			continue;
		}

		target->push_back({ order, ResolveShaderPath(setting.second,
			config.GetLoadedPath()) });
	}

	SortShaderEntries(preScale);
	SortShaderEntries(postScale);
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
	// Preserve compatibility with configurations created before the selectable
	// NLS geometry was introduced. Parameters unused by other shaders are inert.
	rule.parameters["geometry"] = "0";
	rule.parameters["center_protection"] = "0.35";
	rule.parameters["active_height_fraction"] = "1.0";
	rule.parameters["active_left"] = "0.0";
	rule.parameters["active_top"] = "0.0";
	rule.parameters["active_right"] = "1.0";
	rule.parameters["active_bottom"] = "1.0";
	rule.parameters["warp_axis"] = "0";

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

	if (config.TryGetString(section, "manual", rawValue) &&
		!config.TryGetBool(section, "manual", rule.manual))
	{
		DebugLog::Log("Shaders: rule \"%s\" has invalid manual value \"%s\"",
			rule.name.c_str(), rawValue.c_str());
		rule.valid = false;
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

	LoadShaderEntries(config, section,
		{ "label", "signal", "frame_rates", "none", "manual", "shortcut",
			"output_aspect_ratio", "nls_target_aspect_ratio", "aspect_tolerance_percent",
			"active_aspect_min", "aspect_direction", "inactive_rule" },
		rule.preScale, rule.postScale);
	return rule;
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
	const std::map<std::string, std::string>& parameters,
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
		std::string source;
		if (!ReadShader(entry.path, source))
		{
			shaderInterface->ClearPixelShaders(stage);
			DebugLog::Log("Shaders: %s stage disabled because shader #%u could not be loaded",
				stageName, entry.order);
			return false;
		}
		if (!ApplyShaderParameters(source, parameters, entry.path))
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

		DebugLog::Log("Shaders: applying %s shader #%u \"%s\" (profile=%s)",
			stageName, entry.order, entry.path.u8string().c_str(), profile.c_str());
		hr = shaderInterface->AddPixelShader(source.c_str(), profile.c_str(), stage, nullptr);
		if (FAILED(hr))
		{
			shaderInterface->ClearPixelShaders(stage);
			DebugLog::Log("Shaders: compilation/install failed for %s shader #%u \"%s\" (HRESULT=0x%08lx); stage cleared",
				stageName, entry.order, entry.path.u8string().c_str(),
				static_cast<unsigned long>(hr));
			return false;
		}

		stageShaders.push_back({ entry.path.stem().u8string(),
			stage == MADVR_SHADER_STAGE_POST_SCALE });
	}

	DebugLog::Log("Shaders: %s stage ACTIVE with %u shader(s)",
		stageName, static_cast<unsigned int>(stageShaders.size()));
	activeShaders.insert(activeShaders.end(), stageShaders.begin(), stageShaders.end());
	return true;
}


void ApplyShaderEntries(IBaseFilter* renderer, const std::vector<ShaderEntry>& preScale,
	const std::vector<ShaderEntry>& postScale, const std::string& defaultProfile,
	const std::map<std::string, std::string>& parameters,
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
		parameters,
		selection.activeShaders);
	const bool postActive = ApplyStage(shaderInterface, postScale,
		MADVR_SHADER_STAGE_POST_SCALE, "post-resize", defaultProfile,
		parameters,
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
		// Backward compatibility for the original flat configuration.
		selection.ruleName = "legacy";
		selection.ruleLabel = "All video";
		std::vector<ShaderEntry> preScale;
		std::vector<ShaderEntry> postScale;
		LoadShaderEntries(config, CONFIG_SECTION,
			{ "enabled", "profile", "fallback_shader_model", "default" },
			preScale, postScale);
		if (preScale.empty() && postScale.empty())
		{
			selection.ruleName.clear();
			selection.ruleLabel = "None";
			DebugLog::Log("Shaders: enabled, but no rules or legacy shader entries were configured");
			return selection;
		}
		DebugLog::Log("Shaders: selected legacy all-video configuration");
		ApplyShaderEntries(renderer, preScale, postScale, defaultProfile, {}, selection);
		return selection;
	}

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
		const bool explicitlySelected = !runtimeRule.empty() && rule.name == runtimeRule;
		if (!rule.valid || (!explicitlySelected &&
			(!runtimeRule.empty() || rule.manual || !RuleMatches(rule, videoState.eotf, nominalRate))))
			continue;

		selection.ruleName = rule.name;
		selection.ruleLabel = rule.label;
		selection.outputAspectRatioX = rule.outputAspectRatioX;
		selection.outputAspectRatioY = rule.outputAspectRatioY;
		// NLS rules advertise a target output aspect and contain a stretch-ratio
		// parameter. Derive their active-picture crop, stretch magnitude, and axis
		// from the stable detector result instead of assuming 16:9 content.
		// Narrower content stretches horizontally; wider content stretches
		// vertically. Both paths preserve the complete active image.
		const double activeAspect = runtime.activeGeometry.aspectRatio;
		const MadVRActivePictureGeometry activeGeometry = runtime.activeGeometry;
		const double targetAspect = GetNlsTargetAspect(rule);
		const bool nlsRule = targetAspect > 0.0 &&
			rule.parameters.find("stretch_ratio") != rule.parameters.end();
		const bool currentGeometry = activeGeometry.stable &&
			activeGeometry.rendererGeneration == runtime.rendererGeneration;
		if (nlsRule && (runtime.nlsMode == MadVRNlsMappingMode::WAITING ||
			!currentGeometry))
		{
			selection.ruleLabel = "NLS: Waiting";
			DebugLog::Log(
				"Shaders: NLS mapping waiting requested=%s effective=%s renderer_generation=%llu last_safe=%s",
				runtime.requestedRule.c_str(), runtime.effectiveRule.c_str(),
				static_cast<unsigned long long>(runtime.rendererGeneration),
				MadVRNlsMappingModeName(runtime.lastSafeNlsMode));
			ApplyShaderEntries(renderer, {}, {}, defaultProfile, {},
				selection);
			return selection;
		}
		if (currentGeometry && activeAspect > 0.0 && videoState.displayMode &&
			targetAspect > 0.0 &&
			rule.parameters.find("stretch_ratio") != rule.parameters.end())
		{
			const double rasterAspect = static_cast<double>(videoState.displayMode->FrameWidth()) /
				std::max<long>(1, videoState.displayMode->FrameHeight());
			const double heightFraction = std::clamp(rasterAspect / activeAspect, 0.25, 1.0);
			const bool verticalWarp =
				runtime.nlsMode == MadVRNlsMappingMode::ACTIVE &&
				activeAspect > targetAspect;
			const double stretchRatio =
				runtime.nlsMode == MadVRNlsMappingMode::SCOPE_PASSTHROUGH ?
				1.0 : std::clamp(verticalWarp ?
					activeAspect / targetAspect : targetAspect / activeAspect,
					1.0, 1.5);
			std::ostringstream heightText;
			std::ostringstream stretchText;
			heightText << std::fixed << std::setprecision(8) << heightFraction;
			stretchText << std::fixed << std::setprecision(8) << stretchRatio;
			rule.parameters["active_height_fraction"] = heightText.str();
			auto coordinateText = [](double value)
			{
				std::ostringstream text;
				text << std::fixed << std::setprecision(8) << value;
				return text.str();
			};
			rule.parameters["active_left"] = coordinateText(activeGeometry.left);
			rule.parameters["active_top"] = coordinateText(activeGeometry.top);
			rule.parameters["active_right"] = coordinateText(activeGeometry.right);
			rule.parameters["active_bottom"] = coordinateText(activeGeometry.bottom);
			rule.parameters["stretch_ratio"] = stretchText.str();
			rule.parameters["warp_axis"] = verticalWarp ? "1" : "0";
			DebugLog::Log(
				"Shaders: NLS mapping=%s rect=%.5f,%.5f-%.5f,%.5f active_generation=%llu source=%.4f target=%.4f axis=%s stretch=%.5f renderer_generation=%llu",
				MadVRNlsMappingModeName(runtime.nlsMode),
				activeGeometry.left, activeGeometry.top, activeGeometry.right, activeGeometry.bottom,
				static_cast<unsigned long long>(activeGeometry.generation),
				activeAspect, targetAspect,
				verticalWarp ? "vertical" : "horizontal", stretchRatio,
				static_cast<unsigned long long>(runtime.rendererGeneration));
		}
		DebugLog::Log("Shaders: selected rule \"%s\" (%s) for signal=%s refresh=%.6f Hz nominal=%d",
			rule.name.c_str(), rule.label.c_str(), SignalName(videoState.eotf),
			refreshRate, nominalRate);

		if (rule.none)
		{
			if (!rule.preScale.empty() || !rule.postScale.empty())
				DebugLog::Log("Shaders: rule \"%s\" has none=true; shader entries are ignored",
					rule.name.c_str());
			DebugLog::Log("Shaders: selected rule explicitly requests no shaders");
			ApplyShaderEntries(renderer, {}, {}, defaultProfile, rule.parameters, selection);
			return selection;
		}

		if (rule.preScale.empty() && rule.postScale.empty())
		{
			DebugLog::Log("Shaders: selected rule \"%s\" contains no shader entries",
				rule.name.c_str());
			return selection;
		}

		DebugLog::Log("Shaders: loading selected rule from \"%s\" (fallback shader model=%s)",
			config.GetLoadedPath().c_str(), defaultProfile.c_str());
		ApplyShaderEntries(renderer, rule.preScale, rule.postScale,
			defaultProfile, rule.parameters, selection);
		return selection;
	}

	DebugLog::Log("Shaders: no rule matched signal=%s refresh=%.6f Hz nominal=%d; default=none",
		SignalName(videoState.eotf), refreshRate, nominalRate);
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
	const std::string runtimeRule =
		g_runtimeState.GetSnapshot().effectiveRule;
	if (runtimeRule.empty())
		return false;

	ConfigFile config;
	if (!config.Load())
		return false;
	ShaderRule rule = LoadRule(config, runtimeRule);
	if (!rule.valid || rule.outputAspectRatioX == 0 || rule.outputAspectRatioY == 0)
		return false;
	if (GetNlsTargetAspect(rule) > 0.0 &&
		ResolveMadVRNlsOutputAspect(GetNlsTargetAspect(rule), aspectX, aspectY))
	{
		return true;
	}

	aspectX = rule.outputAspectRatioX;
	aspectY = rule.outputAspectRatioY;
	return true;
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
	const ShaderRule rule = LoadRule(config, ruleName);
	if (!rule.valid)
	{
		reason = "shader rule is invalid";
		return false;
	}
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
	if (rule.parameters.find("stretch_ratio") != rule.parameters.end() &&
		std::max(target / activeAspectRatio, activeAspectRatio / target) > 1.5)
	{
		std::ostringstream message;
		message << "NLS ratio " << std::max(target / activeAspectRatio,
			activeAspectRatio / target) << " exceeds the safe 1.5 limit";
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
	MadVRNlsMappingDecision& decision)
{
	decision = {};
	ConfigFile config;
	if (!config.Load())
	{
		decision.reason = "configuration file is unavailable";
		return false;
	}
	const ShaderRule rule = LoadRule(config, ruleName);
	if (!rule.valid)
	{
		decision.reason = "shader rule is invalid";
		return false;
	}
	const double target = GetNlsTargetAspect(rule);
	if (target <= 0.0 ||
		rule.parameters.find("stretch_ratio") == rule.parameters.end())
	{
		decision.reason = "shader rule does not define NLS mapping";
		return false;
	}
	decision = EvaluateMadVRNlsMapping(aspectAvailable, activeAspectRatio,
		target, std::max(0.0, rule.aspectTolerancePercent),
		rule.activeAspectMinimum, rule.narrowerOnly);
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


bool MadVRShaderLoader::GetRuleActivationInfo(const std::string& ruleName,
	std::string& label, std::string& inactiveRule, bool& nlsMapping)
{
	label.clear();
	inactiveRule.clear();
	nlsMapping = false;
	ConfigFile config;
	if (!config.Load())
		return false;
	const ShaderRule rule = LoadRule(config, ruleName);
	if (!rule.valid)
		return false;
	label = rule.label;
	inactiveRule = rule.inactiveRule;
	nlsMapping = GetNlsTargetAspect(rule) > 0.0 &&
		rule.parameters.find("stretch_ratio") != rule.parameters.end();
	return true;
}
