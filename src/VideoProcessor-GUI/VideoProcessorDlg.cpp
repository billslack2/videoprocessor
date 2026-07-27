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
#include <wrl/client.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

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
#include <libplacebo/LibplaceboPluginVideoRenderer.h>
#endif
#include <guid.h>
#include <ConfigFile.h>

#include <regex> // Include for regex


#include "VideoProcessorDlg.h"

namespace
{
using Microsoft::WRL::ComPtr;

const TCHAR* ToString(RendererResetReason reason)
{
	switch (reason)
	{
	case RendererResetReason::Manual: return TEXT("manual");
	case RendererResetReason::Startup: return TEXT("startup");
	case RendererResetReason::DisplayTransition: return TEXT("display-transition");
	case RendererResetReason::Resize: return TEXT("resize");
	case RendererResetReason::QueueSizeChange: return TEXT("queue-size-change");
	case RendererResetReason::TimingOffsetChange: return TEXT("timing-offset-change");
	default: return TEXT("none");
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


HACCEL CreateConfiguredAccelerators(
	std::map<WORD, CString>& shaderShortcutRules,
	std::map<WORD, CString>& displayRuleShortcutRules,
	std::map<WORD, unsigned int>& rendererShortcutIndices)
{
	shaderShortcutRules.clear();
	displayRuleShortcutRules.clear();
	rendererShortcutIndices.clear();
	ConfigFile mainConfig;
	const bool hasMainConfig = mainConfig.Load();
	ConfigFile rendererConfig;
	const bool hasRendererConfig =
		rendererConfig.Load(ConfigFile::RENDERER_FILENAME);
	std::vector<ACCEL> accelerators;
	std::set<unsigned int> bindings;

	for (const auto& definition : SHORTCUT_DEFINITIONS)
	{
		ACCEL accelerator = { static_cast<BYTE>(FVIRTKEY | definition.defaultModifiers), definition.defaultKey, definition.command };
		std::string configuredValue;
		const ConfigFile& config =
			definition.rendererSpecific ? rendererConfig : mainConfig;
		const bool hasConfig =
			definition.rendererSpecific ? hasRendererConfig : hasMainConfig;
		if (hasConfig &&
			config.TryGetString("shortcuts", definition.configKey, configuredValue))
		{
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
			if (!bindings.insert(binding).second)
			{
				DEBUGLOG("Duplicate shortcut '%s' ignored for shader rule '%s'", shortcut.c_str(), rule.c_str());
				continue;
			}

			accelerator.cmd = nextCommand;
			accelerators.push_back(accelerator);
			CString ruleName;
			ruleName.Format(TEXT("%S"), rule.c_str());
			shaderShortcutRules[nextCommand] = ruleName;
			++nextCommand;
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

	return CreateAcceleratorTable(accelerators.data(), static_cast<int>(accelerators.size()));
}

struct DisplayTimingSnapshot
{
	double refreshRateHz = 0.0;
	double advertisedRefreshRateHz = 0.0;
	double rawWaitRateHz = 0.0;
	int64_t lastVBlankQpc = 0;
	int64_t refreshPeriodQpc = 0;
	int64_t qpcFrequency = 0;
	int64_t rateMeasuredQpc = 0;
	int64_t measurementStartedQpc = 0;
	int64_t minimumWaitIntervalQpc = 0;
	int64_t maximumWaitIntervalQpc = 0;
	uint64_t intervalsObserved = 0;
	uint64_t rawWaitIntervalsObserved = 0;
	bool rateStable = false;
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
			++m_targetGeneration;
			m_rate = 0.0;
			m_rateMeasuredQpc = 0;
			m_measurementStartedQpc = 0;
			m_intervalsObserved = 0;
			m_rawWaitRate = 0.0;
			m_minimumWaitIntervalQpc = 0;
			m_maximumWaitIntervalQpc = 0;
			m_rawWaitIntervalsObserved = 0;
			m_rateStable = false;
			m_lastVBlankQpc.store(0, std::memory_order_release);
			m_refreshPeriodQpc.store(0, std::memory_order_release);
		}
		m_wake.notify_one();
	}

	void ResetMeasurement()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_targetGeneration;
			m_rate = 0.0;
			m_rateMeasuredQpc = 0;
			m_measurementStartedQpc = 0;
			m_intervalsObserved = 0;
			m_rawWaitRate = 0.0;
			m_minimumWaitIntervalQpc = 0;
			m_maximumWaitIntervalQpc = 0;
			m_rawWaitIntervalsObserved = 0;
			m_rateStable = false;
			m_lastVBlankQpc.store(0, std::memory_order_release);
			m_refreshPeriodQpc.store(0, std::memory_order_release);
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
		result.lastVBlankQpc = m_lastVBlankQpc.load(std::memory_order_acquire);
		result.refreshPeriodQpc = m_refreshPeriodQpc.load(std::memory_order_acquire);
		result.qpcFrequency = m_qpcFrequency;
		result.rateMeasuredQpc = m_rateMeasuredQpc;
		result.measurementStartedQpc = m_measurementStartedQpc;
		result.intervalsObserved = m_intervalsObserved;
		result.rawWaitRateHz = m_rawWaitRate;
		result.minimumWaitIntervalQpc = m_minimumWaitIntervalQpc;
		result.maximumWaitIntervalQpc = m_maximumWaitIntervalQpc;
		result.rawWaitIntervalsObserved = m_rawWaitIntervalsObserved;
		result.rateStable = m_rateStable;
		return result;
	}

private:
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
			uint64_t targetGeneration = 0;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_wake.wait(lock, [this] { return m_monitor != nullptr; });
				monitor = m_monitor;
				window = m_window;
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
			// Publish an early OSD estimate, but do not mark the rate safe for
			// correction until it has covered a full 30 seconds. Time, rather
			// than a fixed frame count, gives the same confidence at 24, 60, and
			// 120 Hz.
			constexpr double kInitialMeasurementSeconds = 1.0;
			constexpr double kPublishIntervalSeconds = 10.0;
			constexpr double kStableMeasurementSeconds = 30.0;
			LARGE_INTEGER first = {};
			LARGE_INTEGER last = {};
			LARGE_INTEGER previous = {};
			long double estimatedRefreshPeriodQpc = 0.0L;
			uint64_t intervals = 0;
			uint64_t rawWaitIntervals = 0;
			int64_t minimumWaitIntervalQpc = 0;
			int64_t maximumWaitIntervalQpc = 0;
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
					++rawWaitIntervals;
					if (minimumWaitIntervalQpc == 0 ||
						elapsedSincePreviousQpc < minimumWaitIntervalQpc)
					{
						minimumWaitIntervalQpc = elapsedSincePreviousQpc;
					}
					maximumWaitIntervalQpc = std::max(maximumWaitIntervalQpc,
						elapsedSincePreviousQpc);
					if (estimatedRefreshPeriodQpc <= 0.0L)
						estimatedRefreshPeriodQpc =
							static_cast<long double>(elapsedSincePreviousQpc);

					const uint64_t elapsedIntervals = std::max<uint64_t>(1,
						static_cast<uint64_t>(llround(
							static_cast<long double>(elapsedSincePreviousQpc) /
							estimatedRefreshPeriodQpc)));
					intervals += elapsedIntervals;

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

				const int64_t elapsedQpc = last.QuadPart - first.QuadPart;
				const double elapsedSeconds = intervals > 0 && elapsedQpc > 0 ?
					static_cast<double>(elapsedQpc) /
					static_cast<double>(frequency.QuadPart) : 0.0;
				const bool rateHasBeenPublished = lastPublishedQpc != 0;
				const bool publishRate = elapsedSeconds >= kInitialMeasurementSeconds &&
					(!rateHasBeenPublished ||
						(last.QuadPart - lastPublishedQpc) >=
							static_cast<int64_t>(kPublishIntervalSeconds * frequency.QuadPart));
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					if (m_targetGeneration == targetGeneration)
					{
						m_intervalsObserved = intervals;
						m_rawWaitIntervalsObserved = rawWaitIntervals;
						m_minimumWaitIntervalQpc = minimumWaitIntervalQpc;
						m_maximumWaitIntervalQpc = maximumWaitIntervalQpc;
					}
				}
				if (publishRate)
				{
					if (elapsedSeconds > 0.0)
					{
						const double rate = static_cast<double>(intervals) / elapsedSeconds;
						if (rate >= 20.0 && rate <= 120.0)
						{
							std::lock_guard<std::mutex> lock(m_mutex);
							if (m_targetGeneration == targetGeneration)
							{
								m_rate = rate;
								m_rawWaitRate = rawWaitIntervals > 0 ?
									static_cast<double>(rawWaitIntervals) / elapsedSeconds : 0.0;
								m_rateMeasuredQpc = last.QuadPart;
								if (elapsedSeconds >= kStableMeasurementSeconds)
									m_rateStable = true;
								m_refreshPeriodQpc.store(
									static_cast<int64_t>(llround(
										static_cast<double>(elapsedQpc) /
										static_cast<double>(intervals))),
									std::memory_order_release);
							}
						}
					}
					lastPublishedQpc = last.QuadPart;
					// Keep the same endpoints after stabilization. Subsequent
					// ten-second publications therefore become progressively more
					// accurate and never regress to a short-window estimate.
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
	uint64_t m_targetGeneration = 0;
	double m_rate = 0.0;
	double m_rawWaitRate = 0.0;
	int64_t m_rateMeasuredQpc = 0;
	int64_t m_measurementStartedQpc = 0;
	uint64_t m_intervalsObserved = 0;
	uint64_t m_rawWaitIntervalsObserved = 0;
	int64_t m_minimumWaitIntervalQpc = 0;
	int64_t m_maximumWaitIntervalQpc = 0;
	bool m_rateStable = false;
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
	ON_MESSAGE(WM_MESSAGE_CAPTURE_DEVICE_ERROR, &CVideoProcessorDlg::OnMessageCaptureDeviceError)
	ON_MESSAGE(WM_MESSAGE_DIRECTSHOW_NOTIFICATION, &CVideoProcessorDlg::OnMessageDirectShowNotification)
	ON_MESSAGE(WM_MESSAGE_RENDERER_STATE_CHANGE, &CVideoProcessorDlg::OnMessageRendererStateChange)
	ON_MESSAGE(WM_MESSAGE_RENDERER_DETAIL_STRING, &CVideoProcessorDlg::OnMessageRendererDetailString)

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
	ON_COMMAND(ID_COMMAND_SCREEN_PROFILE_NORMAL, &CVideoProcessorDlg::OnCommandScreenProfileNormal)
	ON_COMMAND(ID_COMMAND_SCREEN_PROFILE_SCOPE, &CVideoProcessorDlg::OnCommandScreenProfileScope)
	ON_COMMAND(ID_COMMAND_DISPLAY_RULE_AUTO, &CVideoProcessorDlg::OnCommandDisplayRuleAuto)
	ON_COMMAND_RANGE(ID_COMMAND_SHADER_RULE_FIRST, ID_COMMAND_SHADER_RULE_LAST, &CVideoProcessorDlg::OnCommandShaderRule)
	ON_COMMAND_RANGE(ID_COMMAND_DISPLAY_RULE_FIRST, ID_COMMAND_DISPLAY_RULE_LAST, &CVideoProcessorDlg::OnCommandDisplayRule)
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
	LoadDisplayRefreshRateOverrides();

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
	if (m_accelerator)
	{
		DestroyAcceleratorTable(m_accelerator);
		m_accelerator = nullptr;
	}

	for (auto& captureDevice : m_captureDevices)
		(*captureDevice).Release();

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
	m_subtitleRepositionMode = mode;
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
}

void CVideoProcessorDlg::SetQueueSize(const CString& queueSize)
{
	m_defaultQueueSize = queueSize;
	const int capacity = _ttoi(queueSize);
	if (capacity > 0)
		m_directShowQueueCapacity = static_cast<size_t>(capacity);
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
	if (percent > 0 && percent <= 100)
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


void CVideoProcessorDlg::UpdateRendererQueueControl()
{
	const bool alphaSelected = IsAlphaRendererSelected();
	if (m_queueRendererSelectionInitialized)
	{
		const size_t displayedValue =
			std::max<size_t>(1, GetRendererVideoFrameQueueSizeMax());
		if (m_queueSelectionWasAlpha)
			m_alphaQueueDesiredDepth = displayedValue;
		else
			m_directShowQueueCapacity = displayedValue;
	}

	if (alphaSelected)
	{
		const size_t configuredOverride =
			videoProcessorApp.GetAlphaQueueSizeOverride();
		if (configuredOverride > 0)
			m_alphaQueueDesiredDepth = configuredOverride;
	}

	const size_t selectedValue = alphaSelected ?
		m_alphaQueueDesiredDepth : m_directShowQueueCapacity;
	CString queueText;
	queueText.Format(TEXT("%zu"), selectedValue);
	m_rendererVideoFrameQueueSizeMaxEdit.SetWindowText(queueText);

	m_queueSelectionWasAlpha = alphaSelected;
	m_queueRendererSelectionInitialized = true;
	DebugLog::Log("%s queue control selected: value=%zu source=%s",
		alphaSelected ? "Alpha desired depth" : "DirectShow capacity",
		selectedValue,
		alphaSelected && videoProcessorApp.GetAlphaQueueSizeOverride() > 0 ?
			"alpha_queue_size" :
			(alphaSelected ? "remembered/default Alpha value" : "queue_size"));
}


void CVideoProcessorDlg::UpdateSceneCorrectionModeUi()
{
	const bool p010Selected = IsP010VideoConversionSelected();
	m_rendererSceneCorrectionModeCombo.EnableWindow(p010Selected);

	if (!p010Selected)
	{
		// Keep the configured choice visible, but do not allow it to be changed
		// until the renderer is again producing P010.  The DirectShow path also
		// independently gates Scene Detect on the actual output subtype.
		m_rendererSceneCorrectionModeCombo.SetCurSel(
			m_sceneAwareTimingCorrection ?
				(m_sceneCorrectionUpstreamSample ? 2 : 1) : 0);
		return;
	}

	m_rendererSceneCorrectionModeCombo.SetCurSel(
		m_sceneAwareTimingCorrection ? (m_sceneCorrectionUpstreamSample ? 2 : 1) : 0);
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
	m_sceneCorrectionUpstreamSample = selection == 2;
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
}


void CVideoProcessorDlg::OnRendererDirectShowStartStopTimeMethodSelected()
{
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
		FullScreenVideoWindowDestroy();
		//Sleep(1000);
		OnBnClickedRendererRestart();
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



	VideoStateComPtr videoState;
	videoState.Attach((VideoState*)wParam);

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

	UpdateState();

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnMessageCaptureDeviceVideoStateChange(): Done")));
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
	if (m_videoRenderer)
	{
		// Enhanced DirectShow event handling for MadVR changes
		// We'll intercept events before passing them to the renderer to detect important changes

		// First call the renderer to process DirectShow events and get any graph events
		HRESULT hr = m_videoRenderer->OnWindowsEvent(wParam, lParam);

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
				g_displayRefreshRateSampler->ResetMeasurement();
				const ULONGLONG now = GetTickCount64();
				if (m_rendererState == RendererState::RENDERSTATE_RENDERING &&
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
		break;

	// Renderer running, ready for frames
	case RendererState::RENDERSTATE_RENDERING:



		assert(oldRendererState == RendererState::RENDERSTATE_READY);

		m_restartQueuedBecauseEotf = false;
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
		m_deliverCaptureDataToRenderer.store(true, std::memory_order_release);
		enableButtons = true;
		m_windowedVideoWindow.ShowLogo(false);
		m_rendererStateText.SetWindowText(TEXT("Rendering"));

		m_rendererStartTime = GetTickCount();
		g_displayRefreshRateSampler->ResetMeasurement();
		if (!m_startupGraphReprimeCompleted)
		{
			m_startupGraphReprimeCompleted = true;
			RequestRendererReset(RendererResetReason::Startup, true,
				static_cast<UINT>(m_queueResetDelaySeconds * 1000));
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

	// Stopped rendering, can be cleaned up
	case RendererState::RENDERSTATE_STOPPED:

		
		assert(oldRendererState == RendererState::RENDERSTATE_STOPPING);

		m_restartQueuedBecauseEotf = false;

		RenderRemove();
		RenderGUIClear();
		m_rendererStateText.SetWindowText(TEXT(""));
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

	m_wantToRestartRenderer = true;
	UpdateState();
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
		
	m_wantToRestartRenderer = true;
	UpdateState();
	
}


void CVideoProcessorDlg::OnCommandScreenProfileNormal()
{
	if (!m_videoRenderer)
		return;

	CString activeProfile;
	if (!m_videoRenderer->SetScreenProfile(false, activeProfile))
	{
		DEBUGLOG("Normal screen profile ignored: selected renderer does not support screen profiles");
	}
}


void CVideoProcessorDlg::OnCommandScreenProfileScope()
{
	if (!m_videoRenderer)
		return;

	CString activeProfile;
	if (!m_videoRenderer->SetScreenProfile(true, activeProfile))
	{
		DEBUGLOG("Scope screen profile ignored: selected renderer does not support screen profiles");
	}
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
		m_wantToRestartRenderer = true;
		UpdateState();
	}
}


void CVideoProcessorDlg::OnCommandShaderRule(UINT commandId)
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
	DEBUGLOG("Shader rule changed to '%S'", static_cast<LPCTSTR>(activeRule));
	if (rendererRestartRequired)
	{
		DEBUGLOG("Shader rule aspect ratio changed; restarting renderer to renegotiate media type");
		m_wantToRestartRenderer = true;
		UpdateState();
	}
}


void CVideoProcessorDlg::OnCommandDisplayRule(UINT commandId)
{
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

void CVideoProcessorDlg::OnCommandToggleStatsOverlay()
{
	if (m_statsOverlay)
	{
		// Lazy creation - only create the window when first toggled
		if (!m_statsOverlay->IsCreated())
		{
			if (!m_statsOverlay->Create(this->GetSafeHwnd()))
			{
				// Creation failed, silently ignore
				return;
			}
		}
		m_statsOverlay->Toggle();
	}
	
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


void CVideoProcessorDlg::OnCaptureDeviceVideoStateChange(VideoStateComPtr videoState)
{
	// WARNING: Most likely to be called from some internal capture card thread!

	assert(videoState);

	PostMessage(
		WM_MESSAGE_CAPTURE_DEVICE_VIDEO_STATE_CHANGE,
		(WPARAM)videoState.Detach(),
		0);
}


void CVideoProcessorDlg::OnCaptureDeviceVideoFrame(VideoFrame& videoFrame)
{
	// WARNING: Most likely to be called from some internal capture card thread!

	// This is an atomic bool which is set by the main thread and used in context of the
	// capture thread which will deliver frames.
	if (m_deliverCaptureDataToRenderer.load(std::memory_order_acquire))
	{
		assert(m_captureDevice);
		assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);
		assert(m_videoRenderer);
		assert(m_rendererState == RendererState::RENDERSTATE_RENDERING);

		m_videoRenderer->OnVideoFrame(videoFrame);
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
		// If we still have a full screen window and don't want to be full screen anymore clean it up
		if (!m_rendererFullscreenCheck.GetCheck() && m_fullScreenVideoWindow)
		{
			FullScreenVideoWindowDestroy();

			m_fullScreenVideoWindow = nullptr;
		}

		// If the renderer failed we don't auto-start it again but wait for something to happen
		if (m_rendererState == RendererState::RENDERSTATE_FAILED)
			return;

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
		m_wantToRestartRenderer = false;
		DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::UpdateState(): - Asked to restart renderer")));

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

		// If we're in a known state keep current selection
		if(m_captureDeviceState != CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN &&
			captureInput.id == currentCaptureInputId)
		{
			m_captureInputCombo.SetCurSel(index);
			OnCaptureInputSelected();
		}
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

	// Update internal state before call to StartCapture as that might be synchronous
	m_captureDeviceState = CaptureDeviceState::CAPTUREDEVICESTATE_STARTING;

	m_captureDevice->StartCapture();

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

	// A newly constructed graph gets one deliberate startup re-prime. An
	// in-place Reset() does not clear this marker, which prevents a loop when
	// it reports RENDERSTATE_RENDERING again.
	m_startupGraphReprimeCompleted = false;

	int i;

	i = m_rendererCombo.GetCurSel();

	// No renderer picked yet, ignore
	if (i < 0)
		return;

	RendererId* selectedRenderer =
		reinterpret_cast<RendererId*>(m_rendererCombo.GetItemData(i));
	if (!selectedRenderer)
		return;

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
			const size_t alphaQueueOverride =
				videoProcessorApp.GetAlphaQueueSizeOverride();
			const size_t alphaDesiredDepth =
				GetRendererVideoFrameQueueSizeMax();
			if (alphaQueueOverride > 0)
			{
				CString alphaQueueText;
				alphaQueueText.Format(TEXT("%zu"), alphaDesiredDepth);
				m_rendererVideoFrameQueueSizeMaxEdit.SetWindowText(alphaQueueText);
				DebugLog::Log("Alpha queue desired depth uses configuration-only alpha_queue_size=%zu",
					alphaDesiredDepth);
			}
			m_videoRenderer = new LibplaceboPluginVideoRenderer(
				*this,
				GetRenderWindow(),
				timingClock,
				GetRendererVideoFrameUseQueue(),
				alphaDesiredDepth);

			if (m_captureDeviceVideoState)
				m_videoRenderer->OnVideoState(m_builtVideoState);

			m_videoRenderer->Build();
			// Match the DirectShow startup contract. Alpha owns its detector and
			// cadence policy inside the optional renderer, so the configured mode
			// must be forwarded before the first queued frame is accepted.
			m_videoRenderer->SetSceneAwareTimingCorrection(
				m_sceneAwareTimingCorrection);
			m_videoRenderer->Start();
			m_rendererStateText.SetWindowText(
				TEXT("Started VideoProcessor Renderer (Alpha), waiting for image..."));
		}
		catch (const std::exception& e)
		{
			DebugLog::Log("libplacebo renderer startup failed: %s", e.what());
			DestroyVideoRenderer();
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
		m_videoRenderer = new DirectShowGenericHDRVideoRenderer(
			*rendererClSID,
			*this,
			GetRenderWindow(),
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

		if (m_captureDeviceVideoState)
			m_videoRenderer->OnVideoState(m_builtVideoState);

		m_videoRenderer->Build();
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

		try
		{
			if (IsEqualCLSID(*rendererClSID, CLSID_MPCVR))
			{
				m_videoRenderer = new DirectShowMPCVideoRenderer(
					*this,
					GetRenderWindow(),
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
				m_videoRenderer = new DirectShowEnhancedVideoRenderer(
					*this,
					GetRenderWindow(),
					this->GetSafeHwnd(),
					WM_MESSAGE_DIRECTSHOW_NOTIFICATION,
					timingClock,
					directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride);
			}
			else
				m_videoRenderer = new DirectShowGenericVideoRenderer(
					*rendererClSID,
					*this,
					GetRenderWindow(),
					this->GetSafeHwnd(),
					WM_MESSAGE_DIRECTSHOW_NOTIFICATION,
					timingClock,
					directShowStartStopTimeMethod,
					GetRendererVideoFrameUseQueue(),
					GetRendererVideoFrameQueueSizeMax(),
					videoConversionOverride);

			if (!m_videoRenderer)
				FatalError(TEXT("Failed to build DirectShow Video Renderer"));

			if (m_captureDeviceVideoState)
				m_videoRenderer->OnVideoState(m_builtVideoState);

			m_videoRenderer->Build();
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

	// Cancel any pending EOTF change restart timer - a restart is already happening
	KillTimer(EOTF_CHANGE_RESTART_TIMER_ID);
	KillTimer(LLDV_CHANGE_RESTART_TIMER_ID);
	m_eotfChangeRestartCooldownSeconds = -1;
	m_lldvChangeRestartDelaySeconds = -1;
	m_lldvRestartPending = false;
	// A renderer-only restart must preserve a confirmed LLDV candidate. The
	// capture-state path clears it when the input genuinely returns to SDR.
	m_eotfCheckCooldownSeconds = 0;

	assert(m_captureDevice);
	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);

	assert(m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_RENDERING);
	assert(m_deliverCaptureDataToRenderer.load(std::memory_order_acquire));

	assert(m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);

	assert(m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_RENDERING);
	assert(m_deliverCaptureDataToRenderer.load(std::memory_order_acquire));

	// After this call no frames will ever go through to the renderer
	m_deliverCaptureDataToRenderer.store(false, std::memory_order_release);

	// Update internal state before call to StartCapture as that might be synchronous
	m_rendererState = RendererState::RENDERSTATE_STOPPING;

	m_videoRenderer->Stop();

	m_rendererStateText.SetWindowText(TEXT("Stopping"));

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderStop(): End")));
}


void CVideoProcessorDlg::RenderRemove()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderRemove(): Begin")));

	assert(m_videoRenderer);
	assert(m_rendererState == RendererState::RENDERSTATE_STOPPED);
	assert(!m_deliverCaptureDataToRenderer);

	DestroyVideoRenderer();

	m_rendererState = RendererState::RENDERSTATE_UNKNOWN;

	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::RenderRemove(): End")));
}


void CVideoProcessorDlg::DestroyVideoRenderer()
{
	if (!m_videoRenderer)
		return;

	// Releasing a windowed renderer can synchronously pump WM_PAINT and other
	// window messages.  Detach the shared pointer before invoking the destructor
	// so a reentrant handler cannot call a virtual method on an object whose
	// derived destructor has already completed.
	IVideoRenderer* rendererToDestroy = m_videoRenderer;
	m_videoRenderer = nullptr;

	DbgLog((LOG_TRACE, 1,
		TEXT("CVideoProcessorDlg::DestroyVideoRenderer(): Renderer detached before destruction")));
	DebugLog::Log(
		"Renderer teardown: detached renderer before destruction to block reentrant callbacks");

	delete rendererToDestroy;
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
	m_rendererLatencyToDSText.SetWindowText(TEXT("")) ;

	m_windowedVideoWindow.ShowLogo(true);
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

	HMONITOR hmon = MonitorFromWindow(this->GetSafeHwnd(), MONITOR_DEFAULTTONEAREST);

	m_fullScreenVideoWindow = new FullscreenVideoWindow();
	if (!m_fullScreenVideoWindow)
		FatalError(TEXT("Failed to create full screen renderer window"));
	if (m_windowedFullScreenMode == false)
		m_fullScreenVideoWindow->Create(hmon, this->GetSafeHwnd());
	if (m_windowedFullScreenMode == true)
		m_fullScreenVideoWindow->CreateWindowedFullscreen(hmon, this->GetSafeHwnd());

	SetTimer(FULLSCREEN_FOCUS_TIMER_ID, 5000, nullptr);

}


void CVideoProcessorDlg::FullScreenVideoWindowDestroy()
{
	assert(m_fullScreenVideoWindow);
	delete m_fullScreenVideoWindow;
	m_fullScreenVideoWindow = nullptr;
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


int CVideoProcessorDlg::GetTimingClockFrameOffsetMs()
{


	CString text;
	m_timingClockFrameOffsetEdit.GetWindowText(text);

	// ttoi throws non-parsed stuff away so in case there is crap set the output to the
	// used value, this way the user always knows what's going on.
	const int frameOffsetMs = _ttoi(text);
	SetTimingClockFrameOffsetMs(frameOffsetMs);

	return frameOffsetMs;
}


void CVideoProcessorDlg::SetTimingClockFrameOffsetMs(int timingClockFrameOffsetMs)
{
	CString cstring;
	cstring.Format(_T("%i"), timingClockFrameOffsetMs);
	m_timingClockFrameOffsetEdit.SetWindowText(cstring);
}


void CVideoProcessorDlg::UpdateTimingClockFrameOffset()
{
	if (m_captureDevice) 
		m_captureDevice->SetFrameOffsetMs(GetTimingClockFrameOffsetMs());

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

	//
	// Populate selection box, sorted
	//

	std::sort(rendererIds.begin(), rendererIds.end());
	for (const auto& rendererEntry : rendererIds)
	{
		CString normalizedRendererName(rendererEntry.name);
		normalizedRendererName.MakeLower();
		if (normalizedRendererName.Find(TEXT("decklink")) >= 0)
			continue;

		RendererId* id = new RendererId(rendererEntry);

		int comboIndex = m_rendererCombo.AddString(rendererEntry.name);
		m_rendererCombo.SetItemData(comboIndex, reinterpret_cast<DWORD_PTR>(id));
		DEBUGLOG("Renderer order: render.%d = %s",
			comboIndex + 1,
			rendererEntry.name.GetString());

		const bool isConfiguredRenderer =
			rendererEntry.name.CompareNoCase(m_defaultRendererName) == 0 ||
			(rendererEntry.backend == RendererBackend::LIBPLACEBO &&
			 m_defaultRendererName.CompareNoCase(TEXT("libplacebo")) == 0);
		if (isConfiguredRenderer)
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

				const bool newLldv = m_useNewLldvHeuristic;
				videoState->hdrData->maxCll = m_lldvMaxCllOverride >= 0.0
					? m_lldvMaxCllOverride : 1000.0;
				videoState->hdrData->maxFall = m_lldvMaxFallOverride >= 0.0
					? m_lldvMaxFallOverride : (newLldv ? 401.0 : 1000.0);
				videoState->hdrData->masteringDisplayMinLuminance =
					m_lldvMasteringMinLuminanceOverride >= 0.0
					? m_lldvMasteringMinLuminanceOverride : (newLldv ? 0.001 : 0.0001);
				videoState->hdrData->masteringDisplayMaxLuminance =
					m_lldvMasteringMaxLuminanceOverride > 0.0
					? m_lldvMasteringMaxLuminanceOverride : (newLldv ? 4000.0 : 1000.0);
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
	m_rendererSceneCorrectionModeCombo.AddString(TEXT("Basic"));
	m_rendererSceneCorrectionModeCombo.AddString(TEXT("Advanced"));

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
		m_displayRuleShortcutRules,
		m_rendererShortcutIndices);
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
	SetTimer(TIMER_ID_1SECOND, 1000, nullptr);
	
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
	// Handle accelerator combinations
	if (m_accelerator)
	{
		if (::TranslateAccelerator(m_hWnd, m_accelerator, pMsg))
			return TRUE;
	}

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

		if (m_videoRenderer )
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

	if (m_videoRenderer)
		m_videoRenderer->OnSize();

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

	DebugLog::Log(
		"Windows display mode changed: %d x %d, %u bits; display-rate measurement reset",
		width,
		height,
		bitsPerPixel);
	if (m_videoRenderer)
		m_videoRenderer->OnDisplayChange();
	CDialog::OnDisplayChange(bitsPerPixel, width, height);
}


void CVideoProcessorDlg::OnClose()
{
	DbgLog((LOG_TRACE, 1, TEXT("CVideoProcessorDlg::OnClose()")));

	if (m_wantToTerminate)
		return;

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
}

void CVideoProcessorDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == UI_LAYOUT_RESTORE_TIMER_ID)
	{
		KillTimer(UI_LAYOUT_RESTORE_TIMER_ID);
		RestoreFixedDialogLayout();
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
			::SetForegroundWindow(m_fullScreenVideoWindow->GetHWND());
			::SetFocus(m_fullScreenVideoWindow->GetHWND());
		}
		return;
	}

	// One-shot lifecycle reset coordinator. Queue depth monitoring never uses
	// this timer because no VP queue depth proves that madVR is unhealthy.
	if (nIDEvent == QUEUE_RESET_DELAY_TIMER_ID)
	{
		KillTimer(QUEUE_RESET_DELAY_TIMER_ID);
		ExecutePendingRendererReset();
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
			// Conditional manual shader rules remain armed. Re-evaluate their
			// stable active-picture requirement once per second; aspect changes are
			// rare and use the existing controlled renderer restart path.
			CString refreshedShaderRule;
			bool shaderRestartRequired = false;
			if (!m_wantToRestartRenderer &&
				m_videoRenderer->RefreshShaderRule(refreshedShaderRule,
					shaderRestartRequired) && shaderRestartRequired)
			{
				DEBUGLOG("Conditional shader state changed to '%S'; restarting renderer for aspect negotiation",
					static_cast<LPCTSTR>(refreshedShaderRule));
				m_wantToRestartRenderer = true;
				UpdateState();
				return;
			}

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

			cstring.Format(_T("%.01f"), m_videoRenderer->EntryLatencyMs());
			m_rendererLatencyToVPText.SetWindowText(cstring);

			cstring.Format(_T("%.01f"), m_videoRenderer->ExitLatencyMs());
			m_rendererLatencyToDSText.SetWindowText(cstring);

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
			m_rendererLatencyToDSText.SetWindowText(_T(""));
			m_rendererDroppedFrameCountText.SetWindowText(TEXT(""));
		}

		if (m_captureDeviceState == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		{
			cstring.Format(_T("%lu"), m_captureDevice->VideoFrameCapturedCount());
			m_inputVideoFrameCountText.SetWindowText(cstring);

			cstring.Format(_T("%lu"), m_captureDevice->VideoFrameMissedCount());
			m_inputVideoFrameMissedText.SetWindowText(cstring);

			cstring.Format(_T("%.01f"), m_captureDevice->HardwareLatencyMs());
			m_inputLatencyMsText.SetWindowText(cstring);
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
	const double measuredDisplayRefreshRate =
		sampledRateIsFresh ? sampledDisplayTiming.refreshRateHz : 0.0;
	const double nominalInputRefreshRate =
		m_captureDeviceVideoState && m_captureDeviceVideoState->valid &&
		m_captureDeviceVideoState->displayMode ?
			m_captureDeviceVideoState->displayMode->RefreshRateHz() : 0.0;
	double configuredDisplayRefreshRate = 0.0;
	int matchedOverrideNominalRate = 0;
	const bool displayRefreshRateOverridden = nominalInputRefreshRate > 0.0 &&
		TryGetDisplayRefreshRateOverride(nominalInputRefreshRate,
			configuredDisplayRefreshRate, matchedOverrideNominalRate);
	const double displayRefreshRate = displayRefreshRateOverridden ?
		configuredDisplayRefreshRate : measuredDisplayRefreshRate;
	const double activeTargetRefreshRate =
		GetActiveTargetRefreshRate(displayWindow);
	const std::wstring monitorDeviceName = GetMonitorDeviceName(displayWindow);
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
	const double dxgiTargetMismatchPpm =
		measuredDisplayRefreshRate > 0.0 && activeTargetRefreshRate > 0.0 ?
			(measuredDisplayRefreshRate / activeTargetRefreshRate - 1.0) * 1000000.0 : 0.0;

	// Keep a compact record of all available display-timing sources.  This is
	// diagnostic only: DXGI remains the source used by Scene Detect, while DWM
	// and the configured desktop mode let us identify driver/compositor cases
	// where its vblank wakeups are not the panel cadence.
	static double lastLoggedDxgiRate = 0.0;
	static double lastLoggedDwmPeriodRate = 0.0;
	static double lastLoggedDwmAdvertisedRate = 0.0;
	static double lastLoggedTargetRate = 0.0;
	static double lastLoggedSelectedRate = 0.0;
	static bool lastLoggedOverrideActive = false;
	static ULONGLONG lastDisplayTimingLogTick = 0;
	const ULONGLONG displayTimingLogTick = GetTickCount64();
	const auto rateChanged = [](double previous, double current) {
		return (previous <= 0.0) != (current <= 0.0) ||
			(previous > 0.0 && current > 0.0 &&
				fabs(previous - current) / previous >= 0.0001);
	};
	if (measuredDisplayRefreshRate > 0.0 || displayRefreshRateOverridden ||
		displayTiming.refreshRateHz > 0.0 ||
		activeTargetRefreshRate > 0.0)
	{
		const bool shouldLogDisplayTiming = lastDisplayTimingLogTick == 0 ||
			displayTimingLogTick - lastDisplayTimingLogTick >= 30000 ||
			rateChanged(lastLoggedDxgiRate, measuredDisplayRefreshRate) ||
			rateChanged(lastLoggedDwmPeriodRate, displayTiming.refreshRateHz) ||
			rateChanged(lastLoggedDwmAdvertisedRate,
				displayTiming.advertisedRefreshRateHz) ||
			rateChanged(lastLoggedTargetRate, activeTargetRefreshRate) ||
			rateChanged(lastLoggedSelectedRate, displayRefreshRate) ||
			lastLoggedOverrideActive != displayRefreshRateOverridden;
		if (shouldLogDisplayTiming)
		{
			DebugLog::Log(
				"Display timing sources: monitor=%ls; DXGI WaitForVBlank=%.6f Hz "
					"(fresh=%d stable=%d compensated=%llu raw=%.6f Hz rawCount=%llu "
					"rawGap=%.3f..%.3fms); DWM period=%.6f Hz advertised=%.6f Hz "
					"composition=%d result=0x%08lX; Windows target path=%.6f Hz; "
					"DXGI-target=%+.1f ppm; selected=%.6f Hz (%s%s)",
				monitorDeviceName.c_str(),
				measuredDisplayRefreshRate, sampledRateIsFresh ? 1 : 0,
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
				displayRefreshRate,
				displayRefreshRateOverridden ? "CONFIG OVERRIDE nominal=" : "measured DXGI",
				displayRefreshRateOverridden ? std::to_string(matchedOverrideNominalRate).c_str() : "");
			lastLoggedDxgiRate = measuredDisplayRefreshRate;
			lastLoggedDwmPeriodRate = displayTiming.refreshRateHz;
			lastLoggedDwmAdvertisedRate = displayTiming.advertisedRefreshRateHz;
			lastLoggedTargetRate = activeTargetRefreshRate;
			lastLoggedSelectedRate = displayRefreshRate;
			lastLoggedOverrideActive = displayRefreshRateOverridden;
			lastDisplayTimingLogTick = displayTimingLogTick;
		}
	}
	const bool sceneTimingReady =
		sampledRateIsFresh && sampledDisplayTiming.rateStable;
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

	if (!m_statsOverlay || !m_statsOverlay->IsVisible() || !m_lastStatsData)
		return;

	// Fullscreen/windowed changes can put a no-activate layered overlay behind
	// a renderer window.  Reassert topmost only every five seconds while it is
	// visible; this is UI-only and does not touch the DirectShow graph.
	if (m_timerSeconds % 5 == 0)
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
		stats.displayRefreshRate = displayRefreshRate;
		stats.displayRefreshRateOverridden = displayRefreshRateOverridden;
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
		stats.rawQueueSize = m_videoRenderer->GetFrameQueueSize();
		stats.convertedQueueSize = m_videoRenderer->GetConvertedQueueSize();
		stats.currentQueueSize = stats.rawQueueSize + stats.convertedQueueSize;
		stats.maxQueueSize = GetRendererVideoFrameQueueSizeMax();
		stats.isQueueFull = (stats.currentQueueSize >= stats.maxQueueSize);

		stats.entryLatencyMs = m_videoRenderer->EntryLatencyMs();
		stats.exitLatencyMs = m_videoRenderer->ExitLatencyMs();
		stats.queueDroppedFrames = m_videoRenderer->DroppedFrameCount();
		m_videoRenderer->GetOutputModeInfo(stats.outputMode);
		m_videoRenderer->GetDisplayLutInfo(stats.displayLut);
		stats.sceneDetectCorrectionDrops = m_videoRenderer->SceneAwareCorrectionDropCount();
		stats.sceneDetectCorrectionRepeats = m_videoRenderer->SceneAwareCorrectionRepeatCount();
		stats.sceneDetectDetected = m_videoRenderer->SceneAwareDetectedCount();
		stats.sceneTimingRatesCompatible =
			m_videoRenderer->SceneTimingRatesCompatible();
		stats.sceneCorrectionPredictionValid =
			m_videoRenderer->GetSceneTimingPrediction(
				stats.sceneSecondsUntilCorrection,
				stats.sceneSecondsUntilPlan,
				stats.sceneCorrectionAction,
				stats.sceneCorrectionPlanned);
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

	// Update overlay
	m_statsOverlay->UpdateStats(stats);

	// Save current stats for next update
	*m_lastStatsData = stats;
}

// Add this new method to the class
void CVideoProcessorDlg::RequestRendererReset(RendererResetReason reason,
	bool requiresGraph, UINT delayMs)
{
	if (!m_videoRenderer ||
		m_rendererState != RendererState::RENDERSTATE_RENDERING)
	{
		DEBUGLOG("Reset request ignored: reason=%s renderer is not rendering",
			CStringA(ToString(reason)).GetString());
		return;
	}

	// Coalesce a burst of lifecycle changes. A graph reset dominates a live
	// queue re-prime, and the most recent named reason remains in the log.
	m_pendingQueueReset = true;
	m_pendingResetRequiresGraph = m_pendingResetRequiresGraph || requiresGraph;
	m_pendingResetReason = reason;
	KillTimer(QUEUE_RESET_DELAY_TIMER_ID);

	if (delayMs == 0)
	{
		ExecutePendingRendererReset();
		return;
	}

	SetTimer(QUEUE_RESET_DELAY_TIMER_ID, delayMs, nullptr);
	DEBUGLOG("Reset scheduled: reason=%s scope=%s delay=%ums",
		CStringA(ToString(reason)).GetString(),
		m_pendingResetRequiresGraph ? "graph" : "live-queue", delayMs);
}


void CVideoProcessorDlg::ExecutePendingRendererReset()
{
	const RendererResetReason reason = m_pendingResetReason;
	const bool requiresGraph = m_pendingResetRequiresGraph;
	m_pendingQueueReset = false;
	m_pendingResetRequiresGraph = false;
	m_pendingResetReason = RendererResetReason::None;

	if (!m_videoRenderer ||
		m_rendererState != RendererState::RENDERSTATE_RENDERING)
		return;

	// A reset emits display notifications itself. Suppress only those feedback
	// events; explicit subsequent lifecycle changes will schedule a new reset.
	m_queueResetIgnoreEventsUntil = GetTickCount64() + 10000;
	DEBUGLOG("Reset executing: reason=%s scope=%s",
		CStringA(ToString(reason)).GetString(),
		requiresGraph ? "graph" : "live-queue");
	if (requiresGraph)
		m_videoRenderer->Reset();
	else
		m_videoRenderer->ResetLiveQueue();
}


void CVideoProcessorDlg::MonitorQueueHealth(size_t rawQueueSize,
	size_t convertedQueueSize, size_t queueMaxSize, uint64_t droppedFrames)
{
	m_lastQueueSize = rawQueueSize + convertedQueueSize;
	m_lastDroppedFrames = droppedFrames;
	m_consecutiveFullSeconds = 0;

	if (queueMaxSize == 0 || !GetRendererVideoFrameUseQueue() || !m_videoRenderer ||
		m_rendererState != RendererState::RENDERSTATE_RENDERING)
	{
		return;
	}

	// Raw and converted queues each have their own capacity. Do not use the
	// combined UI total here: 12 raw + 12 converted is not a 24/32 overflow.
	const size_t highWaterPercent =
		static_cast<size_t>(m_queueResetHighWaterPercent);
	const bool highWater = rawQueueSize * 100 >= queueMaxSize * highWaterPercent ||
		convertedQueueSize * 100 >= queueMaxSize * highWaterPercent;
	const ULONGLONG now = GetTickCount64();
	if (m_pendingQueueReset || now < m_queueResetIgnoreEventsUntil)
		return;

	// The renderer-start path schedules one deliberate re-prime after the
	// configured delay. Do not let the depth monitor interrupt madVR's initial
	// internal queue fill during its stabilization window.
	if (m_rendererStartTime != 0 &&
		GetTickCount() - static_cast<DWORD>(m_rendererStartTime) < 10000)
		return;

	if (!highWater)
		return;

	// No madVR quality API exists, so a high VP queue is evidence of pressure,
	// not proof that a reset will repair the downstream sink. Record it without
	// interrupting stable live HDMI playback.
	const DWORD tick = GetTickCount();
	if (m_lastQueueHealthDiagnostic == 0 ||
		tick - m_lastQueueHealthDiagnostic >= 5000)
	{
		m_lastQueueHealthDiagnostic = tick;
		DbgLog((LOG_TRACE, 1,
			TEXT("Queue high-water diagnostic (%zu/%zu raw, %zu/%zu converted); no automatic reset"),
			rawQueueSize, queueMaxSize, convertedQueueSize, queueMaxSize));
	}
}



