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
#include <microsoft_directshow/MadVRExternalPixelShaders.h>

#include "MadVRShaderLoader.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
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
	bool valid = true;
	std::vector<ShaderEntry> preScale;
	std::vector<ShaderEntry> postScale;
};


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

	LoadShaderEntries(config, section,
		{ "label", "signal", "frame_rates", "none" }, rule.preScale, rule.postScale);
	return rule;
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
		std::string source;
		if (!ReadShader(entry.path, source))
		{
			shaderInterface->ClearPixelShaders(stage);
			DebugLog::Log("Shaders: %s stage disabled because shader #%u could not be loaded",
				stageName, entry.order);
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
	MadVRShaderSelection& selection)
{
	if (preScale.empty() && postScale.empty())
		return;

	CComQIPtr<IMadVRExternalPixelShaders> shaderInterface(renderer);
	if (!shaderInterface)
	{
		DebugLog::Log("Shaders: configured, but the selected renderer does not expose the required external shader interface");
		return;
	}

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
		ApplyShaderEntries(renderer, preScale, postScale, defaultProfile, selection);
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
		if (!rule.valid || !RuleMatches(rule, videoState.eotf, nominalRate))
			continue;

		selection.ruleName = rule.name;
		selection.ruleLabel = rule.label;
		DebugLog::Log("Shaders: selected rule \"%s\" (%s) for signal=%s refresh=%.6f Hz nominal=%d",
			rule.name.c_str(), rule.label.c_str(), SignalName(videoState.eotf),
			refreshRate, nominalRate);

		if (rule.none)
		{
			if (!rule.preScale.empty() || !rule.postScale.empty())
				DebugLog::Log("Shaders: rule \"%s\" has none=true; shader entries are ignored",
					rule.name.c_str());
			DebugLog::Log("Shaders: selected rule explicitly requests no shaders");
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
			defaultProfile, selection);
		return selection;
	}

	DebugLog::Log("Shaders: no rule matched signal=%s refresh=%.6f Hz nominal=%d; default=none",
		SignalName(videoState.eotf), refreshRate, nominalRate);
	return selection;
}
