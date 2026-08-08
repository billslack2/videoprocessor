/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <atlstr.h>
#include <algorithm>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <regex>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dxgi.lib")

#include <version.h>
#include <cie.h>
#include <resource.h>
#include <StringUtils.h>
#include <VideoProcessorApp.h>
#include <microsoft_directshow/video_renderers/DirectShowVideoRenderers.h>
#include <microsoft_directshow/video_renderers/DirectShowMPCVideoRenderer.h>
#include <microsoft_directshow/video_renderers/DirectShowEnhancedVideoRenderer.h>
#include <microsoft_directshow/video_renderers/DirectShowGenericVideoRenderer.h>
#include <microsoft_directshow/video_renderers/DirectShowGenericHDRVideoRenderer.h>
#if defined(_WIN64)
#include <vprenderer/LibplaceboPluginVideoRenderer.h>
#endif
#include <guid.h>
#include <ConfigFile.h>
#include <EventActionLauncher.h>
#include <DisplayRefreshRateEstimator.h>
#include <DisplayRefreshRatePolicy.h>
#include <RendererProfileConfig.h>
#include <UnifiedProfileRuntime.h>


#include "VideoProcessorDlg.h"

namespace
{
using Microsoft::WRL::ComPtr;

struct CaptureVideoStateNotification
{
	ACaptureDeviceComPtr source;
	VideoStateComPtr state;
	uint64_t captureEpoch = 0;
	uint64_t sequence = 0;
	uint64_t ingressPublicationUs = 0;
	bool retainedRendererIngress = false;
};

const TCHAR* ToString(RendererResetReason reason)
{
	switch (reason)
	{
	case RendererResetReason::Manual: return TEXT("manual");
	case RendererResetReason::PostRendererStart:
		return TEXT("post-renderer-start");
	case RendererResetReason::RefreshTransition:
		return TEXT("refresh-transition");
	case RendererResetReason::HostTransition:
		return TEXT("host-transition");
	case RendererResetReason::OutputReadiness:
		return TEXT("output-readiness");
	case RendererResetReason::DisplayTransition: return TEXT("display-transition");
	case RendererResetReason::Resize: return TEXT("resize");
	case RendererResetReason::QueueSizeChange: return TEXT("queue-size-change");
	case RendererResetReason::TimingOffsetChange: return TEXT("timing-offset-change");
	case RendererResetReason::QueuePressure: return TEXT("queue-pressure");
	case RendererResetReason::QueueCapacity: return TEXT("queue-capacity");
	case RendererResetReason::SourceGapRecovery: return TEXT("source-gap-recovery");
	case RendererResetReason::LivenessRecovery: return TEXT("liveness-recovery");
	default: return TEXT("none");
	}
}

const char* ResetScopeName(RendererResetScope scope)
{
	switch (scope)
	{
	case RendererResetScope::GraphRetarget: return "graph-retarget";
	case RendererResetScope::Graph: return "graph";
	default: return "live-queue";
	}
}

struct ShortcutDefinition
{
	const char* configKey;
	WORD command;
	WORD defaultKey;
	BYTE defaultModifiers;
	bool rendererSpecific = false;
};

const ShortcutDefinition SHORTCUT_DEFINITIONS[] =
{
	{ "auto_set",              ID_COMMAND_AUTO_SET,               'A',       FCONTROL | FSHIFT },
	{ "fullscreen_exit",       ID_COMMAND_FULLSCREEN_EXIT,        VK_ESCAPE, 0 },
	{ "fullscreen_toggle",     ID_COMMAND_FULLSCREEN_TOGGLE,      VK_RETURN, FALT },
	{ "toggle_stats_overlay",  ID_COMMAND_TOGGLE_STATS_OVERLAY,   'I',       FCONTROL },
	{ "pq_set",                ID_COMMAND_PQ_SET,                 'P',       FCONTROL | FSHIFT },
	{ "renderer_restart",      ID_COMMAND_RENDERER_RESTART,       'R',       FSHIFT },
	{ "renderer_reset",        ID_COMMAND_RENDERER_RESET,         'R',       0 },
	{ "capture_1",             ID_COMMAND_CAPTURE_1,              '1',       FCONTROL },
	{ "capture_2",             ID_COMMAND_CAPTURE_2,              '2',       FCONTROL },
	{ "capture_3",             ID_COMMAND_CAPTURE_3,              '3',       FCONTROL },
	{ "capture_4",             ID_COMMAND_CAPTURE_4,              '4',       FCONTROL },
	{ "video_conversion_off",  ID_COMMAND_VC_NONE,                'V',       0 },
	{ "video_conversion_p010", ID_COMMAND_VC_P010,                'V',       FSHIFT },
	{ "config_editor",         ID_COMMAND_CONFIG_EDITOR,          'S',       FCONTROL },
	{ "screen_profile_normal", ID_COMMAND_SCREEN_PROFILE_NORMAL,  VK_F3,     0, true },
	{ "screen_profile_scope",  ID_COMMAND_SCREEN_PROFILE_SCOPE,   VK_F2,     0, true },
	{ "display_rules_auto",    ID_COMMAND_DISPLAY_RULE_AUTO,      VK_F4,     0, true },
};

bool TryParseShortcut(const std::string& value, ACCEL& accelerator)
{
	BYTE modifiers = FVIRTKEY;
	std::string keyToken;
	size_t start = 0;
	while (start <= value.size())
	{
		const size_t end = value.find('+', start);
		const std::string token = ConfigFile::Trim(value.substr(start, end - start));
		if (token.empty())
			return false;

		const std::string normalizedToken = ConfigFile::NormalizeName(token);
		if (normalizedToken == "ctrl" || normalizedToken == "control")
			modifiers |= FCONTROL;
		else if (normalizedToken == "alt")
			modifiers |= FALT;
		else if (normalizedToken == "shift")
			modifiers |= FSHIFT;
		else if (keyToken.empty())
			keyToken = token;
		else
			return false;

		if (end == std::string::npos)
			break;
		start = end + 1;
	}

	if (keyToken.empty())
		return false;

	const std::string normalizedKey = ConfigFile::NormalizeName(keyToken);
	WORD key = 0;
	if (normalizedKey == "escape" || normalizedKey == "esc")
		key = VK_ESCAPE;
	else if (normalizedKey == "enter" || normalizedKey == "return")
		key = VK_RETURN;
	else if (normalizedKey.size() >= 2 && normalizedKey[0] == 'f')
	{
		const std::string numberToken = normalizedKey.substr(1);
		if (numberToken.empty() || numberToken.size() > 2 ||
			!std::all_of(numberToken.begin(), numberToken.end(),
				[](unsigned char character) { return std::isdigit(character) != 0; }))
			return false;

		const int functionNumber = std::stoi(numberToken);
		if (functionNumber < 1 || functionNumber > 24)
			return false;
		key = static_cast<WORD>(VK_F1 + functionNumber - 1);
	}
	else if (keyToken.size() == 1 && std::isalnum(static_cast<unsigned char>(keyToken[0])))
	{
		key = static_cast<WORD>(std::toupper(static_cast<unsigned char>(keyToken[0])));
		// Letter case is meaningful only for the key itself: V means Shift+V,
		// while v means V. Modifier names remain case-insensitive.
		if (std::isupper(static_cast<unsigned char>(keyToken[0])))
			modifiers |= FSHIFT;
	}
	else
		return false;

	accelerator = { modifiers, key, 0 };
	return true;
}

std::vector<std::string> SplitConfiguredList(const std::string& value)
{
	std::vector<std::string> items;
	std::istringstream stream(value);
	std::string item;
	while (std::getline(stream, item, ','))
	{
		item = ConfigFile::Trim(item);
		if (!item.empty())
			items.push_back(item);
	}
	return items;
}

struct ActiveMonitorCandidate
{
	HMONITOR monitor = nullptr;
	CString sourceName;
	CString friendlyName;
};

BOOL CALLBACK EnumerateActiveMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
{
	auto* candidates = reinterpret_cast<std::vector<ActiveMonitorCandidate>*>(parameter);
	MONITORINFOEXW monitorInfo = {};
	monitorInfo.cbSize = sizeof(monitorInfo);
	if (!GetMonitorInfoW(monitor, &monitorInfo))
		return TRUE;

	ActiveMonitorCandidate candidate;
	candidate.monitor = monitor;
	candidate.sourceName = monitorInfo.szDevice;
	candidates->push_back(candidate);
	return TRUE;
}

bool PopulateActiveMonitorFriendlyNames(std::vector<ActiveMonitorCandidate>& candidates)
{
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	UINT32 pathCount = 0;
	UINT32 modeCount = 0;
	LONG result = ERROR_SUCCESS;
	do
	{
		result = GetDisplayConfigBufferSizes(
			QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
		if (result != ERROR_SUCCESS)
			return false;
		paths.resize(pathCount);
		modes.resize(modeCount);
		result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount,
			paths.data(), &modeCount, modes.data(), nullptr);
	} while (result == ERROR_INSUFFICIENT_BUFFER);
	if (result != ERROR_SUCCESS)
		return false;
	paths.resize(pathCount);
	modes.resize(modeCount);

	for (UINT32 index = 0; index < pathCount; ++index)
	{
		const DISPLAYCONFIG_PATH_INFO& path = paths[index];
		DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
		source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		source.header.size = sizeof(source);
		source.header.adapterId = path.sourceInfo.adapterId;
		source.header.id = path.sourceInfo.id;
		if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
			return false;

		DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
		target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		target.header.size = sizeof(target);
		target.header.adapterId = path.targetInfo.adapterId;
		target.header.id = path.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
			return false;

		for (ActiveMonitorCandidate& candidate : candidates)
			if (_wcsicmp(candidate.sourceName, source.viewGdiDeviceName) == 0)
			{
				candidate.friendlyName = target.monitorFriendlyDeviceName;
				break;
			}
	}

	return true;
}

CString DescribeMonitorCandidates(const std::vector<ActiveMonitorCandidate>& candidates)
{
	CString description;
	for (const ActiveMonitorCandidate& candidate : candidates)
	{
		if (!description.IsEmpty())
			description += L"; ";
		CString entry;
		entry.Format(L"%s=%s", candidate.sourceName.GetString(),
			candidate.friendlyName.IsEmpty() ? L"(unnamed)" : candidate.friendlyName.GetString());
		description += entry;
	}
	return description.IsEmpty() ? CString(L"(none)") : description;
}


HACCEL CreateConfiguredAccelerators(
	std::map<WORD, CString>& shaderShortcutRules,
	std::set<WORD>& shaderShortcutKeys,
	std::map<WORD, CString>& displayRuleShortcutRules,
	std::map<WORD, unsigned int>& rendererShortcutIndices,
	std::map<WORD, CString>& unifiedProfileShortcutKeys)
{
	shaderShortcutRules.clear();
	shaderShortcutKeys.clear();
	displayRuleShortcutRules.clear();
	rendererShortcutIndices.clear();
	unifiedProfileShortcutKeys.clear();
	ConfigFile mainConfig;
	const bool hasMainConfig = mainConfig.Load();
	ConfigFile rendererConfig;
	const bool hasRendererConfig =
		rendererConfig.Load(ConfigFile::RENDERER_FILENAME);
	RendererProfileConfig::Model unifiedProfileModel;
	std::string unifiedProfileError;
	bool hasNamedLldvProfile = false;
	if (hasMainConfig)
		for (const std::string& section : mainConfig.GetSectionNames())
			if (section.rfind("lldv.", 0) == 0)
			{
				hasNamedLldvProfile = true;
				break;
			}
	const bool hasUnifiedRendererConfig = hasMainConfig &&
		(RendererProfileConfig::IsUnified(mainConfig) ||
		 mainConfig.HasSection("lldv") ||
		 hasNamedLldvProfile) &&
		RendererProfileConfig::Read(mainConfig, unifiedProfileModel, unifiedProfileError);
	std::vector<ACCEL> accelerators;
	std::set<unsigned int> bindings;

	for (const auto& definition : SHORTCUT_DEFINITIONS)
	{
		if (hasUnifiedRendererConfig && definition.rendererSpecific)
			continue;
		ACCEL accelerator = { static_cast<BYTE>(FVIRTKEY | definition.defaultModifiers), definition.defaultKey, definition.command };
		std::string configuredValue;
		const ConfigFile& config =
			definition.rendererSpecific ? rendererConfig : mainConfig;
		const bool hasConfig =
			definition.rendererSpecific ? hasRendererConfig : hasMainConfig;
		if (hasConfig &&
			config.TryGetString("shortcuts", definition.configKey, configuredValue))
		{
			// A present but empty value is an explicit opt-out.  This is
			// intentionally different from an absent key, which keeps the
			// compiled default shortcut.
			if (ConfigFile::Trim(configuredValue).empty())
				continue;

			ACCEL configuredAccelerator = {};
			if (TryParseShortcut(configuredValue, configuredAccelerator))
			{
				configuredAccelerator.cmd = definition.command;
				accelerator = configuredAccelerator;
			}
		}

		const unsigned int binding = (static_cast<unsigned int>(accelerator.fVirt) << 16) | accelerator.key;
		if (bindings.insert(binding).second)
		{
			accelerators.push_back(accelerator);
		}
		else
		{
			// A duplicate user binding is ambiguous, so retain the command's
			// compiled default when it is still available.
			accelerator = { static_cast<BYTE>(FVIRTKEY | definition.defaultModifiers), definition.defaultKey, definition.command };
			const unsigned int defaultBinding = (static_cast<unsigned int>(accelerator.fVirt) << 16) | accelerator.key;
			if (bindings.insert(defaultBinding).second)
				accelerators.push_back(accelerator);
		}
	}

	// Renderer shortcuts are indexed by the 1-based, sorted renderer-combo
	// order. This remains unambiguous even when registered renderers have the
	// same friendly name.
	const auto* shortcutValues =
		hasMainConfig ? mainConfig.GetSectionValues("shortcuts") : nullptr;
	if (shortcutValues)
	{
		WORD nextCommand = ID_COMMAND_RENDERER_SELECT_FIRST;
		for (const auto& entry : *shortcutValues)
		{
			unsigned int rendererIndex = 0;
			if (!ConfigFile::TryParseIndexedKey(
				entry.first,
				"render",
				rendererIndex))
				continue;
			if (nextCommand > ID_COMMAND_RENDERER_SELECT_LAST)
			{
				DEBUGLOG("Renderer shortcut '%s' ignored: command capacity exceeded",
					entry.first.c_str());
				break;
			}

			ACCEL accelerator = {};
			if (!TryParseShortcut(entry.second, accelerator))
			{
				DEBUGLOG("Invalid shortcut '%s' for renderer index %u",
					entry.second.c_str(),
					rendererIndex);
				continue;
			}
			const unsigned int binding =
				(static_cast<unsigned int>(accelerator.fVirt) << 16) |
				accelerator.key;
			if (!bindings.insert(binding).second)
			{
				DEBUGLOG("Duplicate shortcut '%s' ignored for renderer index %u",
					entry.second.c_str(),
					rendererIndex);
				continue;
			}

			accelerator.cmd = nextCommand;
			accelerators.push_back(accelerator);
			rendererShortcutIndices[nextCommand] = rendererIndex;
			++nextCommand;
		}
	}

	// Shader-rule shortcuts live with their rule rather than in the fixed
	// shortcut table. This permits any external shader chain to be selected at
	// runtime without adding a new command or rebuilding the application.
	std::string ruleList;
	if (hasMainConfig && mainConfig.TryGetString("shaders", "rules", ruleList))
	{
		std::set<std::string> seenRules;
		std::map<unsigned int, WORD> shaderBindingCommands;
		WORD nextCommand = ID_COMMAND_SHADER_RULE_FIRST;
		for (const std::string& configuredRule : SplitConfiguredList(ruleList))
		{
			if (nextCommand > ID_COMMAND_SHADER_RULE_LAST)
				break;
			const std::string rule = ConfigFile::NormalizeName(configuredRule);
			if (!seenRules.insert(rule).second)
				continue;

			const std::string section = "shaders." + rule;
			std::string shortcut;
			if (!mainConfig.TryGetString(section, "shortcut", shortcut))
				continue;

			ACCEL accelerator = {};
			if (!TryParseShortcut(shortcut, accelerator))
			{
				DEBUGLOG("Invalid shortcut '%s' for shader rule '%s'", shortcut.c_str(), rule.c_str());
				continue;
			}

			const unsigned int binding =
				(static_cast<unsigned int>(accelerator.fVirt) << 16) | accelerator.key;
			const auto existingShader =
				shaderBindingCommands.find(binding);
			if (existingShader != shaderBindingCommands.end())
			{
				CString ruleName;
				ruleName.Format(TEXT("%S"), rule.c_str());
				shaderShortcutRules[existingShader->second] += TEXT(",");
				shaderShortcutRules[existingShader->second] += ruleName;
				DEBUGLOG(
					"Shader shortcut '%s' groups effect '%s' with command %u",
					shortcut.c_str(), rule.c_str(),
					existingShader->second);
				continue;
			}
			if (!bindings.insert(binding).second)
			{
				DEBUGLOG("Shortcut '%s' for shader effect '%s' conflicts with a non-shader command and was ignored", shortcut.c_str(), rule.c_str());
				continue;
			}

			accelerator.cmd = nextCommand;
			accelerators.push_back(accelerator);
			CString ruleName;
			ruleName.Format(TEXT("%S"), rule.c_str());
			shaderShortcutRules[nextCommand] = ruleName;
			shaderShortcutKeys.insert(accelerator.key);
			shaderBindingCommands[binding] = nextCommand;
			++nextCommand;
		}
	}

	// VP-0079 gives shader groups the same expression language as the other
	// selectable configuration. A chord is registered once; the shader resolver
	// applies every group member that chord selects (for example all members of
	// a type=multi cleanup group).
	if (hasMainConfig)
	{
		std::map<unsigned int, WORD> targetShaderBindings;
		WORD nextCommand = ID_COMMAND_SHADER_RULE_FIRST;
		for (const std::string& section : mainConfig.GetSectionNames())
		{
			if (section.rfind("shader.", 0) != 0)
				continue;
			std::string when;
			std::string shortcut;
			mainConfig.TryGetString(section, "when", when);
			mainConfig.TryGetString(section, "shortcut", shortcut);
			std::string mergeError;
			if (!RendererProfileConfig::MergeShortcutIntoWhen(
				shortcut, "[" + section + "]", when, mergeError))
			{
				DEBUGLOG("Invalid [%S] shortcut ignored for accelerators: %S",
					section.c_str(), mergeError.c_str());
				continue;
			}
			if (when.empty())
				continue;
			DisplayRuleExpression::Expression expression;
			std::string error;
			if (!expression.Compile(when, error, true))
			{
				DEBUGLOG("Invalid [%S] when ignored for accelerators: %S",
					section.c_str(), error.c_str());
				continue;
			}
			for (const std::string& chord : expression.KeyChords())
			{
				if (nextCommand > ID_COMMAND_SHADER_RULE_LAST)
					break;
				ACCEL accelerator = {};
				if (!TryParseShortcut(chord, accelerator))
				{
					DEBUGLOG("Shader key '%S' in [%S] is not a supported accelerator",
						chord.c_str(), section.c_str());
					continue;
				}
				const unsigned int binding =
					(static_cast<unsigned int>(accelerator.fVirt) << 16) |
					accelerator.key;
				if (targetShaderBindings.find(binding) != targetShaderBindings.end())
					continue;
				if (!bindings.insert(binding).second)
				{
					DEBUGLOG("Shader key '%S' conflicts with an existing command", chord.c_str());
					continue;
				}
				accelerator.cmd = nextCommand;
				accelerators.push_back(accelerator);
				CString selector;
				selector.Format(TEXT("@shader-key:%S"), chord.c_str());
				shaderShortcutRules[nextCommand] = selector;
				shaderShortcutKeys.insert(accelerator.key);
				targetShaderBindings[binding] = nextCommand;
				++nextCommand;
			}
		}
	}

	// Display-rule shortcuts are renderer-specific profiles.  They select a
	// manual override; the renderer returns to automatic selection after a
	// material source transition or when display_rules_auto is pressed.
	std::string displayRuleList;
	if (hasRendererConfig && rendererConfig.TryGetString("display_rules", "rules", displayRuleList))
	{
		std::set<std::string> seenRules;
		WORD nextCommand = ID_COMMAND_DISPLAY_RULE_FIRST;
		for (const std::string& configuredRule : SplitConfiguredList(displayRuleList))
		{
			if (nextCommand > ID_COMMAND_DISPLAY_RULE_LAST)
				break;
			const std::string rule = ConfigFile::NormalizeName(configuredRule);
			if (!seenRules.insert(rule).second)
				continue;

			std::string shortcut;
			if (!rendererConfig.TryGetString("display_rules." + rule, "shortcut", shortcut))
				continue;

			ACCEL accelerator = {};
			if (!TryParseShortcut(shortcut, accelerator))
			{
				DEBUGLOG("Invalid shortcut '%s' for display rule '%s'", shortcut.c_str(), rule.c_str());
				continue;
			}

			const unsigned int binding =
				(static_cast<unsigned int>(accelerator.fVirt) << 16) | accelerator.key;
			if (!bindings.insert(binding).second)
			{
				DEBUGLOG("Duplicate shortcut '%s' ignored for display rule '%s'", shortcut.c_str(), rule.c_str());
				continue;
			}

			accelerator.cmd = nextCommand;
			accelerators.push_back(accelerator);
			CString ruleName;
			ruleName.Format(TEXT("%S"), rule.c_str());
			displayRuleShortcutRules[nextCommand] = ruleName;
			++nextCommand;
		}
	}

	// Register one command per canonical key, not per profile. The renderer then
	// resolves all matching groups from that single physical key event.
	if (hasUnifiedRendererConfig)
	{
		std::vector<std::string> chords;
		if (!RendererProfileConfig::CollectKeyChords(unifiedProfileModel, chords, unifiedProfileError))
		{
			DEBUGLOG("Unified renderer shortcut discovery failed: %s", unifiedProfileError.c_str());
		}
		else
		{
			WORD nextCommand = ID_COMMAND_UNIFIED_PROFILE_FIRST;
			for (const std::string& chord : chords)
			{
				if (nextCommand > ID_COMMAND_UNIFIED_PROFILE_LAST) break;
				ACCEL accelerator = {};
				if (!TryParseShortcut(chord, accelerator)) { DEBUGLOG("Invalid unified profile shortcut '%s'", chord.c_str()); continue; }
				const unsigned int binding = (static_cast<unsigned int>(accelerator.fVirt) << 16) | accelerator.key;
				if (!bindings.insert(binding).second) { DEBUGLOG("Duplicate unified profile shortcut '%s' ignored", chord.c_str()); continue; }
				accelerator.cmd = nextCommand;
				accelerators.push_back(accelerator);
				CString keyName; keyName.Format(TEXT("%S"), chord.c_str());
				unifiedProfileShortcutKeys[nextCommand] = keyName;
				++nextCommand;
			}
		}
	}

	return CreateAcceleratorTable(accelerators.data(), static_cast<int>(accelerators.size()));
}

struct DisplayTimingSnapshot
{
	// Long weighted estimate used only by phase-sensitive correction.
	double refreshRateHz = 0.0;
	// Current clean-window estimate used by output readiness.
	double readinessRefreshRateHz = 0.0;
	double readinessEvidenceSeconds = 0.0;
	// Earliest independently validated DXGI evidence. It may initiate the
	// reset/prefill transition, but cannot establish long phase confidence.
	double startupRefreshRateHz = 0.0;
	double startupEvidenceSeconds = 0.0;
	double startupRawWaitRateHz = 0.0;
	double advertisedRefreshRateHz = 0.0;
	double rawWaitRateHz = 0.0;
	int64_t lastVBlankQpc = 0;
	int64_t refreshPeriodQpc = 0;
	int64_t qpcFrequency = 0;
	int64_t rateMeasuredQpc = 0;
	int64_t startupRateMeasuredQpc = 0;
	int64_t measurementStartedQpc = 0;
	int64_t minimumWaitIntervalQpc = 0;
	int64_t maximumWaitIntervalQpc = 0;
	int64_t startupMinimumWaitIntervalQpc = 0;
	int64_t startupMaximumWaitIntervalQpc = 0;
	uint64_t intervalsObserved = 0;
	uint64_t rawWaitIntervalsObserved = 0;
	uint64_t startupIntervalsObserved = 0;
	uint64_t startupRawWaitIntervalsObserved = 0;
	uint64_t generation = 0;
	bool rateStable = false;
	bool readinessEvidenceReady = false;
	bool startupEvidenceReady = false;
	bool dwmCompositionEnabled = false;
	HRESULT dwmTimingResult = E_FAIL;
};

std::wstring GetMonitorDeviceName(HWND hwnd)
{
	const HMONITOR monitor = hwnd ?
		MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
	if (!monitor)
		return std::wstring();

	MONITORINFOEX monitorInfo = {};
	monitorInfo.cbSize = sizeof(monitorInfo);
	return GetMonitorInfo(monitor, &monitorInfo) ?
		std::wstring(monitorInfo.szDevice) : std::wstring();
}

// QueryDisplayConfig exposes the active target path rate as a rational number
// (for example 24000/1001), unlike EnumDisplaySettings' integer-only
// dmDisplayFrequency.  It is a configured display-mode diagnostic, not a
// physical-vblank measurement, but is a useful independent source when DWM
// and DXGI timing disagree.
double GetActiveTargetRefreshRate(HWND hwnd)
{
	const HMONITOR monitor = hwnd ?
		MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
	if (!monitor)
		return 0.0;

	MONITORINFOEX monitorInfo = {};
	monitorInfo.cbSize = sizeof(monitorInfo);
	if (!GetMonitorInfo(monitor, &monitorInfo))
		return 0.0;

	UINT32 pathCount = 0;
	UINT32 modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount,
		&modeCount) != ERROR_SUCCESS || pathCount == 0)
		return 0.0;

	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
		&modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
	{
		return 0.0;
	}

	for (UINT32 pathIndex = 0; pathIndex < pathCount; ++pathIndex)
	{
		const DISPLAYCONFIG_PATH_INFO& path = paths[pathIndex];
		DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
		sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		sourceName.header.size = sizeof(sourceName);
		sourceName.header.adapterId = path.sourceInfo.adapterId;
		sourceName.header.id = path.sourceInfo.id;
		if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS ||
			wcscmp(sourceName.viewGdiDeviceName, monitorInfo.szDevice) != 0)
		{
			continue;
		}

		const DISPLAYCONFIG_RATIONAL& refreshRate = path.targetInfo.refreshRate;
		if (refreshRate.Numerator > 0 && refreshRate.Denominator > 0)
		{
			return static_cast<double>(refreshRate.Numerator) /
				static_cast<double>(refreshRate.Denominator);
		}
	}

	return 0.0;
}

class DisplayRefreshRateSampler
{
public:
	DisplayRefreshRateSampler()
		: m_thread(&DisplayRefreshRateSampler::Run, this)
	{
	}

	void SetWindow(HWND hwnd)
	{
		const HMONITOR monitor = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_window == hwnd && m_monitor == monitor)
				return;
			m_window = hwnd;
			m_monitor = monitor;
			m_nominalRateHz = 0.0;
			++m_targetGeneration;
			ClearMeasurementLocked();
		}
		m_wake.notify_one();
	}

	void SetNominalRate(double nominalRateHz)
	{
		const double normalizedRate =
			std::isfinite(nominalRateHz) && nominalRateHz >= 10.0 &&
			nominalRateHz <= 240.0 ? nominalRateHz : 0.0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (std::fabs(m_nominalRateHz - normalizedRate) < 0.01)
				return;
			m_nominalRateHz = normalizedRate;
			++m_targetGeneration;
			ClearMeasurementLocked();
		}
		m_wake.notify_one();
	}

	void ResetMeasurement()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_targetGeneration;
			ClearMeasurementLocked();
		}
		m_wake.notify_one();
	}

	void SetPhaseTracking(bool enabled)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_phaseTracking == enabled)
				return;
			m_phaseTracking = enabled;
		}
		m_wake.notify_one();
	}

	double GetRate() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_rate;
	}

	DisplayTimingSnapshot GetTimingSnapshot() const
	{
		DisplayTimingSnapshot result;
		std::lock_guard<std::mutex> lock(m_mutex);
		result.refreshRateHz = m_rate;
		result.readinessRefreshRateHz = m_readinessRate;
		result.readinessEvidenceSeconds = m_readinessEvidenceSeconds;
		result.startupRefreshRateHz = m_startupRate;
		result.startupEvidenceSeconds = m_startupEvidenceSeconds;
		result.startupRawWaitRateHz = m_startupRawWaitRate;
		result.lastVBlankQpc = m_lastVBlankQpc.load(std::memory_order_acquire);
		result.refreshPeriodQpc = m_refreshPeriodQpc.load(std::memory_order_acquire);
		result.qpcFrequency = m_qpcFrequency;
		result.rateMeasuredQpc = m_rateMeasuredQpc;
		result.startupRateMeasuredQpc = m_startupRateMeasuredQpc;
		result.measurementStartedQpc = m_measurementStartedQpc;
		result.intervalsObserved = m_intervalsObserved;
		result.rawWaitRateHz = m_rawWaitRate;
		result.minimumWaitIntervalQpc = m_minimumWaitIntervalQpc;
		result.maximumWaitIntervalQpc = m_maximumWaitIntervalQpc;
		result.rawWaitIntervalsObserved = m_rawWaitIntervalsObserved;
		result.startupMinimumWaitIntervalQpc =
			m_startupMinimumWaitIntervalQpc;
		result.startupMaximumWaitIntervalQpc =
			m_startupMaximumWaitIntervalQpc;
		result.startupIntervalsObserved = m_startupIntervalsObserved;
		result.startupRawWaitIntervalsObserved =
			m_startupRawWaitIntervalsObserved;
		result.generation = m_targetGeneration;
		result.rateStable = m_rateStable;
		result.readinessEvidenceReady = m_readinessEvidenceReady;
		result.startupEvidenceReady = m_startupEvidenceReady;
		return result;
	}

private:
	void ClearMeasurementLocked()
	{
		m_rate = 0.0;
		m_readinessRate = 0.0;
		m_readinessEvidenceSeconds = 0.0;
		m_startupRate = 0.0;
		m_startupEvidenceSeconds = 0.0;
		m_startupRawWaitRate = 0.0;
		m_rateMeasuredQpc = 0;
		m_startupRateMeasuredQpc = 0;
		m_measurementStartedQpc = 0;
		m_intervalsObserved = 0;
		m_rawWaitRate = 0.0;
		m_minimumWaitIntervalQpc = 0;
		m_maximumWaitIntervalQpc = 0;
		m_rawWaitIntervalsObserved = 0;
		m_startupMinimumWaitIntervalQpc = 0;
		m_startupMaximumWaitIntervalQpc = 0;
		m_startupIntervalsObserved = 0;
		m_startupRawWaitIntervalsObserved = 0;
		m_rateStable = false;
		m_readinessEvidenceReady = false;
		m_startupEvidenceReady = false;
		m_lastVBlankQpc.store(0, std::memory_order_release);
		m_refreshPeriodQpc.store(0, std::memory_order_release);
	}

	static bool FindOutput(HMONITOR monitor, ComPtr<IDXGIOutput>& result)
	{
		ComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
			return false;

		for (UINT adapterIndex = 0;; ++adapterIndex)
		{
			ComPtr<IDXGIAdapter1> adapter;
			if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND)
				break;

			for (UINT outputIndex = 0;; ++outputIndex)
			{
				ComPtr<IDXGIOutput> output;
				if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND)
					break;

				DXGI_OUTPUT_DESC desc = {};
				if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor)
				{
					result = output;
					return true;
				}
			}
		}
		return false;
	}

	void Run()
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);

		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_qpcFrequency = frequency.QuadPart;
		}

		for (;;)
		{
			HMONITOR monitor = nullptr;
			HWND window = nullptr;
			double nominalRateHz = 0.0;
			uint64_t targetGeneration = 0;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_wake.wait(lock, [this] { return m_monitor != nullptr; });
				monitor = m_monitor;
				window = m_window;
				nominalRateHz = m_nominalRateHz;
				targetGeneration = m_targetGeneration;
			}

			ComPtr<IDXGIOutput> output;
			if (!FindOutput(monitor, output))
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_wake.wait_for(lock, std::chrono::seconds(1), [this, targetGeneration] {
					return m_targetGeneration != targetGeneration;
				});
				continue;
			}

			// Measure a long set of physical vblank intervals. WaitForVBlank is
			// tied to the output, but the worker can be descheduled across more
			// than one vblank at 24 Hz. Compensate for multi-period QPC gaps rather
			// than assuming every wake-up represents exactly one refresh. DWM's
			// cRefresh is a compositor wake count and can miss a vblank too, so use
			// its period only as the initial interval estimate for this sampler.
			// The estimator keeps three deliberately distinct evidence products:
			// a post-transition quarantine, a clean current rate for readiness,
			// and a longer recency-weighted rate for phase correction.
			constexpr double kPublishIntervalSeconds = 1.0;
			LARGE_INTEGER first = {};
			LARGE_INTEGER last = {};
			LARGE_INTEGER previous = {};
			long double estimatedRefreshPeriodQpc = 0.0L;
			if (nominalRateHz > 0.0)
			{
				estimatedRefreshPeriodQpc =
					static_cast<long double>(frequency.QuadPart) /
					static_cast<long double>(nominalRateHz);
			}
			DisplayRefreshRateEstimator rateEstimator(frequency.QuadPart);
			int64_t lastPublishedQpc = 0;
			unsigned int samples = 0;
			for (;;)
			{
				if (output->WaitForVBlank() != S_OK)
					break;

				LARGE_INTEGER now = {};
				QueryPerformanceCounter(&now);

				DWM_TIMING_INFO timing = {};
				timing.cbSize = sizeof(timing);
				const bool hasDwmTiming = window &&
					SUCCEEDED(DwmGetCompositionTimingInfo(window, &timing)) &&
					timing.qpcRefreshPeriod > 0 && timing.qpcVBlank > 0;
				const int64_t observedVBlankQpc = hasDwmTiming ?
					static_cast<int64_t>(timing.qpcVBlank) : now.QuadPart;
				m_lastVBlankQpc.store(observedVBlankQpc, std::memory_order_release);
				if (samples == 0)
				{
					first = now;
					previous = now;
					if (hasDwmTiming)
						estimatedRefreshPeriodQpc =
							static_cast<long double>(timing.qpcRefreshPeriod);
					std::lock_guard<std::mutex> lock(m_mutex);
					if (m_targetGeneration == targetGeneration)
						m_measurementStartedQpc = first.QuadPart;
				}
				else
				{
					const int64_t elapsedSincePreviousQpc = now.QuadPart - previous.QuadPart;
					if (estimatedRefreshPeriodQpc <= 0.0L)
						estimatedRefreshPeriodQpc =
							static_cast<long double>(elapsedSincePreviousQpc);

					const uint64_t elapsedIntervals = std::max<uint64_t>(1,
						static_cast<uint64_t>(llround(
							static_cast<long double>(elapsedSincePreviousQpc) /
							estimatedRefreshPeriodQpc)));
					rateEstimator.Observe(now.QuadPart, elapsedSincePreviousQpc,
						elapsedIntervals);

					// A single normal interval refines the period. A compensated
					// multi-interval gap is also useful, but give it less weight so a
					// long scheduler pause cannot move the rate materially.
					const long double observedPeriod =
						static_cast<long double>(elapsedSincePreviousQpc) / elapsedIntervals;
					if (observedPeriod > estimatedRefreshPeriodQpc * 0.5L &&
						observedPeriod < estimatedRefreshPeriodQpc * 1.5L)
					{
						const long double weight = elapsedIntervals == 1 ? 0.10L : 0.02L;
						estimatedRefreshPeriodQpc +=
							(observedPeriod - estimatedRefreshPeriodQpc) * weight;
					}
					previous = now;
				}
				last = now;
				++samples;

				const DisplayRefreshRateEstimatorSnapshot estimate =
					rateEstimator.Snapshot();
				if (estimate.materialRateChangeDetected)
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					if (m_targetGeneration == targetGeneration)
					{
						DebugLog::Log(
							"Display-rate measurement invalidated: fast=%.6f Hz "
							"weighted=%.6f Hz; collecting a new generation",
							estimate.fastRateHz, estimate.phaseRateHz);
						++m_targetGeneration;
						ClearMeasurementLocked();
					}
					break;
				}
				const bool rateHasBeenPublished = lastPublishedQpc != 0;
				const bool publishRate = !rateHasBeenPublished ||
						(last.QuadPart - lastPublishedQpc) >=
							static_cast<int64_t>(kPublishIntervalSeconds * frequency.QuadPart);
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					if (m_targetGeneration == targetGeneration)
					{
						m_intervalsObserved = estimate.recentCompensatedIntervals;
						m_rawWaitIntervalsObserved = estimate.recentRawIntervals;
						m_minimumWaitIntervalQpc =
							estimate.recentMinimumWaitIntervalQpc;
						m_maximumWaitIntervalQpc =
							estimate.recentMaximumWaitIntervalQpc;
						m_startupIntervalsObserved =
							estimate.startupCompensatedIntervals;
						m_startupRawWaitIntervalsObserved =
							estimate.startupRawIntervals;
						m_startupMinimumWaitIntervalQpc =
							estimate.startupMinimumWaitIntervalQpc;
						m_startupMaximumWaitIntervalQpc =
							estimate.startupMaximumWaitIntervalQpc;
					}
				}
				if (publishRate)
				{
					if (estimate.startupRateHz >= 10.0 &&
						estimate.startupRateHz <= 240.0)
					{
						std::lock_guard<std::mutex> lock(m_mutex);
						if (m_targetGeneration == targetGeneration)
						{
							m_startupRate = estimate.startupRateHz;
							m_startupEvidenceSeconds =
								estimate.startupEvidenceSeconds;
							m_startupRawWaitRate =
								estimate.startupRawWaitRateHz;
							m_startupEvidenceReady =
								estimate.startupEvidenceReady;
							m_startupRateMeasuredQpc = last.QuadPart;
						}
					}
					if (estimate.phaseRateHz > 0.0)
					{
						const double rate = estimate.phaseRateHz;
						if (rate >= 20.0 && rate <= 120.0)
						{
							std::lock_guard<std::mutex> lock(m_mutex);
							if (m_targetGeneration == targetGeneration)
							{
								m_rate = rate;
								m_readinessRate = estimate.readinessRateHz;
								m_readinessEvidenceSeconds =
									estimate.evidenceSeconds;
								m_readinessEvidenceReady =
									estimate.readinessEvidenceReady;
								m_rawWaitRate = estimate.recentRawWaitRateHz;
								m_rateMeasuredQpc = last.QuadPart;
								m_rateStable = estimate.phaseEvidenceReady;
								m_refreshPeriodQpc.store(
									static_cast<int64_t>(llround(
										static_cast<double>(frequency.QuadPart) / rate)),
									std::memory_order_release);
							}
						}
					}
					lastPublishedQpc = last.QuadPart;
					// The model separates current readiness and weighted phase evidence;
					// neither can borrow validity from a prior display generation.
				}

				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_targetGeneration != targetGeneration)
					break;
			}
		}
	}

	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	std::thread m_thread;
	HWND m_window = nullptr;
	HMONITOR m_monitor = nullptr;
	double m_nominalRateHz = 0.0;
	uint64_t m_targetGeneration = 0;
	double m_rate = 0.0;
	double m_readinessRate = 0.0;
	double m_readinessEvidenceSeconds = 0.0;
	double m_startupRate = 0.0;
	double m_startupEvidenceSeconds = 0.0;
	double m_startupRawWaitRate = 0.0;
	double m_rawWaitRate = 0.0;
	int64_t m_rateMeasuredQpc = 0;
	int64_t m_startupRateMeasuredQpc = 0;
	int64_t m_measurementStartedQpc = 0;
	uint64_t m_intervalsObserved = 0;
	uint64_t m_rawWaitIntervalsObserved = 0;
	int64_t m_minimumWaitIntervalQpc = 0;
	int64_t m_maximumWaitIntervalQpc = 0;
	int64_t m_startupMinimumWaitIntervalQpc = 0;
	int64_t m_startupMaximumWaitIntervalQpc = 0;
	bool m_rateStable = false;
	bool m_readinessEvidenceReady = false;
	bool m_startupEvidenceReady = false;
	uint64_t m_startupIntervalsObserved = 0;
	uint64_t m_startupRawWaitIntervalsObserved = 0;
	bool m_phaseTracking = false;
	int64_t m_qpcFrequency = 0;
	std::atomic<int64_t> m_lastVBlankQpc = 0;
	std::atomic<int64_t> m_refreshPeriodQpc = 0;
};

// IDXGIOutput::WaitForVBlank has no cancellation mechanism. Keep this sampler
// alive for the process lifetime so application shutdown can never block while
// joining the worker.
DisplayRefreshRateSampler* g_displayRefreshRateSampler = new DisplayRefreshRateSampler();

// DWM exposes the compositor's measured refresh period in QPC ticks.  This is
// non-blocking and therefore safe to sample from the UI stats timer.
DisplayTimingSnapshot GetDisplayTimingSnapshot(HWND hwnd)
{
	DisplayTimingSnapshot result;
	BOOL compositionEnabled = FALSE;
	result.dwmCompositionEnabled =
		SUCCEEDED(DwmIsCompositionEnabled(&compositionEnabled)) && compositionEnabled;
	DWM_TIMING_INFO timing = {};
	timing.cbSize = sizeof(timing);
	if (!hwnd)
		return result;

	result.dwmTimingResult = DwmGetCompositionTimingInfo(hwnd, &timing);
	if (FAILED(result.dwmTimingResult) || timing.qpcRefreshPeriod == 0)
		return result;

	LARGE_INTEGER qpcFrequency = {};
	if (!QueryPerformanceFrequency(&qpcFrequency) || qpcFrequency.QuadPart <= 0)
		return result;

	const double refreshRate = static_cast<double>(qpcFrequency.QuadPart) /
		static_cast<double>(timing.qpcRefreshPeriod);
	if (refreshRate < 20.0 || refreshRate > 120.0 || timing.qpcVBlank == 0)
		return result;

	result.refreshRateHz = refreshRate;
	if (timing.rateRefresh.uiNumerator > 0 &&
		timing.rateRefresh.uiDenominator > 0)
	{
		result.advertisedRefreshRateHz =
			static_cast<double>(timing.rateRefresh.uiNumerator) /
			static_cast<double>(timing.rateRefresh.uiDenominator);
	}
	result.lastVBlankQpc = timing.qpcVBlank;
	result.refreshPeriodQpc = timing.qpcRefreshPeriod;
	result.qpcFrequency = qpcFrequency.QuadPart;
	return result;
}
}


BEGIN_MESSAGE_MAP(CVideoProcessorDlg, CDialog)

	// Pre-baked callbacks
	ON_WM_PAINT()
	ON_WM_SIZE()
	ON_WM_QUERYDRAGICON()
	ON_WM_GETMINMAXINFO()
	ON_WM_SETFOCUS()
	ON_WM_CLOSE()
	ON_WM_TIMER()
	ON_WM_DISPLAYCHANGE()

	// UI element messages
	ON_CBN_SELCHANGE(IDC_CAPTURE_DEVICE_COMBO, &CVideoProcessorDlg::OnCaptureDeviceSelected)
	ON_CBN_SELCHANGE(IDC_CAPTURE_INPUT_COMBO, &CVideoProcessorDlg::OnCaptureInputSelected)
	ON_BN_CLICKED(IDC_CAPTURE_RESTART_BUTTON, &CVideoProcessorDlg::OnBnClickedCaptureRestart)
	ON_BN_CLICKED(IDC_TIMING_CLOCK_FRAME_OFFSET_AUTO_CHECK, &CVideoProcessorDlg::OnBnClickedTimingClockFrameOffsetAutoCheck)
	ON_EN_CHANGE(IDC_TIMING_CLOCK_FRAME_OFFSET_EDIT, &CVideoProcessorDlg::OnEnChangeTimingClockFrameOffset)
	ON_CBN_SELCHANGE(IDC_COLORSPACE_CONTAINER_COMBO, &CVideoProcessorDlg::OnColorSpaceContainerSelected)
	ON_CBN_SELCHANGE(IDC_HDR_COLORSPACE_COMBO, &CVideoProcessorDlg::OnHdrColorSpaceSelected)
	ON_CBN_SELCHANGE(IDC_HDR_LUMINANCE_COMBO, &CVideoProcessorDlg::OnHdrLuminanceSelected)
	ON_CBN_SELCHANGE(IDC_RENDERER_COMBO, &CVideoProcessorDlg::OnRendererSelected)
	ON_BN_CLICKED(IDC_RENDERER_RESTART_BUTTON, &CVideoProcessorDlg::OnBnClickedRendererRestart)
	ON_CBN_SELCHANGE(IDC_RENDERER_VIDEO_CONVERSION_COMBO, &CVideoProcessorDlg::OnRendererVideoConversionSelected)
	ON_BN_CLICKED(IDC_RENDERER_VIDEO_FRAME_USE_QUEUE_CHECK, &CVideoProcessorDlg::OnBnClickedRendererVideoFrameUseQueueCheck)
	ON_CBN_SELCHANGE(IDC_RENDERER_SCENE_CORRECTION_MODE_COMBO, &CVideoProcessorDlg::OnRendererSceneCorrectionModeSelected)
	ON_BN_CLICKED(IDC_RENDERER_RESET_BUTTON, &CVideoProcessorDlg::OnBnClickedRendererReset)
	ON_BN_CLICKED(IDC_RENDERER_RESET_AUTO_CHECK, &CVideoProcessorDlg::OnBnClickedRendererResetAutoCheck)
	ON_CBN_SELCHANGE(IDC_RENDERER_DIRECTSHOW_START_STOP_TIME_METHOD_COMBO, &CVideoProcessorDlg::OnRendererDirectShowStartStopTimeMethodSelected)
	ON_CBN_SELCHANGE(IDC_RENDERER_DIRECTSHOW_NOMINAL_RANGE_COMBO, &CVideoProcessorDlg::OnRendererDirectShowNominalRangeSelected)
	ON_CBN_SELCHANGE(IDC_RENDERER_DIRECTSHOW_TRANSFER_FUNCTION_COMBO, &CVideoProcessorDlg::OnRendererDirectShowTransferFunctionSelected)
	ON_CBN_SELCHANGE(IDC_RENDERER_DIRECTSHOW_TRANSFER_MATRIX_COMBO, &CVideoProcessorDlg::OnRendererDirectShowTransferMatrixSelected)
	ON_CBN_SELCHANGE(IDC_RENDERER_DIRECTSHOW_PRIMARIES_COMBO, &CVideoProcessorDlg::OnRendererDirectShowPrimariesSelected)
	ON_BN_CLICKED(IDC_RENDERER_FULL_SCREEN_CHECK, &CVideoProcessorDlg::OnBnClickedRendererFullScreenCheck)
	ON_CBN_SELCHANGE(IDC_FULLSCREENMODE_COMBO, &CVideoProcessorDlg::OnCbnSelchangeFullscreenmodeCombo)


	// Custom messages
	ON_MESSAGE(WM_MESSAGE_CAPTURE_DEVICE_FOUND, &CVideoProcessorDlg::OnMessageCaptureDeviceFound)
	ON_MESSAGE(WM_MESSAGE_CAPTURE_DEVICE_LOST, &CVideoProcessorDlg::OnMessageCaptureDeviceLost)
	ON_MESSAGE(WM_MESSAGE_CAPTURE_DEVICE_STATE_CHANGE, &CVideoProcessorDlg::OnMessageCaptureDeviceStateChange)
	ON_MESSAGE(WM_MESSAGE_CAPTURE_DEVICE_CARD_STATE_CHANGE, &CVideoProcessorDlg::OnMessageCaptureDeviceCardStateChange)
	ON_MESSAGE(WM_MESSAGE_CAPTURE_DEVICE_VIDEO_STATE_CHANGE, &CVideoProcessorDlg::OnMessageCaptureDeviceVideoStateChange)
	ON_MESSAGE(WM_MESSAGE_EVALUATE_RENDERER_START, &CVideoProcessorDlg::OnMessageEvaluateRendererStart)
	ON_MESSAGE(WM_MESSAGE_CAPTURE_DEVICE_ERROR, &CVideoProcessorDlg::OnMessageCaptureDeviceError)
	ON_MESSAGE(WM_MESSAGE_DIRECTSHOW_NOTIFICATION, &CVideoProcessorDlg::OnMessageDirectShowNotification)
	ON_MESSAGE(WM_MESSAGE_RENDERER_STATE_CHANGE, &CVideoProcessorDlg::OnMessageRendererStateChange)
	ON_MESSAGE(WM_MESSAGE_RENDERER_DETAIL_STRING, &CVideoProcessorDlg::OnMessageRendererDetailString)
	ON_MESSAGE(WM_MESSAGE_RENDERER_LIVE_FRAME, &CVideoProcessorDlg::OnMessageRendererLiveFrame)
	ON_MESSAGE(WM_MESSAGE_RENDERER_RESET_REQUEST, &CVideoProcessorDlg::OnMessageRendererResetRequest)
	ON_MESSAGE(WM_MESSAGE_RENDERER_RETIRED, &CVideoProcessorDlg::OnMessageRendererRetired)

	// Command handlers (from accelerator)
	ON_COMMAND(ID_COMMAND_FULLSCREEN_TOGGLE, &CVideoProcessorDlg::OnCommandFullScreenToggle)
	ON_COMMAND(ID_COMMAND_FULLSCREEN_EXIT, &CVideoProcessorDlg::OnCommandFullScreenExit)
	ON_COMMAND(ID_COMMAND_RENDERER_RESET, &CVideoProcessorDlg::OnCommandRendererReset)
	ON_COMMAND(ID_COMMAND_RENDERER_RESTART, &CVideoProcessorDlg::OnCommandRendererRestart)

	ON_COMMAND(ID_COMMAND_PQ_SET, &CVideoProcessorDlg::OnCommandPQSet)
	ON_COMMAND(ID_COMMAND_AUTO_SET, &CVideoProcessorDlg::OnCommandAutoSet)

	ON_COMMAND(ID_COMMAND_VC_NONE, &CVideoProcessorDlg::SetVideoConversionOff)
	ON_COMMAND(ID_COMMAND_VC_P010, &CVideoProcessorDlg::SetVideoConversionP010)
	ON_COMMAND(ID_COMMAND_TOGGLE_STATS_OVERLAY, &CVideoProcessorDlg::OnCommandToggleStatsOverlay)
	ON_COMMAND(ID_COMMAND_DISPLAY_RULE_AUTO, &CVideoProcessorDlg::OnCommandDisplayRuleAuto)
	ON_COMMAND(ID_COMMAND_CONFIG_EDITOR, &CVideoProcessorDlg::OnCommandConfigEditor)
	ON_COMMAND_RANGE(ID_COMMAND_SHADER_RULE_FIRST, ID_COMMAND_SHADER_RULE_LAST, &CVideoProcessorDlg::OnCommandShaderRule)
	ON_COMMAND_RANGE(ID_COMMAND_DISPLAY_RULE_FIRST, ID_COMMAND_DISPLAY_RULE_LAST, &CVideoProcessorDlg::OnCommandDisplayRule)
	ON_COMMAND_RANGE(ID_COMMAND_UNIFIED_PROFILE_FIRST, ID_COMMAND_UNIFIED_PROFILE_LAST, &CVideoProcessorDlg::OnCommandDisplayRule)
	ON_COMMAND_RANGE(ID_COMMAND_RENDERER_SELECT_FIRST, ID_COMMAND_RENDERER_SELECT_LAST, &CVideoProcessorDlg::OnCommandRendererSelect)
	ON_COMMAND_RANGE(ID_COMMAND_CAPTURE_1, ID_COMMAND_CAPTURE_4, &CVideoProcessorDlg::OnSelectCaptureDevice)



END_MESSAGE_MAP()


static const std::vector<std::pair<LPCTSTR, ColorSpace>> COLOLORSPACE_CONTAINER_OPTIONS =
{
	std::make_pair(TEXT("Follow input"),              ColorSpace::UNKNOWN),
	std::make_pair(TEXT("Force BT.2020"),             ColorSpace::BT_2020),
	std::make_pair(TEXT("Force P3-D65 (Display)"),    ColorSpace::P3_D65),
	std::make_pair(TEXT("Force P3-DCI (Theater)"),    ColorSpace::P3_DCI),
	std::make_pair(TEXT("Force P3-D60 (ACES)"),       ColorSpace::P3_D60),
	std::make_pair(TEXT("Force REC.709"),             ColorSpace::REC_709),
	std::make_pair(TEXT("Force REC.601 (NTSC)"),      ColorSpace::REC_601_525),
	std::make_pair(TEXT("Force REC.601 (PAL/SECAM)"), ColorSpace::REC_601_625)
};


static const std::vector<std::pair<LPCTSTR, HdrColorspaceOptions>> HDR_COLORSPACE_OPTIONS =
{
	std::make_pair(TEXT("Follow input"),        HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT),
	std::make_pair(TEXT("Follow input (LLDV)"), HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT_LLDV),
	std::make_pair(TEXT("Follow container"),    HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_CONTAINER),
	std::make_pair(TEXT("Force BT.2020"),       HdrColorspaceOptions::HDR_COLORSPACE_BT2020),
	std::make_pair(TEXT("Force P3"),            HdrColorspaceOptions::HDR_COLORSPACE_P3),
	std::make_pair(TEXT("Force REC709"),        HdrColorspaceOptions::HDR_COLORSPACE_REC709)
};


static const std::vector<std::pair<LPCTSTR, HdrLuminanceOptions>> HDR_LUMINANCE_OPTIONS =
{
	std::make_pair(TEXT("Follow input"),        HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT),
	std::make_pair(TEXT("Follow input (LLDV)"), HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT_LLDV),
	std::make_pair(TEXT("user"),                HdrLuminanceOptions::HDR_LUMINANCE_USER)
};


static const std::vector<DirectShowStartStopTimeMethod> RENDERER_DIRECTSHOW_START_STOP_TIME_OPTIONS =
{
	DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART,
	DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2,
	// RATIONAL_RATIONAL uses defined rational frame rates instead of hardware timestamps
	DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL,
	// CLOCK_RATIONAL combines hardware sync with rational duration (new hybrid mode)
	DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL,
	DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO,
	DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK,
	DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO,
	DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE,
	DirectShowStartStopTimeMethod::DS_SSTM_THEO_NONE,
	DirectShowStartStopTimeMethod::DS_SSTM_NONE
};


static const std::vector<std::pair<LPCTSTR, DXVA_NominalRange>> DIRECTSHOW_NOMINAL_RANGE_OPTIONS =
{
	std::make_pair(TEXT("Auto"),             DXVA_NominalRange::DXVA_NominalRange_Unknown),
	std::make_pair(TEXT("Full (0-255)"),     DXVA_NominalRange::DXVA_NominalRange_0_255),
	std::make_pair(TEXT("Limited (16-235)"), DXVA_NominalRange::DXVA_NominalRange_16_235),
	std::make_pair(TEXT("Small (48-208)"),   DXVA_NominalRange::DXVA_NominalRange_48_208)
};


static const std::vector<std::pair<LPCTSTR, DXVA_VideoTransferFunction>> DIRECTSHOW_TRANSFER_FUNCTION_OPTIONS =
{
	std::make_pair(TEXT("Auto"),                      DXVA_VideoTransferFunction::DXVA_VideoTransFunc_Unknown),
	std::make_pair(TEXT("PQ"),                        DIRECTSHOW_VIDEOTRANSFUNC_2084),
	std::make_pair(TEXT("Rec 709 (γ=2.2)"),           DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_709),
	std::make_pair(TEXT("Bt.2020 constant"),          DIRECTSHOW_VIDEOTRANSFUNC_2020_const),

	std::make_pair(TEXT("True gamma 1.8"),            DXVA_VideoTransferFunction::DXVA_VideoTransFunc_18),
	std::make_pair(TEXT("True gamma 2.0"),            DXVA_VideoTransferFunction::DXVA_VideoTransFunc_20),
	std::make_pair(TEXT("True gamma 2.2"),            DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22),
	std::make_pair(TEXT("True gamma 2.6"),            DIRECTSHOW_VIDEOTRANSFUNC_26),
	std::make_pair(TEXT("True gamma 2.8"),            DXVA_VideoTransferFunction::DXVA_VideoTransFunc_28),

	std::make_pair(TEXT("Linear RGB (γ=1.0)"),        DXVA_VideoTransferFunction::DXVA_VideoTransFunc_10),
	std::make_pair(TEXT("204M (γ=2.2)"),              DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_240M),
	std::make_pair(TEXT("8-bit gamma 2.2"),           DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_8bit_sRGB),
	std::make_pair(TEXT("Log 100:1 H.264"),           DIRECTSHOW_VIDEOTRANSFUNC_Log_100),
	std::make_pair(TEXT("Log 316:1 H.264"),           DIRECTSHOW_VIDEOTRANSFUNC_Log_316),
	std::make_pair(TEXT("Rec 709 (γ=2.2) symmetric"), DIRECTSHOW_VIDEOTRANSFUNC_709_sym),
	std::make_pair(TEXT("Bt.2020 non-const"),         DIRECTSHOW_VIDEOTRANSFUNC_2020),
	std::make_pair(TEXT("Hybrid log"),                DIRECTSHOW_VIDEOTRANSFUNC_HLG)
};


static const std::vector<std::pair<LPCTSTR, DXVA_VideoTransferMatrix>> DIRECTSHOW_TRANSFER_MATRIX_OPTIONS =
{
	std::make_pair(TEXT("Auto"),       DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_Unknown),
	std::make_pair(TEXT("BT.2020 10"), DIRECTSHOW_VIDEOTRANSFERMATRIX_BT2020_10),
	std::make_pair(TEXT("BT.2020 12"), DIRECTSHOW_VIDEOTRANSFERMATRIX_BT2020_12),
	std::make_pair(TEXT("BT.709"),     DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_BT709),
	std::make_pair(TEXT("BT.601"),     DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_BT601),
	std::make_pair(TEXT("240M"),       DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_SMPTE240M),
	std::make_pair(TEXT("FCC"),        DIRECTSHOW_VIDEOTRANSFERMATRIX_FCC),
	std::make_pair(TEXT("YCgCo"),      DIRECTSHOW_VIDEOTRANSFERMATRIX_YCgCo)
};


static const std::vector<std::pair<LPCTSTR, DXVA_VideoPrimaries>> DIRECTSHOW_PRIMARIES_OPTIONS =
{
	std::make_pair(TEXT("Auto"),         DXVA_VideoPrimaries::DXVA_VideoPrimaries_Unknown),
	std::make_pair(TEXT("BT.2020"),      DIRECTSHOW_VIDEOPRIMARIES_BT2020),
	std::make_pair(TEXT("DCI-P3"),       DIRECTSHOW_VIDEOPRIMARIES_DCI_P3),
	std::make_pair(TEXT("BT.709"),       DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT709),

	std::make_pair(TEXT("NTSC SysM"),    DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT470_2_SysM),
	std::make_pair(TEXT("NTSC SysBG"),   DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT470_2_SysBG),
	std::make_pair(TEXT("CIE 1931 XYZ"), DIRECTSHOW_VIDEOPRIMARIES_XYZ),
	std::make_pair(TEXT("ACES"),         DIRECTSHOW_VIDEOPRIMARIES_ACES),
};


static const std::vector<VideoConversionOverride> RENDERER_VIDEO_CONVERSION =
{
	VideoConversionOverride::VIDEOCONVERSION_NONE,
	VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010
};

//
// Constructor/destructor
//


CVideoProcessorDlg::CVideoProcessorDlg():
	CDialog(CVideoProcessorDlg::IDD, nullptr)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_livenessWatchdogStopEvent =
		CreateEvent(nullptr, TRUE, FALSE, nullptr);
	m_unifiedActionCancelEvent =
		CreateEvent(nullptr, TRUE, FALSE, nullptr);
	LoadDisplayRefreshRateOverrides();

	ConfigFile profileConfig;
	if (profileConfig.Load())
	{
		std::string profileError;
		if (!m_profileRuntime.Initialize(profileConfig,
			GetUnifiedProfileSourceLookup(), profileError))
		{
			DebugLog::Log("Unified profile runtime disabled: %s",
				profileError.c_str());
		}
		else if (const auto snapshot = m_profileRuntime.GetSnapshot())
		{
			ApplyUnifiedProfileSnapshot(snapshot, false);
			DebugLog::Log(
				"Unified profile runtime restored generation %llu, viewport %s (%s)",
				static_cast<unsigned long long>(snapshot->generation),
				snapshot->viewport.profile.c_str(),
				snapshot->viewport.screenAspect.Canonical().c_str());
		}
	}

	m_blackMagicDeviceDiscoverer = new BlackMagicDeckLinkCaptureDeviceDiscoverer(*this);
	
	// Initialize stats overlay
	m_statsOverlay = new StatsOverlayWindow();
	m_lastStatsData = new StatsData();
}

void CVideoProcessorDlg::LoadDisplayRefreshRateOverrides()
{
	m_displayRefreshRateOverridesHz.clear();

	ConfigFile config;
	if (!config.Load() || !config.HasSection("display_refresh_rate_override"))
		return;

	const auto* values = config.GetSectionValues("display_refresh_rate_override");
	if (!values)
		return;

	for (const auto& setting : *values)
	{
		try
		{
			size_t keyLength = 0;
			const int nominalRate = std::stoi(setting.first, &keyLength);
			if (keyLength != setting.first.length() ||
				nominalRate <= 0 || nominalRate > 1000)
			{
				throw std::runtime_error("out of range");
			}

			if (ConfigFile::NormalizeName(setting.second) == "auto")
			{
				// Match [ppm_correction]: AUTO explicitly selects normal automatic
				// measurement for this nominal rate rather than an override.
				m_displayRefreshRateOverridesHz[nominalRate] = 0.0;
				DebugLog::Log(
					"Display timing override loaded: nominal %d Hz = AUTO (measurement enabled)",
					nominalRate);
				continue;
			}

			size_t valueLength = 0;
			const double overrideRate = std::stod(setting.second, &valueLength);
			if (valueLength != setting.second.length() ||
				!std::isfinite(overrideRate) ||
				overrideRate < 10.0 || overrideRate > 240.0)
			{
				throw std::runtime_error("out of range");
			}

			m_displayRefreshRateOverridesHz[nominalRate] = overrideRate;
			DebugLog::Log(
				"Display timing override loaded: nominal %d Hz = %.6f Hz",
				nominalRate, overrideRate);
		}
		catch (const std::exception&)
		{
			DebugLog::Log(
				"Display timing override ignored: invalid entry %s=%s "
				"(expected nominal integer 1-1000 = rate 10.0-240.0 or AUTO)",
				setting.first.c_str(), setting.second.c_str());
		}
	}
}

bool CVideoProcessorDlg::TryGetDisplayRefreshRateOverride(
	double nominalRateHz, double& overrideRateHz, int& matchedNominalRate) const
{
	overrideRateHz = 0.0;
	matchedNominalRate = 0;
	if (m_displayRefreshRateOverridesHz.empty())
		return false;

	const int truncatedRate = static_cast<int>(nominalRateHz);
	auto overrideIt = m_displayRefreshRateOverridesHz.find(truncatedRate);
	if (overrideIt == m_displayRefreshRateOverridesHz.end())
	{
		constexpr double kRefreshRateToleranceHz = 0.5;
		double bestDistance = kRefreshRateToleranceHz + 1.0;
		for (auto candidate = m_displayRefreshRateOverridesHz.begin();
			candidate != m_displayRefreshRateOverridesHz.end(); ++candidate)
		{
			const double distance = fabs(nominalRateHz - candidate->first);
			if (distance <= kRefreshRateToleranceHz && distance < bestDistance)
			{
				overrideIt = candidate;
				bestDistance = distance;
			}
		}
		if (bestDistance > kRefreshRateToleranceHz)
			return false;
	}

	overrideRateHz = overrideIt->second;
	matchedNominalRate = overrideIt->first;
	return overrideRateHz > 0.0;
}

CVideoProcessorDlg::~CVideoProcessorDlg()
{
	if (m_rendererResetCoordinator)
		m_rendererResetCoordinator->Close();
	m_rendererRetirementService.RequestClose();
	HANDLE retirementWorker =
		m_rendererRetirementService.NativeThreadHandle();
	if (retirementWorker)
	{
		for (;;)
		{
			const DWORD waitResult = MsgWaitForMultipleObjectsEx(
				1, &retirementWorker, INFINITE, QS_SENDMESSAGE,
				MWMO_INPUTAVAILABLE);
			if (waitResult == WAIT_OBJECT_0)
				break;
			if (waitResult != WAIT_OBJECT_0 + 1)
				break;
			MSG message;
			PeekMessage(
				&message, nullptr, WM_NULL, WM_NULL, PM_NOREMOVE);
		}
	}
	m_rendererRetirementService.Join();

	if (m_livenessWatchdogStopEvent)
		SetEvent(m_livenessWatchdogStopEvent);
	if (m_livenessWatchdogThread.joinable())
		m_livenessWatchdogThread.join();
	if (m_livenessWatchdogStopEvent)
	{
		CloseHandle(m_livenessWatchdogStopEvent);
		m_livenessWatchdogStopEvent = nullptr;
	}
	if (m_unifiedActionCancelEvent)
		SetEvent(m_unifiedActionCancelEvent);
	for (std::thread& worker : m_unifiedActionWorkers)
		if (worker.joinable()) worker.join();
	m_unifiedActionWorkers.clear();
	if (m_unifiedActionCancelEvent)
	{
		CloseHandle(m_unifiedActionCancelEvent);
		m_unifiedActionCancelEvent = nullptr;
	}

	if (m_accelerator)
	{
		DestroyAcceleratorTable(m_accelerator);
		m_accelerator = nullptr;
	}

	for (auto& captureDevice : m_captureDevices)
		(*captureDevice).Release();

	// Capture release above prevents new callbacks and lets any final ingress
	// lease drain. Do not destroy callback/UI state while a reset still retains
	// the renderer and may be returning from third-party graph code. Service
	// synchronous renderer window calls while Join waits, but leave posted UI
	// work queued so teardown cannot re-enter the dialog state machine.
	if (m_rendererResetCoordinator)
	{
		HANDLE joinedEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		if (!joinedEvent)
		{
			m_rendererResetCoordinator->Join();
		}
		else
		{
			try
			{
				std::thread joiner(
					[this, joinedEvent]()
					{
						m_rendererResetCoordinator->Join();
						SetEvent(joinedEvent);
					});
				for (;;)
				{
					const DWORD waitResult =
						MsgWaitForMultipleObjectsEx(
							1, &joinedEvent, INFINITE,
							QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
					if (waitResult == WAIT_OBJECT_0)
						break;
					if (waitResult != WAIT_OBJECT_0 + 1)
						break;
					MSG message;
					PeekMessage(
						&message, nullptr, WM_NULL, WM_NULL,
						PM_NOREMOVE);
				}
				joiner.join();
			}
			catch (...)
			{
				m_rendererResetCoordinator->Join();
			}
			CloseHandle(joinedEvent);
		}
	}

	// Clean up stats overlay
	if (m_statsOverlay)
	{
		delete m_statsOverlay;
		m_statsOverlay = nullptr;
	}
	if (m_lastStatsData)
	{
		delete m_lastStatsData;
		m_lastStatsData = nullptr;


	}
}

//
// Option handlers
//


void CVideoProcessorDlg::StartFullScreen(bool enabled)
{
	m_rendererFullScreenStart = enabled;
}

void CVideoProcessorDlg::SetCaptureDevice(const CString& initialCaptureDevice)
{

	m_initialCaptureDevice = initialCaptureDevice;
}

void CVideoProcessorDlg::SetCaptureInput(const CString& initialCaptureInput)
{
	m_initialCaptureInput = initialCaptureInput;
}

void CVideoProcessorDlg::HideUI(bool enabled)
{
	m_hideUI = enabled;
	if (enabled)
		m_rendererFullScreenStart = false;
}

void CVideoProcessorDlg::StartMinimized(bool enabled)
{
	m_startMinimized = enabled;
}

void CVideoProcessorDlg::SceneDetect(bool enabled)
{
	m_sceneAwareTimingCorrection = enabled;
}

void CVideoProcessorDlg::SceneCorrectionUpstreamSample(bool enabled)
{
	m_sceneCorrectionUpstreamSample = enabled;
}

void CVideoProcessorDlg::SubtitleRepositioning(SubtitleRepositionMode mode)
{
	if (mode != SubtitleRepositionMode::DISABLED)
		DebugLog::Log("Subtitle repositioning request ignored; feature is disabled");
	m_subtitleRepositionMode = SubtitleRepositionMode::DISABLED;
}

void CVideoProcessorDlg::EnableNewLldvHeuristic(bool enabled)
{
	m_useNewLldvHeuristic = enabled;
}

void CVideoProcessorDlg::SetLldvMaxCll(double value)
{
	m_lldvMaxCllOverride = value;
}

void CVideoProcessorDlg::SetLldvMaxFall(double value)
{
	m_lldvMaxFallOverride = value;
}

void CVideoProcessorDlg::SetLldvMasteringMinLuminance(double value)
{
	m_lldvMasteringMinLuminanceOverride = value;
}

void CVideoProcessorDlg::SetLldvMasteringMaxLuminance(double value)
{
	m_lldvMasteringMaxLuminanceOverride = value;
}


void CVideoProcessorDlg::WindowedFullScreenMode(bool enabled)
{
	m_windowedFullScreenMode = enabled;
}

void CVideoProcessorDlg::FullscreenMonitorName(const CString& name)
{
	m_fullscreenMonitorName = name;
	m_fullscreenMonitorName.Trim();
	DebugLog::Log(
		"Fullscreen monitor selection configured: requested='%S'",
		m_fullscreenMonitorName.GetString());
}


void CVideoProcessorDlg::DefaultRendererName(const CString& rendererName)
{
	m_defaultRendererName = rendererName;
}


void CVideoProcessorDlg::StartFrameOffsetAuto()
{
	m_frameOffsetAutoStart = true;
}


void CVideoProcessorDlg::StartFrameOffset(const CString& frameOffset)
{
	m_defaultFrameOffset = frameOffset;
	m_directShowFrameOffsetMs = _ttoi(frameOffset);
}

void CVideoProcessorDlg::SetQueueSize(const CString& queueSize)
{
	m_defaultQueueSize = queueSize;
	const int capacity = _ttoi(queueSize);
	if (capacity > 0)
	{
		m_directShowQueueCapacity = static_cast<size_t>(capacity);
		m_profileBaseQueueCapacity = static_cast<size_t>(capacity);
	}
}

void CVideoProcessorDlg::SetQueueResetDelaySeconds(const CString& value)
{
	const int seconds = _ttoi(value);
	if (seconds > 0)
		m_queueResetDelaySeconds = seconds;
}

void CVideoProcessorDlg::SetQueueResetHighWaterPercent(const CString& value)
{
	const int percent = _ttoi(value);
	if (percent > 0 && percent <= 200)
		m_queueResetHighWaterPercent = percent;
}

void CVideoProcessorDlg::DefaultVideoConversionOverride(VideoConversionOverride videoConversionOverride)
{
	m_defaultVideoConversionOverride = videoConversionOverride;
}


void CVideoProcessorDlg::DefaultContainerColorSpace(ColorSpace containerColorSpace)
{
	m_defaultContainerColorSpace = containerColorSpace;
}


void CVideoProcessorDlg::DefaultHDRColorSpace(HdrColorspaceOptions hdrColorSpaceOption)
{
	m_defaultHDRColorSpaceOption = hdrColorSpaceOption;
}


void CVideoProcessorDlg::DefaultHDRLuminance(HdrLuminanceOptions hdrLuminanceOption)
{
	m_defaultHDRLuminanceOption = hdrLuminanceOption;
}

void CVideoProcessorDlg::SetVideoConversionOff()
{
	if (m_rendererVideoConversionCombo.GetCurSel() != 0) {

		m_rendererVideoConversionCombo.SetCurSel(0);
		OnBnClickedCaptureRestart();
	}
}

void CVideoProcessorDlg::SetVideoConversionP010()
{
	if (m_rendererVideoConversionCombo.GetCurSel() != 1) {
		m_rendererVideoConversionCombo.SetCurSel(1);
		OnBnClickedCaptureRestart();
	}
}

void CVideoProcessorDlg::DefaultRendererStartStopTimeMethod(DirectShowStartStopTimeMethod dsssTimeMethod)
{
	m_defaultDSSSTimeMethod = dsssTimeMethod;
}

//				dlg.DefaultRendererStartStopTimeMethod(dsssTimeMethod);


void CVideoProcessorDlg::DefaultRendererNominalRange(DXVA_NominalRange nominalRange)
{
	m_defaultNominalRange = nominalRange;
}


void CVideoProcessorDlg::DefaultRendererTransferFunction(DXVA_VideoTransferFunction transferFunction)
{
	m_defaultTransferFunction = transferFunction;
}


void CVideoProcessorDlg::DefaultRendererTransferMatrix(DXVA_VideoTransferMatrix transferMatrix)
{
	m_defaultTransferMatrix = transferMatrix;
}


void CVideoProcessorDlg::DefaultRendererPrimaries(DXVA_VideoPrimaries primaries)
{
	m_defaultPrimaries = primaries;
}

//
// UI-related handlers
//


void CVideoProcessorDlg::OnCaptureDeviceSelected()


{
	const int captureDeviceIndex = m_captureDeviceCombo.GetCurSel();

	if (captureDeviceIndex < 0)
		return;

	// Find input device based on IDeckLink* object
	for (auto& captureDevice : m_captureDevices)
	{
		if (m_captureDeviceCombo.GetItemDataPtr(captureDeviceIndex) == captureDevice.p)
		{
			m_desiredCaptureDevice = captureDevice;
			break;
		}
	}

	UpdateState();
}


void CVideoProcessorDlg::OnCaptureInputSelected()
{
	assert(m_captureDevice);

	const int captureInputIndex = m_captureInputCombo.GetCurSel();
	if (captureInputIndex < 0)
		return;

	const CaptureInputId selectedCaptureInputId = (CaptureInputId)m_captureInputCombo.GetItemData(captureInputIndex);
	assert(selectedCaptureInputId != INVALID_CAPTURE_INPUT_ID);

	m_desiredCaptureInputId = selectedCaptureInputId;

	UpdateState();
}


void CVideoProcessorDlg::OnBnClickedCaptureRestart()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnBnClickedCaptureRestart()")));

	if (m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_FAILED)
		m_captureDeviceState = CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN;

	m_wantToRestartCapture = true;
	UpdateState();
}


void CVideoProcessorDlg::OnBnClickedTimingClockFrameOffsetAutoCheck()
{
	const bool checked = m_timingClockFrameOffsetAutoCheck.GetCheck();

	m_timingClockFrameOffsetEdit.EnableWindow(!checked);
}


void CVideoProcessorDlg::OnEnChangeTimingClockFrameOffset()
{
	// Some renderer/layout notifications can arrive while this edit has focus.
	// Keep its resource-layout rectangle stable without rewriting its text or
	// disturbing the user's caret.
	RestoreFrameOffsetEditLayout();
}


void CVideoProcessorDlg::OnColorSpaceContainerSelected()
{
	BuildPushRestartVideoState();
}


void CVideoProcessorDlg::OnHdrColorSpaceSelected()
{
	BuildPushRestartVideoState();
}


void CVideoProcessorDlg::OnHdrLuminanceSelected()
{
	const int i = m_hdrLuminanceCombo.GetCurSel();
	const bool enableEdit = ((HdrLuminanceOptions)m_hdrLuminanceCombo.GetItemData(i) == HdrLuminanceOptions::HDR_LUMINANCE_USER);

	m_hdrLuminanceMaxCll.EnableWindow(enableEdit);
	m_hdrLuminanceMaxFall.EnableWindow(enableEdit);
	m_hdrLuminanceMasterMin.EnableWindow(enableEdit);
	m_hdrLuminanceMasterMax.EnableWindow(enableEdit);

	BuildPushRestartVideoState();
}


void CVideoProcessorDlg::OnRendererSelected()
{
	UpdateRendererBackendUi();
	OnBnClickedRendererRestart();
}


void CVideoProcessorDlg::OnBnClickedRendererRestart()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnBnClickedRendererRestart()")));

	if (m_rendererState == RendererState::RENDERSTATE_FAILED)
		m_rendererState = RendererState::RENDERSTATE_UNKNOWN;

	m_postRendererStartRequiresGraph = true;
	m_wantToRestartRenderer = true;
	UpdateState();
}


void CVideoProcessorDlg::OnRendererVideoConversionSelected()
{
	UpdateSceneCorrectionModeUi();
	OnBnClickedRendererRestart();
}


bool CVideoProcessorDlg::IsP010VideoConversionSelected() const
{
	const int selection = m_rendererVideoConversionCombo.GetCurSel();
	return selection >= 0 &&
		static_cast<VideoConversionOverride>(m_rendererVideoConversionCombo.GetItemData(selection)) ==
			VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010;
}

bool CVideoProcessorDlg::IsAlphaRendererSelected() const
{
	const int selection = m_rendererCombo.GetCurSel();
	if (selection < 0)
		return false;

	const RendererId* renderer = reinterpret_cast<const RendererId*>(
		m_rendererCombo.GetItemData(selection));
	return renderer && renderer->backend == RendererBackend::LIBPLACEBO;
}


bool CVideoProcessorDlg::IsUnifiedActionRendererSelected(
	const RendererProfileConfig::Model::EventAction& action) const
{
	if (action.renderer == "*")
		return true;
	if (action.renderer == "vprenderer")
		return IsAlphaRendererSelected();
	const int selection = m_rendererCombo.GetCurSel();
	return selection >= 0 && action.rendererSelectorIndex == selection + 1;
}


void CVideoProcessorDlg::UpdateRendererQueueControl()
{
	if (m_queueRendererSelectionInitialized)
		m_directShowQueueCapacity = std::max<size_t>(1,
			GetRendererVideoFrameQueueSizeMax());

	CString queueText;
	queueText.Format(TEXT("%zu"), m_directShowQueueCapacity);
	m_rendererVideoFrameQueueSizeMaxEdit.SetWindowText(queueText);

	m_queueRendererSelectionInitialized = true;
	DebugLog::Log("Renderer queue control selected: hard capacity=%zu source=queue_size",
		m_directShowQueueCapacity);
}


void CVideoProcessorDlg::UpdateSceneCorrectionModeUi()
{
	const bool p010Selected = IsP010VideoConversionSelected();
	m_rendererSceneCorrectionModeCombo.EnableWindow(p010Selected);

	// Correction method is deliberately not a UI choice.  Alpha has one native
	// method; DirectShow normally uses the advanced upstream-sample method and
	// can opt into Basic only through configuration.
	m_rendererSceneCorrectionModeCombo.SetCurSel(
		m_sceneAwareTimingCorrection ? 1 : 0);
}


void CVideoProcessorDlg::UpdateRendererBackendUi()
{
	UpdateRendererQueueControl();
	bool directShowSelected = true;
	const int selection = m_rendererCombo.GetCurSel();
	if (selection >= 0)
	{
		const RendererId* renderer = reinterpret_cast<const RendererId*>(
			m_rendererCombo.GetItemData(selection));
		directShowSelected =
			renderer && renderer->backend == RendererBackend::DIRECTSHOW;
	}

	// Start/Stop describes DirectShow sample timestamps. Those timestamps pace
	// the downstream DirectShow queue, but do not exist in the in-process
	// renderer, whose FIFO is paced by its D3D swapchain.
	m_rendererDirectShowStartStopTimeMethodCombo.EnableWindow(directShowSelected);
	if (CWnd* label = GetDlgItem(IDC_STATIC_RENDERER_START_STOP_LABEL))
		label->EnableWindow(directShowSelected);
	if (CWnd* group = GetDlgItem(IDC_STATIC_RENDERER_TIMING_GROUP))
		group->EnableWindow(directShowSelected);

	// These controls override metadata attached to DirectShow media samples.
	// Preserve the configured values while disabling controls which cannot
	// affect the in-process renderer.
	m_rendererNominalRangeCombo.EnableWindow(directShowSelected);
	m_rendererTransferFunctionCombo.EnableWindow(directShowSelected);
	m_rendererTransferMatrixCombo.EnableWindow(directShowSelected);
	m_rendererPrimariesCombo.EnableWindow(directShowSelected);
	if (CWnd* group = GetDlgItem(IDC_STATIC_RENDERER_DIRECTSHOW_GROUP))
		group->EnableWindow(directShowSelected);

	const UINT directShowLabels[] = {
		IDC_STATIC_RENDERER_NOMINAL_RANGE_LABEL,
		IDC_STATIC_RENDERER_TRANSFER_FUNCTION_LABEL,
		IDC_STATIC_RENDERER_TRANSFER_MATRIX_LABEL,
		IDC_STATIC_RENDERER_PRIMARIES_LABEL
	};
	for (const UINT controlId : directShowLabels)
	{
		if (CWnd* label = GetDlgItem(controlId))
			label->EnableWindow(directShowSelected);
	}

	UpdateTimingClockFrameOffsetAvailability();
}


void CVideoProcessorDlg::OnBnClickedRendererVideoFrameUseQueueCheck()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnBnClickedRendererVideoFrameUseQueueCheck()")));

	const bool useQueue = m_rendererVideoFrameUseQeueueCheck.GetCheck();

	m_wantToRestartRenderer = true;
	UpdateState();
}


void CVideoProcessorDlg::OnRendererSceneCorrectionModeSelected()
{
	if (!IsP010VideoConversionSelected())
		return;

	const int selection = m_rendererSceneCorrectionModeCombo.GetCurSel();
	m_sceneAwareTimingCorrection = selection != 0;
	if (m_videoRenderer)
	{
		// Scene Detect changes the presentation timestamp generator. Start it on
		// a fresh DirectShow segment instead of switching timestamp domains while
		// madVR still owns queued samples from the previous cadence.
		OnBnClickedRendererRestart();
	}
}


void CVideoProcessorDlg::OnBnClickedRendererReset()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnBnClickedRendererReset()")));
	
	DebugLog::Log("UI: OnBnClickedRendererReset() - button clicked");

	if (!m_videoRenderer)
	{
		DebugLog::Log("UI: OnBnClickedRendererReset() - ERROR: m_videoRenderer is null!");
		return;
	}
	
	DebugLog::Log("UI: OnBnClickedRendererReset() - calling m_videoRenderer->Reset()");

	RequestRendererReset(RendererResetReason::Manual, true, 0);
	
	DebugLog::Log("UI: OnBnClickedRendererReset() - Reset() returned");
}


void CVideoProcessorDlg::OnBnClickedRendererResetAutoCheck()
{
	const bool checked = m_rendererResetAutoCheck.GetCheck();
	DEBUGLOG("Queue pressure auto-reset %s",
		checked ? "enabled" : "disabled");
	if (!checked)
		m_consecutiveFullSeconds = 0;
}


void CVideoProcessorDlg::OnRendererDirectShowStartStopTimeMethodSelected()
{
	UpdateTimingClockFrameOffsetAvailability();
	OnBnClickedRendererRestart();
}


void CVideoProcessorDlg::OnRendererDirectShowNominalRangeSelected()
{
	OnBnClickedRendererRestart();
}


void CVideoProcessorDlg::OnRendererDirectShowTransferFunctionSelected()
{
	OnBnClickedRendererRestart();
}


void CVideoProcessorDlg::OnRendererDirectShowTransferMatrixSelected()
{
	OnBnClickedRendererRestart();
}


void CVideoProcessorDlg::OnRendererDirectShowPrimariesSelected()
{
	OnBnClickedRendererRestart();
}


void CVideoProcessorDlg::OnBnClickedRendererFullScreenCheck()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnBnClickedRendererFullScreenCheck()")));

	if (m_fullscreenRetargetPending)
	{
		// Keyboard/API commands can still change the checkbox while the UI
		// control is disabled. Do not interrupt madVR's active graph retarget;
		// the frame-ready boundary consumes this final intent below.
		const bool desiredFullscreen =
			m_rendererFullscreenCheck.GetCheck() != FALSE;
		const bool requiresCoveredRebuild =
			FullscreenRetargetRequiresCoveredRebuild(
				m_fullscreenRetargetExiting, desiredFullscreen);
		DebugLog::Log(
			"Fullscreen retarget intent coalesced: active_direction=%s "
			"desired_fullscreen=%d action=%s",
			m_fullscreenRetargetExiting ? "exit" : "enter",
			desiredFullscreen ? 1 : 0,
			requiresCoveredRebuild ?
				"defer-covered-rebuild" : "continue-active-retarget");
		return;
	}

	if (TryStartFullscreenRetarget())
		return;
	if (!m_rendererFullscreenCheck.GetCheck() && !m_fullScreenVideoWindow)
		videoProcessorApp.RestoreDisplayTopology("fullscreen-off");
	if (m_videoRenderer && !m_activeRendererIsDirectShow)
	{
		// Alpha reconstructs its own swapchain for an HWND/fullscreen change.
		// Coalesce rapid toggles into the final renderer start. The replacement
		// owns a fresh queue, so the host boundary is diagnostic rather than a
		// reason for a second delayed queue generation.
		m_alphaHostTransitionPending = true;
		DebugLog::Log(
			"Alpha fullscreen host transition requested: state=fresh-start-pending");
	}

	m_postRendererStartRequiresGraph = true;
	m_wantToRestartRenderer = true;
	UpdateState();
}

void CVideoProcessorDlg::OnCbnSelchangeFullscreenmodeCombo()
{
	int p = m_fullScreenModeCombo.GetCurSel();
	if (p == 0)
		m_windowedFullScreenMode = false;
	if (p == 1)
		m_windowedFullScreenMode = true;

	if (m_fullScreenVideoWindow)
	{
		if (m_videoRenderer && !m_activeRendererIsDirectShow)
		{
			m_alphaHostTransitionPending = true;
			DebugLog::Log(
				"Alpha fullscreen presentation-mode transition requested: "
				"state=fresh-start-pending");
		}
		// The current or pending DirectShow graph can still own this HWND.
		// Recreate it only after renderer teardown has reached a terminal point.
		m_fullscreenModeChangePending = true;
		m_postRendererStartRequiresGraph = true;
		m_wantToRestartRenderer = true;
		UpdateState();
	}

}

//
// Custom message handlers
//


LRESULT CVideoProcessorDlg::OnMessageCaptureDeviceFound(WPARAM wParam, LPARAM lParam)
{
	ACaptureDeviceComPtr captureDevice;
	captureDevice.Attach((ACaptureDevice*)wParam);

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceFound(): %s"), captureDevice->GetName()));

	m_captureDevices.insert(captureDevice);

	RefreshCaptureDeviceList();

	return 0;
}


LRESULT	CVideoProcessorDlg::OnMessageCaptureDeviceLost(WPARAM wParam, LPARAM lParam)
{
	ACaptureDeviceComPtr captureDevice;
	captureDevice.Attach((ACaptureDevice*)wParam);

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceLost(): %s"), captureDevice->GetName()));

	auto it = m_captureDevices.find(captureDevice);
	if (it == m_captureDevices.end())
		FatalError(TEXT("Cannot find capture device to remove"));

	// Device being removed is the one we're using, let's stop using it
	if (m_captureDevice == captureDevice)
	{
		m_desiredCaptureDevice = nullptr;
		UpdateState();
	}

	m_captureDevices.erase(it);

	RefreshCaptureDeviceList();

	return 0;
}


LRESULT	CVideoProcessorDlg::OnMessageCaptureDeviceStateChange(WPARAM wParam, LPARAM lParam)
{
	const CaptureDeviceState newState = (CaptureDeviceState)wParam;

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceStateChange(): %s->%s"),
		ToString(m_captureDeviceState), ToString(newState)));

	if (!m_captureDevice)
		return 0;

	assert(newState != m_captureDeviceState);
	m_captureDeviceState = newState;

	bool enableButtons = false;

	switch (newState)
	{
	case CaptureDeviceState::CAPTUREDEVICESTATE_READY:
		m_captureDeviceStateText.SetWindowText(TEXT("Ready"));
		break;

	case CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING:
		m_captureDeviceStateText.SetWindowText(TEXT("Capturing"));
		m_timingClockDescriptionText.SetWindowText(m_captureDevice->GetTimingClock()->TimingClockDescription());
		enableButtons = true;
		break;

	default:
		assert(false);
	}

	m_captureDeviceRestartButton.EnableWindow(enableButtons);

	UpdateState();

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceStateChange(): Done")));
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageCaptureDeviceCardStateChange(WPARAM wParam, LPARAM lParam)
{
	CaptureDeviceCardStateComPtr cardState;
	cardState.Attach((CaptureDeviceCardState*)wParam);
	assert(cardState);

	DbgLog((LOG_TRACE, 1,
		TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceCardStateChange(): Locked=%s, DisplayMode=%s"),
		ToString(cardState->inputLocked),
		cardState->inputDisplayMode ? cardState->inputDisplayMode->ToString() : TEXT("")
		));

	// Input fields
	m_inputLockedText.SetWindowText(ToString(cardState->inputLocked));

	if (cardState->inputDisplayMode)
		m_inputDisplayModeText.SetWindowText(cardState->inputDisplayMode->ToString());
	else
		m_inputDisplayModeText.SetWindowText(TEXT(""));

	if (cardState->inputEncoding != ColorFormat::UNKNOWN)
		m_inputEncodingText.SetWindowText(ToString(cardState->inputEncoding));
	else
		m_inputEncodingText.SetWindowText(TEXT(""));

	if (cardState->inputBitDepth != BitDepth::UNKNOWN)
		m_inputBitDepthText.SetWindowText(ToString(cardState->inputBitDepth));
	else
		m_inputBitDepthText.SetWindowText(TEXT(""));

	// Other
	m_captureDeviceOtherList.ResetContent();
	for (auto& str : cardState->other)
	{
		m_captureDeviceOtherList.AddString(str);
	}

	UpdateState();

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceCardStateChange(): Done")));
	return 0;
}

LRESULT CVideoProcessorDlg::OnMessageCaptureDeviceVideoStateChange(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<CaptureVideoStateNotification> notification(
		reinterpret_cast<CaptureVideoStateNotification*>(wParam));
	if (!notification || !notification->state)
		return 0;
	VideoStateComPtr videoState = notification->state;
	const uint64_t captureEpoch = notification->captureEpoch;
	const uint64_t notificationSequence = notification->sequence;
	{
		std::lock_guard<std::mutex> sourceLock(
			m_captureVideoStateNotificationMutex);
		if (!m_captureDevice ||
			notification->source.p != m_captureVideoStateSource ||
			captureEpoch != m_captureVideoStateSourceEpoch)
		{
			DebugLog::Log(
				"Capture video-state notification ignored for stale "
				"capture run: sequence=%llu epoch=%llu source=%p "
				"current_epoch=%llu current_source=%p",
				static_cast<unsigned long long>(notificationSequence),
				static_cast<unsigned long long>(captureEpoch),
				notification->source.p,
				static_cast<unsigned long long>(
					m_captureVideoStateSourceEpoch),
				m_captureVideoStateSource);
			return 0;
		}
	}
	if (!notification->source.IsEqualObject(m_captureDevice))
	{
		DebugLog::Log(
			"Capture video-state notification ignored for stale device: "
			"sequence=%llu source=%p current=%p",
			static_cast<unsigned long long>(notificationSequence),
			notification->source.p, m_captureDevice.p);
		return 0;
	}
	const uint64_t latestNotificationSequence =
		m_rendererIngressState->LatestCaptureSequence();
	if (notificationSequence != latestNotificationSequence)
	{
		DebugLog::Log(
			"Capture video-state notification superseded before UI handling: "
			"sequence=%llu latest=%llu valid=%d action=ignore",
			static_cast<unsigned long long>(notificationSequence),
			static_cast<unsigned long long>(latestNotificationSequence),
			videoState->valid ? 1 : 0);
		return 0;
	}
	m_appliedCaptureVideoStateNotificationSequence =
		notificationSequence;

	// A valid notification resolves any previously deferred invalid state before
	// it can stop the renderer.  DeckLink can transiently publish UNKNOWN video
	// state while its live samples continue, notably after a profile refresh.
	if (videoState->valid && m_deferredInvalidCaptureVideoState)
	{
		KillTimer(TRANSIENT_INVALID_VIDEO_STATE_TIMER_ID);
		m_deferredInvalidCaptureVideoState.Release();
		m_deferredInvalidCaptureVideoStateDeadlineTick = 0;
		m_deferredInvalidCaptureVideoStateFrameCount = 0;
		DebugLog::Log(
			"Transient invalid capture video state cleared by valid notification: "
			"sequence=%llu",
			static_cast<unsigned long long>(notificationSequence));
	}

	// Do not immediately tear down a running renderer on a single invalid state
	// notification.  A sustained loss still follows the ordinary invalid-signal
	// stop path after this bounded grace period; a real valid update cancels it.
	if (!videoState->valid &&
		m_videoRenderer &&
		m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		m_captureDeviceVideoState &&
		m_captureDeviceVideoState->valid)
	{
		constexpr UINT transientInvalidGraceMs = 1500;
		m_deferredInvalidCaptureVideoState = videoState;
		m_deferredInvalidCaptureVideoStateDeadlineTick =
			GetTickCount64() + transientInvalidGraceMs;
		m_deferredInvalidCaptureVideoStateFrameCount = m_captureDevice ?
			m_captureDevice->VideoFrameCapturedCount() : 0;
		SetTimer(
			TRANSIENT_INVALID_VIDEO_STATE_TIMER_ID,
			transientInvalidGraceMs,
			nullptr);
		const RendererIngressState::CaptureSequenceSnapshot ingress =
			m_rendererIngressState->CaptureSequences();
		DebugLog::Log(
			"Transient invalid capture video state deferred: sequence=%llu "
			"grace_ms=%u captured_frames=%llu action=retain-last-valid-state "
			"ingress=%s publication_us=%llu "
			"published=%llu required=%llu acknowledged=%llu admitted=%d",
			static_cast<unsigned long long>(notificationSequence),
			transientInvalidGraceMs,
			static_cast<unsigned long long>(
				m_deferredInvalidCaptureVideoStateFrameCount),
			notification->retainedRendererIngress ?
				"retained-at-source" : "awaiting-renderer-acknowledgement",
			static_cast<unsigned long long>(
				notification->ingressPublicationUs),
			static_cast<unsigned long long>(ingress.published),
			static_cast<unsigned long long>(ingress.required),
			static_cast<unsigned long long>(ingress.acknowledged),
			ingress.admissionOpen ? 1 : 0);
		return 0;
	}

	DbgLog((LOG_TRACE, 1,
		TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceVideoStateChange(): Valid=%s"),
		videoState->valid ? TEXT("Yes") : TEXT("No")));

	assert(videoState);
	assert(m_captureDevice);

	const bool wasNewLldvEffective =
		m_useNewLldvHeuristic &&
		m_newLldvCandidateConfirmed &&
		IsNewLldvModeSelected();

	m_captureDeviceVideoState = videoState;
	const bool newLldvJustConfirmed = UpdateNewLldvCandidate();
	const bool newLldvWasCleared =
		wasNewLldvEffective && !m_newLldvCandidateConfirmed;
	const bool newLldvJustEndedInSdr =
		newLldvWasCleared && videoState->valid && videoState->eotf == EOTF::SDR;
	const bool promotionRestartWasPending =
		newLldvWasCleared &&
		(m_lldvRestartPending || m_lldvChangeRestartDelaySeconds >= 0);

	// A source can return to its menu while the delayed promotion restart is
	// still pending.  Do not let that stale timer rebuild the graph as PQ after
	// the candidate has already disappeared.
	if (newLldvWasCleared)
	{
		KillTimer(LLDV_CHANGE_RESTART_TIMER_ID);
		m_lldvChangeRestartDelaySeconds = -1;
		m_lldvRestartPending = false;

		if (promotionRestartWasPending)
		{
			DebugLog::Log(
				"New LLDV promotion cancelled: candidate ended before renderer restart");
		}
	}

	// Reset refresh rate tracking on video state change to prevent false positive detection
	m_lastKnownRefreshRate = 0.0;
	m_resyncPendingResetSeconds = -1;
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceVideoStateChange(): Reset refresh rate tracking")));

	// EOTF CHANGE DETECTION: Check if EOTF changed during active stream (e.g., SDR↔HDR switching)
	// Only check if renderer is actively rendering and feature is enabled
	if (m_enableEotfChangeRestart &&
		m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		(!m_videoRenderer || !m_videoRenderer->SupportsDynamicVideoState()) &&
		videoState->valid &&
		m_eotfChangeRestartCooldownSeconds < 0)  // Not in cooldown period
	{
		// Initialize on first valid state
		if (m_lastKnownEotf == EOTF::UNKNOWN && videoState->eotf != EOTF:: UNKNOWN)
		{
			m_lastKnownEotf = videoState->eotf;
			DbgLog((LOG_TRACE, 1, TEXT("VideoStateChange: Initialized EOTF tracking to %s"),
				ToString(videoState->eotf)));
		}
		// Detect actual EOTF change (not initialization)
		else if (m_lastKnownEotf != EOTF::UNKNOWN &&
			videoState->eotf != EOTF::UNKNOWN &&
			m_lastKnownEotf != videoState->eotf)
		{
			DbgLog((LOG_TRACE, 1, TEXT("VideoStateChange: EOTF changed %s -> %s - scheduling renderer restart in 3 seconds"),
				ToString(m_lastKnownEotf), ToString(videoState->eotf)));

			DebugLog::Log("EOTF change detected: %s -> %s - renderer restart in 3 seconds",
				CStringA(ToString(m_lastKnownEotf)).GetString(),
				CStringA(ToString(videoState->eotf)).GetString());

			// Update tracked EOTF immediately to prevent re-triggering
			m_lastKnownEotf = videoState->eotf;

			// Schedule restart with 3-second delay to allow signal to stabilize
			m_eotfChangeRestartCooldownSeconds = 5;
			SetTimer(EOTF_CHANGE_RESTART_TIMER_ID, 1000, nullptr);  // 1-second tick
		}
	}

	const bool rendererAcceptedState = BuildPushVideoState();
	if (rendererAcceptedState &&
		videoState->valid &&
		m_videoRenderer &&
		m_rendererState == RendererState::RENDERSTATE_RENDERING)
	{
		m_rendererCaptureVideoStateNotificationSequence =
			notificationSequence;
		m_rendererIngressState->AcknowledgeCaptureSequence(
			notificationSequence);
	}

	// If the renderer did not accept the new state we need to restart the renderer
	if (!rendererAcceptedState)
	{
		if (!videoState->valid)
		{
			// UpdateState() stops the graph for an invalid signal.  Do not leave a
			// restart request behind for when capture becomes valid again.
			DbgLog((LOG_TRACE, 1,
				TEXT("Renderer rejected invalid video state; existing invalid-signal stop path will handle it")));
			DebugLog::Log(
				"Renderer video-state update rejected: signal invalid; action=stop renderer");
		}
		else if (newLldvJustConfirmed && IsNewLldvModeSelected())
		{
			// BT.2020/SDR LLDV changes the effective output to PQ while the raw
			// DeckLink EOTF remains SDR.  Use the stabilized LLDV restart path.
			ScheduleNewLldvRendererRestart();
		}
		else if (newLldvJustEndedInSdr)
		{
			if (promotionRestartWasPending)
			{
				DbgLog((LOG_TRACE, 1,
					TEXT("New LLDV heuristic: promotion cancelled; restarting renderer for rejected menu state")));
				DebugLog::Log(
					"New LLDV exit: promotion cancelled before PQ graph restart; raw state %s / %s; restarting renderer for rejected menu state",
					CStringA(ToString(videoState->eotf)).GetString(),
					CStringA(ToString(videoState->colorspace)).GetString());
			}
			else
			{
				DbgLog((LOG_TRACE, 1,
					TEXT("New LLDV heuristic: effective PQ ended; restarting renderer to clear HDR state")));
				DebugLog::Log(
					"New LLDV exit: effective PQ -> SDR, raw state %s / %s; restarting renderer to clear madVR HDR state",
					CStringA(ToString(videoState->eotf)).GetString(),
					CStringA(ToString(videoState->colorspace)).GetString());
			}
			m_wantToRestartRenderer = true;
		}
		else if (m_lldvRestartPending || m_lldvChangeRestartDelaySeconds >= 0)
		{
			// Additional state notifications must not bypass the stabilized LLDV
			// promotion path while its restart is already queued.
			DbgLog((LOG_TRACE, 1,
				TEXT("Renderer rejected pending LLDV PQ state; keeping scheduled promotion restart")));
			DebugLog::Log(
				"Renderer video-state update rejected during pending LLDV promotion; action=keep scheduled PQ restart");
		}
		else if (m_eotfChangeRestartCooldownSeconds >= 0)
		{
			// The raw EOTF path has already scheduled its stabilization restart.
			// Preserve that delay instead of performing a second immediate restart.
			DbgLog((LOG_TRACE, 1,
				TEXT("Renderer rejected video state during raw EOTF transition; using pending stabilization restart")));
			DebugLog::Log(
				"Renderer video-state update rejected during raw EOTF transition; action=deferred stabilization restart");
		}
		else
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("Capture video state update was rejected by renderer; restarting renderer")));
			DebugLog::Log(
				"Renderer rejected capture video state update; action=restart renderer");
			m_wantToRestartRenderer = true;
		}
	}
	// Note: Automatic reset for signal changes is now handled at the lower level
	// by CBufferedLiveSourceVideoOutputPin detecting frame counter changes

	// New round, new chances, reset state here
	if (m_rendererState == RendererState::RENDERSTATE_FAILED)
	{
		m_rendererState = RendererState::RENDERSTATE_UNKNOWN;
	}

	if (!m_videoRenderer && videoState->valid)
	{
		if (!m_rendererStartEvaluationPosted)
		{
			m_rendererStartEvaluationPosted = true;
			if (!PostMessage(WM_MESSAGE_EVALUATE_RENDERER_START, 0, 0))
			{
				m_rendererStartEvaluationPosted = false;
				DebugLog::Log(
					"Capture video-state start evaluation post failed: "
					"sequence=%llu",
					static_cast<unsigned long long>(
						notificationSequence));
				UpdateState();
			}
		}
	}
	else
	{
		UpdateState();
	}

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceVideoStateChange(): Done")));
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageEvaluateRendererStart(
	WPARAM,
	LPARAM)
{
	m_rendererStartEvaluationPosted = false;
	const uint64_t latestNotificationSequence =
		m_rendererIngressState->LatestCaptureSequence();
	if (m_appliedCaptureVideoStateNotificationSequence !=
		latestNotificationSequence)
	{
		DebugLog::Log(
			"Renderer start evaluation superseded: applied_sequence=%llu "
			"latest_sequence=%llu action=wait-for-latest-state",
			static_cast<unsigned long long>(
				m_appliedCaptureVideoStateNotificationSequence),
			static_cast<unsigned long long>(
				latestNotificationSequence));
		return 0;
	}
	UpdateState();
	return 0;
}



LRESULT CVideoProcessorDlg::OnMessageCaptureDeviceError(WPARAM wParam, LPARAM lParam)
{
	CString error(*(CString*)wParam);
	delete (CString*)wParam;

	// TODO: DO something with the error
	::MessageBox(nullptr, error, TEXT("Capture error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

	return 0;
}
LRESULT CVideoProcessorDlg::OnMessageDirectShowNotification(WPARAM wParam, LPARAM lParam)
{
	const std::shared_ptr<IVideoRenderer> renderer =
		std::atomic_load_explicit(
			&m_videoRenderer, std::memory_order_acquire);
	if (renderer)
	{
		// Enhanced DirectShow event handling for MadVR changes
		// We'll intercept events before passing them to the renderer to detect important changes

		// First call the renderer to process DirectShow events and get any graph events
		HRESULT hr = renderer->OnWindowsEvent(wParam, lParam);

		// Now check for specific DirectShow graph events that indicate MadVR changes
		// These events are typically sent via the DirectShow event system
		if (SUCCEEDED(hr))
		{
			// Try to detect specific DirectShow events that affect MadVR
			// Note: The exact event codes depend on DirectShow implementation
			// We'll add logging to detect what events we're getting

			DbgLog((LOG_TRACE, 2, TEXT("DirectShow notification received - wParam: 0x%08X, lParam: 0x%08X"),
				(DWORD)wParam, (DWORD)lParam));

			// Check for common DirectShow events that might indicate renderer changes
			// EC_DISPLAY_CHANGED = 0x16, EC_WINDOW_DESTROYED = 0x11, etc.
			long eventCode = (long)wParam;

			switch (eventCode)
			{
		case 0x16: // EC_DISPLAY_CHANGED
		case 0x0E: // EC_VIDEO_SIZE_CHANGED
		{
				if (!m_outputReadinessGraphReprimeActive)
				{
					g_displayRefreshRateSampler->ResetMeasurement();
				}
				else
				{
					DebugLog::Log(
						"DirectShow display event belongs to the output-readiness "
						"graph re-prime; preserving its selecting DXGI evidence");
				}
				const ULONGLONG now = GetTickCount64();
				if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
					(!m_rendererResetCoordinator ||
						!m_rendererResetCoordinator->
							GetDiagnostics().hasPending) &&
					now >= m_queueResetIgnoreEventsUntil)
				{
					RequestRendererReset(RendererResetReason::DisplayTransition, true,
						static_cast<UINT>(m_queueResetDelaySeconds * 1000));
				}
				else
				{
					DbgLog((LOG_TRACE, 1, TEXT("DirectShow display transition - queue re-prime already pending or suppressed")));
				}
				break;
			}

			case 0x11: // EC_WINDOW_DESTROYED  
				DbgLog((LOG_TRACE, 1, TEXT("EC_WINDOW_DESTROYED detected - MadVR window change")));
				break;

			case 0x12: // EC_QUALITY_CHANGE
				DbgLog((LOG_TRACE, 1, TEXT("EC_QUALITY_CHANGE detected - potential MadVR quality adjustment")));
				// Don't reset for quality changes, but log them for diagnostics
				break;

			case 0x0D: // EC_REPAINT
				// Very common, don't log at normal trace level
				break;

			default:
				// Log unknown events for debugging
				if (eventCode > 0 && eventCode < 0x50) // DirectShow event code range
				{
					DbgLog((LOG_TRACE, 2, TEXT("Unknown DirectShow event code: 0x%02X"), eventCode));
				}
				break;
			}
		}

		if (FAILED(hr))
			FatalError(TEXT("Failed to handle windows event in renderer"));
	}

	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageRendererStateChange(WPARAM wParam, LPARAM lParam)
{
	const RendererState newRendererState = (RendererState)wParam;

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageRendererStateChange(): %s->%s"),
		ToString(m_rendererState), ToString(newRendererState)));

	if (m_rendererState == RendererState::RENDERSTATE_STOPPING && newRendererState == RendererState::RENDERSTATE_READY)
	{
		int a = 1;
	}


	assert(m_videoRenderer);

	bool enableButtons = false;

	assert(newRendererState != RendererState::RENDERSTATE_UNKNOWN);
	assert(m_rendererState != newRendererState);
	const RendererState oldRendererState = m_rendererState;
	m_rendererState = newRendererState;

	switch (newRendererState)
	{
	// Renderer ready, can be started if wanted
	case RendererState::RENDERSTATE_READY:

		
		assert(oldRendererState == RendererState::RENDERSTATE_STARTING);

		m_restartQueuedBecauseEotf = false;

		m_rendererStateText.SetWindowText(TEXT("Ready"));
		if (m_profileRuntime.IsInitialized())
		{
			const auto snapshot = m_profileRuntime.GetSnapshot();
			PublishUnifiedProfileEvent("renderer.ready", "renderer_ready",
				nullptr, snapshot);
		}
		break;

	// Renderer running, ready for frames
	case RendererState::RENDERSTATE_RENDERING:
	{


		assert(oldRendererState == RendererState::RENDERSTATE_READY);

		m_restartQueuedBecauseEotf = false;
		const uint64_t latestCaptureSequence =
			m_rendererIngressState->LatestCaptureSequence();
		if (m_rendererCaptureVideoStateNotificationSequence == 0 ||
			m_rendererCaptureVideoStateNotificationSequence !=
				latestCaptureSequence ||
			m_rendererCaptureVideoStateNotificationSequence !=
				m_appliedCaptureVideoStateNotificationSequence)
		{
			DebugLog::Log(
				"Renderer reached running with superseded capture state: "
				"renderer_sequence=%llu applied_sequence=%llu "
				"latest_sequence=%llu action=covered-retire",
				static_cast<unsigned long long>(
					m_rendererCaptureVideoStateNotificationSequence),
				static_cast<unsigned long long>(
					m_appliedCaptureVideoStateNotificationSequence),
				static_cast<unsigned long long>(
					latestCaptureSequence));
			m_postRendererStartRequiresGraph = true;
			m_wantToRestartRenderer = true;
			break;
		}
		// EOTF TRACKING: Store the EOTF the renderer was started with
		if (m_captureDeviceVideoState && m_captureDeviceVideoState->valid)
		{
			m_rendererStartedWithEotf = m_captureDeviceVideoState->eotf;
			m_lastKnownEotf = m_captureDeviceVideoState->eotf;  // Sync to prevent false detection
			m_eotfCheckCooldownSeconds = 5;  // Wait 5 seconds before checking for changes
			DbgLog((LOG_TRACE, 1, TEXT("Renderer started with EOTF: %s, will check for changes in 5 seconds"), ToString(m_rendererStartedWithEotf)));
		}
		else
		{
			m_rendererStartedWithEotf = EOTF::UNKNOWN;
			m_eotfCheckCooldownSeconds = 0;
		}

		if (m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING &&
			m_captureDevice)
		{
			m_rendererStartCapturedFrameCount =
				m_captureDevice->VideoFrameCapturedCount();
			m_rendererFrameBaselineValid = true;
			DbgLog((LOG_TRACE, 1,
				TEXT("OSD VFrames: renderer-start baseline set to %llu captured frames"),
				m_rendererStartCapturedFrameCount));
		}
		else
		{
			m_rendererStartCapturedFrameCount = 0;
			m_rendererFrameBaselineValid = false;
		}
		ResumeRendererIngress();
		enableButtons = true;
		m_rendererTransitionWindow.KeepOnTop();
		m_rendererStateText.SetWindowText(TEXT("Rendering"));
		ApplyStatsOverlayForActiveRenderer();

		m_rendererStartTime = GetTickCount();
		const bool settlingDisplayTransition =
			m_displayTransitionAwaitingRenderer;
		m_displayTransitionAwaitingRenderer = false;
		const UINT windowSettleDelayMs =
			settlingDisplayTransition ? 2000 : 0;
		m_queueResetIgnoreEventsUntil =
			GetTickCount64() + windowSettleDelayMs + 10000;
		g_displayRefreshRateSampler->ResetMeasurement();
		// A newly constructed Alpha queue/swapchain is already clean and can
		// reveal on its first verified submit. DirectShow retains the proven
		// stop/reset/run re-prime after its full configured settling delay.
		const bool postStartRequiresGraph =
			m_postRendererStartRequiresGraph;
		m_postRendererStartRequiresGraph = true;
		if (m_activeRendererIsDirectShow)
		{
			// DirectShow now starts provisionally, then uses validated DXGI vblank
			// evidence to make one serialized LiveQueue reset and exact VP prefill.
			// Do not stack the legacy configured-delay reset behind it: that was the
			// source of display-handshake-dependent queue depth.
			DebugLog::Log(
				"Post-start reset deferred: renderer=%S backend=DirectShow "
				"legacy_requires_graph=%d display_settle=%u; "
				"awaiting output-readiness evidence",
				static_cast<LPCTSTR>(m_activeRendererName),
				postStartRequiresGraph ? 1 : 0, windowSettleDelayMs);
		}
		else
		{
			const bool refreshTransition = m_alphaRefreshTransitionPending;
			const bool hostTransition = m_alphaHostTransitionPending;
			const bool backendHandoff = m_alphaBackendHandoffPending;
			const double previousRate = m_alphaRefreshTransitionPreviousRateHz;
			const double currentRate = m_alphaRefreshTransitionCurrentRateHz;
			m_alphaRefreshTransitionPending = false;
			m_alphaHostTransitionPending = false;
			m_alphaBackendHandoffPending = false;
			const AlphaFreshStartTransition freshStartTransition =
				refreshTransition ? AlphaFreshStartTransition::RefreshTransition :
				hostTransition ? AlphaFreshStartTransition::HostTransition :
				backendHandoff ? AlphaFreshStartTransition::BackendHandoff :
				AlphaFreshStartTransition::None;

			if (AlphaFreshStartRequiresDelayedReprime(freshStartTransition))
			{
				// A real cross-family refresh change is asynchronous outside Alpha.
				// Preserve its one delayed queue-only cleanup so transition-era
				// frames cannot survive the Windows/DXGI settling boundary.
				RequestRendererReset(
					RendererResetReason::RefreshTransition,
					false,
					static_cast<UINT>(m_queueResetDelaySeconds * 1000));
				DebugLog::Log(
					"Alpha refresh transition re-prime armed: previous=%.6fHz "
					"configured=%.6fHz delay=%d seconds action=queue-only "
					"coalesced_host=%d coalesced_backend_handoff=%d",
					previousRate,
					currentRate,
					m_queueResetDelaySeconds,
					hostTransition ? 1 : 0,
					backendHandoff ? 1 : 0);
			}
			else if (windowSettleDelayMs != 0)
			{
				// A real display-mode transition still needs its bounded hardware
				// settle interval even though the Alpha queue itself is fresh.
				RequestRendererReset(
					RendererResetReason::PostRendererStart,
					false, windowSettleDelayMs);
				DebugLog::Log(
					"Post-start reset retained: renderer=%S backend=Alpha "
					"reason=display-settle delay=%u",
					static_cast<LPCTSTR>(m_activeRendererName),
					windowSettleDelayMs);
			}
			else if (m_rendererResetTransitionActive)
			{
				// A replacement created inside an already-covered reset must still
				// acknowledge the rebound target and advance the transition model.
				// Its fresh queue needs no settling delay.
				RequestRendererReset(
					RendererResetReason::PostRendererStart,
					false, 0);
				DebugLog::Log(
					"Post-start reset retained: renderer=%S backend=Alpha "
					"reason=covered-transition-rebind delay=0",
					static_cast<LPCTSTR>(m_activeRendererName));
			}
			else
			{
				const char* transitionName =
					freshStartTransition == AlphaFreshStartTransition::HostTransition ?
						"host-transition" :
					freshStartTransition == AlphaFreshStartTransition::BackendHandoff ?
						"backend-handoff" : "routine-start";
				DebugLog::Log(
					"Post-start reset skipped: renderer=%S backend=Alpha "
					"reason=fresh-queue-and-swapchain transition=%s "
					"action=reveal-on-first-live-frame",
					static_cast<LPCTSTR>(m_activeRendererName),
					transitionName);
			}
		}
		if (m_rendererFullscreenCheck.GetCheck())
		{
			HWND renderWindow = m_rendererTargetHwnd;
			RECT rect = {};
			if (renderWindow)
				::GetWindowRect(renderWindow, &rect);
			DEBUGLOG(
				"Fullscreen host after renderer start hwnd=%p visible=%d "
				"rect=%ld,%ld-%ld,%ld display_settle_ms=%u",
				renderWindow, ::IsWindowVisible(renderWindow) ? 1 : 0,
				rect.left, rect.top, rect.right, rect.bottom,
				windowSettleDelayMs);
		}
		// LLDV can be confirmed while the graph is still starting. The
		// effective PQ state has already been rebuilt, but this graph accepted
		// the original SDR state, so restart once it is fully running.
		if (m_lldvRestartPending)
		{
			m_lldvRestartPending = false;
			m_wantToRestartRenderer = true;
			DbgLog((LOG_TRACE, 1,
				TEXT("LLDV confirmed during renderer startup - scheduling renderer restart")));
		}
		break;
	}

	// Stopped rendering, can be cleaned up
	case RendererState::RENDERSTATE_STOPPED:

		
		assert(oldRendererState == RendererState::RENDERSTATE_STOPPING);

		m_restartQueuedBecauseEotf = false;

		RenderRemove();
		RenderGUIClear();
		m_rendererStateText.SetWindowText(TEXT(""));
		break;

	case RendererState::RENDERSTATE_FAILED:
		PauseRendererIngress();
		DestroyVideoRenderer();
		if (m_rendererRetirementPending)
		{
			// Keep the shield and host alive until the retirement worker has
			// finished the old renderer apartment.
		}
		else if (m_rendererResetTransitionActive)
			m_rendererState = RendererState::RENDERSTATE_UNKNOWN;
		else
		{
			m_rendererTransitionWindow.Hide();
			m_windowedVideoWindow.ShowLogo(true);
		}
		m_rendererStateText.SetWindowText(TEXT("Failed"));
		m_windowedVideoWindow.SetWindowText(
			TEXT("DirectShow renderer failed to build or start"));
		m_rendererFullscreenCheck.SetCheck(FALSE);
		enableButtons = !m_rendererResetTransitionActive;
		break;

	default:
		assert(false);
	}

	m_rendererRestartButton.EnableWindow(enableButtons);
	m_rendererResetButton.EnableWindow(enableButtons);

	UpdateState();

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageRendererStateChange(): Done")));
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageRendererLiveFrame(
	WPARAM wParam,
	LPARAM)
{
	TryRevealRendererTransition(static_cast<uint32_t>(wParam));
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageRendererResetRequest(
	WPARAM,
	LPARAM)
{
	PumpRendererResetMailbox();
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageRendererRetired(
	WPARAM wParam,
	LPARAM lParam)
{
	const uint64_t token = static_cast<uint64_t>(wParam);
	if (!m_rendererRetirementPending ||
		token != m_rendererRetirementToken)
	{
		DebugLog::Log(
			"Renderer retirement completion ignored: token=%llu current=%llu pending=%d",
			static_cast<unsigned long long>(token),
			static_cast<unsigned long long>(m_rendererRetirementToken),
			m_rendererRetirementPending ? 1 : 0);
		return 0;
	}

	m_rendererRetirementPending = false;
	if (lParam != 0)
	{
		DebugLog::Log(
			"Renderer retirement failed: token=%llu renderer=%S; replacement remains blocked",
			static_cast<unsigned long long>(token),
			static_cast<LPCTSTR>(m_retiringRendererName));
		m_rendererState = RendererState::RENDERSTATE_FAILED;
		m_rendererStateText.SetWindowText(TEXT("Retirement failed"));
		return 0;
	}
	DebugLog::Log(
		"Renderer transition: process=%lu generation=%u event=old-surface-retired "
		"renderer=%S target=%p cover=%p token=%llu",
		GetCurrentProcessId(), m_retiringRendererGeneration,
		static_cast<LPCTSTR>(m_retiringRendererName),
		m_rendererTargetHwnd, m_rendererTransitionWindow.GetHWND(),
		static_cast<unsigned long long>(token));
	m_retiringRendererName.Empty();
	m_retiringRendererGeneration = 0;
	if (m_rendererState == RendererState::RENDERSTATE_STOPPED)
		m_rendererState = RendererState::RENDERSTATE_UNKNOWN;
	else if (m_rendererState == RendererState::RENDERSTATE_FAILED &&
		!m_wantToTerminate)
	{
		if (m_rendererResetTransitionActive)
		{
			m_rendererState = RendererState::RENDERSTATE_UNKNOWN;
		}
		else
		{
			m_rendererTransitionWindow.Hide();
			m_windowedVideoWindow.ShowLogo(true);
			m_rendererFullscreenCheck.SetCheck(FALSE);
		}
	}
	UpdateState();
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageRendererDetailString(WPARAM wParam, LPARAM lParam)
{
	CString* pDetailString = (CString*)wParam;

	m_rendererDetailStringStatic.SetWindowText(*pDetailString);

	delete pDetailString;
	return 0;
}

//
// Command handlers
//


void CVideoProcessorDlg::OnCommandFullScreenToggle()
{
	m_rendererFullscreenCheck.SetCheck(
		m_rendererFullscreenCheck.GetCheck() ? 0 : 1);
	OnBnClickedRendererFullScreenCheck();
}


void CVideoProcessorDlg::OnCommandFullScreenExit()
{
	// If fullscreen toggle off, else do nothing
	if (m_rendererFullscreenCheck.GetCheck())
		OnCommandFullScreenToggle();
}


void CVideoProcessorDlg::OnCommandRendererReset()
{
	if (m_videoRenderer)
	{
		RequestRendererReset(RendererResetReason::Manual, true, 0);
		DEBUGLOG("OnCommandRendererReset");
	}
}

void CVideoProcessorDlg::OnCommandRendererRestart()
{
		
	m_postRendererStartRequiresGraph = true;
	m_wantToRestartRenderer = true;
	UpdateState();
	
}


void CVideoProcessorDlg::OnCommandDisplayRuleAuto()
{
	if (!m_videoRenderer)
		return;

	CString activeRule;
	bool rendererRestartRequired = false;
	if (!m_videoRenderer->SelectDisplayRule(TEXT("auto"), activeRule,
		rendererRestartRequired))
	{
		DEBUGLOG("Automatic display-rule selection ignored: selected renderer does not support display rules");
		return;
	}

	DEBUGLOG("Automatic display-rule selection requested: %s", activeRule.GetString());
	if (rendererRestartRequired)
	{
		m_postRendererStartRequiresGraph = false;
		m_wantToRestartRenderer = true;
		UpdateState();
	}
}


void CVideoProcessorDlg::OnCommandShaderRule(UINT commandId)
{
	const auto rule = m_shaderShortcutRules.find(static_cast<WORD>(commandId));
	if (rule == m_shaderShortcutRules.end())
		return;

	CString replacedSelector;
	if (m_shaderShortcutDebounce.HasPending())
	{
		const auto replaced = m_shaderShortcutRules.find(
			static_cast<WORD>(m_shaderShortcutDebounce.PendingCommand()));
		if (replaced != m_shaderShortcutRules.end())
			replacedSelector = replaced->second;
	}
	m_shaderShortcutDebounce.Queue(commandId, GetTickCount64());
	KillTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID);
	SetTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID,
		SHADER_SHORTCUT_DEBOUNCE_MS, nullptr);
	DEBUGLOG(
		"Shader shortcut pending selector='%S' debounce=%ums replaced='%S'",
		static_cast<LPCTSTR>(rule->second),
		static_cast<unsigned int>(SHADER_SHORTCUT_DEBOUNCE_MS),
		static_cast<LPCTSTR>(replacedSelector));
}


void CVideoProcessorDlg::ApplyShaderRuleCommand(UINT commandId)
{
	const auto rule = m_shaderShortcutRules.find(static_cast<WORD>(commandId));
	if (rule == m_shaderShortcutRules.end() || !m_videoRenderer)
		return;

	CString activeRule;
	bool rendererRestartRequired = false;
	if (!m_videoRenderer->SelectShaderRule(rule->second, activeRule,
		rendererRestartRequired))
	{
		DEBUGLOG("Shader rule '%S' ignored: selected renderer does not support it or the rule is invalid",
			static_cast<LPCTSTR>(rule->second));
		return;
	}
	m_requestedShaderSelector = rule->second;
	DEBUGLOG("Shader rule changed to '%S'", static_cast<LPCTSTR>(activeRule));
	if (rendererRestartRequired)
	{
		DEBUGLOG("Shader rule aspect ratio changed; restarting renderer to renegotiate media type");
		m_postRendererStartRequiresGraph = false;
		m_wantToRestartRenderer = true;
		UpdateState();
	}
}


bool CVideoProcessorDlg::ApplyRequestedShaderSelection()
{
	if (!m_videoRenderer || m_requestedShaderSelector.IsEmpty())
		return true;

	CString activeRule;
	bool rendererRestartRequired = false;
	if (!m_videoRenderer->SelectShaderRule(
		m_requestedShaderSelector, activeRule, rendererRestartRequired))
	{
		DEBUGLOG(
			"Shader selector '%S' is not applicable to the newly built renderer",
			static_cast<LPCTSTR>(m_requestedShaderSelector));
		return false;
	}
	DEBUGLOG(
		"Shader selector '%S' restored on the newly built renderer as '%S'",
		static_cast<LPCTSTR>(m_requestedShaderSelector),
		static_cast<LPCTSTR>(activeRule));
	if (rendererRestartRequired)
	{
		DEBUGLOG(
			"Restored shader selector requested an additional renderer negotiation");
		m_wantToRestartRenderer = true;
	}
	return true;
}


void CVideoProcessorDlg::OnCommandDisplayRule(UINT commandId)
{
	const auto unifiedKey = m_unifiedProfileShortcutKeys.find(static_cast<WORD>(commandId));
	if (unifiedKey != m_unifiedProfileShortcutKeys.end())
	{
		const DWORD commandTime = static_cast<DWORD>(GetMessageTime());
		if (m_lastUnifiedProfileCommand == commandId &&
			commandTime - m_lastUnifiedProfileCommandTime < 100)
		{
			DEBUGLOG("Unified profile key repeat suppressed: %S",
				static_cast<LPCTSTR>(unifiedKey->second));
			return;
		}
		m_lastUnifiedProfileCommand = static_cast<WORD>(commandId);
		m_lastUnifiedProfileCommandTime = commandTime;
		UnifiedProfileRuntime::SelectionResult result;
		std::string error;
		if (!m_profileRuntime.SelectKey(
			CStringA(unifiedKey->second).GetString(),
			GetUnifiedProfileSourceLookup(), result, error))
		{
			DebugLog::Log("Unified profile key '%s' is unavailable: %s",
				CStringA(unifiedKey->second).GetString(),
				error.c_str());
			return;
		}
		std::ostringstream activeProfiles;
		for (size_t index = 0; index < result.selections.size(); ++index)
		{
			if (index != 0) activeProfiles << ", ";
			activeProfiles << result.selections[index].group << "="
				<< result.selections[index].profile;
		}
		DebugLog::Log("Unified profile key selected: %s",
			activeProfiles.str().c_str());
		if (result.changed)
		{
			ApplyUnifiedProfileSnapshot(result.snapshot, true);
			ScheduleUnifiedProfileActions(result.actions);
		}
		return;
	}
	const auto rule = m_displayRuleShortcutRules.find(static_cast<WORD>(commandId));
	if (rule == m_displayRuleShortcutRules.end() || !m_videoRenderer)
		return;

	CString activeRule;
	bool rendererRestartRequired = false;
	if (!m_videoRenderer->SelectDisplayRule(rule->second, activeRule,
		rendererRestartRequired))
	{
		DEBUGLOG("Display rule '%S' is unavailable",
			static_cast<LPCTSTR>(rule->second));
		return;
	}

	DEBUGLOG("Manual display rule selected: %S",
		static_cast<LPCTSTR>(activeRule));
	if (rendererRestartRequired)
	{
		m_postRendererStartRequiresGraph = false;
		m_wantToRestartRenderer = true;
		UpdateState();
	}
}

void CVideoProcessorDlg::OnCommandRendererSelect(UINT commandId)
{
	const auto shortcut =
		m_rendererShortcutIndices.find(static_cast<WORD>(commandId));
	if (shortcut == m_rendererShortcutIndices.end())
		return;

	const unsigned int oneBasedIndex = shortcut->second;
	if (oneBasedIndex == 0 ||
		oneBasedIndex > static_cast<unsigned int>(m_rendererCombo.GetCount()))
	{
		DEBUGLOG(
			"Renderer shortcut render.%u ignored: only %d renderers are available",
			oneBasedIndex,
			m_rendererCombo.GetCount());
		return;
	}

	const int comboIndex = static_cast<int>(oneBasedIndex - 1);
	CString rendererName;
	m_rendererCombo.GetLBText(comboIndex, rendererName);
	if (m_rendererCombo.GetCurSel() == comboIndex)
	{
		DEBUGLOG("Renderer shortcut render.%u already selected: %s",
			oneBasedIndex,
			rendererName.GetString());
		return;
	}

	m_rendererCombo.SetCurSel(comboIndex);
	DEBUGLOG("Renderer shortcut render.%u selected: %s",
		oneBasedIndex,
		rendererName.GetString());
	UpdateRendererBackendUi();
	OnBnClickedRendererRestart();
}



void CVideoProcessorDlg::OnCommandPQSet()
{
	m_rendererTransferFunctionCombo.SetCurSel(1);
	OnBnClickedRendererRestart();
}


void CVideoProcessorDlg::OnCommandAutoSet()
{
	m_rendererTransferFunctionCombo.SetCurSel(0);
	OnBnClickedRendererRestart();
}

void CVideoProcessorDlg::OnCommandConfigEditor()
{
	wchar_t modulePath[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath)) == 0)
		return;
	std::wstring executablePath(modulePath);
	const size_t separator = executablePath.find_last_of(L"\\/");
	if (separator == std::wstring::npos)
		return;
	executablePath.resize(separator + 1);
	const std::wstring editorPath = executablePath + L"VideoProcessorConfig.exe";
	if (GetFileAttributesW(editorPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		DEBUGLOG("Configuration editor is not installed beside VideoProcessor.exe");
		AfxMessageBox(L"VideoProcessorConfig.exe is not installed beside VideoProcessor.exe.");
		return;
	}

	ConfigFile config;
	std::wstring configPath = executablePath + L"VideoProcessor.cfg";
	if (config.Load() && !config.GetLoadedPath().empty())
	{
		const int length = MultiByteToWideChar(CP_ACP, 0,
			config.GetLoadedPath().c_str(), -1, nullptr, 0);
		if (length > 1)
		{
			configPath.assign(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_ACP, 0, config.GetLoadedPath().c_str(), -1,
				&configPath[0], length);
			configPath.pop_back();
		}
	}
	wchar_t arguments[2 * MAX_PATH + 80] = {};
	swprintf_s(arguments, L"--config \"%s\" --owner %llu", configPath.c_str(),
		static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(GetSafeHwnd())));
	const HINSTANCE result = ShellExecuteW(GetSafeHwnd(), L"open", editorPath.c_str(),
		arguments, executablePath.c_str(), SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
		AfxMessageBox(L"Could not launch VideoProcessorConfig.exe.");
}

void CVideoProcessorDlg::OnCommandToggleStatsOverlay()
{
	DebugLog::Log(
		"Keyboard command handler: command=toggle-stats requested_before=%d renderer_state=%d generation=%u retirement_pending=%d reset_active=%d foreground=%p focus=%p",
		m_statsOverlayRequestedVisible ? 1 : 0,
		static_cast<int>(m_rendererState),
		m_rendererGeneration.load(std::memory_order_acquire),
		m_rendererRetirementPending ? 1 : 0,
		RendererResetOperationInProgress() ? 1 : 0,
		reinterpret_cast<void*>(::GetForegroundWindow()),
		reinterpret_cast<void*>(::GetFocus()));
	if (!m_statsOverlay)
	{
		DebugLog::Log("Keyboard command handler: command=toggle-stats result=no-overlay");
		return;
	}
	m_statsOverlayRequestedVisible = !m_statsOverlayRequestedVisible;
	ApplyStatsOverlayForActiveRenderer();
	DebugLog::Log(
		"Keyboard command handler: command=toggle-stats result=applied requested_after=%d",
		m_statsOverlayRequestedVisible ? 1 : 0);
}

void CVideoProcessorDlg::ApplyStatsOverlayForActiveRenderer()
{
	if (!m_statsOverlay)
		return;
	const bool native = m_videoRenderer &&
		m_videoRenderer->SupportsNativeStatsOverlay();
	if (native)
	{
		if (!m_statsOverlay->IsCreated() &&
			!m_statsOverlay->Create(this->GetSafeHwnd()))
			return;
		if (m_statsOverlay->IsVisible())
			m_statsOverlay->Show(false);
		if (m_statsOverlayRequestedVisible)
			UpdateStatsOverlay();
		else
			m_videoRenderer->SetNativeStatsOverlay(nullptr, 0, 0, 0, 0);
		return;
	}
	if (!m_statsOverlay->IsCreated() && m_statsOverlayRequestedVisible &&
		!m_statsOverlay->Create(this->GetSafeHwnd()))
		return;
	m_statsOverlay->Show(m_statsOverlayRequestedVisible);
}

//
// ICaptureDeviceDiscovererCallback
//


void CVideoProcessorDlg::OnCaptureDeviceFound(ACaptureDeviceComPtr& captureDevice)
{
	// WARNING: Most likely to be called from some internal capture card thread!

	assert(captureDevice);

	PostMessage(
		WM_MESSAGE_CAPTURE_DEVICE_FOUND,
		(WPARAM)captureDevice.Detach(),
		0);
}


void CVideoProcessorDlg::OnCaptureDeviceLost(ACaptureDeviceComPtr& captureDevice)
{
	// WARNING: Most likely to be called from some internal capture card thread!

	assert(captureDevice);

	PostMessage(
		WM_MESSAGE_CAPTURE_DEVICE_LOST,
		(WPARAM)captureDevice.Detach(),
		0);
}

//
// ICaptureDeviceCallback
//


void CVideoProcessorDlg::OnCaptureDeviceState(CaptureDeviceState state)
{
	// WARNING: Often, but not always, called from some internal capture card thread!

	PostMessage(
		WM_MESSAGE_CAPTURE_DEVICE_STATE_CHANGE,
		state,
		0);
}


void CVideoProcessorDlg::OnCaptureDeviceCardStateChange(CaptureDeviceCardStateComPtr cardState)
{
	// WARNING: Most likely to be called from some internal capture card thread!

	assert(cardState);

	PostMessage(
		WM_MESSAGE_CAPTURE_DEVICE_CARD_STATE_CHANGE,
		(WPARAM)cardState.Detach(),
		0);
}


void CVideoProcessorDlg::OnCaptureDeviceVideoStateChange(
	ACaptureDevice* source,
	CaptureRunToken captureRunToken,
	VideoStateComPtr videoState)
{
	// WARNING: Most likely to be called from some internal capture card thread!

	assert(videoState);
	assert(source);

	std::lock_guard<std::mutex> sourceLock(
		m_captureVideoStateNotificationMutex);
	if (source != m_captureVideoStateSource ||
		captureRunToken != m_captureVideoStateSourceEpoch)
	{
		DebugLog::Log(
			"Capture video-state callback ignored for stale capture run: "
			"epoch=%llu source=%p current_epoch=%llu current_source=%p",
			static_cast<unsigned long long>(captureRunToken),
			source,
			static_cast<unsigned long long>(
				m_captureVideoStateSourceEpoch),
			m_captureVideoStateSource);
		return;
	}

	const bool retainRendererIngress = !videoState->valid;
	const std::chrono::steady_clock::time_point ingressStart =
		std::chrono::steady_clock::now();
	const uint64_t notificationSequence =
		m_rendererIngressState->PublishCaptureSequence(
			retainRendererIngress ?
				RendererIngressState::CaptureSequencePublication::
					RetainCurrentRendererState :
				RendererIngressState::CaptureSequencePublication::
					RequiresRendererAcknowledgement);
	const uint64_t ingressPublicationUs = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - ingressStart).count());
	std::unique_ptr<CaptureVideoStateNotification> notification(
		new CaptureVideoStateNotification());
	notification->source = source;
	notification->state = videoState;
	notification->captureEpoch = captureRunToken;
	notification->sequence = notificationSequence;
	notification->ingressPublicationUs = ingressPublicationUs;
	notification->retainedRendererIngress = retainRendererIngress;
	if (!PostMessage(
		WM_MESSAGE_CAPTURE_DEVICE_VIDEO_STATE_CHANGE,
		reinterpret_cast<WPARAM>(notification.get()),
		0))
	{
		DebugLog::Log(
			"Capture video-state notification post failed: sequence=%llu "
			"action=fail-closed",
			static_cast<unsigned long long>(notificationSequence));
		return;
	}
	notification.release();
}


void CVideoProcessorDlg::OnCaptureDeviceVideoFrame(
	ACaptureDevice* source,
	CaptureRunToken captureRunToken,
	VideoFrame& videoFrame)
{
	// WARNING: Most likely to be called from some internal capture card thread!

	RendererIngressState::Lease ingressLease;
	{
		std::lock_guard<std::mutex> sourceLock(
			m_captureVideoStateNotificationMutex);
		if (source != m_captureVideoStateSource ||
			captureRunToken != m_captureVideoStateSourceEpoch)
			return;
		ingressLease = m_rendererIngressState->TryAcquire();
	}
	if (!ingressLease)
		return;

	const std::shared_ptr<IVideoRenderer> renderer =
		std::atomic_load_explicit(
			&m_videoRenderer, std::memory_order_acquire);
	if (!renderer)
		return;

	assert(m_captureDevice);
	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);
	assert(m_rendererState == RendererState::RENDERSTATE_RENDERING);

	renderer->OnVideoFrame(videoFrame);
	if (renderer->HasPresentedLiveFrame() &&
		!m_transitionRevealPosted.exchange(
			true, std::memory_order_acq_rel))
	{
		PostMessage(
			WM_MESSAGE_RENDERER_LIVE_FRAME,
			static_cast<WPARAM>(
				m_rendererGeneration.load(std::memory_order_acquire)),
			0);
	}
}


void CVideoProcessorDlg::OnCaptureDeviceError(const CString& error)
{
	// WARNING: Most likely to be called from some internal capture card thread!

	CString* postedError = new CString(error);

	PostMessage(
		WM_MESSAGE_CAPTURE_DEVICE_ERROR,
		(WPARAM)postedError,
		0);
}


void CVideoProcessorDlg::OnRendererState(RendererState rendererState)
{
	// Will be called synchronous as a response to our calls and hence does
	// not need posting messages, we still do so to keep the pattern.

	PostMessage(
		WM_MESSAGE_RENDERER_STATE_CHANGE,
		rendererState,
		0);
}


void CVideoProcessorDlg::OnRendererDetailString(const CString& details)
{
	// Will be called synchronous as a response to our calls and hence does
	// not need posting messages, we still do so to keep the pattern.

	CString* pDetailString = new CString(details);

	PostMessage(
		WM_MESSAGE_RENDERER_DETAIL_STRING,
		(WPARAM)pDetailString,
		0);
}


void CVideoProcessorDlg::UpdateState()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::UpdateState()")));
	if (m_rendererRetirementPending)
	{
		DbgLog((LOG_TRACE, 1,
			TEXT("CVideoProcessorDlg::UpdateState(): waiting for renderer retirement")));
		return;
	}

	// Want to change cards or want to restart capture
	if (!m_desiredCaptureDevice.IsEqualObject(m_captureDevice) ||
		m_wantToRestartCapture)
	{
		m_captureInputCombo.EnableWindow(FALSE);

		// Have a render and it's rendering, stop it
		if (m_videoRenderer &&
			m_rendererState == RendererState::RENDERSTATE_RENDERING)
		{
			RenderStop();
			return;
		}

		// Waiting for render to go away
		// (This has to come before stopping the capture as the renderer might be
		//  using the capture card as a clock.)
		if (m_videoRenderer)
			return;

		// Have a capture and it's capturing, stop it
		if (m_captureDevice &&
			m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		{
			CaptureStop();
			return;
		}

		// If capture device is stopped we're happy to remove it
		if (m_captureDevice && m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_READY)
			CaptureRemove();

		// We're waiting for the capture device to go away
		if (m_captureDevice)
			return;

		// If this came from a desire to restart the capture, that ends now
		if (m_wantToRestartCapture)
			m_wantToRestartCapture = false;

		// From this point on we should be clean, set up new card if so desired
		assert(!m_videoRenderer);
		assert(!m_captureDevice);
		assert(!m_wantToRestartCapture);
		assert(m_rendererState == RendererState::RENDERSTATE_UNKNOWN ||
			   m_rendererState == RendererState::RENDERSTATE_FAILED);
		assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN);

		if (m_desiredCaptureDevice)
		{
			m_captureDevice = m_desiredCaptureDevice;
			m_captureDevice->SetCallbackHandler(this);
			m_captureDevice->SetFrameOffsetMs(GetTimingClockFrameOffsetMs());

			RefreshInputConnectionCombo();

			m_captureInputCombo.EnableWindow(TRUE);
		}
	}

	// Capture card gone, but still have renderer, can't live for much longer
	if (!m_captureDevice && m_videoRenderer)
	{
		assert(m_rendererState != RendererState::RENDERSTATE_RENDERING);
		return;
	}

	// If we want to terminate at this point we should be good to do so
	if (m_wantToTerminate)
	{
		DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::UpdateState(): - Want to terminate")));

		assert(!m_captureDevice);
		assert(!m_desiredCaptureDevice);
		assert(!m_videoRenderer);
		assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN);
		assert(
			m_rendererState == RendererState::RENDERSTATE_UNKNOWN ||
			m_rendererState == RendererState::RENDERSTATE_FAILED ||
			m_rendererState == RendererState::RENDERSTATE_STOPPED);

		CDialog::EndDialog(S_OK);
		return;
	}

	// If we don't have a capture card here we we don't want to.
	if (!m_captureDevice)
	{
		assert(!m_desiredCaptureDevice);
		return;
	}

	assert(m_desiredCaptureDevice == m_captureDevice);

	// Capture device still starting up
	if (m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN)
		return;

	assert(m_captureDeviceState != CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN);

	// Have the right capture device, but want different input
	if (m_desiredCaptureInputId != INVALID_CAPTURE_INPUT_ID &&
		m_desiredCaptureInputId != m_currentCaptureInputId)
	{
		// Have a render and it's rendering, stop it
		if (m_videoRenderer &&
			m_rendererState == RendererState::RENDERSTATE_RENDERING)
		{
			RenderStop();
			return;
		}

		// Waiting for render to go away
		// (This has to come before stopping the capture as the renderer might be
		//  using the capture card as a clock.)
		if (m_videoRenderer)
			return;

		// Have a capture and it's capturing, stop it
		if (m_captureDevice &&
			m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		{
			CaptureStop();
			return;
		}

		// Waiting for the capture to be stopped
		if (m_captureDeviceState != CaptureDeviceState::CAPTUREDEVICESTATE_READY)
			return;

		// From this point on we should be clean, set up new card
		assert(!m_videoRenderer);
		assert(m_rendererState == RendererState::RENDERSTATE_UNKNOWN);
		assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_READY);

		m_captureDevice->SetCaptureInput(m_desiredCaptureInputId);
		m_currentCaptureInputId = m_desiredCaptureInputId;

		CaptureStart();
		return;
	}

	// Have the right card and the right input
	assert(m_captureDevice);
	assert(m_desiredCaptureDevice == m_captureDevice);
	assert(m_desiredCaptureInputId == m_currentCaptureInputId);

	// Only continue if we're actually capturing
	if (m_captureDeviceState != CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		return;

	//
	// Capture is good from here on out
	//

	// No render, start one if the current state is not failed and we have a valid video state
	if (!m_videoRenderer)
	{
		if (m_fullScreenVideoWindow &&
			!IsWindow(m_fullScreenVideoWindow->GetHWND()))
		{
			DebugLog::Log(
				"Fullscreen host is invalid after renderer teardown; recreating it");
			FullScreenVideoWindowDestroy();
		}
		if (!m_rendererRetirementPending &&
			m_fullscreenModeChangePending)
		{
			if (m_fullScreenVideoWindow)
				FullScreenVideoWindowDestroy();
			m_fullscreenModeChangePending = false;
		}
		// If we still have a full screen window and don't want to be full screen anymore clean it up
		if (!m_rendererFullscreenCheck.GetCheck() && m_fullScreenVideoWindow &&
			!m_preserveFullscreenHostForProfileRestart)
		{
			FullScreenVideoWindowDestroy();
			videoProcessorApp.RestoreDisplayTopology("fullscreen-off");
		}

		// If the renderer failed we don't auto-start it again but wait for something to happen
		if (m_rendererState == RendererState::RENDERSTATE_FAILED)
		{
			videoProcessorApp.RestoreDisplayTopology("renderer-start-failure");
			return;
		}

		if (m_appliedCaptureVideoStateNotificationSequence !=
			m_rendererIngressState->LatestCaptureSequence())
		{
			return;
		}
		if (m_captureDeviceVideoState &&
			m_captureDeviceVideoState->valid)
			RenderStart();

		return;
	}

	assert(m_videoRenderer);

	// If we have a renderer but the video state is invalid stop if rendering
	if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		(!m_captureDeviceVideoState ||
	  	 !m_captureDeviceVideoState->valid))
	{
		DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::UpdateState(): - Stopping renderering because of invalid capture video state")));

		RenderStop();
		return;
	}

	// Somebody wants to restart rendering
	if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		m_wantToRestartRenderer)
	{
		DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::UpdateState(): - Asked to restart renderer")));

		if (RendererResetOperationInProgress())
		{
			// Keep the intent latched. Reset completion calls UpdateState again,
			// at which point teardown can safely begin.
			RenderStop();
			return;
		}
		m_wantToRestartRenderer = false;
		RenderStop();
		return;
	}

	// We have a renderer and a valid video state, relax and enjoy the show
	assert(m_captureDeviceVideoState);
	//assert(m_captureDeviceVideoState->valid);

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::UpdateState(): - No changes")));
}

//
// Helpers
//

int CVideoProcessorDlg::CalculateAutoFrameOffset() {
	// Alpha presents from its own FIFO and does not schedule delivery from the
	// capture timestamp.  A positive capture timestamp offset therefore adds no
	// presentation benefit there; keep automatic mode neutral.
	if (IsAlphaRendererSelected())
		return 0;

	size_t m_frameQueueMaxSize = GetRendererVideoFrameQueueSizeMax();


	size_t nominalTarget = (m_frameQueueMaxSize / 8);
	double fps = m_captureDeviceVideoState->displayMode->RefreshRateHz();
	size_t frames = fps > 30.0 ? nominalTarget + 1 : nominalTarget / 2;
	DEBUGLOG("Frames calculated: %zu (fps=%.2f, nominalTarget=%zu)", frames, fps, nominalTarget);
	//return frames;
	//---

	if (fps <= 0.0)
		return 0;

	const double frameTime = 1000.0 / fps;
	const double roundedOffset = ceil((static_cast<double>(frames) * frameTime) / 5.0) * 5.0;
	const int offset = roundedOffset >= static_cast<double>(INT_MAX)
		? INT_MAX
		: static_cast<int>(roundedOffset);


	DEBUGLOG("Auto frame offset calc: queueMax=%zu, nominalTarget=%zu, refresh=%.1f, frames=%zu, frameTime=%.2f, offset=%d",
		m_frameQueueMaxSize, nominalTarget, fps, frames, frameTime, offset);

	return offset;

/*	// Return default if no capture device/video state
	if (!m_captureDevice || !m_captureDeviceVideoState || !m_captureDeviceVideoState->valid)
		return 50;  // Safe default

	// Base hardware latency
	const double hwLatency = m_captureDevice->HardwareLatencyMs();
	int offset = static_cast<int>(hwLatency + 0.5);  // Round up
	offset = std::max(offset, 1);  // Minimum 1ms

	// Refresh rate consideration
	const double refreshRate = m_captureDeviceVideoState->displayMode->RefreshRateHz();

	// Queue configuration impact
	const bool isAsync = GetRendererVideoFrameUseQueue();
	const size_t queueMaxSize = GetRendererVideoFrameQueueSizeMax();

	int queueBuffer = 0;
	if (isAsync && queueMaxSize > 0)
	{
		// Async: Queue provides buffering, less offset needed
		queueBuffer = 8 + static_cast<int>(queueMaxSize * 0.3);  // ~8-18ms range
	}
	else
	{
		// Sync: No queue buffering, need more safety margin
		queueBuffer = 20;
	}
	offset += queueBuffer;

	// Timing method consideration
	int methodIndex = m_rendererDirectShowStartStopTimeMethodCombo.GetCurSel();
	int safetyMargin = 5;  // Default

	if (methodIndex >= 0)
	{
		DirectShowStartStopTimeMethod method =
			static_cast<DirectShowStartStopTimeMethod>(
				m_rendererDirectShowStartStopTimeMethodCombo.GetItemData(methodIndex));

		switch (method)
		{
		case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
		case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
			safetyMargin = 3;  // Precise timing needs less margin
			break;
		case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:
		case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2:
			safetyMargin = 5;  // Moderate
			break;
		case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO:
		case DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO:
			safetyMargin = 7;  // Conservative timing needs more margin
			break;
		}
	}
	offset += safetyMargin;

	// Refresh rate scaling
	if (refreshRate >= 100.0)
		offset = static_cast<int>(offset * 0.9);  // High refresh: tighter timing
	else if (refreshRate <= 30.0)
		offset = static_cast<int>(offset * 1.1);  // Low refresh: more conservative

	// Clamp to reasonable range
	offset = std::max(offset, 15);   // Minimum for reliable operation
	offset = std::min(offset, 100);  // Maximum to avoid excessive latency

	DbgLog((LOG_TRACE, 1, TEXT("Auto frame offset: %dms (async=%d, queue=%zu, fps=%.1f)"),
		offset, isAsync, queueMaxSize, refreshRate));

	return offset;
	*/
}

void CVideoProcessorDlg::OnSelectCaptureDevice(UINT nID)
{
	int deviceIndex = nID - ID_COMMAND_CAPTURE_1 + 1; // Convert ID to device number
	SelectCaptureDevice(deviceIndex);
}

void CVideoProcessorDlg::SelectCaptureDevice(int n)
{
	// Logic to select the capture device
	CString captureDeviceName;
	captureDeviceName.Format(_T(".*?DeckLink.*?\\(%d\\)"), n);
	SelectCaptureDevice(captureDeviceName);
}

void CVideoProcessorDlg::SelectCaptureDevice(CString& captureDeviceName)
{
	int initialDeviceSelection = 0;
	bool found = false;

	std::wregex pattern;
	try
	{
		pattern = std::wregex((LPCWSTR)captureDeviceName, std::regex_constants::icase);
	}
	catch (const std::regex_error&)
	{
		// If regex is invalid, fall back to substring search
		pattern = std::wregex(L".*"); // Match anything
	}

	int itemCount = m_captureDeviceCombo.GetCount();
	for (int i = 0; i < itemCount; ++i)
	{
		CString itemText;
		m_captureDeviceCombo.GetLBText(i, itemText);

		std::wstring witem((LPCWSTR)itemText);
		if (std::regex_search(witem, pattern))
		{
			initialDeviceSelection = i;
			found = true;
			break; // Stop at first match, or keep going to find best
		}
	}

	m_captureDeviceCombo.SetCurSel(initialDeviceSelection);
	OnCaptureDeviceSelected();
}


void CVideoProcessorDlg::RefreshCaptureDeviceList()
{
	// Rebuild combo box with all devices which can capture
	m_captureDeviceCombo.ResetContent();

	// Convert set to vector for sorting
	std::vector<ACaptureDeviceComPtr> sortedDevices(m_captureDevices.begin(), m_captureDevices.end());

	// Sort devices by name
	std::sort(sortedDevices.begin(), sortedDevices.end(),
		[](const ACaptureDeviceComPtr& a, const ACaptureDeviceComPtr& b) {
			return wcscmp(a->GetName(), b->GetName()) < 0;
		});

	for (auto& captureDevice : sortedDevices)
	{
		if (!captureDevice->CanCapture())
			continue;

		const int index = m_captureDeviceCombo.AddString(captureDevice->GetName());
		m_captureDeviceCombo.SetItemDataPtr(index, (void*)captureDevice.p);

		// Retain selected device even if combo box position has changed
		if (captureDevice == m_captureDevice)
			m_captureDeviceCombo.SetCurSel(index);
	}

	if (m_captureDeviceCombo.GetCount() > 0)
	{
		m_captureDeviceCombo.EnableWindow(TRUE);

		// Discovery is asynchronous. Do not start the first card that happens
		// to arrive while waiting for the configured card; otherwise a later
		// matching device can never replace the already-active default.
		if (m_initialCaptureDevice.GetLength() > 0)
		{
			int configuredDeviceSelection = CB_ERR;
			for (int deviceIndex = 0; deviceIndex < m_captureDeviceCombo.GetCount(); ++deviceIndex)
			{
				CString deviceName;
				m_captureDeviceCombo.GetLBText(deviceIndex, deviceName);
				if (deviceName.CompareNoCase(m_initialCaptureDevice) == 0)
				{
					configuredDeviceSelection = deviceIndex;
					break;
				}
			}

			if (configuredDeviceSelection == CB_ERR)
				return;

			if (m_captureDeviceCombo.GetCurSel() != configuredDeviceSelection)
			{
				DEBUGLOG("Selecting configured capture device: %s", CStringA(m_initialCaptureDevice).GetString());
				m_captureDeviceCombo.SetCurSel(configuredDeviceSelection);
				OnCaptureDeviceSelected();
			}
			return;
		}

		// No configured device: select the first capture device if none is selected yet.
		if (m_captureDeviceCombo.GetCurSel() == CB_ERR)
		{
			m_captureDeviceCombo.SetCurSel(0);
			OnCaptureDeviceSelected();
		}
	}
	else
	{
		m_captureDeviceCombo.EnableWindow(FALSE);

		if (m_captureDevice)
		{
			m_desiredCaptureDevice = nullptr;
			UpdateState();
		}
	}
}

void CVideoProcessorDlg::RefreshInputConnectionCombo()
{
	assert(m_captureDevice);

	const CaptureInputs captureInputs = m_captureDevice->SupportedCaptureInputs();
	const CaptureInputId currentCaptureInputId = m_captureDevice->CurrentCaptureInputId();

	m_captureInputCombo.ResetContent();

	int index;
	for (auto& captureInput : captureInputs)
	{
		index = m_captureInputCombo.AddString(captureInput.name);
		m_captureInputCombo.SetItemData(index, captureInput.id);
	}

	// An explicit startup connection takes precedence over the device's current
	// preference. This selects a connection type (HDMI/SDI/etc.), not a port;
	// multi-port DeckLink devices expose each port in the capture-device list.
	if (!m_initialCaptureInput.IsEmpty())
	{
		for (int i = 0; i < m_captureInputCombo.GetCount(); ++i)
		{
			CString name;
			m_captureInputCombo.GetLBText(i, name);
			if (name.CompareNoCase(m_initialCaptureInput) == 0)
			{
				m_captureInputCombo.SetCurSel(i);
				OnCaptureInputSelected();
				return;
			}
		}
		DEBUGLOG("Configured capture input connection is unavailable for %s: %s",
			m_captureDevice->GetName().GetString(), m_initialCaptureInput.GetString());
	}

	// If we're in a known state keep the device's current selection.
	if (m_captureDeviceState != CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN)
		for (const auto& captureInput : captureInputs)
			if (captureInput.id == currentCaptureInputId)
			{
				for (int i = 0; i < m_captureInputCombo.GetCount(); ++i)
					if (static_cast<CaptureInputId>(m_captureInputCombo.GetItemData(i)) ==
						currentCaptureInputId)
					{
						m_captureInputCombo.SetCurSel(i);
						OnCaptureInputSelected();
						break;
					}
				break;
			}

	// If no input connection has been selected, select first index
	if (m_captureInputCombo.GetCount() > 0)
	{
		index = m_captureInputCombo.GetCurSel();

		// Nothing selected yet
		if (index == CB_ERR)
		{
			// Iterate all options
			bool found = false;
			for (int i = 0; i < m_captureInputCombo.GetCount(); i++)
			{
				const int n = m_captureInputCombo.GetLBTextLen(i);

				CString str;
				m_captureInputCombo.GetLBText(i, str.GetBuffer(n));
				str.ReleaseBuffer();

				if (str == TEXT("HDMI"))
				{
					m_captureInputCombo.SetCurSel(i);
					found = true;
					break;
				}
			}

			// Nothing found, just take first
			if(!found)
				m_captureInputCombo.SetCurSel(0);

			OnCaptureInputSelected();
		}
	}
}


void CVideoProcessorDlg::CaptureStart()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::CaptureStart()")));

	assert(m_captureDevice);
	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_READY);
	assert(!m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_UNKNOWN);

	// A new capture session is an initial lifecycle start, never a continuation
	// of a profile-only replacement from the preceding session.
	m_postRendererStartRequiresGraph = true;
	m_nextRendererIsRecoveryRecreation = false;
	m_directShowRecoveryRecreatedGeneration = 0;
	m_directShowRecoveryRecreationAttempted = false;
	m_directShowRecoveryRecreationCaptureSequence = 0;

	// Update internal state before call to StartCapture as that might be synchronous
	m_captureDeviceState = CaptureDeviceState::CAPTUREDEVICESTATE_STARTING;
	CaptureRunToken captureRunToken = 0;
	{
		std::lock_guard<std::mutex> sourceLock(
			m_captureVideoStateNotificationMutex);
		m_captureVideoStateSource = m_captureDevice.p;
		m_captureVideoStateSourceEpoch =
			++m_captureVideoStateNextEpoch;
		captureRunToken = m_captureVideoStateSourceEpoch;
	}

	m_captureDevice->StartCapture(captureRunToken);

	// Update GUI
	m_captureDeviceStateText.SetWindowText(TEXT("Starting"));
}


void CVideoProcessorDlg::CaptureStop()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::CaptureStop(): Begin")));

	assert(m_captureDevice);
	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);
	assert(!m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_UNKNOWN ||
		   m_rendererState == RendererState::RENDERSTATE_FAILED);

	// Update internal state before call to StartCapture as that might be synchronous
	m_captureDeviceState = CaptureDeviceState::CAPTUREDEVICESTATE_STOPPING;
	{
		std::lock_guard<std::mutex> sourceLock(
			m_captureVideoStateNotificationMutex);
		m_captureVideoStateSource = nullptr;
		m_captureVideoStateSourceEpoch = 0;
	}

	m_captureDevice->StopCapture();

	m_captureDeviceVideoState = nullptr;

	// Update GUI
	CaptureGUIClear();
	m_captureDeviceStateText.SetWindowText(TEXT("Stopping"));

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::CaptureStop(): End")));
}


void CVideoProcessorDlg::CaptureRemove()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::CaptureRemove(): Begin")));

	assert(m_captureDevice);
	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_READY);
	assert(!m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_UNKNOWN ||
		   m_rendererState == RendererState::RENDERSTATE_FAILED);

	m_captureDeviceState = CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN;
	{
		std::lock_guard<std::mutex> sourceLock(
			m_captureVideoStateNotificationMutex);
		m_captureVideoStateSource = nullptr;
		m_captureVideoStateSourceEpoch = 0;
	}
	m_captureDevice->SetCallbackHandler(nullptr);
	m_captureDevice.Release();
	m_captureDevice = nullptr;

	m_desiredCaptureInputId = INVALID_CAPTURE_INPUT_ID;
	m_currentCaptureInputId = INVALID_CAPTURE_INPUT_ID;

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::CaptureRemove(): End")));
}


void CVideoProcessorDlg::CaptureGUIClear()
{
	// Capture device group
	m_captureDeviceStateText.SetWindowText(TEXT(""));
	m_captureDeviceOtherList.ResetContent();

	// Input group
	m_inputLockedText.SetWindowText(TEXT(""));
	m_inputDisplayModeText.SetWindowText(TEXT("")) ;

	// Other
	m_captureDeviceOtherList.ResetContent();

	// Timing clock
	m_timingClockDescriptionText.SetWindowText(TEXT("")) ;

	// HDR colorSpace group
	m_hdrColorspaceREdit.SetWindowText(TEXT(""));
	m_hdrColorspaceGEdit.SetWindowText(TEXT(""));
	m_hdrColorspaceBEdit.SetWindowText(TEXT(""));
	m_hdrColorspaceWPEdit.SetWindowText(TEXT("")) ;

	// HDR Lumiance group
	m_hdrLuminanceMaxCll.SetWindowText(TEXT(""));
	m_hdrLuminanceMaxFall.SetWindowText(TEXT(""));
	m_hdrLuminanceMasterMin.SetWindowText(TEXT(""));
	m_hdrLuminanceMasterMax.SetWindowText(TEXT("")) ;

	// CIE1931 graph
	m_colorspaceCie1931xy.SetColorSpace(ColorSpace::UNKNOWN);
	m_colorspaceCie1931xy.SetHDRData(nullptr);
}


void CVideoProcessorDlg::RenderStart()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderStart(): Begin")));
	m_rendererCaptureVideoStateNotificationSequence =
		m_appliedCaptureVideoStateNotificationSequence;

	assert(!m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_UNKNOWN ||
		   m_rendererState == RendererState::RENDERSTATE_STOPPED);

	assert(m_captureDevice);
	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);

	// Rebuild from the current raw capture metadata for every new graph. A
	// confirmed new-LLDV candidate remains effective across renderer-only
	// rebuilds (such as NLS aspect-ratio renegotiation); it is cleared only by
	// a subsequent capture-state update that no longer qualifies as LLDV.
	if (m_captureDeviceVideoState)
		BuildPushVideoState();

	int i;

	i = m_rendererCombo.GetCurSel();

	// No renderer picked yet, ignore
	if (i < 0)
		return;

	RendererId* selectedRenderer =
		reinterpret_cast<RendererId*>(m_rendererCombo.GetItemData(i));
	if (!selectedRenderer)
		return;

	const CString previousRendererName = m_activeRendererName;
	const bool previousRendererWasDirectShow = m_activeRendererIsDirectShow;
	m_activeRendererName = selectedRenderer->name;
	m_activeRendererIsDirectShow =
		selectedRenderer->backend == RendererBackend::DIRECTSHOW;
	m_alphaBackendHandoffPending = IsDirectShowToAlphaBackendHandoff(
		previousRendererWasDirectShow, m_activeRendererIsDirectShow);
	if (m_alphaBackendHandoffPending)
	{
		DebugLog::Log(
			"Alpha backend handoff requested: previous_renderer=%S "
			"next_renderer=%S state=fresh-start-pending",
			static_cast<LPCTSTR>(previousRendererName),
			static_cast<LPCTSTR>(m_activeRendererName));
	}
	const uint32_t rendererGeneration =
		m_rendererGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
	const bool recoveryRecreation = m_nextRendererIsRecoveryRecreation;
	m_nextRendererIsRecoveryRecreation = false;
	m_directShowRecoveryRecreatedGeneration =
		recoveryRecreation ? rendererGeneration : 0;
	m_directShowGraphRecoveryAwaitingHealth = false;
	m_directShowGraphRecoveryWasRetarget = false;
	m_directShowRecoveryRebuildRequested = false;
	m_directShowGraphRecoveryGeneration = rendererGeneration;
	m_directShowGraphRecoveryEpoch = 0;
	m_directShowGraphRecoveryStartedTick = 0;
	if (m_preserveFullscreenHostForProfileRestart &&
		m_fullScreenVideoWindow &&
		IsWindow(m_fullScreenVideoWindow->GetHWND()))
	{
		m_rendererTargetHwnd = m_fullScreenVideoWindow->GetHWND();
		DebugLog::Log(
			"Profile renderer rebuild: preserving fullscreen host hwnd=%p generation=%u",
			m_rendererTargetHwnd, rendererGeneration);
	}
	else
	{
		m_rendererTargetHwnd = GetRenderWindow();
	}
	m_preserveFullscreenHostForProfileRestart = false;
	++m_rendererTargetRevision;
	m_transitionGeneration = rendererGeneration;
	if (m_rendererResetTransitionActive)
	{
		const RendererTransitionModel::Actions actions =
			m_rendererTransitionModel.ReplaceCoveredTarget(
				rendererGeneration, m_rendererTargetRevision);
		if (actions.empty() ||
			actions.front().type !=
				RendererTransitionActionType::RebindShieldTarget)
		{
			DEBUGLOG(
				"Renderer start cancelled: generation=%u "
				"failure=covered-target-rebind-rejected",
				rendererGeneration);
			return;
		}
	}
	else
	{
		m_rendererTransitionModel = RendererTransitionModel();
	}
	m_transitionRevealPosted.store(false, std::memory_order_release);
	if (!ShowRendererTransitionBlack("renderer-start"))
	{
		DEBUGLOG(
			"Renderer start cancelled: generation=%u "
			"failure=transition-black-unavailable",
			rendererGeneration);
		return;
	}

	// Get user-selectable options
	i = m_rendererDirectShowStartStopTimeMethodCombo.GetCurSel();
	assert(i >= 0);
	DirectShowStartStopTimeMethod directShowStartStopTimeMethod =
		(DirectShowStartStopTimeMethod)m_rendererDirectShowStartStopTimeMethodCombo.GetItemData(i);

	i = m_rendererVideoConversionCombo.GetCurSel();
	assert(i >= 0);
	VideoConversionOverride videoConversionOverride =
		(VideoConversionOverride)m_rendererVideoConversionCombo.GetItemData(i);

	i = m_rendererNominalRangeCombo.GetCurSel();
	assert(i >= 0);
	DXVA_NominalRange forceNominalRange =
		(DXVA_NominalRange)m_rendererNominalRangeCombo.GetItemData(i);

	i = m_rendererTransferFunctionCombo.GetCurSel();
	assert(i >= 0);
	DXVA_VideoTransferFunction forceVideoTransferFunction =
		(DXVA_VideoTransferFunction)m_rendererTransferFunctionCombo.GetItemData(i);

	i = m_rendererTransferMatrixCombo.GetCurSel();
	assert(i >= 0);
	DXVA_VideoTransferMatrix forceVideoTransferMatrix =
		(DXVA_VideoTransferMatrix)m_rendererTransferMatrixCombo.GetItemData(i);

	i = m_rendererPrimariesCombo.GetCurSel();
	assert(i >= 0);
	DXVA_VideoPrimaries forceVideoPrimaries =
		(DXVA_VideoPrimaries)m_rendererPrimariesCombo.GetItemData(i);

	// Capture card always provides the clock
	ITimingClock* timingClock = m_captureDevice->GetTimingClock();
	if (!timingClock)
		FatalError(TEXT("Failed to get timing clock from capture card"));

	m_windowedVideoWindow.SetWindowTextW(TEXT("Starting..."));
	m_rendererState = RendererState::RENDERSTATE_STARTING;

	// Internal renderers are deliberately listed beside registered renderers,
	// but do not construct or register a DirectShow graph.
#if defined(_WIN64)
	if (selectedRenderer->backend == RendererBackend::LIBPLACEBO)
	{
		try
		{
			const size_t alphaQueueCapacity =
				GetRendererVideoFrameQueueSizeMax();
			m_videoRenderer = std::make_shared<LibplaceboPluginVideoRenderer>(
				*this,
				m_rendererTargetHwnd,
				timingClock,
				GetRendererVideoFrameUseQueue(),
				alphaQueueCapacity,
				videoConversionOverride);
			BindRendererResetSink();

			ApplyUnifiedProfileSnapshot(m_profileRuntime.GetSnapshot(), false);

			if (m_captureDeviceVideoState)
				m_videoRenderer->OnVideoState(m_builtVideoState);

			m_videoRenderer->Build();
			m_videoRenderer->SetQueueFramePolicy(
				videoProcessorApp.GetQueueStartupPrerollFrames(),
				videoProcessorApp.GetQueueSteadyReserveFrames(),
				videoProcessorApp.HasQueueSteadyReserveFrames());
			m_videoRenderer->SetActivePictureLookaheadFrames(
				videoProcessorApp.GetActivePictureLookaheadFrames());
			ApplyRequestedShaderSelection();
			m_rendererTransitionWindow.KeepOnTop();
			// Match the DirectShow startup contract. Alpha owns its detector and
			// cadence policy inside the optional renderer, so the configured mode
			// must be forwarded before the first queued frame is accepted.
			m_videoRenderer->SetSceneAwareTimingCorrection(
				m_sceneAwareTimingCorrection);
			m_videoRenderer->Start();
			m_rendererStateText.SetWindowText(
				TEXT("Started VP Renderer, waiting for image..."));
		}
		catch (const std::exception& e)
		{
			DebugLog::Log("libplacebo renderer startup failed: %s", e.what());
			DestroyVideoRenderer();
			if (!m_rendererRetirementPending)
			{
				m_rendererTransitionWindow.Hide();
				m_windowedVideoWindow.ShowLogo(true);
			}
			m_rendererState = RendererState::RENDERSTATE_FAILED;
			m_rendererStateText.SetWindowText(TEXT("Failed"));

			wchar_t* ew = ToString(e.what());
			m_windowedVideoWindow.SetWindowText(ew);
			delete[] ew;

			m_rendererFullscreenCheck.SetCheck(FALSE);
			UpdateState();
			m_rendererRestartButton.EnableWindow(true);
		}

		DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderStart(): End internal renderer")));
		return;
	}
#endif

	GUID* rendererClSID = &selectedRenderer->guid;

	//
	// Construct renderer
	//

	try
	{
		if (IsEqualCLSID(*rendererClSID, CLSID_MPCVR))
			m_videoRenderer = std::make_shared<DirectShowMPCVideoRenderer>(
				*this, m_rendererTargetHwnd, GetSafeHwnd(),
				WM_MESSAGE_DIRECTSHOW_NOTIFICATION, timingClock,
				directShowStartStopTimeMethod,
				GetRendererVideoFrameUseQueue(),
				GetRendererVideoFrameQueueSizeMax(),
				videoConversionOverride, forceNominalRange,
				forceVideoTransferFunction, forceVideoTransferMatrix,
				forceVideoPrimaries);
		else if (IsEqualCLSID(
			*rendererClSID, CLSID_EnhancedVideoRenderer))
			m_videoRenderer =
				std::make_shared<DirectShowEnhancedVideoRenderer>(
					*this, m_rendererTargetHwnd, GetSafeHwnd(),
					WM_MESSAGE_DIRECTSHOW_NOTIFICATION, timingClock,
					directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride);
		else if (m_activeRendererName.Find(TEXT("madVR")) >= 0)
			m_videoRenderer =
				std::make_shared<DirectShowGenericHDRVideoRenderer>(
					*rendererClSID, *this, m_rendererTargetHwnd,
					GetSafeHwnd(), WM_MESSAGE_DIRECTSHOW_NOTIFICATION,
					timingClock, directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride, forceNominalRange,
					forceVideoTransferFunction, forceVideoTransferMatrix,
					forceVideoPrimaries);
		else
			m_videoRenderer =
				std::make_shared<DirectShowGenericVideoRenderer>(
					*rendererClSID, *this, m_rendererTargetHwnd,
					GetSafeHwnd(), WM_MESSAGE_DIRECTSHOW_NOTIFICATION,
					timingClock, directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride);
		BindRendererResetSink();

		ApplyUnifiedProfileSnapshot(m_profileRuntime.GetSnapshot(), false);

		if (m_captureDeviceVideoState)
			m_videoRenderer->OnVideoState(m_builtVideoState);

		m_videoRenderer->Build();
		m_videoRenderer->SetQueueFramePolicy(
			videoProcessorApp.GetQueueStartupPrerollFrames(),
			videoProcessorApp.GetQueueSteadyReserveFrames(),
			videoProcessorApp.HasQueueSteadyReserveFrames());
		m_videoRenderer->SetPresentationLeadFrames(
			videoProcessorApp.GetPresentationLeadFrames(),
			videoProcessorApp.HasPresentationLeadFrames());
		m_videoRenderer->SetActivePictureLookaheadFrames(
			videoProcessorApp.GetActivePictureLookaheadFrames());
		ApplyRequestedShaderSelection();
		m_rendererTransitionWindow.KeepOnTop();
		m_videoRenderer->SetSceneAwareTimingCorrection(m_sceneAwareTimingCorrection);
		m_videoRenderer->SetSceneCorrectionUpstreamSample(
			m_sceneCorrectionUpstreamSample);
		m_videoRenderer->SetSubtitleRepositioningMode(
			m_subtitleRepositionMode);
		m_videoRenderer->Start();

		m_rendererStateText.SetWindowText(TEXT("Started HDR renderer, waiting for image..."));

	}
	catch (std::runtime_error e)
	{
		DestroyVideoRenderer();
		if (m_rendererRetirementPending)
		{
			DebugLog::Log(
				"DirectShow startup fallback deferred until failed renderer retirement completes: %s",
				e.what());
			m_rendererState = RendererState::RENDERSTATE_FAILED;
			m_rendererStateText.SetWindowText(TEXT("Failed"));
			return;
		}

		try
		{
			if (IsEqualCLSID(*rendererClSID, CLSID_MPCVR))
			{
				m_videoRenderer = std::make_shared<DirectShowMPCVideoRenderer>(
					*this,
					m_rendererTargetHwnd,
					this->GetSafeHwnd(),
					WM_MESSAGE_DIRECTSHOW_NOTIFICATION,
					timingClock,
					directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride,
					forceNominalRange,
					forceVideoTransferFunction,
					forceVideoTransferMatrix,
					forceVideoPrimaries);
			}
			else if (IsEqualCLSID(*rendererClSID, CLSID_EnhancedVideoRenderer))
			{
				m_videoRenderer = std::make_shared<DirectShowEnhancedVideoRenderer>(
					*this,
					m_rendererTargetHwnd,
					this->GetSafeHwnd(),
					WM_MESSAGE_DIRECTSHOW_NOTIFICATION,
					timingClock,
					directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride);
			}
			else
				m_videoRenderer = std::make_shared<DirectShowGenericVideoRenderer>(
					*rendererClSID,
					*this,
					m_rendererTargetHwnd,
					this->GetSafeHwnd(),
					WM_MESSAGE_DIRECTSHOW_NOTIFICATION,
					timingClock,
					directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride);

			if (!m_videoRenderer)
				FatalError(TEXT("Failed to build DirectShow Video Renderer"));
			BindRendererResetSink();

			if (m_captureDeviceVideoState)
				m_videoRenderer->OnVideoState(m_builtVideoState);

			m_videoRenderer->Build();
			m_videoRenderer->SetQueueFramePolicy(
				videoProcessorApp.GetQueueStartupPrerollFrames(),
				videoProcessorApp.GetQueueSteadyReserveFrames(),
				videoProcessorApp.HasQueueSteadyReserveFrames());
			m_videoRenderer->SetPresentationLeadFrames(
				videoProcessorApp.GetPresentationLeadFrames(),
				videoProcessorApp.HasPresentationLeadFrames());
			m_videoRenderer->SetActivePictureLookaheadFrames(
				videoProcessorApp.GetActivePictureLookaheadFrames());
			m_rendererTransitionWindow.KeepOnTop();
			m_videoRenderer->SetSceneAwareTimingCorrection(m_sceneAwareTimingCorrection);
			m_videoRenderer->SetSceneCorrectionUpstreamSample(
				m_sceneCorrectionUpstreamSample);
			m_videoRenderer->SetSubtitleRepositioningMode(
				m_subtitleRepositionMode);
			m_videoRenderer->Start();

			m_rendererStateText.SetWindowText(TEXT("Started, waiting for image..."));
		}
		catch (std::runtime_error e)
		{
			DestroyVideoRenderer();
			if (!m_rendererRetirementPending)
			{
				m_rendererTransitionWindow.Hide();
				m_windowedVideoWindow.ShowLogo(true);
			}

			m_rendererState = RendererState::RENDERSTATE_FAILED;
			m_rendererStateText.SetWindowText(TEXT("Failed"));

			// Show error in renderer box
			wchar_t* ew = ToString(e.what());
			m_windowedVideoWindow.SetWindowText(ew);
			delete[] ew;

			// Ensure we're not full screen anymore and update state
			m_rendererFullscreenCheck.SetCheck(FALSE);
			UpdateState();

			// Give the user a chance to try again
			m_rendererRestartButton.EnableWindow(true);
		}
	}

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderStart(): End")));
}


void CVideoProcessorDlg::RenderStop()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderStop(): Begin")));

	if (RendererResetOperationInProgress())
	{
		const RendererResetCoordinator::Diagnostics diagnostics =
			m_rendererResetCoordinator->GetDiagnostics();
		const ULONGLONG now = GetTickCount64();
		if (m_lastResetDeferralLogTick == 0 ||
			now - m_lastResetDeferralLogTick >= 5000)
		{
			m_lastResetDeferralLogTick = now;
			DebugLog::Log(
				"Renderer stop deferred: graph-control request=%llu generation=%u "
				"is still active; UI remains available",
				static_cast<unsigned long long>(
					diagnostics.activeOperationId),
				diagnostics.rendererGeneration);
		}
		return;
	}

	// Cancel any pending EOTF change restart timer - a restart is already happening
	KillTimer(EOTF_CHANGE_RESTART_TIMER_ID);
	KillTimer(LLDV_CHANGE_RESTART_TIMER_ID);
	KillTimer(LLDV_PROFILE_APPLY_TIMER_ID);
	m_eotfChangeRestartCooldownSeconds = -1;
	m_lldvChangeRestartDelaySeconds = -1;
	m_lldvProfileApplyPending = false;
	m_lldvRestartPending = false;
	// A renderer-only restart must preserve a confirmed LLDV candidate. The
	// capture-state path clears it when the input genuinely returns to SDR.
	m_eotfCheckCooldownSeconds = 0;

	assert(m_captureDevice);
	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);

	assert(m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_RENDERING);

	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);

	assert(m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_RENDERING);

	if (!ShowRendererTransitionBlack("renderer-stop"))
	{
		DEBUGLOG(
			"Renderer stop cancelled: generation=%u "
			"failure=transition-black-unavailable",
			m_rendererGeneration.load(std::memory_order_acquire));
		return;
	}
	m_transitionRevealPosted.store(true, std::memory_order_release);

	// Gate new callbacks first. DirectShow Stop/BeginFlush must run before the
	// drain wait because an unbuffered callback can be blocked in Receive until
	// downstream graph control releases it.
	PauseRendererIngress();

	// Update internal state before call to StartCapture as that might be synchronous
	m_rendererState = RendererState::RENDERSTATE_STOPPING;

	const std::shared_ptr<RendererIngressState> ingress =
		m_rendererIngressState;
	const ULONGLONG stopQueuedTick = GetTickCount64();
	DebugLog::Log(
		"Renderer stop dispatch: phase=before-stop generation=%u renderer_state=%d foreground=%p focus=%p",
		m_rendererGeneration.load(std::memory_order_acquire),
		static_cast<int>(m_rendererState),
		reinterpret_cast<void*>(::GetForegroundWindow()),
		reinterpret_cast<void*>(::GetFocus()));
	m_videoRenderer->StopWithIngressDrain([ingress]()
		{
			ingress->WaitForDrain();
		});
	DebugLog::Log(
		"Renderer stop dispatch: phase=after-stop-call return_ms=%llu generation=%u renderer_state=%d foreground=%p focus=%p",
		static_cast<unsigned long long>(GetTickCount64() - stopQueuedTick),
		m_rendererGeneration.load(std::memory_order_acquire),
		static_cast<int>(m_rendererState),
		reinterpret_cast<void*>(::GetForegroundWindow()),
		reinterpret_cast<void*>(::GetFocus()));

	m_rendererStateText.SetWindowText(TEXT("Stopping"));

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderStop(): End")));
}


void CVideoProcessorDlg::RenderRemove()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderRemove(): Begin")));

	assert(m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_STOPPED);
	assert(!m_rendererIngressState->IsAdmitting());

	DestroyVideoRenderer();

	if (!m_rendererRetirementPending)
		m_rendererState = RendererState::RENDERSTATE_UNKNOWN;

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderRemove(): End")));
}


void CVideoProcessorDlg::DestroyVideoRenderer()
{
	if (!m_videoRenderer)
		return;
	if (m_fullscreenRetargetPending)
		ClearFullscreenRetarget(true);
	m_rendererCaptureVideoStateNotificationSequence = 0;
	m_rendererIngressState->AcknowledgeCaptureSequence(0);

	// Releasing a windowed renderer can synchronously pump WM_PAINT and other
	// window messages.  Detach the shared pointer before invoking the destructor
	// so a reentrant handler cannot call a virtual method on an object whose
	// derived destructor has already completed.
	std::shared_ptr<IVideoRenderer> rendererToDestroy =
		std::atomic_exchange_explicit(
			&m_videoRenderer,
			std::shared_ptr<IVideoRenderer>(),
			std::memory_order_acq_rel);
	m_dropDiagnosticRenderer = nullptr;
	m_dropDiagnosticInitialized = false;
	rendererToDestroy->SetResetRequestSink({});
	RevokeRendererResetSink();

	DbgLog((LOG_TRACE, 1,
		TEXT("CVideoProcessorDlg::DestroyVideoRenderer(): Renderer detached before destruction")));
	DebugLog::Log(
		"Renderer teardown: detached renderer before destruction to block reentrant callbacks");

	// Alpha/plugin retirement has not yet been audited as an idempotent
	// cross-thread contract. Retire it synchronously, explicitly releasing its
	// swapchain before a replacement renderer can claim the target HWND.
	if (!m_activeRendererIsDirectShow)
	{
		rendererToDestroy->Retire();
		m_rendererTransitionWindow.KeepOnTop();
		const HRESULT compositionResult =
			m_rendererTransitionWindow.SynchronizeComposition();
		rendererToDestroy.reset();
		DebugLog::Log(
			"Renderer transition: process=%lu generation=%u event=old-surface-retired "
			"renderer=%S target=%p cover=%p mode=alpha-synchronous "
			"composition_sync=0x%08lx",
			GetCurrentProcessId(),
			m_rendererGeneration.load(std::memory_order_acquire),
			static_cast<LPCTSTR>(m_activeRendererName),
			m_rendererTargetHwnd,
			m_rendererTransitionWindow.GetHWND(),
			static_cast<unsigned long>(compositionResult));
		return;
	}

	m_rendererRetirementPending = true;
	m_rendererRetirementToken++;
	m_retiringRendererName = m_activeRendererName;
	m_retiringRendererGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	DebugLog::Log(
		"Renderer retirement queued: process=%lu generation=%u renderer=%S "
		"token=%llu ui_thread=%lu",
		GetCurrentProcessId(), m_retiringRendererGeneration,
		static_cast<LPCTSTR>(m_retiringRendererName),
		static_cast<unsigned long long>(m_rendererRetirementToken),
		GetCurrentThreadId());
	const bool queued = m_rendererRetirementService.Retire(
		std::move(rendererToDestroy), m_rendererRetirementToken,
		GetSafeHwnd(), WM_MESSAGE_RENDERER_RETIRED);
	if (!queued)
		throw std::runtime_error("Renderer retirement service is closed");
}


void CVideoProcessorDlg::RenderGUIClear()
{
	// Renderer group
	m_rendererDetailStringStatic.SetWindowText(TEXT("")) ;

	// Renderer Queue group
	m_rendererVideoFrameQueueSizeText.SetWindowText(TEXT("")) ;
	m_rendererDroppedFrameCountText.SetWindowText(TEXT("")) ;

	// Renderer latency (ms) group
	m_rendererLatencyToVPText.SetWindowText(TEXT("")) ;
	m_rendererLatencyDsLeadText.SetWindowText(TEXT("")) ;
	m_rendererLatencyToDSText.SetWindowText(TEXT("")) ;

	m_windowedVideoWindow.ShowLogo(true);
}


bool CVideoProcessorDlg::ShowRendererTransitionBlack(const char* reason)
{
	// The transition popup can retain input focus/z-order during windowed and
	// fullscreen target changes. Keep the lifecycle state machine intact, but
	// deliberately run transitions without creating an opaque cover window.
	m_rendererTransitionWindow.Hide();
	m_transitionBlackStartTick = GetTickCount64();
	DebugLog::Log(
		"Renderer transition: process=%lu generation=%u event=black-suppressed "
		"reason=%s renderer=%S target=%p",
		GetCurrentProcessId(),
		m_rendererGeneration.load(std::memory_order_acquire),
		reason ? reason : "unknown",
		static_cast<LPCTSTR>(m_activeRendererName),
		m_rendererTargetHwnd);
	return true;
}


void CVideoProcessorDlg::OnRendererRestartRequired()
{
	m_postRendererStartRequiresGraph = false;
	m_wantToRestartRenderer = true;
	UpdateState();
}


void CVideoProcessorDlg::PauseRendererIngress()
{
	m_rendererIngressState->CloseAdmission();
}


void CVideoProcessorDlg::WaitForRendererIngressDrain()
{
	m_rendererIngressState->WaitForDrain();
}


void CVideoProcessorDlg::ResumeRendererIngress()
{
	assert(m_rendererIngressState->ActiveLeases() == 0);
	const RendererIngressState::CaptureSequenceSnapshot ingress =
		m_rendererIngressState->CaptureSequences();
	// A retained-invalid publication already acknowledges that the existing
	// renderer state remains authoritative. Do not replace that admission
	// acknowledgement with the older renderer-state sequence during a graph
	// reset; doing so would strand ingress until another state notification.
	if (ingress.required != ingress.acknowledged)
	{
		m_rendererIngressState->AcknowledgeCaptureSequence(
			m_rendererCaptureVideoStateNotificationSequence);
	}
	m_rendererIngressState->OpenAdmission();
}


void CVideoProcessorDlg::BindRendererResetSink()
{
	if (!m_videoRenderer || !m_rendererResetCoordinator)
		return;

	RevokeRendererResetSink();
	const RendererResetCoordinator::Binding binding =
		m_rendererResetCoordinator->Bind(
			m_rendererGeneration.load(std::memory_order_acquire));
	m_rendererResetBindingToken = binding.token;
	m_videoRenderer->SetResetRequestSink(binding.sink);
}


void CVideoProcessorDlg::RevokeRendererResetSink()
{
	if (m_rendererResetCoordinator && m_rendererResetBindingToken != 0)
	{
		m_rendererResetCoordinator->Revoke(m_rendererResetBindingToken);
		m_rendererResetBindingToken = 0;
	}
}


void CVideoProcessorDlg::PumpRendererResetMailbox()
{
	if (!m_rendererResetCoordinator)
		return;

	const ULONGLONG now = GetTickCount64();
	const uint32_t currentGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	const RendererTransitionKey expectedTransitionKey =
		m_rendererTransitionModel.Key();
	RendererResetCoordinator::OperationResult completion;
	if (m_rendererResetCoordinator->ConsumeCompletion(
			currentGeneration,
			m_videoRenderer &&
				m_rendererState == RendererState::RENDERSTATE_RENDERING,
			completion,
			expectedTransitionKey.transitionToken,
			expectedTransitionKey.targetRevision,
			m_rendererResetTransitionActive &&
				m_rendererTransitionModel.State() ==
					RendererTransitionState::Resetting))
	{
		RendererTransitionKey transitionKey;
		transitionKey.rendererGeneration =
			completion.rendererGeneration;
		transitionKey.transitionToken =
			completion.transitionToken;
		transitionKey.targetRevision =
			completion.targetRevision;
		const bool currentSuccess =
			completion.succeeded &&
			!completion.staleGeneration &&
			!completion.restartRequired;
		const RendererTransitionModel::Actions actions =
			m_rendererTransitionModel.OnResetCompleted(
				transitionKey, currentSuccess);
		const bool modelAccepted =
			m_rendererTransitionModel.Key() == transitionKey &&
			(currentSuccess ?
				m_rendererTransitionModel.State() ==
					RendererTransitionState::AwaitingFrame :
				m_rendererTransitionModel.State() ==
					RendererTransitionState::FailedCovered);
		DEBUGLOG(
			"Reset %s: operation=%llu request=%llu generation=%u "
			"current_generation=%u reason=%s scope=%s%s%s",
			currentSuccess ? "completed" : "failed",
			static_cast<unsigned long long>(completion.operationId),
			static_cast<unsigned long long>(completion.request.sequence),
			completion.rendererGeneration,
			currentGeneration,
			CStringA(ToString(completion.request.reason)).GetString(),
			ResetScopeName(completion.request.scope),
			completion.failure.empty() ? "" : " failure=",
			completion.failure.empty() ? "" : completion.failure.c_str());
		const bool outputReadinessGraphReprime =
			completion.request.reason == RendererResetReason::OutputReadiness &&
			completion.request.scope == RendererResetScope::Graph;
		if (currentSuccess && m_activeRendererIsDirectShow &&
			outputReadinessGraphReprime)
		{
			RendererLivenessSnapshot snapshot;
			if (m_videoRenderer && m_videoRenderer->GetLivenessSnapshot(snapshot) &&
				snapshot.supported && snapshot.queueEpoch != 0)
			{
				const DisplayTimingSnapshot timing =
					g_displayRefreshRateSampler->GetTimingSnapshot();
				m_outputReadinessResetCompletedGeneration =
					(static_cast<uint64_t>(m_transitionGeneration) << 32) ^
					(timing.generation & 0xffffffffULL);
				m_outputReadinessResetCompletedEpoch = snapshot.queueEpoch;
				DebugLog::Log(
					"Output readiness reset completed: generation=%llu epoch=%llu "
					"converted=%zu/%zu reserve=%zu",
					static_cast<unsigned long long>(
						m_outputReadinessResetCompletedGeneration),
					static_cast<unsigned long long>(snapshot.queueEpoch),
					snapshot.convertedQueueDepth, snapshot.queueCapacity,
					snapshot.deliveryReserveFrames);
			}
			else
			{
				DebugLog::Log(
					"Output readiness reset completed without a usable VP queue "
					"snapshot; deterministic prefill remains pending");
			}
		}
		if (currentSuccess && m_activeRendererIsDirectShow &&
			completion.request.scope != RendererResetScope::LiveQueue &&
			!outputReadinessGraphReprime)
		{
			// A completed stop/run or retarget transaction may represent a real
			// HDMI re-sync even when the renderer object itself survived. A
			// LiveQueue reset only flushes VP source queues while madVR and the
			// graph keep running, so it intentionally preserves measurement history.
			g_displayRefreshRateSampler->ResetMeasurement();
			DebugLog::Log(
				"Display-rate measurement reset after successful DirectShow graph reset: "
				"operation=%llu reason=%s scope=%s",
				static_cast<unsigned long long>(completion.operationId),
				CStringA(ToString(completion.request.reason)).GetString(),
				ResetScopeName(completion.request.scope));

			RendererLivenessSnapshot snapshot;
			if (m_videoRenderer && m_videoRenderer->GetLivenessSnapshot(snapshot) &&
				snapshot.supported && snapshot.queueEpoch != 0)
			{
				const DisplayTimingSnapshot timing =
					g_displayRefreshRateSampler->GetTimingSnapshot();
				m_outputReadinessExistingGraphResetGeneration =
					(static_cast<uint64_t>(m_transitionGeneration) << 32) ^
					(timing.generation & 0xffffffffULL);
				m_outputReadinessExistingGraphResetEpoch = snapshot.queueEpoch;
				m_outputReadinessExistingGraphReservePublishedEpoch = 0;
				DebugLog::Log(
					"Output readiness will adopt fresh DirectShow graph reset: "
					"generation=%llu epoch=%llu reason=%s",
					static_cast<unsigned long long>(
						m_outputReadinessExistingGraphResetGeneration),
					static_cast<unsigned long long>(snapshot.queueEpoch),
					CStringA(ToString(completion.request.reason)).GetString());
			}
		}
		if (completion.request.reason == RendererResetReason::OutputReadiness)
			m_outputReadinessGraphReprimeActive = false;
		if (currentSuccess && m_activeRendererIsDirectShow &&
			(completion.request.scope == RendererResetScope::Graph ||
			 completion.request.scope == RendererResetScope::GraphRetarget))
		{
			RendererLivenessSnapshot recoverySnapshot;
			if (m_videoRenderer &&
				m_videoRenderer->GetLivenessSnapshot(recoverySnapshot) &&
				recoverySnapshot.supported)
			{
				m_directShowGraphRecoveryAwaitingHealth = true;
				m_directShowGraphRecoveryWasRetarget =
					completion.request.scope == RendererResetScope::GraphRetarget;
				m_directShowRecoveryRebuildRequested = false;
				m_directShowGraphRecoveryGeneration = currentGeneration;
				m_directShowGraphRecoveryEpoch = recoverySnapshot.queueEpoch;
				m_directShowGraphRecoveryStartedTick = now;
				DebugLog::Log(
					"DirectShow graph recovery awaiting health: generation=%u "
					"epoch=%llu reason=%s",
					currentGeneration,
					static_cast<unsigned long long>(
						recoverySnapshot.queueEpoch),
					CStringA(ToString(completion.request.reason)).GetString());
			}
		}
		if (currentSuccess && m_activeRendererIsDirectShow &&
			completion.request.scope == RendererResetScope::GraphRetarget)
		{
			// RetargetWindowWithIngressDrain performs the HWND transaction and an
			// immediate graph re-prime, but madVR's new windowed/exclusive
			// presentation path can settle later.  Preserve the long-proven,
			// configurable delayed queue flush as a second, LiveQueue-only phase.
			// This is deliberately timing-method agnostic: Rational and Clock
			// modes exhibit the same downstream transition race.
			const UINT delayMs = static_cast<UINT>(
				m_queueResetDelaySeconds * 1000);
			RequestRendererReset(
				RendererResetReason::DisplayTransition, false, delayMs);
			DebugLog::Log(
				"Post-retarget queue re-prime armed: generation=%u "
				"delay=%ums source=reset_after_render_restart_seconds",
				currentGeneration, delayMs);
		}
		if (completion.request.scope != RendererResetScope::LiveQueue)
			m_lastLivenessRecoveryTick = now;
		m_consecutiveStuckSeconds = 0;
		m_activeGraphRequestId.store(0, std::memory_order_release);
		m_activeGraphRequestGeneration.store(0, std::memory_order_release);
		m_activeGraphRequestStartedTick.store(0, std::memory_order_release);
		if (completion.request.scope ==
				RendererResetScope::GraphRetarget &&
			!currentSuccess)
		{
			const bool targetInvalid =
				!m_fullscreenRetargetTargetHwnd ||
				!IsWindow(m_fullscreenRetargetTargetHwnd);
			if (targetInvalid)
			{
				// The graph-owner operation could not safely target this HWND.
				// Restore the known-valid previous cover target before teardown.
				ClearFullscreenRetarget(true);
			}
			else
			{
				// The graph may still be attached to either leased HWND when
				// retarget or rollback fails. Keep the existing cover and both
				// hosts pinned until terminal renderer teardown.
				DebugLog::Log(
					"Fullscreen retarget failure remains covered through "
					"renderer teardown: target=%p previous=%p",
					m_fullscreenRetargetTargetHwnd,
					m_fullscreenRetargetPreviousTargetHwnd);
			}
		}
		if (!modelAccepted || !currentSuccess || !actions.empty())
			m_wantToRestartRenderer = true;
		UpdateState();
	}

	RendererResetCoordinator::SelectedReset selected;
	if (m_rendererResetCoordinator->DrainReady(now, selected))
	{
		if (selected.rendererGeneration != currentGeneration ||
			!m_videoRenderer ||
			m_rendererState != RendererState::RENDERSTATE_RENDERING)
		{
			DEBUGLOG(
				"Backend reset request ignored: requested_generation=%u "
				"current_generation=%u renderer=%S state=%d token=%llu",
				selected.rendererGeneration,
				currentGeneration,
				static_cast<LPCTSTR>(m_activeRendererName),
				static_cast<int>(m_rendererState),
				static_cast<unsigned long long>(
					selected.request.bindingToken));
			m_rendererResetCoordinator->RejectBlackAndRequireRestart(
				selected, "renderer selection is no longer usable");
			return;
		}

		RendererTransitionModel::Actions transitionActions;
		if (m_rendererTransitionModel.State() ==
			RendererTransitionState::AwaitingFrame)
		{
			transitionActions =
				m_rendererTransitionModel.RequestAnotherReset(
					m_rendererTransitionModel.Key());
		}
		else if (m_rendererTransitionModel.State() ==
			RendererTransitionState::BlackHeld &&
			m_rendererResetTransitionActive)
		{
			transitionActions =
				m_rendererTransitionModel.OnShieldTargetRebound(
					m_rendererTransitionModel.Key(), true);
		}
		else
		{
			transitionActions = m_rendererTransitionModel.BeginReset(
				currentGeneration, m_rendererTargetRevision);
			m_rendererResetTransitionActive =
				!transitionActions.empty();
			const RendererTransitionKey key =
				m_rendererTransitionModel.Key();
			const bool blackShown =
				!transitionActions.empty() &&
				ShowRendererTransitionBlack(
					selected.request.scope ==
						RendererResetScope::LiveQueue ?
						"live-queue-reset" :
					selected.request.scope ==
						RendererResetScope::GraphRetarget ?
						"fullscreen-retarget" : "graph-reset");
			transitionActions =
				m_rendererTransitionModel.OnShieldAcquired(
					key, blackShown);
			if (!blackShown)
			{
				m_rendererResetCoordinator->
					RejectBlackAndRequireRestart(
						selected, "transition black unavailable");
				return;
			}
		}

		const bool executeReset =
			std::any_of(
				transitionActions.begin(),
				transitionActions.end(),
				[](const RendererTransitionAction& action)
				{
					return action.type ==
						RendererTransitionActionType::ExecuteReset;
				});
		if (!executeReset)
		{
			m_rendererResetCoordinator->RejectBlackAndRequireRestart(
				selected, "transition model rejected reset");
			return;
		}

		m_transitionRevealPosted.store(false, std::memory_order_release);
		m_queueResetIgnoreEventsUntil = now + 10000;
		const RendererTransitionKey transitionKey =
			m_rendererTransitionModel.Key();
		selected.transitionToken = transitionKey.transitionToken;
		selected.targetRevision = transitionKey.targetRevision;
		const RendererResetCoordinator::StartResult start =
			m_rendererResetCoordinator->AcknowledgeBlackAndStart(
				selected, m_videoRenderer);
		if (start != RendererResetCoordinator::StartResult::Started)
		{
			m_wantToRestartRenderer = true;
			UpdateState();
			return;
		}
		m_rendererTransitionModel.OnResetStarted(
			m_rendererTransitionModel.Key());
		m_activeGraphRequestId.store(
			selected.request.sequence, std::memory_order_release);
		m_activeGraphRequestGeneration.store(
			currentGeneration, std::memory_order_release);
		m_activeGraphRequestStartedTick.store(
			now, std::memory_order_release);
		DEBUGLOG(
			"Reset started: request=%llu renderer=%S generation=%u "
			"reason=%s scope=%s",
			static_cast<unsigned long long>(selected.request.sequence),
			static_cast<LPCTSTR>(m_activeRendererName),
			currentGeneration,
			CStringA(ToString(selected.request.reason)).GetString(),
			ResetScopeName(selected.request.scope));
	}

	const RendererResetCoordinator::Diagnostics diagnostics =
		m_rendererResetCoordinator->GetDiagnostics();
	if (diagnostics.hasPending)
	{
		const uint64_t delay =
			diagnostics.pendingDeadlineTick > now ?
			diagnostics.pendingDeadlineTick - now : 1;
		SetTimer(
			RENDERER_RESET_MAILBOX_TIMER_ID,
			static_cast<UINT>(std::min<uint64_t>(
				delay, (std::numeric_limits<UINT>::max)())),
			nullptr);
	}
}


void CVideoProcessorDlg::TryRevealRendererTransition(uint32_t generation)
{
	bool resetBlocksReveal =
		m_rendererResetCoordinator &&
		m_rendererResetCoordinator->BlocksReveal(generation);
	// Delayed post-start and post-retarget re-primes must not keep the newly
	// live renderer black while their deadlines are pending; only the reset
	// transaction itself should cover the output when it begins.
	if (resetBlocksReveal)
	{
		const RendererResetCoordinator::Diagnostics diagnostics =
			m_rendererResetCoordinator->GetDiagnostics();
		const bool delayedPostStart =
			diagnostics.pendingReason ==
				RendererResetReason::PostRendererStart;
		const bool delayedPostRetarget =
			diagnostics.pendingReason ==
				RendererResetReason::DisplayTransition &&
			diagnostics.pendingScope == RendererResetScope::LiveQueue;
		if (diagnostics.hasPending &&
			!diagnostics.selectionPrepared &&
			!diagnostics.operationActive &&
			(delayedPostStart || delayedPostRetarget))
		{
			resetBlocksReveal = false;
		}
	}
	const bool currentFrameReady =
		generation == m_transitionGeneration &&
		generation == m_rendererGeneration.load(std::memory_order_acquire) &&
		m_videoRenderer &&
		m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		m_videoRenderer->HasPresentedLiveFrame();
	if (generation != m_transitionGeneration ||
		generation != m_rendererGeneration.load(std::memory_order_acquire) ||
		!m_videoRenderer ||
		m_rendererState != RendererState::RENDERSTATE_RENDERING ||
		resetBlocksReveal ||
		!currentFrameReady)
	{
		return;
	}

	const bool coordinatedReset = m_rendererResetTransitionActive;
	// The UI timer also probes for a first frame. Once the shield has already
	// been released there is no transition work left to perform; in particular,
	// do not synchronize DWM and log another reveal on every timer tick.
	if (!m_rendererTransitionWindow.IsVisible() &&
		!m_fullscreenRetargetPending && !coordinatedReset)
	{
		return;
	}
	if (m_fullscreenRetargetPending)
	{
		const bool desiredFullscreen =
			m_rendererFullscreenCheck.GetCheck() != FALSE;
		const bool targetValid =
			m_fullscreenRetargetTargetHwnd &&
			IsWindow(m_fullscreenRetargetTargetHwnd);
		const bool generationMatches =
			m_fullscreenRetargetRendererGeneration == generation;
		if (!targetValid || !generationMatches)
		{
			DebugLog::Log(
				"Fullscreen retarget reveal rejected before frame acceptance: "
				"target=%p target_valid=%d operation_generation=%u "
				"current_generation=%u action=covered-full-rebuild",
				m_fullscreenRetargetTargetHwnd,
				targetValid ? 1 : 0,
				m_fullscreenRetargetRendererGeneration,
				generation);
			ClearFullscreenRetarget(true);
			m_wantToRestartRenderer = true;
			UpdateState();
			return;
		}
		if (FullscreenRetargetRequiresCoveredRebuild(
			m_fullscreenRetargetExiting, desiredFullscreen))
		{
			// The user reversed direction while the graph-owner transaction was
			// active. Do not expose the superseded target. Keep both HWNDs leased
			// until covered renderer teardown detaches the graph. Commands received
			// during the retarget were coalesced, so this is one rebuild toward the
			// final requested state rather than an interrupting retry.
			DebugLog::Log(
				"Fullscreen retarget superseded before reveal: "
				"completed_direction=%s desired_fullscreen=%d "
				"action=coalesced-covered-full-rebuild",
				m_fullscreenRetargetExiting ? "exit" : "enter",
				desiredFullscreen ? 1 : 0);
			m_wantToRestartRenderer = true;
			UpdateState();
			return;
		}
	}
	if (coordinatedReset)
	{
		if (m_rendererTransitionModel.State() !=
				RendererTransitionState::AwaitingFrame ||
			m_rendererTransitionModel.Key().rendererGeneration !=
				generation)
		{
			return;
		}
		const RendererTransitionModel::Actions actions =
			m_rendererTransitionModel.OnFrameReady(
				m_rendererTransitionModel.Key());
		if (actions.empty() ||
			actions.front().type !=
				RendererTransitionActionType::ReleaseShield)
		{
			return;
		}
	}

	const char* evidence = m_videoRenderer->PresentedLiveFrameEvidence();
	if (m_fullscreenRetargetPending)
	{
		const bool exitingFullscreen = m_fullscreenRetargetExiting;
		const HWND completedTarget = m_fullscreenRetargetTargetHwnd;
		m_rendererTargetHwnd = completedTarget;
		if (exitingFullscreen)
			++m_rendererTargetRevision;
		DebugLog::Log(
			"Fullscreen retarget ready to reveal: renderer=%S generation=%u "
			"direction=%s target=%p total_ms=%llu",
			static_cast<LPCTSTR>(m_activeRendererName),
			generation,
			exitingFullscreen ? "exit" : "enter",
			completedTarget,
			static_cast<unsigned long long>(
				m_fullscreenRetargetStartTick > 0 ?
					GetTickCount64() -
						m_fullscreenRetargetStartTick : 0));
		ClearFullscreenRetarget(false);
		if (exitingFullscreen && m_fullScreenVideoWindow)
			FullScreenVideoWindowDestroy();
		if (exitingFullscreen)
			videoProcessorApp.RestoreDisplayTopology("fullscreen-off");
	}
	// The renderer's successful submission can still be queued behind the
	// compositor. Keep black above it through one composition boundary so
	// hiding the cover cannot briefly expose the retired surface.
	m_rendererTransitionWindow.KeepOnTop();
	const ULONGLONG compositionSyncStart = GetTickCount64();
	const HRESULT compositionSyncResult =
		m_rendererTransitionWindow.SynchronizeComposition();
	const ULONGLONG compositionSyncMs =
		GetTickCount64() - compositionSyncStart;
	const uint64_t blackDurationMs =
		m_transitionBlackStartTick > 0
			? GetTickCount64() - m_transitionBlackStartTick
			: 0;
	m_windowedVideoWindow.ShowLogo(false);
	m_rendererTransitionWindow.Hide();
	if (coordinatedReset)
	{
		const RendererTransitionModel::Actions actions =
			m_rendererTransitionModel.OnShieldReleased(
				m_rendererTransitionModel.Key(),
				!m_rendererTransitionWindow.IsVisible());
		if (!actions.empty())
		{
			m_wantToRestartRenderer = true;
			UpdateState();
		}
		else if (m_rendererTransitionModel.State() ==
			RendererTransitionState::Visible)
		{
			m_rendererResetTransitionActive = false;
		}
	}
	DebugLog::Log(
		"Renderer transition: process=%lu generation=%u event=first-live-frame-reveal "
		"renderer=%S target=%p evidence=%s black_ms=%llu "
		"composition_sync=0x%08lx composition_sync_ms=%llu",
		GetCurrentProcessId(),
		generation,
		static_cast<LPCTSTR>(m_activeRendererName),
		m_rendererTargetHwnd,
		evidence ? evidence : "unknown",
		static_cast<unsigned long long>(blackDurationMs),
		static_cast<unsigned long>(compositionSyncResult),
		static_cast<unsigned long long>(compositionSyncMs));
}


void CVideoProcessorDlg::ApplyNoUiLayout()
{
	if (!m_windowedVideoWindow.GetSafeHwnd())
		return;

	CRect videoScreenRect;
	m_windowedVideoWindow.GetWindowRect(&videoScreenRect);

	for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
	{
		if (child->GetSafeHwnd() != m_windowedVideoWindow.GetSafeHwnd())
			child->ShowWindow(SW_HIDE);
	}

	const int videoWidth = videoScreenRect.Width();
	const int videoHeight = videoScreenRect.Height();

	CRect adjustedWindowRect(0, 0, videoWidth, videoHeight);
	AdjustWindowRectEx(
		&adjustedWindowRect,
		static_cast<DWORD>(GetWindowLongPtr(GetSafeHwnd(), GWL_STYLE)),
		FALSE,
		static_cast<DWORD>(GetWindowLongPtr(GetSafeHwnd(), GWL_EXSTYLE)));

	SetWindowPos(
		nullptr,
		videoScreenRect.left + adjustedWindowRect.left,
		videoScreenRect.top + adjustedWindowRect.top,
		adjustedWindowRect.Width(),
		adjustedWindowRect.Height(),
		SWP_NOZORDER | SWP_NOACTIVATE);

	m_windowedVideoWindow.ShowWindow(SW_SHOW);
	m_windowedVideoWindow.MoveWindow(0, 0, videoWidth, videoHeight, TRUE);
}


void CVideoProcessorDlg::FullScreenVideoWindowConstruct()
{
	assert(!m_fullScreenVideoWindow);

	HMONITOR hmon = SelectFullscreenMonitor();

	m_fullScreenVideoWindow = new FullscreenVideoWindow();
	if (!m_fullScreenVideoWindow)
		FatalError(TEXT("Failed to create full screen renderer window"));
	if (m_windowedFullScreenMode == false)
		m_fullScreenVideoWindow->Create(hmon, this->GetSafeHwnd());
	if (m_windowedFullScreenMode == true)
		m_fullScreenVideoWindow->CreateWindowedFullscreen(hmon, this->GetSafeHwnd());

	const HWND fullscreenHwnd = m_fullScreenVideoWindow->GetHWND();
	HMONITOR actualMonitor = MonitorFromWindow(
		fullscreenHwnd, MONITOR_DEFAULTTONULL);
	if (actualMonitor != hmon)
	{
		MONITORINFO monitorInfo = { sizeof(monitorInfo) };
		if (GetMonitorInfo(hmon, &monitorInfo))
		{
			const RECT& rect = monitorInfo.rcMonitor;
			const BOOL moved = ::SetWindowPos(fullscreenHwnd, nullptr,
				rect.left, rect.top, rect.right - rect.left,
				rect.bottom - rect.top,
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER |
				SWP_SHOWWINDOW);
			actualMonitor = MonitorFromWindow(
				fullscreenHwnd, MONITOR_DEFAULTTONULL);
			DebugLog::Log(
				"Fullscreen monitor placement correction: requested=%p before=%p moved=%d after=%p",
				reinterpret_cast<void*>(hmon),
				reinterpret_cast<void*>(MonitorFromWindow(
					this->GetSafeHwnd(), MONITOR_DEFAULTTONEAREST)),
				moved ? 1 : 0, reinterpret_cast<void*>(actualMonitor));
		}
	}
	DebugLog::Log(
		"Fullscreen monitor placement verified: requested=%p actual=%p matched=%d",
		reinterpret_cast<void*>(hmon), reinterpret_cast<void*>(actualMonitor),
		actualMonitor == hmon ? 1 : 0);

	SetTimer(FULLSCREEN_FOCUS_TIMER_ID, 5000, nullptr);

}

HMONITOR CVideoProcessorDlg::SelectFullscreenMonitor()
{
	const HMONITOR fallback = MonitorFromWindow(
		this->GetSafeHwnd(), MONITOR_DEFAULTTONEAREST);
	if (m_fullscreenMonitorName.IsEmpty())
		return fallback;

	std::vector<ActiveMonitorCandidate> candidates;
	if (!EnumDisplayMonitors(nullptr, nullptr, EnumerateActiveMonitor,
		reinterpret_cast<LPARAM>(&candidates)) ||
		!PopulateActiveMonitorFriendlyNames(candidates))
	{
		const ULONGLONG now = GetTickCount64();
		if (m_lastFullscreenMonitorSelectionLogTick == 0 ||
			now - m_lastFullscreenMonitorSelectionLogTick >= 5000)
		{
			m_lastFullscreenMonitorSelectionLogTick = now;
			DebugLog::Log(
				"Fullscreen monitor selection: requested='%S' fallback=existing reason=configured monitor unavailable (active monitor enumeration failed)",
				m_fullscreenMonitorName.GetString());
		}
		return fallback;
	}

	std::vector<const ActiveMonitorCandidate*> matches;
	for (const ActiveMonitorCandidate& candidate : candidates)
		if (!candidate.friendlyName.IsEmpty() &&
			_wcsicmp(candidate.friendlyName, m_fullscreenMonitorName) == 0)
			matches.push_back(&candidate);

	const ULONGLONG now = GetTickCount64();
	if (m_lastFullscreenMonitorSelectionLogTick == 0 ||
		now - m_lastFullscreenMonitorSelectionLogTick >= 5000)
	{
		m_lastFullscreenMonitorSelectionLogTick = now;
		const CString candidateDescription = DescribeMonitorCandidates(candidates);
		if (matches.size() == 1)
			DebugLog::Log(
				"Fullscreen monitor selection: requested='%S' candidates=[%S] selected=%S (%S)",
				m_fullscreenMonitorName.GetString(), candidateDescription.GetString(),
				matches.front()->sourceName.GetString(), matches.front()->friendlyName.GetString());
		else
			DebugLog::Log(
				"Fullscreen monitor selection: requested='%S' candidates=[%S] fallback=existing reason=%s",
				m_fullscreenMonitorName.GetString(), candidateDescription.GetString(),
				matches.empty() ? "configured monitor unavailable" :
				"configured monitor name is ambiguous");
	}

	return matches.size() == 1 ? matches.front()->monitor : fallback;
}


void CVideoProcessorDlg::FullScreenVideoWindowDestroy()
{
	assert(m_fullScreenVideoWindow);
	assert(!m_fullscreenRetargetPending);
	if (m_fullscreenRetargetPending)
		return;
	if (m_rendererTargetHwnd == m_fullScreenVideoWindow->GetHWND())
		m_rendererTargetHwnd = nullptr;
	delete m_fullScreenVideoWindow;
	m_fullScreenVideoWindow = nullptr;
}


bool CVideoProcessorDlg::TryStartFullscreenRetarget()
{
	if (!m_videoRenderer ||
		m_rendererState != RendererState::RENDERSTATE_RENDERING ||
		!m_rendererResetCoordinator ||
		m_rendererRetirementPending ||
		m_fullscreenRetargetPending ||
		m_wantToRestartRenderer ||
		m_wantToTerminate ||
		RendererResetOperationInProgress() ||
		!m_activeRendererIsDirectShow ||
		m_activeRendererName.CompareNoCase(
			TEXT("DirectShow - madVR")) != 0)
	{
		return false;
	}
	const RendererResetCoordinator::Diagnostics diagnostics =
		m_rendererResetCoordinator->GetDiagnostics();
	if (diagnostics.hasPending ||
		diagnostics.selectionPrepared ||
		diagnostics.operationActive ||
		diagnostics.completionPending ||
		diagnostics.restartCoverRequired)
	{
		return false;
	}

	const bool enteringFullscreen =
		m_rendererFullscreenCheck.GetCheck() != FALSE;
	HWND targetHwnd = nullptr;
	const HWND previousTarget = m_rendererTargetHwnd;
	const uint64_t previousRevision = m_rendererTargetRevision;
	if (enteringFullscreen)
	{
		if (!m_fullScreenVideoWindow)
			FullScreenVideoWindowConstruct();
		targetHwnd = m_fullScreenVideoWindow ?
			m_fullScreenVideoWindow->GetHWND() : nullptr;
	}
	else
	{
		targetHwnd = m_windowedVideoWindow.GetSafeHwnd();
	}
	if (!targetHwnd || !IsWindow(targetHwnd) ||
		targetHwnd == previousTarget)
	{
		return false;
	}

	// Entering fullscreen must acquire black over the new top-level host before
	// madVR is retargeted. Exiting keeps the cover over the old fullscreen host
	// until the windowed target has produced current-epoch preroll.
	if (enteringFullscreen)
	{
		m_rendererTargetHwnd = targetHwnd;
		++m_rendererTargetRevision;
	}
	m_fullscreenRetargetPending = true;
	m_fullscreenRetargetTargetHwnd = targetHwnd;
	m_fullscreenRetargetPreviousTargetHwnd = previousTarget;
	m_fullscreenRetargetPreviousTargetRevision = previousRevision;
	m_fullscreenRetargetRendererGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	m_fullscreenRetargetExiting = !enteringFullscreen;
	m_fullscreenRetargetStartTick = GetTickCount64();

	const bool accepted = m_rendererResetCoordinator->RequestUi(
		RendererResetReason::DisplayTransition,
		RendererResetScope::GraphRetarget,
		0, 0, reinterpret_cast<uintptr_t>(targetHwnd));
	if (!accepted)
	{
		m_rendererTargetHwnd = previousTarget;
		m_rendererTargetRevision = previousRevision;
		ClearFullscreenRetarget(false);
		if (enteringFullscreen && m_fullScreenVideoWindow)
			FullScreenVideoWindowDestroy();
		return false;
	}
	if (!enteringFullscreen)
	{
		// Keep the old HWND leased for rollback, but do not leave its topmost
		// fullscreen surface occluding the new windowed presentation target.
		// Otherwise madVR can accept a small preroll and then block Receive while
		// VP waits for that same Receive to prove the new target ready.
		m_rendererTargetHwnd = targetHwnd;
		++m_rendererTargetRevision;
		if (m_fullScreenVideoWindow &&
			IsWindow(m_fullScreenVideoWindow->GetHWND()))
		{
			::ShowWindow(m_fullScreenVideoWindow->GetHWND(), SW_HIDE);
		}
		DebugLog::Log(
			"Fullscreen retarget target exposed: direction=exit old=%p new=%p "
			"old_visible=%d new_visible=%d target_revision=%llu",
			previousTarget, targetHwnd,
			previousTarget && ::IsWindowVisible(previousTarget) ? 1 : 0,
			::IsWindowVisible(targetHwnd) ? 1 : 0,
			static_cast<unsigned long long>(m_rendererTargetRevision));
	}

	RECT oldClient = {};
	RECT newClient = {};
	if (previousTarget && IsWindow(previousTarget))
		::GetClientRect(previousTarget, &oldClient);
	::GetClientRect(targetHwnd, &newClient);
	DebugLog::Log(
		"Fullscreen retarget requested: renderer=%S generation=%u "
		"direction=%s old=%p new=%p target_revision=%llu "
		"old_visible=%d old_client=%ldx%ld old_parent=%p "
		"new_visible=%d new_client=%ldx%ld new_parent=%p",
		static_cast<LPCTSTR>(m_activeRendererName),
		m_rendererGeneration.load(std::memory_order_acquire),
		enteringFullscreen ? "enter" : "exit",
		previousTarget, targetHwnd,
		static_cast<unsigned long long>(m_rendererTargetRevision),
		previousTarget && ::IsWindowVisible(previousTarget) ? 1 : 0,
		oldClient.right - oldClient.left,
		oldClient.bottom - oldClient.top,
		previousTarget ? ::GetParent(previousTarget) : nullptr,
		::IsWindowVisible(targetHwnd) ? 1 : 0,
		newClient.right - newClient.left,
		newClient.bottom - newClient.top,
		::GetParent(targetHwnd));
	m_rendererFullscreenCheck.EnableWindow(FALSE);
	m_fullScreenModeCombo.EnableWindow(FALSE);
	return true;
}


void CVideoProcessorDlg::ClearFullscreenRetarget(bool restorePreviousTarget)
{
	if (restorePreviousTarget &&
		m_fullscreenRetargetPreviousTargetHwnd &&
		IsWindow(m_fullscreenRetargetPreviousTargetHwnd))
	{
		m_rendererTargetHwnd = m_fullscreenRetargetPreviousTargetHwnd;
		m_rendererTargetRevision =
			m_fullscreenRetargetPreviousTargetRevision;
		if (m_fullscreenRetargetExiting)
			::ShowWindow(m_fullscreenRetargetPreviousTargetHwnd, SW_SHOW);
	}
	m_fullscreenRetargetPending = false;
	m_fullscreenRetargetTargetHwnd = nullptr;
	m_fullscreenRetargetPreviousTargetHwnd = nullptr;
	m_fullscreenRetargetPreviousTargetRevision = 0;
	m_fullscreenRetargetRendererGeneration = 0;
	m_fullscreenRetargetExiting = false;
	m_fullscreenRetargetStartTick = 0;
	m_rendererFullscreenCheck.EnableWindow(TRUE);
	m_fullScreenModeCombo.EnableWindow(TRUE);
}


HWND CVideoProcessorDlg::GetRenderWindow()
{
	if (m_rendererFullscreenCheck.GetCheck())
	{
		// If we don't have a full screen window yet make one
		if (!m_fullScreenVideoWindow)
			FullScreenVideoWindowConstruct();

		assert(IsWindow(m_fullScreenVideoWindow->GetHWND()));
		return m_fullScreenVideoWindow->GetHWND();
	}

	assert(!m_fullScreenVideoWindow);
	assert(IsWindow(m_windowedVideoWindow));
	return m_windowedVideoWindow;
}


size_t CVideoProcessorDlg::GetRendererVideoFrameQueueSizeMax()
{
	// Note that this field is marked as numbers only so guaranteed to convert corrtectly

	CString text;
	m_rendererVideoFrameQueueSizeMaxEdit.GetWindowText(text);
	return _ttoi(text);
}


bool CVideoProcessorDlg::GetRendererVideoFrameUseQueue()
{
	return m_rendererVideoFrameUseQeueueCheck.GetCheck();
}


double CVideoProcessorDlg::GetWindowTextAsDouble(CEdit& edit)
{
	CString text;
	edit.GetWindowText(text);
	return _wtof(text);
}

std::vector<int> m_frame_offsets_by_refresh;

std::vector<int> CVideoProcessorDlg::GetFrameOffsetByRefresh() {
	return m_frameOffsetsByRefresh;
}

void CVideoProcessorDlg::SetFrameOffsetByRefresh(std::vector<int> offsets) {
	m_frameOffsetsByRefresh = offsets;
}


void CVideoProcessorDlg::UpdateTimingClockFrameOffsetAvailability()
{
	const bool alphaSelected = IsAlphaRendererSelected();
	if (alphaSelected && !m_alphaFrameOffsetDisabled)
	{
		// Alpha's FIFO is not timestamp-scheduled. Preserve the DirectShow value
		// for a later backend switch, but force the capture clock to its neutral
		// offset while Alpha owns the renderer.
		m_directShowFrameOffsetMs = GetTimingClockFrameOffsetMs();
		SetTimingClockFrameOffsetMs(0);
		m_alphaFrameOffsetDisabled = true;
		DebugLog::Log("Alpha frame offset disabled; preserved DirectShow value=%d ms",
			m_directShowFrameOffsetMs);
	}
	else if (!alphaSelected && m_alphaFrameOffsetDisabled)
	{
		SetTimingClockFrameOffsetMs(m_directShowFrameOffsetMs);
		m_alphaFrameOffsetDisabled = false;
		DebugLog::Log("DirectShow frame offset restored: %d ms",
			m_directShowFrameOffsetMs);
	}

	m_timingClockFrameOffsetAutoCheck.EnableWindow(!alphaSelected);
	const bool autoOffset =
		m_timingClockFrameOffsetAutoCheck.GetCheck() == BST_CHECKED;
	m_timingClockFrameOffsetEdit.EnableWindow(!alphaSelected && !autoOffset);
	for (const UINT controlId : { IDC_STATIC_TIMING_CLOCK_FRAME_OFFSET_LABEL,
		IDC_STATIC_TIMING_CLOCK_FRAME_OFFSET_MS })
	{
		if (CWnd* label = GetDlgItem(controlId))
			label->EnableWindow(!alphaSelected);
	}
	if (m_captureDevice)
		m_captureDevice->SetFrameOffsetMs(alphaSelected ? 0 :
			GetTimingClockFrameOffsetMs());
}


int CVideoProcessorDlg::GetTimingClockFrameOffsetMs()
{
	CString text;
	m_timingClockFrameOffsetEdit.GetWindowText(text);

	// Reading the model must not rewrite the edit control. Periodic stats reads
	// otherwise reset the caret/selection while the user is typing.
	return _ttoi(text);
}


void CVideoProcessorDlg::SetTimingClockFrameOffsetMs(int timingClockFrameOffsetMs)
{
	CString cstring;
	cstring.Format(_T("%i"), timingClockFrameOffsetMs);
	m_timingClockFrameOffsetEdit.SetWindowText(cstring);
}


void CVideoProcessorDlg::UpdateTimingClockFrameOffset()
{
	if (!IsAlphaRendererSelected())
		m_directShowFrameOffsetMs = GetTimingClockFrameOffsetMs();

	if (m_captureDevice) 
		m_captureDevice->SetFrameOffsetMs(IsAlphaRendererSelected() ? 0 :
			GetTimingClockFrameOffsetMs());

	if (m_videoRenderer)
		RequestRendererReset(RendererResetReason::TimingOffsetChange, false, 0);

	DEBUGLOG("Timing clock frame offset changed; requesting live re-prime");

}


void CVideoProcessorDlg::RebuildRendererCombo()
{
	ClearRendererCombo();

	std::vector<RendererId> rendererIds;

	//
	// Get all supported renderer ids
	//

	DirectShowVideoRendererIds(rendererIds);
#if defined(_WIN64)
	if (RendererId::IsLibplaceboAvailable())
		rendererIds.push_back(RendererId::Libplacebo());
#endif

	// Alpha is always first; remaining eligible renderers are shown in the
	// reverse of the previous alphabetical order.
	for (const auto& rendererEntry : RendererId::OrderForDisplay(rendererIds))
	{
		RendererId* id = new RendererId(rendererEntry);

		int comboIndex = m_rendererCombo.AddString(rendererEntry.name);
		m_rendererCombo.SetItemData(comboIndex, reinterpret_cast<DWORD_PTR>(id));
		DEBUGLOG("Renderer order: render.%d = %S",
			comboIndex + 1,
			rendererEntry.name.GetString());

		if (rendererEntry.MatchesConfiguredName(m_defaultRendererName))
			m_rendererCombo.SetCurSel(comboIndex);
	}
}


void CVideoProcessorDlg::ClearRendererCombo()
{
	for (int i = 0; i < m_rendererCombo.GetCount(); i++)
	{
		delete reinterpret_cast<RendererId*>(m_rendererCombo.GetItemData(i));
	}

	m_rendererCombo.ResetContent();
}


bool CVideoProcessorDlg::IsNewLldvModeSelected()
{
	if (m_hdrColorspaceCombo.GetCurSel() < 0 || m_hdrLuminanceCombo.GetCurSel() < 0)
		return false;

	return
		static_cast<HdrColorspaceOptions>(m_hdrColorspaceCombo.GetItemData(m_hdrColorspaceCombo.GetCurSel())) ==
			HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT_LLDV &&
		static_cast<HdrLuminanceOptions>(m_hdrLuminanceCombo.GetItemData(m_hdrLuminanceCombo.GetCurSel())) ==
			HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT_LLDV;
}

bool CVideoProcessorDlg::UpdateNewLldvCandidate()
{
	if (!m_useNewLldvHeuristic || !m_captureDeviceVideoState || !m_captureDeviceVideoState->valid)
	{
		m_newLldvCandidateActive = false;
		m_newLldvCandidateConfirmed = false;
		m_newLldvCandidateSince = 0;
		return false;
	}

	const bool isCandidate =
		m_captureDeviceVideoState->colorspace == ColorSpace::BT_2020 &&
		m_captureDeviceVideoState->eotf == EOTF::SDR &&
		!m_captureDeviceVideoState->hdrData;

	if (!isCandidate)
	{
		if (m_newLldvCandidateActive)
		{
			if (m_newLldvCandidateConfirmed)
			{
				DebugLog::Log(
					"New LLDV heuristic: confirmed LLDV input ended; capture state is now %s / %s",
					CStringA(ToString(m_captureDeviceVideoState->eotf)).GetString(),
					CStringA(ToString(m_captureDeviceVideoState->colorspace)).GetString());
			}
			else
			{
				DebugLog::Log(
					"New LLDV heuristic: BT.2020/SDR candidate cleared before confirmation");
			}
		}

		m_newLldvCandidateActive = false;
		m_newLldvCandidateConfirmed = false;
		m_newLldvCandidateSince = 0;
		return false;
	}

	const DWORD now = GetTickCount();
	if (!m_newLldvCandidateActive)
	{
		m_newLldvCandidateActive = true;
		m_newLldvCandidateSince = now;
		DebugLog::Log("New LLDV heuristic: BT.2020/SDR candidate seen; waiting 1500ms before treating it as LLDV");
		return false;
	}

	if (!m_newLldvCandidateConfirmed && now - m_newLldvCandidateSince >= 1500)
	{
		m_newLldvCandidateConfirmed = true;
		DebugLog::Log("New LLDV heuristic: BT.2020/SDR candidate confirmed after stabilization period");
		return true;
	}

	return false;
}

bool CVideoProcessorDlg::BuildPushVideoState()
{
	const bool hadPreviousEffectiveState =
		m_builtVideoState && m_builtVideoState->valid;
	const EOTF previousEffectiveEotf = hadPreviousEffectiveState
		? m_builtVideoState->eotf
		: EOTF::UNKNOWN;
	const ColorSpace previousEffectiveColorSpace = hadPreviousEffectiveState
		? m_builtVideoState->colorspace
		: ColorSpace::UNKNOWN;

	VideoStateComPtr videoState = new VideoState(*m_captureDeviceVideoState);
	if (!videoState)
		throw std::runtime_error("Failed to alloc VideoStateComPtr");

	//
	// Alterations
	//

	// Legacy behavior: a HDFury LLDV source was identified as BT.2020 + PQ
	// without static HDR metadata.  Keep this path exactly when /newlldv is off.
	const bool isLegacyHDFuryLLDV =
		m_captureDeviceVideoState->colorspace == ColorSpace::BT_2020 &&
		m_captureDeviceVideoState->eotf == EOTF::PQ &&
		!m_captureDeviceVideoState->hdrData;

	// New behavior: DeckLink reports LLDV as BT.2020 + SDR without static HDR
	// metadata.  There is no exposed VSIF to prove this, so only apply the
	// heuristic when explicitly enabled, the user selected both LLDV follow
	// modes, and the candidate has remained stable for 1.5 seconds.
	const bool isHDFuryLLDV = m_useNewLldvHeuristic
		? (m_newLldvCandidateConfirmed &&
			IsNewLldvModeSelected())
		: isLegacyHDFuryLLDV;

	if (m_useNewLldvHeuristic && isHDFuryLLDV)
		videoState->eotf = EOTF::PQ;

	// Change container colorspace
	int i = m_colorspaceContainerCombo.GetCurSel();
	ColorSpace colorSpace = (ColorSpace)m_colorspaceContainerCombo.GetItemData(i);
	if (colorSpace != ColorSpace::UNKNOWN)
		videoState->colorspace = colorSpace;

	// Change HDR primaries if requested
	{
		ColorSpace hdrColorSpace = ColorSpace::UNKNOWN;

		i = m_hdrColorspaceCombo.GetCurSel();
		switch ((HdrColorspaceOptions)m_hdrColorspaceCombo.GetItemData(i))
		{
			// No need to do anything
			case HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT:
				break;

			// Special mode where if there is an input, but there are no HDR settings
			// we force the HDR settings for that colorspace
			case HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT_LLDV:
				if (isHDFuryLLDV)
					hdrColorSpace = videoState->colorspace;
				break;

			// Translate container's colorspace to XY coordinates
			case HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_CONTAINER:
				hdrColorSpace = videoState->colorspace;
				break;

			case HdrColorspaceOptions::HDR_COLORSPACE_BT2020:
				hdrColorSpace = ColorSpace::BT_2020;
				break;

			case HdrColorspaceOptions::HDR_COLORSPACE_P3:
				hdrColorSpace = ColorSpace::P3_D65;  // They're all the same from the XY perspective
				break;

			case HdrColorspaceOptions::HDR_COLORSPACE_REC709:
				hdrColorSpace = ColorSpace::REC_709;
				break;

			default:
				throw std::runtime_error("Unknown HdrColorspaceOptions");
		}

		if (hdrColorSpace != ColorSpace::UNKNOWN)
		{
			if (!videoState->hdrData)
				videoState->hdrData = std::make_shared<HDRData>();

			videoState->hdrData->displayPrimaryRedX = ColorSpaceToCie1931RedX(hdrColorSpace);
			videoState->hdrData->displayPrimaryRedY = ColorSpaceToCie1931RedY(hdrColorSpace);
			videoState->hdrData->displayPrimaryGreenX = ColorSpaceToCie1931GreenX(hdrColorSpace);
			videoState->hdrData->displayPrimaryGreenY = ColorSpaceToCie1931GreenY(hdrColorSpace);
			videoState->hdrData->displayPrimaryBlueX = ColorSpaceToCie1931BlueX(hdrColorSpace);
			videoState->hdrData->displayPrimaryBlueY = ColorSpaceToCie1931BlueY(hdrColorSpace);
			videoState->hdrData->whitePointX = ColorSpaceToCie1931WpX(hdrColorSpace);
			videoState->hdrData->whitePointY = ColorSpaceToCie1931WpY(hdrColorSpace);
		}
	}

	// Change HDR lumiance if available and requested
	i = m_hdrLuminanceCombo.GetCurSel();
	switch ((HdrLuminanceOptions)m_hdrLuminanceCombo.GetItemData(i))
	{
		// No need to do anything
		case HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT:
			break;

		case HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT_LLDV:

			if (isHDFuryLLDV)
			{
				if (!videoState->hdrData)
					videoState->hdrData = std::make_shared<HDRData>();

				const RendererProfileConfig::LldvMetadata lldvDefaults =
					RendererProfileConfig::DefaultLldvMetadata(
						m_useNewLldvHeuristic);
				auto resolveLldvValue = [](double commandLineValue,
					double profileValue, double fallback, bool strictlyPositive)
				{
					const bool hasCommandLineValue = strictlyPositive ?
						commandLineValue > 0.0 : commandLineValue >= 0.0;
					if (hasCommandLineValue)
						return commandLineValue;
					const bool hasProfileValue = strictlyPositive ?
						profileValue > 0.0 : profileValue >= 0.0;
					return hasProfileValue ? profileValue : fallback;
				};
				videoState->hdrData->maxCll = resolveLldvValue(
					m_lldvMaxCllOverride, m_profileLldvMaxCllOverride,
					lldvDefaults.maxCll, false);
				videoState->hdrData->maxFall = resolveLldvValue(
					m_lldvMaxFallOverride, m_profileLldvMaxFallOverride,
					lldvDefaults.maxFall, false);
				videoState->hdrData->masteringDisplayMinLuminance =
					resolveLldvValue(m_lldvMasteringMinLuminanceOverride,
						m_profileLldvMasteringMinLuminanceOverride,
						lldvDefaults.masteringMinLuminance, false);
				videoState->hdrData->masteringDisplayMaxLuminance =
					resolveLldvValue(m_lldvMasteringMaxLuminanceOverride,
						m_profileLldvMasteringMaxLuminanceOverride,
						lldvDefaults.masteringMaxLuminance, true);
			}
			break;


		// Take what the user has inputted
		case HdrLuminanceOptions::HDR_LUMINANCE_USER:

			if (!videoState->hdrData)
				videoState->hdrData = std::make_shared<HDRData>();

			videoState->hdrData->maxCll = GetWindowTextAsDouble(m_hdrLuminanceMaxCll);
			videoState->hdrData->maxFall = GetWindowTextAsDouble(m_hdrLuminanceMaxFall);
			videoState->hdrData->masteringDisplayMinLuminance = GetWindowTextAsDouble(m_hdrLuminanceMasterMin);
			videoState->hdrData->masteringDisplayMaxLuminance = GetWindowTextAsDouble(m_hdrLuminanceMasterMax);
			break;

		default:
			throw std::runtime_error("Unknown HdrLuminanceOptions");
	}

	m_builtVideoState = videoState;
	m_lastEffectiveEotf = m_builtVideoState->eotf;

	UnifiedProfileRuntime::RefreshResult profileRefresh;
	std::string profileError;
	if (m_profileRuntime.IsInitialized() &&
		!m_profileRuntime.Refresh(GetUnifiedProfileSourceLookup(),
			profileRefresh, profileError))
	{
		DebugLog::Log("Unified profile refresh failed: %s",
			profileError.c_str());
	}
	else if (profileRefresh.changed &&
		m_rendererState != RendererState::RENDERSTATE_STOPPING)
	{
		ApplyUnifiedProfileSnapshot(profileRefresh.snapshot, true);
		ScheduleUnifiedProfileActions(profileRefresh.actions);
	}
	

	//
	// GUI
	//

	if (videoState->valid)
	{
		m_videoValidText.SetWindowText(_T("Yes"));
		m_videoDisplayModeText.SetWindowText(videoState->displayMode->ToString());
		m_videoPixelFormatText.SetWindowText(ToString(videoState->videoFrameEncoding));
		m_videoEotfText.SetWindowText(ToString(videoState->eotf));
		m_videoColorSpaceText.SetWindowText(ToString(videoState->colorspace));
	}
	else
	{
		m_videoValidText.SetWindowText(_T("No"));
		m_videoDisplayModeText.SetWindowText(_T(""));
		m_videoPixelFormatText.SetWindowText(_T(""));
		m_videoEotfText.SetWindowText(_T(""));
		m_videoColorSpaceText.SetWindowText(_T(""));
	}

	if (videoState->valid)
	{
		m_colorspaceCie1931xy.SetColorSpace(videoState->colorspace);
	}
	else
	{
		m_colorspaceCie1931xy.SetColorSpace(ColorSpace::UNKNOWN);
	}

	m_colorspaceCie1931xy.SetHDRData(videoState->hdrData);

	if (videoState->valid && videoState->hdrData)
	{
		const HDRData hdrData = *(videoState->hdrData);

		CString cstring;
		cstring.Format(_T("%.01f"), hdrData.maxCll);
		m_hdrLuminanceMaxCll.SetWindowText(cstring);

		cstring.Format(_T("%.01f"), hdrData.maxFall);
		m_hdrLuminanceMaxFall.SetWindowText(cstring);

		cstring.Format(_T("%.05f"), hdrData.masteringDisplayMinLuminance);
		m_hdrLuminanceMasterMin.SetWindowText(cstring);

		cstring.Format(_T("%.01f"), hdrData.masteringDisplayMaxLuminance);
		m_hdrLuminanceMasterMax.SetWindowText(cstring);

		m_hdrColorspaceREdit.SetWindowTextW(
			CieXYToString(hdrData.displayPrimaryRedX, hdrData.displayPrimaryRedY));

		m_hdrColorspaceGEdit.SetWindowTextW(
			CieXYToString(hdrData.displayPrimaryGreenX, hdrData.displayPrimaryGreenY));

		m_hdrColorspaceBEdit.SetWindowTextW(
			CieXYToString(hdrData.displayPrimaryBlueX, hdrData.displayPrimaryBlueY));

		m_hdrColorspaceWPEdit.SetWindowTextW(
			CieXYToString(hdrData.whitePointX, hdrData.whitePointY));
	}
	else
	{
		m_hdrLuminanceMaxCll.SetWindowText(_T(""));
		m_hdrLuminanceMaxFall.SetWindowText(_T(""));
		m_hdrLuminanceMasterMin.SetWindowText(_T(""));
		m_hdrLuminanceMasterMax.SetWindowText(_T("")) ;

		m_hdrColorspaceREdit.SetWindowTextW(_T(""));
		m_hdrColorspaceGEdit.SetWindowTextW(_T(""));
		m_hdrColorspaceBEdit.SetWindowTextW(_T(""));
		m_hdrColorspaceWPEdit.SetWindowTextW(_T(""));
	}

	//
	// Push
	//

	// During asynchronous graph teardown the existing renderer can no longer
	// consume updates.  Retain the newly built state for the replacement graph
	// without queuing a second restart.
	if (m_videoRenderer && m_rendererState == RendererState::RENDERSTATE_STOPPING)
	{
		DbgLog((LOG_TRACE, 1,
			TEXT("CVideoProcessorDlg::BuildPushVideoState(): Renderer stopping; state retained for next graph")));
		return true;
	}

	// Push to renderer if available
	if (m_videoRenderer)
	{
		const bool rendererAcceptedState =
			m_videoRenderer->OnVideoState(m_builtVideoState);

		if (!rendererAcceptedState)
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("CVideoProcessorDlg::BuildPushVideoState(): Renderer rejected effective state %s/%s -> %s/%s"),
				ToString(previousEffectiveEotf),
				ToString(previousEffectiveColorSpace),
				ToString(m_builtVideoState->eotf),
				ToString(m_builtVideoState->colorspace)));
			DebugLog::Log(
				"Renderer rejected effective video state %s / %s -> %s / %s; renderer update required",
				CStringA(ToString(previousEffectiveEotf)).GetString(),
				CStringA(ToString(previousEffectiveColorSpace)).GetString(),
				CStringA(ToString(m_builtVideoState->eotf)).GetString(),
				CStringA(ToString(m_builtVideoState->colorspace)).GetString());
		}

		return rendererAcceptedState;
	}
	else
	{
		DbgLog((LOG_TRACE, 1,
			TEXT("CVideoProcessorDlg::BuildPushVideoState(): Renderer unavailable; state retained for next graph")));
		return true;
	}
}

DisplayRuleExpression::ValueLookup
CVideoProcessorDlg::GetUnifiedProfileSourceLookup() const
{
	return StateVariables::VideoStateLookup(m_builtVideoState);
}

void CVideoProcessorDlg::ApplyUnifiedProfileSnapshot(
	const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& snapshot,
	bool allowRestart)
{
	if (!snapshot)
		return;

	bool lldvPolicyChanged = false;
	if (!snapshot->lldv.profile.empty())
	{
		auto applyLldvOverride = [&lldvPolicyChanged](double& destination,
			bool configured, double value)
		{
			const double desired = configured ? value : -1.0;
			if (destination != desired)
			{
				destination = desired;
				lldvPolicyChanged = true;
			}
		};
		applyLldvOverride(m_profileLldvMaxCllOverride,
			snapshot->lldv.hasMaxCll, snapshot->lldv.maxCll);
		applyLldvOverride(m_profileLldvMaxFallOverride,
			snapshot->lldv.hasMaxFall, snapshot->lldv.maxFall);
		applyLldvOverride(m_profileLldvMasteringMinLuminanceOverride,
			snapshot->lldv.hasMasteringMinLuminance,
			snapshot->lldv.masteringMinLuminance);
		applyLldvOverride(m_profileLldvMasteringMaxLuminanceOverride,
			snapshot->lldv.hasMasteringMaxLuminance,
			snapshot->lldv.masteringMaxLuminance);
		if (lldvPolicyChanged)
			DebugLog::Log(
				"LLDV profile applied: profile=%s max_cll=%g max_fall=%g mastering_min=%g mastering_max=%g",
				snapshot->lldv.profile.c_str(),
				m_profileLldvMaxCllOverride,
				m_profileLldvMaxFallOverride,
				m_profileLldvMasteringMinLuminanceOverride,
				m_profileLldvMasteringMaxLuminanceOverride);
	}

	if (!m_videoRenderer)
		return;
	if (lldvPolicyChanged && allowRestart && m_captureDeviceVideoState &&
		m_rendererState != RendererState::RENDERSTATE_STOPPING)
	{
		// A manual profile selection can arrive while BuildPushVideoState is
		// refreshing automatic profiles. Defer the next state build instead of
		// re-entering that path, so the selected metadata is pushed even when no
		// new capture notification follows the shortcut.
		m_lldvProfileApplyPending = true;
		SetTimer(LLDV_PROFILE_APPLY_TIMER_ID, 1, nullptr);
	}

	bool queuePolicyChanged = false;
	if (!snapshot->queue.profile.empty())
	{
		if (!m_profileQueueDefaultsCaptured)
		{
			m_profileBaseLeadFrames =
				videoProcessorApp.GetPresentationLeadFrames();
			m_profileBaseTargetFrames =
				videoProcessorApp.GetQueueSteadyReserveFrames();
			m_profileBaseActivePictureLookaheadFrames =
				videoProcessorApp.GetActivePictureLookaheadFrames();
			m_profileBaseStartupPrerollFrames =
				videoProcessorApp.GetQueueStartupPrerollFrames();
			m_profileBaseQueueResetDelaySeconds = m_queueResetDelaySeconds;
			m_profileBaseQueueResetHighWaterPercent =
				m_queueResetHighWaterPercent;
			m_profileQueueDefaultsCaptured = true;
		}
		const size_t desiredQueueSize = snapshot->queue.hasQueueSize ?
			snapshot->queue.queueSize : m_profileBaseQueueCapacity;
		if (GetRendererVideoFrameQueueSizeMax() != desiredQueueSize)
		{
			CString queueSize;
			queueSize.Format(TEXT("%zu"), desiredQueueSize);
			m_defaultQueueSize = queueSize;
			m_rendererVideoFrameQueueSizeMaxEdit.SetWindowText(queueSize);
			m_videoRenderer->SetFrameQueueMaxSize(desiredQueueSize);
			queuePolicyChanged = true;
		}
		const size_t desiredLeadFrames = snapshot->queue.hasLeadFrames ?
			snapshot->queue.leadFrames : m_profileBaseLeadFrames;
		if (videoProcessorApp.GetPresentationLeadFrames() != desiredLeadFrames)
		{
			videoProcessorApp.SetPresentationLeadFrames(desiredLeadFrames);
			m_videoRenderer->SetPresentationLeadFrames(desiredLeadFrames, true);
			queuePolicyChanged = true;
		}
		const size_t desiredTargetFrames = snapshot->queue.hasTargetFrames ?
			snapshot->queue.targetFrames : m_profileBaseTargetFrames;
		const size_t desiredStartupPreroll =
			snapshot->queue.hasStartupPrerollFrames ?
			snapshot->queue.startupPrerollFrames :
			m_profileBaseStartupPrerollFrames;
		if (videoProcessorApp.GetQueueSteadyReserveFrames() != desiredTargetFrames ||
			videoProcessorApp.GetQueueStartupPrerollFrames() != desiredStartupPreroll)
		{
			videoProcessorApp.SetQueueSteadyReserveFrames(desiredTargetFrames);
			videoProcessorApp.SetQueueStartupPrerollFrames(desiredStartupPreroll);
			m_videoRenderer->SetQueueFramePolicy(
				desiredStartupPreroll, desiredTargetFrames, true);
			queuePolicyChanged = true;
		}
		const size_t desiredLookahead =
			snapshot->queue.hasActivePictureLookaheadFrames ?
			snapshot->queue.activePictureLookaheadFrames :
			m_profileBaseActivePictureLookaheadFrames;
		if (videoProcessorApp.GetActivePictureLookaheadFrames() != desiredLookahead)
		{
			videoProcessorApp.SetActivePictureLookaheadFrames(desiredLookahead);
			m_videoRenderer->SetActivePictureLookaheadFrames(desiredLookahead);
			queuePolicyChanged = true;
		}
		const int desiredResetDelay =
			snapshot->queue.hasResetAfterRendererRestartSeconds ?
			snapshot->queue.resetAfterRendererRestartSeconds :
			m_profileBaseQueueResetDelaySeconds;
		const int desiredHighWater = snapshot->queue.hasResetQueueTooLargePercent ?
			snapshot->queue.resetQueueTooLargePercent :
			m_profileBaseQueueResetHighWaterPercent;
		if (m_queueResetDelaySeconds != desiredResetDelay ||
			m_queueResetHighWaterPercent != desiredHighWater)
		{
			m_queueResetDelaySeconds = desiredResetDelay;
			m_queueResetHighWaterPercent = desiredHighWater;
			queuePolicyChanged = true;
		}
		if (queuePolicyChanged)
			DebugLog::Log(
				"Queue profile applied: profile=%s capacity=%zu lead=%zu "
				"startup=%zu target=%zu lookahead=%zu reset=%ds high_water=%d%%",
				snapshot->queue.profile.c_str(),
				GetRendererVideoFrameQueueSizeMax(),
				videoProcessorApp.GetPresentationLeadFrames(),
				videoProcessorApp.GetQueueStartupPrerollFrames(),
				videoProcessorApp.GetQueueSteadyReserveFrames(),
				videoProcessorApp.GetActivePictureLookaheadFrames(),
				m_queueResetDelaySeconds, m_queueResetHighWaterPercent);
	}

	CString activeState;
	bool rendererRestartRequired = false;
	if (!m_videoRenderer->ApplyApplicationState(
		*snapshot, activeState, rendererRestartRequired))
		return;

	DebugLog::Log("Applied unified profile state: %s",
		CStringA(activeState).GetString());
	if (allowRestart && rendererRestartRequired)
	{
		if (m_rendererFullscreenCheck.GetCheck() && m_fullScreenVideoWindow &&
			IsWindow(m_fullScreenVideoWindow->GetHWND()))
		{
			m_preserveFullscreenHostForProfileRestart = true;
			DebugLog::Log(
				"Unified profile restart: preserving fullscreen host hwnd=%p",
				m_fullScreenVideoWindow->GetHWND());
		}
		DebugLog::Log(
			"Unified viewport output aspect changed; restarting renderer");
		m_postRendererStartRequiresGraph = false;
		m_wantToRestartRenderer = true;
		UpdateState();
	}
	else if (allowRestart && queuePolicyChanged)
	{
		RequestRendererReset(RendererResetReason::QueueSizeChange, false, 0);
	}
}


void CVideoProcessorDlg::ScheduleUnifiedProfileActions(
	const std::vector<UnifiedProfileRuntime::ActionInvocation>& actions)
{
	if (!m_unifiedActionCancelEvent || actions.empty())
		return;
	const std::string configPath = m_profileRuntime.ConfigPath();
	for (const UnifiedProfileRuntime::ActionInvocation& invocation : actions)
	{
		if (!IsUnifiedActionRendererSelected(invocation.action))
			continue;
		const DWORD delayMs = static_cast<DWORD>(
			invocation.action.delaySeconds * 1000);
		DebugLog::Log("event action '%s' scheduled for %s (%s) in %d seconds",
			invocation.action.name.c_str(), invocation.event.c_str(),
			invocation.reason.c_str(), invocation.action.delaySeconds);
		m_unifiedActionWorkers.emplace_back([this, invocation, configPath,
			delayMs]()
			{
				if (m_unifiedActionCancelEvent &&
					WaitForSingleObject(m_unifiedActionCancelEvent, delayMs) ==
						WAIT_TIMEOUT)
				{
					EventActionLauncher::Launch(invocation.action, configPath);
				}
				else
				{
					DebugLog::Log("event action '%s' cancelled while waiting for %s",
						invocation.action.name.c_str(), invocation.event.c_str());
				}
			});
	}
}


void CVideoProcessorDlg::PublishUnifiedProfileEvent(const std::string& event,
	const std::string& reason,
	const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& previous,
	const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& current)
{
	std::vector<UnifiedProfileRuntime::ActionInvocation> actions;
	std::string error;
	if (!m_profileRuntime.CollectActionInvocations(event, reason, previous,
		current, actions, error))
	{
		DebugLog::Log("unified action event '%s' was not published: %s",
			event.c_str(), error.c_str());
		return;
	}
	ScheduleUnifiedProfileActions(actions);
}


void CVideoProcessorDlg::BuildPushRestartVideoState()
{
	if (!m_captureDeviceVideoState)
		return;

	const bool rendererAcceptedState = BuildPushVideoState();

	if (!rendererAcceptedState)
	{
		m_wantToRestartRenderer = true;
		UpdateState();
	}
}

void CVideoProcessorDlg::ScheduleNewLldvRendererRestart()
{
	if (!m_videoRenderer)
		return;

	// If confirmation arrives before the renderer reaches RENDERING, carry
	// the request into OnMessageRendererStateChange instead of dropping it.
	if (m_rendererState != RendererState::RENDERSTATE_RENDERING)
	{
		m_lldvRestartPending = true;
		DebugLog::Log(
			"New LLDV promotion: candidate confirmed during renderer startup; PQ restart queued");
		return;
	}

	if (m_lldvChangeRestartDelaySeconds >= 0)
		return;

	m_lldvChangeRestartDelaySeconds = 2;
	SetTimer(LLDV_CHANGE_RESTART_TIMER_ID, 1000, nullptr);
	DebugLog::Log(
		"New LLDV promotion: candidate confirmed; scheduling PQ renderer restart in 2 seconds");
}


void CVideoProcessorDlg::_FatalError(int line, const std::string& functionName, const CString& error)
{
	CString s;
	s.Format(
		_T("%s\r\n\rFile: VideoProcessorDlg.cpp:%i\r\nFunction: %s"),
		error, line, CString(functionName.c_str()));

	::MessageBox(nullptr, s, TEXT("Fatal error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

	if (m_videoRenderer || m_rendererRetirementPending)
	{
		DebugLog::Log(
			"Fatal termination deferred through renderer stop/retirement state machine");
		OnClose();
		return;
	}
	CDialog::EndDialog(S_FALSE);
}

//
// CDialog
//


void CVideoProcessorDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);

	// Capture device group
	DDX_Control(pDX, IDC_CAPTURE_DEVICE_COMBO, m_captureDeviceCombo);
	DDX_Control(pDX, IDC_CAPTURE_INPUT_COMBO, m_captureInputCombo);
	DDX_Control(pDX, IDC_CAPTURE_STATE_STATIC, m_captureDeviceStateText);
	DDX_Control(pDX, IDC_CAPTURE_RESTART_BUTTON, m_captureDeviceRestartButton);
	DDX_Control(pDX, IDC_CAPTURE_DEVICE_OTHER_LIST, m_captureDeviceOtherList);

	// Input group
	DDX_Control(pDX, IDC_INPUT_LOCKED_STATIC, m_inputLockedText);
	DDX_Control(pDX, IDC_INPUT_DISPLAY_MODE_STATIC, m_inputDisplayModeText);
	DDX_Control(pDX, IDC_INPUT_ENCODING_STATIC, m_inputEncodingText);
	DDX_Control(pDX, IDC_INPUT_BIT_DEPTH_STATIC, m_inputBitDepthText);
	DDX_Control(pDX, IDC_INPUT_VIDEO_FRAME_COUNT_STATIC, m_inputVideoFrameCountText);
	DDX_Control(pDX, IDC_INPUT_VIDEO_FRAME_MISSED_STATIC, m_inputVideoFrameMissedText);
	DDX_Control(pDX, IDC_INPUT_LATENCY_MS_STATIC, m_inputLatencyMsText);

	// Captured video group
	DDX_Control(pDX, IDC_VIDEO_VALID_STATIC, m_videoValidText);
	DDX_Control(pDX, IDC_VIDEO_DISPLAY_MODE_STATIC, m_videoDisplayModeText);
	DDX_Control(pDX, IDC_VIDEO_PIXEL_FORMAT_STATIC, m_videoPixelFormatText);
	DDX_Control(pDX, IDC_VIDEO_EOTF_STATIC, m_videoEotfText);
	DDX_Control(pDX, IDC_VIDEO_COLORSPACE_STATIC, m_videoColorSpaceText);

	// Timing clock group
	DDX_Control(pDX, IDC_TIMING_CLOCK_DESCRIPTION_STATIC, m_timingClockDescriptionText);
	DDX_Control(pDX, IDC_TIMING_CLOCK_FRAME_OFFSET_EDIT, m_timingClockFrameOffsetEdit);
	DDX_Control(pDX, IDC_TIMING_CLOCK_FRAME_OFFSET_AUTO_CHECK, m_timingClockFrameOffsetAutoCheck);

	// colorSpace group
	DDX_Control(pDX, IDC_COLORSPACE_CONTAINER_COMBO, m_colorspaceContainerCombo);

	// HDR colorSpace group
	DDX_Control(pDX, IDC_HDR_COLORSPACE_R_EDIT, m_hdrColorspaceREdit);
	DDX_Control(pDX, IDC_HDR_COLORSPACE_G_EDIT, m_hdrColorspaceGEdit);
	DDX_Control(pDX, IDC_HDR_COLORSPACE_B_EDIT, m_hdrColorspaceBEdit);
	DDX_Control(pDX, IDC_HDR_COLORSPACE_WP_EDIT, m_hdrColorspaceWPEdit);
	DDX_Control(pDX, IDC_HDR_COLORSPACE_COMBO, m_hdrColorspaceCombo);

	// HDR luminance group
	DDX_Control(pDX, IDC_HDR_LUMINANCE_MAXCLL_EDIT, m_hdrLuminanceMaxCll);
	DDX_Control(pDX, IDC_HDR_LUMINANCE_MAXFALL_EDIT, m_hdrLuminanceMaxFall);
	DDX_Control(pDX, IDC_HDR_LUMINANCE_MASTER_MIN_EDIT, m_hdrLuminanceMasterMin);
	DDX_Control(pDX, IDC_HDR_LUMINANCE_MASTER_MAX_EDIT, m_hdrLuminanceMasterMax);
	DDX_Control(pDX, IDC_HDR_LUMINANCE_COMBO, m_hdrLuminanceCombo);

	// CIE1931 graph
	DDX_Control(pDX, IDC_CIE1931XY_GRAPH, m_colorspaceCie1931xy);

	// Renderer group
	DDX_Control(pDX, IDC_RENDERER_COMBO, m_rendererCombo);
	DDX_Control(pDX, IDC_RENDERER_DETAIL_STRING_STATIC, m_rendererDetailStringStatic);
	DDX_Control(pDX, IDC_RENDERER_RESTART_BUTTON, m_rendererRestartButton);
	DDX_Control(pDX, IDC_RENDERER_STATE_STATIC, m_rendererStateText);
	DDX_Control(pDX, IDC_RENDERER_WINDOWED_VIDEO_WINDOW, m_windowedVideoWindow);

	// Renderer Queue group
	DDX_Control(pDX, IDC_RENDERER_VIDEO_FRAME_USE_QUEUE_CHECK, m_rendererVideoFrameUseQeueueCheck);
	DDX_Control(pDX, IDC_RENDERER_SCENE_CORRECTION_MODE_COMBO, m_rendererSceneCorrectionModeCombo);
	DDX_Control(pDX, IDC_RENDERER_VIDEO_FRAME_QUEUE_SIZE_STATIC, m_rendererVideoFrameQueueSizeText);
	DDX_Control(pDX, IDC_RENDERER_VIDEO_FRAME_QUEUE_SIZE_MAX_EDIT, m_rendererVideoFrameQueueSizeMaxEdit);
	DDX_Control(pDX, IDC_RENDERER_DROPPED_FRAME_COUNT_STATIC, m_rendererDroppedFrameCountText);
	DDX_Control(pDX, IDC_RENDERER_RESET_BUTTON, m_rendererResetButton);
	DDX_Control(pDX, IDC_RENDERER_RESET_AUTO_CHECK, m_rendererResetAutoCheck);

	// Renderer Video conversion group
	DDX_Control(pDX, IDC_RENDERER_VIDEO_CONVERSION_COMBO, m_rendererVideoConversionCombo);

	// Renderer DirectShow override group
	DDX_Control(pDX, IDC_RENDERER_DIRECTSHOW_START_STOP_TIME_METHOD_COMBO, m_rendererDirectShowStartStopTimeMethodCombo);
	DDX_Control(pDX, IDC_RENDERER_DIRECTSHOW_NOMINAL_RANGE_COMBO, m_rendererNominalRangeCombo);
	DDX_Control(pDX, IDC_RENDERER_DIRECTSHOW_TRANSFER_FUNCTION_COMBO, m_rendererTransferFunctionCombo);
	DDX_Control(pDX, IDC_RENDERER_DIRECTSHOW_TRANSFER_MATRIX_COMBO, m_rendererTransferMatrixCombo);
	DDX_Control(pDX, IDC_RENDERER_DIRECTSHOW_PRIMARIES_COMBO, m_rendererPrimariesCombo);

	// Renderer latency (ms) group
	DDX_Control(pDX, IDC_RENDERER_LATENCY_TO_VP_STATIC, m_rendererLatencyToVPText);
	DDX_Control(pDX, IDC_RENDERER_LATENCY_DS_LEAD_STATIC, m_rendererLatencyDsLeadText);
	DDX_Control(pDX, IDC_RENDERER_LATENCY_TO_DS_STATIC, m_rendererLatencyToDSText);

	// Renderer output group
	DDX_Control(pDX, IDC_RENDERER_FULL_SCREEN_CHECK, m_rendererFullscreenCheck);
	DDX_Control(pDX, IDC_FULLSCREENMODE_COMBO, m_fullScreenModeCombo);
}



//// Called when the dialog box is initialized
BOOL CVideoProcessorDlg::OnInitDialog()
{

		if (!CDialog::OnInitDialog())
		return FALSE;

	const HWND resetWakeWindow = GetSafeHwnd();
	m_rendererResetCoordinator =
		std::make_unique<RendererResetCoordinator>(
			[resetWakeWindow]()
			{
				return resetWakeWindow &&
					::PostMessage(
					resetWakeWindow,
					WM_MESSAGE_RENDERER_RESET_REQUEST,
					0, 0) != FALSE;
			},
			[]()
			{
				return static_cast<uint64_t>(GetTickCount64());
			});
	m_rendererIngressState =
		m_rendererResetCoordinator->GetIngressState();

	// The generated AFX_DIALOG_LAYOUT table moves unrelated labels and controls
	// when the dialog is resized.  This application has one resizable surface:
	// the video host.  Keep every other control at its resource position.
	EnableDynamicLayout(FALSE);

	CString title;
	title.Format(_T("VideoProcessor (%s)"), VERSION_DESCRIBE);
	SetWindowText(title.GetBuffer());

	SetIcon(m_hIcon, FALSE);

	// Set initial dialog size as minimum size
	CRect rectWindow;
	GetWindowRect(rectWindow);
	m_minDialogSize = rectWindow.Size();
	CaptureFixedDialogLayout();

	// Empty popup menus
	m_captureDeviceCombo.ResetContent();

	// Disable the interface
	m_captureDeviceCombo.EnableWindow(FALSE);
	m_captureInputCombo.EnableWindow(FALSE);

	// Get all renderers
	RebuildRendererCombo();

	//
	// Fill renderer selection boxes
	//

	for (auto p : COLOLORSPACE_CONTAINER_OPTIONS)
	{
		int index = m_colorspaceContainerCombo.AddString(p.first);
		m_colorspaceContainerCombo.SetItemData(index, (int)p.second);

		if (p.second == m_defaultContainerColorSpace)
			m_colorspaceContainerCombo.SetCurSel(index);
	}

	for (auto p : HDR_COLORSPACE_OPTIONS)
	{
		int index = m_hdrColorspaceCombo.AddString(p.first);
		m_hdrColorspaceCombo.SetItemData(index, (int)p.second);

		if (p.second == m_defaultHDRColorSpaceOption)
			m_hdrColorspaceCombo.SetCurSel(index);
	}

	for (auto p : HDR_LUMINANCE_OPTIONS)
	{
		int index = m_hdrLuminanceCombo.AddString(p.first);
		m_hdrLuminanceCombo.SetItemData(index, (int)p.second);

		if (p.second == m_defaultHDRLuminanceOption)
			m_hdrLuminanceCombo.SetCurSel(index);
	}

	for (auto p : RENDERER_DIRECTSHOW_START_STOP_TIME_OPTIONS)
	{
		int index = m_rendererDirectShowStartStopTimeMethodCombo.AddString(ToString(p));
		m_rendererDirectShowStartStopTimeMethodCombo.SetItemData(index, (int)p);

		if (p == m_defaultDSSSTimeMethod)
			m_rendererDirectShowStartStopTimeMethodCombo.SetCurSel(index);
	}

	for (const auto& p : DIRECTSHOW_NOMINAL_RANGE_OPTIONS)
	{
		int index = m_rendererNominalRangeCombo.AddString(p.first);
		m_rendererNominalRangeCombo.SetItemData(index, p.second);

		if (p.second == m_defaultNominalRange)
			m_rendererNominalRangeCombo.SetCurSel(index);
	}

	for (const auto& p : DIRECTSHOW_TRANSFER_FUNCTION_OPTIONS)
	{
		int index = m_rendererTransferFunctionCombo.AddString(p.first);
		m_rendererTransferFunctionCombo.SetItemData(index, (int)p.second);

		if (p.second == m_defaultTransferFunction)
			m_rendererTransferFunctionCombo.SetCurSel(index);
	}

	for (const auto& p : DIRECTSHOW_TRANSFER_MATRIX_OPTIONS)
	{
		int index = m_rendererTransferMatrixCombo.AddString(p.first);
		m_rendererTransferMatrixCombo.SetItemData(index, (int)p.second);

		if (p.second == m_defaultTransferMatrix)
			m_rendererTransferMatrixCombo.SetCurSel(index);
	}

	for (const auto& p : DIRECTSHOW_PRIMARIES_OPTIONS)
	{
		int index = m_rendererPrimariesCombo.AddString(p.first);
		m_rendererPrimariesCombo.SetItemData(index, (int)p.second);

		if (p.second == m_defaultPrimaries)
			m_rendererPrimariesCombo.SetCurSel(index);
	}

	for (const auto& p : RENDERER_VIDEO_CONVERSION)
	{
		int index = m_rendererVideoConversionCombo.AddString(ToString(p));
		m_rendererVideoConversionCombo.SetItemData(index, (int)p);

		if (p == m_defaultVideoConversionOverride)
			m_rendererVideoConversionCombo.SetCurSel(index);
	}

	m_rendererSceneCorrectionModeCombo.AddString(TEXT("Off"));
	m_rendererSceneCorrectionModeCombo.AddString(TEXT("On"));

	//for (const auto& p : FULLSCREEN_MODES)
	//{
	//	int index = m_fullScreenModeCombo.AddString(p.c_str);
	//	 m_fullScreenModeCombo.SetItemData(index, p.c_str);

	//	if (m_windowedFullScreenMode == false)
	//		m_fullScreenModeCombo.SetCurSel(0);

	//	if (m_windowedFullScreenMode == true)
	//		m_fullScreenModeCombo.SetCurSel(1);

	//}

	m_fullScreenModeCombo.AddString(L"Exclusive");
	m_fullScreenModeCombo.AddString(L"Windowed");
	if (m_windowedFullScreenMode == false)
		m_fullScreenModeCombo.SetCurSel(0);
	if (m_windowedFullScreenMode == true)
		m_fullScreenModeCombo.SetCurSel(1);


	// Start discovery services
	m_blackMagicDeviceDiscoverer->Start();

	m_accelerator = CreateConfiguredAccelerators(
		m_shaderShortcutRules,
		m_shaderShortcutKeys,
		m_displayRuleShortcutRules,
		m_rendererShortcutIndices,
		m_unifiedProfileShortcutKeys);
	if (!m_accelerator)
		FatalError(TEXT("Failed to create accelerator table"));

	CaptureGUIClear();
	RenderGUIClear();
	m_rendererDetailStringStatic.ShowWindow(SW_HIDE);

	m_rendererVideoFrameQueueSizeMaxEdit.SetWindowText(m_defaultQueueSize);
	m_timingClockFrameOffsetEdit.SetWindowText(m_defaultFrameOffset);
	m_rendererVideoFrameUseQeueueCheck.SetCheck(true);
	UpdateSceneCorrectionModeUi();
	UpdateRendererBackendUi();
	m_rendererResetAutoCheck.SetCheck(true);
	m_rendererFullscreenCheck.SetCheck(m_hideUI ? BST_UNCHECKED : m_rendererFullScreenStart);

	m_timingClockFrameOffsetAutoCheck.SetCheck(m_frameOffsetAutoStart);
	OnBnClickedTimingClockFrameOffsetAutoCheck();

	// Start timers
	const ULONGLONG initialUiTick = GetTickCount64();
	m_lastUiMessageTick.store(initialUiTick, std::memory_order_release);
	m_lastUiPaintTick.store(initialUiTick, std::memory_order_release);
	if (m_livenessWatchdogStopEvent &&
		!m_livenessWatchdogThread.joinable())
	{
		ResetEvent(m_livenessWatchdogStopEvent);
		m_livenessWatchdogThread =
			std::thread(&CVideoProcessorDlg::LivenessWatchdogWorker, this);
	}
	SetTimer(TIMER_ID_1SECOND, 1000, nullptr);
	// Active-picture analysis remains sparse on the conversion worker. This
	// cheap generation poll only consumes a published change, bounding NLS
	// mapping reaction without putting image analysis on the UI thread.
	SetTimer(SHADER_RULE_REFRESH_TIMER_ID,
		SHADER_RULE_REFRESH_INTERVAL_MS, nullptr);
	
	// Stats overlay will be created lazily on first toggle (Ctrl+I)
	// No initialization needed here

	
	if (m_hideUI)
		ApplyNoUiLayout();

	// If requested, minimize after the startup layout is applied.
	if (m_startMinimized) {
		ShowWindow(SW_MINIMIZE);
	}
	
	return TRUE;
}


BOOL CVideoProcessorDlg::PreTranslateMessage(MSG* pMsg)
{
	m_lastUiMessageTick.store(GetTickCount64(), std::memory_order_release);
	const bool keyDown = pMsg->message == WM_KEYDOWN ||
		pMsg->message == WM_SYSKEYDOWN;
	const bool keyUp = pMsg->message == WM_KEYUP ||
		pMsg->message == WM_SYSKEYUP;
	const WORD virtualKey = static_cast<WORD>(pMsg->wParam);
	const bool guardedShaderShortcut = m_shaderShortcutKeys.find(virtualKey) !=
		m_shaderShortcutKeys.end();
	const bool repeat = (static_cast<ULONG_PTR>(pMsg->lParam) &
		(1ull << 30)) != 0;
	if (m_shaderShortcutDebounce.ProcessPhysicalKey(virtualKey, keyDown, keyUp,
		repeat, guardedShaderShortcut))
	{
		m_shaderShortcutDebounce.Touch(GetTickCount64());
		if (m_shaderShortcutDebounce.HasPending())
		{
			KillTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID);
			SetTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID,
				SHADER_SHORTCUT_DEBOUNCE_MS, nullptr);
		}
		DebugLog::Log(
			"Keyboard shader shortcut auto-repeat retained pending intent vk=0x%02x shift=%d ctrl=%d alt=%d",
			static_cast<unsigned int>(virtualKey),
			(GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0,
			(GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0,
			(GetKeyState(VK_MENU) & 0x8000) ? 1 : 0);
		return TRUE;
	}
	if (keyUp && guardedShaderShortcut &&
		m_shaderShortcutDebounce.HasPending())
	{
		m_shaderShortcutDebounce.Touch(GetTickCount64());
		KillTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID);
		SetTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID,
			SHADER_SHORTCUT_DEBOUNCE_MS, nullptr);
	}
	const bool diagnosticKey =
		keyDown &&
		(pMsg->wParam == 'I' || pMsg->wParam == VK_F4);
	if (diagnosticKey)
	{
		DebugLog::Log(
			"Keyboard message: phase=pretranslate message=0x%04x vk=0x%02x ctrl=%d alt=%d age_ms=%lu target=%p dialog=%p foreground=%p focus=%p renderer_state=%d generation=%u retirement_pending=%d reset_active=%d",
			pMsg->message,
			static_cast<unsigned int>(pMsg->wParam),
			(GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0,
			(GetKeyState(VK_MENU) & 0x8000) ? 1 : 0,
			static_cast<unsigned long>(GetTickCount() - static_cast<DWORD>(GetMessageTime())),
			reinterpret_cast<void*>(pMsg->hwnd),
			reinterpret_cast<void*>(m_hWnd),
			reinterpret_cast<void*>(::GetForegroundWindow()),
			reinterpret_cast<void*>(::GetFocus()),
			static_cast<int>(m_rendererState),
			m_rendererGeneration.load(std::memory_order_acquire),
			m_rendererRetirementPending ? 1 : 0,
			RendererResetOperationInProgress() ? 1 : 0);
	}
	// Handle accelerator combinations
	if (m_accelerator)
	{
		if (::TranslateAccelerator(m_hWnd, m_accelerator, pMsg))
		{
			if (diagnosticKey)
				DebugLog::Log("Keyboard message: phase=pretranslate result=accelerator-consumed vk=0x%02x",
					static_cast<unsigned int>(pMsg->wParam));
			return TRUE;
		}
	}
	if (diagnosticKey)
		DebugLog::Log("Keyboard message: phase=pretranslate result=not-consumed vk=0x%02x",
			static_cast<unsigned int>(pMsg->wParam));

	return CDialog::PreTranslateMessage(pMsg);
}

void CVideoProcessorDlg::OnOK()
{
	// Called if the user presses enter somewhere

	CWnd* pwndCtrl = GetFocus();
	int ctrl_ID = pwndCtrl->GetDlgCtrlID();

	switch (ctrl_ID)
	{
		case IDC_TIMING_CLOCK_FRAME_OFFSET_EDIT:
			UpdateTimingClockFrameOffset();
			break;

		case IDC_RENDERER_VIDEO_FRAME_QUEUE_SIZE_MAX_EDIT:
			if (m_videoRenderer)
			{
				m_videoRenderer->SetFrameQueueMaxSize(GetRendererVideoFrameQueueSizeMax());
				RequestRendererReset(RendererResetReason::QueueSizeChange, false, 0);
			}
			break;

		case IDC_HDR_LUMINANCE_MAXCLL_EDIT:
		case IDC_HDR_LUMINANCE_MAXFALL_EDIT:
		case IDC_HDR_LUMINANCE_MASTER_MIN_EDIT:
		case IDC_HDR_LUMINANCE_MASTER_MAX_EDIT:
			BuildPushRestartVideoState();
			break;

		default:
			m_wantToRestartRenderer = true;
			UpdateState();
	}

	// Don't call the super implmenetation as that will close the window
}


void CVideoProcessorDlg::OnPaint()
{
	m_lastUiPaintTick.store(GetTickCount64(), std::memory_order_release);
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{

		if (m_videoRenderer &&
			!RendererResetOperationInProgress())
		{
			
			try
			{
				m_videoRenderer->OnPaint();
			}
			catch (const std::runtime_error&)
			{
			
					//log some issue
			}
			 

		}
				
		

			CDialog::OnPaint();

		
		
	}
}


void CVideoProcessorDlg::CaptureFixedDialogLayout()
{
	m_fixedControlLayout.clear();
	if (!GetSafeHwnd())
		return;

	const HWND videoWindow = m_windowedVideoWindow.GetSafeHwnd();
	CRect clientRect;
	GetClientRect(&clientRect);
	m_initialClientSize = clientRect.Size();
	m_windowedVideoWindow.GetWindowRect(&m_initialVideoWindowRect);
	ScreenToClient(&m_initialVideoWindowRect);

	for (HWND child = ::GetWindow(GetSafeHwnd(), GW_CHILD);
		child != nullptr;
		child = ::GetWindow(child, GW_HWNDNEXT))
	{
		if (child == videoWindow)
			continue;

		CRect rect;
		::GetWindowRect(child, &rect);
		ScreenToClient(&rect);
		m_fixedControlLayout.push_back({ child, rect });
	}
}


void CVideoProcessorDlg::RestoreFixedDialogLayout()
{
	if (m_hideUI || !GetSafeHwnd() || m_fixedControlLayout.empty())
		return;

	HDWP positions = ::BeginDeferWindowPos(
		static_cast<int>(m_fixedControlLayout.size()));
	if (!positions)
		return;

	for (const FixedControlLayout& control : m_fixedControlLayout)
	{
		if (!::IsWindow(control.hwnd))
			continue;

		positions = ::DeferWindowPos(
			positions,
			control.hwnd,
			nullptr,
			control.rect.left,
			control.rect.top,
			control.rect.Width(),
			control.rect.Height(),
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
		if (!positions)
			return;
	}

	::EndDeferWindowPos(positions);

	// A renderer's combo field can otherwise remain visually stale until clicked.
	m_rendererCombo.RedrawWindow(nullptr, nullptr,
		RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
}


void CVideoProcessorDlg::RestoreFrameOffsetEditLayout()
{
	if (!m_timingClockFrameOffsetEdit.GetSafeHwnd())
		return;

	for (const FixedControlLayout& control : m_fixedControlLayout)
	{
		if (control.hwnd != m_timingClockFrameOffsetEdit.GetSafeHwnd())
			continue;

		CRect currentRect;
		m_timingClockFrameOffsetEdit.GetWindowRect(&currentRect);
		ScreenToClient(&currentRect);
		if (!currentRect.EqualRect(&control.rect))
		{
			m_timingClockFrameOffsetEdit.SetWindowPos(
				nullptr,
				control.rect.left,
				control.rect.top,
				control.rect.Width(),
				control.rect.Height(),
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
		}
		return;
	}
}


void CVideoProcessorDlg::OnSize(UINT nType, int cx, int cy)
{
	if (m_hideUI)
	{
		if (m_windowedVideoWindow.GetSafeHwnd())
			m_windowedVideoWindow.MoveWindow(0, 0, cx, cy, TRUE);
	}
	else if (m_windowedVideoWindow.GetSafeHwnd() &&
		m_initialClientSize.cx > 0 && m_initialClientSize.cy > 0)
	{
		CRect videoRect = m_initialVideoWindowRect;
		videoRect.right += std::max<LONG>(
			0, static_cast<LONG>(cx) - m_initialClientSize.cx);
		videoRect.bottom += std::max<LONG>(
			0, static_cast<LONG>(cy) - m_initialClientSize.cy);
		m_windowedVideoWindow.MoveWindow(&videoRect, TRUE);
	}

	if (m_videoRenderer &&
		!RendererResetOperationInProgress())
		m_videoRenderer->OnSize();
	m_rendererTransitionWindow.KeepOnTop();

	// Some windowed DirectShow renderers finish processing WM_SIZE after this
	// handler returns.  Restore the fixed UI now and once more after that work
	// completes, without affecting the renderer graph or its media timeline.
	if (!m_hideUI)
	{
		RestoreFixedDialogLayout();
		SetTimer(UI_LAYOUT_RESTORE_TIMER_ID, 75, nullptr);
	}

	// Update stats overlay position
	if (m_statsOverlay && m_statsOverlay->IsVisible())
	{
		m_statsOverlay->UpdatePosition(this->GetSafeHwnd());
	}
	

	// Track if this is a significant resize (not just minimize/restore)
	static CSize lastSize(0, 0);
	CSize currentSize(cx, cy);

	bool significantResize = (abs(currentSize.cx - lastSize.cx) > 50 ||
		abs(currentSize.cy - lastSize.cy) > 50) &&
		lastSize.cx > 0 && lastSize.cy > 0;

	// ... existing OnSize code ...

	// A settled fullscreen resize can invalidate the renderer's window/swapchain
	// assumptions. Coalesce the burst, then rebuild from fresh live HDMI data.
	if (m_rendererFullscreenCheck.GetCheck() && significantResize && m_videoRenderer &&
		m_rendererState == RendererState::RENDERSTATE_RENDERING)
	{
		RequestRendererReset(RendererResetReason::Resize, true, 250);
	}

	lastSize = currentSize;
	CDialog::OnSize(nType, cx, cy);
}


HCURSOR CVideoProcessorDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CVideoProcessorDlg::OnGetMinMaxInfo(MINMAXINFO* minMaxInfo)
{
	CDialog::OnGetMinMaxInfo(minMaxInfo);

	if (m_hideUI) {
		minMaxInfo->ptMinTrackSize.x = 100;
		minMaxInfo->ptMinTrackSize.y = 100;
	}
	else {
		// Guarantee minimum size of window
		minMaxInfo->ptMinTrackSize.x = std::max(minMaxInfo->ptMinTrackSize.x, m_minDialogSize.cx);
		minMaxInfo->ptMinTrackSize.y = std::max(minMaxInfo->ptMinTrackSize.y, m_minDialogSize.cy);
	}
}


void CVideoProcessorDlg::OnSetFocus(CWnd* pOldWnd)
{
	CDialog::OnSetFocus(pOldWnd);
}


void CVideoProcessorDlg::OnDisplayChange(UINT bitsPerPixel, int width, int height)
{
	if (g_displayRefreshRateSampler)
		g_displayRefreshRateSampler->ResetMeasurement();

	// Display notifications can precede the capture/renderer replacement by
	// many seconds. Do not flush the old madVR graph because its queues fill
	// during that handshake; the replacement renderer owns recovery.
	HWND displayWindow = nullptr;
	if (m_fullScreenVideoWindow && IsWindow(m_fullScreenVideoWindow->GetHWND()))
		displayWindow = m_fullScreenVideoWindow->GetHWND();
	else if (m_windowedVideoWindow.GetSafeHwnd())
		displayWindow = m_windowedVideoWindow.GetSafeHwnd();
	const double configuredRefreshRate = GetActiveTargetRefreshRate(displayWindow);
	const double previousRefreshRate = m_lastAlphaTargetRefreshRateHz;
	const bool materiallyDifferentRefreshFamily =
		previousRefreshRate > 0.0 && configuredRefreshRate > 0.0 &&
		std::fabs(previousRefreshRate - configuredRefreshRate) /
			std::max(previousRefreshRate, configuredRefreshRate) >= 0.01;
	m_alphaRefreshTransitionPending =
		m_videoRenderer && !m_activeRendererIsDirectShow &&
		materiallyDifferentRefreshFamily;
	if (m_alphaRefreshTransitionPending)
	{
		m_alphaRefreshTransitionPreviousRateHz = previousRefreshRate;
		m_alphaRefreshTransitionCurrentRateHz = configuredRefreshRate;
		DebugLog::Log(
			"Alpha refresh transition requested: previous=%.6fHz configured=%.6fHz "
			"state=timer-pending",
			previousRefreshRate,
			configuredRefreshRate);
	}
	else if (m_videoRenderer && !m_activeRendererIsDirectShow)
	{
		DebugLog::Log(
			"Alpha display change does not require refresh re-prime: previous=%.6fHz "
			"configured=%.6fHz",
			previousRefreshRate,
			configuredRefreshRate);
	}
	m_displayTransitionAwaitingRenderer = true;
	m_queueResetIgnoreEventsUntil = GetTickCount64() + 30000;
	DebugLog::Log(
		"Windows display mode changed: %d x %d, %u bits; "
		"display-rate measurement reset; emergency queue recovery "
		"suppressed pending renderer replacement",
		width,
		height,
		bitsPerPixel);
	if (m_videoRenderer &&
		m_rendererState == RendererState::RENDERSTATE_RENDERING)
	{
		// Fallback only: a replacement renderer will supersede this request
		// and start its normal settle-plus-configured-delay recovery.
		RequestRendererReset(RendererResetReason::DisplayTransition, false,
			30000 +
			static_cast<UINT>(m_queueResetDelaySeconds * 1000));
	}
	if (m_videoRenderer &&
		!RendererResetOperationInProgress())
		m_videoRenderer->OnDisplayChange();
	if (m_rendererFullscreenCheck.GetCheck() && m_fullScreenVideoWindow &&
		IsWindow(m_fullScreenVideoWindow->GetHWND()))
	{
		// A renderer-initiated mode switch can leave an already-created popup
		// logically visible and focused but no longer composed above the desktop.
		// Coalesce the display-change burst, then reassert its monitor bounds and
		// z-order after Windows has settled. This also replaces the construction
		// timer when the mode switch takes longer than expected.
		SetTimer(FULLSCREEN_FOCUS_TIMER_ID, 1000, nullptr);
	}
	CDialog::OnDisplayChange(bitsPerPixel, width, height);
}


void CVideoProcessorDlg::OnClose()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnClose()")));
	DebugLog::Log(
		"Keyboard close handler: phase=enter want_terminate=%d renderer_state=%d generation=%u retirement_pending=%d reset_active=%d foreground=%p focus=%p",
		m_wantToTerminate ? 1 : 0,
		static_cast<int>(m_rendererState),
		m_rendererGeneration.load(std::memory_order_acquire),
		m_rendererRetirementPending ? 1 : 0,
		RendererResetOperationInProgress() ? 1 : 0,
		reinterpret_cast<void*>(::GetForegroundWindow()),
		reinterpret_cast<void*>(::GetFocus()));

	if (m_wantToTerminate)
	{
		DebugLog::Log("Keyboard close handler: phase=exit result=already-terminating");
		return;
	}

	// Set intent first, stopping the discoverer will lead to state update calls
	m_desiredCaptureDevice = nullptr;
	m_wantToTerminate = true;

	// Stop discovery
	if (m_blackMagicDeviceDiscoverer)
	{
		m_blackMagicDeviceDiscoverer->Stop();
		m_blackMagicDeviceDiscoverer.Release();
	}

	UpdateState();

	// Remove all renderers
	ClearRendererCombo();
	DebugLog::Log("Keyboard close handler: phase=exit result=termination-requested");
}

void CVideoProcessorDlg::OnTimer(UINT_PTR nIDEvent)
{
	const ULONGLONG uiNow = GetTickCount64();
	m_lastUiMessageTick.store(uiNow, std::memory_order_release);
	if (m_rendererResetCoordinator)
	{
		const RendererResetCoordinator::Diagnostics diagnostics =
			m_rendererResetCoordinator->GetDiagnostics();
		if (diagnostics.completionPending)
		{
			CompleteRendererResetOperation();
		}
		const ULONGLONG startedTick =
			m_activeGraphRequestStartedTick.load(std::memory_order_acquire);
		if (diagnostics.operationActive &&
			startedTick != 0 &&
			uiNow - startedTick >= 10000 &&
			m_lastGraphTimeoutLoggedOperationId !=
				diagnostics.activeOperationId)
		{
			m_lastGraphTimeoutLoggedOperationId =
				diagnostics.activeOperationId;
			DebugLog::Log(
				"Reset terminal diagnostic: request=%llu generation=%u "
				"reason=%S scope=%s elapsed=%llums "
				"failure=graph-control-operation-did-not-return; "
				"UI thread remains responsive and overlapping recovery is disabled",
				static_cast<unsigned long long>(
					diagnostics.activeOperationId),
				diagnostics.rendererGeneration,
				ToString(diagnostics.pendingReason),
				ResetScopeName(diagnostics.pendingScope),
				static_cast<unsigned long long>(
					uiNow - startedTick));
			RendererLivenessSnapshot snapshot;
			if (m_videoRenderer &&
				m_videoRenderer->GetLivenessSnapshot(snapshot))
			{
				LogLivenessSnapshot(
					snapshot,
					snapshot.rawQueueDepth,
					snapshot.convertedQueueDepth,
					snapshot.queueCapacity,
					"graph-control-timeout");
			}
		}
	}

	if (nIDEvent == RENDERER_RESET_MAILBOX_TIMER_ID)
	{
		KillTimer(RENDERER_RESET_MAILBOX_TIMER_ID);
		PumpRendererResetMailbox();
		return;
	}

	if (nIDEvent == TRANSIENT_INVALID_VIDEO_STATE_TIMER_ID)
	{
		KillTimer(TRANSIENT_INVALID_VIDEO_STATE_TIMER_ID);
		if (!m_deferredInvalidCaptureVideoState)
			return;

		const ULONGLONG now = GetTickCount64();
		if (now < m_deferredInvalidCaptureVideoStateDeadlineTick)
		{
			SetTimer(
				TRANSIENT_INVALID_VIDEO_STATE_TIMER_ID,
				static_cast<UINT>(
					m_deferredInvalidCaptureVideoStateDeadlineTick - now),
				nullptr);
			return;
		}

		const uint64_t capturedFramesNow = m_captureDevice ?
			m_captureDevice->VideoFrameCapturedCount() : 0;
		if (capturedFramesNow > m_deferredInvalidCaptureVideoStateFrameCount)
		{
			const uint64_t capturedFramesAtDeferral =
				m_deferredInvalidCaptureVideoStateFrameCount;
			m_deferredInvalidCaptureVideoState.Release();
			m_deferredInvalidCaptureVideoStateDeadlineTick = 0;
			m_deferredInvalidCaptureVideoStateFrameCount = 0;
			const RendererIngressState::CaptureSequenceSnapshot ingress =
				m_rendererIngressState->CaptureSequences();
			DebugLog::Log(
				"Transient invalid capture video state ignored: capture advanced "
				"from=%llu to=%llu action=retain-live-renderer "
				"published=%llu required=%llu acknowledged=%llu admitted=%d",
				static_cast<unsigned long long>(
					capturedFramesAtDeferral),
				static_cast<unsigned long long>(capturedFramesNow),
				static_cast<unsigned long long>(ingress.published),
				static_cast<unsigned long long>(ingress.required),
				static_cast<unsigned long long>(ingress.acknowledged),
				ingress.admissionOpen ? 1 : 0);
			return;
		}

		m_captureDeviceVideoState = m_deferredInvalidCaptureVideoState;
		m_deferredInvalidCaptureVideoState.Release();
		m_deferredInvalidCaptureVideoStateDeadlineTick = 0;
		m_deferredInvalidCaptureVideoStateFrameCount = 0;
		DebugLog::Log(
			"Transient invalid capture video state persisted through grace; "
			"action=apply-invalid-state");
		BuildPushVideoState();
		UpdateState();
		return;
	}

	TryRevealRendererTransition(
		m_rendererGeneration.load(std::memory_order_acquire));

	if (nIDEvent == UI_LAYOUT_RESTORE_TIMER_ID)
	{
		KillTimer(UI_LAYOUT_RESTORE_TIMER_ID);
		RestoreFixedDialogLayout();
		return;
	}

	if (nIDEvent == SHADER_SHORTCUT_DEBOUNCE_TIMER_ID)
	{
		KillTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID);
		if (!m_shaderShortcutDebounce.HasPending())
			return;
		if (!m_videoRenderer ||
			m_rendererState != RendererState::RENDERSTATE_RENDERING ||
			m_wantToRestartRenderer)
		{
			SetTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID,
				SHADER_SHORTCUT_DEBOUNCE_MS, nullptr);
			return;
		}

		uint32_t commandId = 0;
		const ULONGLONG now = GetTickCount64();
		if (!m_shaderShortcutDebounce.TryTake(now,
			SHADER_SHORTCUT_DEBOUNCE_MS, commandId))
		{
			const uint32_t remaining =
				m_shaderShortcutDebounce.DelayRemaining(
					now, SHADER_SHORTCUT_DEBOUNCE_MS);
			SetTimer(SHADER_SHORTCUT_DEBOUNCE_TIMER_ID,
				remaining > 0 ? remaining : 1, nullptr);
			return;
		}
		const auto rule = m_shaderShortcutRules.find(
			static_cast<WORD>(commandId));
		DEBUGLOG("Shader shortcut debounce settled selector='%S'",
			rule != m_shaderShortcutRules.end() ?
				static_cast<LPCTSTR>(rule->second) : TEXT("(missing)"));
		ApplyShaderRuleCommand(commandId);
		return;
	}

	if (nIDEvent == SHADER_RULE_REFRESH_TIMER_ID)
	{
		if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
			m_videoRenderer && !m_wantToRestartRenderer)
		{
			CString refreshedShaderRule;
			bool shaderRestartRequired = false;
			if (m_videoRenderer->RefreshShaderRule(
					refreshedShaderRule, shaderRestartRequired) &&
				shaderRestartRequired)
			{
				DEBUGLOG(
					"Conditional shader state changed to '%S'; "
					"restarting renderer for aspect negotiation",
					static_cast<LPCTSTR>(refreshedShaderRule));
				m_postRendererStartRequiresGraph = false;
				m_wantToRestartRenderer = true;
				UpdateState();
			}
		}
		return;
	}

	if (nIDEvent == LLDV_PROFILE_APPLY_TIMER_ID)
	{
		KillTimer(LLDV_PROFILE_APPLY_TIMER_ID);
		if (!m_lldvProfileApplyPending)
			return;
		if (!m_videoRenderer || !m_captureDeviceVideoState ||
			m_rendererState == RendererState::RENDERSTATE_STOPPING ||
			m_rendererState == RendererState::RENDERSTATE_STOPPED ||
			m_rendererState == RendererState::RENDERSTATE_FAILED)
		{
			m_lldvProfileApplyPending = false;
			return;
		}
		if (m_rendererState != RendererState::RENDERSTATE_RENDERING)
		{
			// RenderStart applies the current snapshot before its first state push,
			// but retain this coalesced request until an in-flight graph reaches
			// its running state.
			SetTimer(LLDV_PROFILE_APPLY_TIMER_ID, 25, nullptr);
			return;
		}

		m_lldvProfileApplyPending = false;
		const bool rendererAcceptedState = BuildPushVideoState();
		if (!rendererAcceptedState && m_videoRenderer &&
			m_rendererState == RendererState::RENDERSTATE_RENDERING)
		{
			DebugLog::Log(
				"LLDV profile metadata was rejected by the renderer; restarting graph");
			m_wantToRestartRenderer = true;
			UpdateState();
		}
		return;
	}
	
	// The reset coordinator owns resize debouncing. Keep this timer as a
	// compatibility cleanup path for any stale timer posted before coordination.
	if (nIDEvent == RESIZE_DEBOUNCE_TIMER_ID)
	{
		KillTimer(RESIZE_DEBOUNCE_TIMER_ID);
		return;
	}
	
	// Handle fullscreen focus grab
	if (nIDEvent == FULLSCREEN_FOCUS_TIMER_ID)
	{
		KillTimer(FULLSCREEN_FOCUS_TIMER_ID);

		if (m_fullScreenVideoWindow && IsWindow(m_fullScreenVideoWindow->GetHWND()))
		{
			DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnTimer(): FULLSCREEN_FOCUS - Grabbing focus")));
			const HWND fullscreenHwnd = m_fullScreenVideoWindow->GetHWND();
			const HMONITOR requestedMonitor = SelectFullscreenMonitor();
			MONITORINFO monitorInfo = { sizeof(monitorInfo) };
			RECT rectBefore = {};
			::GetWindowRect(fullscreenHwnd, &rectBefore);
			const BOOL visibleBefore = ::IsWindowVisible(fullscreenHwnd);
			const BOOL iconicBefore = ::IsIconic(fullscreenHwnd);
			if (iconicBefore)
				::ShowWindow(fullscreenHwnd, SW_RESTORE);
			else if (!visibleBefore)
				::ShowWindow(fullscreenHwnd, SW_SHOWNA);

			BOOL placementResult = FALSE;
			if (::GetMonitorInfo(requestedMonitor, &monitorInfo))
			{
				const RECT& targetRect = monitorInfo.rcMonitor;
				placementResult = ::SetWindowPos(
					fullscreenHwnd,
					m_windowedFullScreenMode ? HWND_TOP : HWND_TOPMOST,
					targetRect.left,
					targetRect.top,
					targetRect.right - targetRect.left,
					targetRect.bottom - targetRect.top,
					SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW |
					SWP_FRAMECHANGED);
			}
			const HWND foregroundBefore = ::GetForegroundWindow();
			const HWND focusBefore = ::GetFocus();
			const BOOL foregroundResult = ::SetForegroundWindow(fullscreenHwnd);
			const HWND focusResult = ::SetFocus(fullscreenHwnd);
			RECT rectAfter = {};
			::GetWindowRect(fullscreenHwnd, &rectAfter);
			const HMONITOR actualMonitor = ::MonitorFromWindow(
				fullscreenHwnd, MONITOR_DEFAULTTONULL);
			DebugLog::Log(
				"Fullscreen focus timer: target=%p visible_before=%d iconic_before=%d "
				"rect_before=%ld,%ld-%ld,%ld placement=%d requested_monitor=%p "
				"actual_monitor=%p monitor_matched=%d visible_after=%d "
				"rect_after=%ld,%ld-%ld,%ld foreground_before=%p focus_before=%p "
				"set_foreground=%d set_focus_previous=%p foreground_after=%p focus_after=%p",
				reinterpret_cast<void*>(fullscreenHwnd),
				visibleBefore ? 1 : 0,
				iconicBefore ? 1 : 0,
				rectBefore.left, rectBefore.top, rectBefore.right, rectBefore.bottom,
				placementResult ? 1 : 0,
				reinterpret_cast<void*>(requestedMonitor),
				reinterpret_cast<void*>(actualMonitor),
				actualMonitor == requestedMonitor ? 1 : 0,
				::IsWindowVisible(fullscreenHwnd) ? 1 : 0,
				rectAfter.left, rectAfter.top, rectAfter.right, rectAfter.bottom,
				reinterpret_cast<void*>(foregroundBefore),
				reinterpret_cast<void*>(focusBefore),
				foregroundResult ? 1 : 0,
				reinterpret_cast<void*>(focusResult),
				reinterpret_cast<void*>(::GetForegroundWindow()),
				reinterpret_cast<void*>(::GetFocus()));
		}
		return;
	}

	// EOTF CHANGE RESTART TIMER: Countdown to renderer restart after EOTF change
	if (nIDEvent == EOTF_CHANGE_RESTART_TIMER_ID)
	{
		if (m_eotfChangeRestartCooldownSeconds > 0)
		{
			m_eotfChangeRestartCooldownSeconds--;

			if (m_eotfChangeRestartCooldownSeconds == 0)
			{
				// Cooldown complete - execute restart
				KillTimer(EOTF_CHANGE_RESTART_TIMER_ID);

				if (m_videoRenderer && m_rendererState == RendererState::RENDERSTATE_RENDERING)
				{
					DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnTimer(): EOTF_CHANGE - Executing renderer restart")));
					DebugLog::Log("EOTF change: Executing renderer restart after stabilization period");

					m_wantToRestartRenderer = true;
					UpdateState();
				}

				m_eotfChangeRestartCooldownSeconds = -1;  // Reset cooldown
			}
		}
		else
		{
			// Cooldown is -1, kill the timer
			KillTimer(EOTF_CHANGE_RESTART_TIMER_ID);
			m_eotfChangeRestartCooldownSeconds = -1;
		}
		return;
	}

	// DeckLink reports LLDV as BT.2020 + SDR, so the normal raw-EOTF change
	// path cannot see the effective SDR-to-PQ transition.  Once the opt-in
	// heuristic has been stable, rebuild the graph after a short delay; this is
	// the same practical operation as Shift+R, but only for newlldv=true.
	if (nIDEvent == LLDV_CHANGE_RESTART_TIMER_ID)
	{
		if (--m_lldvChangeRestartDelaySeconds <= 0)
		{
			KillTimer(LLDV_CHANGE_RESTART_TIMER_ID);
			m_lldvChangeRestartDelaySeconds = -1;

			if (m_videoRenderer && m_rendererState == RendererState::RENDERSTATE_RENDERING)
			{
				DbgLog((LOG_TRACE, 1, TEXT("New LLDV heuristic: restarting renderer after PQ/HDR metadata stabilization")));
				DebugLog::Log("New LLDV heuristic confirmed: restarting renderer for effective PQ transition");
				m_wantToRestartRenderer = true;
				UpdateState();
			}
		}
		return;
	}

	// Handle regular 1-second timer for UI updates
	if (nIDEvent == TIMER_ID_1SECOND)
	{
		// A source can publish its BT.2020/SDR state only once at startup.
		// Re-evaluate the opt-in LLDV candidate here so confirmation does not
		// depend on receiving a second video-state notification.
		if (UpdateNewLldvCandidate() && IsNewLldvModeSelected())
		{
			DbgLog((LOG_TRACE, 1, TEXT("New LLDV heuristic confirmed; preparing PQ and synthetic HDR metadata")));

			// Build the effective PQ state now. Dynamic renderers apply it in place;
			// graph-based renderers retain the stabilized delayed-restart behavior
			// when their SDR media type cannot accept the effective PQ transition.
			const bool rendererAcceptedState = BuildPushVideoState();
			if (!m_videoRenderer ||
				!m_videoRenderer->SupportsDynamicVideoState() ||
				!rendererAcceptedState)
			{
				ScheduleNewLldvRendererRestart();
			}
			else
			{
				DebugLog::Log(
					"New LLDV heuristic confirmed: renderer accepted effective PQ transition in place; restart not required");
			}
		}

		// EOTF CHANGE CHECK: Decrement cooldown timer if set
		if (m_eotfCheckCooldownSeconds > 0)
		{
			m_eotfCheckCooldownSeconds--;
		}

		// SIMPLE EOTF CHANGE DETECTION: Every 5 seconds, check if EOTF changed since renderer started
		if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
			(!m_videoRenderer || !m_videoRenderer->SupportsDynamicVideoState()) &&
			m_timerSeconds % 5 == 0 &&
			m_eotfCheckCooldownSeconds == 0 &&  // Cooldown expired
			m_captureDeviceVideoState &&
			m_captureDeviceVideoState->valid &&
			m_rendererStartedWithEotf != EOTF::UNKNOWN &&
			m_captureDeviceVideoState->eotf != EOTF::UNKNOWN &&
			!m_wantToRestartCapture &&  // Don't trigger if restart already pending
			!m_wantToRestartRenderer)   // Don't trigger if restart already pending
		{
			// Check if EOTF changed since renderer started
			if (m_captureDeviceVideoState->eotf != m_rendererStartedWithEotf)
			{
				DbgLog((LOG_TRACE, 1, TEXT("EOTF changed %s -> %s while rendering - restarting capture"),
					ToString(m_rendererStartedWithEotf), ToString(m_captureDeviceVideoState->eotf)));

				DebugLog::Log("EOTF changed %s -> %s - restarting capture",
					CStringA(ToString(m_rendererStartedWithEotf)).GetString(),
					CStringA(ToString(m_captureDeviceVideoState->eotf)).GetString());

				// Trigger capture restart (which will restart renderer)
				if (m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_FAILED)
					m_captureDeviceState = CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN;

				m_wantToRestartCapture = true;
				UpdateState();

				// Reset cooldown to prevent rapid restarts
				m_eotfCheckCooldownSeconds = 5;
			}
		}

		CString cstring;

		if (m_rendererState == RendererState::RENDERSTATE_RENDERING)
		{
			// Auto-offset recalculation every 5 seconds (if enabled)
			if (m_timerSeconds % 5 == 0 &&
				m_timingClockFrameOffsetAutoCheck.GetCheck() &&
				m_captureDevice &&
				m_captureDeviceVideoState &&
				m_captureDeviceVideoState->valid)
			{
				int currentOffset = GetTimingClockFrameOffsetMs();
				int autoOffset = CalculateAutoFrameOffset();

				// Only update if offset changed by >= 2ms to avoid jitter
				if (abs(autoOffset - currentOffset) >= 2)
				{
					SetTimingClockFrameOffsetMs(autoOffset);
					UpdateTimingClockFrameOffset();
					DbgLog((LOG_TRACE, 1, TEXT("Auto-offset updated: %dms -> %dms"), currentOffset, autoOffset));
				}
			}

			// PERIODIC EOTF CHANGE DETECTION (every 5 seconds)
			// Catches EOTF changes that don't trigger full video state updates
			if (m_timerSeconds % 5 == 0 &&
				m_enableEotfChangeRestart &&
				m_captureDeviceVideoState &&
				m_captureDeviceVideoState->valid &&
				m_eotfChangeRestartCooldownSeconds < 0)  // Not in cooldown
			{
				EOTF currentEotf = m_captureDeviceVideoState->eotf;
				
				// Initialize tracking on first valid check
				if (m_lastKnownEotf == EOTF::UNKNOWN && currentEotf != EOTF::UNKNOWN)
				{
					m_lastKnownEotf = currentEotf;
					DbgLog((LOG_TRACE, 1, TEXT("Periodic EOTF check: Initialized to %s"), ToString(currentEotf)));
				}
				// Detect EOTF change
				else if (m_lastKnownEotf != EOTF::UNKNOWN &&
						 currentEotf != EOTF::UNKNOWN &&
						 m_lastKnownEotf != currentEotf)
				{
					DbgLog((LOG_TRACE, 1, TEXT("Periodic EOTF check: EOTF changed %s -> %s - scheduling renderer restart"),
						ToString(m_lastKnownEotf), ToString(currentEotf)));

					DebugLog::Log("Periodic EOTF change detected: %s -> %s - renderer restart in 5 seconds",
						CStringA(ToString(m_lastKnownEotf)).GetString(),
						CStringA(ToString(currentEotf)).GetString());

					// Update tracked EOTF immediately
					m_lastKnownEotf = currentEotf;

					// Schedule restart with delay
					m_eotfChangeRestartCooldownSeconds = 5;
					SetTimer(EOTF_CHANGE_RESTART_TIMER_ID, 1000, nullptr);
				}
			}

			const size_t rawQueueSize = m_videoRenderer->GetFrameQueueSize();
			const size_t convertedQueueSize = m_videoRenderer->GetConvertedQueueSize();
			const size_t currentQueueSize = rawQueueSize + convertedQueueSize;
			
			const uint64_t droppedFrames = m_videoRenderer->DroppedFrameCount();

			const int selectedRenderer = m_rendererCombo.GetCurSel();
			const RendererId* renderer = selectedRenderer >= 0 ?
				reinterpret_cast<const RendererId*>(m_rendererCombo.GetItemData(selectedRenderer)) : nullptr;
			if (renderer && renderer->backend == RendererBackend::LIBPLACEBO)
				cstring.Format(_T("%zu / %zu"), rawQueueSize, GetRendererVideoFrameQueueSizeMax());
			else
				cstring.Format(_T("%zu/%zu/%zu"), rawQueueSize, convertedQueueSize, currentQueueSize);
			m_rendererVideoFrameQueueSizeText.SetWindowText(cstring);

			RendererLatencySnapshot latencySnapshot;
			if (m_videoRenderer->GetLatencySnapshot(latencySnapshot))
			{
				cstring.Format(_T("%.01f"), latencySnapshot.vpInternalMs);
				m_rendererLatencyToVPText.SetWindowText(cstring);
				if (latencySnapshot.scheduledPresentationKnown)
				{
					cstring.Format(_T("%.01f"), latencySnapshot.dsScheduleLeadMs);
					m_rendererLatencyDsLeadText.SetWindowText(cstring);
					cstring.Format(_T("%.01f"), latencySnapshot.scheduledLatencyMs);
					m_rendererLatencyToDSText.SetWindowText(cstring);
				}
				else
				{
					m_rendererLatencyDsLeadText.SetWindowText(_T("---"));
					m_rendererLatencyToDSText.SetWindowText(_T("---"));
				}
			}
			else if (renderer && renderer->backend == RendererBackend::LIBPLACEBO)
			{
				const double alphaInternalMs = std::max(0.0,
					m_videoRenderer->ExitLatencyMs() -
					m_videoRenderer->EntryLatencyMs());
				cstring.Format(_T("%.01f"), alphaInternalMs);
				m_rendererLatencyToVPText.SetWindowText(cstring);
				m_rendererLatencyDsLeadText.SetWindowText(_T("---"));
				m_rendererLatencyToDSText.SetWindowText(_T("---"));
			}
			else
			{
				m_rendererLatencyToVPText.SetWindowText(_T("---"));
				m_rendererLatencyDsLeadText.SetWindowText(_T("---"));
				m_rendererLatencyToDSText.SetWindowText(_T("---"));
			}

			cstring.Format(_T("%lu"), droppedFrames);
			m_rendererDroppedFrameCountText.SetWindowText(cstring);

			// INTELLIGENT QUEUE HEALTH MONITORING
			MonitorQueueHealth(rawQueueSize, convertedQueueSize,
				GetRendererVideoFrameQueueSizeMax(), droppedFrames);
		}
		else
		{
			m_rendererVideoFrameQueueSizeText.SetWindowText(_T(""));
			m_rendererLatencyToVPText.SetWindowText(_T(""));
			m_rendererLatencyDsLeadText.SetWindowText(_T(""));
			m_rendererLatencyToDSText.SetWindowText(_T(""));
			m_rendererDroppedFrameCountText.SetWindowText(TEXT(""));
		}

		if (m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		{
			cstring.Format(_T("%lu"), m_captureDevice->VideoFrameCapturedCount());
			m_inputVideoFrameCountText.SetWindowText(cstring);

			cstring.Format(_T("%lu"), m_captureDevice->VideoFrameMissedCount());
			m_inputVideoFrameMissedText.SetWindowText(cstring);

			// DeckLink hardware-latency measurement was removed from the capture
			// callback. Do not display its zero-initialized storage as a measured
			// end-to-end latency.
			m_inputLatencyMsText.SetWindowText(_T("---"));
		}
		else
		{
			m_inputVideoFrameCountText.SetWindowText(TEXT(""));
			m_inputVideoFrameMissedText.SetWindowText(TEXT(""));
			m_inputLatencyMsText.SetWindowText(_T(""));
		}

		// Prevent screensaver
		if (m_timerSeconds % 60 == 0)
		{
			SetThreadExecutionState(ES_DISPLAY_REQUIRED);
		}

		UpdateStatsOverlay();


		++m_timerSeconds;
	}

	CDialog::OnTimer(nIDEvent);
}

void CVideoProcessorDlg::UpdateStatsOverlay()
{
	HWND displayWindow = nullptr;
	if (m_fullScreenVideoWindow && IsWindow(m_fullScreenVideoWindow->GetHWND()))
		displayWindow = m_fullScreenVideoWindow->GetHWND();
	else if (m_windowedVideoWindow.GetSafeHwnd())
		displayWindow = m_windowedVideoWindow.GetSafeHwnd();
	// Keep the fallback vblank sampler associated with the active render window
	// even when DWM timing is available. Scene Detect uses the sampler's phase
	// only while enabled; otherwise it remains on its low-overhead cadence.
	g_displayRefreshRateSampler->SetWindow(displayWindow);
	const double activeTargetRefreshRate =
		GetActiveTargetRefreshRate(displayWindow);
	if (m_videoRenderer && !m_activeRendererIsDirectShow &&
		activeTargetRefreshRate > 0.0)
	{
		m_lastAlphaTargetRefreshRateHz = activeTargetRefreshRate;
	}
	// Seed interval compensation from the configured path family. The selected
	// value still comes only from measured DXGI samples after validation.
	g_displayRefreshRateSampler->SetNominalRate(activeTargetRefreshRate);
	g_displayRefreshRateSampler->SetPhaseTracking(m_sceneAwareTimingCorrection);
	// DWM provides a useful current phase, but qpcRefreshPeriod is effectively
	// the nominal compositor period rather than the measured physical display
	// rate. Scene correction must only use a recent physical-vblank average.
	DisplayTimingSnapshot displayTiming = GetDisplayTimingSnapshot(displayWindow);
	const DisplayTimingSnapshot sampledDisplayTiming =
		g_displayRefreshRateSampler->GetTimingSnapshot();
	LARGE_INTEGER qpcNow = {};
	QueryPerformanceCounter(&qpcNow);
	const bool sampledRateIsFresh =
		sampledDisplayTiming.refreshRateHz > 0.0 &&
		sampledDisplayTiming.qpcFrequency > 0 &&
		sampledDisplayTiming.rateMeasuredQpc > 0 &&
		qpcNow.QuadPart >= sampledDisplayTiming.rateMeasuredQpc &&
		(qpcNow.QuadPart - sampledDisplayTiming.rateMeasuredQpc) <=
			sampledDisplayTiming.qpcFrequency * 20;
	const bool startupRateIsFresh =
		sampledDisplayTiming.startupRefreshRateHz > 0.0 &&
		sampledDisplayTiming.qpcFrequency > 0 &&
		sampledDisplayTiming.startupRateMeasuredQpc > 0 &&
		qpcNow.QuadPart >= sampledDisplayTiming.startupRateMeasuredQpc &&
		(qpcNow.QuadPart - sampledDisplayTiming.startupRateMeasuredQpc) <=
			sampledDisplayTiming.qpcFrequency * 5;
	const double rawWaitMinimumMs =
		sampledDisplayTiming.qpcFrequency > 0 &&
		sampledDisplayTiming.minimumWaitIntervalQpc > 0 ?
		static_cast<double>(sampledDisplayTiming.minimumWaitIntervalQpc) * 1000.0 /
			static_cast<double>(sampledDisplayTiming.qpcFrequency) : 0.0;
	const double rawWaitMaximumMs =
		sampledDisplayTiming.qpcFrequency > 0 &&
		sampledDisplayTiming.maximumWaitIntervalQpc > 0 ?
		static_cast<double>(sampledDisplayTiming.maximumWaitIntervalQpc) * 1000.0 /
			static_cast<double>(sampledDisplayTiming.qpcFrequency) : 0.0;
	const double startupRawWaitMinimumMs =
		sampledDisplayTiming.qpcFrequency > 0 &&
		sampledDisplayTiming.startupMinimumWaitIntervalQpc > 0 ?
		static_cast<double>(sampledDisplayTiming.startupMinimumWaitIntervalQpc) *
			1000.0 / static_cast<double>(sampledDisplayTiming.qpcFrequency) : 0.0;
	const double startupRawWaitMaximumMs =
		sampledDisplayTiming.qpcFrequency > 0 &&
		sampledDisplayTiming.startupMaximumWaitIntervalQpc > 0 ?
		static_cast<double>(sampledDisplayTiming.startupMaximumWaitIntervalQpc) *
			1000.0 / static_cast<double>(sampledDisplayTiming.qpcFrequency) : 0.0;
	// Keep current readiness and phase-sensitive cadence validation separate.
	// The former needs a clean, recent rate promptly; the latter intentionally
	// waits for the longer weighted-history predicate.
	DisplayRefreshRateInput displayRateInput;
	displayRateInput.candidateRateHz = sampledDisplayTiming.refreshRateHz;
	displayRateInput.rawWaitRateHz = sampledDisplayTiming.rawWaitRateHz;
	displayRateInput.nominalRateHz = activeTargetRefreshRate;
	displayRateInput.minimumWaitIntervalMs = rawWaitMinimumMs;
	displayRateInput.maximumWaitIntervalMs = rawWaitMaximumMs;
	displayRateInput.compensatedIntervals =
		sampledDisplayTiming.intervalsObserved;
	displayRateInput.rawWaitIntervals =
		sampledDisplayTiming.rawWaitIntervalsObserved;
	displayRateInput.fresh = sampledRateIsFresh;
	displayRateInput.stable = sampledDisplayTiming.rateStable;
	const DisplayRefreshRateResult displayRateResult =
		EvaluateDisplayRefreshRate(displayRateInput);
	DisplayRefreshRateInput readinessRateInput = displayRateInput;
	readinessRateInput.candidateRateHz =
		sampledDisplayTiming.readinessRefreshRateHz;
	readinessRateInput.readinessObservationSeconds =
		sampledDisplayTiming.readinessEvidenceSeconds;
	// Output readiness uses the policy's short evidence rule. It must not
	// borrow the phase predicate, which deliberately requires a longer run.
	readinessRateInput.stable = false;
	const DisplayRefreshRateResult readinessRateResult =
		EvaluateDisplayRefreshRate(readinessRateInput);
	// Startup evidence uses the first credible two seconds of DXGI vblank
	// intervals. It is separately cadence- and nominal-validated, rather than
	// borrowing samples from the post-transition quarantine or phase history.
	DisplayRefreshRateInput startupRateInput;
	startupRateInput.candidateRateHz =
		sampledDisplayTiming.startupRefreshRateHz;
	startupRateInput.rawWaitRateHz =
		sampledDisplayTiming.startupRawWaitRateHz;
	startupRateInput.nominalRateHz = activeTargetRefreshRate;
	startupRateInput.minimumWaitIntervalMs = startupRawWaitMinimumMs;
	startupRateInput.maximumWaitIntervalMs = startupRawWaitMaximumMs;
	startupRateInput.compensatedIntervals =
		sampledDisplayTiming.startupIntervalsObserved;
	startupRateInput.rawWaitIntervals =
		sampledDisplayTiming.startupRawWaitIntervalsObserved;
	startupRateInput.startupObservationSeconds =
		sampledDisplayTiming.startupEvidenceSeconds;
	startupRateInput.fresh = startupRateIsFresh;
	startupRateInput.stable = false;
	const DisplayRefreshRateResult startupRateResult =
		EvaluateDisplayRefreshRate(startupRateInput);
	const double measuredDisplayRefreshRate =
		displayRateResult.selectedRateHz;
	const double nominalInputRefreshRate =
		m_captureDeviceVideoState && m_captureDeviceVideoState->valid &&
		m_captureDeviceVideoState->displayMode ?
			m_captureDeviceVideoState->displayMode->RefreshRateHz() : 0.0;
	double configuredDisplayRefreshRate = 0.0;
	int matchedOverrideNominalRate = 0;
	const bool displayRefreshRateOverridden = nominalInputRefreshRate > 0.0 &&
		TryGetDisplayRefreshRateOverride(nominalInputRefreshRate,
			configuredDisplayRefreshRate, matchedOverrideNominalRate);
	double madVRDetectedRefreshRate = 0.0;
	const bool madVRDetectedRefreshRateKnown =
		m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		m_videoRenderer && m_videoRenderer->GetDetectedDisplayRefreshRate(
			madVRDetectedRefreshRate);
	// An explicit configuration remains authoritative. Otherwise, madVR's own
	// settled display measurement is preferred for its graph. DXGI remains the
	// immediate source during HDMI/renderer warm-up and the fallback for Alpha.
	const double displayRefreshRate = displayRefreshRateOverridden ?
		configuredDisplayRefreshRate :
		(madVRDetectedRefreshRateKnown ? madVRDetectedRefreshRate :
			measuredDisplayRefreshRate);
	const std::wstring monitorDeviceName = GetMonitorDeviceName(displayWindow);
	const double dxgiTargetMismatchPpm =
		sampledDisplayTiming.refreshRateHz > 0.0 &&
		activeTargetRefreshRate > 0.0 ?
			(sampledDisplayTiming.refreshRateHz /
				activeTargetRefreshRate - 1.0) * 1000000.0 : 0.0;

	// A large mismatch can poison the cumulative estimator indefinitely. Start
	// a bounded number of fresh generations for the same display contract so a
	// newer credible measurement can replace it without creating a reset loop.
	static std::wstring recoveryMonitorDeviceName;
	static double recoveryTargetRate = 0.0;
	static unsigned int recoveryAttempts = 0;
	static ULONGLONG lastRecoveryTick = 0;
	const ULONGLONG displayTimingLogTick = GetTickCount64();
	const bool recoveryContractChanged =
		recoveryMonitorDeviceName != monitorDeviceName ||
		std::fabs(recoveryTargetRate - activeTargetRefreshRate) >= 0.01;
	if (recoveryContractChanged)
	{
		recoveryMonitorDeviceName = monitorDeviceName;
		recoveryTargetRate = activeTargetRefreshRate;
		recoveryAttempts = 0;
		lastRecoveryTick = 0;
	}
	if (displayRateResult.decision == DisplayRefreshRateDecision::Accepted)
		recoveryAttempts = 0;
	bool recoveryRequested = false;
	if (displayRateResult.shouldRecalculate && recoveryAttempts < 2 &&
		(lastRecoveryTick == 0 ||
			displayTimingLogTick - lastRecoveryTick >= 5000))
	{
		g_displayRefreshRateSampler->ResetMeasurement();
		++recoveryAttempts;
		lastRecoveryTick = displayTimingLogTick;
		recoveryRequested = true;
	}

	// Log material rate decisions and a bounded periodic warm-up record. DXGI
	// remains the measured source; DWM and the target path are cross-checks.
	static double lastLoggedCandidateRate = 0.0;
	static double lastLoggedDwmPeriodRate = 0.0;
	static double lastLoggedDwmAdvertisedRate = 0.0;
	static double lastLoggedTargetRate = 0.0;
	static double lastLoggedSelectedRate = 0.0;
	static DisplayRefreshRateDecision lastLoggedDecision =
		DisplayRefreshRateDecision::Unavailable;
	static DisplayRefreshRateReason lastLoggedReason =
		DisplayRefreshRateReason::NoSamples;
	static uint64_t lastLoggedGeneration = 0;
	static bool lastLoggedOverrideActive = false;
	static ULONGLONG lastDisplayTimingLogTick = 0;
	static double lastAcceptedMeasuredRate = 0.0;
	static uint64_t lastAcceptedGeneration = 0;
	static ULONGLONG lastAcceptedTick = 0;
	const auto rateChanged = [](double previous, double current) {
		return (previous <= 0.0) != (current <= 0.0) ||
			(previous > 0.0 && current > 0.0 &&
				fabs(previous - current) / previous >= 0.0001);
	};
	if (sampledDisplayTiming.refreshRateHz > 0.0 ||
		displayRefreshRateOverridden ||
		displayTiming.refreshRateHz > 0.0 ||
		activeTargetRefreshRate > 0.0)
	{
		const ULONGLONG periodicLogInterval =
			displayRateResult.decision == DisplayRefreshRateDecision::Accepted ?
			30000 : 5000;
		const bool shouldLogDisplayTiming = lastDisplayTimingLogTick == 0 ||
			displayTimingLogTick - lastDisplayTimingLogTick >=
				periodicLogInterval ||
			rateChanged(lastLoggedCandidateRate,
				sampledDisplayTiming.refreshRateHz) ||
			rateChanged(lastLoggedDwmPeriodRate, displayTiming.refreshRateHz) ||
			rateChanged(lastLoggedDwmAdvertisedRate,
				displayTiming.advertisedRefreshRateHz) ||
			rateChanged(lastLoggedTargetRate, activeTargetRefreshRate) ||
			rateChanged(lastLoggedSelectedRate, displayRefreshRate) ||
			lastLoggedDecision != displayRateResult.decision ||
			lastLoggedReason != displayRateResult.reason ||
			lastLoggedGeneration != sampledDisplayTiming.generation ||
			recoveryRequested ||
			lastLoggedOverrideActive != displayRefreshRateOverridden;
		if (shouldLogDisplayTiming)
		{
			std::ostringstream selectedRateText;
			const char* selectedSource = "measured unavailable";
			if (displayRefreshRateOverridden)
			{
				selectedRateText << std::fixed << std::setprecision(6)
					<< configuredDisplayRefreshRate << " Hz";
				selectedSource = "CONFIG OVERRIDE";
			}
			else if (madVRDetectedRefreshRateKnown)
			{
				selectedRateText << std::fixed << std::setprecision(6)
					<< madVRDetectedRefreshRate << " Hz";
				selectedSource = "madVR detected refresh";
			}
			else if (measuredDisplayRefreshRate > 0.0)
			{
				selectedRateText << std::fixed << std::setprecision(6)
					<< measuredDisplayRefreshRate << " Hz";
				selectedSource = "measured DXGI";
			}
			else
			{
				selectedRateText << "---";
			}
			const double candidateAgeSeconds =
				sampledDisplayTiming.qpcFrequency > 0 &&
				sampledDisplayTiming.rateMeasuredQpc > 0 &&
				qpcNow.QuadPart >= sampledDisplayTiming.rateMeasuredQpc ?
				static_cast<double>(qpcNow.QuadPart -
					sampledDisplayTiming.rateMeasuredQpc) /
					static_cast<double>(sampledDisplayTiming.qpcFrequency) :
				0.0;
			const double previousSelectedAgeSeconds =
				lastAcceptedTick > 0 &&
				displayTimingLogTick >= lastAcceptedTick ?
				static_cast<double>(displayTimingLogTick -
					lastAcceptedTick) / 1000.0 : 0.0;
			const char* preventedConsumers =
				displayRefreshRateOverridden || madVRDetectedRefreshRateKnown ||
				displayRateResult.decision ==
					DisplayRefreshRateDecision::Accepted ?
				"none" : "OSD,scene-aware,PPM/delivery";
			DebugLog::Log(
				"Display timing sources: monitor=%ls; DXGI WaitForVBlank=%.6f Hz "
					"(fresh=%d stable=%d compensated=%llu raw=%.6f Hz rawCount=%llu "
					"rawGap=%.3f..%.3fms); DWM period=%.6f Hz advertised=%.6f Hz "
					"composition=%d result=0x%08lX; Windows target path=%.6f Hz; "
					"DXGI-target=%+.1f ppm; generation=%llu candidateAge=%.1fs; "
					"previous=%.6f Hz generation=%llu age=%.1fs; "
					"decision=%s reason=\"%s\" recalculate=%d attempt=%u/2; "
					"selected=%s (%s%s); prevented=%s",
				monitorDeviceName.c_str(),
				sampledDisplayTiming.refreshRateHz,
				sampledRateIsFresh ? 1 : 0,
				sampledDisplayTiming.rateStable ? 1 : 0,
				static_cast<unsigned long long>(sampledDisplayTiming.intervalsObserved),
				sampledDisplayTiming.rawWaitRateHz,
				static_cast<unsigned long long>(sampledDisplayTiming.rawWaitIntervalsObserved),
				rawWaitMinimumMs, rawWaitMaximumMs,
				displayTiming.refreshRateHz,
				displayTiming.advertisedRefreshRateHz,
				displayTiming.dwmCompositionEnabled ? 1 : 0,
				static_cast<unsigned long>(displayTiming.dwmTimingResult),
				activeTargetRefreshRate,
				dxgiTargetMismatchPpm,
				static_cast<unsigned long long>(
					sampledDisplayTiming.generation),
				candidateAgeSeconds,
				lastAcceptedMeasuredRate,
				static_cast<unsigned long long>(lastAcceptedGeneration),
				previousSelectedAgeSeconds,
				ToString(displayRateResult.decision),
				ToString(displayRateResult.reason),
				recoveryRequested ? 1 : 0,
				recoveryAttempts,
				selectedRateText.str().c_str(),
				selectedSource,
				displayRefreshRateOverridden ?
					(std::string(" nominal=") +
						std::to_string(matchedOverrideNominalRate)).c_str() :
					"",
				preventedConsumers);
			lastLoggedCandidateRate =
				sampledDisplayTiming.refreshRateHz;
			lastLoggedDwmPeriodRate = displayTiming.refreshRateHz;
			lastLoggedDwmAdvertisedRate = displayTiming.advertisedRefreshRateHz;
			lastLoggedTargetRate = activeTargetRefreshRate;
			lastLoggedSelectedRate = displayRefreshRate;
			lastLoggedDecision = displayRateResult.decision;
			lastLoggedReason = displayRateResult.reason;
			lastLoggedGeneration = sampledDisplayTiming.generation;
			lastLoggedOverrideActive = displayRefreshRateOverridden;
			lastDisplayTimingLogTick = displayTimingLogTick;
		}
	}
	if (displayRateResult.decision == DisplayRefreshRateDecision::Accepted &&
		(lastAcceptedGeneration != sampledDisplayTiming.generation ||
			rateChanged(lastAcceptedMeasuredRate, measuredDisplayRefreshRate)))
	{
		lastAcceptedMeasuredRate = measuredDisplayRefreshRate;
		lastAcceptedGeneration = sampledDisplayTiming.generation;
		lastAcceptedTick = displayTimingLogTick;
	}

	// A validated DXGI measurement is evidence that the renderer/display timing
	// path is usable, not proof that an HDMI sink has physically locked and not
	// an observation of madVR's internal queues. It selects the proven
	// DirectShow/madVR graph re-prime transaction; it does not size madVR.
	OutputReadinessInput readinessInput;
	// Renderer lifecycle and sampler generations are both readiness boundaries:
	// never carry a validated rate across either one.
	readinessInput.transitionGeneration =
		(static_cast<uint64_t>(m_transitionGeneration) << 32) ^
		(sampledDisplayTiming.generation & 0xffffffffULL);
	readinessInput.graphOperational =
		m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		m_videoRenderer != nullptr && !m_rendererResetTransitionActive;
	RendererLivenessSnapshot readinessLiveness;
	const bool hasReadinessLiveness = m_activeRendererIsDirectShow &&
		m_videoRenderer && m_videoRenderer->GetLivenessSnapshot(readinessLiveness) &&
		readinessLiveness.supported;
	// An explicit [queue] steady value controls the VP prefill/cushion for this
	// fresh epoch. It never sizes madVR. Without one, retain the proven automatic
	// eight-frame readiness reserve.
	const size_t configuredVpReserveFrames =
		videoProcessorApp.GetQueueSteadyReserveFrames();
	const size_t queueCapacity = hasReadinessLiveness &&
		readinessLiveness.queueCapacity > 0 ?
		readinessLiveness.queueCapacity : 32;
	const size_t requestedVpReserveFrames =
		videoProcessorApp.HasQueueSteadyReserveFrames() ?
		std::min(configuredVpReserveFrames, queueCapacity) :
		std::min<size_t>(8, queueCapacity);
	const uint64_t readinessObservationTick = GetTickCount64();
	readinessInput.observationTickMs = readinessObservationTick;
	if (m_currentGraphPrimeObservedTransitionGeneration !=
		readinessInput.transitionGeneration)
	{
		const bool firstObservedGeneration =
			m_currentGraphPrimeObservedTransitionGeneration == 0;
		const bool rendererGenerationChanged = !firstObservedGeneration &&
			(m_currentGraphPrimeObservedTransitionGeneration >> 32) !=
				(readinessInput.transitionGeneration >> 32);
		m_currentGraphPrimeObservedTransitionGeneration =
			readinessInput.transitionGeneration;
		m_currentGraphPrimeTransitionStartTick = firstObservedGeneration ?
			0 : readinessObservationTick;
		m_currentGraphPrimeEvidenceEpoch = 0;
		m_currentGraphPrimeEvidenceTick = 0;
		m_currentGraphPrimeEvidenceTransitionGeneration = 0;
		if (rendererGenerationChanged)
		{
			m_currentGraphPrimeObservedQueueEpoch = 0;
			m_currentGraphPrimeQueueTransitionGeneration = 0;
		}
	}
	if (hasReadinessLiveness && readinessLiveness.queueEpoch != 0 &&
		m_currentGraphPrimeObservedQueueEpoch != readinessLiveness.queueEpoch)
	{
		// Bind the queue epoch before consuming any convergence proof. A later
		// sampler-generation change cannot relabel an older live epoch as proof
		// for the new output contract.
		m_currentGraphPrimeObservedQueueEpoch = readinessLiveness.queueEpoch;
		m_currentGraphPrimeQueueTransitionGeneration =
			readinessInput.transitionGeneration;
	}
	const bool currentGraphBoundarySafe = hasReadinessLiveness &&
		readinessLiveness.active && readinessLiveness.queueEpoch != 0 &&
		!readinessLiveness.resetInProgress &&
		!m_rendererResetTransitionActive &&
		!m_outputReadinessGraphReprimeActive &&
		!m_fullscreenRetargetPending &&
		!m_rendererRetirementPending &&
		!m_wantToRestartRenderer &&
		!m_wantToTerminate &&
		!m_directShowGraphRecoveryAwaitingHealth &&
		!m_directShowRecoveryRebuildRequested &&
		!RendererResetOperationInProgress() &&
		m_rendererTransitionModel.State() == RendererTransitionState::Visible;
	if (hasReadinessLiveness &&
		readinessLiveness.convergenceAppliedEpoch != 0 &&
		(m_currentGraphPrimeTransitionStartTick == 0 ||
		 readinessLiveness.convergenceAppliedTick >
			m_currentGraphPrimeTransitionStartTick) &&
		(readinessLiveness.convergenceAppliedEpoch !=
			m_currentGraphPrimeEvidenceEpoch ||
		 readinessLiveness.convergenceAppliedTick !=
			m_currentGraphPrimeEvidenceTick))
	{
		m_currentGraphPrimeEvidenceEpoch =
			readinessLiveness.convergenceAppliedEpoch;
		m_currentGraphPrimeEvidenceTick =
			readinessLiveness.convergenceAppliedTick;
		m_currentGraphPrimeEvidenceTransitionGeneration =
			readinessInput.transitionGeneration;
	}
	const bool currentGraphPrimeProven = currentGraphBoundarySafe &&
		readinessLiveness.convergenceHardBlockRecovered &&
		readinessLiveness.convergenceConvertedQueueWasFull &&
		readinessLiveness.convergenceAppliedEpoch != 0 &&
		readinessLiveness.convergenceAppliedEpoch ==
			readinessLiveness.queueEpoch &&
		m_currentGraphPrimeObservedQueueEpoch ==
			readinessLiveness.queueEpoch &&
		m_currentGraphPrimeQueueTransitionGeneration ==
			readinessInput.transitionGeneration &&
		m_currentGraphPrimeEvidenceEpoch ==
			readinessLiveness.convergenceAppliedEpoch &&
		m_currentGraphPrimeEvidenceTransitionGeneration ==
			readinessInput.transitionGeneration;
	const bool currentGraphPrimeCandidateSameEpoch = hasReadinessLiveness &&
		readinessLiveness.convergenceAppliedEpoch != 0 &&
		readinessLiveness.convergenceAppliedEpoch == readinessLiveness.queueEpoch;
	const uint64_t postProofDeliverySuccesses =
		currentGraphPrimeCandidateSameEpoch &&
		readinessLiveness.currentEpochDeliverySuccessCount >=
			readinessLiveness.convergenceDeliverySuccessCount ?
		readinessLiveness.currentEpochDeliverySuccessCount -
			readinessLiveness.convergenceDeliverySuccessCount : 0;
	const bool currentGraphDeliveryRecent = currentGraphPrimeProven &&
		readinessLiveness.lastDeliverySuccessQueueEpoch ==
			readinessLiveness.queueEpoch &&
		readinessLiveness.lastDeliverySuccessTick != 0 &&
		readinessObservationTick >= readinessLiveness.lastDeliverySuccessTick &&
		readinessObservationTick - readinessLiveness.lastDeliverySuccessTick <= 500 &&
		!readinessLiveness.deliveryInProgress;
	const bool readinessGraphResetCompleted =
		m_outputReadinessResetCompletedGeneration ==
			readinessInput.transitionGeneration &&
		m_outputReadinessResetCompletedEpoch != 0;
	const bool existingGraphResetCanSatisfyReadiness = hasReadinessLiveness &&
		m_outputReadinessExistingGraphResetGeneration ==
			readinessInput.transitionGeneration &&
		m_outputReadinessExistingGraphResetEpoch != 0 &&
		readinessLiveness.queueEpoch == m_outputReadinessExistingGraphResetEpoch;
	const bool recoveryRecreationCanSatisfyReadiness = hasReadinessLiveness &&
		m_directShowRecoveryRecreatedGeneration == m_transitionGeneration &&
		readinessLiveness.queueEpoch != 0;
	readinessInput.postReadyResetCompleted = readinessGraphResetCompleted ||
		existingGraphResetCanSatisfyReadiness ||
		recoveryRecreationCanSatisfyReadiness;
	readinessInput.postReadyEpoch = readinessGraphResetCompleted ?
		m_outputReadinessResetCompletedEpoch :
		(existingGraphResetCanSatisfyReadiness ?
			m_outputReadinessExistingGraphResetEpoch :
			(recoveryRecreationCanSatisfyReadiness ?
				readinessLiveness.queueEpoch : 0));
	readinessInput.currentEpochProcessedDepth = hasReadinessLiveness &&
		readinessLiveness.queueEpoch == readinessInput.postReadyEpoch ?
		readinessLiveness.convertedQueueDepth : 0;
	// The pin snapshot can still describe the old reserve while a new policy is
	// being published. Use this generation's selected policy consistently for
	// both reset prefill and the controller's completion criterion.
	readinessInput.reserveFrames = requestedVpReserveFrames;
	readinessInput.currentGraphPrimeProven = currentGraphPrimeProven;
	readinessInput.currentGraphPrimeObservedFullConvertedQueue =
		currentGraphPrimeProven &&
		readinessLiveness.convergenceConvertedQueueWasFull;
	readinessInput.currentGraphBoundarySafe = currentGraphBoundarySafe;
	readinessInput.currentGraphDeliveryRecent = currentGraphDeliveryRecent;
	readinessInput.currentGraphPrimeTransitionGeneration =
		currentGraphPrimeCandidateSameEpoch ?
			m_currentGraphPrimeEvidenceTransitionGeneration : 0;
	readinessInput.currentGraphPrimeEpoch = currentGraphPrimeCandidateSameEpoch ?
		readinessLiveness.convergenceAppliedEpoch : 0;
	readinessInput.currentGraphPrimeTargetFrames = currentGraphPrimeCandidateSameEpoch ?
		readinessLiveness.convergenceTargetFrames : 0;
	readinessInput.currentGraphRawDepth = hasReadinessLiveness ?
		readinessLiveness.rawQueueDepth : 0;
	readinessInput.currentGraphConvertedDepth = hasReadinessLiveness ?
		readinessLiveness.convertedQueueDepth : 0;
	readinessInput.currentGraphPostProofDeliverySuccesses =
		static_cast<uint32_t>(std::min<uint64_t>(
			postProofDeliverySuccesses,
			std::numeric_limits<uint32_t>::max()));
	readinessInput.currentGraphMaximumSuccessfulDeliveryDurationUs =
		hasReadinessLiveness ?
			readinessLiveness.maximumSuccessfulDeliveryDurationUs : 0;
	// Phase correction waits for DisplayRefreshRateDecision::Accepted. Output
	// readiness instead uses independently cadence-validated startup evidence,
	// so it need not impose a multi-second first-image blackout.
	readinessInput.displayDecision = startupRateResult.startupValidated ?
		DisplayRefreshRateDecision::Accepted : startupRateResult.decision;
	readinessInput.displayReason = startupRateResult.startupValidated ?
		DisplayRefreshRateReason::Accepted : startupRateResult.reason;
	readinessInput.expectedOutputRefreshHz = activeTargetRefreshRate;
	readinessInput.observedOutputRefreshHz =
		startupRateResult.startupValidated ?
			startupRateResult.startupRateHz :
			sampledDisplayTiming.startupRefreshRateHz;
	const OutputReadinessDecision readinessDecision =
		m_outputReadinessObserver.Observe(readinessInput);
	if (readinessDecision.adoptedCurrentGraph &&
		m_activeRendererIsDirectShow && m_videoRenderer &&
		hasReadinessLiveness)
	{
		m_videoRenderer->SetOutputReadinessDeliveryReserve(
			requestedVpReserveFrames);
		m_outputReadinessExistingGraphReservePublishedEpoch =
			readinessDecision.postReadyEpoch;
		DebugLog::Log(
			"Output readiness adopted recovered current graph: "
			"generation=%llu epoch=%llu target=%zu raw=%zu converted=%zu/%zu "
			"post_proof_success=%llu retained_source=%zu high_water=%zu "
			"oldest_source_ms=%llu max_deliver_us=%llu "
			"madvr_queue=unobservable",
			static_cast<unsigned long long>(
				readinessInput.transitionGeneration),
			static_cast<unsigned long long>(readinessDecision.postReadyEpoch),
			requestedVpReserveFrames,
			readinessLiveness.rawQueueDepth,
			readinessLiveness.convertedQueueDepth,
			readinessLiveness.queueCapacity,
			static_cast<unsigned long long>(postProofDeliverySuccesses),
			readinessLiveness.retainedSourceBufferCount,
			readinessLiveness.retainedSourceBufferHighWater,
			static_cast<unsigned long long>(
				readinessLiveness.oldestRetainedSourceBufferAgeMs),
			static_cast<unsigned long long>(
				readinessLiveness.maximumSuccessfulDeliveryDurationUs));
	}
	if ((existingGraphResetCanSatisfyReadiness ||
		recoveryRecreationCanSatisfyReadiness) &&
		(readinessDecision.state == OutputReadinessState::Prefilling ||
			readinessDecision.state == OutputReadinessState::Steady) &&
		m_outputReadinessExistingGraphReservePublishedEpoch !=
			readinessInput.postReadyEpoch)
	{
		// The completed graph reset already re-primed madVR. Publish the VP floor
		// into that fresh epoch rather than adding a second reset/black interval.
		m_videoRenderer->SetOutputReadinessDeliveryReserve(
			requestedVpReserveFrames);
		m_outputReadinessExistingGraphReservePublishedEpoch =
			readinessInput.postReadyEpoch;
		DebugLog::Log(
			"Output readiness adopted %s: generation=%llu "
			"epoch=%llu reserve=%zu VPdepth=%zu/%zu madvr_queue=unobservable",
			recoveryRecreationCanSatisfyReadiness ?
				"recovery renderer recreation" : "existing graph re-prime",
			static_cast<unsigned long long>(
				readinessInput.transitionGeneration),
			static_cast<unsigned long long>(readinessInput.postReadyEpoch),
			requestedVpReserveFrames,
			readinessLiveness.convertedQueueDepth,
			readinessLiveness.queueCapacity);
	}
	if (readinessDecision.requestSerializedPostReadyReset &&
		m_activeRendererIsDirectShow && m_videoRenderer &&
		m_rendererResetCoordinator)
	{
		// Publish before resetting. The DirectShow pin atomically adopts the
		// reserve, then the proven graph stop/reset/run re-primes madVR while
		// the fresh VP epoch rebuilds to that exact floor. VP does not attempt to
		// emulate madVR's independently configurable queues with a guessed burst.
		m_videoRenderer->SetOutputReadinessDeliveryReserve(
			requestedVpReserveFrames);
		const bool accepted = m_rendererResetCoordinator->RequestUi(
			RendererResetReason::OutputReadiness,
			RendererResetScope::Graph);
		if (accepted)
			m_outputReadinessGraphReprimeActive = true;
		else
			m_outputReadinessObserver.RearmResetRequest();
		DebugLog::Log(
			"Output readiness graph re-prime request: generation=%llu "
			"reserve=%zu accepted=%d VPdepth=%zu/%zu "
			"madvr_queue=unobservable",
			static_cast<unsigned long long>(
				readinessInput.transitionGeneration),
			requestedVpReserveFrames, accepted ? 1 : 0,
			hasReadinessLiveness ? readinessLiveness.convertedQueueDepth : 0,
			hasReadinessLiveness ? readinessLiveness.queueCapacity : 0);
	}
	const bool readinessChanged = !m_outputReadinessObservationValid ||
		m_lastObservedOutputReadinessState != readinessDecision.state ||
		m_lastObservedOutputReadinessReason != readinessDecision.reason ||
		m_lastObservedReadinessResetRequest !=
			readinessDecision.requestSerializedPostReadyReset;
	if (readinessChanged)
	{
		DebugLog::Log(
			"Output readiness state: generation=%llu graph=%d "
			"expected=%.6fHz observed=%.6fHz phase=%s/%s startup=%s/%s evidence=%.1fs validated=%d readiness=%s/%s evidence=%.1fs validated=%d state=%s "
			"reason=%s settle=%u/%ums validated_tick=%llu "
			"would_request_reset=%d adopt_prime=%d "
			"prime_epoch=%llu post_proof_success=%u raw=%zu converted=%zu/%zu "
			"retained_source=%zu high_water=%zu oldest_source_ms=%llu "
			"discard=%d admit=%d deliver=%d",
			static_cast<unsigned long long>(
				readinessInput.transitionGeneration),
			readinessInput.graphOperational ? 1 : 0,
			readinessInput.expectedOutputRefreshHz,
			readinessInput.observedOutputRefreshHz,
			ToString(displayRateResult.decision),
			ToString(displayRateResult.reason),
			ToString(startupRateResult.decision),
			ToString(startupRateResult.reason),
			startupRateInput.startupObservationSeconds,
			startupRateResult.startupValidated ? 1 : 0,
			ToString(readinessRateResult.decision),
			ToString(readinessRateResult.reason),
			readinessRateInput.readinessObservationSeconds,
			readinessRateResult.readinessValidated ? 1 : 0,
			ToString(readinessDecision.state),
			ToString(readinessDecision.reason),
			readinessDecision.postReadySettleElapsedMs,
			readinessDecision.postReadySettleRequiredMs,
			static_cast<unsigned long long>(
				readinessDecision.readinessValidatedTickMs),
			readinessDecision.requestSerializedPostReadyReset ? 1 : 0,
			readinessDecision.adoptedCurrentGraph ? 1 : 0,
			static_cast<unsigned long long>(
				readinessInput.currentGraphPrimeEpoch),
			readinessInput.currentGraphPostProofDeliverySuccesses,
			readinessInput.currentGraphRawDepth,
			readinessInput.currentGraphConvertedDepth,
			queueCapacity,
			hasReadinessLiveness ?
				readinessLiveness.retainedSourceBufferCount : 0,
			hasReadinessLiveness ?
				readinessLiveness.retainedSourceBufferHighWater : 0,
			static_cast<unsigned long long>(hasReadinessLiveness ?
				readinessLiveness.oldestRetainedSourceBufferAgeMs : 0),
			readinessDecision.discardLiveCapture ? 1 : 0,
			readinessDecision.admitCurrentEpochCapture ? 1 : 0,
			readinessDecision.allowDownstreamDelivery ? 1 : 0);
		m_outputReadinessObservationValid = true;
		m_lastObservedOutputReadinessState = readinessDecision.state;
		m_lastObservedOutputReadinessReason = readinessDecision.reason;
		m_lastObservedReadinessResetRequest =
			readinessDecision.requestSerializedPostReadyReset;
	}
	const bool sceneTimingReady =
		displayRateResult.decision == DisplayRefreshRateDecision::Accepted;
	const double sceneTimingElapsedSeconds =
		sampledDisplayTiming.qpcFrequency > 0 &&
		sampledDisplayTiming.measurementStartedQpc > 0 &&
		qpcNow.QuadPart >= sampledDisplayTiming.measurementStartedQpc ?
		static_cast<double>(qpcNow.QuadPart - sampledDisplayTiming.measurementStartedQpc) /
			static_cast<double>(sampledDisplayTiming.qpcFrequency) : 0.0;
	if (displayTiming.lastVBlankQpc <= 0 &&
		sampledDisplayTiming.lastVBlankQpc > 0)
	{
		displayTiming.lastVBlankQpc = sampledDisplayTiming.lastVBlankQpc;
		displayTiming.refreshPeriodQpc = sampledDisplayTiming.refreshPeriodQpc;
		displayTiming.qpcFrequency = sampledDisplayTiming.qpcFrequency;
	}

	double theoreticalCaptureRate = 0.0;
	double measuredCaptureRate = 0.0;
	int measuredCapturePpm = 0;
	bool hasMeasuredCaptureRate = false;
	if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		m_videoRenderer && m_captureDeviceVideoState &&
		m_captureDeviceVideoState->valid &&
		m_captureDeviceVideoState->displayMode)
	{
		theoreticalCaptureRate =
			m_captureDeviceVideoState->displayMode->RefreshRateHz();
		hasMeasuredCaptureRate = m_videoRenderer->GetFrameRateAndPPM(
			measuredCaptureRate, measuredCapturePpm);
		if (!hasMeasuredCaptureRate)
			measuredCaptureRate = theoreticalCaptureRate;

		if (m_sceneAwareTimingCorrection)
		{
			// This remains active when the OSD is hidden. Scene correction must not
			// depend on whether diagnostics are visible.
			// Scene correction is valid only when display and delivery run at
			// essentially the same rate. Large differences are frame-rate
			// conversion, not a one-frame drift that can be hidden at a scene cut.
			m_videoRenderer->SetSceneTimingRates(
				displayRefreshRate, measuredCaptureRate);
			m_videoRenderer->SetSceneTimingReadiness(
				sceneTimingReady, sampledDisplayTiming.intervalsObserved);
			if (sceneTimingReady)
				m_videoRenderer->SetSceneTimingPhase(
					displayTiming.lastVBlankQpc,
					displayTiming.refreshPeriodQpc,
					displayTiming.qpcFrequency);
			else
				m_videoRenderer->SetSceneTimingPhase(0, 0, 0);
		}
		else
		{
			// Clear stale predictions and avoid Scene Detect timing work while the
			// feature is off. Display-rate diagnostics continue independently.
			m_videoRenderer->SetSceneTimingRates(0.0, 0.0);
			m_videoRenderer->SetSceneTimingReadiness(false, 0);
			m_videoRenderer->SetSceneTimingPhase(0, 0, 0);
		}
	}

	const bool nativeOverlay = m_statsOverlayRequestedVisible && m_videoRenderer &&
		m_videoRenderer->SupportsNativeStatsOverlay();
	// A madVR OSD API failure is diagnostics-only.  On the following periodic
	// refresh, make the existing window overlay visible rather than leaving the
	// requested panel absent or attempting repeated failing submissions.
	if (m_statsOverlayRequestedVisible && !nativeOverlay && m_statsOverlay &&
		!m_statsOverlay->IsVisible())
	{
		if (!m_statsOverlay->IsCreated())
			m_statsOverlay->Create(GetSafeHwnd());
		m_statsOverlay->Show(m_statsOverlay->IsCreated());
	}
	if (!m_statsOverlay ||
		(!m_statsOverlay->IsVisible() && !nativeOverlay) || !m_lastStatsData)
		return;

	// Fullscreen/windowed changes can put a no-activate layered overlay behind
	// a renderer window.  Reassert topmost only every five seconds while it is
	// visible; this is UI-only and does not touch the DirectShow graph.
	if (!nativeOverlay && m_timerSeconds % 5 == 0)
		m_statsOverlay->UpdatePosition(displayWindow ? displayWindow : GetSafeHwnd());

	StatsData stats;

	// Video format info
	if (m_captureDeviceVideoState && m_captureDeviceVideoState->valid)
	{
		// Resolution
		stats.resolution.Format(_T("%u x %u"),
			m_captureDeviceVideoState->displayMode->FrameWidth(),
			m_captureDeviceVideoState->displayMode->FrameHeight());

		// Refresh rate
		stats.refreshRate = m_captureDeviceVideoState->displayMode->RefreshRateHz();
		// Keep the accepted/renderer-selected value authoritative for timing.
		// During DXGI warm-up, the provisional candidate is still useful OSD
		// telemetry when labelled as such; it is never fed to timing consumers.
		stats.displayRefreshRate = displayRefreshRate > 0.0 ?
			displayRefreshRate : sampledDisplayTiming.refreshRateHz;
		stats.displayRefreshRateOverridden = displayRefreshRateOverridden;
		if (!displayRefreshRateOverridden &&
			displayRateResult.decision !=
				DisplayRefreshRateDecision::Accepted)
		{
			stats.displayRefreshRateStatus.Format(TEXT("%S"),
				ToString(displayRateResult.decision));
		}
		stats.sceneTimingIntervals = sampledDisplayTiming.intervalsObserved;
		stats.sceneTimingElapsedSeconds = sceneTimingElapsedSeconds;
		stats.sceneTimingReady = sceneTimingReady;

		// EOTF
		stats.eotf = ToString(m_captureDeviceVideoState->eotf);

		// Colorspace
		stats.colorspace = ToString(m_captureDeviceVideoState->colorspace);

		// Pixel Format
		stats.pixelFormat = ToString(m_captureDeviceVideoState->videoFrameEncoding);
	}

	// Renderer settings and capture metrics
	if (m_captureDevice)
	{
		stats.frameOffsetMs = GetTimingClockFrameOffsetMs();
		stats.hwLatencyMs = m_captureDevice->HardwareLatencyMs();
	}

	// Method - get from renderer DirectShow start/stop time method combo
	int methodIndex = m_rendererDirectShowStartStopTimeMethodCombo.GetCurSel();
	if (methodIndex >= 0)
	{
		DirectShowStartStopTimeMethod method = (DirectShowStartStopTimeMethod)m_rendererDirectShowStartStopTimeMethodCombo.GetItemData(methodIndex);
		stats.method = ToString(method);
	}
	else
	{
		stats.method = TEXT("---");
	}

	if (const auto profileSnapshot = m_profileRuntime.GetSnapshot())
	{
		stats.viewport.Format(TEXT("%S (%S)"),
			profileSnapshot->viewport.profile.c_str(),
			profileSnapshot->viewport.hasScreenAspect ?
				profileSnapshot->viewport.screenAspect.Canonical().c_str() :
				"renderer native");
	}
	else
	{
		stats.viewport = TEXT("default (renderer native)");
	}

	if (m_rendererSceneCorrectionModeCombo.GetCurSel() >= 0)
		m_rendererSceneCorrectionModeCombo.GetLBText(
			m_rendererSceneCorrectionModeCombo.GetCurSel(),
			stats.sceneDetectMode);
	else
		stats.sceneDetectMode = TEXT("Off");
	// Keep the configured user choice (Off/Basic/Advanced) as the mode label.
	// Renderer-native detector lifecycle is readiness, not a replacement mode.

	// Queue stats
	if (m_rendererState == RendererState::RENDERSTATE_RENDERING && m_videoRenderer)
	{
		const int selectedRenderer = m_rendererCombo.GetCurSel();
		const RendererId* renderer = selectedRenderer >= 0 ?
			reinterpret_cast<const RendererId*>(m_rendererCombo.GetItemData(selectedRenderer)) : nullptr;
		stats.isAlphaRenderer = renderer &&
			renderer->backend == RendererBackend::LIBPLACEBO;
		if (renderer)
			stats.rendererName = renderer->name;
		else
			stats.rendererName = stats.isAlphaRenderer ?
				TEXT("VP Renderer") : TEXT("DirectShow");
		stats.rawQueueSize = m_videoRenderer->GetFrameQueueSize();
		stats.convertedQueueSize = m_videoRenderer->GetConvertedQueueSize();
		stats.currentQueueSize = stats.rawQueueSize + stats.convertedQueueSize;
		stats.maxQueueSize = GetRendererVideoFrameQueueSizeMax();
		stats.isQueueFull = (stats.currentQueueSize >= stats.maxQueueSize);

		RendererLatencySnapshot latencySnapshot;
		if (m_videoRenderer->GetLatencySnapshot(latencySnapshot))
		{
			stats.vpInternalLatencyKnown = true;
			stats.scheduledLatencyKnown =
				latencySnapshot.scheduledPresentationKnown;
			stats.vpInternalLatencyMs = latencySnapshot.vpInternalMs;
			stats.dsScheduleLeadMs = latencySnapshot.dsScheduleLeadMs;
			stats.scheduledLatencyMs = latencySnapshot.scheduledLatencyMs;
		}
		else if (stats.isAlphaRenderer)
		{
			stats.vpInternalLatencyKnown = true;
			stats.vpInternalLatencyMs = std::max(0.0,
				m_videoRenderer->ExitLatencyMs() -
				m_videoRenderer->EntryLatencyMs());
		}
		if (stats.isAlphaRenderer)
		{
			stats.presentationTargetTimingKnown =
				m_videoRenderer->GetPresentationTargetTiming(
					stats.presentationTargetLeadMs,
					stats.captureToPresentationTargetMs);
		}
		stats.queueDroppedFrames = m_videoRenderer->DroppedFrameCount();
		m_videoRenderer->GetOutputModeInfo(stats.outputMode);
		m_videoRenderer->GetDisplayLutInfo(stats.displayLut);
		stats.sceneDetectCorrectionDrops = m_videoRenderer->SceneAwareCorrectionDropCount();
		stats.sceneDetectCorrectionRepeats = m_videoRenderer->SceneAwareCorrectionRepeatCount();
		stats.sceneDetectDetected = m_videoRenderer->SceneAwareDetectedCount();
		stats.sceneTimingRatesCompatible =
			m_videoRenderer->SceneTimingRatesCompatible();
		m_videoRenderer->GetSceneTimingStatus(stats.sceneTimingStatus);
		stats.sceneCorrectionPredictionValid =
			m_videoRenderer->GetSceneTimingPrediction(
				stats.sceneSecondsUntilCorrection,
				stats.sceneSecondsUntilPlan,
				stats.sceneCorrectionAction,
				stats.sceneCorrectionPlanned);
		int dueAction = 0;
		stats.sceneCorrectionDue =
			m_videoRenderer->GetSceneTimingDueStatus(
				dueAction, stats.sceneCorrectionBlockReason);
		if (stats.sceneCorrectionDue && dueAction != 0)
			stats.sceneCorrectionAction = dueAction;
		stats.sceneLastCorrectionValid =
			m_videoRenderer->GetSceneTimingLastCorrection(
				stats.sceneLastCorrectionAction,
				stats.sceneLastCorrectionSecondsFromDeadline,
				stats.sceneLastCorrectionTick);
		stats.activeShaderRule = m_videoRenderer->ActiveShaderRule();
		stats.activeShaders = m_videoRenderer->ActiveShaders();
	}

	// Capture device frame counts
	if (m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING && m_captureDevice)
	{
		const uint64_t totalCapturedFrames =
			m_captureDevice->VideoFrameCapturedCount();
		stats.capturedFrames = totalCapturedFrames;
		stats.rendererCapturedFrames =
			m_rendererFrameBaselineValid &&
			totalCapturedFrames >= m_rendererStartCapturedFrameCount
			? totalCapturedFrames - m_rendererStartCapturedFrameCount
			: 0;
		stats.capturedDroppedFrames = m_captureDevice->VideoFrameMissedCount();
	}

	// Video conversion
	if (m_rendererVideoConversionCombo.GetCurSel() >= 0)
	{
		m_rendererVideoConversionCombo.GetLBText(m_rendererVideoConversionCombo.GetCurSel(), stats.videoConversion);
	}
	// Alpha can choose an actual native or P010 ingress path per frame. Show
	// that resolved path rather than the generic DirectShow override label.
	if (stats.isAlphaRenderer && m_videoRenderer)
	{
		CString ingress;
		if (m_videoRenderer->GetVideoIngressInfo(ingress))
			stats.videoConversion = ingress;
	}
	
	// Conversion performance (NEW - V210→P010 etc.)
	if (m_rendererState == RendererState::RENDERSTATE_RENDERING && m_videoRenderer)
	{
		double currentUs, avg10s, max10s;
		stats.hasConversionData = m_videoRenderer->GetConversionPerformance(currentUs, avg10s, max10s);
		if (stats.hasConversionData)
		{
			stats.currentConversionTimeUs = currentUs;
			stats.avgConversionTime10s = avg10s;
			stats.maxConversionTime10s = max10s;
		}
	}

	// Handle reset tracking
	if (stats.queueDroppedFrames < m_lastStatsData->queueDroppedFrames)
	{
		// Reset detected (dropped frame count decreased)
		stats.OnReset();
		stats.capturedFramesAtReset = stats.capturedFrames;
		*m_lastStatsData = stats;
	}
	else
	{
		// Update from last known state
		stats.lastResetTickCount = m_lastStatsData->lastResetTickCount;
		stats.capturedFramesAtReset = m_lastStatsData->capturedFramesAtReset;
		stats.framesSinceReset = stats.capturedFrames - stats.capturedFramesAtReset;
		stats.maxQueueSizeSinceReset = m_lastStatsData->maxQueueSizeSinceReset;
	}

	stats.UpdateTimeSinceReset();
	stats.UpdateMaxQueueSize();

	// PPM Correction info (NEW)
	if (m_rendererState == RendererState::RENDERSTATE_RENDERING && m_videoRenderer)
	{
		int ppmValue;
		bool hasCorrection;
		CString source;
		if (m_videoRenderer->GetPPMCorrectionInfo(ppmValue, hasCorrection, source))
		{
			stats.ppmCorrection = ppmValue;
			stats.hasPPMCorrection = hasCorrection;
			stats.ppmSource = source;
		}
	}

	// Frame rate and PPM measurement (NEW)
	if (m_videoRenderer && m_captureDeviceVideoState && m_captureDeviceVideoState->valid)
	{
		stats.theoreticalRefreshRate = theoreticalCaptureRate;
		stats.measuredRefreshRate = measuredCaptureRate > 0.0 ?
			measuredCaptureRate : theoreticalCaptureRate;
		stats.ppmDeviation = hasMeasuredCaptureRate ? measuredCapturePpm : 0;
	}

	LogDroppedCounterChanges(stats);

	// Update overlay
	m_statsOverlay->UpdateStats(stats);
	if (nativeOverlay)
	{
		std::vector<uint8_t> pixels;
		int width = 0;
		int height = 0;
		int stride = 0;
		if (m_statsOverlay->RenderBgra(pixels, width, height, stride))
			m_videoRenderer->SetNativeStatsOverlay(
				pixels.data(), pixels.size(), width, height, stride);
	}

	// Save current stats for next update
	*m_lastStatsData = stats;
}


void CVideoProcessorDlg::LogDroppedCounterChanges(const StatsData& stats)
{
	if (m_rendererState != RendererState::RENDERSTATE_RENDERING ||
		!m_videoRenderer)
	{
		m_dropDiagnosticRenderer = nullptr;
		m_dropDiagnosticInitialized = false;
		return;
	}

	const bool rendererChanged =
		!m_dropDiagnosticInitialized ||
		m_dropDiagnosticRenderer != m_videoRenderer.get();
	if (rendererChanged)
	{
		DebugLog::Log(
			"OSD dropped counters: event=baseline renderer=%s capture_missed=%llu renderer_dropped=%llu queue=%zu/%zu cadence_drops=%llu cadence_repeats=%llu",
			stats.isAlphaRenderer ? "Alpha" : "DirectShow",
			static_cast<unsigned long long>(stats.capturedDroppedFrames),
			static_cast<unsigned long long>(stats.queueDroppedFrames),
			stats.rawQueueSize + stats.convertedQueueSize,
			stats.maxQueueSize,
			static_cast<unsigned long long>(
				stats.sceneDetectCorrectionDrops),
			static_cast<unsigned long long>(
				stats.sceneDetectCorrectionRepeats));
		m_dropDiagnosticRenderer = m_videoRenderer.get();
		m_dropDiagnosticInitialized = true;
		m_lastLoggedCaptureMissed = stats.capturedDroppedFrames;
		m_lastLoggedRendererDropped = stats.queueDroppedFrames;
		return;
	}

	if (stats.capturedDroppedFrames == m_lastLoggedCaptureMissed &&
		stats.queueDroppedFrames == m_lastLoggedRendererDropped)
	{
		return;
	}

	const int64_t captureDelta =
		static_cast<int64_t>(stats.capturedDroppedFrames) -
		static_cast<int64_t>(m_lastLoggedCaptureMissed);
	const int64_t rendererDelta =
		static_cast<int64_t>(stats.queueDroppedFrames) -
		static_cast<int64_t>(m_lastLoggedRendererDropped);
	DebugLog::Log(
		"OSD dropped counters: event=changed renderer=%s capture_missed=%llu capture_delta=%+lld renderer_dropped=%llu renderer_delta=%+lld queue=%zu/%zu cadence_drops=%llu cadence_repeats=%llu",
		stats.isAlphaRenderer ? "Alpha" : "DirectShow",
		static_cast<unsigned long long>(stats.capturedDroppedFrames),
		static_cast<long long>(captureDelta),
		static_cast<unsigned long long>(stats.queueDroppedFrames),
		static_cast<long long>(rendererDelta),
		stats.rawQueueSize + stats.convertedQueueSize,
		stats.maxQueueSize,
		static_cast<unsigned long long>(stats.sceneDetectCorrectionDrops),
		static_cast<unsigned long long>(stats.sceneDetectCorrectionRepeats));
	m_lastLoggedCaptureMissed = stats.capturedDroppedFrames;
	m_lastLoggedRendererDropped = stats.queueDroppedFrames;
}


void CVideoProcessorDlg::RequestRendererReset(RendererResetReason reason,
	bool requiresGraph, UINT delayMs)
{
	if (!m_videoRenderer ||
		m_rendererState != RendererState::RENDERSTATE_RENDERING ||
		!m_rendererResetCoordinator)
	{
		DEBUGLOG("Reset request ignored: reason=%s renderer is not rendering",
			CStringA(ToString(reason)).GetString());
		return;
	}

	const bool accepted = m_rendererResetCoordinator->RequestUi(
		reason,
		requiresGraph ?
			RendererResetScope::Graph :
			RendererResetScope::LiveQueue,
		delayMs);
	DEBUGLOG(
		"Reset request %s: renderer=%s backend=%s generation=%u "
		"reason=%s priority=%d scope=%s delay=%ums",
		accepted ? "accepted" : "rejected",
		CStringA(m_activeRendererName).GetString(),
		m_activeRendererIsDirectShow ? "DirectShow" : "Alpha",
		m_rendererGeneration.load(std::memory_order_acquire),
		CStringA(ToString(reason)).GetString(),
		RendererResetPriority(reason),
		requiresGraph ? "graph" : "live-queue",
		delayMs);
}


bool CVideoProcessorDlg::RendererResetOperationInProgress() const
{
	if (!m_rendererResetCoordinator)
		return false;
	return m_rendererResetCoordinator->RequiresLifecycleDeferral();
}


void CVideoProcessorDlg::CompleteRendererResetOperation()
{
	PumpRendererResetMailbox();
}


void CVideoProcessorDlg::MonitorQueueHealth(size_t rawQueueSize,
	size_t convertedQueueSize, size_t queueMaxSize, uint64_t droppedFrames)
{
	m_lastQueueSize = rawQueueSize + convertedQueueSize;
	m_lastDroppedFrames = droppedFrames;

	if (queueMaxSize == 0 || !GetRendererVideoFrameUseQueue() || !m_videoRenderer ||
		m_rendererState != RendererState::RENDERSTATE_RENDERING)
	{
		m_consecutiveFullSeconds = 0;
		m_consecutiveStuckSeconds = 0;
		return;
	}

	// Raw and converted queues each have their own capacity. Do not use the
	// combined UI total here: 12 raw + 12 converted is not a 24/32 overflow.
	const size_t highWaterPercent =
		static_cast<size_t>(m_queueResetHighWaterPercent);
	const bool highWater = rawQueueSize * 100 >= queueMaxSize * highWaterPercent ||
		convertedQueueSize * 100 >= queueMaxSize * highWaterPercent;
	const ULONGLONG now = GetTickCount64();
	const bool atCapacity =
		rawQueueSize >= queueMaxSize ||
		convertedQueueSize >= queueMaxSize;
	RendererLivenessSnapshot liveness;
	const bool hasLiveness =
		m_videoRenderer->GetLivenessSnapshot(liveness);
	const auto ageMs = [now](uint64_t tick) -> ULONGLONG
	{
		return tick == 0 || tick > now ?
			(std::numeric_limits<ULONGLONG>::max)() : now - tick;
	};
	const bool autoReset =
		m_rendererResetAutoCheck.GetCheck() == BST_CHECKED;
	const size_t sustainedSeconds =
		static_cast<size_t>(std::max(3, m_queueResetDelaySeconds));
	const ULONGLONG stallThresholdMs =
		static_cast<ULONGLONG>(sustainedSeconds) * 1000;

	if (m_directShowGraphRecoveryAwaitingHealth &&
		(m_directShowGraphRecoveryGeneration !=
			m_rendererGeneration.load(std::memory_order_acquire) ||
		 (hasLiveness &&
			liveness.queueEpoch != m_directShowGraphRecoveryEpoch)))
	{
		DebugLog::Log(
			"DirectShow graph recovery health proof discarded: "
			"expected_generation=%u current_generation=%u "
			"expected_epoch=%llu current_epoch=%llu liveness=%d",
			m_directShowGraphRecoveryGeneration,
			m_rendererGeneration.load(std::memory_order_acquire),
			static_cast<unsigned long long>(
				m_directShowGraphRecoveryEpoch),
			static_cast<unsigned long long>(liveness.queueEpoch),
			hasLiveness ? 1 : 0);
		m_directShowGraphRecoveryAwaitingHealth = false;
		m_directShowGraphRecoveryWasRetarget = false;
	}

	const bool recoveryDeliveryHealthy =
		hasLiveness && HasRecentCurrentEpochDelivery(liveness, now, 500) &&
		(!liveness.deliveryInProgress ||
		 ageMs(liveness.lastDeliveryStartTick) < 500);
	if (m_directShowGraphRecoveryAwaitingHealth &&
		recoveryDeliveryHealthy &&
		now - m_directShowGraphRecoveryStartedTick >= 2000)
	{
		DebugLog::Log(
			"DirectShow graph recovery proved healthy: generation=%u "
			"epoch=%llu elapsed_ms=%llu deliveries=%llu",
			m_directShowGraphRecoveryGeneration,
			static_cast<unsigned long long>(
				m_directShowGraphRecoveryEpoch),
			static_cast<unsigned long long>(
				now - m_directShowGraphRecoveryStartedTick),
			static_cast<unsigned long long>(
				liveness.currentEpochDeliverySuccessCount));
		m_directShowGraphRecoveryAwaitingHealth = false;
		m_directShowGraphRecoveryWasRetarget = false;
	}

	// Retarget health cannot depend on fresh capture ingress. Once converted
	// queues fill behind an opaque madVR Receive call, backpressure correctly
	// stops capture admission and the generic steady-state predicate no longer
	// has "input advancing" evidence. A retarget must instead prove current-
	// epoch delivery within a bounded transition window.
	const bool retargetReceiveStall =
		m_directShowGraphRecoveryAwaitingHealth &&
		m_directShowGraphRecoveryWasRetarget &&
		hasLiveness && IsPostRetargetReceiveStall(
			liveness, now, m_directShowGraphRecoveryStartedTick);
	if (retargetReceiveStall && !m_directShowRecoveryRebuildRequested)
	{
		uint64_t captureSequence =
			m_appliedCaptureVideoStateNotificationSequence;
		if (captureSequence == 0 && m_rendererIngressState)
			captureSequence = m_rendererIngressState->LatestCaptureSequence();
		m_directShowRecoveryRebuildRequested = true;
		m_directShowGraphRecoveryAwaitingHealth = false;
		m_directShowGraphRecoveryWasRetarget = false;
		m_directShowRecoveryRecreationAttempted = true;
		m_directShowRecoveryRecreationCaptureSequence = captureSequence;
		m_nextRendererIsRecoveryRecreation = true;
		DebugLog::Log(
			"Fullscreen retarget health timeout: generation=%u epoch=%llu "
			"capture_sequence=%llu blocked_ms=%llu deliveries=%llu "
			"raw=%zu/%zu converted=%zu/%zu "
			"old=%p new=%p direction=%s action=full-renderer-recreation",
			m_rendererGeneration.load(std::memory_order_acquire),
			static_cast<unsigned long long>(liveness.queueEpoch),
			static_cast<unsigned long long>(captureSequence),
			static_cast<unsigned long long>(
				ageMs(liveness.lastDeliveryStartTick)),
			static_cast<unsigned long long>(
				liveness.currentEpochDeliverySuccessCount),
			rawQueueSize, queueMaxSize,
			convertedQueueSize, queueMaxSize,
			m_fullscreenRetargetPreviousTargetHwnd,
			m_fullscreenRetargetTargetHwnd,
			m_fullscreenRetargetExiting ? "exit" : "enter");
		m_postRendererStartRequiresGraph = false;
		m_wantToRestartRenderer = true;
		UpdateState();
		return;
	}

	if (!highWater)
	{
		m_consecutiveFullSeconds = 0;
		m_consecutiveStuckSeconds = 0;
		m_queuePressureRecoveryRequested = false;
		m_queueCapacityRecoveryRequested = false;
		return;
	}

	if (m_consecutiveFullSeconds <
		(std::numeric_limits<size_t>::max)())
		++m_consecutiveFullSeconds;
	// Alpha uses the UI value as its hard queue cap. Treat any reported excess
	// as a recovery condition in case a future queue-path regression violates
	// that invariant. DirectShow keeps its liveness-based recovery below because
	// its downstream queues are not directly observable.
	const bool alphaQueueExceeded =
		!m_activeRendererIsDirectShow &&
		(rawQueueSize > queueMaxSize || convertedQueueSize > queueMaxSize);
	const bool alphaQueueSustainedAtCapacity =
		!m_activeRendererIsDirectShow && atCapacity &&
		m_consecutiveFullSeconds >= sustainedSeconds;
	if (autoReset && (alphaQueueExceeded || alphaQueueSustainedAtCapacity))
	{
		DEBUGLOG(
			"Alpha queue recovery: raw=%zu/%zu converted=%zu/%zu consecutive=%zu threshold=%zu reason=%s; requesting live-queue reset",
			rawQueueSize, queueMaxSize,
			convertedQueueSize, queueMaxSize,
			m_consecutiveFullSeconds, sustainedSeconds,
			alphaQueueExceeded ? "exceeded-capacity" : "sustained-at-capacity");
		RequestRendererReset(RendererResetReason::QueuePressure, false, 0);
		return;
	}

	// A full VP queue is normal backpressure when madVR's independently
	// configurable CPU/GPU queues are full. It is not, by itself, a failure.
	// Escalate only after input is still arriving and downstream delivery has
	// made no progress for the configured sustained-stall interval.
	const bool postResetDeliveryDeadlock =
		m_directShowGraphRecoveryAwaitingHealth &&
		hasLiveness && IsSustainedDirectShowDeliveryStall(
			liveness, now, atCapacity, stallThresholdMs);
	if (autoReset && m_activeRendererIsDirectShow &&
		postResetDeliveryDeadlock)
	{
		if (!m_directShowRecoveryRebuildRequested)
		{
			uint64_t captureSequence =
				m_appliedCaptureVideoStateNotificationSequence;
			if (captureSequence == 0 && m_rendererIngressState)
				captureSequence = m_rendererIngressState->LatestCaptureSequence();
			if (m_directShowRecoveryRecreationAttempted &&
				captureSequence ==
					m_directShowRecoveryRecreationCaptureSequence)
			{
				m_directShowRecoveryRebuildRequested = true;
				DebugLog::Log(
					"DirectShow recovery recreation suppressed: generation=%u "
					"epoch=%llu capture_sequence=%llu "
					"reason=already-attempted-for-capture-state",
					m_directShowGraphRecoveryGeneration,
					static_cast<unsigned long long>(
						m_directShowGraphRecoveryEpoch),
					static_cast<unsigned long long>(captureSequence));
				return;
			}
			m_directShowRecoveryRebuildRequested = true;
			m_directShowRecoveryRecreationAttempted = true;
			m_directShowRecoveryRecreationCaptureSequence = captureSequence;
			m_nextRendererIsRecoveryRecreation = true;
			DebugLog::Log(
				"DirectShow in-place recovery failed; requesting one full "
				"renderer recreation: generation=%u epoch=%llu "
				"capture_sequence=%llu raw=%zu/%zu converted=%zu/%zu deliveries=%llu "
				"blocked_ms=%llu",
				m_directShowGraphRecoveryGeneration,
				static_cast<unsigned long long>(
					m_directShowGraphRecoveryEpoch),
				static_cast<unsigned long long>(captureSequence),
				rawQueueSize, queueMaxSize,
				convertedQueueSize, queueMaxSize,
				static_cast<unsigned long long>(
					liveness.currentEpochDeliverySuccessCount),
				static_cast<unsigned long long>(
					ageMs(liveness.lastDeliveryStartTick)));
			m_postRendererStartRequiresGraph = false;
			m_wantToRestartRenderer = true;
			UpdateState();
		}
		return;
	}
	const bool provenDirectShowStall =
		m_activeRendererIsDirectShow && hasLiveness &&
		IsSustainedDirectShowDeliveryStall(
			liveness, now, atCapacity, stallThresholdMs);

	if (provenDirectShowStall)
	{
		if (m_consecutiveStuckSeconds <
			(std::numeric_limits<size_t>::max)())
			++m_consecutiveStuckSeconds;
	}
	else
	{
		m_consecutiveStuckSeconds = 0;
	}

	const DWORD tick = GetTickCount();
	if (m_lastQueueHealthDiagnostic == 0 ||
		tick - m_lastQueueHealthDiagnostic >= 5000)
	{
		m_lastQueueHealthDiagnostic = tick;
		if (hasLiveness)
			LogLivenessSnapshot(
				liveness, rawQueueSize, convertedQueueSize, queueMaxSize,
				provenDirectShowStall ? "proven-stall" : "high-water");
		else
			DbgLog((LOG_TRACE, 1,
				TEXT("Queue high-water diagnostic (%zu/%zu raw, %zu/%zu "
					"converted, consecutive=%zu, auto_reset=%d, "
					"liveness=unavailable)"),
				rawQueueSize, queueMaxSize,
				convertedQueueSize, queueMaxSize,
				m_consecutiveFullSeconds, autoReset ? 1 : 0));
	}

	const bool recoveryCooldownComplete =
		m_lastLivenessRecoveryTick == 0 ||
		now - m_lastLivenessRecoveryTick >= 30000;
	if (autoReset &&
		provenDirectShowStall &&
		m_consecutiveStuckSeconds > 0 &&
		recoveryCooldownComplete)
	{
		DEBUGLOG(
			"Critical liveness recovery requested: raw=%zu/%zu "
			"converted=%zu/%zu stuck_seconds=%zu "
			"delivery_in_progress=%d last_delivery_success_age=%llums",
			rawQueueSize, queueMaxSize,
			convertedQueueSize, queueMaxSize,
			m_consecutiveStuckSeconds,
			liveness.deliveryInProgress ? 1 : 0,
			static_cast<unsigned long long>(
				ageMs(liveness.lastDeliverySuccessTick)));
		m_consecutiveStuckSeconds = 0;
		RequestRendererReset(
			RendererResetReason::LivenessRecovery, true, 0);
		return;
	}
}


void CVideoProcessorDlg::LogLivenessSnapshot(
	const RendererLivenessSnapshot& snapshot,
	size_t rawQueueSize,
	size_t convertedQueueSize,
	size_t queueMaxSize,
	const char* trigger)
{
	const ULONGLONG now = GetTickCount64();
	const auto ageMs = [now](uint64_t tick) -> unsigned long long
	{
		return static_cast<unsigned long long>(
			tick == 0 || tick > now ?
				(std::numeric_limits<ULONGLONG>::max)() : now - tick);
	};
	const uint64_t graphRequest =
		m_activeGraphRequestId.load(std::memory_order_acquire);
	const ULONGLONG graphStarted =
		m_activeGraphRequestStartedTick.load(std::memory_order_acquire);
	DebugLog::Log(
		"Liveness snapshot: trigger=%s renderer=%S generation=%u "
		"queue=%zu/%zu+%zu/%zu buffering=%d epoch=%llu "
		"ui_message_age=%llums ui_paint_age=%llums "
		"capture_thread=%u input=%llu input_age=%llums "
		"conversion_thread=%u converted=%llu conversion_age=%llums "
		"delivery_thread=%u dequeued=%llu dequeue_age=%llums "
		"deliver_attempts=%llu deliver_success=%llu "
		"deliver_start_age=%llums deliver_success_age=%llums "
		"delivery_lock_owner=%s reset_lock_owner=%s "
		"graph_request=%llu graph_age=%llums",
		trigger ? trigger : "periodic",
		static_cast<LPCTSTR>(m_activeRendererName),
		m_rendererGeneration.load(std::memory_order_acquire),
		rawQueueSize, queueMaxSize,
		convertedQueueSize, queueMaxSize,
		snapshot.buffering ? 1 : 0,
		static_cast<unsigned long long>(snapshot.queueEpoch),
		ageMs(m_lastUiMessageTick.load(std::memory_order_acquire)),
		ageMs(m_lastUiPaintTick.load(std::memory_order_acquire)),
		snapshot.captureThreadId,
		static_cast<unsigned long long>(snapshot.inputCount),
		ageMs(snapshot.lastInputTick),
		snapshot.conversionThreadId,
		static_cast<unsigned long long>(snapshot.conversionCount),
		ageMs(snapshot.lastConversionTick),
		snapshot.deliveryThreadId,
		static_cast<unsigned long long>(snapshot.dequeueCount),
		ageMs(snapshot.lastDequeueTick),
		static_cast<unsigned long long>(snapshot.deliveryAttemptCount),
		static_cast<unsigned long long>(snapshot.deliverySuccessCount),
		ageMs(snapshot.lastDeliveryStartTick),
		ageMs(snapshot.lastDeliverySuccessTick),
		snapshot.deliveryInProgress ? "delivery-thread" : "none",
		snapshot.resetInProgress ? "reset-thread" : "none",
		static_cast<unsigned long long>(graphRequest),
		graphStarted ? ageMs(graphStarted) : 0);
}


void CVideoProcessorDlg::LivenessWatchdogWorker()
{
	ULONGLONG lastAlertTick = 0;
	while (m_livenessWatchdogStopEvent &&
		WaitForSingleObject(m_livenessWatchdogStopEvent, 1000) ==
			WAIT_TIMEOUT)
	{
		const ULONGLONG now = GetTickCount64();
		const ULONGLONG lastMessage =
			m_lastUiMessageTick.load(std::memory_order_acquire);
		const ULONGLONG messageAge =
			lastMessage == 0 || lastMessage > now ?
				0 : now - lastMessage;
		if (messageAge < 3000 ||
			(lastAlertTick != 0 && now - lastAlertTick < 5000))
		{
			continue;
		}

		if (!m_rendererIngressState->IsAdmitting())
			continue;
		const std::shared_ptr<IVideoRenderer> renderer =
			std::atomic_load_explicit(
				&m_videoRenderer, std::memory_order_acquire);
		if (!renderer)
			continue;

		RendererLivenessSnapshot snapshot;
		if (!renderer->GetLivenessSnapshot(snapshot))
			continue;

		lastAlertTick = now;
		const auto ageMs = [now](uint64_t tick) -> unsigned long long
		{
			return static_cast<unsigned long long>(
				tick == 0 || tick > now ?
					(std::numeric_limits<ULONGLONG>::max)() : now - tick);
		};
		const ULONGLONG graphStarted =
			m_activeGraphRequestStartedTick.load(std::memory_order_acquire);
		DebugLog::Log(
			"UI liveness alert: message_age=%llums paint_age=%llums "
			"generation=%u queue=%zu/%zu+%zu/%zu buffering=%d "
			"input_age=%llums conversion_age=%llums dequeue_age=%llums "
			"deliver_start_age=%llums deliver_success_age=%llums "
			"delivery_lock_owner=%s reset_lock_owner=%s "
			"graph_request=%llu graph_generation=%u graph_age=%llums",
			static_cast<unsigned long long>(messageAge),
			ageMs(m_lastUiPaintTick.load(std::memory_order_acquire)),
			m_rendererGeneration.load(std::memory_order_acquire),
			snapshot.rawQueueDepth, snapshot.queueCapacity,
			snapshot.convertedQueueDepth, snapshot.queueCapacity,
			snapshot.buffering ? 1 : 0,
			ageMs(snapshot.lastInputTick),
			ageMs(snapshot.lastConversionTick),
			ageMs(snapshot.lastDequeueTick),
			ageMs(snapshot.lastDeliveryStartTick),
			ageMs(snapshot.lastDeliverySuccessTick),
			snapshot.deliveryInProgress ? "delivery-thread" : "none",
			snapshot.resetInProgress ? "reset-thread" : "none",
			static_cast<unsigned long long>(
				m_activeGraphRequestId.load(std::memory_order_acquire)),
			m_activeGraphRequestGeneration.load(std::memory_order_acquire),
			graphStarted == 0 ? 0 : ageMs(graphStarted));
	}
}



