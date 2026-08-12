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
#include <DisplayRuleExpression.h>
#include <RendererProfileConfig.h>
#include <AspectRatio.h>
#include <ActivePictureTransitionModel.h>
#include <microsoft_directshow/MadVRExternalPixelShaders.h>

#include <d3dcompiler.h>
#include <d3d9.h>
#include <dxgi.h>

#include "MadVRShaderLoader.h"
#include <ShaderConfigValidation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dxgi.lib")


namespace
{
constexpr const char* CONFIG_SECTION = "shaders";
constexpr size_t MAX_SHADER_BYTES = 4 * 1024 * 1024;


uint64_t ShaderSourceFingerprint(const std::string& source)
{
	// A short, stable identifier lets a field log establish exactly which
	// fully-expanded shader reached madVR without exposing the whole source.
	uint64_t hash = 1469598103934665603ull;
	for (const unsigned char byte : source)
	{
		hash ^= byte;
		hash *= 1099511628211ull;
	}
	return hash;
}


void AppendShaderFingerprint(uint64_t& hash, const std::string& value)
{
	for (const unsigned char byte : value)
	{
		hash ^= byte;
		hash *= 1099511628211ull;
	}
	// Keep adjacent chain members distinct even when their concatenated source
	// text would otherwise be identical.
	hash ^= 0xFF;
	hash *= 1099511628211ull;
}


std::string CompactShaderDiagnostics(ID3DBlob* diagnostics)
{
	if (!diagnostics || !diagnostics->GetBufferPointer() ||
		diagnostics->GetBufferSize() == 0)
	{
		return {};
	}

	std::string text(static_cast<const char*>(diagnostics->GetBufferPointer()),
		diagnostics->GetBufferSize());
	for (char& character : text)
	{
		if (character == '\r' || character == '\n' || character == '\t')
			character = ' ';
	}
	while (!text.empty() && text.back() == '\0')
		text.pop_back();
	return text;
}


std::string Utf8FromWide(const wchar_t* text)
{
	if (!text || !*text)
		return {};

	const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
		nullptr, nullptr);
	if (bytes <= 1)
		return {};

	std::string utf8(static_cast<size_t>(bytes), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), bytes, nullptr,
		nullptr);
	utf8.pop_back();
	return utf8;
}


std::string DriverVersionText(const LARGE_INTEGER& version)
{
	std::ostringstream stream;
	stream << HIWORD(version.HighPart) << '.' << LOWORD(version.HighPart)
		<< '.' << HIWORD(version.LowPart) << '.' << LOWORD(version.LowPart);
	return stream.str();
}


void LogMadVRShaderEnvironment()
{
	SYSTEM_INFO systemInfo{};
	GetNativeSystemInfo(&systemInfo);
	DebugLog::Log(
		"Shaders: environment CPU architecture=%u type=%u level=%u revision=%u logical_processors=%u page_size=%u",
		static_cast<unsigned int>(systemInfo.wProcessorArchitecture),
		static_cast<unsigned int>(systemInfo.dwProcessorType),
		static_cast<unsigned int>(systemInfo.wProcessorLevel),
		static_cast<unsigned int>(systemInfo.wProcessorRevision),
		static_cast<unsigned int>(systemInfo.dwNumberOfProcessors),
		static_cast<unsigned int>(systemInfo.dwPageSize));

	CComPtr<IDXGIFactory> factory;
	const HRESULT factoryHr = CreateDXGIFactory(__uuidof(IDXGIFactory),
		reinterpret_cast<void**>(&factory));
	if (FAILED(factoryHr))
	{
		DebugLog::Log("Shaders: environment DXGI factory creation failed HRESULT=0x%08lx",
			static_cast<unsigned long>(factoryHr));
	}
	else
	{
		for (UINT index = 0;; ++index)
		{
			CComPtr<IDXGIAdapter> adapter;
			const HRESULT enumHr = factory->EnumAdapters(index, &adapter);
			if (enumHr == DXGI_ERROR_NOT_FOUND)
				break;
			if (FAILED(enumHr))
			{
				DebugLog::Log("Shaders: environment DXGI adapter enumeration stopped at index=%u HRESULT=0x%08lx",
					index, static_cast<unsigned long>(enumHr));
				break;
			}

			DXGI_ADAPTER_DESC description{};
			const HRESULT descHr = adapter->GetDesc(&description);
			LARGE_INTEGER driverVersion{};
			const HRESULT driverHr = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),
				&driverVersion);
			DebugLog::Log(
				"Shaders: environment DXGI adapter=%u name=\"%s\" vendor=0x%04X device=0x%04X subsystem=0x%08X revision=%u dedicated_vram=%llu shared_memory=%llu desc_hr=0x%08lx driver=%s driver_hr=0x%08lx",
				index, descHr == S_OK ? Utf8FromWide(description.Description).c_str() : "(unavailable)",
				description.VendorId, description.DeviceId, description.SubSysId,
				description.Revision,
				static_cast<unsigned long long>(description.DedicatedVideoMemory),
				static_cast<unsigned long long>(description.SharedSystemMemory),
				static_cast<unsigned long>(descHr),
				SUCCEEDED(driverHr) ? DriverVersionText(driverVersion).c_str() : "(unavailable)",
				static_cast<unsigned long>(driverHr));
		}
	}

	CComPtr<IDirect3D9> d3d9;
	d3d9.Attach(Direct3DCreate9(D3D_SDK_VERSION));
	if (!d3d9)
	{
		DebugLog::Log("Shaders: environment default D3D9 adapter creation failed");
		return;
	}

	D3DCAPS9 caps{};
	const HRESULT capsHr = d3d9->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
		&caps);
	if (FAILED(capsHr))
	{
		DebugLog::Log("Shaders: environment default D3D9 HAL capabilities unavailable HRESULT=0x%08lx",
			static_cast<unsigned long>(capsHr));
		return;
	}

	DebugLog::Log(
		"Shaders: environment default D3D9 HAL caps vertex_shader=%u.%u pixel_shader=%u.%u max_texture=%ux%u simultaneous_textures=%u texture_caps=0x%08lx primitive_misc_caps=0x%08lx",
		D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion),
		D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion),
		D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion),
		D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion), caps.MaxTextureWidth,
		caps.MaxTextureHeight, caps.MaxSimultaneousTextures,
		static_cast<unsigned long>(caps.TextureCaps),
		static_cast<unsigned long>(caps.PrimitiveMiscCaps));
}


bool LogMadVRShaderPreflight(const std::string& source, const std::string& profile,
	const std::filesystem::path& path, const char* stageName, unsigned int order)
{
	static std::once_flag environmentLogged;
	std::call_once(environmentLogged, LogMadVRShaderEnvironment);

	const uint64_t fingerprint = ShaderSourceFingerprint(source);
	CComPtr<ID3DBlob> bytecode;
	CComPtr<ID3DBlob> diagnostics;
	const HRESULT hr = D3DCompile(source.data(), source.size(),
		path.u8string().c_str(), nullptr, nullptr, "main", profile.c_str(),
		0, 0, &bytecode, &diagnostics);
	const std::string message = CompactShaderDiagnostics(diagnostics);
	if (FAILED(hr))
	{
		DebugLog::Log(
			"Shaders: local HLSL preflight failed for %s shader #%u \"%s\" profile=%s bytes=%zu fingerprint=%016llX HRESULT=0x%08lx diagnostic=\"%s\"",
			stageName, order, path.u8string().c_str(), profile.c_str(), source.size(),
			static_cast<unsigned long long>(fingerprint), static_cast<unsigned long>(hr),
			message.empty() ? "(none)" : message.c_str());
		return false;
	}

	DebugLog::Log(
		"Shaders: local HLSL preflight succeeded for %s shader #%u \"%s\" profile=%s bytes=%zu fingerprint=%016llX bytecode=%zu diagnostic=\"%s\"",
		stageName, order, path.u8string().c_str(), profile.c_str(), source.size(),
		static_cast<unsigned long long>(fingerprint),
		bytecode ? bytecode->GetBufferSize() : 0,
		message.empty() ? "(none)" : message.c_str());
	return true;
}

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


struct PreparedShaderEntry
{
	unsigned int order = 0;
	std::filesystem::path path;
	std::string displayName;
	std::string source;
	std::string profile;
};


struct PreparedShaderStage
{
	uint64_t fingerprint = 1469598103934665603ull;
	std::vector<PreparedShaderEntry> entries;
	std::vector<ActiveMadVRShader> activeShaders;
};


struct InstalledShaderStage
{
	bool known = false;
	PreparedShaderStage prepared;
};


struct InstalledShaderChain
{
	uint64_t rendererGeneration = 0;
	InstalledShaderStage preScale;
	InstalledShaderStage postScale;
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
	double maximumStretchRatio = NLS_DEFAULT_MAXIMUM_STRETCH_RATIO;
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
std::mutex g_installedShaderChainMutex;
InstalledShaderChain g_installedShaderChain;

// VP-0079 shader selections deliberately live only for this process. The
// configuration names groups; a key selects one child (single) or all matching
// children (multi), while the root selection clears that group back to empty.
std::mutex g_targetShaderSelectionMutex;
std::map<std::string, std::vector<std::string>> g_targetShaderSelections;

struct TargetShaderGroup
{
	std::string name;
	bool multi = false;
	std::string resetWhen;
	std::vector<std::pair<std::string, std::string>> members; // name, when
	bool rootIsEffect = false;
};

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


bool IsTargetShaderConfiguration(const ConfigFile& config)
{
	for (const std::string& section : config.GetSectionNames())
		if (section.rfind("shader.", 0) == 0)
			return true;
	return false;
}


bool ParseTargetShaderGroups(const ConfigFile& config,
	std::vector<TargetShaderGroup>& groups, std::string& reason)
{
	groups.clear();
	reason.clear();
	for (const std::string& section : config.GetSectionNames())
	{
		if (section.rfind("shader.", 0) != 0)
			continue;
		const std::string tail = section.substr(7);
		if (tail.empty() || tail.find('.') != std::string::npos)
			continue;
		const auto* root = config.GetSectionValues(section);
		TargetShaderGroup group;
		group.name = tail;
		std::string type;
		if (config.TryGetString(section, "type", type))
		{
			const std::string normalized = ConfigFile::NormalizeName(type);
			if (normalized == "multi") group.multi = true;
			else if (normalized != "single")
			{
				reason = "[" + section + "] type must be single or multi";
				return false;
			}
		}
		config.TryGetString(section, "when", group.resetWhen);
		std::string resetShortcut;
		config.TryGetString(section, "shortcut", resetShortcut);
		if (!RendererProfileConfig::MergeShortcutIntoWhen(
			resetShortcut, "[" + section + "]", group.resetWhen, reason))
			return false;
		group.rootIsEffect = root &&
			(root->find("shader_type") != root->end() ||
			 root->find("hlsl_file") != root->end() ||
			 root->find("glsl_file") != root->end());
		for (const std::string& child : config.GetSectionNames())
		{
			const std::string prefix = section + ".";
			if (child.rfind(prefix, 0) != 0)
				continue;
			const std::string name = child.substr(prefix.size());
			if (name.empty() || name.find('.') != std::string::npos)
			{
				reason = "[" + child + "] must be exactly one shader member deep";
				return false;
			}
			std::string when;
			std::string shortcut;
			config.TryGetString(child, "when", when);
			config.TryGetString(child, "shortcut", shortcut);
			if (!RendererProfileConfig::MergeShortcutIntoWhen(
				shortcut, "[" + child + "]", when, reason))
				return false;
			group.members.emplace_back(name, when);
		}
		if (group.rootIsEffect && !group.members.empty())
		{
			reason = "[" + section + "] cannot define an effect and child members";
			return false;
		}
		if (group.multi && group.rootIsEffect)
		{
			reason = "[" + section + "] type=multi requires child members";
			return false;
		}
		groups.push_back(std::move(group));
	}
	return true;
}


bool MatchesTargetShaderWhen(const std::string& rawWhen,
	const std::string& key, const DisplayRuleExpression::ValueLookup& source,
	bool& matches, std::string& reason)
{
	matches = false;
	DisplayRuleExpression::Expression expression;
	if (!expression.Compile(rawWhen, reason, true))
		return false;
	// Preserve the configured literal for the expression comparison, while
	// comparing the physical chord canonically. This makes L and l equivalent
	// and keeps Shift+L as the distinct shifted chord, including older manual
	// ${key} expressions whose literal spelling has not been rewritten yet.
	std::string expressionKey = key;
	std::string canonicalKey;
	if (RendererProfileConfig::CanonicalizeKeyChord(key, canonicalKey))
		for (const std::string& chord : expression.KeyChords())
		{
			std::string canonicalChord;
			if (RendererProfileConfig::CanonicalizeKeyChord(chord,
				canonicalChord) && canonicalChord == canonicalKey)
			{
				expressionKey = chord;
				break;
			}
		}
	int specificity = 0;
	matches = expression.Matches([&](const std::string& variable,
		std::string& value)
		{
			if (variable == "key") { value = expressionKey; return true; }
			return source && source(variable, value);
		}, specificity, reason);
	return reason.empty();
}


bool ResolveTargetShaderKey(const ConfigFile& config, const std::string& key,
	const DisplayRuleExpression::ValueLookup& source,
	std::vector<std::string>& names, std::string& reason,
	std::vector<std::string>* activeSections = nullptr)
{
	names.clear();
	if (activeSections) activeSections->clear();
	std::vector<TargetShaderGroup> groups;
	if (!ParseTargetShaderGroups(config, groups, reason))
		return false;

	if (!key.empty())
	{
		bool consumed = false;
		std::lock_guard<std::mutex> guard(g_targetShaderSelectionMutex);
		for (const TargetShaderGroup& group : groups)
		{
			bool matches = false;
			if (!group.resetWhen.empty() &&
				!MatchesTargetShaderWhen(group.resetWhen, key, source,
					matches, reason))
				return false;
			if (matches)
			{
				g_targetShaderSelections.erase(group.name);
				consumed = true;
				continue;
			}
			std::vector<std::string> selected;
			for (const auto& member : group.members)
			{
				if (member.second.empty()) continue;
				if (!MatchesTargetShaderWhen(member.second, key, source,
					matches, reason)) return false;
				if (matches) selected.push_back(member.first);
			}
			if (selected.empty()) continue;
			if (!group.multi && selected.size() != 1)
			{
				reason = "key '" + key + "' selects multiple members of [shader." +
					group.name + "]";
				return false;
			}
			g_targetShaderSelections[group.name] = selected;
			consumed = true;
		}
		if (!consumed)
		{
			reason = "key '" + key + "' does not select a shader group";
			return false;
		}
	}

	std::map<std::string, std::vector<std::string>> manual;
	{
		std::lock_guard<std::mutex> guard(g_targetShaderSelectionMutex);
		manual = g_targetShaderSelections;
	}
	for (const TargetShaderGroup& group : groups)
	{
		const auto selected = manual.find(group.name);
		if (selected != manual.end())
		{
			for (const std::string& member : selected->second)
			{
				names.push_back(group.name + "." + member);
				if (activeSections)
					activeSections->push_back(
						"shader." + group.name + "." + member);
			}
			continue;
		}
		if (group.rootIsEffect)
		{
			bool matches = group.resetWhen.empty();
			if (!group.resetWhen.empty() && !MatchesTargetShaderWhen(
				group.resetWhen, std::string(), source, matches, reason))
				return false;
			if (matches)
			{
				names.push_back(group.name);
				if (activeSections)
					activeSections->push_back("shader." + group.name);
			}
			continue;
		}
		std::vector<std::string> automatic;
		for (const auto& member : group.members)
		{
			if (member.second.empty()) continue;
			bool matches = false;
			if (!MatchesTargetShaderWhen(member.second, std::string(), source,
				matches, reason)) return false;
			if (matches) automatic.push_back(member.first);
		}
		if (!group.multi && automatic.size() > 1)
		{
			reason = "automatic state selects multiple members of [shader." +
				group.name + "]";
			return false;
		}
		for (const std::string& member : automatic)
		{
			names.push_back(group.name + "." + member);
			if (activeSections)
				activeSections->push_back(
					"shader." + group.name + "." + member);
		}
		if (automatic.empty() && !group.multi && activeSections)
			activeSections->push_back("shader." + group.name);
	}
	return true;
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

	const auto maximumRatio = settings->find("max_stretch_ratio");
	if (maximumRatio != settings->end() &&
		!ParseBoundedDouble(maximumRatio->second,
			NLS_MINIMUM_STRETCH_RATIO,
			NLS_SHADER_MAXIMUM_STRETCH_RATIO,
			rule.maximumStretchRatio))
	{
		DebugLog::Log(
			"Shaders: rule \"%s\" has invalid max_stretch_ratio \"%s\"; use %.1f through %.1f",
			rule.name.c_str(), maximumRatio->second.c_str(),
			NLS_MINIMUM_STRETCH_RATIO,
			NLS_SHADER_MAXIMUM_STRETCH_RATIO);
		rule.valid = false;
	}

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
		{
			rule.nls = true;
			// Typed NLS is expansion-only unless a rule explicitly opts into
			// vertical warping. A vertical warp makes a wider picture fit a
			// narrower target and can visibly shrink the presentation.
			rule.narrowerOnly = true;
		}
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
		else if (direction == "any")
			rule.narrowerOnly = false;
		else
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
				  "active_aspect_min", "inactive_rule" })
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


ShaderRule LoadTargetRule(const ConfigFile& config, const std::string& name,
	ShaderRendererBackend backend)
{
	ShaderRule rule;
	rule.name = ConfigFile::NormalizeName(name);
	rule.label = name;
	const std::string section = "shader." + rule.name;
	const auto* settings = config.GetSectionValues(section);
	if (!settings)
	{
		rule.valid = false;
		return rule;
	}
	std::string value;
	if (config.TryGetString(section, "label", value) &&
		!ConfigFile::Trim(value).empty())
		rule.label = ConfigFile::Trim(value);
	if (!config.TryGetString(section, "shader_type", value))
	{
		DebugLog::Log("Shaders: [%s] requires shader_type", section.c_str());
		rule.valid = false;
		return rule;
	}
	rule.explicitType = true;
	const std::string type = ConfigFile::NormalizeName(value);
	if (type == "nls")
	{
		rule.nls = true;
		// NLS expands narrower content by default. Reverse-direction vertical
		// warping is opt-in because fitting a wider picture to a narrower target
		// visibly reduces its presentation size.
		rule.narrowerOnly = true;
	}
	else if (type != "custom")
	{
		DebugLog::Log("Shaders: [%s] shader_type must be nls or custom", section.c_str());
		rule.valid = false;
		return rule;
	}
	if (rule.nls)
	{
		if (config.TryGetString(section, "tolerance_percent", value) &&
			!ParseBoundedDouble(value, 0.0, 50.0,
				rule.aspectTolerancePercent))
		{
			DebugLog::Log("Shaders: [%s] tolerance_percent is invalid", section.c_str());
			rule.valid = false;
		}
		if (rule.aspectTolerancePercent < 0.0)
			rule.aspectTolerancePercent = 5.0;
		if (config.TryGetString(section, "aspect_direction", value))
		{
			const std::string direction = ConfigFile::NormalizeName(value);
			if (direction == "narrower_only")
				rule.narrowerOnly = true;
			else if (direction == "any")
				rule.narrowerOnly = false;
			else
			{
				DebugLog::Log(
					"Shaders: [%s] aspect_direction must be narrower_only or any",
					section.c_str());
				rule.valid = false;
			}
		}
		LoadTypedNlsSettings(config, section, rule);
	}
	std::string stage = "pre_resize";
	config.TryGetString(section, "stage", stage);
	stage = ConfigFile::NormalizeName(stage);
	int order = 0;
	if (config.TryGetString(section, "order", value))
	{
		try
		{
			size_t consumed = 0;
			const long parsed = std::stol(ConfigFile::Trim(value), &consumed);
			if (consumed != ConfigFile::Trim(value).size() || parsed < 0 ||
				parsed > INT_MAX) throw std::out_of_range("order");
			order = static_cast<int>(parsed);
		}
		catch (const std::exception&)
		{
			DebugLog::Log("Shaders: [%s] order must be a non-negative integer", section.c_str());
			rule.valid = false;
		}
	}
	const char* fileKey = backend == ShaderRendererBackend::MADVR ?
		"hlsl_file" : "glsl_file";
	std::string filename;
	if (!config.TryGetString(section, fileKey, filename) ||
		ConfigFile::Trim(filename).empty())
	{
		// An omitted backend file is deliberately valid: the logical shader is
		// simply ignored by that renderer.
		rule.sourceBackend = backend == ShaderRendererBackend::MADVR ?
			ShaderSourceBackend::LIBPLACEBO : ShaderSourceBackend::MADVR;
		return rule;
	}
	rule.filename = ConfigFile::Trim(filename);
	rule.sourceBackend = backend == ShaderRendererBackend::MADVR ?
		ShaderSourceBackend::MADVR : ShaderSourceBackend::LIBPLACEBO;
	const std::filesystem::path path = ResolveShaderPath(rule.filename);
	if (path.empty())
	{
		rule.valid = false;
		return rule;
	}
	if (stage == "pre_resize" || stage == "pre")
		rule.preScale.push_back({ static_cast<unsigned int>(order), path });
	else if (stage == "post_resize" || stage == "post")
		rule.postScale.push_back({ static_cast<unsigned int>(order), path });
	else
	{
		DebugLog::Log("Shaders: [%s] stage must be pre_resize or post_resize", section.c_str());
		rule.valid = false;
	}
	return rule;
}


DisplayRuleExpression::ValueLookup TargetVideoLookup(const VideoState& videoState)
{
	const double refreshRate = videoState.displayMode ?
		videoState.displayMode->RefreshRateHz() : 0.0;
	return [&videoState, refreshRate](const std::string& variable,
		std::string& value)
	{
		if (variable == "transfer" || variable == "eotf")
		{
			value = SignalName(videoState.eotf);
			return true;
		}
		if (variable == "source_rate")
		{
			std::ostringstream text;
			text.imbue(std::locale::classic());
			text << static_cast<int>(std::floor(refreshRate + 0.0001));
			value = text.str();
			return true;
		}
		return false;
	};
}


bool LoadTargetRuleSelectionForBackend(const ConfigFile& config,
	const std::string& key, const DisplayRuleExpression::ValueLookup& source,
	ShaderRendererBackend backend, std::vector<ShaderRule>& rules,
	std::string& reason, std::vector<std::string>* activeSections = nullptr)
{
	rules.clear();
	std::vector<std::string> names;
	if (!ResolveTargetShaderKey(config, key, source, names, reason,
		activeSections))
		return false;
	std::set<std::string> seen;
	for (const std::string& name : names)
	{
		if (!seen.insert(name).second) continue;
		ShaderRule rule = LoadTargetRule(config, name, backend);
		if (!rule.valid)
		{
			reason = "shader." + name + " is invalid";
			return false;
		}
		if (RuleAppliesToBackend(rule, backend))
			rules.push_back(std::move(rule));
	}
	// An empty target root is a deliberate baseline, not an invalid shader
	// request.  Represent it as the legacy explicit NONE rule so every caller
	// (including the DirectShow aspect guard) has one safe, renderer-neutral
	// selection to inspect.
	if (rules.empty())
	{
		ShaderRule off;
		off.name = "off";
		off.label = "Off";
		off.none = true;
		rules.push_back(std::move(off));
	}
	return true;
}


bool LoadRuleSelectionForBackend(const ConfigFile& config,
	const std::string& selector, ShaderRendererBackend backend,
	std::vector<ShaderRule>& rules)
{
	if (IsTargetShaderConfiguration(config))
	{
		constexpr const char* TARGET_KEY = "@shader-key:";
		const std::string trimmed = ConfigFile::Trim(selector);
		if (trimmed.rfind(TARGET_KEY, 0) != 0)
			return false;
		std::string reason;
		return LoadTargetRuleSelectionForBackend(config,
			trimmed.substr(std::char_traits<char>::length(TARGET_KEY)),
			DisplayRuleExpression::ValueLookup(), backend, rules, reason);
	}
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
	configured.maximumStretchRatio = rule.maximumStretchRatio;
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


double ElapsedMilliseconds(const std::chrono::steady_clock::time_point& start,
	const std::chrono::steady_clock::time_point& finish)
{
	return std::chrono::duration<double, std::milli>(finish - start).count();
}


bool PrepareStage(const std::vector<ShaderEntry>& entries,
	const char* stageName, const std::string& defaultProfile,
	bool postResize, PreparedShaderStage& prepared)
{
	prepared = {};
	for (const ShaderEntry& entry : entries)
	{
		const auto started = std::chrono::steady_clock::now();
		PreparedShaderEntry shader;
		shader.order = entry.order;
		shader.path = entry.path;
		shader.displayName = entry.displayName;
		if (!ReadShader(entry.path, shader.source))
		{
			DebugLog::Log(
				"Shaders: preserving current %s stage because shader #%u could not be loaded",
				stageName, entry.order);
			return false;
		}
		const auto readFinished = std::chrono::steady_clock::now();
		if (!ApplyShaderParameters(shader.source, entry.parameters, entry.path))
			return false;
		shader.profile = ShaderProfile(shader.source, defaultProfile);
		if (shader.profile.empty())
		{
			DebugLog::Log(
				"Shaders: preserving current %s stage because shader #%u has an unsupported D3D9 profile in \"%s\"",
				stageName, entry.order, entry.path.u8string().c_str());
			return false;
		}
		const auto expanded = std::chrono::steady_clock::now();
		if (!LogMadVRShaderPreflight(shader.source, shader.profile,
			entry.path, stageName, entry.order))
		{
			DebugLog::Log(
				"Shaders: preserving current %s stage after local preflight failure",
				stageName);
			return false;
		}
		const auto preflightFinished = std::chrono::steady_clock::now();

		AppendShaderFingerprint(prepared.fingerprint,
			std::to_string(entry.order));
		AppendShaderFingerprint(prepared.fingerprint, shader.profile);
		AppendShaderFingerprint(prepared.fingerprint, shader.source);
		std::string displayName = entry.path.stem().u8string();
		if (!entry.displayName.empty())
			displayName = entry.displayName + " (" + displayName + ")";
		prepared.activeShaders.push_back({ displayName, postResize });
		prepared.entries.push_back(std::move(shader));
		DebugLog::Log(
			"Shaders: %s shader #%u preparation timing read=%.3fms expand=%.3fms preflight=%.3fms total=%.3fms",
			stageName, entry.order,
			ElapsedMilliseconds(started, readFinished),
			ElapsedMilliseconds(readFinished, expanded),
			ElapsedMilliseconds(expanded, preflightFinished),
			ElapsedMilliseconds(started, preflightFinished));
	}
	return true;
}


bool RestorePreparedStage(IMadVRExternalPixelShaders* shaderInterface,
	const InstalledShaderStage& previous, int stage)
{
	if (FAILED(shaderInterface->ClearPixelShaders(stage)))
		return false;
	if (!previous.known)
		return true;
	for (const PreparedShaderEntry& shader : previous.prepared.entries)
	{
		if (FAILED(shaderInterface->AddPixelShader(shader.source.c_str(),
			shader.profile.c_str(), stage, nullptr)))
			return false;
	}
	return true;
}


bool InstallPreparedStage(IMadVRExternalPixelShaders* shaderInterface,
	const PreparedShaderStage& desired, const InstalledShaderStage& previous,
	int stage, const char* stageName, double& clearMilliseconds,
	double& installMilliseconds)
{
	const auto clearStarted = std::chrono::steady_clock::now();
	const HRESULT clearHr = shaderInterface->ClearPixelShaders(stage);
	const auto clearFinished = std::chrono::steady_clock::now();
	clearMilliseconds += ElapsedMilliseconds(clearStarted, clearFinished);
	if (FAILED(clearHr))
	{
		DebugLog::Log("Shaders: failed to clear changed %s stage (HRESULT=0x%08lx)",
			stageName, static_cast<unsigned long>(clearHr));
		return false;
	}

	for (const PreparedShaderEntry& shader : desired.entries)
	{
		DebugLog::Log("Shaders: installing prepared %s shader #%u \"%s\" (profile=%s)",
			stageName, shader.order, shader.path.u8string().c_str(),
			shader.profile.c_str());
		const auto installStarted = std::chrono::steady_clock::now();
		const HRESULT hr = shaderInterface->AddPixelShader(shader.source.c_str(),
			shader.profile.c_str(), stage, nullptr);
		const auto installFinished = std::chrono::steady_clock::now();
		const double duration = ElapsedMilliseconds(
			installStarted, installFinished);
		installMilliseconds += duration;
		DebugLog::Log(
			"Shaders: prepared %s shader #%u madVR install timing=%.3fms HRESULT=0x%08lx",
			stageName, shader.order, duration, static_cast<unsigned long>(hr));
		if (FAILED(hr))
		{
			const bool restored = RestorePreparedStage(
				shaderInterface, previous, stage);
			DebugLog::Log(
				"Shaders: %s replacement failed; previous coherent stage restored=%d",
				stageName, restored ? 1 : 0);
			return false;
		}
	}
	DebugLog::Log("Shaders: %s stage ACTIVE with %u shader(s)",
		stageName, static_cast<unsigned int>(desired.entries.size()));
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
			if (entry.order == 0)
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

	const double activeAspect = runtime.activeGeometry.aspectRatio;
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
			NlsMappingModeName(runtime.lastSafeNlsMode));
		return true;
	}
	// The target output contract is exposed only after the exact, source-owned
	// crop is current for this renderer generation. Merely arming NLS must not
	// cause madVR to fit the raster as though a mapping already exists.
	outputAspectX = 0;
	outputAspectY = 0;
	MadVRNlsMappingDecision decision = runtime.nlsDecision;
	decision.mode = runtime.nlsMode;
	decision.sourceAspect = activeAspect;
	decision.targetAspect = targetAspect;
	decision.requestedRatio = std::max(targetAspect / activeAspect,
		activeAspect / targetAspect);
	decision.maximumRatio = rule.maximumStretchRatio;
	decision.verticalWarp = activeAspect > targetAspect;
	const MadVRNlsPresentationPlan plan =
		ResolveMadVRNlsPresentationPlan(decision, runtime.activeGeometry);
	if (!plan.customShader)
	{
		// madVR already detects hard-coded bars and owns the final native
		// crop/fit. Installing VP's active-rectangle shader in passthrough or
		// safe-fit mode would crop the same bars a second time.
		rule.preScale.clear();
		rule.postScale.clear();
		DebugLog::Log(
			"Shaders: NLS mapping=%s delegated to madVR native crop/fit active_generation=%llu source=%.4f target=%.4f renderer_generation=%llu",
			NlsMappingModeName(runtime.nlsMode),
			static_cast<unsigned long long>(
				runtime.activeGeometry.generation),
			activeAspect, targetAspect,
			static_cast<unsigned long long>(runtime.rendererGeneration));
		return true;
	}
	outputAspectX = plan.aspectX;
	outputAspectY = plan.aspectY;
	if (activeAspect <= 0.0 || !videoState.displayMode)
		return true;

	const MadVRActivePictureGeometry& activeGeometry = plan.shaderGeometry;
	const bool verticalWarp =
		runtime.nlsMode == MadVRNlsMappingMode::ACTIVE &&
		activeAspect > targetAspect;
	const double stretchRatio =
		runtime.nlsMode == MadVRNlsMappingMode::LINEAR_PASSTHROUGH ?
			1.0 : decision.stretchRatio;
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
		coordinateText(activeGeometry.bottom - activeGeometry.top);
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
		"Shaders: NLS backend=madvr rule=%s mapping=%s measured_rect=%.5f,%.5f-%.5f,%.5f sample_rect=%.5f,%.5f-%.5f,%.5f active_generation=%llu source=%.4f target=%.4f requested_ratio=%.5f max_ratio=%.5f raster_aspect=%.5f axis=%s stretch=%.5f renderer_generation=%llu reason=\"%s\"",
		rule.name.c_str(),
		NlsMappingModeName(runtime.nlsMode),
		runtime.activeGeometry.left, runtime.activeGeometry.top,
		runtime.activeGeometry.right, runtime.activeGeometry.bottom,
		activeGeometry.left, activeGeometry.top,
		activeGeometry.right, activeGeometry.bottom,
		static_cast<unsigned long long>(runtime.activeGeometry.generation),
		activeAspect, targetAspect, decision.requestedRatio,
		decision.maximumRatio, plan.rasterAspect,
		NlsMappingAxisName(decision), stretchRatio,
		static_cast<unsigned long long>(runtime.rendererGeneration),
		decision.reason.c_str());
	return true;
}


void ApplyShaderEntries(IBaseFilter* renderer, const std::vector<ShaderEntry>& preScale,
	const std::vector<ShaderEntry>& postScale, const std::string& defaultProfile,
	MadVRShaderSelection& selection)
{
	const auto transitionStarted = std::chrono::steady_clock::now();
	CComQIPtr<IMadVRExternalPixelShaders> shaderInterface(renderer);
	if (!shaderInterface)
	{
		DebugLog::Log("Shaders: configured, but the selected renderer does not expose the required external shader interface");
		return;
	}

	// Expand and locally compile the complete desired chain while the previous
	// coherent chain is still presenting. madVR exposes only clear/add, so the
	// unavoidable visible mutation window begins after all fallible preparation.
	const uint64_t rendererGeneration =
		g_runtimeState.GetSnapshot().rendererGeneration;
	PreparedShaderStage desiredPre;
	PreparedShaderStage desiredPost;
	if (!PrepareStage(preScale, "pre-resize", defaultProfile, false,
		desiredPre) ||
		!PrepareStage(postScale, "post-resize", defaultProfile, true,
			desiredPost))
	{
		std::lock_guard<std::mutex> lock(g_installedShaderChainMutex);
		if (g_installedShaderChain.rendererGeneration == rendererGeneration)
		{
			selection.activeShaders =
				g_installedShaderChain.preScale.prepared.activeShaders;
			selection.activeShaders.insert(selection.activeShaders.end(),
				g_installedShaderChain.postScale.prepared.activeShaders.begin(),
				g_installedShaderChain.postScale.prepared.activeShaders.end());
		}
		DebugLog::Log(
			"Shaders: chain transition aborted during preparation; previous coherent chain retained");
		return;
	}
	const auto preparationFinished = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> lock(g_installedShaderChainMutex);
	if (g_installedShaderChain.rendererGeneration != rendererGeneration)
	{
		g_installedShaderChain = {};
		g_installedShaderChain.rendererGeneration = rendererGeneration;
	}

	const InstalledShaderChain previous = g_installedShaderChain;
	const MadVRShaderChainUpdatePlan plan =
		ResolveMadVRShaderChainUpdatePlan(
			previous.preScale.known,
			previous.preScale.prepared.fingerprint,
			desiredPre.fingerprint, desiredPre.entries.empty(),
			previous.postScale.known,
			previous.postScale.prepared.fingerprint,
			desiredPost.fingerprint, desiredPost.entries.empty());
	double clearMilliseconds = 0.0;
	double installMilliseconds = 0.0;
	bool succeeded = true;
	if (plan.preScale)
		succeeded = InstallPreparedStage(shaderInterface, desiredPre,
			previous.preScale, MADVR_SHADER_STAGE_PRE_SCALE, "pre-resize",
			clearMilliseconds, installMilliseconds);
	if (succeeded && plan.postScale)
		succeeded = InstallPreparedStage(shaderInterface, desiredPost,
			previous.postScale, MADVR_SHADER_STAGE_POST_SCALE, "post-resize",
			clearMilliseconds, installMilliseconds);
	if (!succeeded)
	{
		// InstallPreparedStage restored its own stage. If the other stage was
		// already replaced, restore that too so a multi-stage request is atomic
		// from VP's perspective.
		if (plan.preScale && plan.postScale)
		{
			RestorePreparedStage(shaderInterface, previous.preScale,
				MADVR_SHADER_STAGE_PRE_SCALE);
			RestorePreparedStage(shaderInterface, previous.postScale,
				MADVR_SHADER_STAGE_POST_SCALE);
		}
		selection.activeShaders = previous.preScale.prepared.activeShaders;
		selection.activeShaders.insert(selection.activeShaders.end(),
			previous.postScale.prepared.activeShaders.begin(),
			previous.postScale.prepared.activeShaders.end());
	}
	else
	{
		g_installedShaderChain.preScale.known = true;
		g_installedShaderChain.preScale.prepared = desiredPre;
		g_installedShaderChain.postScale.known = true;
		g_installedShaderChain.postScale.prepared = desiredPost;
		selection.activeShaders = desiredPre.activeShaders;
		selection.activeShaders.insert(selection.activeShaders.end(),
			desiredPost.activeShaders.begin(), desiredPost.activeShaders.end());
	}
	const auto transitionFinished = std::chrono::steady_clock::now();
	DebugLog::Log(
		"Shaders: chain transition pre=%s post=%s coalesced=%d prepare=%.3fms clear=%.3fms install=%.3fms total=%.3fms result=%s",
		plan.preScale ? "changed" : "unchanged",
		plan.postScale ? "changed" : "unchanged", plan.Any() ? 0 : 1,
		ElapsedMilliseconds(transitionStarted, preparationFinished),
		clearMilliseconds, installMilliseconds,
		ElapsedMilliseconds(transitionStarted, transitionFinished),
		succeeded ? "active" : "previous-retained");
	const bool preActive = !selection.activeShaders.empty() &&
		std::any_of(selection.activeShaders.begin(), selection.activeShaders.end(),
			[](const ActiveMadVRShader& shader) { return !shader.postResize; });
	const bool postActive = !selection.activeShaders.empty() &&
		std::any_of(selection.activeShaders.begin(), selection.activeShaders.end(),
			[](const ActiveMadVRShader& shader) { return shader.postResize; });
	DebugLog::Log("Shaders: configuration complete (pre-resize=%s, post-resize=%s)",
		preActive ? "active" : "inactive", postActive ? "active" : "inactive");
}
}


static bool ValidateConfiguredShadersWithRuntimeParser(const ConfigFile& config,
	std::string& reason)
{
	reason.clear();
	if (!IsTargetShaderConfiguration(config))
		return true;

	std::vector<TargetShaderGroup> groups;
	if (!ParseTargetShaderGroups(config, groups, reason))
		return false;

	for (const std::string& section : config.GetSectionNames())
	{
		if (section.rfind("shader.", 0) != 0)
			continue;
		const auto* settings = config.GetSectionValues(section);
		if (!settings)
			continue;
		const std::string tail = section.substr(7);
		const bool member = tail.find('.') != std::string::npos;
		const bool rootEffect = settings->find("shader_type") != settings->end() ||
			settings->find("hlsl_file") != settings->end() ||
			settings->find("glsl_file") != settings->end();
		if (!member && !rootEffect)
			continue;

		std::string rawType;
		if (!config.TryGetString(section, "shader_type", rawType))
		{
			reason = "[" + section + "] requires shader_type";
			return false;
		}
		const std::string type = ConfigFile::NormalizeName(rawType);
		if (type != "nls" && type != "custom")
		{
			reason = "[" + section + "] shader_type must be nls or custom";
			return false;
		}
		if (type == "nls")
		{
			for (const char* rawName :
				{ "strength", "geometry", "center_protection", "curve", "quality" })
			{
				const std::string name(rawName);
				const std::string alias = "param_" + name;
				const auto typed = settings->find(name);
				const auto legacy = settings->find(alias);
				if (typed != settings->end() && legacy != settings->end())
				{
					reason = "[" + section + "] cannot define both " + name +
						" and " + alias;
					return false;
				}
				const auto selected = typed != settings->end() ? typed : legacy;
				if (selected == settings->end()) continue;
				std::string normalized;
				if (!NormalizeNlsSetting(name, selected->second, normalized))
				{
					reason = "[" + section + "] has invalid NLS " + name +
						" value '" + selected->second + "'";
					return false;
				}
			}
			double tolerance = 0.0;
			std::string rawTolerance;
			if (config.TryGetString(section, "tolerance_percent", rawTolerance) &&
				!ParseBoundedDouble(rawTolerance, 0.0, 50.0, tolerance))
			{
				reason = "[" + section + "] tolerance_percent must be from 0 through 50";
				return false;
			}
		}
		std::string rawOrder;
		if (config.TryGetString(section, "order", rawOrder))
		{
			try
			{
				size_t consumed = 0;
				const long parsed = std::stol(ConfigFile::Trim(rawOrder), &consumed);
				if (consumed != ConfigFile::Trim(rawOrder).size() || parsed < 0 ||
					parsed > INT_MAX) throw std::out_of_range("order");
			}
			catch (...)
			{
				reason = "[" + section + "] order must be a non-negative integer";
				return false;
			}
		}
		std::string rawStage;
		if (config.TryGetString(section, "stage", rawStage))
		{
			const std::string stage = ConfigFile::NormalizeName(rawStage);
			if (stage != "pre_resize" && stage != "pre" &&
				stage != "post_resize" && stage != "post")
			{
				reason = "[" + section + "] stage must be pre_resize or post_resize";
				return false;
			}
		}
	}
	return true;
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
	if (!config.Load())
		return selection;
	if (IsTargetShaderConfiguration(config))
	{
		constexpr const char* TARGET_KEY = "@shader-key:";
		const MadVRShaderRuntimeSnapshot runtime = g_runtimeState.GetSnapshot();
		const std::string selector = runtime.effectiveRule;
		const std::string key = selector.rfind(TARGET_KEY, 0) == 0 ?
			selector.substr(std::char_traits<char>::length(TARGET_KEY)) :
			std::string();
		std::vector<ShaderRule> rules;
		std::string reason;
		if (!LoadTargetRuleSelectionForBackend(config, key,
			TargetVideoLookup(videoState), ShaderRendererBackend::MADVR,
			rules, reason, &selection.activeSections))
		{
			DebugLog::Log("Shaders: VP-0079 selection failed: %s", reason.c_str());
			return selection;
		}
		selection.activeSectionsAvailable = true;
		std::vector<ShaderEntry> preScale;
		std::vector<ShaderEntry> postScale;
		for (ShaderRule rule : rules)
		{
			bool waiting = false;
			if (!ResolveNlsRuleForFrame(rule, runtime, videoState,
				selection.outputAspectRatioX, selection.outputAspectRatioY,
				waiting))
				continue;
			AppendLabel(selection.ruleLabel, rule.label);
			AppendLabel(selection.ruleName, rule.name);
			if (!waiting) AppendRuleEntries(rule, preScale, postScale);
		}
		std::stable_sort(preScale.begin(), preScale.end(),
			[](const ShaderEntry& left, const ShaderEntry& right)
			{ return left.order < right.order; });
		std::stable_sort(postScale.begin(), postScale.end(),
			[](const ShaderEntry& left, const ShaderEntry& right)
			{ return left.order < right.order; });
		ApplyShaderEntries(renderer, preScale, postScale, "ps_3_0", selection);
		return selection;
	}
	if (!config.HasSection(CONFIG_SECTION))
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
	ConfigFile config;
	const bool target = config.Load() && IsTargetShaderConfiguration(config);
	const std::string runtimeName = target ? ruleName :
		ConfigFile::NormalizeName(ruleName);
	if (updateRuntimeRequest)
	{
		g_runtimeState.SetRequestedRule(runtimeName);
		g_runtimeState.SetEffectiveRule(runtimeName);
		DebugLog::Log("Shaders: manual runtime request changed to \"%s\"",
			ruleName.empty() ? "automatic" : ruleName.c_str());
	}
	else
	{
		g_runtimeState.SetEffectiveRule(runtimeName);
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
			{
				MadVRNlsMappingDecision decision = runtime.nlsDecision;
				decision.mode = runtime.nlsMode;
				decision.sourceAspect =
					runtime.activeGeometry.aspectRatio;
				decision.targetAspect = GetNlsTargetAspect(rule);
				decision.verticalWarp =
					decision.sourceAspect > decision.targetAspect;
				const MadVRNlsPresentationPlan plan =
					ResolveMadVRNlsPresentationPlan(
						decision, runtime.activeGeometry);
				ruleX = plan.aspectX;
				ruleY = plan.aspectY;
			}
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
	if (rules.empty())
	{
		reason = "shader rule selects no effect applicable to this renderer";
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


std::string MadVRShaderLoader::CanonicalizeRuleSelector(
	const std::string& selector)
{
	constexpr const char* TARGET_KEY = "@shader-key:";
	const std::string trimmed = ConfigFile::Trim(selector);
	if (trimmed.rfind(TARGET_KEY, 0) == 0)
	{
		std::string canonical;
		const std::string key = trimmed.substr(
			std::char_traits<char>::length(TARGET_KEY));
		return RendererProfileConfig::CanonicalizeKeyChord(key, canonical) ?
			std::string(TARGET_KEY) + canonical : trimmed;
	}
	return ConfigFile::NormalizeName(trimmed);
}


bool MadVRShaderLoader::RuleSelectorsEqual(const std::string& left,
	const std::string& right)
{
	return CanonicalizeRuleSelector(left) == CanonicalizeRuleSelector(right);
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
		decision.reason = rule.nls ?
			"madVR NLS requires an explicit viewport screen_aspect" :
			"shader rule does not define NLS mapping";
		return false;
	}
	decision = ::EvaluateNlsMapping(aspectAvailable, activeAspectRatio,
		target, std::max(0.0, rule.aspectTolerancePercent),
		rule.activeAspectMinimum, rule.narrowerOnly,
		rule.maximumStretchRatio);
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


void MadVRShaderLoader::SetRuntimeNlsDecision(
	const MadVRNlsMappingDecision& decision)
{
	g_runtimeState.SetNlsDecision(decision);
}


void MadVRShaderLoader::SetRuntimeShaderSelection(
	const std::string& requestedRule, const std::string& effectiveRule,
	MadVRNlsMappingMode nlsMode)
{
	g_runtimeState.SetRuleSelection(
		CanonicalizeRuleSelector(requestedRule),
		CanonicalizeRuleSelector(effectiveRule), nlsMode);
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
	std::vector<std::string> ignoredSections;
	return GetConfiguredRuleSelection(ruleName, backend, selection,
		ignoredSections, reason);
}


bool MadVRShaderLoader::GetConfiguredRuleSelection(
	const std::string& ruleName, ShaderRendererBackend backend,
	std::vector<ConfiguredShaderRule>& selection,
	std::vector<std::string>& activeSections, std::string& reason)
{
	ConfigFile config;
	if (!config.Load())
	{
		reason = "configuration file is unavailable";
		return false;
	}
	return ResolveConfiguredRuleSelection(config, ruleName, backend,
		selection, activeSections, reason);
}


bool MadVRShaderLoader::ResolveConfiguredRuleSelection(const ConfigFile& config,
	const std::string& ruleName, ShaderRendererBackend backend,
	std::vector<ConfiguredShaderRule>& selection, std::string& reason)

{
	std::vector<std::string> ignoredSections;
	return ResolveConfiguredRuleSelection(config, ruleName, backend,
		selection, ignoredSections, reason);
}


bool MadVRShaderLoader::ResolveConfiguredRuleSelection(const ConfigFile& config,
	const std::string& ruleName, ShaderRendererBackend backend,
	std::vector<ConfiguredShaderRule>& selection,
	std::vector<std::string>& activeSections, std::string& reason)
{
	selection.clear();
	activeSections.clear();
	reason.clear();

	std::vector<ShaderRule> rules;
	if (IsTargetShaderConfiguration(config))
	{
		constexpr const char* TARGET_KEY = "@shader-key:";
		const std::string trimmed = ConfigFile::Trim(ruleName);
		if (trimmed.rfind(TARGET_KEY, 0) != 0)
		{
			reason = "target shader selector is invalid";
			return false;
		}
		if (!LoadTargetRuleSelectionForBackend(config,
			trimmed.substr(std::char_traits<char>::length(TARGET_KEY)),
			DisplayRuleExpression::ValueLookup(), backend, rules, reason,
			&activeSections))
			return false;
	}
	else if (!LoadRuleSelectionForBackend(config, ruleName, backend, rules))
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
	// A target root such as [shader.nls] may intentionally have no effect.
	// It is the group's baseline selected by its reset shortcut (for example
	// `n`).  Keep that meaning explicit for Alpha, whose NLS path otherwise
	// interprets an empty vector as an invalid NLS request.
	if (selection.empty() && IsTargetShaderConfiguration(config))
	{
		ConfiguredShaderRule off;
		off.name = "off";
		off.label = "Off";
		off.none = true;
		selection.push_back(std::move(off));
	}
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
