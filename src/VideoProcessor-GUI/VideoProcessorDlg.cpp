/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <ApplicationShutdownPolicy.h>
#include <BuildIdentityPolicy.h>
#include <ModernOperatorLayout.h>
#include <ModernOperatorStatusPolicy.h>

#include <atlstr.h>
#include <algorithm>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
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
#include <ConfigurationLiveApply.h>
#include <ActiveProfileStatus.h>
#include <ActiveOutputSweepPolicy.h>
#include <EventActionLauncher.h>
#include <DisplayRefreshRateEstimator.h>
#include <DisplayRefreshRatePolicy.h>
#include <RendererGenerationGate.h>
#include <RendererProfileConfig.h>
#include <MainConfigSchema.h>
#include <UnifiedProfileRuntime.h>


#include "VideoProcessorDlg.h"

namespace
{
using Microsoft::WRL::ComPtr;

constexpr wchar_t ConfigurationEditorRelativePath[] =
	L"config\\VideoProcessorConfig.exe";

bool GetApplicationDirectory(std::wstring& directory)
{
	wchar_t modulePath[32768] = {};
	const DWORD length = ::GetModuleFileNameW(nullptr, modulePath,
		ARRAYSIZE(modulePath));
	if (!length || length >= ARRAYSIZE(modulePath))
		return false;
	directory.assign(modulePath, length);
	const size_t separator = directory.find_last_of(L"\\/");
	if (separator == std::wstring::npos)
		return false;
	directory.resize(separator + 1);
	return true;
}

std::wstring ConfigurationEditorPath(const std::wstring& applicationDirectory)
{
	return applicationDirectory + ConfigurationEditorRelativePath;
}

bool ReadShortcutsForegroundOnly(const ConfigFile& config, bool& enabled,
	std::string& error)
{
	enabled = false;
	std::string raw;
	if (!config.TryGetString("shortcuts", "foreground_only", raw))
		return true;
	if (config.TryGetBool("shortcuts", "foreground_only", enabled))
		return true;
	error = "unsupported value for shortcuts.foreground_only: " + raw;
	return false;
}

std::wstring ConfigurationEditorDirectory(
	const std::wstring& applicationDirectory)
{
	return applicationDirectory + L"config\\";
}

struct CaptureVideoStateNotification
{
	ACaptureDeviceComPtr source;
	VideoStateComPtr state;
	uint64_t captureEpoch = 0;
	uint64_t sequence = 0;
	uint64_t ingressPublicationUs = 0;
	bool retainedRendererIngress = false;
};

using ConfigurationSnapshot =
	std::map<std::string, std::map<std::string, std::string>>;

ConfigurationSnapshot CaptureConfigurationSnapshot(const ConfigFile& config)
{
	ConfigurationSnapshot snapshot;
	for (const std::string& section : config.GetSectionNames())
		if (const auto* values = config.GetSectionValues(section))
			snapshot[section] = *values;
	return snapshot;
}

std::vector<std::string> ChangedConfigurationSections(
	const ConfigurationSnapshot& previous,
	const ConfigurationSnapshot& current)
{
	std::set<std::string> names;
	for (const auto& section : previous) names.insert(section.first);
	for (const auto& section : current) names.insert(section.first);
	std::vector<std::string> changed;
	for (const std::string& name : names)
	{
		const auto before = previous.find(name);
		const auto after = current.find(name);
		if (before == previous.end() || after == current.end() ||
			before->second != after->second)
			changed.push_back(name);
	}
	return changed;
}

std::vector<ConfigurationApplyPolicy::Change> ChangedConfigurationValues(
	const ConfigurationSnapshot& previous,
	const ConfigurationSnapshot& current)
{
	std::set<std::string> sections;
	for (const auto& section : previous) sections.insert(section.first);
	for (const auto& section : current) sections.insert(section.first);
	std::vector<ConfigurationApplyPolicy::Change> changed;
	for (const std::string& section : sections)
	{
		std::set<std::string> keys;
		const auto beforeSection = previous.find(section);
		const auto afterSection = current.find(section);
		if (beforeSection != previous.end())
			for (const auto& value : beforeSection->second) keys.insert(value.first);
		if (afterSection != current.end())
			for (const auto& value : afterSection->second) keys.insert(value.first);
		for (const std::string& key : keys)
		{
			const auto before = beforeSection == previous.end() ?
				std::map<std::string, std::string>::const_iterator() :
				beforeSection->second.find(key);
			const auto after = afterSection == current.end() ?
				std::map<std::string, std::string>::const_iterator() :
				afterSection->second.find(key);
			const bool beforePresent = beforeSection != previous.end() &&
				before != beforeSection->second.end();
			const bool afterPresent = afterSection != current.end() &&
				after != afterSection->second.end();
			if (beforePresent != afterPresent ||
				(beforePresent && before->second != after->second))
				changed.push_back({ section, key });
		}
	}
	return changed;
}

bool IsStartupOnlyConfigurationSection(const std::string& section)
{
	return section == "command_line" || section == "general" ||
		section == "renderer_alias" || section == "directshow" ||
		section == "directshow.conversion" ||
		section == "directshow.ppm" || section == "decklink" ||
		section == "logging" || section == "p010_conversion" ||
		section == "ppm_correction";
}

std::string JoinConfigurationSections(const std::vector<std::string>& sections)
{
	std::ostringstream text;
	for (size_t index = 0; index < sections.size(); ++index)
	{
		if (index) text << ", ";
		text << sections[index];
	}
	return text.str();
}

struct ConfigurationEditorSearch
{
	std::wstring expectedPath;
	HWND window = nullptr;
	bool exactInstallationMatch = false;
	int score = -1;
};

bool IsConfigurationEditorTopLevel(HWND window, DWORD expectedProcessId,
	bool requireVisible)
{
	if (!window || !::IsWindow(window) ||
		::GetAncestor(window, GA_ROOT) != window ||
		(requireVisible && !::IsWindowVisible(window)))
		return false;
	DWORD processId = 0;
	::GetWindowThreadProcessId(window, &processId);
	if (!processId || (expectedProcessId && processId != expectedProcessId))
		return false;
	wchar_t className[128] = {};
	wchar_t title[128] = {};
	if (::GetClassNameW(window, className, ARRAYSIZE(className)) <= 0 ||
		_wcsnicmp(className, L"Qt", 2) != 0 ||
		::GetWindowTextW(window, title, ARRAYSIZE(title)) <= 0 ||
		wcscmp(title, L"VideoProcessor Configuration") != 0)
		return false;
	return true;
}

BOOL CALLBACK FindConfigurationEditor(HWND window, LPARAM parameter)
{
	auto* search = reinterpret_cast<ConfigurationEditorSearch*>(parameter);
	if (!IsConfigurationEditorTopLevel(window, 0, false))
		return TRUE;
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	HANDLE process = processId ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE, processId) : nullptr;
	if (!process)
		return TRUE;
	wchar_t processPath[32768] = {};
	DWORD processPathLength = ARRAYSIZE(processPath);
	const bool matches = QueryFullProcessImageNameW(process, 0, processPath,
		&processPathLength) &&
		_wcsicmp(processPath, search->expectedPath.c_str()) == 0;
	CloseHandle(process);
	// A running Config from another checkout or deployment must never become
	// this VP instance's editor.  Cross-installation fallback leaves the
	// shortcut talking to stale Qt HWNDs, which is why the first press can be
	// consumed by rediscovery and only the next one reveals the right editor.
	if (!matches)
		return TRUE;
	const int score = (matches ? 1000 : 0) +
		(::IsWindowVisible(window) ? 100 : 0) +
		(!::IsIconic(window) ? 10 : 0);
	if (score > search->score)
	{
		search->window = window;
		search->exactInstallationMatch = matches;
		search->score = score;
	}
	return FALSE;
}

struct ConfigurationEditorProcessSearch
{
	DWORD processId = 0;
	HWND excluded = nullptr;
	HWND window = nullptr;
	int score = -1;
};

BOOL CALLBACK FindConfigurationEditorForProcessCallback(HWND window,
	LPARAM parameter)
{
	auto* search = reinterpret_cast<ConfigurationEditorProcessSearch*>(parameter);
	if (window == search->excluded ||
		!IsConfigurationEditorTopLevel(window, search->processId, false))
		return TRUE;
	const int score = (::IsWindowVisible(window) ? 100 : 0) +
		(!::IsIconic(window) ? 10 : 0);
	if (score > search->score)
	{
		search->window = window;
		search->score = score;
	}
	return TRUE;
}

HWND FindConfigurationEditorForProcess(DWORD processId, HWND excluded)
{
	ConfigurationEditorProcessSearch search{ processId, excluded };
	::EnumWindows(FindConfigurationEditorForProcessCallback,
		reinterpret_cast<LPARAM>(&search));
	return search.window;
}

HWND FindConfigurationEditorForCurrentInstallation()
{
	std::wstring applicationDirectory;
	if (!GetApplicationDirectory(applicationDirectory))
		return nullptr;
	const std::wstring expectedPath =
		ConfigurationEditorPath(applicationDirectory);
	ConfigurationEditorSearch search{ expectedPath };
	EnumWindows(FindConfigurationEditor,
		reinterpret_cast<LPARAM>(&search));
	return search.window;
}

bool SignalConfigurationEditorReveal(DWORD processId)
{
	if (!processId)
		return false;
	// The warm Config process is separate from the foreground VP process.
	// Grant it the one-shot foreground right before signaling reveal; otherwise
	// Windows may show its topmost window without activating it and consume the
	// operator's first selector click merely to activate the editor.
	::AllowSetForegroundWindow(processId);
	const std::wstring eventName =
		ConfigurationLiveApply::ConfigurationEditorRevealEventName(processId);
	HANDLE eventHandle = ::OpenEventW(EVENT_MODIFY_STATE, FALSE,
		eventName.c_str());
	if (!eventHandle)
		return false;
	const bool signaled = ::SetEvent(eventHandle) != FALSE;
	::CloseHandle(eventHandle);
	return signaled;
}

bool SendConfigurationEditorRevealOnce(HWND editor, HWND owner,
	DWORD ownerProcessId, bool& messageDelivered,
	DWORD_PTR& acknowledgement)
{
	messageDelivered = false;
	if (!editor || !::IsWindow(editor))
		return false;
	DWORD editorProcessId = 0;
	::GetWindowThreadProcessId(editor, &editorProcessId);
	if (editorProcessId)
		::AllowSetForegroundWindow(editorProcessId);
	static const UINT activateMessage = ::RegisterWindowMessageW(
		L"VideoProcessor.ConfigEditor.Activate.v1");
	if (!activateMessage)
		return false;
	acknowledgement = 0;
	const LRESULT delivered = ::SendMessageTimeoutW(editor, activateMessage,
		static_cast<WPARAM>(ownerProcessId),
		reinterpret_cast<LPARAM>(owner),
		SMTO_ABORTIFHUNG | SMTO_BLOCK, 400, &acknowledgement);
	DebugLog::Log(
		"Configuration editor reveal attempt: editor=%p pid=%lu owner=%p owner_pid=%lu delivered=%lld ack=%llu",
		reinterpret_cast<void*>(editor), editorProcessId,
		reinterpret_cast<void*>(owner), ownerProcessId,
		static_cast<long long>(delivered),
		static_cast<unsigned long long>(acknowledgement));
	messageDelivered = delivered != 0;
	return ConfigurationLiveApply::ConfigurationEditorRevealAcknowledged(
		messageDelivered, acknowledgement);
}

bool LaunchConfigurationEditorActivationFallback(HWND owner,
	DWORD ownerProcessId)
{
	std::wstring applicationDirectory;
	if (!GetApplicationDirectory(applicationDirectory))
		return false;
	const std::wstring editorPath =
		ConfigurationEditorPath(applicationDirectory);
	const std::wstring workingDirectory =
		ConfigurationEditorDirectory(applicationDirectory);
	if (::GetFileAttributesW(editorPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		return false;
	wchar_t arguments[160] = {};
	swprintf_s(arguments, L"--owner %llu --owner-process %lu",
		static_cast<unsigned long long>(
			reinterpret_cast<UINT_PTR>(owner)), ownerProcessId);
	const HINSTANCE result = ::ShellExecuteW(owner, L"open",
		editorPath.c_str(), arguments, workingDirectory.c_str(), SW_SHOWNORMAL);
	return reinterpret_cast<INT_PTR>(result) > 32;
}

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
	case RendererResetReason::ProfileChange: return TEXT("profile-change");
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
	{ "fullscreen_toggle",     ID_COMMAND_FULLSCREEN_TOGGLE,
		ConfigurationLiveApply::ViewToggleDefaultKey,
		ConfigurationLiveApply::ViewToggleDefaultModifiers },
	{ "toggle_stats_overlay",  ID_COMMAND_TOGGLE_STATS_OVERLAY,   'I',       FCONTROL },
	{ "capture_rendered_output", ID_COMMAND_CAPTURE_RENDERED_OUTPUT, 'S',     FCONTROL | FALT },
	{ "pq_set",                ID_COMMAND_PQ_SET,                 'P',       FCONTROL | FSHIFT },
	{ "renderer_restart",      ID_COMMAND_RENDERER_RESTART,       'R',       FSHIFT },
	{ "renderer_reset",        ID_COMMAND_RENDERER_RESET,         'R',       0 },
	{ "capture_1",             ID_COMMAND_CAPTURE_1,              '1',       FCONTROL },
	{ "capture_2",             ID_COMMAND_CAPTURE_2,              '2',       FCONTROL },
	{ "capture_3",             ID_COMMAND_CAPTURE_3,              '3',       FCONTROL },
	{ "capture_4",             ID_COMMAND_CAPTURE_4,              '4',       FCONTROL },
	{ "video_conversion_off",  ID_COMMAND_VC_NONE,                'V',       0 },
	{ "video_conversion_p010", ID_COMMAND_VC_P010,                'V',       FSHIFT },
	{ "config_editor",         ID_COMMAND_CONFIG_EDITOR,          'S',       FCONTROL | FSHIFT },
	{ "toggle_noui",           ID_COMMAND_TOGGLE_NO_UI,
		ConfigurationLiveApply::VideoOnlyToggleDefaultKey,
		ConfigurationLiveApply::VideoOnlyToggleDefaultModifiers },
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
	std::map<WORD, CString>& unifiedProfileShortcutKeys,
	std::vector<ACCEL>& configuredAccelerators,
	const ConfigFile* stagedMainConfig = nullptr,
	const ConfigFile* stagedRendererConfig = nullptr,
	bool rejectInvalidBindings = false,
	std::string* bindingError = nullptr)
{
	shaderShortcutRules.clear();
	shaderShortcutKeys.clear();
	displayRuleShortcutRules.clear();
	rendererShortcutIndices.clear();
	unifiedProfileShortcutKeys.clear();
	ConfigFile loadedMainConfig;
	const bool hasMainConfig = stagedMainConfig != nullptr ||
		loadedMainConfig.Load();
	const ConfigFile& mainConfig = stagedMainConfig ?
		*stagedMainConfig : loadedMainConfig;
	ConfigFile loadedRendererConfig;
	const bool hasRendererConfig = stagedRendererConfig != nullptr ||
		loadedRendererConfig.Load(ConfigFile::RENDERER_FILENAME);
	const ConfigFile& rendererConfig = stagedRendererConfig ?
		*stagedRendererConfig : loadedRendererConfig;
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
	auto failBinding = [bindingError](const std::string& error)
	{
		if (bindingError)
			*bindingError = error;
	};

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
			else if (rejectInvalidBindings)
			{
				failBinding(std::string("invalid [shortcuts] ") +
					definition.configKey);
				return nullptr;
			}
		}

		const unsigned int binding = (static_cast<unsigned int>(accelerator.fVirt) << 16) | accelerator.key;
		if (bindings.insert(binding).second)
		{
			accelerators.push_back(accelerator);
		}
		else
		{
			if (rejectInvalidBindings)
			{
				failBinding(std::string("duplicate [shortcuts] binding for ") +
					definition.configKey);
				return nullptr;
			}
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
				if (rejectInvalidBindings)
				{
					failBinding("invalid renderer-selection shortcut " + entry.first);
					return nullptr;
				}
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
				if (rejectInvalidBindings)
				{
					failBinding("duplicate renderer-selection shortcut " + entry.first);
					return nullptr;
				}
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
				if (rejectInvalidBindings)
				{
					failBinding("invalid shader shortcut in [" + section + "]");
					return nullptr;
				}
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
				if (rejectInvalidBindings)
				{
					failBinding("shader shortcut conflicts with another command in [" + section + "]");
					return nullptr;
				}
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
				if (rejectInvalidBindings)
				{
					failBinding("invalid shortcut in [" + section + "]: " +
						mergeError);
					return nullptr;
				}
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
				if (rejectInvalidBindings)
				{
					failBinding("invalid shortcut expression in [" + section +
						"]: " + error);
					return nullptr;
				}
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
					if (rejectInvalidBindings)
					{
						failBinding("unsupported shortcut " + chord +
							" in [" + section + "]");
						return nullptr;
					}
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
					if (rejectInvalidBindings)
					{
						failBinding("shortcut " + chord +
							" conflicts with another command in [" + section + "]");
						return nullptr;
					}
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
				if (rejectInvalidBindings)
				{
					failBinding("invalid display-rule shortcut " + rule);
					return nullptr;
				}
				DEBUGLOG("Invalid shortcut '%s' for display rule '%s'", shortcut.c_str(), rule.c_str());
				continue;
			}

			const unsigned int binding =
				(static_cast<unsigned int>(accelerator.fVirt) << 16) | accelerator.key;
			if (!bindings.insert(binding).second)
			{
				if (rejectInvalidBindings)
				{
					failBinding("duplicate display-rule shortcut " + rule);
					return nullptr;
				}
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
			if (rejectInvalidBindings)
			{
				failBinding("unified shortcut discovery failed: " +
					unifiedProfileError);
				return nullptr;
			}
			DEBUGLOG("Unified renderer shortcut discovery failed: %s", unifiedProfileError.c_str());
		}
		else
		{
			WORD nextCommand = ID_COMMAND_UNIFIED_PROFILE_FIRST;
			for (const std::string& chord : chords)
			{
				if (nextCommand > ID_COMMAND_UNIFIED_PROFILE_LAST) break;
				ACCEL accelerator = {};
				if (!TryParseShortcut(chord, accelerator)) { if (rejectInvalidBindings) { failBinding("invalid unified profile shortcut " + chord); return nullptr; } DEBUGLOG("Invalid unified profile shortcut '%s'", chord.c_str()); continue; }
				const unsigned int binding = (static_cast<unsigned int>(accelerator.fVirt) << 16) | accelerator.key;
				if (!bindings.insert(binding).second) { if (rejectInvalidBindings) { failBinding("duplicate unified profile shortcut " + chord); return nullptr; } DEBUGLOG("Duplicate unified profile shortcut '%s' ignored", chord.c_str()); continue; }
				accelerator.cmd = nextCommand;
				accelerators.push_back(accelerator);
				CString keyName; keyName.Format(TEXT("%S"), chord.c_str());
				unifiedProfileShortcutKeys[nextCommand] = keyName;
				++nextCommand;
			}
		}
	}

	configuredAccelerators = accelerators;
	return CreateAcceleratorTable(accelerators.data(), static_cast<int>(accelerators.size()));
}

// Modern needs to observe VP shortcuts while another application owns focus,
// but keyboard Raw Input registration is process-wide and conflicts with
// in-process renderers such as madVR. Keep the observer on its own message
// thread, post VP commands without consuming the original key, and leave the
// normal MFC/DirectShow message-drain route authoritative while VP owns focus.
class GlobalShortcutObserver
{
public:
	static bool Start(HWND target, const std::vector<ACCEL>& accelerators)
	{
		Stop();
		if (!target || accelerators.empty())
			return false;
		s_target = target;
		s_accelerators = accelerators;
		s_readyEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!s_readyEvent)
		{
			ClearState();
			return false;
		}
		s_thread = ::CreateThread(nullptr, 0, ThreadProcedure, nullptr, 0,
			&s_threadId);
		if (!s_thread)
		{
			::CloseHandle(s_readyEvent);
			s_readyEvent = nullptr;
			ClearState();
			return false;
		}
		const DWORD ready = ::WaitForSingleObject(s_readyEvent, 2000);
		::CloseHandle(s_readyEvent);
		s_readyEvent = nullptr;
		if (ready != WAIT_OBJECT_0 || !s_hook)
		{
			Stop();
			return false;
		}
		return true;
	}

	static void Stop()
	{
		if (s_thread)
		{
			if (s_threadId)
				::PostThreadMessageW(s_threadId, WM_QUIT, 0, 0);
			::WaitForSingleObject(s_thread, INFINITE);
			::CloseHandle(s_thread);
		}
		s_thread = nullptr;
		s_threadId = 0;
		s_hook = nullptr;
		ClearState();
	}

	static bool IsRunning() { return s_thread != nullptr && s_hook != nullptr; }

private:
	static void ClearState()
	{
		s_target = nullptr;
		s_accelerators.clear();
		s_pressedKeys.clear();
	}

	static DWORD WINAPI ThreadProcedure(void*)
	{
		MSG message = {};
		::PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
		s_hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, HookProcedure,
			::GetModuleHandleW(nullptr), 0);
		if (s_readyEvent)
			::SetEvent(s_readyEvent);
		if (s_hook)
		{
			while (::GetMessageW(&message, nullptr, 0, 0) > 0)
			{
				::TranslateMessage(&message);
				::DispatchMessageW(&message);
			}
			::UnhookWindowsHookEx(s_hook);
			s_hook = nullptr;
		}
		return 0;
	}

	static LRESULT CALLBACK HookProcedure(int code, WPARAM message,
		LPARAM parameter)
	{
		if (code != HC_ACTION || !s_target)
			return ::CallNextHookEx(s_hook, code, message, parameter);
		const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(parameter);
		const WORD virtualKey = static_cast<WORD>(key->vkCode);
		const bool keyUp = message == WM_KEYUP || message == WM_SYSKEYUP;
		if (keyUp)
		{
			s_pressedKeys.erase(virtualKey);
			return ::CallNextHookEx(s_hook, code, message, parameter);
		}
		if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
			return ::CallNextHookEx(s_hook, code, message, parameter);
		if (!s_pressedKeys.insert(virtualKey).second)
			return ::CallNextHookEx(s_hook, code, message, parameter);

		const HWND foreground = ::GetForegroundWindow();
		DWORD foregroundProcessId = 0;
		if (foreground)
			::GetWindowThreadProcessId(foreground, &foregroundProcessId);
		const HWND configurationEditor =
			FindConfigurationEditorForCurrentInstallation();
		DWORD configurationEditorProcessId = 0;
		if (configurationEditor)
			::GetWindowThreadProcessId(configurationEditor,
				&configurationEditorProcessId);
		const bool configurationModal =
			configurationEditorProcessId != 0 &&
			foregroundProcessId == configurationEditorProcessId;
		if (!ConfigurationLiveApply::MayDispatchGlobalShortcut(
			::GetCurrentProcessId(), foregroundProcessId, configurationModal))
		{
			return ::CallNextHookEx(s_hook, code, message, parameter);
		}

		const bool control = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		const bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
		const bool shift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
		const bool rightAlt = (::GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
		const ACCEL* matchedAccelerator = nullptr;
		for (const ACCEL& accelerator : s_accelerators)
		{
			if (accelerator.key == virtualKey &&
				ConfigurationLiveApply::ShortcutModifiersMatch(
					(accelerator.fVirt & FCONTROL) != 0,
					(accelerator.fVirt & FALT) != 0,
					(accelerator.fVirt & FSHIFT) != 0,
					control, alt, shift))
			{
				matchedAccelerator = &accelerator;
				break;
			}
		}
		if (!matchedAccelerator && rightAlt)
		{
			for (const ACCEL& accelerator : s_accelerators)
			{
				if (accelerator.cmd == ID_COMMAND_FULLSCREEN_TOGGLE &&
					accelerator.key == virtualKey &&
					ConfigurationLiveApply::FullscreenShortcutModifiersMatch(
						(accelerator.fVirt & FCONTROL) != 0,
						(accelerator.fVirt & FALT) != 0,
						(accelerator.fVirt & FSHIFT) != 0,
						control, alt, shift, rightAlt))
				{
					matchedAccelerator = &accelerator;
					break;
				}
			}
		}
		if (matchedAccelerator)
		{
			const ACCEL& accelerator = *matchedAccelerator;
			if (!ConfigurationLiveApply::MayDispatchBackgroundAccelerator(
				(accelerator.fVirt & FCONTROL) != 0,
				(accelerator.fVirt & FALT) != 0,
				(accelerator.fVirt & FSHIFT) != 0))
			{
				return ::CallNextHookEx(s_hook, code, message, parameter);
			}
			if (accelerator.cmd == ID_COMMAND_CONFIG_EDITOR)
			{
				// The configuration editor owns this key while it has focus so
				// its normal close-to-tray behavior handles the hide operation.
				// VP remains responsible for the global reveal when Config is hidden.
				if (foreground == FindConfigurationEditorForCurrentInstallation())
					return ::CallNextHookEx(s_hook, code, message, parameter);
				const BOOL posted = ::PostMessageW(s_target, WM_COMMAND,
					MAKEWPARAM(accelerator.cmd, 0), 0);
				DebugLog::Log(
					"Background shortcut dispatch: command=%u foreground_pid=%lu posted=%d consume=1",
					static_cast<unsigned>(accelerator.cmd), foregroundProcessId,
					posted ? 1 : 0);
				if (posted)
					return 1;
				return ::CallNextHookEx(s_hook, code, message, parameter);
			}
			const bool consumeOriginal =
				accelerator.cmd == ID_COMMAND_TOGGLE_NO_UI;
			const BOOL posted = ::PostMessageW(s_target, WM_COMMAND,
				MAKEWPARAM(accelerator.cmd, 0), 0);
			DebugLog::Log(
				"Background shortcut dispatch: command=%u foreground_pid=%lu posted=%d consume=%d",
				static_cast<unsigned>(accelerator.cmd), foregroundProcessId,
				posted ? 1 : 0, consumeOriginal ? 1 : 0);
			if (consumeOriginal && posted)
				return 1;
		}
		// Except for the VP-owned runtime UI toggle, do not consume the original.
		// madVR/the foreground application receives its normal keyboard event.
		return ::CallNextHookEx(s_hook, code, message, parameter);
	}

	static HANDLE s_thread;
	static DWORD s_threadId;
	static HANDLE s_readyEvent;
	static HHOOK s_hook;
	static HWND s_target;
	static std::vector<ACCEL> s_accelerators;
	static std::set<WORD> s_pressedKeys;
};

HANDLE GlobalShortcutObserver::s_thread = nullptr;
DWORD GlobalShortcutObserver::s_threadId = 0;
HANDLE GlobalShortcutObserver::s_readyEvent = nullptr;
HHOOK GlobalShortcutObserver::s_hook = nullptr;
HWND GlobalShortcutObserver::s_target = nullptr;
std::vector<ACCEL> GlobalShortcutObserver::s_accelerators;
std::set<WORD> GlobalShortcutObserver::s_pressedKeys;

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

static UINT WM_CONFIGURATION_EDITOR_ASSOCIATION =
	::RegisterWindowMessageW(L"VideoProcessor.ConfigEditor.Association.v1");
static UINT WM_CONFIGURATION_EDITOR_PRESENTATION_TARGET_ACKNOWLEDGEMENT =
	::RegisterWindowMessageW(
		L"VideoProcessor.ConfigEditor.PresentationTargetAck.v2");

BEGIN_MESSAGE_MAP(CVideoProcessorDlg, CDialog)

	// Pre-baked callbacks
	ON_WM_PAINT()
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_QUERYDRAGICON()
	ON_WM_GETMINMAXINFO()
	ON_WM_SETFOCUS()
	ON_WM_CLOSE()
	ON_WM_SYSCOMMAND()
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
	ON_MESSAGE(WM_MESSAGE_EXTERNAL_SHORTCUT, &CVideoProcessorDlg::OnMessageExternalShortcut)
	ON_MESSAGE(WM_MESSAGE_RENDERER_LIVE_FRAME, &CVideoProcessorDlg::OnMessageRendererLiveFrame)
	ON_MESSAGE(WM_MESSAGE_RENDERER_RESET_REQUEST, &CVideoProcessorDlg::OnMessageRendererResetRequest)
	ON_MESSAGE(WM_MESSAGE_RENDERER_RETIRED, &CVideoProcessorDlg::OnMessageRendererRetired)
	ON_MESSAGE(WM_MESSAGE_RENDERER_INTENT_READY, &CVideoProcessorDlg::OnMessageRendererIntentReady)
	ON_MESSAGE(WM_MESSAGE_RENDERER_GRAPH_EVENT, &CVideoProcessorDlg::OnMessageRendererGraphEvent)
	ON_MESSAGE(WM_MESSAGE_RENDERER_RESTART_REQUIRED, &CVideoProcessorDlg::OnMessageRendererRestartRequired)
	ON_MESSAGE(WM_MODERN_OPERATOR_ACTION, &CVideoProcessorDlg::OnMessageModernOperatorAction)

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
	ON_COMMAND(ID_COMMAND_CAPTURE_RENDERED_OUTPUT, &CVideoProcessorDlg::OnCommandCaptureRenderedOutput)
	ON_COMMAND(ID_COMMAND_DISPLAY_RULE_AUTO, &CVideoProcessorDlg::OnCommandDisplayRuleAuto)
	ON_COMMAND(ID_COMMAND_CONFIG_EDITOR, &CVideoProcessorDlg::OnCommandConfigEditor)
	ON_COMMAND(ID_COMMAND_TOGGLE_NO_UI, &CVideoProcessorDlg::OnCommandToggleNoUi)
	ON_MESSAGE(WM_HOTKEY, &CVideoProcessorDlg::OnConfigurationEditorHotkey)
	ON_REGISTERED_MESSAGE(WM_CONFIGURATION_EDITOR_ASSOCIATION,
		&CVideoProcessorDlg::OnConfigurationEditorAssociation)
	ON_REGISTERED_MESSAGE(
		WM_CONFIGURATION_EDITOR_PRESENTATION_TARGET_ACKNOWLEDGEMENT,
		&CVideoProcessorDlg::OnConfigurationEditorPresentationTargetAcknowledgement)
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
	m_configurationChangedEvent = CreateEventW(nullptr, FALSE, FALSE,
		ConfigurationLiveApply::ChangedEventName);
	LoadDisplayRefreshRateOverrides();

	ConfigFile profileConfig;
	if (profileConfig.Load())
	{
		m_configurationSnapshot = CaptureConfigurationSnapshot(profileConfig);
		std::string shortcutPolicyError;
		if (!ReadShortcutsForegroundOnly(profileConfig,
			m_shortcutsForegroundOnly, shortcutPolicyError))
		{
			m_shortcutsForegroundOnly = false;
			DebugLog::Log("Shortcut focus policy retained default: %s",
				shortcutPolicyError.c_str());
		}
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
				"Unified profile runtime restored generation %llu, viewport %s (%s, %s)",
				static_cast<unsigned long long>(snapshot->generation),
				snapshot->viewport.profile.c_str(),
				snapshot->viewport.screenAspect.Canonical().c_str(),
				snapshot->viewport.verticalAlignment.c_str());
		}
	}

	DeckLinkCaptureFormatPreferences deckLinkFormatPreferences;
	std::string deckLinkFormatError;
	if (profileConfig.IsLoaded() && !ReadDeckLinkCaptureFormatPreferences(
		profileConfig, deckLinkFormatPreferences, deckLinkFormatError))
		throw std::runtime_error(deckLinkFormatError);
	DebugLog::Log(
		"DeckLink capture packing preferences: rgb8=%s rgb10=%s rgb12=%s",
		DeckLinkPixelFormatName(deckLinkFormatPreferences.rgb8),
		DeckLinkPixelFormatName(deckLinkFormatPreferences.rgb10),
		DeckLinkPixelFormatName(deckLinkFormatPreferences.rgb12));
	m_blackMagicDeviceDiscoverer = new BlackMagicDeckLinkCaptureDeviceDiscoverer(
		*this, deckLinkFormatPreferences);
	
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

void CVideoProcessorDlg::ReloadConfiguredAccelerators()
{
	std::map<WORD, CString> shaderShortcutRules;
	std::set<WORD> shaderShortcutKeys;
	std::map<WORD, CString> displayRuleShortcutRules;
	std::map<WORD, unsigned int> rendererShortcutIndices;
	std::map<WORD, CString> unifiedProfileShortcutKeys;
	std::vector<ACCEL> configuredAccelerators;
	HACCEL candidate = CreateConfiguredAccelerators(shaderShortcutRules,
		shaderShortcutKeys, displayRuleShortcutRules,
		rendererShortcutIndices, unifiedProfileShortcutKeys,
		configuredAccelerators);
	if (!candidate)
	{
		DebugLog::Log(
			"Configuration live apply retained previous shortcuts: accelerator rebuild failed");
		return;
	}

	HACCEL previous = m_accelerator;
	m_accelerator = candidate;
	m_shaderShortcutRules = std::move(shaderShortcutRules);
	m_shaderShortcutKeys = std::move(shaderShortcutKeys);
	m_displayRuleShortcutRules = std::move(displayRuleShortcutRules);
	m_rendererShortcutIndices = std::move(rendererShortcutIndices);
	m_unifiedProfileShortcutKeys = std::move(unifiedProfileShortcutKeys);
	m_configuredAccelerators = std::move(configuredAccelerators);
	if (previous)
		DestroyAcceleratorTable(previous);
	DebugLog::Log("Configuration shortcuts applied live");
	StartGlobalShortcutObserver();
}

void CVideoProcessorDlg::StartGlobalShortcutObserver()
{
	StopGlobalShortcutObserver();
	if (!ConfigurationLiveApply::ShouldEnableBackgroundShortcuts(
		m_interfaceMode == ApplicationInterface::Mode::Modern, m_hideUI,
		m_shortcutsForegroundOnly) ||
		!GetSafeHwnd())
	{
		DebugLog::Log(
			"Background shortcut observer suppressed: modern=%d noui=%d foreground_only=%d",
			m_interfaceMode == ApplicationInterface::Mode::Modern ? 1 : 0,
			m_hideUI ? 1 : 0, m_shortcutsForegroundOnly ? 1 : 0);
		return;
	}

	const bool observerStarted = GlobalShortcutObserver::Start(GetSafeHwnd(),
		m_configuredAccelerators);
	DebugLog::Log(
		"Background shortcut observer %s (%zu bindings)",
		observerStarted ? "started" : "unavailable",
		m_configuredAccelerators.size());
}

void CVideoProcessorDlg::StopGlobalShortcutObserver()
{
	if (GlobalShortcutObserver::IsRunning())
		DebugLog::Log("Background shortcut observer stopped");
	GlobalShortcutObserver::Stop();
}

LRESULT CVideoProcessorDlg::OnConfigurationEditorHotkey(
	WPARAM wParam, LPARAM)
{
	if (wParam == CONFIGURATION_EDITOR_HOTKEY_ID)
		ToggleConfigurationEditor();
	return 0;
}

void CVideoProcessorDlg::ToggleConfigurationEditor()
{
	HWND editor = VisibleAssociatedConfigurationEditor();
	if (!editor)
	{
		editor = FindConfigurationEditorForCurrentInstallation();
		TrackConfigurationEditor(editor);
		editor = VisibleAssociatedConfigurationEditor();
	}
	const bool visible = editor && ::IsWindowVisible(editor);
	DWORD foregroundProcessId = 0;
	const HWND foreground = ::GetForegroundWindow();
	if (foreground)
		::GetWindowThreadProcessId(foreground, &foregroundProcessId);
	const bool editorOwnsForeground = visible &&
		m_configurationEditorProcessId != 0 &&
		foregroundProcessId == m_configurationEditorProcessId;
	const auto action = ConfigurationLiveApply::ResolveConfigurationEditorToggle(
		visible, editorOwnsForeground,
		m_configurationEditorActivationPending);
	DebugLog::Log(
		"Configuration editor toggle requested: editor=%p visible=%d foreground=%p foreground_pid=%lu editor_pid=%lu pending=%d action=%d",
		reinterpret_cast<void*>(editor), visible ? 1 : 0,
		reinterpret_cast<void*>(foreground), foregroundProcessId,
		m_configurationEditorProcessId,
		m_configurationEditorActivationPending ? 1 : 0,
		static_cast<int>(action));
	// This shortcut intentionally always reveals and activates Config. Closing
	// the editor remains a direct editor/tray action, never a side effect of
	// trying to bring it in front of a fullscreen video surface.
	OnCommandConfigEditor();
}

HWND CVideoProcessorDlg::ConfigurationEditorOwner()
{
	// Renderer targets are destroyed and recreated during capture and renderer
	// switches. Config needs a stable owner even when no renderer exists.
	return GetSafeHwnd();
}

void CVideoProcessorDlg::TrackConfigurationEditor(HWND editor)
{
	if (!editor || !::IsWindow(editor))
	{
		if (m_configurationEditorHwnd &&
			!::IsWindow(m_configurationEditorHwnd))
		{
			DebugLog::Log(
				"Configuration editor association cleared hwnd=%p pid=%lu",
				reinterpret_cast<void*>(m_configurationEditorHwnd),
				m_configurationEditorProcessId);
			m_configurationEditorHwnd = nullptr;
			m_configurationEditorProcessId = 0;
		}
		return;
	}
	DWORD processId = 0;
	::GetWindowThreadProcessId(editor, &processId);
	if (editor == m_configurationEditorHwnd &&
		processId == m_configurationEditorProcessId)
		return;
	m_configurationEditorHwnd = editor;
	m_configurationEditorProcessId = processId;
	DebugLog::Log(
		"Configuration editor associated hwnd=%p pid=%lu owner=%p",
		reinterpret_cast<void*>(editor), processId,
		reinterpret_cast<void*>(ConfigurationEditorOwner()));
}

HWND CVideoProcessorDlg::VisibleAssociatedConfigurationEditor() const
{
	if (!m_configurationEditorHwnd || !m_configurationEditorProcessId ||
		!IsConfigurationEditorTopLevel(m_configurationEditorHwnd,
			m_configurationEditorProcessId, false) ||
		!::IsWindowVisible(m_configurationEditorHwnd))
	{
		return nullptr;
	}
	return m_configurationEditorHwnd;
}

LRESULT CVideoProcessorDlg::OnConfigurationEditorAssociation(
	WPARAM wParam, LPARAM lParam)
{
	const DWORD advertisedProcessId = static_cast<DWORD>(wParam);
	const HWND advertisedWindow = reinterpret_cast<HWND>(lParam);
	if (!advertisedProcessId ||
		!IsConfigurationEditorTopLevel(advertisedWindow,
			advertisedProcessId, false))
	{
		DebugLog::Log(
			"Configuration editor association rejected: hwnd=%p pid=%lu reason=invalid-top-level-or-class",
			reinterpret_cast<void*>(advertisedWindow), advertisedProcessId);
		return 0;
	}
	const HWND previousWindow = m_configurationEditorHwnd;
	const DWORD previousProcessId = m_configurationEditorProcessId;
	TrackConfigurationEditor(advertisedWindow);
	PublishConfigurationEditorPresentationTarget(advertisedWindow);
	DebugLog::Log(
		"Configuration editor association advertised: previous=%p/%lu current=%p/%lu visible=%d ack=1",
		reinterpret_cast<void*>(previousWindow), previousProcessId,
		reinterpret_cast<void*>(advertisedWindow), advertisedProcessId,
		::IsWindowVisible(advertisedWindow) ? 1 : 0);
	if (ConfigurationLiveApply::ShouldRetryRevealForAssociation(
		m_configurationEditorActivationPending, true) &&
		!m_configurationEditorRevealAcknowledged)
	{
		// The editor is synchronously waiting for this Association acknowledgement.
		// Queue the reciprocal Activate request so its UI thread can return to the
		// native event loop before VP sends it.
		::PostMessage(GetSafeHwnd(), WM_COMMAND,
			MAKEWPARAM(ID_COMMAND_CONFIG_EDITOR, 0), 0);
		DebugLog::Log(
			"Configuration editor pending reveal resumed by current association: hwnd=%p pid=%lu",
			reinterpret_cast<void*>(advertisedWindow), advertisedProcessId);
	}
	return 1;
}

bool CVideoProcessorDlg::PublishConfigurationEditorPresentationTarget(
	HWND editor)
{
	if (!editor || !::IsWindow(editor))
		return false;
	const HWND fullscreenTarget = m_fullScreenVideoWindow &&
		::IsWindow(m_fullScreenVideoWindow->GetHWND()) ?
		m_fullScreenVideoWindow->GetHWND() : nullptr;
	const HWND windowedTarget =
		::IsWindow(m_windowedVideoWindow.GetSafeHwnd()) ?
		m_windowedVideoWindow.GetSafeHwnd() : nullptr;
	HWND target = reinterpret_cast<HWND>(
		ConfigurationLiveApply::SelectConfigurationEditorPresentationTarget(
			m_rendererFullscreenCheck.GetCheck() != FALSE,
			reinterpret_cast<uintptr_t>(fullscreenTarget),
			::IsWindow(m_rendererTargetHwnd) ?
				reinterpret_cast<uintptr_t>(m_rendererTargetHwnd) : 0,
			reinterpret_cast<uintptr_t>(windowedTarget)));
	if (target && ::IsWindow(target))
		target = ::GetAncestor(target, GA_ROOT);
	if (!target || !::IsWindow(target))
		target = GetSafeHwnd();
	DWORD targetProcessId = 0;
	if (target)
		::GetWindowThreadProcessId(target, &targetProcessId);
	if (!ConfigurationLiveApply::IsValidatedPresentationTargetProcess(
		GetCurrentProcessId(), targetProcessId,
		target && ::IsWindow(target)))
	{
		DebugLog::Log(
			"Configuration editor presentation target skipped: editor=%p target=%p target_pid=%lu vp_pid=%lu",
			reinterpret_cast<void*>(editor), reinterpret_cast<void*>(target),
			targetProcessId, GetCurrentProcessId());
		return false;
	}
	static const UINT targetMessage = ::RegisterWindowMessageW(
		L"VideoProcessor.ConfigEditor.PresentationTarget.v2");
	static const UINT acknowledgementEndpointMessage =
		::RegisterWindowMessageW(
			L"VideoProcessor.ConfigEditor.PresentationTargetAckEndpoint.v1");
	const HWND acknowledgementEndpoint = GetSafeHwnd();
	if (!targetMessage || !acknowledgementEndpointMessage ||
		!acknowledgementEndpoint || !::IsWindow(acknowledgementEndpoint))
		return false;
	const ULONGLONG now = GetTickCount64();
	if (editor == m_configurationEditorPresentationEditor &&
		target == m_configurationEditorPresentationTarget &&
		m_configurationEditorPresentationRequired != 0)
	{
		if (m_configurationEditorPresentationAcknowledged ==
			m_configurationEditorPresentationRequired)
		{
			return true;
		}
		if (m_configurationEditorPresentationQueuedTick != 0 &&
			now - m_configurationEditorPresentationQueuedTick < 1500)
		{
			return true;
		}
	}
	uint32_t sequence = ++m_configurationEditorPresentationSequence;
	if (sequence == 0)
		sequence = ++m_configurationEditorPresentationSequence;
	const WPARAM request =
		(static_cast<WPARAM>(sequence) << 32) |
		static_cast<WPARAM>(GetCurrentProcessId());
	const BOOL endpointQueued = ::PostMessageW(editor,
		acknowledgementEndpointMessage,
		static_cast<WPARAM>(GetCurrentProcessId()),
		reinterpret_cast<LPARAM>(acknowledgementEndpoint));
	if (!endpointQueued)
	{
		DebugLog::Log(
			"Configuration editor presentation acknowledgement endpoint queue failed: editor=%p endpoint=%p error=%lu",
			reinterpret_cast<void*>(editor),
			reinterpret_cast<void*>(acknowledgementEndpoint), GetLastError());
		return false;
	}
	const BOOL queued = ::PostMessageW(editor, targetMessage, request,
		reinterpret_cast<LPARAM>(target));
	if (!queued)
	{
		DebugLog::Log(
			"Configuration editor presentation target queue failed: editor=%p target=%p target_pid=%lu sequence=%u error=%lu",
			reinterpret_cast<void*>(editor), reinterpret_cast<void*>(target),
			targetProcessId, sequence, GetLastError());
		return false;
	}
	m_configurationEditorPresentationRequired = sequence;
	m_configurationEditorPresentationEditor = editor;
	m_configurationEditorPresentationTarget = target;
	m_configurationEditorPresentationQueuedTick = now;
	m_configurationEditorPresentationTimeoutLogged = false;
	DebugLog::Log(
		"Configuration editor presentation target queued: editor=%p target=%p target_pid=%lu monitor=%p sequence=%u queued=1 acknowledgement=awaiting",
		reinterpret_cast<void*>(editor), reinterpret_cast<void*>(target),
		targetProcessId,
		reinterpret_cast<void*>(::MonitorFromWindow(
			target, MONITOR_DEFAULTTONEAREST)), sequence);
	return true;
}

LRESULT CVideoProcessorDlg::OnConfigurationEditorPresentationTargetAcknowledgement(
	WPARAM wParam, LPARAM lParam)
{
	const uint32_t sequence = static_cast<uint32_t>(wParam);
	const HWND acknowledgedEditor = reinterpret_cast<HWND>(lParam);
	if (sequence == 0 ||
		sequence != m_configurationEditorPresentationRequired ||
		acknowledgedEditor != m_configurationEditorPresentationEditor)
	{
		DebugLog::Log(
			"Configuration editor presentation target acknowledgement ignored: sequence=%u required=%u editor=%p expected=%p",
			sequence, m_configurationEditorPresentationRequired,
			reinterpret_cast<void*>(acknowledgedEditor),
			reinterpret_cast<void*>(m_configurationEditorPresentationEditor));
		return 0;
	}
	if (!m_configurationEditorPresentationEditor ||
		!::IsWindow(m_configurationEditorPresentationEditor) ||
		!IsConfigurationEditorTopLevel(
			m_configurationEditorPresentationEditor,
			m_configurationEditorProcessId, false) ||
		!m_configurationEditorPresentationTarget ||
		!::IsWindow(m_configurationEditorPresentationTarget))
	{
		DebugLog::Log(
			"Configuration editor presentation target acknowledgement rejected: sequence=%u editor=%p target=%p reason=stale-window",
			sequence,
			reinterpret_cast<void*>(m_configurationEditorPresentationEditor),
			reinterpret_cast<void*>(m_configurationEditorPresentationTarget));
		return 0;
	}
	m_configurationEditorPresentationAcknowledged = sequence;
	m_configurationEditorPresentationQueuedTick = 0;
	m_configurationEditorPresentationTimeoutLogged = false;
	DebugLog::Log(
		"Configuration editor presentation target acknowledged: editor=%p target=%p sequence=%u accepted=1",
		reinterpret_cast<void*>(m_configurationEditorPresentationEditor),
		reinterpret_cast<void*>(m_configurationEditorPresentationTarget), sequence);
	RequestConfigurationEditorOneShotReassert(
		m_configurationEditorPresentationEditor,
		m_configurationEditorPresentationTarget);
	return 1;
}

void CVideoProcessorDlg::RequestPresentationFocus(const char* reason,
	unsigned int generation)
{
	HWND configurationEditor = VisibleAssociatedConfigurationEditor();
	if (!configurationEditor)
		configurationEditor = FindConfigurationEditorForCurrentInstallation();
	const bool configurationVisible = configurationEditor &&
		::IsWindow(configurationEditor) &&
		::IsWindowVisible(configurationEditor);

	const HWND fullscreenTarget = m_fullScreenVideoWindow &&
		::IsWindow(m_fullScreenVideoWindow->GetHWND()) ?
		m_fullScreenVideoWindow->GetHWND() : nullptr;
	const HWND windowedTarget =
		::IsWindow(m_windowedVideoWindow.GetSafeHwnd()) ?
		m_windowedVideoWindow.GetSafeHwnd() : nullptr;
	HWND target = reinterpret_cast<HWND>(
		ConfigurationLiveApply::SelectConfigurationEditorPresentationTarget(
			m_rendererFullscreenCheck.GetCheck() != FALSE,
			reinterpret_cast<uintptr_t>(fullscreenTarget),
			::IsWindow(m_rendererTargetHwnd) ?
				reinterpret_cast<uintptr_t>(m_rendererTargetHwnd) : 0,
			reinterpret_cast<uintptr_t>(windowedTarget)));
	if (target && ::IsWindow(target))
		target = ::GetAncestor(target, GA_ROOT);
	if (!target || !::IsWindow(target))
		target = GetSafeHwnd();
	DWORD targetProcessId = 0;
	if (target) ::GetWindowThreadProcessId(target, &targetProcessId);
	const bool validTarget = target && ::IsWindow(target) &&
		targetProcessId == GetCurrentProcessId();
	if (!ConfigurationLiveApply::ShouldRequestPresentationFocus(
		m_shortcutsForegroundOnly, configurationVisible, validTarget))
	{
		DebugLog::Log(
			"Presentation focus request skipped: reason=%s generation=%u foreground_only=%d config_visible=%d valid_target=%d target=%p",
			reason, generation, m_shortcutsForegroundOnly ? 1 : 0,
			configurationVisible ? 1 : 0, validTarget ? 1 : 0, target);
		return;
	}

	if (::IsIconic(target)) ::ShowWindowAsync(target, SW_RESTORE);
	::BringWindowToTop(target);
	const BOOL requested = ::SetForegroundWindow(target);
	const HWND foreground = ::GetForegroundWindow();
	DebugLog::Log(
		"Presentation focus request: reason=%s generation=%u target=%p requested=%d foreground=%p acquired=%d",
		reason, generation, target, requested ? 1 : 0, foreground,
		foreground == target ? 1 : 0);
}

bool CVideoProcessorDlg::RequestConfigurationEditorOneShotReassert(
	HWND editor, HWND presentationTarget)
{
	if (!editor || !::IsWindow(editor) || !presentationTarget ||
		!::IsWindow(presentationTarget))
	{
		return false;
	}
	DWORD targetProcessId = 0;
	::GetWindowThreadProcessId(presentationTarget, &targetProcessId);
	if (!ConfigurationLiveApply::IsValidatedPresentationTargetProcess(
		GetCurrentProcessId(), targetProcessId, true))
	{
		return false;
	}
	static const UINT reassertMessage = ::RegisterWindowMessageW(
		L"VideoProcessor.ConfigEditor.Reassert.v1");
	if (!reassertMessage)
		return false;
	const BOOL posted = ::PostMessageW(editor, reassertMessage,
		static_cast<WPARAM>(GetCurrentProcessId()),
		reinterpret_cast<LPARAM>(presentationTarget));
	DebugLog::Log(
		"Configuration editor one-shot reassert requested: editor=%p target=%p posted=%d",
		reinterpret_cast<void*>(editor),
		reinterpret_cast<void*>(presentationTarget), posted ? 1 : 0);
	return posted != FALSE;
}

bool CVideoProcessorDlg::RequestConfigurationEditorReveal(HWND editor)
{
	DWORD editorProcessId = 0;
	if (editor)
		::GetWindowThreadProcessId(editor, &editorProcessId);
	if (!editorProcessId)
		return false;

	HWND target = editor;
	if (m_configurationEditorProcessId == editorProcessId &&
		IsConfigurationEditorTopLevel(m_configurationEditorHwnd,
			editorProcessId, false))
		target = m_configurationEditorHwnd;

	DWORD_PTR acknowledgement = 0;
	bool messageDelivered = false;
	PublishConfigurationEditorPresentationTarget(target);
	if (SendConfigurationEditorRevealOnce(target,
		ConfigurationEditorOwner(), GetCurrentProcessId(), messageDelivered,
		acknowledgement))
	{
		TrackConfigurationEditor(target);
		return true;
	}

	// Qt may recreate its native main-window HWND during transient-owner or
	// popup transitions. An old HWND can remain valid and accept the message
	// while no longer hosting ConfigEditorWindow::nativeEvent (ack=0). Rediscover
	// a current same-process Qt top level and retry exactly once.
	if (!ConfigurationLiveApply::ShouldRediscoverConfigurationEditor(
		messageDelivered, acknowledgement, false))
		return false;
	const HWND rediscovered = FindConfigurationEditorForProcess(
		editorProcessId, target);
	if (!rediscovered)
	{
		DebugLog::Log(
			"Configuration editor reveal rediscovery failed: stale=%p pid=%lu first_ack=%llu",
			reinterpret_cast<void*>(target), editorProcessId,
			static_cast<unsigned long long>(acknowledgement));
		return false;
	}
	TrackConfigurationEditor(rediscovered);
	PublishConfigurationEditorPresentationTarget(rediscovered);
	DWORD_PTR retryAcknowledgement = 0;
	bool retryDelivered = false;
	const bool acknowledged = SendConfigurationEditorRevealOnce(rediscovered,
		ConfigurationEditorOwner(), GetCurrentProcessId(),
		retryDelivered, retryAcknowledgement);
	DebugLog::Log(
		"Configuration editor reveal rediscovered: stale=%p current=%p pid=%lu first_ack=%llu retry_ack=%llu success=%d",
		reinterpret_cast<void*>(target),
		reinterpret_cast<void*>(rediscovered), editorProcessId,
		static_cast<unsigned long long>(acknowledgement),
		static_cast<unsigned long long>(retryAcknowledgement),
		acknowledged ? 1 : 0);
	return acknowledged;
}

void CVideoProcessorDlg::UpdateConfigurationEditorModal()
{
	// Recurring Config lease/polling was removed. Cold-start reveal resumes once
	// from Association.v1; explicit shortcut activation is dispatched once.
	return;
#if 0
	// Config is a separate Qt application with its own tray lifetime and
	// native owner association. VP must not repeatedly retarget, raise, or model
	// every visible Config window as a cross-process modal dialog: the hidden
	// warm instance can briefly be visible during startup, and that loop caused
	// the reported activation/hang behavior. This timer performs one bounded
	// activation only after an explicit VP command; VP itself remains enabled.
	constexpr ULONGLONG revealTimeoutMs = 20000;
	constexpr ULONGLONG revealRetryIntervalMs = 500;
	constexpr ULONGLONG foregroundFallbackDelayMs = 5000;
	const ULONGLONG now = ::GetTickCount64();
	const ULONGLONG elapsed = m_configurationEditorRevealStartedTick == 0 ? 0 :
		now - m_configurationEditorRevealStartedTick;
	HWND editor = VisibleAssociatedConfigurationEditor();
	if (!editor)
	{
		editor = FindConfigurationEditorForCurrentInstallation();
		TrackConfigurationEditor(editor);
		editor = VisibleAssociatedConfigurationEditor();
	}
	const bool visible = editor && IsWindow(editor) && ::IsWindowVisible(editor);
	if (!visible && m_fullscreenActivationDeferredForConfigurationEditor)
	{
		const HWND fullscreen = m_fullScreenVideoWindow ?
			m_fullScreenVideoWindow->GetHWND() : nullptr;
		DWORD foregroundProcessId = 0;
		const HWND foreground = ::GetForegroundWindow();
		if (foreground)
			::GetWindowThreadProcessId(foreground, &foregroundProcessId);
		const bool mayActivate = ConfigurationLiveApply::MayActivateFullscreen(
			GetCurrentProcessId(), foregroundProcessId, foreground != nullptr);
		const bool execute = ConfigurationLiveApply::
			ShouldExecuteDeferredPresentationActivation(
				true, false, m_rendererFullscreenCheck.GetCheck() != FALSE,
				fullscreen && ::IsWindow(fullscreen),
				fullscreen && ::IsIconic(fullscreen), mayActivate);
		m_fullscreenActivationDeferredForConfigurationEditor = false;
		m_configurationEditorOwnedForegroundBeforePresentation = false;
		if (execute)
			::ShowWindow(fullscreen, SW_RESTORE);
		DebugLog::Log(
			"Fullscreen activation deferred by Config resolved: execute=%d target=%p requested=%d iconic=%d foreground=%p",
			execute ? 1 : 0, reinterpret_cast<void*>(fullscreen),
			m_rendererFullscreenCheck.GetCheck() != FALSE ? 1 : 0,
			fullscreen && ::IsIconic(fullscreen) ? 1 : 0,
			reinterpret_cast<void*>(foreground));
	}
	if (!visible)
		m_configurationEditorOwnedForegroundBeforePresentation = false;
	if (!m_configurationEditorActivationPending &&
		!m_configurationEditorFullscreenWasTopmost &&
		!m_fullscreenActivationDeferredForConfigurationEditor)
		return;
	const auto outcome = ConfigurationLiveApply::
		ResolveConfigurationEditorReveal(
			m_configurationEditorActivationPending,
			m_configurationEditorRevealAcknowledged, visible, elapsed,
			revealTimeoutMs);
	if (outcome == ConfigurationLiveApply::
		ConfigurationEditorRevealOutcome::Complete)
	{
		DebugLog::Log(
			"Configuration editor durable reveal complete: hwnd=%p pid=%lu elapsed_ms=%llu attempts=%u association_ack=1 visible=1",
			reinterpret_cast<void*>(m_configurationEditorHwnd),
			m_configurationEditorProcessId,
			static_cast<unsigned long long>(elapsed),
			m_configurationEditorActivationAttempts);
		m_configurationEditorActivationPending = false;
		m_configurationEditorActivationAttempts = 0;
		m_configurationEditorFallbackLaunched = false;
		m_configurationEditorRevealAcknowledged = false;
		m_configurationEditorActivationAcknowledgedTick = 0;
		m_configurationEditorForegroundFallbackAttempted = false;
		m_configurationEditorRevealStartedTick = 0;
		m_configurationEditorLastRevealAttemptTick = 0;
		return;
	}
	if (outcome == ConfigurationLiveApply::
		ConfigurationEditorRevealOutcome::Expired)
	{
		DebugLog::Log(
			"Configuration editor durable reveal expired: hwnd=%p pid=%lu elapsed_ms=%llu attempts=%u acknowledged=%d visible=%d launch_attempted=%d foreground_fallback=%d",
			reinterpret_cast<void*>(m_configurationEditorHwnd),
			m_configurationEditorProcessId,
			static_cast<unsigned long long>(elapsed),
			m_configurationEditorActivationAttempts,
			m_configurationEditorRevealAcknowledged ? 1 : 0,
			visible ? 1 : 0,
			m_configurationEditorFallbackLaunched ? 1 : 0,
			m_configurationEditorForegroundFallbackAttempted ? 1 : 0);
		m_configurationEditorActivationPending = false;
		m_configurationEditorActivationAttempts = 0;
		m_configurationEditorFallbackLaunched = false;
		m_configurationEditorRevealAcknowledged = false;
		m_configurationEditorActivationAcknowledgedTick = 0;
		m_configurationEditorForegroundFallbackAttempted = false;
		m_configurationEditorRevealStartedTick = 0;
		m_configurationEditorLastRevealAttemptTick = 0;
		return;
	}
	if (!m_configurationEditorActivationPending)
	{
		return;
	}

	constexpr ULONGLONG acknowledgementGraceMs = 1500;
	const bool retryIntervalReady =
		m_configurationEditorLastRevealAttemptTick == 0 ||
		now - m_configurationEditorLastRevealAttemptTick >=
			revealRetryIntervalMs;
	const ULONGLONG elapsedSinceAcknowledgement =
		m_configurationEditorActivationAcknowledgedTick == 0 ? 0 :
		now - m_configurationEditorActivationAcknowledgedTick;
	if (editor && ConfigurationLiveApply::
		ShouldRetryConfigurationEditorActivate(
			m_configurationEditorActivationPending,
			m_configurationEditorRevealAcknowledged,
			elapsedSinceAcknowledgement, acknowledgementGraceMs,
			retryIntervalReady))
	{
		++m_configurationEditorActivationAttempts;
		m_configurationEditorLastRevealAttemptTick = now;
		if (RequestConfigurationEditorReveal(editor))
		{
			m_configurationEditorRevealAcknowledged = true;
			m_configurationEditorActivationAcknowledgedTick = now;
		}
	}

	// This is intentionally a one-shot last resort. Failure does not consume
	// the durable intent; a later Association advertisement can still complete
	// the original request before the overall deadline.
	if (visible && !m_configurationEditorRevealAcknowledged &&
		!m_configurationEditorForegroundFallbackAttempted &&
		elapsed >= foregroundFallbackDelayMs)
	{
		m_configurationEditorForegroundFallbackAttempted = true;
		DebugLog::Log(
			"Configuration editor foreground fallback: editor=%p elapsed_ms=%llu action=nonactivating-flash pending_retained=1",
			reinterpret_cast<void*>(editor),
			static_cast<unsigned long long>(elapsed));
	}
#endif
}

void CVideoProcessorDlg::ApplySavedConfiguration()
{
	if (!StageSavedConfiguration("editor-apply", false))
		return;
	// The target is read when a fullscreen host is created, so publish an
	// accepted monitor change before any renderer transition. This preserves the
	// current presentation state while making the next fullscreen entry use the
	// newly saved monitor.
	PublishStagedFullscreenMonitorSelection();

	switch (m_stagedConfigurationAction)
	{
	case ConfigurationApplyPolicy::Action::RestartCapture:
	{
		const bool replaceShortcuts = m_stagedShortcutsChanged;
		if (PublishStagedConfiguration(replaceShortcuts))
		{
			DebugLog::Log(
				"Configuration transaction accepted: action=restart-capture state=published shortcuts=%s",
				replaceShortcuts ? "replaced" : "retained");
			m_wantToRestartCapture = true;
			OnCaptureDeviceSelected();
		}
		break;
	}
	case ConfigurationApplyPolicy::Action::RestartRenderer:
		DebugLog::Log(
			"Configuration transaction accepted: identity=%s action=restart-renderer state=staged",
			m_stagedConfigurationIdentity.c_str());
		m_postRendererStartRequiresGraph = true;
		m_wantToRestartRenderer = true;
		UpdateState();
		break;
	case ConfigurationApplyPolicy::Action::ResetQueues:
	case ConfigurationApplyPolicy::Action::ApplyProfiles:
	{
		const bool replaceShortcuts = m_stagedShortcutsChanged;
		if (PublishStagedConfiguration(replaceShortcuts))
			DebugLog::Log(
				"Configuration transaction accepted: action=reset-queues state=published shortcuts=%s",
				replaceShortcuts ? "replaced" : "retained");
		break;
	}
	case ConfigurationApplyPolicy::Action::ReloadShortcuts:
		PublishStagedShortcutsOnly();
		break;
	default:
		DebugLog::Log(
			"Configuration transaction complete: identity=%s action=save-only runtime=retained shortcuts=retained",
			m_stagedConfigurationIdentity.c_str());
		ClearStagedConfiguration();
		break;
	}
}

void CVideoProcessorDlg::ConfigureActiveOutputSweep(
	bool enabled, DWORD holdMs, bool showInfo, bool captureRestart,
	const CString& suite, const CString& requestedTests)
{
	m_activeOutputSweepRequested = enabled;
	m_activeOutputSweepHoldMs = holdMs < 1000 ? 1000 :
		(holdMs > 600000 ? 600000 : holdMs);
	m_activeOutputSweepShowInfo = showInfo;
	m_activeOutputSweepCaptureRestart = captureRestart;
	m_activeOutputSweepSuite = suite;
	m_activeOutputSweepSuite.MakeLower();
	m_activeOutputSweepRequestedTests = requestedTests;
	if (!enabled)
		return;

	// A live output test is useful only on the actual fullscreen target. This
	// call occurs while command-line settings are applied, before capture starts.
	StartFullScreen(true);
	DebugLog::Log(
		"Active output sweep armed: suite=%S fullscreen=required hold_ms=%lu show_info=%d reinitialize=%s requested_tests=%S",
		m_activeOutputSweepSuite.GetString(),
		m_activeOutputSweepHoldMs, m_activeOutputSweepShowInfo ? 1 : 0,
		m_activeOutputSweepCaptureRestart ? "capture" : "renderer",
		m_activeOutputSweepRequestedTests.GetString());
}

bool CVideoProcessorDlg::StartActiveOutputSweep()
{
	if (!m_activeOutputSweepRequested || m_activeOutputSweepRunning)
		return false;
	if (!IsAlphaRendererSelected())
	{
		CompleteActiveOutputSweep(L"refused: VP Renderer is not selected");
		return false;
	}
	if (m_rendererFullscreenCheck.GetCheck() != BST_CHECKED)
	{
		CompleteActiveOutputSweep(L"refused: fullscreen target is not active");
		return false;
	}
	const bool hdrSuite = m_activeOutputSweepSuite.CompareNoCase(L"hdr") == 0;
	if (hdrSuite && (!m_captureDeviceVideoState || !m_captureDeviceVideoState->valid ||
		(m_captureDeviceVideoState->eotf != EOTF::PQ &&
			m_captureDeviceVideoState->eotf != EOTF::HLG &&
			m_captureDeviceVideoState->eotf != EOTF::HDR)))
	{
		CompleteActiveOutputSweep(
			L"refused: HDR suite requires live PQ, HLG, or HDR input");
		return false;
	}

	ConfigFile rendererConfig;
	if (!rendererConfig.Load(ConfigFile::RENDERER_FILENAME) ||
		rendererConfig.GetLoadedPath().empty())
	{
		CompleteActiveOutputSweep(L"refused: active renderer config was not found");
		return false;
	}
	const int wideLength = MultiByteToWideChar(CP_ACP, 0,
		rendererConfig.GetLoadedPath().c_str(), -1, nullptr, 0);
	if (wideLength <= 1)
	{
		CompleteActiveOutputSweep(L"refused: renderer config path is invalid");
		return false;
	}
	std::wstring configPath(static_cast<size_t>(wideLength), L'\0');
	MultiByteToWideChar(CP_ACP, 0, rendererConfig.GetLoadedPath().c_str(), -1,
		&configPath[0], wideLength);
	configPath.pop_back();
	std::wstring normalizedPath = configPath;
	std::transform(normalizedPath.begin(), normalizedPath.end(),
		normalizedPath.begin(), towlower);
	// The launch helper creates this isolated copy. Refusing every other path
	// guarantees the live runner cannot rewrite a normal user configuration.
	if (normalizedPath.find(L"active-output-sweep") == std::wstring::npos)
	{
		CompleteActiveOutputSweep(
			L"refused: launch with generated active-output-sweep config");
		DebugLog::Log(
			"Active output sweep refused: renderer_config=%s reason=not-isolated-sweep-copy",
			rendererConfig.GetLoadedPath().c_str());
		return false;
	}

	auto document = std::make_unique<ConfigEditorCore::ConfigDocument>();
	std::wstring error;
	if (!document->Load(configPath, error))
	{
		CString status;
		status.Format(L"refused: cannot load sweep config (%s)", error.c_str());
		CompleteActiveOutputSweep(status);
		return false;
	}
	m_activeOutputSweepDocument = std::move(document);
	m_activeOutputSweepOriginalDocument =
		std::make_unique<ConfigEditorCore::ConfigDocument>(*m_activeOutputSweepDocument);
	ClearActiveOutputSweepSummary("new-sweep");
	m_activeOutputSweepResults.clear();
	if (!hdrSuite)
	{
		m_activeOutputSweepCases = {
		{ L"1/10 legacy direct full/sRGB", "direct", "full", "srgb", false, false, false, false, false,
			L"Test 1/10: Baseline legacy presenter; expect Flip, Full range, sRGB pixels, and a successful Present" },
		{ L"2/10 VP direct full/sRGB", "direct", "full", "srgb", false, false, true, false, false,
			L"Test 2/10: VP-owned baseline; expect Flip, Full range, sRGB, verified DXGI application, and Present" },
		{ L"3/10 full/G22 guard off", "direct", "full", "2.2", false, false, false, false, false,
			L"Test 3/10: Full pure 2.2 guard off; expect an explicit safe fallback to Full/sRGB on the legacy presenter" },
		{ L"4/10 VP full/G22 proof", "direct", "full", "2.2", false, false, true, false, false,
			L"Test 4/10: VP-owned Full pure 2.2 proof; verify pixels/owner/DXGI/Present, then visually or meter-grade the curve" },
		{ L"5/10 limited/G22 guard off", "direct", "limited", "2.2", false, false, false, false, false,
			L"Test 5/10: Limited pure 2.2 guard off; expect an explicit safe fallback to Full/sRGB on the legacy presenter" },
		{ L"6/10 VP limited/G22 proof", "direct", "limited", "2.2", false, true, true, false, false,
			L"Test 6/10: VP-owned Limited pure 2.2 proof; verify pixels/owner/DXGI/Present, then meter-grade black floor and curve" },
		{ L"7/10 VP limited/G24", "direct", "limited", "2.4", false, false, true, false, false,
			L"Test 7/10: VP-owned Limited pure 2.4 control; verify contract, then compare black floor against tests 4 and 6" },
		{ L"8/10 VP full/sRGB 8-bit", "direct", "full", "srgb", true, false, true, false, false,
			L"Test 8/10: VP-owned 8-bit Full/sRGB control; compare banding and levels with the 10-bit baseline" },
		{ L"9/10 composed full/sRGB", "composed", "full", "srgb", false, false, false, false, false,
			L"Test 9/10: Composed Full/sRGB control; expect libplacebo bitblt presentation and a successful Present" },
		{ L"10/10 composed VP request", "composed", "full", "srgb", false, false, true, false, false,
			L"Test 10/10: Request VP ownership in Composed mode; expect documented libplacebo bitblt ownership, Full/sRGB, and Present" },
	};
	}
	else
	{
		// The HDR suite first brackets the target-nits boundary, then repeats the
		// non-redundant output contracts which can affect black floor or transfer.
		// Target primaries and HDMI BT.2020 signaling are selected by the launcher
		// and preserved across every case in this run.
		m_activeOutputSweepCases = {
			{ L"1/17 HDR 100 nits", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 1/17: 100-nit target; VP-owned Full pure 2.2 anchor; meter-grade highlights, color, and black floor", "100", "auto", "auto", "auto", "auto" },
			{ L"2/17 HDR 200 nits", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 2/17: 200-nit target; reported safe-boundary comparison on the same Full pure 2.2 contract", "200", "auto", "auto", "auto", "auto" },
			{ L"3/17 HDR 250 nits", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 3/17: 250-nit target; first just-above-boundary color-crush check", "250", "auto", "auto", "auto", "auto" },
			{ L"4/17 HDR 300 nits", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 4/17: 300-nit target; Full pure 2.2 baseline for mapping controls", "300", "auto", "auto", "auto", "auto" },
			{ L"5/17 HDR 400 nits", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 5/17: 400-nit target; higher-target color and highlight stress comparison", "400", "auto", "auto", "auto", "auto" },
			{ L"6/17 HDR legacy full sRGB", "direct", "full", "srgb", false, false, false, false, false,
				L"HDR test 6/17: Legacy Full/sRGB baseline; compare curve and black floor with the VP-owned pure 2.2 anchor", "300" },
			{ L"7/17 HDR full G22 guard off", "direct", "full", "2.2", false, false, false, false, false,
				L"HDR test 7/17: Full pure 2.2 guard off; expect an explicit safe fallback to legacy Full/sRGB", "300" },
			{ L"8/17 HDR VP full G22", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 8/17: VP-owned Full pure 2.2 proof; verify metadata/Present, then measure the physical curve", "300" },
			{ L"9/17 HDR limited G22 guard off", "direct", "limited", "2.2", false, false, false, false, false,
				L"HDR test 9/17: Limited pure 2.2 guard off; expect an explicit safe fallback to legacy Full/sRGB", "300" },
			{ L"10/17 HDR VP limited G22", "direct", "limited", "2.2", false, true, true, false, false,
				L"HDR test 10/17: VP-owned Limited pure 2.2 proof; meter-grade black floor against Full pure 2.2", "300" },
			{ L"11/17 HDR VP limited G24", "direct", "limited", "2.4", false, false, true, false, false,
				L"HDR test 11/17: VP-owned Limited pure 2.4 control; compare lifted blacks against Limited pure 2.2", "300" },
			{ L"12/17 HDR composed full sRGB", "composed", "full", "srgb", false, false, false, false, false,
				L"HDR test 12/17: Composed Full/sRGB control; expect libplacebo bitblt presentation and measure fullscreen/windowed behavior", "300" },
			{ L"13/17 HDR BT2390", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 13/17: 300-nit Full pure 2.2; BT.2390 highlight roll-off", "300", "bt2390", "auto", "auto", "auto" },
			{ L"14/17 HDR Reinhard", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 14/17: 300-nit Full pure 2.2; Reinhard compression comparison", "300", "reinhard", "auto", "auto", "auto" },
			{ L"15/17 HDR softclip", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 15/17: 300-nit Full pure 2.2; soft-clip gamut-boundary comparison", "300", "auto", "softclip", "auto", "auto" },
			{ L"16/17 HDR peak off", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 16/17: 300-nit Full pure 2.2; peak detection disabled", "300", "auto", "auto", "off", "auto" },
			{ L"17/17 HDR recovery 0", "direct", "full", "2.2", false, false, true, false, false,
				L"HDR test 17/17: 300-nit Full pure 2.2; contrast recovery disabled", "300", "auto", "auto", "auto", "0.0" },
		};
	}
	if (!m_activeOutputSweepRequestedTests.IsEmpty())
	{
		std::vector<bool> selected(m_activeOutputSweepCases.size(), false);
		const std::wstring expression = m_activeOutputSweepRequestedTests.GetString();
		auto parseNumber = [](const std::wstring& value, size_t& number)
		{
			if (value.empty())
				return false;
			wchar_t* end = nullptr;
			const unsigned long parsed = wcstoul(value.c_str(), &end, 10);
			if (end == value.c_str() || *end != L'\0' || parsed == 0)
				return false;
			number = static_cast<size_t>(parsed);
			return true;
		};
		bool valid = true;
		size_t start = 0;
		while (valid && start <= expression.size())
		{
			const size_t separator = expression.find(L',', start);
			std::wstring token = expression.substr(start,
				separator == std::wstring::npos ? std::wstring::npos : separator - start);
			const size_t firstCharacter = token.find_first_not_of(L" \t");
			const size_t lastCharacter = token.find_last_not_of(L" \t");
			if (firstCharacter == std::wstring::npos)
			{
				valid = false;
				break;
			}
			token = token.substr(firstCharacter, lastCharacter - firstCharacter + 1);
			const size_t dash = token.find(L'-');
			if (dash != std::wstring::npos && token.find(L'-', dash + 1) != std::wstring::npos)
			{
				valid = false;
				break;
			}
			size_t first = 0;
			size_t last = 0;
			if (dash == std::wstring::npos)
			{
				valid = parseNumber(token, first);
				last = first;
			}
			else
			{
				valid = parseNumber(token.substr(0, dash), first) &&
					parseNumber(token.substr(dash + 1), last) && first <= last;
			}
			if (!valid || last > selected.size())
			{
				valid = false;
				break;
			}
			for (size_t number = first; number <= last; ++number)
				selected[number - 1] = true;
			if (separator == std::wstring::npos)
				break;
			start = separator + 1;
		}
		if (!valid || std::find(selected.begin(), selected.end(), true) == selected.end())
		{
			CString error;
			error.Format(L"refused: invalid test list '%s' (use 2,5 or 2-5)",
				m_activeOutputSweepRequestedTests.GetString());
			CompleteActiveOutputSweep(error);
			return false;
		}
		std::vector<ActiveOutputSweepCase> filtered;
		for (size_t index = 0; index < selected.size(); ++index)
		{
			if (selected[index])
				filtered.push_back(m_activeOutputSweepCases[index]);
		}
		m_activeOutputSweepCases = std::move(filtered);
	}
	m_activeOutputSweepCaseIndex = 0;
	m_activeOutputSweepRunning = true;
	m_activeOutputSweepPaused = false;
	// The live test owns a separate native top-right banner. Do not force the
	// user's normal diagnostic panel on merely to identify the active contract.
	DebugLog::Log(
		"Active output sweep start: config=%S fullscreen_monitor=%S cases=%zu hold_ms=%lu",
		configPath.c_str(), m_fullscreenMonitorName.GetString(),
		m_activeOutputSweepCases.size(), m_activeOutputSweepHoldMs);
	return ApplyActiveOutputSweepCase(0);
}

bool CVideoProcessorDlg::ApplyActiveOutputSweepCase(size_t index)
{
	if (!m_activeOutputSweepDocument || index >= m_activeOutputSweepCases.size())
		return false;
	const ActiveOutputSweepCase& test = m_activeOutputSweepCases[index];
	auto& document = *m_activeOutputSweepDocument;
	const bool hdrSuite = m_activeOutputSweepSuite.CompareNoCase(L"hdr") == 0;
	auto applyTestSettings = [&document, &test, hdrSuite](const std::string& section)
	{
		document.SetKnown(section, "output_path_profile", "custom");
		document.SetKnown(section, "output_presentation", test.presentation);
		document.SetKnown(section, "output_range", test.range);
		document.SetKnown(section, "output_gamma", test.gamma);
		document.SetKnown(section, "diagnostic_force_8bit_sdr_swapchain",
			test.force8Bit ? "true" : "false");
		document.SetKnown(section, "diagnostic_allow_limited_g22",
			test.allowLimitedG22 ? "true" : "false");
		document.SetKnown(section, "diagnostic_allow_full_g22",
			"false");
		document.SetKnown(section, "diagnostic_vp_owned_dxgi_presenter",
			test.vpOwnedPresenter ? "true" : "false");
		document.SetKnown(section, "diagnostic_disable_compute",
			test.disableCompute ? "true" : "false");
		document.SetKnown(section, "diagnostic_disable_shader_cache",
			test.disableShaderCache ? "true" : "false");
		if (hdrSuite)
		{
			if (test.sdrTargetNits)
				document.SetKnown(section, "sdr_target_nits", test.sdrTargetNits);
			if (test.toneMapping)
				document.SetKnown(section, "tone_mapping", test.toneMapping);
			if (test.gamutMapping)
				document.SetKnown(section, "gamut_mapping", test.gamutMapping);
			if (test.peakDetection)
				document.SetKnown(section, "peak_detection", test.peakDetection);
			if (test.contrastRecovery)
				document.SetKnown(section, "contrast_recovery", test.contrastRecovery);
			if (test.targetPrimaries)
				document.SetKnown(section, "sdr_target_primaries", test.targetPrimaries);
			if (test.reportBt2020ToDisplay)
				document.SetKnown(section, "report_bt2020_to_display",
					test.reportBt2020ToDisplay);
		}
		document.SetKnown(section, "output_diagnostics", "true");
	};
	for (const std::string& section : document.SectionNames())
	{
		const std::string normalized = ConfigFile::NormalizeName(section);
		if (normalized != "vprenderer" &&
			!(normalized.rfind("vprenderer.", 0) == 0 &&
				normalized.substr(strlen("vprenderer.")).find('.') == std::string::npos))
		{
			continue;
		}
		applyTestSettings(section);
	}
	// Ensure there is a base display section even for a minimal profile file.
	applyTestSettings("vprenderer");

	ConfigEditorCore::SaveResult saveResult;
	std::wstring error;
	if (!ConfigEditorCore::SaveSafely(document, saveResult, error))
	{
		CString status;
		status.Format(L"sweep save failed: %s", error.c_str());
		DebugLog::Log("Active output sweep failed: case=%zu detail=%S", index + 1,
			error.c_str());
		RestoreActiveOutputSweepConfiguration(status);
		return false;
	}
	m_activeOutputSweepCaseIndex = index;
	m_activeOutputSweepAwaitingLiveFrame = true;
	m_activeOutputSweepCaseFailed = false;
	m_activeOutputSweepBannerState = SweepBannerState::Testing;
	m_activeOutputSweepCaseResult = SweepResultState::Failed;
	m_activeOutputSweepCaseDetail.Empty();
	m_activeOutputSweepDeadlineTick = GetTickCount64() + 15000;
	if (hdrSuite)
	{
		const std::string targetPrimaries = document.Get("vprenderer",
			"sdr_target_primaries");
		const std::string reportBt2020 = document.Get("vprenderer",
			"report_bt2020_to_display");
		m_activeOutputSweepStatus.Format(
			L"%s\nTarget primaries: %S; BT.2020 InfoFrame: %S\nRestarting %s for this test",
			test.description, targetPrimaries.c_str(), reportBt2020.c_str(),
			m_activeOutputSweepCaptureRestart ? L"capture" : L"renderer");
	}
	else
	{
		m_activeOutputSweepStatus.Format(L"%s\nRestarting %s for this test",
			test.description,
			m_activeOutputSweepCaptureRestart ? L"capture" : L"renderer");
	}
	DebugLog::Log(
		"Active output sweep case: suite=%S index=%zu label=%S presentation=%s range=%s gamma=%s target_nits=%s tone_mapping=%s gamut_mapping=%s peak_detection=%s contrast_recovery=%s target_primaries=%s report_bt2020=%s force8=%d allow_full_g22=%d vp_owned=%d no_compute=%d no_shader_cache=%d action=%s-restart backup=%S",
		m_activeOutputSweepSuite.GetString(),
		index + 1, test.label, test.presentation, test.range, test.gamma,
		test.sdrTargetNits ? test.sdrTargetNits : "(unchanged)",
		test.toneMapping ? test.toneMapping : "(unchanged)",
		test.gamutMapping ? test.gamutMapping : "(unchanged)",
		test.peakDetection ? test.peakDetection : "(unchanged)",
		test.contrastRecovery ? test.contrastRecovery : "(unchanged)",
		test.targetPrimaries ? test.targetPrimaries : "(unchanged)",
		test.reportBt2020ToDisplay ? test.reportBt2020ToDisplay : "(unchanged)",
		test.force8Bit ? 1 : 0,
		(test.vpOwnedPresenter && strcmp(test.range, "full") == 0 &&
			strcmp(test.gamma, "2.2") == 0) ? 1 : 0,
		test.vpOwnedPresenter ? 1 : 0,
		test.disableCompute ? 1 : 0, test.disableShaderCache ? 1 : 0,
		m_activeOutputSweepCaptureRestart ? "capture" : "renderer",
		saveResult.backupPath.c_str());
	if (!ApplyActiveOutputSweepConfiguration())
	{
		RestoreActiveOutputSweepConfiguration(L"configuration application failed");
		return false;
	}
	UpdateStatsOverlay();
	return true;
}

bool CVideoProcessorDlg::ApplyActiveOutputSweepConfiguration()
{
	if (m_activeOutputSweepCaptureRestart)
	{
		ApplySavedConfiguration();
		return true;
	}
	if (!StageSavedConfiguration("active-output-sweep-renderer-restart", true))
	{
		DebugLog::Log("Active output sweep failed: renderer-only configuration stage rejected");
		return false;
	}
	// These options are constructed into the D3D11 device and swapchain. A
	// renderer rebuild is still necessary, but capture remains live. This is the
	// closest valid no-full-teardown experiment until the renderer exposes a
	// transactional live output-contract API.
	if (m_stagedConfigurationAction == ConfigurationApplyPolicy::Action::RestartCapture)
	{
		DebugLog::Log("Active output sweep override: policy=restart-capture action=restart-renderer reason=renderer-only experiment");
		m_stagedConfigurationAction = ConfigurationApplyPolicy::Action::RestartRenderer;
	}
	if (m_stagedConfigurationAction != ConfigurationApplyPolicy::Action::RestartRenderer)
	{
		DebugLog::Log("Active output sweep failed: renderer-only action=%s is not restart-renderer",
			ConfigurationApplyPolicy::ActionLabel(m_stagedConfigurationAction));
		return false;
	}
	m_postRendererStartRequiresGraph = true;
	m_wantToRestartRenderer = true;
	UpdateState();
	return true;
}

void CVideoProcessorDlg::RestoreActiveOutputSweepConfiguration(const wchar_t* reason)
{
	if (!m_activeOutputSweepDocument || !m_activeOutputSweepOriginalDocument)
	{
		CompleteActiveOutputSweep(reason);
		return;
	}
	auto restore = std::make_unique<ConfigEditorCore::ConfigDocument>(
		*m_activeOutputSweepDocument);
	// Preserve the current loaded bytes for SafeSave's external-edit guard, but
	// restore the original document byte-for-byte (including comments/order).
	restore->lines = m_activeOutputSweepOriginalDocument->lines;
	restore->lineEnding = m_activeOutputSweepOriginalDocument->lineEnding;
	restore->hasTerminalLineEnding =
		m_activeOutputSweepOriginalDocument->hasTerminalLineEnding;
	ConfigEditorCore::SaveResult saveResult;
	std::wstring error;
	if (!ConfigEditorCore::SaveSafely(*restore, saveResult, error))
	{
		m_activeOutputSweepStatus.Format(
			L"restore failed - test config retained (%s)", error.c_str());
		DebugLog::Log("Active output sweep restore failed: detail=%S", error.c_str());
		CompleteActiveOutputSweep(m_activeOutputSweepStatus);
		return;
	}
	m_activeOutputSweepDocument = std::move(restore);
	m_activeOutputSweepRestorePending = true;
	m_activeOutputSweepAwaitingLiveFrame = false;
	m_activeOutputSweepCaseFailed = false;
	m_activeOutputSweepStatus.Format(L"%s - restoring test config", reason);
	DebugLog::Log("Active output sweep restore: result=%S backup=%S action=%s-restart",
		reason, saveResult.backupPath.c_str(),
		m_activeOutputSweepCaptureRestart ? "capture" : "renderer");
	if (!ApplyActiveOutputSweepConfiguration())
	{
		CompleteActiveOutputSweep(L"restore saved but renderer restart failed");
		return;
	}
	UpdateStatsOverlay();
}

void CVideoProcessorDlg::CompleteActiveOutputSweep(const wchar_t* result)
{
	m_activeOutputSweepRequested = false;
	m_activeOutputSweepRunning = false;
	m_activeOutputSweepAwaitingLiveFrame = false;
	m_activeOutputSweepPaused = false;
	m_activeOutputSweepCaseFailed = false;
	m_activeOutputSweepRestorePending = false;
	m_activeOutputSweepStatus = result;
	m_activeOutputSweepSummaryVisible = !m_activeOutputSweepResults.empty();
	m_activeOutputSweepSummaryStartedTick = m_activeOutputSweepSummaryVisible ?
		GetTickCount64() : 0;
	DebugLog::Log("Active output sweep complete: result=%S", result);
	if (m_activeOutputSweepSummaryVisible)
	{
		const size_t failed = static_cast<size_t>(std::count_if(
			m_activeOutputSweepResults.begin(), m_activeOutputSweepResults.end(),
			[](const SweepSummaryItem& item)
			{
				return item.state == SweepResultState::Failed;
			}));
		DebugLog::Log("Active output sweep summary retained: successful=%zu failed=%zu clear_on=renderer-reset-or-rebuild",
			m_activeOutputSweepResults.size() - failed, failed);
		UpdateStatsOverlay();
	}
	else if (m_videoRenderer)
		m_videoRenderer->SetNativeSweepOverlay(nullptr, 0, 0, 0, 0);
}

void CVideoProcessorDlg::RecordActiveOutputSweepResult(SweepResultState state,
	const wchar_t* detail)
{
	if (m_activeOutputSweepCaseIndex >= m_activeOutputSweepCases.size() ||
		m_activeOutputSweepResults.size() > m_activeOutputSweepCaseIndex)
		return;
	SweepSummaryItem item;
	item.label = m_activeOutputSweepCases[m_activeOutputSweepCaseIndex].description;
	item.detail = detail;
	item.state = state;
	m_activeOutputSweepResults.push_back(std::move(item));
}

bool CVideoProcessorDlg::EvaluateActiveOutputSweepCase(
	SweepResultState& state, CString& detail) const
{
	using namespace ActiveOutputSweepPolicy;
	using namespace RendererOutputContract;
	state = SweepResultState::Failed;
	detail.Empty();
	if (!m_videoRenderer ||
		m_activeOutputSweepCaseIndex >= m_activeOutputSweepCases.size())
	{
		detail = L"renderer or active test case is unavailable";
		return false;
	}

	const ActiveOutputSweepCase& test =
		m_activeOutputSweepCases[m_activeOutputSweepCaseIndex];
	Status actual;
	if (!m_videoRenderer->GetOutputContractStatus(actual))
	{
		detail = L"renderer did not expose structured output-contract state";
		return false;
	}

	Expected expected;
	const bool direct = strcmp(test.presentation, "direct") == 0;
	const bool composed = strcmp(test.presentation, "composed") == 0;
	const bool limited = strcmp(test.range, "limited") == 0;
	const bool gamma22 = strcmp(test.gamma, "2.2") == 0;
	const bool gamma24 = strcmp(test.gamma, "2.4") == 0;
	const bool limitedGammaSupported = strcmp(test.gamma, "auto") == 0 ||
		gamma24 || (gamma22 && test.allowLimitedG22);
	const bool expectedFallback = limited && !limitedGammaSupported;
	expected.disposition = expectedFallback ? Disposition::FALLBACK :
		Disposition::EXACT;
	expected.presentation = composed ? Presentation::BITBLT :
		direct ? Presentation::FLIP : Presentation::UNKNOWN;
	expected.range = expectedFallback ? Range::FULL :
		limited ? Range::LIMITED : Range::FULL;
	const auto configuredTransfer = [gamma = std::string(test.gamma)]()
	{
		if (gamma == "bt1886") return Transfer::BT1886;
		if (gamma == "1.8") return Transfer::GAMMA18;
		if (gamma == "2.0") return Transfer::GAMMA20;
		if (gamma == "2.2") return Transfer::GAMMA22;
		if (gamma == "2.4") return Transfer::GAMMA24;
		if (gamma == "2.6") return Transfer::GAMMA26;
		if (gamma == "2.8") return Transfer::GAMMA28;
		return Transfer::SRGB;
	};
	expected.transfer = expectedFallback ? Transfer::SRGB :
		(limited && strcmp(test.gamma, "auto") == 0) ?
		Transfer::GAMMA24 : configuredTransfer();
	expected.requireVpOwner = test.vpOwnedPresenter && direct;
	expected.requireDxgiVerification = expected.requireVpOwner;
	expected.swapchainBitDepth = expected.requireVpOwner ?
		(test.force8Bit ? 8u : 10u) : 0u;
	const std::string configuredPrimaries = m_activeOutputSweepDocument ?
		m_activeOutputSweepDocument->Get("vprenderer", "sdr_target_primaries") :
		std::string();
	expected.primaries = ConfigFile::NormalizeName(configuredPrimaries) == "bt2020" ?
		Primaries::BT2020 : Primaries::REC709;
	const bool hdrSuite = m_activeOutputSweepSuite.CompareNoCase(L"hdr") == 0;
	expected.measurementRequired = !expectedFallback &&
		(hdrSuite || strcmp(test.gamma, "auto") != 0 || limited || test.force8Bit);

	const Decision decision = Evaluate(expected, actual);
	state = decision.verdict == Verdict::PASS ? SweepResultState::Passed :
		decision.verdict == Verdict::EXPECTED ? SweepResultState::Expected :
		decision.verdict == Verdict::MEASURE ? SweepResultState::Measure :
		SweepResultState::Failed;
	const wchar_t* range = actual.range == Range::FULL ? L"Full" :
		actual.range == Range::LIMITED ? L"Limited" : L"Unknown";
	const wchar_t* transfer = actual.transfer == Transfer::BT1886 ? L"BT.1886" :
		actual.transfer == Transfer::GAMMA18 ? L"Pure1.8" :
		actual.transfer == Transfer::GAMMA20 ? L"Pure2.0" :
		actual.transfer == Transfer::GAMMA22 ? L"Pure2.2" :
		actual.transfer == Transfer::GAMMA24 ? L"Pure2.4" :
		actual.transfer == Transfer::GAMMA26 ? L"Pure2.6" :
		actual.transfer == Transfer::GAMMA28 ? L"Pure2.8" :
		actual.transfer == Transfer::SRGB ? L"sRGB" : L"Unknown";
	const wchar_t* owner = actual.vpOwnsPresentation ? L"VP" : L"libplacebo";
	const wchar_t* primaries = actual.primaries == Primaries::BT2020 ?
		L"BT.2020" : actual.primaries == Primaries::REC709 ? L"Rec.709" : L"Unknown";
	const wchar_t* content = actual.rendererContent ==
		RendererContentEvidence::NONBLACK ? L"nonblack" :
		actual.rendererContent == RendererContentEvidence::ALL_BLACK ? L"black" :
		L"unverified";
	const wchar_t* delivery = actual.displayDelivery ==
		DisplayDeliveryEvidence::PRESENTED ? L"presented" :
		actual.displayDelivery == DisplayDeliveryEvidence::SUBMITTED ? L"submitted-only" :
		L"unverified";
	detail.Format(L"%S; actual=%s/%s/%s/%ubit owner=%s accepted=%d verified=%d submissions=%llu rendered=%s display_delivery=%s format=%S DXGI=%S",
		decision.reason.c_str(), range, transfer, primaries,
		actual.swapchainBitDepth, owner,
		actual.requestedContractActive ? 1 : 0,
		actual.dxgiAppliedVerified ? 1 : 0,
		static_cast<unsigned long long>(actual.successfulPresents), content, delivery,
		actual.swapchainFormat.empty() ? "(none)" :
			actual.swapchainFormat.c_str(),
		actual.dxgiDeclaration.empty() ? "(none)" :
			actual.dxgiDeclaration.c_str());
	return decision.verdict != Verdict::WAITING;
}

bool CVideoProcessorDlg::TryClassifyActiveOutputSweepCase(
	ULONGLONG now, const char* trigger)
{
	if (!m_activeOutputSweepRunning || !m_activeOutputSweepAwaitingLiveFrame ||
		m_activeOutputSweepCaseIndex >= m_activeOutputSweepCases.size())
	{
		return false;
	}

	SweepResultState state = SweepResultState::Failed;
	CString detail;
	if (!EvaluateActiveOutputSweepCase(state, detail))
		return false;

	m_activeOutputSweepAwaitingLiveFrame = false;
	m_activeOutputSweepCaseResult = state;
	m_activeOutputSweepCaseDetail = detail;
	m_activeOutputSweepCaseFailed = state == SweepResultState::Failed;
	m_activeOutputSweepBannerState = state == SweepResultState::Passed ?
		SweepBannerState::Passed : state == SweepResultState::Expected ?
		SweepBannerState::Expected : state == SweepResultState::Measure ?
		SweepBannerState::Measure : SweepBannerState::Failed;
	m_activeOutputSweepDeadlineTick = now +
		(m_activeOutputSweepCaseFailed ?
			(std::min<DWORD>)(m_activeOutputSweepHoldMs, 5000) :
			m_activeOutputSweepHoldMs);
	const wchar_t* label = state == SweepResultState::Passed ? L"PASS" :
		state == SweepResultState::Expected ? L"EXPECTED" :
		state == SweepResultState::Measure ? L"MEASURE" : L"FAIL";
	m_activeOutputSweepStatus.Format(L"%s\n%s: %s; holding %lus",
		m_activeOutputSweepCases[m_activeOutputSweepCaseIndex].description,
		label, detail.GetString(),
		(m_activeOutputSweepDeadlineTick - now) / 1000);
	DebugLog::Log(
		"Active output sweep live classification: index=%zu state=%d trigger=%s hold_ms=%llu detail=%S",
		m_activeOutputSweepCaseIndex + 1, static_cast<int>(state), trigger,
		m_activeOutputSweepDeadlineTick - now, detail.GetString());
	UpdateStatsOverlay();
	return true;
}

void CVideoProcessorDlg::ToggleActiveOutputSweepPause()
{
	if (!m_activeOutputSweepRunning || m_activeOutputSweepRestorePending)
		return;

	const ULONGLONG now = GetTickCount64();
	m_activeOutputSweepPaused = !m_activeOutputSweepPaused;
	if (m_activeOutputSweepPaused)
	{
		if (m_videoRenderer)
			m_videoRenderer->SetNativeSweepOverlay(nullptr, 0, 0, 0, 0);
		DebugLog::Log(
			"Active output sweep measurement pause: index=%zu paused=1 overlay=hidden",
			m_activeOutputSweepCaseIndex + 1);
		return;
	}

	// Resuming is an intentional fresh observation interval. If output is
	// still settling, restart the bounded initialization wait; otherwise give
	// the tester the complete configured hold time again.
	m_activeOutputSweepDeadlineTick = now +
		(m_activeOutputSweepAwaitingLiveFrame ? 15000 :
			m_activeOutputSweepHoldMs);
	DebugLog::Log(
		"Active output sweep measurement pause: index=%zu paused=0 overlay=restored countdown_restarted_ms=%llu awaiting_evidence=%d",
		m_activeOutputSweepCaseIndex + 1,
		m_activeOutputSweepDeadlineTick - now,
		m_activeOutputSweepAwaitingLiveFrame ? 1 : 0);
	UpdateStatsOverlay();
}

void CVideoProcessorDlg::ClearActiveOutputSweepSummary(const char* reason)
{
	if (!m_activeOutputSweepSummaryVisible)
		return;
	m_activeOutputSweepSummaryVisible = false;
	m_activeOutputSweepSummaryStartedTick = 0;
	m_activeOutputSweepResults.clear();
	DebugLog::Log("Active output sweep summary cleared: reason=%s", reason);
	if (m_videoRenderer)
		m_videoRenderer->SetNativeSweepOverlay(nullptr, 0, 0, 0, 0);
}

void CVideoProcessorDlg::UpdateActiveOutputSweep(ULONGLONG now)
{
	if (m_activeOutputSweepRequested && !m_activeOutputSweepRunning)
	{
		if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
			m_videoRenderer)
			StartActiveOutputSweep();
		return;
	}
	if (!m_activeOutputSweepRunning)
		return;
	if (m_activeOutputSweepRestorePending)
	{
		if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
			m_videoRenderer)
			CompleteActiveOutputSweep(L"complete - original test config restored");
		return;
	}
	if (m_activeOutputSweepPaused)
		return;
	// The first live-frame message normally arrives before DXGI frame
	// statistics have recovered from the expected post-recreate DISJOINT
	// state. Keep polling the structured contract during the settle window so
	// later presented-frame evidence can classify the case.
	if (TryClassifyActiveOutputSweepCase(now, "settle-poll"))
		return;
	if (now < m_activeOutputSweepDeadlineTick)
		return;
	if (m_activeOutputSweepAwaitingLiveFrame)
	{
		DebugLog::Log("Active output sweep case timed out: index=%zu reason=no-live-frame timeout_ms=15000",
			m_activeOutputSweepCaseIndex + 1);
		m_activeOutputSweepAwaitingLiveFrame = false;
		m_activeOutputSweepCaseFailed = true;
		m_activeOutputSweepBannerState = SweepBannerState::Failed;
		m_activeOutputSweepCaseResult = SweepResultState::Failed;
		SweepResultState classifiedState = SweepResultState::Failed;
		CString classifiedDetail;
		if (EvaluateActiveOutputSweepCase(classifiedState, classifiedDetail))
			m_activeOutputSweepCaseDetail = classifiedDetail;
		else
			m_activeOutputSweepCaseDetail = L"no live frame after 15 seconds";
		const ULONGLONG failureHoldMs = m_activeOutputSweepHoldMs < 5000 ?
			m_activeOutputSweepHoldMs : 5000;
		m_activeOutputSweepDeadlineTick = now + failureHoldMs;
		m_activeOutputSweepStatus.Format(L"%s\nFAIL: %s; holding failure",
			m_activeOutputSweepCases[m_activeOutputSweepCaseIndex].description,
			m_activeOutputSweepCaseDetail.GetString());
		DebugLog::Log("Active output sweep case failure hold: index=%zu hold_ms=%llu",
			m_activeOutputSweepCaseIndex + 1,
			m_activeOutputSweepDeadlineTick - now);
		UpdateStatsOverlay();
		return;
	}
	if (m_activeOutputSweepCaseFailed)
	{
		RecordActiveOutputSweepResult(SweepResultState::Failed,
			m_activeOutputSweepCaseDetail.GetString());
		DebugLog::Log("Active output sweep case failed after visible hold: index=%zu",
			m_activeOutputSweepCaseIndex + 1);
	}
	else
	{
		SweepResultState state = SweepResultState::Failed;
		CString detail;
		if (!EvaluateActiveOutputSweepCase(state, detail))
		{
			state = SweepResultState::Failed;
			detail = L"output metadata did not settle before the hold completed";
		}
		RecordActiveOutputSweepResult(state, detail.GetString());
		const char* verdict = state == SweepResultState::Passed ? "PASS" :
			state == SweepResultState::Expected ? "EXPECTED" :
			state == SweepResultState::Measure ? "MEASURE" : "FAIL";
		DebugLog::Log("Active output sweep assertion: index=%zu verdict=%s hold_ms=%lu detail=%S",
			m_activeOutputSweepCaseIndex + 1, verdict,
			m_activeOutputSweepHoldMs, detail.GetString());
	}
	const size_t next = m_activeOutputSweepCaseIndex + 1;
	if (next < m_activeOutputSweepCases.size())
		ApplyActiveOutputSweepCase(next);
	else
		RestoreActiveOutputSweepConfiguration(L"all cases complete");
}

bool CVideoProcessorDlg::StageSavedConfiguration(
	const char* reason, bool stageAccelerators)
{
	ClearStagedConfiguration();
	std::unique_ptr<ConfigFile> candidate;
	ConfigurationSnapshot candidateSnapshot;
	for (unsigned int attempt = 1; attempt <= 3; ++attempt)
	{
		auto first = std::make_unique<ConfigFile>();
		if (!first->Load())
		{
			DebugLog::Log(
				"Configuration reload rejected: reason=%s attempt=%u failure=load",
				reason, attempt);
			continue;
		}
		const ConfigurationSnapshot firstSnapshot =
			CaptureConfigurationSnapshot(*first);
		auto second = std::make_unique<ConfigFile>();
		if (!second->Load() ||
			CaptureConfigurationSnapshot(*second) != firstSnapshot)
		{
			DebugLog::Log(
				"Configuration reload retry: reason=%s attempt=%u failure=unstable-file",
				reason, attempt);
			continue;
		}
		candidateSnapshot = firstSnapshot;
		candidate = std::move(second);
		break;
	}
	if (!candidate)
	{
		DebugLog::Log(
			"Configuration reload retained last-known-good: reason=%s failure=stable-snapshot-unavailable",
			reason);
		return false;
	}

	std::string error;
	if (!MainConfigSchema::Validate(*candidate, error))
	{
		DebugLog::Log(
			"Configuration reload retained last-known-good: reason=%s failure=validation detail=%s",
			reason, error.c_str());
		return false;
	}
	if (!StageRuntimeSettings(*candidate, error))
	{
		DebugLog::Log(
			"Configuration reload retained last-known-good: reason=%s failure=runtime-settings detail=%s",
			reason, error.c_str());
		return false;
	}
	// Resolve against current source facts in an isolated runtime. This proves
	// profile discovery and rule resolution before the live runtime is touched.
	UnifiedProfileRuntime::Runtime candidateRuntime;
	if (!candidateRuntime.Initialize(*candidate,
		GetUnifiedProfileSourceLookup(), error))
	{
		DebugLog::Log(
			"Configuration reload retained last-known-good: reason=%s failure=profile-resolution detail=%s",
			reason, error.c_str());
		return false;
	}

	const std::vector<std::string> changed =
		ChangedConfigurationSections(m_configurationSnapshot, candidateSnapshot);
	const std::vector<ConfigurationApplyPolicy::Change> changedValues =
		ChangedConfigurationValues(m_configurationSnapshot, candidateSnapshot);
	m_stagedEditorApply = strcmp(reason, "editor-apply") == 0;
	m_stagedRendererChanged = std::any_of(changedValues.begin(),
		changedValues.end(), [](const ConfigurationApplyPolicy::Change& change)
		{
			const std::string section = ConfigFile::NormalizeName(change.section);
			return (section == "general" || section == "command_line") &&
				ConfigFile::NormalizeName(change.key) == "renderer";
		});
	const bool directShowActive = m_videoRenderer &&
		m_activeRendererIsDirectShow;
	m_stagedConfigurationAction =
		ConfigurationApplyPolicy::ClassifyChanges(changedValues,
			directShowActive);
	const bool fullscreenMonitorSelectionChanged = std::any_of(
		changedValues.begin(), changedValues.end(),
		ConfigurationApplyPolicy::IsFullscreenMonitorSelectionChange);
	if (fullscreenMonitorSelectionChanged)
	{
		m_stagedRuntimeSettings.hasFullscreenMonitorName = true;
		std::string fullscreenMonitorName;
		if (candidate->TryGetString("general", "fullscreen_monitor_name",
			fullscreenMonitorName) ||
			candidate->TryGetString("command_line", "fullscreen_monitor_name",
				fullscreenMonitorName))
		{
			m_stagedRuntimeSettings.fullscreenMonitorName =
				fullscreenMonitorName.c_str();
		}
	}
	m_stagedShortcutsChanged = std::any_of(changedValues.begin(),
		changedValues.end(), ConfigurationApplyPolicy::IsShortcutAffectingChange);
	stageAccelerators = stageAccelerators ||
		ConfigurationLiveApply::ShouldStageShortcutTable(
			m_stagedShortcutsChanged,
			m_stagedConfigurationAction ==
				ConfigurationApplyPolicy::Action::RestartRenderer);
	if (stageAccelerators)
	{
		ConfigFile rendererConfig;
		const ConfigFile* rendererConfigPtr = nullptr;
		for (unsigned int attempt = 1; attempt <= 3; ++attempt)
		{
			ConfigFile firstRendererConfig;
			if (!firstRendererConfig.Load(ConfigFile::RENDERER_FILENAME))
				break; // Optional Alpha renderer override is absent.
			ConfigFile secondRendererConfig;
			if (secondRendererConfig.Load(ConfigFile::RENDERER_FILENAME) &&
				CaptureConfigurationSnapshot(firstRendererConfig) ==
					CaptureConfigurationSnapshot(secondRendererConfig))
			{
				rendererConfig = std::move(secondRendererConfig);
				rendererConfigPtr = &rendererConfig;
				break;
			}
			if (attempt == 3)
			{
				DebugLog::Log(
					"Configuration reload retained last-known-good: reason=%s failure=unstable-renderer-config",
					reason);
				ClearStagedConfiguration();
				return false;
			}
		}
		if (rendererConfigPtr)
		{
			RendererProfileConfig::Model rendererModel;
			if (!DisplayRuleExpression::ValidateConfig(*rendererConfigPtr,
				error) ||
				!RendererProfileConfig::Read(*rendererConfigPtr,
					rendererModel, error))
			{
				DebugLog::Log(
					"Configuration reload retained last-known-good: reason=%s failure=renderer-config-validation detail=%s",
					reason, error.c_str());
				ClearStagedConfiguration();
				return false;
			}
		}
		m_stagedAccelerator = CreateConfiguredAccelerators(
			m_stagedShaderShortcutRules, m_stagedShaderShortcutKeys,
			m_stagedDisplayRuleShortcutRules,
			m_stagedRendererShortcutIndices,
			m_stagedUnifiedProfileShortcutKeys,
			m_stagedConfiguredAccelerators, candidate.get(),
			rendererConfigPtr, true, &error);
		if (!m_stagedAccelerator)
		{
			DebugLog::Log(
				"Configuration reload retained last-known-good: reason=%s failure=accelerator-build detail=%s",
				reason, error.empty() ? "CreateAcceleratorTable failed" :
					error.c_str());
			ClearStagedConfiguration();
			return false;
		}
	}

	uint64_t fingerprint = 1469598103934665603ull;
	for (const auto& section : candidateSnapshot)
	{
		for (const unsigned char value : section.first)
			fingerprint = (fingerprint ^ value) * 1099511628211ull;
		for (const auto& setting : section.second)
		{
			for (const unsigned char value : setting.first)
				fingerprint = (fingerprint ^ value) * 1099511628211ull;
			for (const unsigned char value : setting.second)
				fingerprint = (fingerprint ^ value) * 1099511628211ull;
		}
	}
	std::ostringstream identity;
	identity << candidate->GetLoadedPath() << "#" << std::hex << fingerprint;
	m_stagedConfigurationIdentity = identity.str();
	m_stagedConfiguration = std::move(candidate);
	DebugLog::Log(
		"Configuration reload staged: reason=%s identity=%s action=%s categories=%s directshow_active=%d shortcuts=%s shortcut_changes=%d editor_apply=%d renderer_changed=%d session_renderer=%S accepted_renderer=%S",
		reason, m_stagedConfigurationIdentity.c_str(),
		ConfigurationApplyPolicy::ActionLabel(m_stagedConfigurationAction),
		changed.empty() ? "(none)" : JoinConfigurationSections(changed).c_str(),
		directShowActive ? 1 : 0,
		m_stagedAccelerator ? "staged" : "retained",
		m_stagedShortcutsChanged ? 1 : 0,
		m_stagedEditorApply ? 1 : 0,
		m_stagedRendererChanged ? 1 : 0,
		m_sessionRendererOverride.IsEmpty() ? L"(none)" :
			m_sessionRendererOverride.GetString(),
		m_acceptedRendererName.IsEmpty() ? L"(none)" :
			m_acceptedRendererName.GetString());
	return true;
}

bool CVideoProcessorDlg::StageRuntimeSettings(
	const ConfigFile& config, std::string& error)
{
	m_stagedRuntimeSettings = StagedRuntimeSettings();
	auto getApplicationValue = [&config](const char* key,
		std::string& value)
	{
		return config.TryGetString("general", key, value) ||
			config.TryGetString("command_line", key, value);
	};
	auto getDirectShowValue = [&config, &getApplicationValue](const char* key,
		std::string& value)
	{
		return config.TryGetString("directshow", key, value) ||
			getApplicationValue(key, value);
	};
	auto getBackendInputValue = [&config, &getApplicationValue](bool vpRenderer,
		const char* key, std::string& value)
	{
		const char* section = vpRenderer ? "vprenderer.input_processing" : "directshow";
		if (config.TryGetString(section, key, value)) return true;
		if (vpRenderer && config.TryGetString("vprenderer.input", key, value))
			return true;
		// VP-0123 initially wrote the renderer override into the display-profile
		// root. Keep that spelling readable while new saves use the independent
		// input-policy root, which profile rename/remove operations cannot touch.
		if (vpRenderer && config.TryGetString("vprenderer", key, value))
			return true;
		if (getApplicationValue(key, value)) return true;
		// Before VP-0123, [directshow] was a shared compatibility location.
		// Retain it only as VP Renderer's final fallback for old unsaved files.
		return vpRenderer && config.TryGetString("directshow", key, value);
	};
	auto invalid = [&error](const char* key, const std::string& value)
	{
		error = std::string("unsupported value for ") + key + ": " + value;
		return false;
	};
	std::string value;
	if (!ReadShortcutsForegroundOnly(config,
		m_stagedRuntimeSettings.shortcutsForegroundOnly, error))
		return false;
	if (getApplicationValue("capture_device", value))
	{
		m_stagedRuntimeSettings.hasCaptureDevice = true;
		m_stagedRuntimeSettings.captureDevice = CString(value.c_str());
		bool discovered = false;
		for (int index = 0; index < m_captureDeviceCombo.GetCount(); ++index)
		{
			CString name;
			m_captureDeviceCombo.GetLBText(index, name);
			if (name.CompareNoCase(m_stagedRuntimeSettings.captureDevice) == 0)
			{
				discovered = true;
				break;
			}
		}
		if (!discovered)
			return invalid("capture_device (not discovered)", value);
	}
	if (getApplicationValue("capture_input", value))
	{
		m_stagedRuntimeSettings.hasCaptureInput = true;
		m_stagedRuntimeSettings.captureInput = CString(value.c_str());
	}
	if (getApplicationValue("renderer", value))
	{
		m_stagedRuntimeSettings.hasRenderer = true;
		m_stagedRuntimeSettings.renderer = CString(value.c_str());
		bool discovered = false;
		for (int index = 0; index < m_rendererCombo.GetCount(); ++index)
		{
			const RendererId* renderer = reinterpret_cast<const RendererId*>(
				m_rendererCombo.GetItemData(index));
			if (renderer && renderer->MatchesConfiguredName(
				m_stagedRuntimeSettings.renderer))
			{
				discovered = true;
				break;
			}
		}
		if (!discovered)
			return invalid("renderer (not discovered)", value);
	}
	if (getDirectShowValue("frame_offset", value))
	{
		m_stagedRuntimeSettings.hasFrameOffset = true;
		const std::string normalized = ConfigFile::NormalizeName(value);
		m_stagedRuntimeSettings.frameOffsetAuto = normalized == "auto";
		if (!m_stagedRuntimeSettings.frameOffsetAuto)
		{
			try
			{
				size_t parsed = 0;
				const int offset = std::stoi(value, &parsed);
				if (parsed != value.size() || offset < 0)
					return invalid("frame_offset", value);
				m_stagedRuntimeSettings.frameOffsetMs = offset;
			}
			catch (const std::exception&)
			{
				return invalid("frame_offset", value);
			}
		}
	}
	auto stageVideoConversion = [&invalid](const std::string& raw,
		VideoConversionOverride& destination) -> bool
	{
		const std::string token = ConfigFile::NormalizeName(raw);
		if (token == "none" || token == "off")
			destination = VideoConversionOverride::VIDEOCONVERSION_NONE;
		else if (token == "v210_to_p010" || token == "uyvy_to_p010")
			destination = VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010;
		else return invalid("video_conversion", raw);
		return true;
	};
	if (getBackendInputValue(false, "video_conversion", value))
	{
		m_stagedRuntimeSettings.hasDirectShowVideoConversion = true;
		if (!stageVideoConversion(value,
			m_stagedRuntimeSettings.directShowVideoConversion)) return false;
	}
	if (getBackendInputValue(true, "video_conversion", value))
	{
		m_stagedRuntimeSettings.hasVpRendererVideoConversion = true;
		if (!stageVideoConversion(value,
			m_stagedRuntimeSettings.vpRendererVideoConversion)) return false;
	}
	auto stageContainerColorSpace = [&invalid](const std::string& raw,
		ColorSpace& destination) -> bool
	{
		const std::map<std::string, ColorSpace> values = {
			{ "auto", ColorSpace::UNKNOWN }, { "follow_input", ColorSpace::UNKNOWN },
			{ "bt2020", ColorSpace::BT_2020 }, { "p3_d65", ColorSpace::P3_D65 },
			{ "p3_dci", ColorSpace::P3_DCI }, { "p3_d60", ColorSpace::P3_D60 },
			{ "rec709", ColorSpace::REC_709 },
			{ "rec601_525", ColorSpace::REC_601_525 },
			{ "rec601_625", ColorSpace::REC_601_625 } };
		const auto found = values.find(ConfigFile::NormalizeName(raw));
		if (found == values.end()) return invalid("container_colorspace", raw);
		destination = found->second;
		return true;
	};
	if (getBackendInputValue(false, "container_colorspace", value))
	{
		m_stagedRuntimeSettings.hasDirectShowContainerColorSpace = true;
		if (!stageContainerColorSpace(value,
			m_stagedRuntimeSettings.directShowContainerColorSpace)) return false;
	}
	if (getBackendInputValue(true, "container_colorspace", value))
	{
		m_stagedRuntimeSettings.hasVpRendererContainerColorSpace = true;
		if (!stageContainerColorSpace(value,
			m_stagedRuntimeSettings.vpRendererContainerColorSpace)) return false;
	}
	auto stageHdrColorSpace = [&invalid](const std::string& raw,
		HdrColorspaceOptions& destination) -> bool
	{
		const std::map<std::string, HdrColorspaceOptions> values = {
			{ "follow_input", HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT },
			{ "follow_input_lldv", HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT_LLDV },
			{ "follow_container", HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_CONTAINER },
			{ "bt2020", HdrColorspaceOptions::HDR_COLORSPACE_BT2020 },
			{ "p3", HdrColorspaceOptions::HDR_COLORSPACE_P3 },
			{ "rec709", HdrColorspaceOptions::HDR_COLORSPACE_REC709 } };
		const auto found = values.find(ConfigFile::NormalizeName(raw));
		if (found == values.end()) return invalid("hdr_colorspace", raw);
		destination = found->second;
		return true;
	};
	if (getBackendInputValue(false, "hdr_colorspace", value))
	{
		m_stagedRuntimeSettings.hasDirectShowHdrColorSpace = true;
		if (!stageHdrColorSpace(value,
			m_stagedRuntimeSettings.directShowHdrColorSpace)) return false;
	}
	if (getBackendInputValue(true, "hdr_colorspace", value))
	{
		m_stagedRuntimeSettings.hasVpRendererHdrColorSpace = true;
		if (!stageHdrColorSpace(value,
			m_stagedRuntimeSettings.vpRendererHdrColorSpace)) return false;
	}
	auto stageHdrLuminance = [&invalid](const std::string& raw,
		HdrLuminanceOptions& destination) -> bool
	{
		const std::map<std::string, HdrLuminanceOptions> values = {
			{ "follow_input", HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT },
			{ "follow_input_lldv", HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT_LLDV },
			{ "hdr_luminance_user", HdrLuminanceOptions::HDR_LUMINANCE_USER },
			{ "user", HdrLuminanceOptions::HDR_LUMINANCE_USER } };
		const auto found = values.find(ConfigFile::NormalizeName(raw));
		if (found == values.end()) return invalid("hdr_luminance", raw);
		destination = found->second;
		return true;
	};
	if (getBackendInputValue(false, "hdr_luminance", value))
	{
		m_stagedRuntimeSettings.hasDirectShowHdrLuminance = true;
		if (!stageHdrLuminance(value,
			m_stagedRuntimeSettings.directShowHdrLuminance)) return false;
	}
	if (getBackendInputValue(true, "hdr_luminance", value))
	{
		m_stagedRuntimeSettings.hasVpRendererHdrLuminance = true;
		if (!stageHdrLuminance(value,
			m_stagedRuntimeSettings.vpRendererHdrLuminance)) return false;
	}
	if (getDirectShowValue("renderer_start_stop_time_method", value))
	{
		const std::string token = ConfigFile::NormalizeName(value);
		const std::map<std::string, DirectShowStartStopTimeMethod> values = {
			{ "clock_smart", DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART },
			{ "clock_smart2", DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2 },
			{ "clock_theo", DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO },
			{ "clock_clock", DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK },
			{ "theo_theo", DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO },
			{ "rational_rational", DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL },
			{ "clock_rational", DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL },
			{ "clock_none", DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE },
			{ "theo_none", DirectShowStartStopTimeMethod::DS_SSTM_THEO_NONE },
			{ "none", DirectShowStartStopTimeMethod::DS_SSTM_NONE } };
		const auto found = values.find(token);
		if (found == values.end()) return invalid("renderer_start_stop_time_method", value);
		m_stagedRuntimeSettings.hasDirectShowTimeMethod = true;
		m_stagedRuntimeSettings.directShowTimeMethod = found->second;
	}
	if (getDirectShowValue("renderer_nominal_range", value))
	{
		const std::string token = ConfigFile::NormalizeName(value);
		const std::map<std::string, DXVA_NominalRange> values = {
			{ "auto", DXVA_NominalRange::DXVA_NominalRange_Unknown },
			{ "full", DXVA_NominalRange::DXVA_NominalRange_0_255 },
			{ "limited", DXVA_NominalRange::DXVA_NominalRange_16_235 },
			{ "small", DXVA_NominalRange::DXVA_NominalRange_48_208 } };
		const auto found = values.find(token);
		if (found == values.end()) return invalid("renderer_nominal_range", value);
		m_stagedRuntimeSettings.hasNominalRange = true;
		m_stagedRuntimeSettings.nominalRange = found->second;
	}
	if (getDirectShowValue("renderer_transfer_function", value))
	{
		const std::string token = ConfigFile::NormalizeName(value);
		const std::map<std::string, DXVA_VideoTransferFunction> values = {
			{ "auto", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_Unknown },
			{ "pq", DIRECTSHOW_VIDEOTRANSFUNC_2084 },
			{ "rec709", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_709 },
			{ "bt2020_const", DIRECTSHOW_VIDEOTRANSFUNC_2020_const },
			{ "gamma_1.8", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_18 },
			{ "gamma_2.0", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_20 },
			{ "gamma_2.2", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22 },
			{ "gamma_2.6", DIRECTSHOW_VIDEOTRANSFUNC_26 },
			{ "gamma_2.8", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_28 },
			{ "linear_rgb", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_10 },
			{ "204m", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_240M },
			{ "8bit_gamma_2.2", DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_8bit_sRGB },
			{ "log_100_1", DIRECTSHOW_VIDEOTRANSFUNC_Log_100 },
			{ "log_316_1", DIRECTSHOW_VIDEOTRANSFUNC_Log_316 },
			{ "bt2020", DIRECTSHOW_VIDEOTRANSFUNC_2020 },
			{ "hybrid_log_gamma", DIRECTSHOW_VIDEOTRANSFUNC_HLG } };
		const auto found = values.find(token);
		if (found == values.end()) return invalid("renderer_transfer_function", value);
		m_stagedRuntimeSettings.hasTransferFunction = true;
		m_stagedRuntimeSettings.transferFunction = found->second;
	}
	if (getDirectShowValue("renderer_transfer_matrix", value))
	{
		const std::string token = ConfigFile::NormalizeName(value);
		const std::map<std::string, DXVA_VideoTransferMatrix> values = {
			{ "auto", DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_Unknown },
			{ "bt2020_10", DIRECTSHOW_VIDEOTRANSFERMATRIX_BT2020_10 },
			{ "bt2020_12", DIRECTSHOW_VIDEOTRANSFERMATRIX_BT2020_12 },
			{ "bt709", DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_BT709 },
			{ "bt601", DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_BT601 },
			{ "240m", DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_SMPTE240M },
			{ "fcc", DIRECTSHOW_VIDEOTRANSFERMATRIX_FCC },
			{ "ycgco", DIRECTSHOW_VIDEOTRANSFERMATRIX_YCgCo } };
		const auto found = values.find(token);
		if (found == values.end()) return invalid("renderer_transfer_matrix", value);
		m_stagedRuntimeSettings.hasTransferMatrix = true;
		m_stagedRuntimeSettings.transferMatrix = found->second;
	}
	if (getDirectShowValue("renderer_primaries", value))
	{
		const std::string token = ConfigFile::NormalizeName(value);
		const std::map<std::string, DXVA_VideoPrimaries> values = {
			{ "auto", DXVA_VideoPrimaries::DXVA_VideoPrimaries_Unknown },
			{ "bt2020", DIRECTSHOW_VIDEOPRIMARIES_BT2020 },
			{ "dci-p3", DIRECTSHOW_VIDEOPRIMARIES_DCI_P3 },
			{ "bt709", DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT709 },
			{ "ntsc_sysm", DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT470_2_SysM },
			{ "ntsc_sysbg", DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT470_2_SysBG },
			{ "cie1931_zyx", DIRECTSHOW_VIDEOPRIMARIES_XYZ },
			{ "aces", DIRECTSHOW_VIDEOPRIMARIES_ACES } };
		const auto found = values.find(token);
		if (found == values.end()) return invalid("renderer_primaries", value);
		m_stagedRuntimeSettings.hasPrimaries = true;
		m_stagedRuntimeSettings.primaries = found->second;
	}
	for (const char* section : { "general", "command_line" })
	{
		if (!config.TryGetString(section, "scene_detect", value) &&
			!config.TryGetString(section, "scene", value))
			continue;
		bool enabled = false;
		if (!config.TryGetBool(section, config.TryGetString(section,
			"scene_detect", value) ? "scene_detect" : "scene", enabled))
			return invalid("scene_detect", value);
		m_stagedRuntimeSettings.hasSceneDetect = true;
		m_stagedRuntimeSettings.sceneDetect = enabled;
		break;
	}
	return true;
}

void CVideoProcessorDlg::PublishStagedFullscreenMonitorSelection()
{
	if (!m_stagedRuntimeSettings.hasFullscreenMonitorName)
		return;
	const CString previousSelection = m_fullscreenMonitorName;
	FullscreenMonitorName(m_stagedRuntimeSettings.fullscreenMonitorName);
	DebugLog::Log(
		"Configuration fullscreen monitor selection published: previous='%S' current='%S' effect=next-fullscreen",
		previousSelection.IsEmpty() ? L"(default)" : previousSelection.GetString(),
		m_fullscreenMonitorName.IsEmpty() ? L"(default)" :
			m_fullscreenMonitorName.GetString());
}

void CVideoProcessorDlg::PublishStagedRuntimeSettings()
{
	const auto sessionPresentation =
		ConfigurationLiveApply::PreserveSessionPresentation(
			m_hideUI,
			m_rendererFullscreenCheck.GetCheck() == BST_CHECKED);
	auto selectData = [](CComboBox& combo, DWORD_PTR value)
	{
		for (int index = 0; index < combo.GetCount(); ++index)
			if (combo.GetItemData(index) == value)
			{
				combo.SetCurSel(index);
				return true;
			}
		return false;
	};
	if (m_stagedRuntimeSettings.hasCaptureDevice)
	{
		for (int index = 0; index < m_captureDeviceCombo.GetCount(); ++index)
		{
			CString name;
			m_captureDeviceCombo.GetLBText(index, name);
			if (name.CompareNoCase(m_stagedRuntimeSettings.captureDevice) != 0)
				continue;
			m_captureDeviceCombo.SetCurSel(index);
			m_initialCaptureDevice = name;
			DebugLog::Log(
				"Configuration runtime capture selection published: device=%S index=%d",
				name.GetString(), index + 1);
			break;
		}
	}
	if (m_stagedRuntimeSettings.hasCaptureInput)
	{
		m_initialCaptureInput = m_stagedRuntimeSettings.captureInput;
		DebugLog::Log(
			"Configuration runtime capture input published: input=%S",
			m_initialCaptureInput.GetString());
	}
	const std::wstring savedRenderer = m_stagedRuntimeSettings.hasRenderer ?
		std::wstring(m_stagedRuntimeSettings.renderer.GetString()) :
		std::wstring();
	const auto rendererDecision =
		ConfigurationLiveApply::ResolveRendererPublication(
			savedRenderer,
			std::wstring(m_sessionRendererOverride.GetString()),
			std::wstring(m_acceptedRendererName.GetString()),
			m_stagedEditorApply, m_stagedRendererChanged);
	bool rendererSelected = rendererDecision.renderer.empty();
	if (!rendererDecision.renderer.empty())
	{
		for (int index = 0; index < m_rendererCombo.GetCount(); ++index)
		{
			const RendererId* renderer = reinterpret_cast<const RendererId*>(
				m_rendererCombo.GetItemData(index));
			if (renderer && renderer->MatchesConfiguredName(
				CString(rendererDecision.renderer.c_str())))
			{
				m_rendererCombo.SetCurSel(index);
				rendererSelected = true;
				break;
			}
		}
		if (!rendererSelected)
			DebugLog::Log(
				"Configuration runtime renderer retained: requested=%S source=%d failure=not-discovered",
				rendererDecision.renderer.c_str(),
				static_cast<int>(rendererDecision.source));
	}
	if (rendererSelected && m_stagedEditorApply &&
		m_stagedRendererChanged && !rendererDecision.renderer.empty())
	{
		m_sessionRendererOverride = rendererDecision.renderer.c_str();
		DebugLog::Log(
			"Session renderer override replaced by editor apply: renderer=%S",
			m_sessionRendererOverride.GetString());
	}
	if (m_stagedRuntimeSettings.hasFrameOffset)
	{
		m_timingClockFrameOffsetAutoCheck.SetCheck(
			m_stagedRuntimeSettings.frameOffsetAuto ? BST_CHECKED : BST_UNCHECKED);
		const int offset = m_stagedRuntimeSettings.frameOffsetAuto ?
			CalculateAutoFrameOffset() : m_stagedRuntimeSettings.frameOffsetMs;
		m_directShowFrameOffsetMs = offset;
		SetTimingClockFrameOffsetMs(offset);
		if (m_captureDevice)
			m_captureDevice->SetFrameOffsetMs(IsAlphaRendererSelected() ? 0 : offset);
	}
	if (m_stagedRuntimeSettings.hasDirectShowVideoConversion)
		m_directShowVideoConversionOverride =
			m_stagedRuntimeSettings.directShowVideoConversion;
	if (m_stagedRuntimeSettings.hasVpRendererVideoConversion)
		m_vpRendererVideoConversionOverride =
			m_stagedRuntimeSettings.vpRendererVideoConversion;
	if (m_stagedRuntimeSettings.hasDirectShowContainerColorSpace)
		m_directShowContainerColorSpace =
			m_stagedRuntimeSettings.directShowContainerColorSpace;
	if (m_stagedRuntimeSettings.hasVpRendererContainerColorSpace)
		m_vpRendererContainerColorSpace =
			m_stagedRuntimeSettings.vpRendererContainerColorSpace;
	if (m_stagedRuntimeSettings.hasDirectShowHdrColorSpace)
		m_directShowHDRColorSpaceOption =
			m_stagedRuntimeSettings.directShowHdrColorSpace;
	if (m_stagedRuntimeSettings.hasVpRendererHdrColorSpace)
		m_vpRendererHDRColorSpaceOption =
			m_stagedRuntimeSettings.vpRendererHdrColorSpace;
	if (m_stagedRuntimeSettings.hasDirectShowHdrLuminance)
		m_directShowHDRLuminanceOption =
			m_stagedRuntimeSettings.directShowHdrLuminance;
	if (m_stagedRuntimeSettings.hasVpRendererHdrLuminance)
		m_vpRendererHDRLuminanceOption =
			m_stagedRuntimeSettings.vpRendererHdrLuminance;
	if (m_stagedRuntimeSettings.hasDirectShowTimeMethod)
		selectData(m_rendererDirectShowStartStopTimeMethodCombo,
			static_cast<DWORD_PTR>(m_stagedRuntimeSettings.directShowTimeMethod));
	if (m_stagedRuntimeSettings.hasNominalRange)
		selectData(m_rendererNominalRangeCombo,
			static_cast<DWORD_PTR>(m_stagedRuntimeSettings.nominalRange));
	if (m_stagedRuntimeSettings.hasTransferFunction)
		selectData(m_rendererTransferFunctionCombo,
			static_cast<DWORD_PTR>(m_stagedRuntimeSettings.transferFunction));
	if (m_stagedRuntimeSettings.hasTransferMatrix)
		selectData(m_rendererTransferMatrixCombo,
			static_cast<DWORD_PTR>(m_stagedRuntimeSettings.transferMatrix));
	if (m_stagedRuntimeSettings.hasPrimaries)
		selectData(m_rendererPrimariesCombo,
			static_cast<DWORD_PTR>(m_stagedRuntimeSettings.primaries));
	if (m_stagedRuntimeSettings.hasSceneDetect)
	{
		m_sceneAwareTimingCorrection = m_stagedRuntimeSettings.sceneDetect;
		m_rendererSceneCorrectionModeCombo.SetCurSel(
			m_sceneAwareTimingCorrection ? 1 : 0);
	}
	UpdateRendererBackendUi();
	UpdateTimingClockFrameOffsetAvailability();
	UpdateSceneCorrectionModeUi();
	// Live Apply publishes restart-class runtime settings, never presentation
	// defaults. Retain the operator's current Video Only and View choices even
	// if those fields are present in the newly accepted configuration.
	m_hideUI = sessionPresentation.videoOnly;
	m_rendererFullscreenCheck.SetCheck(
		sessionPresentation.fullscreen ? BST_CHECKED : BST_UNCHECKED);
	DebugLog::Log(
		"Configuration runtime settings published: renderer=%S renderer_source=%d saved_renderer=%S session_renderer=%S accepted_renderer=%S alpha_selected=%d conversion=%d directshow_conversion=%d vp_conversion=%d active_conversion=%d frame_offset=%d metadata=%d directshow=%d presentation_retained=1 video_only=%d fullscreen=%d",
		rendererDecision.renderer.empty() ? L"(retained)" :
			rendererDecision.renderer.c_str(),
		static_cast<int>(rendererDecision.source),
		savedRenderer.empty() ? L"(none)" : savedRenderer.c_str(),
		m_sessionRendererOverride.IsEmpty() ? L"(none)" :
			m_sessionRendererOverride.GetString(),
		m_acceptedRendererName.IsEmpty() ? L"(none)" :
			m_acceptedRendererName.GetString(),
		IsAlphaRendererSelected() ? 1 : 0,
		(m_stagedRuntimeSettings.hasDirectShowVideoConversion ||
			m_stagedRuntimeSettings.hasVpRendererVideoConversion) ? 1 : 0,
		static_cast<int>(m_directShowVideoConversionOverride),
		static_cast<int>(m_vpRendererVideoConversionOverride),
		static_cast<int>(IsAlphaRendererSelected() ?
			m_vpRendererVideoConversionOverride :
			m_directShowVideoConversionOverride),
		m_stagedRuntimeSettings.hasFrameOffset ? 1 : 0,
		(m_stagedRuntimeSettings.hasDirectShowContainerColorSpace ||
			m_stagedRuntimeSettings.hasVpRendererContainerColorSpace ||
			m_stagedRuntimeSettings.hasDirectShowHdrColorSpace ||
			m_stagedRuntimeSettings.hasVpRendererHdrColorSpace ||
			m_stagedRuntimeSettings.hasDirectShowHdrLuminance ||
			m_stagedRuntimeSettings.hasVpRendererHdrLuminance) ? 1 : 0,
		(m_stagedRuntimeSettings.hasDirectShowTimeMethod ||
			m_stagedRuntimeSettings.hasNominalRange ||
			m_stagedRuntimeSettings.hasTransferFunction ||
			m_stagedRuntimeSettings.hasTransferMatrix ||
			m_stagedRuntimeSettings.hasPrimaries) ? 1 : 0,
		m_hideUI ? 1 : 0,
		m_rendererFullscreenCheck.GetCheck() == BST_CHECKED ? 1 : 0);
}

void CVideoProcessorDlg::RestoreAcceptedRendererSelectionAfterReloadFailure()
{
	const CString& fallbackRenderer = m_sessionRendererOverride.IsEmpty() ?
		m_acceptedRendererName : m_sessionRendererOverride;
	if (fallbackRenderer.IsEmpty())
		return;
	const int requestedIndex = m_rendererCombo.GetCurSel();
	CString requestedName;
	if (requestedIndex >= 0)
		m_rendererCombo.GetLBText(requestedIndex, requestedName);
	for (int index = 0; index < m_rendererCombo.GetCount(); ++index)
	{
		const RendererId* renderer = reinterpret_cast<const RendererId*>(
			m_rendererCombo.GetItemData(index));
		if (!renderer || !renderer->MatchesConfiguredName(fallbackRenderer))
			continue;
		const bool differs = index != requestedIndex;
		if (!ConfigurationLiveApply::
			ShouldRestoreAcceptedRendererAfterReload(
				false, true, differs))
			return;
		m_rendererCombo.SetCurSel(index);
		UpdateRendererBackendUi();
		DebugLog::Log(
			"Configuration reload fallback restored session renderer: requested=%S fallback=%S session_override=%d index=%d",
			requestedName.IsEmpty() ? L"(none)" : requestedName.GetString(),
			fallbackRenderer.GetString(),
			m_sessionRendererOverride.IsEmpty() ? 0 : 1, index + 1);
		return;
	}
	DebugLog::Log(
		"Configuration reload fallback could not restore session renderer: fallback=%S failure=not-discovered",
		fallbackRenderer.GetString());
}

bool CVideoProcessorDlg::ReplaceStagedAccelerators()
{
	if (!m_stagedAccelerator)
		return false;
	HACCEL previous = m_accelerator;
	m_accelerator = m_stagedAccelerator;
	m_stagedAccelerator = nullptr;
	m_shaderShortcutRules = std::move(m_stagedShaderShortcutRules);
	m_shaderShortcutKeys = std::move(m_stagedShaderShortcutKeys);
	m_displayRuleShortcutRules =
		std::move(m_stagedDisplayRuleShortcutRules);
	m_rendererShortcutIndices =
		std::move(m_stagedRendererShortcutIndices);
	m_unifiedProfileShortcutKeys =
		std::move(m_stagedUnifiedProfileShortcutKeys);
	m_configuredAccelerators =
		std::move(m_stagedConfiguredAccelerators);
	m_shortcutsForegroundOnly =
		m_stagedRuntimeSettings.shortcutsForegroundOnly;
	if (previous)
		DestroyAcceleratorTable(previous);
	StartGlobalShortcutObserver();
	DebugLog::Log("Shortcut focus policy applied live: foreground_only=%d",
		m_shortcutsForegroundOnly ? 1 : 0);
	return true;
}

bool CVideoProcessorDlg::PublishStagedShortcutsOnly()
{
	if (!m_stagedConfiguration || !m_stagedAccelerator)
	{
		DebugLog::Log(
			"Configuration shortcut publication retained last-known-good: identity=%s failure=missing-staged-accelerators shortcuts=retained renderer=retained capture=retained queues=retained",
			m_stagedConfigurationIdentity.c_str());
		ClearStagedConfiguration();
		return false;
	}
	// Profile shortcut commands are resolved by the runtime model after the
	// accelerator dispatch. Refresh that model from the same staged file, but
	// deliberately do not apply its renderer/queue snapshot on this path.
	UnifiedProfileRuntime::RefreshResult profileResult;
	std::string error;
	const bool profileReady = m_profileRuntime.IsInitialized() ?
		m_profileRuntime.Reload(*m_stagedConfiguration,
			GetUnifiedProfileSourceLookup(), profileResult, error) :
		m_profileRuntime.Initialize(*m_stagedConfiguration,
			GetUnifiedProfileSourceLookup(), error);
	if (!profileReady)
	{
		DebugLog::Log(
			"Configuration shortcut publication retained last-known-good: identity=%s failure=profile-model-refresh detail=%s shortcuts=retained renderer=retained capture=retained queues=retained",
			m_stagedConfigurationIdentity.c_str(), error.c_str());
		ClearStagedConfiguration();
		return false;
	}
	if (!ReplaceStagedAccelerators())
	{
		DebugLog::Log(
			"Configuration shortcut publication retained last-known-good: identity=%s failure=accelerator-swap shortcuts=retained renderer=retained capture=retained queues=retained",
			m_stagedConfigurationIdentity.c_str());
		ClearStagedConfiguration();
		return false;
	}
	m_configurationSnapshot =
		CaptureConfigurationSnapshot(*m_stagedConfiguration);
	DebugLog::Log(
		"Configuration shortcut publication complete: identity=%s shortcuts=replaced renderer=retained capture=retained queues=retained",
		m_stagedConfigurationIdentity.c_str());
	ClearStagedConfiguration();
	return true;
}

bool CVideoProcessorDlg::PublishStagedConfiguration(bool replaceAccelerators)
{
	if (!m_stagedConfiguration)
		return true;
	UnifiedProfileRuntime::RefreshResult result;
	std::string error;
	if (m_profileRuntime.IsInitialized())
	{
		if (!m_profileRuntime.Reload(*m_stagedConfiguration,
			GetUnifiedProfileSourceLookup(), result, error))
		{
			DebugLog::Log(
				"Configuration publication retained last-known-good: identity=%s failure=%s shortcuts=retained",
				m_stagedConfigurationIdentity.c_str(), error.c_str());
			ClearStagedConfiguration();
			return false;
		}
	}
	else if (!m_profileRuntime.Initialize(*m_stagedConfiguration,
		GetUnifiedProfileSourceLookup(), error))
	{
		DebugLog::Log(
			"Configuration publication retained last-known-good: identity=%s failure=%s shortcuts=retained",
			m_stagedConfigurationIdentity.c_str(), error.c_str());
		ClearStagedConfiguration();
		return false;
	}
	else
	{
		result.changed = true;
		result.snapshot = m_profileRuntime.GetSnapshot();
	}

	if (replaceAccelerators)
	{
		if (!ReplaceStagedAccelerators())
		{
			DebugLog::Log(
				"Configuration publication retained last-known-good: identity=%s failure=missing-staged-accelerators",
				m_stagedConfigurationIdentity.c_str());
			ClearStagedConfiguration();
			return false;
		}
	}

	PublishStagedRuntimeSettings();
	m_configurationSnapshot =
		CaptureConfigurationSnapshot(*m_stagedConfiguration);
	ApplyUnifiedProfileSnapshot(result.snapshot ? result.snapshot :
		m_profileRuntime.GetSnapshot(),
		m_stagedConfigurationAction ==
			ConfigurationApplyPolicy::Action::ResetQueues ||
		m_stagedConfigurationAction ==
			ConfigurationApplyPolicy::Action::ApplyProfiles);
	DebugLog::Log(
		"Configuration publication complete: identity=%s action=%s shortcuts=%s",
		m_stagedConfigurationIdentity.c_str(),
		ConfigurationApplyPolicy::ActionLabel(m_stagedConfigurationAction),
		replaceAccelerators ? "replaced" : "retained");
	ClearStagedConfiguration();
	return true;
}

void CVideoProcessorDlg::ClearStagedConfiguration()
{
	if (m_stagedAccelerator)
		DestroyAcceleratorTable(m_stagedAccelerator);
	m_stagedAccelerator = nullptr;
	m_stagedConfiguration.reset();
	m_stagedConfiguredAccelerators.clear();
	m_stagedShaderShortcutRules.clear();
	m_stagedShaderShortcutKeys.clear();
	m_stagedDisplayRuleShortcutRules.clear();
	m_stagedRendererShortcutIndices.clear();
	m_stagedUnifiedProfileShortcutKeys.clear();
	m_stagedConfigurationIdentity.clear();
	m_stagedShortcutsChanged = false;
	m_stagedEditorApply = false;
	m_stagedRendererChanged = false;
	m_stagedConfigurationAction =
		ConfigurationApplyPolicy::Action::SaveOnly;
	m_stagedRuntimeSettings = StagedRuntimeSettings();
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
	ClearStagedConfiguration();
	if (m_fullScreenVideoWindow &&
		::IsWindow(m_fullScreenVideoWindow->GetHWND()))
		::EnableWindow(m_fullScreenVideoWindow->GetHWND(), TRUE);
	if (GetSafeHwnd()) ::EnableWindow(GetSafeHwnd(), TRUE);
	StopGlobalShortcutObserver();
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
	if (m_configurationChangedEvent)
	{
		CloseHandle(m_configurationChangedEvent);
		m_configurationChangedEvent = nullptr;
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

void CVideoProcessorDlg::AlwaysWarnPci(bool enabled)
{
	m_alwaysWarnPci = enabled;
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
	m_directShowVideoConversionOverride = videoConversionOverride;
	m_vpRendererVideoConversionOverride = videoConversionOverride;
}


void CVideoProcessorDlg::DefaultContainerColorSpace(ColorSpace containerColorSpace)
{
	m_directShowContainerColorSpace = containerColorSpace;
	m_vpRendererContainerColorSpace = containerColorSpace;
}


void CVideoProcessorDlg::DefaultHDRColorSpace(HdrColorspaceOptions hdrColorSpaceOption)
{
	m_directShowHDRColorSpaceOption = hdrColorSpaceOption;
	m_vpRendererHDRColorSpaceOption = hdrColorSpaceOption;
}


void CVideoProcessorDlg::DefaultHDRLuminance(HdrLuminanceOptions hdrLuminanceOption)
{
	m_directShowHDRLuminanceOption = hdrLuminanceOption;
	m_vpRendererHDRLuminanceOption = hdrLuminanceOption;
}

void CVideoProcessorDlg::SetVideoConversionOff()
{
	const bool vpRenderer = IsAlphaRendererSelected();
	if (vpRenderer)
		m_vpRendererVideoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_NONE;
	else
		m_directShowVideoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_NONE;
	DEBUGLOG("Video conversion command applied: owner=%s value=NONE", vpRenderer ? "vprenderer" : "directshow");
	if (m_rendererVideoConversionCombo.GetCurSel() != 0) {

		m_rendererVideoConversionCombo.SetCurSel(0);
		OnBnClickedCaptureRestart();
	}
}

void CVideoProcessorDlg::SetVideoConversionP010()
{
	const bool vpRenderer = IsAlphaRendererSelected();
	if (vpRenderer)
		m_vpRendererVideoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010;
	else
		m_directShowVideoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010;
	DEBUGLOG("Video conversion command applied: owner=%s value=V210_TO_P010", vpRenderer ? "vprenderer" : "directshow");
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
	EstablishSessionRendererOverrideFromSelection("operator-selection");
	UpdateRendererBackendUi();
	OnBnClickedRendererRestart();
}

bool CVideoProcessorDlg::EstablishSessionRendererOverrideFromSelection(
	const char* reason)
{
	const int selection = m_rendererCombo.GetCurSel();
	if (selection < 0)
		return false;
	const RendererId* renderer = reinterpret_cast<const RendererId*>(
		m_rendererCombo.GetItemData(selection));
	if (!renderer)
		return false;
	CString rendererName;
	m_rendererCombo.GetLBText(selection, rendererName);
	if (rendererName.IsEmpty())
		return false;
	m_sessionRendererOverride = rendererName;
	DebugLog::Log(
		"Session renderer override established: reason=%s renderer=%S index=%d accepted=%S",
		reason ? reason : "unknown", m_sessionRendererOverride.GetString(),
		selection + 1, m_acceptedRendererName.IsEmpty() ? L"(none)" :
			m_acceptedRendererName.GetString());
	return true;
}


void CVideoProcessorDlg::OnBnClickedRendererRestart()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnBnClickedRendererRestart()")));

	if (m_rendererState == RendererState::RENDERSTATE_FAILED)
		m_rendererState = RendererState::RENDERSTATE_UNKNOWN;

	m_postRendererStartRequiresGraph = true;
	m_wantToRestartRenderer = true;
	const RendererRestartDispatch dispatch = ClassifyRendererRestartDispatch(
		m_rendererConstructionActive, m_rendererRetirementPending);
	if (dispatch != RendererRestartDispatch::DispatchNow)
	{
		DebugLog::Log(
			"Renderer restart intent coalesced: renderer=%S construction_active=%d "
			"retirement_pending=%d action=latest-intent-waits-for-lifecycle-boundary",
			m_sessionRendererOverride.IsEmpty() ? L"(selection)" :
				m_sessionRendererOverride.GetString(),
			m_rendererConstructionActive ? 1 : 0,
			m_rendererRetirementPending ? 1 : 0);
		return;
	}
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
	const VideoConversionOverride conversion = directShowSelected ?
		m_directShowVideoConversionOverride : m_vpRendererVideoConversionOverride;
	const ColorSpace containerColorSpace = directShowSelected ?
		m_directShowContainerColorSpace : m_vpRendererContainerColorSpace;
	const HdrColorspaceOptions hdrColorSpace = directShowSelected ?
		m_directShowHDRColorSpaceOption : m_vpRendererHDRColorSpaceOption;
	const HdrLuminanceOptions hdrLuminance = directShowSelected ?
		m_directShowHDRLuminanceOption : m_vpRendererHDRLuminanceOption;
	auto selectData = [](CComboBox& combo, DWORD_PTR value)
	{
		for (int index = 0; index < combo.GetCount(); ++index)
			if (combo.GetItemData(index) == value)
			{
				combo.SetCurSel(index);
				return;
			}
	};
	selectData(m_rendererVideoConversionCombo, static_cast<DWORD_PTR>(conversion));
	selectData(m_colorspaceContainerCombo, static_cast<DWORD_PTR>(containerColorSpace));
	selectData(m_hdrColorspaceCombo, static_cast<DWORD_PTR>(hdrColorSpace));
	selectData(m_hdrLuminanceCombo, static_cast<DWORD_PTR>(hdrLuminance));

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
	const uint32_t messageGeneration = static_cast<uint32_t>(lParam);
	const uint32_t currentGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	const std::shared_ptr<IVideoRenderer> renderer =
		std::atomic_load_explicit(
			&m_videoRenderer, std::memory_order_acquire);
	if (!RendererGenerationGate::Accept(
		messageGeneration, currentGeneration, renderer != nullptr) ||
		!m_activeRendererIsDirectShow)
	{
		DebugLog::Log(
			"DirectShow notification rejected: message_generation=%u "
			"current_generation=%u renderer=%d directshow=%d",
			messageGeneration, currentGeneration, renderer ? 1 : 0,
			m_activeRendererIsDirectShow ? 1 : 0);
		return 0;
	}

	const HRESULT hr = renderer->OnWindowsEvent(wParam, lParam);
	if (FAILED(hr))
		FatalError(TEXT("Failed to handle windows event in renderer"));

	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageRendererStateChange(WPARAM wParam, LPARAM lParam)
{
	const RendererState newRendererState = (RendererState)wParam;
	const uint32_t messageGeneration = static_cast<uint32_t>(lParam);
	const uint32_t currentGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	if (!RendererGenerationGate::Accept(
		messageGeneration, currentGeneration, m_videoRenderer != nullptr))
	{
		DebugLog::Log(
			"Renderer state rejected: state=%d message_generation=%u "
			"current_generation=%u renderer=%d",
			static_cast<int>(newRendererState), messageGeneration,
			currentGeneration, m_videoRenderer ? 1 : 0);
		return 0;
	}

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
		m_acceptedRendererName = m_activeRendererName;
		DebugLog::Log(
			"Renderer accepted for configuration fallback: renderer=%S generation=%u",
			m_acceptedRendererName.GetString(),
			m_rendererGeneration.load(std::memory_order_acquire));
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
		if (m_queueProfileRestartCompletionPending &&
			!m_wantToRestartRenderer &&
			m_rendererGeneration.load(std::memory_order_acquire) >=
				m_queueProfileRestartStartingGeneration)
		{
			const auto profileSnapshot = m_profileRuntime.GetSnapshot();
			const std::string effectiveProfile = profileSnapshot ?
				profileSnapshot->queue.profile : std::string();
			DebugLog::Log(
				"Queue profile restart: profile=%s source=%s generation=%u "
				"outcome=completed effective_profile=%s",
				m_queueProfileRestartCompletionProfile.c_str(),
				m_queueProfileRestartCompletionSource.c_str(),
				m_rendererGeneration.load(std::memory_order_acquire),
				effectiveProfile.c_str());
			m_queueProfileRestartCompletionPending = false;
			m_queueProfileRestartCompletionProfile.clear();
			m_queueProfileRestartCompletionSource.clear();
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
		if (m_queueProfileRestartCompletionPending)
		{
			DebugLog::Log(
				"Queue profile restart: profile=%s source=%s generation=%u "
				"outcome=failed action=resolve-renderer-error-then-use-Restart-Renderer",
				m_queueProfileRestartCompletionProfile.c_str(),
				m_queueProfileRestartCompletionSource.c_str(),
				m_rendererGeneration.load(std::memory_order_acquire));
		}
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
		if (m_queueProfileRestartCompletionPending)
		{
			m_windowedVideoWindow.SetWindowText(
				TEXT("Queue profile applied, but renderer restart failed. Resolve the renderer error, then use Restart Renderer."));
			m_queueProfileRestartCompletionPending = false;
			m_queueProfileRestartCompletionProfile.clear();
			m_queueProfileRestartCompletionSource.clear();
		}
		else
		{
			m_windowedVideoWindow.SetWindowText(
				TEXT("DirectShow renderer failed to build or start"));
		}
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
	TryClassifyActiveOutputSweepCase(GetTickCount64(), "live-frame-message");
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
	LPARAM)
{
	const uint64_t token = static_cast<uint64_t>(wParam);
	if (TryFinalizeRendererRetirement(token, "window-message"))
		UpdateState();
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageRendererIntentReady(
	WPARAM,
	LPARAM)
{
	if (!m_rendererConstructionActive)
		UpdateState();
	return 0;
}


bool CVideoProcessorDlg::TryFinalizeRendererRetirement(
	uint64_t token,
	const char* completionSource)
{
	if (!m_rendererRetirementPending ||
		token != m_rendererRetirementToken)
	{
		DebugLog::Log(
			"Renderer retirement completion ignored: token=%llu current=%llu pending=%d",
			static_cast<unsigned long long>(token),
			static_cast<unsigned long long>(m_rendererRetirementToken),
			m_rendererRetirementPending ? 1 : 0);
		return false;
	}
	RendererRetirementService::Completion completion;
	if (!m_rendererRetirementService.TryTakeCompletion(token, completion))
	{
		if (m_rendererRetirementWaitLoggedToken != token)
		{
			m_rendererRetirementWaitLoggedToken = token;
			DebugLog::Log(
				"Renderer retirement awaiting durable completion: "
				"token=%llu source=%s subsequent_polls=suppressed",
				static_cast<unsigned long long>(token), completionSource);
		}
		return false;
	}

	m_rendererRetirementPending = false;
	m_rendererRetirementWaitLoggedToken = 0;
	if (!completion.succeeded)
	{
		m_rendererRetirementRetryActive = false;
		m_failedRendererRetirement = std::move(completion.renderer);
		m_failedRendererRetirementNextRetryTick = GetTickCount64() + 1000;
		DebugLog::Log(
			"Renderer retirement incomplete: token=%llu renderer=%S source=%s; "
			"external-state restoration remains pending and replacement is blocked",
			static_cast<unsigned long long>(token),
			static_cast<LPCTSTR>(m_retiringRendererName), completionSource);
		m_rendererState = RendererState::RENDERSTATE_FAILED;
		m_rendererStateText.SetWindowText(TEXT("Display restore pending"));
		return true;
	}
	const bool completedRetry = m_rendererRetirementRetryActive;
	m_rendererRetirementRetryActive = false;
	m_failedRendererRetirementNextRetryTick = 0;
	DebugLog::Log(
		"Renderer transition: process=%lu generation=%u event=old-surface-retired "
		"renderer=%S target=%p cover=%p token=%llu source=%s "
		"wake_posted=%d wake_error=%lu",
		GetCurrentProcessId(), m_retiringRendererGeneration,
		static_cast<LPCTSTR>(m_retiringRendererName),
		m_rendererTargetHwnd, m_rendererTransitionWindow.GetHWND(),
		static_cast<unsigned long long>(token), completionSource,
		completion.wakePosted ? 1 : 0, completion.wakePostError);
	m_retiringRendererName.Empty();
	m_retiringRendererGeneration = 0;
	if (completedRetry)
		m_rendererState = RendererState::RENDERSTATE_UNKNOWN;
	else if (m_rendererState == RendererState::RENDERSTATE_STOPPED)
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
	return true;
}


LRESULT CVideoProcessorDlg::OnMessageRendererDetailString(WPARAM wParam, LPARAM lParam)
{
	CString* pDetailString = (CString*)wParam;
	const uint32_t messageGeneration = static_cast<uint32_t>(lParam);
	const uint32_t currentGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	if (RendererGenerationGate::Accept(
		messageGeneration, currentGeneration, m_videoRenderer != nullptr))
		m_rendererDetailStringStatic.SetWindowText(*pDetailString);
	else
		DebugLog::Log(
			"Renderer detail rejected: message_generation=%u "
			"current_generation=%u renderer=%d",
			messageGeneration, currentGeneration, m_videoRenderer ? 1 : 0);

	delete pDetailString;
	return 0;
}

LRESULT CVideoProcessorDlg::OnMessageRendererGraphEvent(
	WPARAM wParam, LPARAM lParam)
{
	const long eventCode = static_cast<long>(wParam);
	const uint32_t messageGeneration = static_cast<uint32_t>(lParam);
	const uint32_t currentGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	if (!RendererGenerationGate::Accept(
		messageGeneration, currentGeneration, m_videoRenderer != nullptr) ||
		!m_activeRendererIsDirectShow)
	{
		DebugLog::Log(
			"DirectShow graph event rejected: event=0x%lx "
			"message_generation=%u current_generation=%u renderer=%d "
			"directshow=%d",
			eventCode, messageGeneration, currentGeneration,
			m_videoRenderer ? 1 : 0,
			m_activeRendererIsDirectShow ? 1 : 0);
		return 0;
	}

	DbgLog((LOG_TRACE, 2,
		TEXT("DirectShow graph event received: event=0x%08lX generation=%u"),
		eventCode, messageGeneration));
	switch (eventCode)
	{
	case EC_DISPLAY_CHANGED:
		if (!m_outputReadinessGraphReprimeActive)
			g_displayRefreshRateSampler->ResetMeasurement();
		else
			DebugLog::Log(
				"DirectShow display event belongs to the output-readiness "
				"graph re-prime; preserving its selecting DXGI evidence");
		if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
			(!m_rendererResetCoordinator ||
				!m_rendererResetCoordinator->GetDiagnostics().hasPending) &&
			GetTickCount64() >= m_queueResetIgnoreEventsUntil)
		{
			RequestRendererReset(
				RendererResetReason::DisplayTransition, true,
				static_cast<UINT>(m_queueResetDelaySeconds * 1000));
		}
		else
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("DirectShow display transition - queue re-prime already pending or suppressed")));
		}
		break;
	case EC_VIDEO_SIZE_CHANGED:
		ASSERT(ClassifyDirectShowGraphEvent(eventCode) ==
			DirectShowGraphEventImpact::GeometryOnly);
		DebugLog::Log(
			"DirectShow video geometry changed: event=EC_VIDEO_SIZE_CHANGED "
			"action=retain-display-timing-and-queue");
		break;
	case EC_WINDOW_DESTROYED:
		DbgLog((LOG_TRACE, 1,
			TEXT("EC_WINDOW_DESTROYED detected - DirectShow window change")));
		break;
	case EC_QUALITY_CHANGE:
		DbgLog((LOG_TRACE, 1,
			TEXT("EC_QUALITY_CHANGE detected - renderer quality adjustment")));
		break;
	case EC_REPAINT:
		break;
	default:
		break;
	}
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageRendererRestartRequired(
	WPARAM, LPARAM lParam)
{
	const uint32_t messageGeneration = static_cast<uint32_t>(lParam);
	const uint32_t currentGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	if (!RendererGenerationGate::Accept(
		messageGeneration, currentGeneration, m_videoRenderer != nullptr))
	{
		DebugLog::Log(
			"Renderer restart request rejected: message_generation=%u "
			"current_generation=%u renderer=%d",
			messageGeneration, currentGeneration, m_videoRenderer ? 1 : 0);
		return 0;
	}
	m_postRendererStartRequiresGraph = false;
	m_wantToRestartRenderer = true;
	UpdateState();
	return 0;
}


LRESULT CVideoProcessorDlg::OnMessageExternalShortcut(WPARAM wParam,
	LPARAM lParam)
{
	const BYTE supportedModifiers = FCONTROL | FALT | FSHIFT;
	if (wParam > 0xffff ||
		(static_cast<UINT_PTR>(lParam) & ~supportedModifiers) != 0)
	{
		DebugLog::Log(
			"External shortcut rejected: key=%llu modifiers=0x%llx reason=invalid-payload",
			static_cast<unsigned long long>(wParam),
			static_cast<unsigned long long>(lParam));
		return 0;
	}

	const WORD key = static_cast<WORD>(wParam);
	const BYTE modifiers = static_cast<BYTE>(lParam);
	const bool control = (modifiers & FCONTROL) != 0;
	const bool alt = (modifiers & FALT) != 0;
	const bool shift = (modifiers & FSHIFT) != 0;
	const bool rightAlt = (::GetKeyState(VK_RMENU) & 0x8000) != 0;
	const ACCEL* matchedAccelerator = nullptr;
	for (const ACCEL& accelerator : m_configuredAccelerators)
	{
		if (accelerator.key != key ||
			(accelerator.fVirt & supportedModifiers) != modifiers)
			continue;
		matchedAccelerator = &accelerator;
		break;
	}
	if (!matchedAccelerator && rightAlt)
	{
		for (const ACCEL& accelerator : m_configuredAccelerators)
		{
			if (accelerator.cmd == ID_COMMAND_FULLSCREEN_TOGGLE &&
				accelerator.key == key &&
				ConfigurationLiveApply::FullscreenShortcutModifiersMatch(
					(accelerator.fVirt & FCONTROL) != 0,
					(accelerator.fVirt & FALT) != 0,
					(accelerator.fVirt & FSHIFT) != 0,
					control, alt, shift, rightAlt))
			{
				matchedAccelerator = &accelerator;
				break;
			}
		}
	}
	if (matchedAccelerator)
	{
		const BOOL posted = PostMessage(WM_COMMAND,
			MAKEWPARAM(matchedAccelerator->cmd, 1), 0);
		DebugLog::Log(
			"External shortcut dispatch: key=%u modifiers=0x%02x command=%u posted=%d",
			static_cast<unsigned>(key), static_cast<unsigned>(modifiers),
			static_cast<unsigned>(matchedAccelerator->cmd), posted ? 1 : 0);
		return posted ? 1 : 0;
	}

	DebugLog::Log(
		"External shortcut rejected: key=%u modifiers=0x%02x reason=not-configured",
		static_cast<unsigned>(key), static_cast<unsigned>(modifiers));
	return 0;
}

//
// Command handlers
//


void CVideoProcessorDlg::OnCommandFullScreenToggle()
{
	const bool requested =
		m_rendererFullscreenCheck.GetCheck() == BST_CHECKED;
	const bool active = m_fullScreenVideoWindow &&
		IsWindow(m_fullScreenVideoWindow->GetHWND()) &&
		::IsWindowVisible(m_fullScreenVideoWindow->GetHWND());
	auto transitionDirection = ConfigurationLiveApply::
		FullscreenTransitionDirection::None;
	if (m_fullscreenRetargetPending)
	{
		transitionDirection = m_fullscreenRetargetExiting ?
			ConfigurationLiveApply::FullscreenTransitionDirection::Exiting :
			ConfigurationLiveApply::FullscreenTransitionDirection::Entering;
	}
	else if (m_alphaHostTransitionPending)
	{
		transitionDirection = requested ?
			ConfigurationLiveApply::FullscreenTransitionDirection::Entering :
			ConfigurationLiveApply::FullscreenTransitionDirection::Exiting;
	}
	const auto action = ConfigurationLiveApply::ResolveFullscreenToggle(
		requested, active, transitionDirection);
	m_rendererFullscreenCheck.SetCheck(
		ConfigurationLiveApply::FullscreenRequestedAfterToggle(action) ?
		BST_CHECKED : BST_UNCHECKED);
	DebugLog::Log(
		"Fullscreen session toggle: configured=%d requested_before=%d "
		"active=%d transition=%s action=%s requested_after=%d",
		m_rendererFullScreenStart ? 1 : 0, requested ? 1 : 0,
		active ? 1 : 0,
		transitionDirection == ConfigurationLiveApply::
			FullscreenTransitionDirection::Entering ? "entering" :
			transitionDirection == ConfigurationLiveApply::
				FullscreenTransitionDirection::Exiting ? "exiting" : "none",
		action == ConfigurationLiveApply::FullscreenToggleAction::CancelPending ?
			"cancel-pending" :
			action == ConfigurationLiveApply::FullscreenToggleAction::ExitFullscreen ?
				"exit" : "enter",
		m_rendererFullscreenCheck.GetCheck() == BST_CHECKED ? 1 : 0);
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
	PublishActiveProfileStatus();
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
	if (!m_videoRenderer)
		return true;
	if (m_requestedShaderSelector.IsEmpty())
	{
		PublishActiveProfileStatus();
		return true;
	}

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
	PublishActiveProfileStatus();
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
		const auto previousSnapshot = m_profileRuntime.GetSnapshot();
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
			const std::string previousQueueProfile = previousSnapshot ?
				previousSnapshot->queue.profile : std::string();
			const bool queueProfileRestart = result.snapshot &&
				QueueProfileRestartPolicy::RequiresRestartAfterManualSelection(
					true, previousQueueProfile, result.snapshot->queue.profile);
			ApplyUnifiedProfileSnapshot(result.snapshot, true,
				queueProfileRestart);
			if (queueProfileRestart)
				QueueUnifiedQueueProfileRendererRestart(result.snapshot,
					"shortcut:" + std::string(
						CStringA(unifiedKey->second).GetString()));
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
		EstablishSessionRendererOverrideFromSelection(
			"renderer-shortcut-already-selected");
		DEBUGLOG("Renderer shortcut render.%u already selected: %s",
			oneBasedIndex,
			rendererName.GetString());
		return;
	}

	m_rendererCombo.SetCurSel(comboIndex);
	EstablishSessionRendererOverrideFromSelection("renderer-shortcut");
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

void CVideoProcessorDlg::OnCommandToggleNoUi()
{
	// TranslateAccelerator repeats WM_COMMAND while a shortcut is held. Video
	// Only is a state toggle, so treat its keyboard shortcut as edge-triggered:
	// one transition on key-down, then wait for the matching key-up below.
	const bool videoOnlyKeyDown =
		(::GetAsyncKeyState(
			ConfigurationLiveApply::VideoOnlyToggleDefaultKey) & 0x8000) != 0;
	if (videoOnlyKeyDown && m_noUiToggleShortcutLatched)
	{
		DebugLog::Log("Runtime UI shortcut suppressed: held key auto-repeat");
		return;
	}
	if (videoOnlyKeyDown)
		m_noUiToggleShortcutLatched = true;

	DebugLog::Log("Runtime UI shortcut requested: noui=%d layout_applied=%d",
		m_hideUI ? 1 : 0, m_noUiLayoutApplied ? 1 : 0);
	if (m_hideUI)
	{
		m_hideUI = false;
		RestoreNormalUiLayout();
	}
	else
	{
		m_hideUI = true;
		ApplyNoUiLayout();
	}

	// Do not restart the low-level shortcut observer during the Ctrl+U command
	// that it dispatched. Restarting clears its pressed-key set before Ctrl+U is
	// released, causing the held key to be seen as a fresh command and rapidly
	// re-entering this presentation toggle. The existing observer remains valid
	// across Video Only; configuration changes still rebuild it separately.
	DebugLog::Log("Runtime UI shortcut complete: noui=%d layout_applied=%d fullscreen=%d renderer_state=%d",
		m_hideUI ? 1 : 0,
		m_noUiLayoutApplied ? 1 : 0,
		m_rendererFullscreenCheck.GetCheck() == BST_CHECKED ? 1 : 0,
		static_cast<int>(m_rendererState));
}

void CVideoProcessorDlg::OnCommandConfigEditor()
{
	const ULONGLONG now = ::GetTickCount64();
	// UpdateConfigurationEditorModal no longer polls or expires reveal state.
	// Therefore an explicit shortcut must always begin a fresh attempt; otherwise
	// one failed cold launch permanently coalesces every later user request.
	m_configurationEditorActivationPending = true;
	m_configurationEditorFallbackLaunched = false;
	m_configurationEditorRevealAcknowledged = false;
	m_configurationEditorActivationAcknowledgedTick = 0;
	m_configurationEditorForegroundFallbackAttempted = false;
	m_configurationEditorActivationAttempts = 0;
	m_configurationEditorRevealStartedTick = now;
	m_configurationEditorLastRevealAttemptTick = 0;
	DebugLog::Log(
		"Configuration editor fresh reveal intent started: timeout_ms=20000");
	// Config normally stays warm in the tray.  A reveal through its stable
	// process event bypasses the association/reveal path below, so refresh the
	// presentation target first.  Otherwise Config can retain the monitor from
	// the previous reveal after VP has moved, or after fullscreen has retargeted
	// a different display.
	HWND existingEditor = FindConfigurationEditorForCurrentInstallation();
	if (!existingEditor && IsConfigurationEditorTopLevel(
		m_configurationEditorHwnd, m_configurationEditorProcessId, false))
	{
		existingEditor = m_configurationEditorHwnd;
	}
	TrackConfigurationEditor(existingEditor);
	if (existingEditor)
		PublishConfigurationEditorPresentationTarget(existingEditor);
	if (m_configurationEditorProcessId &&
		SignalConfigurationEditorReveal(m_configurationEditorProcessId))
	{
		m_configurationEditorActivationPending = false;
		m_configurationEditorRevealAcknowledged = true;
		m_configurationEditorActivationAcknowledgedTick = now;
		DebugLog::Log(
			"Configuration editor reveal signaled through stable process endpoint: pid=%lu",
			m_configurationEditorProcessId);
		return;
	}
	if (existingEditor && m_configurationEditorProcessId &&
		SignalConfigurationEditorReveal(m_configurationEditorProcessId))
	{
		m_configurationEditorActivationPending = false;
		m_configurationEditorRevealAcknowledged = true;
		m_configurationEditorActivationAcknowledgedTick = now;
		DebugLog::Log(
			"Configuration editor reveal signaled after process discovery: pid=%lu",
			m_configurationEditorProcessId);
		return;
	}
	if (existingEditor && m_configurationEditorActivationAttempts == 0)
	{
		++m_configurationEditorActivationAttempts;
		const bool acknowledged =
			RequestConfigurationEditorReveal(existingEditor);
		m_configurationEditorRevealAcknowledged = acknowledged;
		m_configurationEditorActivationAcknowledgedTick = now;
		m_configurationEditorLastRevealAttemptTick = now;
		m_configurationEditorActivationPending = false;
		DebugLog::Log(
			"Configuration editor explicit reveal dispatched once: acknowledged=%d",
			acknowledged ? 1 : 0);
		return;
	}
	if (existingEditor)
	{
		DebugLog::Log(
			"Configuration editor reveal coalesced: explicit activation already dispatched");
		return;
	}
	if (m_configurationEditorFallbackLaunched)
	{
		DebugLog::Log(
			"Configuration editor reveal still pending: launch already attempted association=%p",
			reinterpret_cast<void*>(existingEditor));
		return;
	}
	std::wstring executablePath;
	if (!GetApplicationDirectory(executablePath))
		return;
	const std::wstring editorPath = ConfigurationEditorPath(executablePath);
	const std::wstring editorDirectory =
		ConfigurationEditorDirectory(executablePath);
	if (GetFileAttributesW(editorPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		m_configurationEditorActivationPending = false;
		m_configurationEditorRevealStartedTick = 0;
		DEBUGLOG("Configuration editor is not installed at config\\VideoProcessorConfig.exe");
		AfxMessageBox(L"config\\VideoProcessorConfig.exe is not installed.");
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
	// Use the stable main dialog as the cross-process owner. Config remains in
	// the normal z-order band; VP suspends exclusive fullscreen while it is open.
	const HWND editorOwner = ConfigurationEditorOwner();
	wchar_t arguments[2 * MAX_PATH + 120] = {};
	swprintf_s(arguments,
		L"--config \"%s\" --owner %llu --owner-process %lu",
		configPath.c_str(),
		static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(editorOwner)),
		GetCurrentProcessId());
	const HINSTANCE result = ShellExecuteW(GetSafeHwnd(), L"open", editorPath.c_str(),
		arguments, editorDirectory.c_str(), SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
	{
		m_configurationEditorActivationPending = false;
		m_configurationEditorFallbackLaunched = false;
		m_configurationEditorRevealAcknowledged = false;
		m_configurationEditorActivationAcknowledgedTick = 0;
		m_configurationEditorRevealStartedTick = 0;
		AfxMessageBox(L"Could not launch VideoProcessorConfig.exe.");
	}
	else
	{
		m_configurationEditorFallbackLaunched = true;
	}
}

void CVideoProcessorDlg::StartConfigurationEditorInTray()
{
	// Config's hardware discovery touches capture, display, and renderer
	// registrations.  Keep that independent work out of VP's startup path so
	// the editor is already warm in the notification area when it is needed.
	std::wstring executablePath;
	if (!GetApplicationDirectory(executablePath))
		return;
	const std::wstring editorPath = ConfigurationEditorPath(executablePath);
	const std::wstring editorDirectory =
		ConfigurationEditorDirectory(executablePath);
	if (GetFileAttributesW(editorPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		DEBUGLOG("Configuration editor warm start skipped: executable is not installed");
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

	wchar_t arguments[2 * MAX_PATH + 32] = {};
	swprintf_s(arguments, L"--config \"%s\" --background", configPath.c_str());
	const HINSTANCE result = ShellExecuteW(GetSafeHwnd(), L"open", editorPath.c_str(),
		arguments, editorDirectory.c_str(), SW_HIDE);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
		DEBUGLOG("Configuration editor warm start failed result=%p", result);
	else
		DEBUGLOG("Configuration editor warm start requested");
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

void CVideoProcessorDlg::OnCommandCaptureRenderedOutput()
{
	CString status;
	const bool accepted = m_videoRenderer &&
		m_videoRenderer->RequestRenderedOutputCapture(status);
	if (status.IsEmpty())
		status = accepted ? TEXT("Rendered-output capture queued") :
			TEXT("Rendered-output capture is unavailable");
	DebugLog::Log(
		"Rendered-output capture command: accepted=%d renderer_state=%d detail=%ls",
		accepted ? 1 : 0, static_cast<int>(m_rendererState),
		static_cast<LPCWSTR>(status));
	OnRendererDetailString(
		status, m_rendererGeneration.load(std::memory_order_acquire));
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


void CVideoProcessorDlg::OnRendererState(
	RendererState rendererState, uint32_t rendererGeneration)
{
	// Will be called synchronous as a response to our calls and hence does
	// not need posting messages, we still do so to keep the pattern.

	PostMessage(
		WM_MESSAGE_RENDERER_STATE_CHANGE,
		rendererState,
		static_cast<LPARAM>(rendererGeneration));
}


void CVideoProcessorDlg::OnRendererDetailString(
	const CString& details, uint32_t rendererGeneration)
{
	// Will be called synchronous as a response to our calls and hence does
	// not need posting messages, we still do so to keep the pattern.

	CString* pDetailString = new CString(details);

	PostMessage(
		WM_MESSAGE_RENDERER_DETAIL_STRING,
		(WPARAM)pDetailString,
		static_cast<LPARAM>(rendererGeneration));
}


void CVideoProcessorDlg::OnRendererGraphEvent(
	long eventCode, uint32_t rendererGeneration)
{
	PostMessage(
		WM_MESSAGE_RENDERER_GRAPH_EVENT,
		static_cast<WPARAM>(eventCode),
		static_cast<LPARAM>(rendererGeneration));
}



void CVideoProcessorDlg::UpdateState()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::UpdateState()")));
	if (m_rendererRetirementPending)
	{
		TryFinalizeRendererRetirement(
			m_rendererRetirementToken, "state-reconciliation");
	}
	if (m_rendererRetirementPending)
	{
		DbgLog((LOG_TRACE, 1,
			TEXT("CVideoProcessorDlg::UpdateState(): waiting for renderer retirement")));
		return;
	}
	if (m_failedRendererRetirement)
	{
		const ULONGLONG now = GetTickCount64();
		if (now < m_failedRendererRetirementNextRetryTick)
			return;

		m_rendererRetirementPending = true;
		m_rendererRetirementRetryActive = true;
		m_rendererRetirementToken++;
		m_rendererRetirementWaitLoggedToken = 0;
		DebugLog::Log(
			"Renderer handoff restoration retry queued: generation=%u token=%llu",
			m_rendererGeneration.load(std::memory_order_acquire),
			static_cast<unsigned long long>(m_rendererRetirementToken));
		const bool queued = m_rendererRetirementService.Retire(
			std::move(m_failedRendererRetirement), m_rendererRetirementToken,
			GetSafeHwnd(), WM_MESSAGE_RENDERER_RETIRED);
		if (!queued)
			throw std::runtime_error("Renderer retirement retry service is closed");
		return;
	}
	if (m_rendererConstructionActive)
	{
		DbgLog((LOG_TRACE, 1,
			TEXT("CVideoProcessorDlg::UpdateState(): waiting for renderer construction")));
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

		CloseOwnedTopLevelWindowsForShutdown();
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
		// Persistent libplacebo entries are consumed opportunistically. Never
		// delay renderer construction for an exhaustive synthetic matrix; live
		// playback compiles only the shader it actually requires.
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

	const bool hasVideoState = m_captureDeviceVideoState != nullptr;
	const bool videoStateValid = hasVideoState &&
		m_captureDeviceVideoState->valid;
	const bool hasDisplayMode = hasVideoState &&
		m_captureDeviceVideoState->displayMode != nullptr;
	if (!ConfigurationLiveApply::HasUsableCaptureModeForAutoOffset(
			hasVideoState, videoStateValid, hasDisplayMode))
	{
		const int retainedOffset = GetTimingClockFrameOffsetMs();
		DebugLog::Log(
			"Auto frame offset deferred: capture video mode unavailable state=%d valid=%d display_mode=%d retained_ms=%d",
			hasVideoState ? 1 : 0, videoStateValid ? 1 : 0,
			hasDisplayMode ? 1 : 0, retainedOffset);
		return retainedOffset;
	}

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

	DebugLog::Log(
		"Capture stop dispatch: phase=before-stop termination=%d renderer_retired=%d",
		m_wantToTerminate ? 1 : 0,
		(!m_videoRenderer && !m_rendererRetirementPending) ? 1 : 0);
	m_captureDevice->StopCapture();
	DebugLog::Log(
		"Capture stop dispatch: phase=after-stop termination=%d",
		m_wantToTerminate ? 1 : 0);

	m_captureDeviceVideoState = nullptr;
	if (ApplicationShutdownPolicy::MayFinalizeCaptureAfterStopReturns(
		m_wantToTerminate,
		!m_videoRenderer && !m_rendererRetirementPending))
	{
		// StopCapture completed synchronously. Do not make process termination
		// depend on the optional DeckLink busy-state notification that normally
		// advances CAPTURING -> READY during a live restart.
		m_captureDeviceState = CaptureDeviceState::CAPTUREDEVICESTATE_READY;
		CaptureGUIClear();
		CaptureRemove();
		DebugLog::Log(
			"Capture stop dispatch: phase=termination-finalized notification=not-required");
		UpdateState();
		return;
	}

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
	if (m_activeOutputSweepSummaryVisible && !m_activeOutputSweepRunning)
		ClearActiveOutputSweepSummary("renderer-rebuild");
	// Stage even the first construction: renderer-owned input policy cannot be
	// represented by the legacy shared command-line defaults alone.
	const bool firstRendererConstruction =
		m_rendererGeneration.load(std::memory_order_acquire) == 0;
	if (!m_stagedConfiguration &&
		!StageSavedConfiguration("renderer-lifecycle", true))
	{
		RestoreAcceptedRendererSelectionAfterReloadFailure();
	}
	if (m_stagedConfiguration)
	{
		DebugLog::Log(
			"Renderer restart beginning: configuration=%s publication=pending",
			m_stagedConfigurationIdentity.c_str());
		PublishStagedConfiguration(true);
	}
	else
	{
		DebugLog::Log(
			"Renderer restart beginning: configuration=last-known-good publication=retained");
	}
	if (ConfigurationLiveApply::ShouldConsumeRestartForFreshRenderer(
			m_videoRenderer != nullptr, m_wantToRestartRenderer))
	{
		DebugLog::Log(
			"Renderer restart intent consumed by fresh construction: first_construction=%d",
			firstRendererConstruction ? 1 : 0);
		m_wantToRestartRenderer = false;
	}

	// Configuration staging can perform bounded file retries and renderer-rule
	// discovery. A capture callback may publish a newer source generation while
	// that UI-thread work is in progress. Never build a graph from the stale
	// state that passed UpdateState's earlier check; the posted latest-state
	// notification will call UpdateState and retry this start.
	const uint64_t latestCaptureSequence =
		m_rendererIngressState->LatestCaptureSequence();
	if (!ConfigurationLiveApply::
		MayConstructRendererAfterConfigurationBoundary(
			m_appliedCaptureVideoStateNotificationSequence,
			latestCaptureSequence))
	{
		DebugLog::Log(
			"Renderer start deferred after configuration boundary: applied_sequence=%llu latest_sequence=%llu first_construction=%d action=wait-for-latest-state",
			static_cast<unsigned long long>(
				m_appliedCaptureVideoStateNotificationSequence),
			static_cast<unsigned long long>(latestCaptureSequence),
			firstRendererConstruction ? 1 : 0);
		return;
	}
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
	// The fullscreen host itself displays its waiting surface when transitions
	// run without the optional black popup. Track the reveal obligation by the
	// exact renderer generation and target instead of popup visibility.
	m_rendererFirstFrameRevealPendingGeneration = rendererGeneration;
	m_rendererFirstFrameRevealTargetHwnd = m_rendererTargetHwnd;

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
		m_rendererConstructionActive = true;
		try
		{
			const size_t alphaQueueCapacity =
				GetRendererVideoFrameQueueSizeMax();
			m_videoRenderer = std::make_shared<LibplaceboPluginVideoRenderer>(
				*this,
				rendererGeneration,
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
			m_rendererConstructionActive = false;
			if (m_wantToRestartRenderer)
				PostMessage(WM_MESSAGE_RENDERER_INTENT_READY, 0, 0);
			m_rendererStateText.SetWindowText(
				TEXT("Started VP Renderer, waiting for image..."));
		}
		catch (const std::exception& e)
		{
			m_rendererConstructionActive = false;
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
		m_rendererConstructionActive = true;
		if (IsEqualCLSID(*rendererClSID, CLSID_MPCVR))
			m_videoRenderer = std::make_shared<DirectShowMPCVideoRenderer>(
				*this, rendererGeneration, m_rendererTargetHwnd, GetSafeHwnd(),
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
					*this, rendererGeneration, m_rendererTargetHwnd, GetSafeHwnd(),
					WM_MESSAGE_DIRECTSHOW_NOTIFICATION, timingClock,
					directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride);
		else if (m_activeRendererName.Find(TEXT("madVR")) >= 0)
			m_videoRenderer =
				std::make_shared<DirectShowGenericHDRVideoRenderer>(
					*rendererClSID, *this, rendererGeneration, m_rendererTargetHwnd,
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
					*rendererClSID, *this, rendererGeneration, m_rendererTargetHwnd,
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
		m_rendererConstructionActive = false;
		if (m_wantToRestartRenderer)
			PostMessage(WM_MESSAGE_RENDERER_INTENT_READY, 0, 0);

		m_rendererStateText.SetWindowText(TEXT("Started HDR renderer, waiting for image..."));

	}
	catch (std::runtime_error e)
	{
		m_rendererConstructionActive = false;
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
			m_rendererConstructionActive = true;
			if (IsEqualCLSID(*rendererClSID, CLSID_MPCVR))
			{
				m_videoRenderer = std::make_shared<DirectShowMPCVideoRenderer>(
					*this,
					rendererGeneration,
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
					rendererGeneration,
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
					rendererGeneration,
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
			m_rendererConstructionActive = false;
			if (m_wantToRestartRenderer)
				PostMessage(WM_MESSAGE_RENDERER_INTENT_READY, 0, 0);

			m_rendererStateText.SetWindowText(TEXT("Started, waiting for image..."));
		}
		catch (std::runtime_error e)
		{
			m_rendererConstructionActive = false;
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

	if (!m_rendererRetirementPending && !m_failedRendererRetirement)
		m_rendererState = RendererState::RENDERSTATE_UNKNOWN;

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderRemove(): End")));
}


void CVideoProcessorDlg::DestroyVideoRenderer()
{
	m_shaderLoadingWindow.Hide();
	m_shaderLoadingPopupShownTick = 0;
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
	PublishActiveProfileStatus();
	m_dropDiagnosticRenderer = nullptr;
	m_dropDiagnosticInitialized = false;
	rendererToDestroy->SetResetRequestSink({});
	RevokeRendererResetSink();

	DbgLog((LOG_TRACE, 1,
		TEXT("CVideoProcessorDlg::DestroyVideoRenderer(): Renderer detached before destruction")));
	DebugLog::Log(
		"Renderer teardown: detached renderer before destruction to block reentrant callbacks");

	// Final swapchain/device release can enter the driver. Both DirectShow and
	// VP Renderer retirement therefore use the lifecycle worker; replacement
	// construction waits for durable restoration before a successor is built.
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


void CVideoProcessorDlg::OnRendererRestartRequired(uint32_t rendererGeneration)
{
	PostMessage(
		WM_MESSAGE_RENDERER_RESTART_REQUIRED,
		0,
		static_cast<LPARAM>(rendererGeneration));
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
		m_rendererFirstFrameRevealPendingGeneration = currentGeneration;
		m_rendererFirstFrameRevealTargetHwnd = m_rendererTargetHwnd;
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
	const uint32_t currentGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	const bool rendererAvailable = m_videoRenderer != nullptr;
	const bool rendererRunning =
		m_rendererState == RendererState::RENDERSTATE_RENDERING;
	const bool liveFramePresented = rendererAvailable &&
		m_videoRenderer->HasPresentedLiveFrame();
	const bool currentFrameReady =
		ConfigurationLiveApply::ShouldRetireWaitingSurfaceAfterLiveFrame(
			generation,
			reinterpret_cast<uintptr_t>(m_rendererTargetHwnd),
			m_rendererFirstFrameRevealPendingGeneration,
			reinterpret_cast<uintptr_t>(m_rendererFirstFrameRevealTargetHwnd),
			currentGeneration,
			reinterpret_cast<uintptr_t>(m_rendererTargetHwnd),
			rendererAvailable, rendererRunning, liveFramePresented,
			resetBlocksReveal, false);
	if (generation != m_transitionGeneration || !currentFrameReady)
	{
		return;
	}
	const bool coordinatedReset = m_rendererResetTransitionActive;
	// The UI timer also probes for a first frame. Once the shield has already
	// been released there is no transition work left to perform; in particular,
	// do not synchronize DWM and log another reveal on every timer tick.
	if (!m_rendererTransitionWindow.IsVisible() &&
		!m_fullscreenRetargetPending && !coordinatedReset &&
		m_rendererFirstFrameRevealPendingGeneration != generation)
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
	m_rendererFirstFrameRevealPendingGeneration = 0;
	m_rendererFirstFrameRevealTargetHwnd = nullptr;
	HWND configurationEditor = VisibleAssociatedConfigurationEditor();
	if (!configurationEditor)
	{
		configurationEditor = FindConfigurationEditorForCurrentInstallation();
		TrackConfigurationEditor(configurationEditor);
		configurationEditor = VisibleAssociatedConfigurationEditor();
	}
	if (configurationEditor && ::IsWindowVisible(configurationEditor))
	{
		PublishConfigurationEditorPresentationTarget(configurationEditor);
	}
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
	RequestPresentationFocus("first-live-frame", generation);
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
	CRect outerBefore;
	GetWindowRect(&outerBefore);
	if (!m_noUiLayoutApplied)
	{
		m_normalUiChildVisibility.clear();
		for (CWnd* child = GetWindow(GW_CHILD); child;
			child = child->GetNextWindow())
		{
			m_normalUiChildVisibility.push_back({
				child->GetSafeHwnd(), child->IsWindowVisible() != FALSE });
		}
		m_normalUiWindowPlacement.length = sizeof(m_normalUiWindowPlacement);
		GetWindowPlacement(&m_normalUiWindowPlacement);
		m_normalUiMinDialogSize = m_minDialogSize;
		m_noUiLayoutApplied = true;
	}

	for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
	{
		if (child->GetSafeHwnd() != m_windowedVideoWindow.GetSafeHwnd())
			child->ShowWindow(SW_HIDE);
	}

	// Video Only is a presentation toggle, not a window-mode change. Keep the
	// exact outer bounds and expand only the child video surface into the client
	// area released by the hidden controls. When fullscreen is active, leave its
	// popup/renderer target untouched.
	CRect client;
	GetClientRect(&client);
	// The fullscreen checkbox records the requested presentation mode, not the
	// active renderer target.  During startup it can already be checked while
	// the renderer is still windowed, which previously left this child at its
	// old operator-preview bounds until the user manually resized the dialog.
	// Always size the Video Only host now; an active fullscreen renderer owns a
	// separate top-level target and is not affected by this child-window move.
	const auto video = NoUiLayout::ResolveVideoBounds(
		true, client.Width(), client.Height(), {});
	m_windowedVideoWindow.ShowWindow(SW_SHOW);
	m_windowedVideoWindow.MoveWindow(
		video.x, video.y, video.width, video.height, TRUE);
	RefreshPresentationLayoutAfterSessionToggle("enter");
	CRect retainedWindowRect;
	GetWindowRect(&retainedWindowRect);
	const bool boundsRetained = NoUiLayout::PreservesOuterBounds(
		{ outerBefore.left, outerBefore.top,
			outerBefore.Width(), outerBefore.Height() },
		{ retainedWindowRect.left, retainedWindowRect.top,
			retainedWindowRect.Width(), retainedWindowRect.Height() });
	DebugLog::Log(
		"Video Only layout applied: outer=%ld,%ld %ldx%ld client=%dx%d fullscreen=%d window_bounds=%s",
		retainedWindowRect.left, retainedWindowRect.top,
		retainedWindowRect.Width(), retainedWindowRect.Height(),
		client.Width(), client.Height(),
		m_rendererFullscreenCheck.GetCheck() == BST_CHECKED ? 1 : 0,
		boundsRetained ? "retained" : "changed-unexpectedly");
}


void CVideoProcessorDlg::RestoreNormalUiLayout()
{
	if (!m_noUiLayoutApplied || !GetSafeHwnd())
		return;
	CRect outerBefore;
	GetWindowRect(&outerBefore);

	const bool initializeModern =
		m_interfaceMode == ApplicationInterface::Mode::Modern &&
		!m_modernOperatorView.GetSafeHwnd();
	if (initializeModern)
	{
		m_normalUiChildVisibility.clear();
		m_noUiLayoutApplied = false;
		InitializeModernInterface(true);
		RefreshPresentationLayoutAfterSessionToggle("exit-initialize-modern");
		StartConfigurationEditorInTray();
		DebugLog::Log(
			"Video Only layout restored: initialized=modern outer=%ld,%ld %ldx%ld window_bounds=retained fullscreen=%d",
			outerBefore.left, outerBefore.top,
			outerBefore.Width(), outerBefore.Height(),
			m_rendererFullscreenCheck.GetCheck() == BST_CHECKED ? 1 : 0);
		return;
	}

	for (const ChildVisibility& child : m_normalUiChildVisibility)
	{
		if (::IsWindow(child.hwnd))
			::ShowWindow(child.hwnd, child.visible ? SW_SHOWNA : SW_HIDE);
	}
	m_minDialogSize = m_normalUiMinDialogSize;
	// Do not restore the old WINDOWPLACEMENT here. The operator may have moved
	// the Video Only window; exiting must preserve its current outer bounds too.

	if (m_interfaceMode == ApplicationInterface::Mode::Modern)
	{
		ApplyModernLayout();
		RefreshModernStatus();
	}
	else
	{
		CRect client;
		GetClientRect(&client);
		CRect videoRect = m_initialVideoWindowRect;
		videoRect.right += std::max<LONG>(0,
			static_cast<LONG>(client.Width()) - m_initialClientSize.cx);
		videoRect.bottom += std::max<LONG>(0,
			static_cast<LONG>(client.Height()) - m_initialClientSize.cy);
		m_windowedVideoWindow.MoveWindow(&videoRect, TRUE);
		RestoreFixedDialogLayout();
	}
	RefreshPresentationLayoutAfterSessionToggle("exit");

	m_normalUiChildVisibility.clear();
	m_noUiLayoutApplied = false;
	CRect retainedWindowRect;
	GetWindowRect(&retainedWindowRect);
	const bool boundsRetained = NoUiLayout::PreservesOuterBounds(
		{ outerBefore.left, outerBefore.top,
			outerBefore.Width(), outerBefore.Height() },
		{ retainedWindowRect.left, retainedWindowRect.top,
			retainedWindowRect.Width(), retainedWindowRect.Height() });
	DebugLog::Log(
		"Video Only layout restored: outer=%ld,%ld %ldx%ld window_bounds=%s fullscreen=%d",
		retainedWindowRect.left, retainedWindowRect.top,
		retainedWindowRect.Width(), retainedWindowRect.Height(),
		boundsRetained ? "retained" : "changed-unexpectedly",
		m_rendererFullscreenCheck.GetCheck() == BST_CHECKED ? 1 : 0);
	Invalidate(FALSE);
}

void CVideoProcessorDlg::RefreshPresentationLayoutAfterSessionToggle(
	const char* phase)
{
	// A dialog WM_SIZE normally performs both host placement and the renderer's
	// backend-specific presentation resize. Video Only deliberately preserves
	// the outer window, so it does not receive WM_SIZE; complete the non-graph
	// portion of that path explicitly after moving the child host.
	const bool windowedTarget =
		m_rendererTargetHwnd == m_windowedVideoWindow.GetSafeHwnd();
	const bool rendererNotified = windowedTarget && m_videoRenderer &&
		!RendererResetOperationInProgress();
	if (rendererNotified)
		m_videoRenderer->OnSize();
	if (m_windowedVideoWindow.GetSafeHwnd())
		m_windowedVideoWindow.RedrawWindow(nullptr, nullptr,
			RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	DebugLog::Log(
		"Video Only presentation refresh: phase=%s target=%p windowed_target=%d renderer_notified=%d graph_reset=0 renderer_reconstruction=0",
		phase ? phase : "unknown", m_rendererTargetHwnd,
		windowedTarget ? 1 : 0, rendererNotified ? 1 : 0);
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
	HWND configurationEditor = VisibleAssociatedConfigurationEditor();
	if (!configurationEditor)
	{
		configurationEditor = FindConfigurationEditorForCurrentInstallation();
		TrackConfigurationEditor(configurationEditor);
		configurationEditor = VisibleAssociatedConfigurationEditor();
	}
	if (configurationEditor && ::IsWindowVisible(configurationEditor))
	{
		PublishConfigurationEditorPresentationTarget(configurationEditor);
	}
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

	// One normal delayed focus pass gives the renderer and the shell time to
	// finish fullscreen creation. It is not retried or coupled to Config.
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
	if (ConfigurationLiveApply::ShouldSelectFirstDiscoveredValue(
			!m_defaultRendererName.IsEmpty(), m_rendererCombo.GetCurSel(),
			m_rendererCombo.GetCount()))
	{
		m_rendererCombo.SetCurSel(0);
		CString selected;
		m_rendererCombo.GetLBText(0, selected);
		DebugLog::Log(
			"Renderer selection omitted: using first discovered renderer '%S'",
			selected.GetString());
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

	// Legacy HDFury firmware/DeckLink combinations exposed LLDV as BT.2020 +
	// PQ without static HDR metadata. Current Vertex2 + DeckLink combinations
	// can expose the fixed custom HDR block used by the documented VP workflow:
	// mastering 0..1000 nits and MaxCLL/MaxFALL 1000. Recognize both forms so
	// Follow input (LLDV) can replace the synthetic block with VP's configured
	// values. The exact signature prevents ordinary HDR10 metadata from being
	// reinterpreted merely because it is PQ/BT.2020.
	const HDRData* rawHdrData = m_captureDeviceVideoState->hdrData.get();
	const bool hasVertex2CustomHdrSignature = rawHdrData &&
		rawHdrData->maxCll == 1000.0 &&
		rawHdrData->maxFall == 1000.0 &&
		rawHdrData->masteringDisplayMinLuminance == 0.0 &&
		rawHdrData->masteringDisplayMaxLuminance == 1000.0;
	const bool isLegacyHDFuryLLDV =
		m_captureDeviceVideoState->colorspace == ColorSpace::BT_2020 &&
		m_captureDeviceVideoState->eotf == EOTF::PQ &&
		(!rawHdrData || hasVertex2CustomHdrSignature);
	DebugLog::Log(
		"LLDV state trace: raw_eotf=%s raw_colorspace=%s raw_hdr=%d raw_values=%g/%g/%g/%g vertex2_signature=%d lldv_modes_selected=%d newlldv=%d legacy_match=%d",
		CStringA(ToString(m_captureDeviceVideoState->eotf)).GetString(),
		CStringA(ToString(m_captureDeviceVideoState->colorspace)).GetString(),
		rawHdrData ? 1 : 0,
		rawHdrData ? rawHdrData->maxCll : -1.0,
		rawHdrData ? rawHdrData->maxFall : -1.0,
		rawHdrData ? rawHdrData->masteringDisplayMinLuminance : -1.0,
		rawHdrData ? rawHdrData->masteringDisplayMaxLuminance : -1.0,
		hasVertex2CustomHdrSignature ? 1 : 0,
		IsNewLldvModeSelected() ? 1 : 0,
		m_useNewLldvHeuristic ? 1 : 0,
		isLegacyHDFuryLLDV ? 1 : 0);

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
				// Resolve against the runtime snapshot used for this effective state.
				// The cached members are retained for change detection/restart
				// scheduling, but renderer lifecycle publication can replace the
				// profile snapshot before another capture notification arrives. Reading
				// the authoritative snapshot here prevents a newly saved singleton
				// [lldv] section from falling back to the legacy hard-coded values.
				const auto activeProfileSnapshot =
					m_profileRuntime.GetSnapshot();
				const auto* activeLldv = activeProfileSnapshot ?
					&activeProfileSnapshot->lldv : nullptr;
				const double profileMaxCll =
					activeLldv && activeLldv->hasMaxCll ?
						activeLldv->maxCll : -1.0;
				const double profileMaxFall =
					activeLldv && activeLldv->hasMaxFall ?
						activeLldv->maxFall : -1.0;
				const double profileMasteringMin =
					activeLldv && activeLldv->hasMasteringMinLuminance ?
						activeLldv->masteringMinLuminance : -1.0;
				const double profileMasteringMax =
					activeLldv && activeLldv->hasMasteringMaxLuminance ?
						activeLldv->masteringMaxLuminance : -1.0;
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
					m_lldvMaxCllOverride, profileMaxCll,
					lldvDefaults.maxCll, false);
				videoState->hdrData->maxFall = resolveLldvValue(
					m_lldvMaxFallOverride, profileMaxFall,
					lldvDefaults.maxFall, false);
				videoState->hdrData->masteringDisplayMinLuminance =
					resolveLldvValue(m_lldvMasteringMinLuminanceOverride,
						profileMasteringMin,
						lldvDefaults.masteringMinLuminance, false);
				videoState->hdrData->masteringDisplayMaxLuminance =
					resolveLldvValue(m_lldvMasteringMaxLuminanceOverride,
						profileMasteringMax,
						lldvDefaults.masteringMaxLuminance, true);
				DebugLog::Log(
					"LLDV effective metadata: profile=%s command_line=%g/%g/%g/%g profile_values=%g/%g/%g/%g defaults=%g/%g/%g/%g effective=%g/%g/%g/%g",
					!activeLldv || activeLldv->profile.empty() ? "(none)" :
						activeLldv->profile.c_str(),
					m_lldvMaxCllOverride, m_lldvMaxFallOverride,
					m_lldvMasteringMinLuminanceOverride,
					m_lldvMasteringMaxLuminanceOverride,
					profileMaxCll, profileMaxFall, profileMasteringMin,
					profileMasteringMax, lldvDefaults.maxCll,
					lldvDefaults.maxFall, lldvDefaults.masteringMinLuminance,
					lldvDefaults.masteringMaxLuminance,
					videoState->hdrData->maxCll, videoState->hdrData->maxFall,
					videoState->hdrData->masteringDisplayMinLuminance,
					videoState->hdrData->masteringDisplayMaxLuminance);
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
	else if (m_profileRuntime.IsInitialized())
	{
		// Status is published independently of the renderer backend. Display and
		// viewport profiles apply to both alpha and non-alpha renderers.
		PublishActiveProfileStatus();
		if (profileRefresh.changed &&
			m_rendererState != RendererState::RENDERSTATE_STOPPING)
		{
			ApplyUnifiedProfileSnapshot(profileRefresh.snapshot, true);
			ScheduleUnifiedProfileActions(profileRefresh.actions);
		}
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
		PublishActiveProfileStatus();

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

void CVideoProcessorDlg::PublishActiveProfileStatus()
{
	const auto snapshot = m_profileRuntime.GetSnapshot();
	if (!snapshot) return;
	std::vector<CString> rendererSections;
	const bool shaderAvailable = m_videoRenderer &&
		m_videoRenderer->GetActiveShaderSections(rendererSections);
	std::vector<std::string> shaderSections;
	shaderSections.reserve(rendererSections.size());
	for (const CString& section : rendererSections)
		shaderSections.emplace_back(CStringA(section).GetString());
	const uint64_t rendererGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	const std::string sourceEotf = m_builtVideoState && m_builtVideoState->valid ?
		CStringA(ToString(m_builtVideoState->eotf)).GetString() : std::string();
	const std::string sourceColorSpace = m_builtVideoState && m_builtVideoState->valid ?
		CStringA(ToString(m_builtVideoState->colorspace)).GetString() : std::string();
	ActiveProfileStatus::Publish(GetCurrentProcessId(), snapshot->generation,
		snapshot->effectiveSelections, rendererGeneration, shaderAvailable,
		shaderSections, sourceEotf, sourceColorSpace);
}

void CVideoProcessorDlg::ApplyUnifiedProfileSnapshot(
	const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& snapshot,
	bool allowRestart, bool queueProfileRestart)
{
	if (!snapshot)
		return;
	PublishActiveProfileStatus();

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
	bool liveResetRequired = false;
	if (!m_videoRenderer->ApplyApplicationState(
		*snapshot, activeState, rendererRestartRequired, liveResetRequired))
		return;

	DebugLog::Log("Applied unified profile state: %s",
		CStringA(activeState).GetString());
	if (allowRestart && rendererRestartRequired && !queueProfileRestart)
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
	else if (allowRestart && liveResetRequired && !queueProfileRestart)
	{
		DebugLog::Log(
			"Rendering profile applied live; requesting cache-preserving queue reset");
		RequestRendererReset(RendererResetReason::ProfileChange, false, 0);
	}
	else if (allowRestart && queuePolicyChanged && !queueProfileRestart)
	{
		const bool requiresGraph = QueuePolicyApplyRequiresGraphReset(
			m_activeRendererIsDirectShow);
		DebugLog::Log(
			"Queue policy apply reset selected: backend=%s scope=%s renderer_reconstruction=0",
			m_activeRendererIsDirectShow ? "DirectShow" : "Alpha",
			requiresGraph ? "graph" : "live-queue");
		RequestRendererReset(RendererResetReason::QueueSizeChange,
			requiresGraph, 0);
	}
}


void CVideoProcessorDlg::QueueUnifiedQueueProfileRendererRestart(
	const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& snapshot,
	const std::string& source)
{
	if (!snapshot)
		return;
	const QueueProfileRestartPolicy::EnqueueResult result =
		QueueProfileRestartPolicy::Enqueue(m_queueProfileRestartRequest,
			snapshot->generation, snapshot->queue.profile, source);
	if (result == QueueProfileRestartPolicy::EnqueueResult::Ignored)
		return;
	DebugLog::Log(
		"Queue profile restart: profile=%s source=%s generation=%llu outcome=%s "
		"action=awaiting-selection-settle",
		snapshot->queue.profile.c_str(), source.c_str(),
		static_cast<unsigned long long>(snapshot->generation),
		result == QueueProfileRestartPolicy::EnqueueResult::Coalesced ?
			"coalesced" : "queued");
	KillTimer(QUEUE_PROFILE_RESTART_TIMER_ID);
	SetTimer(QUEUE_PROFILE_RESTART_TIMER_ID,
		QUEUE_PROFILE_RESTART_DEBOUNCE_MS, nullptr);
}


void CVideoProcessorDlg::DispatchQueuedQueueProfileRendererRestart()
{
	QueueProfileRestartPolicy::PendingRequest request;
	if (!QueueProfileRestartPolicy::Consume(m_queueProfileRestartRequest,
		request))
		return;
	const auto currentSnapshot = m_profileRuntime.GetSnapshot();
	const std::string profile = currentSnapshot &&
		!currentSnapshot->queue.profile.empty() ?
		currentSnapshot->queue.profile : request.profile;
	if (!m_videoRenderer)
	{
		DebugLog::Log(
			"Queue profile restart: profile=%s source=%s generation=%llu "
			"outcome=not-required action=fresh-renderer-uses-committed-profile",
			profile.c_str(), request.source.c_str(),
			static_cast<unsigned long long>(request.snapshotGeneration));
		return;
	}
	if (m_rendererState == RendererState::RENDERSTATE_FAILED)
	{
		DebugLog::Log(
			"Queue profile restart: profile=%s source=%s generation=%llu "
			"outcome=failed action=resolve-renderer-error-then-use-Restart-Renderer",
			profile.c_str(), request.source.c_str(),
			static_cast<unsigned long long>(request.snapshotGeneration));
		m_rendererStateText.SetWindowText(TEXT("Queue profile restart unavailable"));
		m_windowedVideoWindow.SetWindowText(
			TEXT("Queue profile applied. Resolve the renderer error, then use Restart Renderer."));
		return;
	}

	m_queueProfileRestartCompletionPending = true;
	m_queueProfileRestartStartingGeneration =
		m_rendererGeneration.load(std::memory_order_acquire);
	m_queueProfileRestartCompletionProfile = profile;
	m_queueProfileRestartCompletionSource = request.source;
	if (m_rendererState != RendererState::RENDERSTATE_RENDERING ||
		m_wantToRestartRenderer)
	{
		DebugLog::Log(
			"Queue profile restart: profile=%s source=%s generation=%llu "
			"outcome=coalesced-with-renderer-lifecycle state=%d restart_pending=%d",
			profile.c_str(), request.source.c_str(),
			static_cast<unsigned long long>(request.snapshotGeneration),
			static_cast<int>(m_rendererState), m_wantToRestartRenderer ? 1 : 0);
		return;
	}

	if (m_rendererFullscreenCheck.GetCheck() && m_fullScreenVideoWindow &&
		IsWindow(m_fullScreenVideoWindow->GetHWND()))
	{
		m_preserveFullscreenHostForProfileRestart = true;
		DebugLog::Log(
			"Queue profile restart: preserving fullscreen host hwnd=%p",
			m_fullScreenVideoWindow->GetHWND());
	}
	m_postRendererStartRequiresGraph = false;
	m_wantToRestartRenderer = true;
	const RendererRestartDispatch dispatch = ClassifyRendererRestartDispatch(
		m_rendererConstructionActive, m_rendererRetirementPending);
	DebugLog::Log(
		"Queue profile restart: profile=%s source=%s generation=%llu backend=%s "
		"outcome=%s action=controlled-renderer-restart",
		profile.c_str(), request.source.c_str(),
		static_cast<unsigned long long>(request.snapshotGeneration),
		m_activeRendererIsDirectShow ? "DirectShow/madVR" : "VP Renderer",
		dispatch == RendererRestartDispatch::DispatchNow ?
			"restart-requested" : "coalesced-with-lifecycle-boundary");
	if (dispatch == RendererRestartDispatch::DispatchNow)
		UpdateState();
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

	// VideoProcessor is commonly started from an elevated launcher while
	// automation clients run at normal integrity. Permit only the dedicated
	// shortcut request; ordinary cross-integrity keyboard/window messages remain
	// blocked by UIPI. The handler accepts only exact configured accelerators.
	if (!::ChangeWindowMessageFilterEx(GetSafeHwnd(),
		WM_MESSAGE_EXTERNAL_SHORTCUT, MSGFLT_ALLOW, nullptr))
	{
		DebugLog::Log(
			"External shortcut message filter unavailable: error=%lu",
			::GetLastError());
	}

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
	const std::wstring buildIdentity = BuildIdentityPolicy::Format(
		VERSION_BRANCH, VERSION_COMMIT_SHORT, VERSION_DESCRIBE);
	title.Format(_T("VideoProcessor (%s)"), buildIdentity.c_str());
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

		if (p.second == m_directShowContainerColorSpace)
			m_colorspaceContainerCombo.SetCurSel(index);
	}

	for (auto p : HDR_COLORSPACE_OPTIONS)
	{
		int index = m_hdrColorspaceCombo.AddString(p.first);
		m_hdrColorspaceCombo.SetItemData(index, (int)p.second);

		if (p.second == m_directShowHDRColorSpaceOption)
			m_hdrColorspaceCombo.SetCurSel(index);
	}

	for (auto p : HDR_LUMINANCE_OPTIONS)
	{
		int index = m_hdrLuminanceCombo.AddString(p.first);
		m_hdrLuminanceCombo.SetItemData(index, (int)p.second);

		if (p.second == m_directShowHDRLuminanceOption)
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

		if (p == m_directShowVideoConversionOverride)
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
		m_unifiedProfileShortcutKeys,
		m_configuredAccelerators);
	if (!m_accelerator)
		FatalError(TEXT("Failed to create accelerator table"));
	StartGlobalShortcutObserver();
	if (!m_hideUI && m_interfaceMode == ApplicationInterface::Mode::Modern)
		StartConfigurationEditorInTray();

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
	if (m_configurationChangedEvent)
		SetTimer(CONFIGURATION_LIVE_APPLY_TIMER_ID,
			CONFIGURATION_LIVE_APPLY_INTERVAL_MS, nullptr);
	
	// Stats overlay will be created lazily on first toggle (Ctrl+I)
	// No initialization needed here

	
	if (m_hideUI)
		ApplyNoUiLayout();
	else if (m_interfaceMode == ApplicationInterface::Mode::Modern)
		InitializeModernInterface();

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
	const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool rightAlt = (::GetKeyState(VK_RMENU) & 0x8000) != 0;
	const bool guardedShaderShortcut = m_shaderShortcutKeys.find(virtualKey) !=
		m_shaderShortcutKeys.end();
	const bool repeat = (static_cast<ULONG_PTR>(pMsg->lParam) &
		(1ull << 30)) != 0;
	if (keyDown && virtualKey == VK_SPACE && !repeat && !control && !alt &&
		!shift && m_activeOutputSweepRunning)
	{
		ToggleActiveOutputSweepPause();
		return TRUE;
	}
	if (keyUp)
	{
		for (const ACCEL& accelerator : m_configuredAccelerators)
		{
			if (accelerator.cmd == ID_COMMAND_TOGGLE_NO_UI &&
				accelerator.key == virtualKey)
			{
				m_noUiToggleShortcutLatched = false;
				break;
			}
		}
	}
	if (keyDown && repeat)
	{
		for (const ACCEL& accelerator : m_configuredAccelerators)
		{
			if (accelerator.cmd == ID_COMMAND_CAPTURE_RENDERED_OUTPUT &&
				accelerator.key == virtualKey &&
				ConfigurationLiveApply::ShortcutModifiersMatch(
					(accelerator.fVirt & FCONTROL) != 0,
					(accelerator.fVirt & FALT) != 0,
					(accelerator.fVirt & FSHIFT) != 0,
					control, alt, shift))
			{
				DebugLog::Log(
					"Rendered-output capture shortcut suppressed held-key auto-repeat");
				return TRUE;
			}
		}
	}
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
			shift ? 1 : 0, control ? 1 : 0, alt ? 1 : 0);
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
		(pMsg->wParam == 'I' || pMsg->wParam == VK_F4 ||
			pMsg->wParam == VK_RETURN);
	if (diagnosticKey)
	{
		DebugLog::Log(
			"Keyboard message: phase=pretranslate message=0x%04x vk=0x%02x ctrl=%d alt=%d right_alt=%d age_ms=%lu target=%p dialog=%p foreground=%p focus=%p renderer_state=%d generation=%u retirement_pending=%d reset_active=%d",
			pMsg->message,
			static_cast<unsigned int>(pMsg->wParam),
			control ? 1 : 0, alt ? 1 : 0, rightAlt ? 1 : 0,
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
	// Handle accelerator combinations.
	if (TranslateConfiguredAccelerator(pMsg))
	{
		if (diagnosticKey)
			DebugLog::Log("Keyboard message: phase=pretranslate result=accelerator-consumed vk=0x%02x",
				static_cast<unsigned int>(pMsg->wParam));
		return TRUE;
	}
	if (ConfigurationLiveApply::ShouldConsumeUnmatchedModifiedEnter(
		keyDown, virtualKey, control, alt, shift))
	{
		DebugLog::Log(
			"Keyboard message: phase=pretranslate result=unmatched-modified-enter-consumed vk=0x%02x ctrl=%d alt=%d right_alt=%d shift=%d",
			static_cast<unsigned int>(virtualKey), control ? 1 : 0,
			alt ? 1 : 0, rightAlt ? 1 : 0, shift ? 1 : 0);
		return TRUE;
	}
	if (diagnosticKey)
		DebugLog::Log("Keyboard message: phase=pretranslate result=not-consumed vk=0x%02x",
			static_cast<unsigned int>(pMsg->wParam));

	return CDialog::PreTranslateMessage(pMsg);
}
BOOL CVideoProcessorDlg::TranslateConfiguredAccelerator(MSG* message)
{
	if (!ConfigurationLiveApply::MayDispatchForegroundPresentationShortcut(
		m_accelerator != nullptr, m_hideUI) || !message)
		return FALSE;

	if (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
	{
		const WORD virtualKey = static_cast<WORD>(message->wParam);
		const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
		const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
		const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
		const bool rightAlt = (::GetKeyState(VK_RMENU) & 0x8000) != 0;
		const ACCEL* matchedAccelerator = nullptr;
		bool rightAltFallback = false;
		for (const ACCEL& accelerator : m_configuredAccelerators)
		{
			if (accelerator.key == virtualKey &&
				ConfigurationLiveApply::ShortcutModifiersMatch(
					(accelerator.fVirt & FCONTROL) != 0,
					(accelerator.fVirt & FALT) != 0,
					(accelerator.fVirt & FSHIFT) != 0,
					control, alt, shift))
			{
				matchedAccelerator = &accelerator;
				break;
			}
		}
		if (!matchedAccelerator && rightAlt)
		{
			for (const ACCEL& accelerator : m_configuredAccelerators)
			{
				if (accelerator.cmd == ID_COMMAND_FULLSCREEN_TOGGLE &&
					accelerator.key == virtualKey &&
					ConfigurationLiveApply::FullscreenShortcutModifiersMatch(
						(accelerator.fVirt & FCONTROL) != 0,
						(accelerator.fVirt & FALT) != 0,
						(accelerator.fVirt & FSHIFT) != 0,
						control, alt, shift, rightAlt))
				{
					matchedAccelerator = &accelerator;
					rightAltFallback = true;
					break;
				}
			}
		}
		if (matchedAccelerator)
		{
			const ACCEL& accelerator = *matchedAccelerator;

			const ULONGLONG now = GetTickCount64();
			if (ConfigurationLiveApply::IsDuplicateBackgroundShortcut(
				m_lastBackgroundShortcutCommand, accelerator.cmd,
				now - m_lastBackgroundShortcutTick,
				BACKGROUND_SHORTCUT_DUPLICATE_WINDOW_MS))
			{
				m_lastBackgroundShortcutCommand = 0;
				m_lastBackgroundShortcutTick = 0;
				DebugLog::Log(
					"Keyboard message: suppressed duplicate background command=%u",
					static_cast<unsigned int>(accelerator.cmd));
				return TRUE;
			}

			if (accelerator.cmd == ID_COMMAND_CONFIG_EDITOR)
			{
				ToggleConfigurationEditor();
				return TRUE;
			}
			if (rightAltFallback)
			{
				DebugLog::Log(
					"Keyboard message: Right Alt normalized to fullscreen shortcut command=%u",
					static_cast<unsigned int>(accelerator.cmd));
				::SendMessageW(m_hWnd, WM_COMMAND,
					MAKEWPARAM(accelerator.cmd, 1), 0);
				return TRUE;
			}
		}
	}

	return ::TranslateAccelerator(m_hWnd, m_accelerator, message);
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
				RequestRendererReset(RendererResetReason::QueueSizeChange,
					QueuePolicyApplyRequiresGraphReset(
						m_activeRendererIsDirectShow), 0);
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

void CVideoProcessorDlg::InitializeModernInterface(bool preserveWindowBounds)
{
	// The Classic resource and all of its handlers remain intact. Modern is a
	// presentation overlay created only after the existing runtime/controller
	// initialization has completed.
	const HWND videoWindow = m_windowedVideoWindow.GetSafeHwnd();
	for (HWND child = ::GetWindow(GetSafeHwnd(), GW_CHILD);
		child != nullptr; child = ::GetWindow(child, GW_HWNDNEXT))
	{
		if (child != videoWindow)
			::ShowWindow(child, SW_HIDE);
	}

	CRect desiredClient(0, 0,
		ModernOperatorLayout::DefaultClientWidth,
		ModernOperatorLayout::DefaultClientHeight);
	AdjustWindowRectEx(&desiredClient, GetStyle(), FALSE, GetExStyle());
	if (!preserveWindowBounds)
	{
		SetWindowPos(nullptr, 0, 0, desiredClient.Width(), desiredClient.Height(),
			SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER |
			SWP_FRAMECHANGED);
		CenterWindow();
	}
	m_minDialogSize = desiredClient.Size();

	if (!m_modernOperatorView.Create(this))
		FatalError(TEXT("Failed to create the Modern operator interface"));
	ApplyModernLayout();
	RefreshModernStatus();
}

void CVideoProcessorDlg::ApplyModernLayout()
{
	if (!m_modernOperatorView.GetSafeHwnd())
		return;
	CRect client;
	GetClientRect(&client);
	CClientDC screen(this);
	const auto layout = ModernOperatorLayout::Calculate(
		client.Width(), client.Height(), screen.GetDeviceCaps(LOGPIXELSX));
	m_modernOperatorView.SetWindowPos(&wndBottom, 0, 0,
		client.Width(), client.Height(), SWP_NOACTIVATE);
	m_windowedVideoWindow.SetWindowPos(&wndTop,
		layout.preview.x, layout.preview.y,
		layout.preview.width, layout.preview.height,
		SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void CVideoProcessorDlg::RefreshModernStatus()
{
	if (!m_modernOperatorView.GetSafeHwnd())
		return;
	auto text = [](CWnd& window)
	{
		CString value;
		window.GetWindowText(value);
		return value.IsEmpty() ? CString(TEXT("---")) : value;
	};
	auto selected = [](CComboBox& combo)
	{
		CString value;
		const int index = combo.GetCurSel();
		if (index >= 0)
			combo.GetLBText(index, value);
		return value.IsEmpty() ? CString(TEXT("---")) : value;
	};
	auto capturedText = [](CWnd& window)
	{
		CString value;
		window.GetWindowText(value);
		const std::wstring normalized =
			ModernOperatorStatusPolicy::NormalizeCapturedValue(
				std::wstring(value.GetString(), value.GetLength()));
		return CString(normalized.c_str());
	};
	auto compactState = [](const CString& raw)
	{
		CString normalized(raw);
		normalized.MakeLower();
		if (normalized.Find(TEXT("fail")) >= 0 ||
			normalized.Find(TEXT("error")) >= 0)
			return CString(TEXT("Failed"));
		if (normalized.Find(TEXT("rendering")) >= 0)
			return CString(TEXT("Rendering"));
		if (normalized.Find(TEXT("waiting")) >= 0 ||
			normalized.Find(TEXT("start")) >= 0)
			return CString(TEXT("Starting"));
		if (normalized.Find(TEXT("stop")) >= 0)
			return CString(TEXT("Stopping"));
		if (normalized.Find(TEXT("ready")) >= 0)
			return CString(TEXT("Ready"));
		return raw.IsEmpty() ? CString(TEXT("---")) : raw;
	};
	auto refreshRate = [](const VideoStateComPtr& videoState)
	{
		CString value(TEXT("---"));
		if (videoState && videoState->valid && videoState->displayMode)
			value.Format(TEXT("%.3f fps"),
				videoState->displayMode->RefreshRateHz());
		return value;
	};
	auto displaySummary = [](const VideoStateComPtr& videoState,
		const CString& fallback)
	{
		if (!videoState || !videoState->valid || !videoState->displayMode)
			return fallback;

		const auto& mode = videoState->displayMode;
		CString value;
		value.Format(TEXT("%ux%u%c"), mode->FrameWidth(), mode->FrameHeight(),
			mode->IsInterlaced() ? TEXT('i') : TEXT('p'));
		if (mode->FrameWidth() == 1280 && mode->FrameHeight() == 720)
			value += TEXT(" - HD");
		else if (mode->FrameWidth() == 1920 && mode->FrameHeight() == 1080)
			value += TEXT(" - Full HD");
		else if (mode->FrameWidth() == 2048 && mode->FrameHeight() == 1556)
			value += TEXT(" - 2K FullFrame");
		else if (mode->FrameWidth() == 2048 && mode->FrameHeight() == 1080)
			value += TEXT(" - 2K DCI");
		else if (mode->FrameWidth() == 3840 && mode->FrameHeight() == 2160)
			value += TEXT(" - 4K UHDTV");
		else if (mode->FrameWidth() == 4096 && mode->FrameHeight() == 2160)
			value += TEXT(" - 4K DCI");
		return value;
	};
	auto hardwareValue = [](CString value)
	{
		const int separator = value.Find(TEXT(':'));
		if (separator >= 0)
			value = value.Mid(separator + 1);
		value.Trim();
		return value.IsEmpty() ? CString(TEXT("---")) : value;
	};
	auto pcieValueBelow = [](const CString& value, int minimum)
	{
		const int firstDigit = value.FindOneOf(TEXT("0123456789"));
		return firstDigit >= 0 && _ttoi(value.Mid(firstDigit)) < minimum;
	};

	ModernOperatorStatus status;
	status.captureDevice = selected(m_captureDeviceCombo);
	status.captureState = text(m_captureDeviceStateText);
	status.inputLock = text(m_inputLockedText);
	status.inputMode = displaySummary(
		m_captureDeviceVideoState, text(m_inputDisplayModeText));
	status.inputRate = refreshRate(m_captureDeviceVideoState);
	status.inputFormat = capturedText(m_inputEncodingText);
	status.inputBitDepth = capturedText(m_inputBitDepthText);
	status.inputFrames = text(m_inputVideoFrameCountText);
	status.inputMissed = text(m_inputVideoFrameMissedText);
	status.capturedValid = text(m_videoValidText);
	status.capturedMode = displaySummary(
		m_builtVideoState, capturedText(m_videoDisplayModeText));
	status.capturedRate = refreshRate(m_builtVideoState);
	status.capturedPixelFormat = capturedText(m_videoPixelFormatText);
	status.capturedPrimaries = capturedText(m_videoColorSpaceText);
	status.capturedTransfer = capturedText(m_videoEotfText);
	status.captureLatency = text(m_inputLatencyMsText);
	for (int index = 0; index < 4 && index < m_captureDeviceOtherList.GetCount(); ++index)
	{
		CString value;
		m_captureDeviceOtherList.GetText(index, value);
		status.hardware[index] = hardwareValue(value);
	}
	if (status.hardware[1] != TEXT("---") &&
		status.hardware[1].Left(1).CompareNoCase(TEXT("x")) != 0)
		status.hardware[1] = TEXT("x") + status.hardware[1];
	status.hardwareSpeedWarning = m_alwaysWarnPci ||
		pcieValueBelow(status.hardware[0], 2);
	status.hardwareWidthWarning = m_alwaysWarnPci ||
		pcieValueBelow(status.hardware[1], 4);
	status.maxCll = text(m_hdrLuminanceMaxCll);
	status.maxFall = text(m_hdrLuminanceMaxFall);
	status.masteringMin = text(m_hdrLuminanceMasterMin);
	status.masteringMax = text(m_hdrLuminanceMasterMax);
	status.rendererName = selected(m_rendererCombo);
	status.rendererState = compactState(text(m_rendererStateText));
	const int selectedRendererIndex = m_rendererCombo.GetCurSel();
	const RendererId* selectedRenderer = selectedRendererIndex >= 0 ?
		reinterpret_cast<const RendererId*>(
			m_rendererCombo.GetItemData(selectedRendererIndex)) : nullptr;
	status.directShowRenderer = selectedRenderer &&
		selectedRenderer->backend == RendererBackend::DIRECTSHOW;
	status.vpRenderer = selectedRenderer &&
		selectedRenderer->backend == RendererBackend::LIBPLACEBO;
	if (status.directShowRenderer)
	{
		const CString prefix(TEXT("DirectShow"));
		if (status.rendererName.Left(prefix.GetLength()).CompareNoCase(prefix) == 0)
		{
			status.rendererName = status.rendererName.Mid(prefix.GetLength());
			status.rendererName.TrimLeft(TEXT(" \t-:|/"));
			if (status.rendererName.IsEmpty())
				status.rendererName = prefix;
		}
	}
	if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
		m_rendererStartTime != 0)
	{
		const DWORD elapsedMilliseconds = GetTickCount() -
			static_cast<DWORD>(m_rendererStartTime);
		const unsigned long long elapsedSeconds = elapsedMilliseconds / 1000ULL;
		status.rendererUptime.Format(TEXT("%02llu:%02llu:%02llu"),
			elapsedSeconds / 3600ULL, (elapsedSeconds / 60ULL) % 60ULL,
			elapsedSeconds % 60ULL);
	}
	if (status.directShowRenderer)
		status.rendererStartStopMethod =
			selected(m_rendererDirectShowStartStopTimeMethodCombo);
	// Queue getters enforce the renderer lifecycle contract and may only be
	// queried after the graph has reached RENDERING. The Modern view is also
	// refreshed during startup, so keep its initial placeholders until then.
	if (m_videoRenderer &&
		m_rendererState == RendererState::RENDERSTATE_RENDERING)
	{
		const size_t rawQueueSize = m_videoRenderer->GetFrameQueueSize();
		const size_t convertedQueueSize = m_videoRenderer->GetConvertedQueueSize();
		status.queueRaw.Format(TEXT("%zu"), rawQueueSize);
		status.queueConverted.Format(TEXT("%zu"), convertedQueueSize);
		status.queueTotal.Format(TEXT("%zu"),
			rawQueueSize + convertedQueueSize);
		status.queueCapacity.Format(TEXT("%zu"),
			GetRendererVideoFrameQueueSizeMax());

		status.singleQueue = status.vpRenderer;
	}
	status.dropped = text(m_rendererDroppedFrameCountText);
	status.vpLatency = text(m_rendererLatencyToVPText);
	status.ptsLead = text(m_rendererLatencyDsLeadText);
	status.outputLatency = text(m_rendererLatencyToDSText);
	// Read Alpha's presentation timing from the renderer just as the OSD does.
	// The legacy latency statics are only a display cache and can remain at their
	// placeholder values when the Modern view is active.
	if (m_videoRenderer &&
		m_rendererState == RendererState::RENDERSTATE_RENDERING)
	{
		if (status.vpRenderer)
		{
			double presentationLeadMs = 0.0;
			double captureToPresentationMs = 0.0;
			if (m_videoRenderer->GetPresentationTargetTiming(
				presentationLeadMs, captureToPresentationMs))
			{
				status.ptsLead.Format(TEXT("%.01f"), presentationLeadMs);
				status.outputLatency.Format(TEXT("%.01f"),
					captureToPresentationMs);
			}
		}
	}
	status.videoOnly = m_hideUI;
	// The Fullscreen button reflects the operator's request, not completion of
	// the asynchronous renderer transition. This confirms the selection as soon
	// as the toggle is pressed or startup fullscreen is requested.
	status.fullscreenRequested =
		m_rendererFullscreenCheck.GetCheck() == BST_CHECKED;
	m_modernOperatorView.SetStatus(status);
}

LRESULT CVideoProcessorDlg::OnMessageModernOperatorAction(WPARAM wParam, LPARAM)
{
	const auto action = static_cast<ModernOperatorAction>(wParam);
	DebugLog::Log("Modern operator action: id=%u",
		static_cast<unsigned int>(action));
	switch (action)
	{
	case ModernOperatorAction::CaptureRestart:
		OnBnClickedCaptureRestart();
		break;
	case ModernOperatorAction::RendererRestart:
		OnBnClickedRendererRestart();
		break;
	case ModernOperatorAction::QueueReset:
		OnBnClickedRendererReset();
		break;
	case ModernOperatorAction::OpenConfiguration:
		OnCommandConfigEditor();
		break;
	case ModernOperatorAction::ToggleVideoOnly:
		OnCommandToggleNoUi();
		break;
	case ModernOperatorAction::ToggleView:
		OnCommandFullScreenToggle();
		break;
	default:
		return 0;
	}
	RefreshModernStatus();
	return 0;
}


void CVideoProcessorDlg::RestoreFixedDialogLayout()
{
	if (m_hideUI || m_interfaceMode == ApplicationInterface::Mode::Modern ||
		!GetSafeHwnd() || m_fixedControlLayout.empty())
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
	else if (m_interfaceMode == ApplicationInterface::Mode::Modern &&
		m_modernOperatorView.GetSafeHwnd())
	{
		ApplyModernLayout();
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
	m_shaderLoadingWindow.UpdatePosition();

	// Some windowed DirectShow renderers finish processing WM_SIZE after this
	// handler returns.  Restore the fixed UI now and once more after that work
	// completes, without affecting the renderer graph or its media timeline.
	if (!m_hideUI && m_interfaceMode != ApplicationInterface::Mode::Modern)
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


void CVideoProcessorDlg::OnMove(int x, int y)
{
	CDialog::OnMove(x, y);
	m_shaderLoadingWindow.UpdatePosition();
}


HCURSOR CVideoProcessorDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CVideoProcessorDlg::OnGetMinMaxInfo(MINMAXINFO* minMaxInfo)
{
	CDialog::OnGetMinMaxInfo(minMaxInfo);

	if (m_hideUI) {
		// Video Only has no control chrome to preserve.  Keep it independently
		// resizable down to the practical 16:9 video minimum, while restoring the
		// larger normal-UI minimum as soon as the controls return.
		CRect minimumWindow(0, 0,
			NoUiLayout::MinimumClientWidth, NoUiLayout::MinimumClientHeight);
		AdjustWindowRectEx(&minimumWindow,
			static_cast<DWORD>(GetWindowLongPtr(GetSafeHwnd(), GWL_STYLE)),
			FALSE,
			static_cast<DWORD>(GetWindowLongPtr(GetSafeHwnd(), GWL_EXSTYLE)));
		minMaxInfo->ptMinTrackSize.x = std::max<LONG>(
			minMaxInfo->ptMinTrackSize.x,
			static_cast<LONG>(minimumWindow.Width()));
		minMaxInfo->ptMinTrackSize.y = std::max<LONG>(
			minMaxInfo->ptMinTrackSize.y,
			static_cast<LONG>(minimumWindow.Height()));
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
		DebugLog::Log(
			"Keyboard close handler: phase=retry result=advance-existing-termination");
		UpdateState();
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

void CVideoProcessorDlg::CloseOwnedTopLevelWindowsForShutdown()
{
	if (m_statsOverlay)
		m_statsOverlay->Destroy();
	if (m_fullscreenRetargetPending)
		ClearFullscreenRetarget(false);
	if (m_fullScreenVideoWindow)
		FullScreenVideoWindowDestroy();
	m_rendererTransitionWindow.Hide();
	// MFC and USER32 own combo-list, tooltip, and IME helper windows. Destroying
	// every UI-thread top-level here re-enters their lifetime management and can
	// fault in USER32. Only destroy the VP surfaces explicitly owned above.
	DebugLog::Log("Application shutdown owned surface cleanup complete");
}

void CVideoProcessorDlg::OnSysCommand(UINT command, LPARAM lParam)
{
	if (ApplicationShutdownPolicy::IsCloseSystemCommand(command))
	{
		DebugLog::Log(
			"System close routed: command=0x%04x video_only=%d fullscreen=%d",
			command, m_hideUI ? 1 : 0,
			m_rendererFullscreenCheck.GetCheck() == BST_CHECKED ? 1 : 0);
		OnClose();
		return;
	}
	CDialog::OnSysCommand(command, lParam);
}

void CVideoProcessorDlg::OnTimer(UINT_PTR nIDEvent)
{
	const ULONGLONG uiNow = GetTickCount64();
	m_lastUiMessageTick.store(uiNow, std::memory_order_release);
	if (m_configurationEditorPresentationRequired != 0 &&
		m_configurationEditorPresentationAcknowledged !=
			m_configurationEditorPresentationRequired &&
		m_configurationEditorPresentationQueuedTick != 0 &&
		uiNow - m_configurationEditorPresentationQueuedTick >= 1500 &&
		!m_configurationEditorPresentationTimeoutLogged)
	{
		// A dead or blocked Qt process must never make the MFC UI wait. Keep the
		// fullscreen close/shortcut routes independent of this advisory target
		// handoff; a later lifecycle event may safely retry the newest sequence.
		m_configurationEditorPresentationTimeoutLogged = true;
		DebugLog::Log(
			"Configuration editor presentation target acknowledgement timed out: editor=%p target=%p sequence=%u elapsed_ms=%llu state=nonblocking-safe-route-retained",
			reinterpret_cast<void*>(m_configurationEditorPresentationEditor),
			reinterpret_cast<void*>(m_configurationEditorPresentationTarget),
			m_configurationEditorPresentationRequired,
			static_cast<unsigned long long>(
				uiNow - m_configurationEditorPresentationQueuedTick));
	}
	if (m_rendererRetirementPending &&
		TryFinalizeRendererRetirement(
			m_rendererRetirementToken, "ui-timer-reconciliation"))
	{
		UpdateState();
	}
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

	if (nIDEvent == QUEUE_PROFILE_RESTART_TIMER_ID)
	{
		KillTimer(QUEUE_PROFILE_RESTART_TIMER_ID);
		DispatchQueuedQueueProfileRendererRestart();
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
		// Renderer preparation remains asynchronous and observable in logs, but
		// the operator-facing pipeline preparation popup is intentionally off.
		constexpr bool showRenderStallOverlay = false;
		CString renderStallStatus;
		const bool rendererCanReportStall =
			m_rendererState == RendererState::RENDERSTATE_STARTING ||
			m_rendererState == RendererState::RENDERSTATE_READY ||
			m_rendererState == RendererState::RENDERSTATE_RENDERING;
		const bool renderStalled =
			showRenderStallOverlay && rendererCanReportStall &&
			m_videoRenderer && !m_wantToRestartRenderer &&
			m_videoRenderer->GetRenderStallStatus(renderStallStatus);
		if (renderStalled && m_rendererTargetHwnd && GetSafeHwnd())
		{
			const bool wasVisible = m_shaderLoadingWindow.IsVisible();
			const bool shown = m_shaderLoadingWindow.Show(
				m_rendererTargetHwnd, GetSafeHwnd(), renderStallStatus);
			if (shown && !wasVisible && m_shaderLoadingWindow.IsVisible())
				m_shaderLoadingPopupShownTick = GetTickCount64();
		}
		else if (m_shaderLoadingWindow.IsVisible() &&
			(!rendererCanReportStall ||
			 GetTickCount64() - m_shaderLoadingPopupShownTick >= 300))
		{
			m_shaderLoadingWindow.Hide();
			m_shaderLoadingPopupShownTick = 0;
		}

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
	
	// Correct fullscreen placement after the shell/display settles. Renderer
	// input activation belongs to the renderer's own lifecycle and HWND thread;
	// this host-only timer must never take keyboard focus.
	if (nIDEvent == FULLSCREEN_FOCUS_TIMER_ID)
	{
		KillTimer(FULLSCREEN_FOCUS_TIMER_ID);
		HWND configurationEditor = VisibleAssociatedConfigurationEditor();
		if (!configurationEditor)
		{
			configurationEditor = FindConfigurationEditorForCurrentInstallation();
			TrackConfigurationEditor(configurationEditor);
			configurationEditor = VisibleAssociatedConfigurationEditor();
		}
		if (configurationEditor && ::IsWindowVisible(configurationEditor))
		{
			PublishConfigurationEditorPresentationTarget(configurationEditor);
			DebugLog::Log(
				"Fullscreen focus pass skipped while configuration editor is visible: owner_hierarchy=1");
			return;
		}
		if (m_fullScreenVideoWindow && IsWindow(m_fullScreenVideoWindow->GetHWND()))
		{
			DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnTimer(): FULLSCREEN_FOCUS - Verifying placement")));
			const HWND fullscreenHwnd = m_fullScreenVideoWindow->GetHWND();
			const HWND foregroundBefore = ::GetForegroundWindow();
			DWORD foregroundProcessId = 0;
			if (foregroundBefore)
				::GetWindowThreadProcessId(foregroundBefore,
					&foregroundProcessId);
			const bool mayActivate =
				ConfigurationLiveApply::MayActivateFullscreen(
					GetCurrentProcessId(), foregroundProcessId,
					foregroundBefore != nullptr);
			const HMONITOR requestedMonitor = SelectFullscreenMonitor();
			MONITORINFO monitorInfo = { sizeof(monitorInfo) };
			RECT rectBefore = {};
			::GetWindowRect(fullscreenHwnd, &rectBefore);
			const BOOL visibleBefore = ::IsWindowVisible(fullscreenHwnd);
			const BOOL iconicBefore = ::IsIconic(fullscreenHwnd);
			if (iconicBefore)
				::ShowWindow(fullscreenHwnd,
					mayActivate ? SW_RESTORE : SW_SHOWNOACTIVATE);
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
			const HWND focusBefore = ::GetFocus();
			RECT rectAfter = {};
			::GetWindowRect(fullscreenHwnd, &rectAfter);
			const HMONITOR actualMonitor = ::MonitorFromWindow(
				fullscreenHwnd, MONITOR_DEFAULTTONULL);
			DebugLog::Log(
				"Fullscreen focus timer: target=%p visible_before=%d iconic_before=%d "
				"rect_before=%ld,%ld-%ld,%ld placement=%d requested_monitor=%p "
				"actual_monitor=%p monitor_matched=%d visible_after=%d "
				"rect_after=%ld,%ld-%ld,%ld foreground_before=%p foreground_pid=%lu "
				"activation_allowed=%d focus_before=%p focus_policy=preserve "
				"foreground_after=%p focus_after=%p",
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
				foregroundProcessId,
				mayActivate ? 1 : 0,
				reinterpret_cast<void*>(focusBefore),
				reinterpret_cast<void*>(::GetForegroundWindow()),
				reinterpret_cast<void*>(::GetFocus()));
		}
		return;
	}

	if (nIDEvent == CONFIGURATION_LIVE_APPLY_TIMER_ID)
	{
		if (m_configurationChangedEvent &&
			WaitForSingleObject(m_configurationChangedEvent, 0) == WAIT_OBJECT_0)
			ApplySavedConfiguration();
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
		if (m_failedRendererRetirement)
			UpdateState();
		UpdateActiveOutputSweep(uiNow);

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
				double alphaPresentationLeadMs = 0.0;
				double alphaCaptureToPresentationMs = 0.0;
				const bool alphaPresentationKnown = renderer &&
					renderer->backend == RendererBackend::LIBPLACEBO &&
					m_videoRenderer->GetPresentationTargetTiming(
						alphaPresentationLeadMs, alphaCaptureToPresentationMs);
				if (alphaPresentationKnown)
				{
					// Match the Alpha OSD: DirectShow scheduling is not the
					// presentation authority for this renderer, but its DXGI timing
					// forecast is meaningful and available in normal playback.
					cstring.Format(_T("%.01f"), alphaPresentationLeadMs);
					m_rendererLatencyDsLeadText.SetWindowText(cstring);
					cstring.Format(_T("%.01f"), alphaCaptureToPresentationMs);
					m_rendererLatencyToDSText.SetWindowText(cstring);
				}
				else if (latencySnapshot.scheduledPresentationKnown)
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
		RefreshModernStatus();


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
	const bool nativeSweepBanner = (m_activeOutputSweepRunning ||
		m_activeOutputSweepSummaryVisible) &&
		!m_activeOutputSweepPaused && m_activeOutputSweepShowInfo && m_videoRenderer &&
		m_videoRenderer->SupportsNativeStatsOverlay();
	// Native-overlay support can appear after the renderer plugin finishes its
	// handoff. Close the legacy window on that transition as well as in the
	// immediate toggle path, otherwise both panels remain visible and the
	// legacy copy contains only the pre-handoff empty snapshot.
	if (nativeOverlay && m_statsOverlay && m_statsOverlay->IsVisible())
		m_statsOverlay->Show(false);
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
		(!m_statsOverlay->IsVisible() && !nativeOverlay && !nativeSweepBanner) ||
		!m_lastStatsData)
		return;

	// Fullscreen/windowed changes can put a no-activate layered overlay behind
	// a renderer window.  Reassert topmost only every five seconds while it is
	// visible; this is UI-only and does not touch the DirectShow graph.
	if (!nativeOverlay && m_timerSeconds % 5 == 0)
		m_statsOverlay->UpdatePosition(displayWindow ? displayWindow : GetSafeHwnd());

	StatsData stats;
	stats.outputSweep = m_activeOutputSweepStatus;
	const IVideoRenderer* const statsRenderer = m_videoRenderer.get();
	const bool sameStatsTelemetryGeneration = statsRenderer != nullptr &&
		statsRenderer == m_lastStatsTelemetryRenderer &&
		m_transitionGeneration == m_lastStatsTelemetryGeneration;

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
		stats.viewport.Format(TEXT("%S (%S, %S)"),
			profileSnapshot->viewport.profile.c_str(),
			profileSnapshot->viewport.hasScreenAspect ?
				profileSnapshot->viewport.screenAspect.Canonical().c_str() :
				"renderer native",
			profileSnapshot->viewport.verticalAlignment.c_str());
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
			m_videoRenderer->GetPresentationTimingStatus(
				stats.presentationTimingStatus);
		}
		stats.queueDroppedFrames = m_videoRenderer->DroppedFrameCount();
		if (!m_videoRenderer->GetOutputModeInfo(stats.outputMode) &&
			sameStatsTelemetryGeneration)
		{
			stats.outputMode = m_lastStatsData->outputMode;
		}
		if (!m_videoRenderer->GetDisplayLutInfo(stats.displayLut) &&
			sameStatsTelemetryGeneration)
		{
			stats.displayLut = m_lastStatsData->displayLut;
		}
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
		else if (sameStatsTelemetryGeneration)
			stats.videoConversion = m_lastStatsData->videoConversion;
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
	if (nativeSweepBanner)
	{
		std::vector<uint8_t> pixels;
		int width = 0;
		int height = 0;
		int stride = 0;
		const size_t summaryItemsPerPage = 3;
		const size_t summaryPage = m_activeOutputSweepSummaryVisible &&
			m_activeOutputSweepSummaryStartedTick != 0 ?
			static_cast<size_t>((GetTickCount64() -
				m_activeOutputSweepSummaryStartedTick) / 6000) : 0;
		const bool rendered = m_activeOutputSweepSummaryVisible ?
			m_statsOverlay->RenderSweepSummaryBgra(m_activeOutputSweepResults,
				summaryPage, summaryItemsPerPage, pixels, width, height, stride) :
			m_statsOverlay->RenderSweepBannerBgra(m_activeOutputSweepStatus,
				m_activeOutputSweepAwaitingLiveFrame || m_activeOutputSweepRestorePending ?
					SweepBannerState::Testing : m_activeOutputSweepBannerState,
				pixels, width, height, stride);
		if (rendered)
		{
			m_videoRenderer->SetNativeSweepOverlay(
				pixels.data(), pixels.size(), width, height, stride);
		}
	}

	// Save current stats for next update
	*m_lastStatsData = stats;
	m_lastStatsTelemetryRenderer = statsRenderer;
	m_lastStatsTelemetryGeneration = m_transitionGeneration;
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
	if (accepted && m_activeOutputSweepSummaryVisible && !m_activeOutputSweepRunning)
		ClearActiveOutputSweepSummary("renderer-reset");
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



