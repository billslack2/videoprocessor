/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <winnt.h>
#include <VideoProcessorDlg.h>
#include <VideoConversionOverride.h>
#include <DebugLog.h>
#include <ConfigFile.h>
#include <DisplayRuleExpression.h>
#include <MainConfigSchema.h>
#include <RendererProfileConfig.h>

#include "VideoProcessorApp.h"
using namespace std;


BEGIN_MESSAGE_MAP(CVideoProcessorApp, CWinAppEx)
	//ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


CVideoProcessorApp videoProcessorApp;

namespace
{
const wchar_t COMMAND_LINE_HELP[] = LR"(VideoProcessor GUI command-line help

Usage:
  VideoProcessor.exe /help
  VideoProcessor.exe help

Boolean switches:
  A bare switch enables the option. Append true or false to explicitly set it,
  for example: /scene_detect false. Explicit command-line values override the
  matching setting in VideoProcessor.cfg.

Options:
  /config <path>
      Use this VideoProcessor.cfg file. Relative paths are resolved from the
      process working directory. The explicit file must exist. --config and
      -config are also accepted.

  /fullscreen
      Start fullscreen.

  /windowedfullscreenmode
      Use windowed fullscreen mode.

  /capture_device "name"
      Select the capture device.

  /renderer "name"
      Select the renderer.

  /queue_size <positive integer>
      Set the maximum renderer frame queue size.

  /reset_after_render_restart_seconds <positive integer>
      Delay the post-start/restart queue recovery reset (default: 5 seconds).

  /reset_queue_too_large_percent <1-100>
      Queue high-water threshold for recovery (default: 75 percent).

  /scene_detect
      Lock output cadence to the measured display rate and prefer required
      whole-frame repeat/drop corrections at detected scene boundaries.
      A hard one-frame limit preserves queue and A/V alignment if no scene occurs.
      Used only with P010 output; otherwise the setting is retained but inactive.
      If omitted or disabled, the legacy timestamp/delivery path is unchanged.

  /disable_detection_features
      Force scene analysis and subtitle repositioning/OCR off and disable the
      Scene Detect selector. Intended for performance and timing isolation.

  /newlldv
      Enable the opt-in BT.2020 + SDR LLDV heuristic (requires both LLDV follow modes).

  /noui
      Hide the user interface; show video only.

  /startminimized
      Start the application minimized.

  /frame_offset <milliseconds|auto>
      Set the timing-clock frame offset in milliseconds, or enable automatic offset.

  /video_conversion V210_TO_P010
      Convert supported YUV/RGB input to P010.
      UYVY_TO_P010 is accepted as an alias.

  /container_colorspace <value>
      BT2020 | P3_D65 | P3_DCI | P3_D60 | REC709 | REC601_525 | REC601_625

  /hdr_colorspace <value>
      FOLLOW_INPUT | FOLLOW_INPUT_LLDV | FOLLOW_CONTAINER | BT2020 | P3 | REC709

  /hdr_luminance <value>
      FOLLOW_INPUT | FOLLOW_INPUT_LLDV | HDR_LUMINANCE_USER

  /renderer_start_stop_time_method <value>
      CLOCK_SMART | CLOCK_SMART2 | CLOCK_THEO | CLOCK_CLOCK | THEO_THEO |
      RATIONAL_RATIONAL | CLOCK_RATIONAL | CLOCK_NONE | THEO_NONE | NONE

  /renderer_nominal_range <value>
      FULL | LIMITED | SMALL

  /renderer_transfer_function <value>
      AUTO | PQ | REC709 | BT2020_CONST | GAMMA_1.8 | GAMMA_2.0 | GAMMA_2.2 |
      GAMMA_2.6 | GAMMA_2.8 | LINEAR_RGB | 204M | 8BIT_GAMMA_2.2 | LOG_100_1 |
      LOG_316_1 | BT2020 | HYBRID_LOG_GAMMA

  /renderer_transfer_matrix <value>
      AUTO | BT2020_10 | BT2020_12 | BT709 | BT601 | 240M | FCC | YCgCo

  /renderer_primaries <value>
      AUTO | BT2020 | DCI-P3 | BT709 | NTSC_SYSM | NTSC_SYSBG | CIE1931_ZYX | ACES

Config file:
  VideoProcessor.cfg
      Optional single configuration file for application, capture, shaders,
      renderer profiles, hotkeys, and completed-event actions. It is searched
      beside VideoProcessor.exe, then its two parent directories, with the
      working directory as a fallback. Command-line switches override matching
      config values.
)";

bool ClearCurrentConsoleLine(HANDLE output)
{
	CONSOLE_SCREEN_BUFFER_INFO screenBufferInfo = {};
	if (!GetConsoleScreenBufferInfo(output, &screenBufferInfo))
		return false;

	const COORD lineStart = { 0, screenBufferInfo.dwCursorPosition.Y };
	DWORD ignored = 0;
	FillConsoleOutputCharacterW(output, L' ', screenBufferInfo.dwSize.X, lineStart, &ignored);
	FillConsoleOutputAttribute(output, screenBufferInfo.wAttributes, screenBufferInfo.dwSize.X, lineStart, &ignored);
	SetConsoleCursorPosition(output, lineStart);

	return screenBufferInfo.dwCursorPosition.X > 0;
}

void SendConsoleEnter()
{
	HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
	bool closeInput = false;
	if (input == nullptr || input == INVALID_HANDLE_VALUE)
	{
		input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		closeInput = true;
	}

	DWORD consoleMode = 0;
	if (input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &consoleMode))
	{
		INPUT_RECORD records[2] = {};
		records[0].EventType = KEY_EVENT;
		records[0].Event.KeyEvent.bKeyDown = TRUE;
		records[0].Event.KeyEvent.wRepeatCount = 1;
		records[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
		records[0].Event.KeyEvent.wVirtualScanCode = MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
		records[0].Event.KeyEvent.uChar.UnicodeChar = L'\r';

		records[1] = records[0];
		records[1].Event.KeyEvent.bKeyDown = FALSE;

		DWORD ignored = 0;
		WriteConsoleInputW(input, records, ARRAYSIZE(records), &ignored);
	}

	if (closeInput && input != INVALID_HANDLE_VALUE)
		CloseHandle(input);
}

std::wstring StringToWideString(const std::string& value)
{
	if (value.empty())
		return {};

	const int requiredLength = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
	if (requiredLength <= 0)
		return std::wstring(value.begin(), value.end());

	std::wstring wideValue(static_cast<size_t>(requiredLength), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &wideValue[0], requiredLength);
	wideValue.resize(static_cast<size_t>(requiredLength - 1));
	return wideValue;
}

std::string NarrowCommandLineToken(const wchar_t* value)
{
	std::string result;
	for (; value != nullptr && *value != L'\0'; ++value)
		result.push_back(static_cast<char>(*value)); // Command-line switches are ASCII.
	return result;
}

bool TryGetFirstConfigString(const ConfigFile& config, const std::initializer_list<const char*> keys, std::string& value)
{
	for (const char* key : keys)
	{
		if (config.TryGetString("command_line", key, value))
			return true;
	}

	return false;
}

bool TryGetFirstConfigStringInSection(const ConfigFile& config,
	const char* section, const std::initializer_list<const char*> keys,
	std::string& value)
{
	for (const char* key : keys)
	{
		if (config.TryGetString(section, key, value))
			return true;
	}

	return false;
}

std::string ConfigLocation(const ConfigFile& config)
{
	return config.GetLoadedPath().empty() ? ConfigFile::DEFAULT_FILENAME : config.GetLoadedPath();
}

bool IsDebugLogRetentionDuplicateWarning(const std::string& warning)
{
	return warning.find("duplicate [logging] key 'debug_log_retention'") !=
			std::string::npos ||
		warning.find("duplicate section [logging]") != std::string::npos;
}

DebugLogRetention::Setting LoadDebugLogRetentionSetting()
{
	ConfigFile config;
	const bool loaded = config.Load();
	const bool duplicate = std::any_of(
		config.GetWarnings().begin(), config.GetWarnings().end(),
		IsDebugLogRetentionDuplicateWarning);
	std::string rawValue;
	const std::string* value = loaded &&
		config.TryGetString("logging", "debug_log_retention", rawValue) ?
		&rawValue : nullptr;
	const bool unreadable = !loaded && !config.GetWarnings().empty();
	return DebugLogRetention::ResolveSetting(value, duplicate, unreadable);
}

void ThrowIfConfigHasSyntaxWarnings(const ConfigFile& config)
{
	const auto& warnings = config.GetWarnings();
	const bool hasFatalWarning = std::any_of(
		warnings.begin(), warnings.end(),
		[](const std::string& warning)
		{
			return !IsDebugLogRetentionDuplicateWarning(warning);
		});
	if (!hasFatalWarning)
		return;

	std::string error = "Invalid " + ConfigLocation(config) + " syntax:";
	for (const auto& warning : warnings)
		if (!IsDebugLogRetentionDuplicateWarning(warning))
			error += "\n" + warning;

	throw std::runtime_error(error);
}

void ValidateRendererConfigRules()
{
	ConfigFile mainConfig;
	const bool hasMainConfig = mainConfig.Load();
	if (!hasMainConfig && !mainConfig.GetWarnings().empty())
		throw std::runtime_error(mainConfig.GetWarnings().front());
	if (hasMainConfig)
		ThrowIfConfigHasSyntaxWarnings(mainConfig);

	ConfigFile rendererConfig;
	if (!rendererConfig.Load(ConfigFile::RENDERER_FILENAME))
	{
		if (!rendererConfig.GetWarnings().empty())
			throw std::runtime_error(rendererConfig.GetWarnings().front());
		return; // The alpha renderer configuration is optional.
	}

	ThrowIfConfigHasSyntaxWarnings(rendererConfig);
	std::string error;
	if (!DisplayRuleExpression::ValidateConfig(rendererConfig, error))
	{
		throw std::runtime_error("Invalid " + ConfigLocation(rendererConfig) +
			" [display_rules] configuration: " + error);
	}
	RendererProfileConfig::Model unifiedModel;
	if (!RendererProfileConfig::Read(rendererConfig, unifiedModel, error))
	{
		throw std::runtime_error("Invalid " + ConfigLocation(rendererConfig) +
			" unified renderer configuration: " + error);
	}
	for (const std::string& warning : unifiedModel.warnings)
		DebugLog::Log("configuration migration warning: %s", warning.c_str());

	auto validateOwnedSections = [](const ConfigFile& config)
	{
		for (const std::string& section : config.GetSectionNames())
			if (!MainConfigSchema::OwnsSection(section) &&
				!RendererProfileConfig::OwnsSection(section))
			{
				throw std::runtime_error(
					"Invalid " + ConfigLocation(config) +
					" unknown configuration section [" + section + "]");
			}
	};
	if (hasMainConfig)
		validateOwnedSections(mainConfig);
	validateOwnedSections(rendererConfig);

	const bool compatibilityOverride = !hasMainConfig ||
		_stricmp(mainConfig.GetLoadedPath().c_str(),
			rendererConfig.GetLoadedPath().c_str()) != 0;
	if (compatibilityOverride &&
		RendererProfileConfig::IsUnified(rendererConfig))
		for (const std::string& section : rendererConfig.GetSectionNames())
			if (MainConfigSchema::OwnsSection(section))
			{
				throw std::runtime_error(
					"Invalid renderer override " + ConfigLocation(rendererConfig) +
					" contains application section [" + section + "]");
			}
	const std::string statePath =
		RendererProfileConfig::IsUnified(rendererConfig) ?
		RendererProfileConfig::StatePath(rendererConfig) : "(legacy)";
	DebugLog::Log(
		"configuration paths: primary=%s renderer=%s vr_override=%d state=%s",
		hasMainConfig ? mainConfig.GetLoadedPath().c_str() : "(none)",
		rendererConfig.GetLoadedPath().c_str(),
		compatibilityOverride ? 1 : 0,
		statePath.c_str());
}

void ValidateCommandLineConfigKeys(const ConfigFile& config)
{
	std::string error;
	if (!MainConfigSchema::Validate(config, error))
		throw std::runtime_error(
			"Invalid " + ConfigLocation(config) + " " + error);
}

bool TryGetFirstConfigBool(const ConfigFile& config, const std::initializer_list<const char*> keys, bool& value)
{
	for (const char* key : keys)
	{
		std::string rawValue;
		if (!config.TryGetString("command_line", key, rawValue))
			continue;

		if (config.TryGetBool("command_line", key, value))
			return true;

		throw std::runtime_error("Invalid boolean value in " + ConfigLocation(config) +
			" [command_line] for " + key + ": " + rawValue +
			" (expected true/false, yes/no, on/off, or 1/0)");
	}

	return false;
}

void AppendConfigBoolOption(std::vector<std::wstring>& arguments, const ConfigFile& config, const std::initializer_list<const char*> keys, const wchar_t* commandLineSwitch)
{
	bool enabled = false;
	if (TryGetFirstConfigBool(config, keys, enabled) && enabled)
		arguments.emplace_back(commandLineSwitch);
}

void AppendConfigStringOption(std::vector<std::wstring>& arguments, const ConfigFile& config, const std::initializer_list<const char*> keys, const wchar_t* commandLineSwitch)
{
	std::string value;
	if (TryGetFirstConfigString(config, keys, value) && !value.empty())
	{
		arguments.emplace_back(commandLineSwitch);
		arguments.emplace_back(StringToWideString(value));
	}
}

void AppendConfigStringOptionInSection(std::vector<std::wstring>& arguments,
	const ConfigFile& config, const char* section,
	const std::initializer_list<const char*> keys, const wchar_t* commandLineSwitch)
{
	std::string value;
	if (TryGetFirstConfigStringInSection(config, section, keys, value) && !value.empty())
	{
		arguments.emplace_back(commandLineSwitch);
		arguments.emplace_back(StringToWideString(value));
	}
}

std::vector<std::wstring> LoadConfiguredCommandLineArguments()
{
	ConfigFile config;
	std::vector<std::wstring> arguments;
	if (!config.Load())
	{
		if (!config.GetWarnings().empty())
			throw std::runtime_error(config.GetWarnings().front());
		return arguments;
	}

	DbgLog((LOG_TRACE, 1, TEXT("VideoProcessor: Loading configuration from %S"), config.GetLoadedPath().c_str()));
	ThrowIfConfigHasSyntaxWarnings(config);
	ValidateCommandLineConfigKeys(config);

	// Compatibility only: startup priming is now automatic and is no longer a
	// normal user-facing control. Existing files retain their prior behavior.
	std::string startupPrerollFrames;
	const bool legacyStartupConfigured = config.TryGetString(
		"queue", "startup_preroll_frames", startupPrerollFrames);
	if (legacyStartupConfigured)
	{
		const size_t frames = static_cast<size_t>(
			std::stoul(startupPrerollFrames));
		videoProcessorApp.SetQueueStartupPrerollFrames(frames);
		DbgLog((LOG_TRACE, 1,
			TEXT("VideoProcessor: Using legacy [queue] startup_preroll_frames=%zu"),
			frames));
	}

	std::string targetFrames;
	if (config.TryGetString("queue", "target_frames", targetFrames) ||
		config.TryGetString("queue", "steady_reserve_frames", targetFrames))
	{
		const size_t frames = static_cast<size_t>(std::stoul(targetFrames));
		videoProcessorApp.SetQueueSteadyReserveFrames(frames);
		DbgLog((LOG_TRACE, 1,
			TEXT("VideoProcessor: Using [queue] target_frames=%zu"),
			frames));
	}

	std::string leadFrames;
	if (config.TryGetString("queue", "lead_frames", leadFrames) ||
		config.TryGetString(
			"directshow", "presentation_lead_frames", leadFrames))
	{
		const size_t frames = static_cast<size_t>(std::stoul(leadFrames));
		videoProcessorApp.SetPresentationLeadFrames(frames);
		DbgLog((LOG_TRACE, 1,
			TEXT("VideoProcessor: Using [queue] lead_frames=%zu"), frames));
	}
	DbgLog((LOG_TRACE, 1,
		TEXT("VideoProcessor: Effective queue policy lead_frames=%zu target_frames=%zu startup_preroll=%s"),
		videoProcessorApp.GetPresentationLeadFrames(),
		videoProcessorApp.GetQueueSteadyReserveFrames(),
		legacyStartupConfigured ? TEXT("legacy-configured") : TEXT("automatic")));

	if (config.HasSection("command_line"))
	{
	AppendConfigBoolOption(arguments, config, { "fullscreen" }, L"/fullscreen");
	AppendConfigBoolOption(arguments, config, { "windowedfullscreenmode", "windowed_fullscreen_mode" }, L"/windowedfullscreenmode");
	AppendConfigStringOption(arguments, config, { "renderer" }, L"/renderer");
	AppendConfigStringOption(arguments, config, { "queue_size" }, L"/queue_size");
	AppendConfigStringOption(arguments, config, { "capture_device" }, L"/capture_device");
	AppendConfigStringOption(arguments, config, { "frame_offset" }, L"/frame_offset");
	AppendConfigStringOption(arguments, config, { "video_conversion" }, L"/video_conversion");
	AppendConfigStringOption(arguments, config, { "container_colorspace" }, L"/container_colorspace");
	AppendConfigStringOption(arguments, config, { "hdr_colorspace" }, L"/hdr_colorspace");
	AppendConfigStringOption(arguments, config, { "hdr_luminance" }, L"/hdr_luminance");
	AppendConfigStringOption(arguments, config, { "renderer_start_stop_time_method" }, L"/renderer_start_stop_time_method");
	AppendConfigStringOption(arguments, config, { "renderer_nominal_range" }, L"/renderer_nominal_range");
	AppendConfigStringOption(arguments, config, { "renderer_transfer_function" }, L"/renderer_transfer_function");
	AppendConfigStringOption(arguments, config, { "renderer_transfer_matrix" }, L"/renderer_transfer_matrix");
	AppendConfigStringOption(arguments, config, { "renderer_primaries" }, L"/renderer_primaries");
	AppendConfigBoolOption(arguments, config, { "scene_detect", "scene" }, L"/scene_detect");
	AppendConfigBoolOption(arguments, config,
		{ "disable_detection_features" }, L"/disable_detection_features");
	// Retain the old spelling as a compatibility alias.  The clearer Boolean
	// override below is appended last and therefore takes precedence if both
	// are present in a configuration file.
	AppendConfigStringOption(arguments, config, { "scene_correction_mode" }, L"/scene_correction_mode");
	// DirectShow uses the advanced upstream-sample correction by default.  This
	// config-only switch preserves a deliberate Basic override without exposing
	// renderer implementation details in the UI.
	AppendConfigBoolOption(arguments, config, { "scene_correction_basic" }, L"/scene_correction_basic");
	AppendConfigStringOption(arguments, config,
		{ "subtitle_reposition" }, L"/subtitle_reposition");
	AppendConfigBoolOption(arguments, config, { "newlldv", "new_lldv" }, L"/newlldv");
	AppendConfigBoolOption(arguments, config, { "noui", "no_ui" }, L"/noui");
	AppendConfigBoolOption(arguments, config, { "startminimized", "start_minimized" }, L"/startminimized");
	}

	AppendConfigStringOptionInSection(arguments, config, "queue_recovery",
		{ "reset_after_render_restart_seconds" },
		L"/reset_after_render_restart_seconds");
	AppendConfigStringOptionInSection(arguments, config, "queue_recovery",
		{ "reset_queue_too_large_percent" },
		L"/reset_queue_too_large_percent");
	AppendConfigStringOptionInSection(arguments, config, "lldv",
		{ "max_cll" }, L"/lldv_max_cll");
	AppendConfigStringOptionInSection(arguments, config, "lldv",
		{ "max_fall" }, L"/lldv_max_fall");
	AppendConfigStringOptionInSection(arguments, config, "lldv",
		{ "mastering_min_luminance" }, L"/lldv_mastering_min_luminance");
	AppendConfigStringOptionInSection(arguments, config, "lldv",
		{ "mastering_max_luminance" }, L"/lldv_mastering_max_luminance");

	return arguments;
}

bool IsHelpArgument(const wchar_t* argument)
{
	return argument != nullptr &&
		(_wcsicmp(argument, L"/help") == 0 ||
		 _wcsicmp(argument, L"help") == 0 ||
		 _wcsicmp(argument, L"-help") == 0 ||
		 _wcsicmp(argument, L"--help") == 0 ||
		 _wcsicmp(argument, L"/?") == 0 ||
		 _wcsicmp(argument, L"?") == 0);
}

bool IsCommandLineSwitch(const wchar_t* argument)
{
	return argument != nullptr && (argument[0] == L'/' || argument[0] == L'-');
}

bool IsCommandLineOption(const wchar_t* argument, const wchar_t* option)
{
	return argument != nullptr && _wcsicmp(argument, option) == 0;
}

bool IsConfigSelectionArgument(const wchar_t* argument)
{
	if (argument == nullptr)
		return false;

	const wchar_t* name = argument;
	while (*name == L'/' || *name == L'-')
		++name;

	return _wcsicmp(name, L"config") == 0 ||
		_wcsicmp(name, L"vr_config") == 0 ||
		_wcsnicmp(name, L"config=", 7) == 0 ||
		_wcsnicmp(name, L"vr_config=", 10) == 0;
}

bool TryParseBooleanArgument(const wchar_t* argument, bool& value)
{
	if (argument == nullptr)
		return false;

	if (_wcsicmp(argument, L"true") == 0 || _wcsicmp(argument, L"yes") == 0 ||
		_wcsicmp(argument, L"on") == 0 || wcscmp(argument, L"1") == 0)
	{
		value = true;
		return true;
	}

	if (_wcsicmp(argument, L"false") == 0 || _wcsicmp(argument, L"no") == 0 ||
		_wcsicmp(argument, L"off") == 0 || wcscmp(argument, L"0") == 0)
	{
		value = false;
		return true;
	}

	return false;
}

bool TryParseSubtitleRepositionMode(
	const wchar_t* argument,
	SubtitleRepositionMode& mode)
{
	if (argument == nullptr)
		return false;

	bool booleanValue = false;
	if (TryParseBooleanArgument(argument, booleanValue))
	{
		mode = booleanValue ?
			SubtitleRepositionMode::BASIC :
			SubtitleRepositionMode::DISABLED;
		return true;
	}
	if (_wcsicmp(argument, L"basic") == 0)
	{
		mode = SubtitleRepositionMode::BASIC;
		return true;
	}
	if (_wcsicmp(argument, L"advanced") == 0)
	{
		mode = SubtitleRepositionMode::ADVANCED;
		return true;
	}
	return false;
}

bool ReadBooleanOption(const wchar_t* const* arguments, int& index, int argumentCount,
	const wchar_t* option, bool& value)
{
	if (!IsCommandLineOption(arguments[index], option))
		return false;

	value = true;
	if (index + 1 < argumentCount && !IsCommandLineSwitch(arguments[index + 1]))
	{
		if (!TryParseBooleanArgument(arguments[index + 1], value))
			throw std::runtime_error("Invalid boolean value for " +
				NarrowCommandLineToken(option) +
				" (expected true/false, yes/no, on/off, or 1/0)");
		++index;
	}

	return true;
}

bool IsPositiveInteger(const wchar_t* value)
{
	if (value == nullptr || value[0] == L'\0')
		return false;

	for (const wchar_t* current = value; *current != L'\0'; ++current)
	{
		if (!iswdigit(*current))
			return false;
	}

	errno = 0;
	const unsigned long long number = wcstoull(value, nullptr, 10);
	return errno == 0 && number > 0 && number <= static_cast<unsigned long long>(INT_MAX);
}

bool IsNonNegativeInteger(const wchar_t* value)
{
	if (value == nullptr || value[0] == L'\0')
		return false;

	for (const wchar_t* current = value; *current != L'\0'; ++current)
	{
		if (!iswdigit(*current))
			return false;
	}

	errno = 0;
	const unsigned long long number = wcstoull(value, nullptr, 10);
	return errno == 0 && number <= static_cast<unsigned long long>(INT_MAX);
}

bool TryParseFiniteNonNegativeDouble(const wchar_t* value, double& parsedValue)
{
	if (value == nullptr || value[0] == L'\0')
		return false;

	wchar_t* end = nullptr;
	const double parsed = wcstod(value, &end);
	if (end == value || *end != L'\0' || !std::isfinite(parsed) || parsed < 0.0)
		return false;

	parsedValue = parsed;
	return true;
}

bool RequiresCommandLineValue(const wchar_t* argument)
{
	return IsCommandLineOption(argument, L"/renderer") ||
		IsCommandLineOption(argument, L"/queue_size") ||
		IsCommandLineOption(argument, L"/reset_after_render_restart_seconds") ||
		IsCommandLineOption(argument, L"/reset_queue_too_large_percent") ||
		IsCommandLineOption(argument, L"/lldv_max_cll") ||
		IsCommandLineOption(argument, L"/lldv_max_fall") ||
		IsCommandLineOption(argument, L"/lldv_mastering_min_luminance") ||
		IsCommandLineOption(argument, L"/lldv_mastering_max_luminance") ||
		IsCommandLineOption(argument, L"/capture_device") ||
		IsCommandLineOption(argument, L"/frame_offset") ||
		IsCommandLineOption(argument, L"/video_conversion") ||
		IsCommandLineOption(argument, L"/container_colorspace") ||
		IsCommandLineOption(argument, L"/hdr_colorspace") ||
		IsCommandLineOption(argument, L"/hdr_luminance") ||
		IsCommandLineOption(argument, L"/renderer_start_stop_time_method") ||
		IsCommandLineOption(argument, L"/scene_correction_basic") ||
		IsCommandLineOption(argument, L"/scene_correction_mode") ||
		IsCommandLineOption(argument, L"/renderer_nominal_range") ||
		IsCommandLineOption(argument, L"/renderer_transfer_function") ||
		IsCommandLineOption(argument, L"/renderer_transfer_matrix") ||
		IsCommandLineOption(argument, L"/renderer_primaries");
}

bool HasCaseInsensitiveValue(const wchar_t* argument)
{
	return IsCommandLineOption(argument, L"/frame_offset") ||
		IsCommandLineOption(argument, L"/video_conversion") ||
		IsCommandLineOption(argument, L"/container_colorspace") ||
		IsCommandLineOption(argument, L"/hdr_colorspace") ||
		IsCommandLineOption(argument, L"/hdr_luminance") ||
		IsCommandLineOption(argument, L"/renderer_start_stop_time_method") ||
		IsCommandLineOption(argument, L"/scene_correction_mode") ||
		IsCommandLineOption(argument, L"/renderer_nominal_range") ||
		IsCommandLineOption(argument, L"/renderer_transfer_function") ||
		IsCommandLineOption(argument, L"/renderer_transfer_matrix") ||
		IsCommandLineOption(argument, L"/renderer_primaries");
}

bool IsBooleanCommandLineOption(const wchar_t* argument)
{
	return IsCommandLineOption(argument, L"/fullscreen") ||
		IsCommandLineOption(argument, L"/windowedfullscreenmode") ||
		IsCommandLineOption(argument, L"/noui") ||
		IsCommandLineOption(argument, L"/czeddie") ||
		IsCommandLineOption(argument, L"/scene_detect") ||
		IsCommandLineOption(argument, L"/scene") ||
		IsCommandLineOption(argument, L"/disable_detection_features") ||
		IsCommandLineOption(argument, L"/scene_correction_basic") ||
		IsCommandLineOption(argument, L"/newlldv") ||
		IsCommandLineOption(argument, L"/startminimized");
}

void ValidateCommandLineArguments(const std::vector<const wchar_t*>& arguments)
{
	for (int index = 1; index < static_cast<int>(arguments.size()); ++index)
	{
		const wchar_t* argument = arguments[index];
		if (IsConfigSelectionArgument(argument))
		{
			if (wcschr(argument, L'=') == nullptr)
				++index; // The shared config loader already validated the value.
			continue;
		}

		if (IsCommandLineOption(argument, L"/subtitle_reposition"))
		{
			if (index + 1 < static_cast<int>(arguments.size()) &&
				!IsCommandLineSwitch(arguments[index + 1]))
			{
				SubtitleRepositionMode ignored =
					SubtitleRepositionMode::DISABLED;
				if (!TryParseSubtitleRepositionMode(
					arguments[index + 1], ignored))
					throw std::runtime_error(
						"Invalid /subtitle_reposition: expected "
						"true, false, basic, or advanced");
				++index;
			}
			continue;
		}
		if (IsBooleanCommandLineOption(argument))
		{
			if (index + 1 < static_cast<int>(arguments.size()) && !IsCommandLineSwitch(arguments[index + 1]))
			{
				bool ignored = false;
				if (!TryParseBooleanArgument(arguments[index + 1], ignored))
					throw std::runtime_error("Invalid boolean value for command-line option");
				++index;
			}
			continue;
		}

		if (!RequiresCommandLineValue(argument))
			throw std::runtime_error("Unknown command-line option: " +
				NarrowCommandLineToken(argument));

		if (index + 1 >= static_cast<int>(arguments.size()) || IsCommandLineSwitch(arguments[index + 1]))
			throw std::runtime_error("Missing value for command-line option: " +
				NarrowCommandLineToken(argument));

		if (IsCommandLineOption(argument, L"/queue_size") && !IsPositiveInteger(arguments[index + 1]))
			throw std::runtime_error("Invalid /queue_size: expected a positive integer");

		if (IsCommandLineOption(argument, L"/reset_after_render_restart_seconds") &&
			!IsPositiveInteger(arguments[index + 1]))
			throw std::runtime_error(
				"Invalid /reset_after_render_restart_seconds: expected a positive integer");

		if (IsCommandLineOption(argument, L"/reset_queue_too_large_percent") &&
			(!IsPositiveInteger(arguments[index + 1]) ||
			 _wtoi(arguments[index + 1]) > 100))
			throw std::runtime_error(
				"Invalid /reset_queue_too_large_percent: expected an integer from 1 to 100");

		if (IsCommandLineOption(argument, L"/lldv_max_cll") ||
			IsCommandLineOption(argument, L"/lldv_max_fall") ||
			IsCommandLineOption(argument, L"/lldv_mastering_min_luminance") ||
			IsCommandLineOption(argument, L"/lldv_mastering_max_luminance"))
		{
			double ignored = 0.0;
			if (!TryParseFiniteNonNegativeDouble(arguments[index + 1], ignored))
				throw std::runtime_error("Invalid LLDV metadata value: expected a finite non-negative number");
			if (IsCommandLineOption(argument, L"/lldv_mastering_max_luminance") && ignored <= 0.0)
				throw std::runtime_error("Invalid /lldv_mastering_max_luminance: expected a value greater than zero");
		}

		if (IsCommandLineOption(argument, L"/frame_offset") &&
			_wcsicmp(arguments[index + 1], L"auto") != 0 && !IsNonNegativeInteger(arguments[index + 1]))
			throw std::runtime_error("Invalid /frame_offset: expected non-negative milliseconds or auto");

		if (IsCommandLineOption(argument, L"/scene_correction_mode") &&
			_wcsicmp(arguments[index + 1], L"BASIC") != 0 &&
			_wcsicmp(arguments[index + 1], L"ADVANCED") != 0 &&
			_wcsicmp(arguments[index + 1], L"RENDERER_REPEAT") != 0 &&
			_wcsicmp(arguments[index + 1], L"VP_REPEAT") != 0 &&
			_wcsicmp(arguments[index + 1], L"RENDERER_GAP") != 0 &&
			_wcsicmp(arguments[index + 1], L"UPSTREAM_SAMPLE") != 0)
			throw std::runtime_error(
				"Invalid /scene_correction_mode: expected BASIC or ADVANCED");

		++index;
	}
}

void PrintCommandLineHelp()
{
	// This is a Windows-subsystem GUI executable. Attach to the invoking shell
	// so /help behaves like a normal CLI command without opening any dialog.
	const bool hasConsole = AttachConsole(ATTACH_PARENT_PROCESS) || GetLastError() == ERROR_ACCESS_DENIED;

	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	bool closeOutput = false;
	if (output == nullptr || output == INVALID_HANDLE_VALUE)
	{
		if (!hasConsole)
			return;

		output = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		closeOutput = true;
	}

	if (output == INVALID_HANDLE_VALUE)
		return;

	DWORD ignored = 0;
	DWORD consoleMode = 0;
	if (GetConsoleMode(output, &consoleMode))
	{
		const bool shellPromptAlreadyShown = ClearCurrentConsoleLine(output);
		WriteConsoleW(output, COMMAND_LINE_HELP, static_cast<DWORD>(wcslen(COMMAND_LINE_HELP)), &ignored, nullptr);
		if (shellPromptAlreadyShown)
			SendConsoleEnter();
	}
	else
	{
		const int bytesRequired = WideCharToMultiByte(CP_UTF8, 0, COMMAND_LINE_HELP, -1, nullptr, 0, nullptr, nullptr);
		if (bytesRequired > 1)
		{
			std::vector<char> utf8(static_cast<size_t>(bytesRequired));
			WideCharToMultiByte(CP_UTF8, 0, COMMAND_LINE_HELP, -1, utf8.data(), bytesRequired, nullptr, nullptr);
			WriteFile(output, utf8.data(), static_cast<DWORD>(bytesRequired - 1), &ignored, nullptr);
		}
	}

	if (closeOutput)
		CloseHandle(output);
}
}


BOOL CVideoProcessorApp::InitInstance()
{
	// Handle help before creating any UI, COM objects, or background workers.
	int argumentCount = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLine(), &argumentCount);
	if (!arguments)
		return FALSE;

	bool helpRequested = false;
	for (int i = 1; i < argumentCount; ++i)
	{
		if (IsHelpArgument(arguments[i]))
		{
			helpRequested = true;
			break;
		}
	}
	LocalFree(arguments);

	if (helpRequested)
	{
		PrintCommandLineHelp();
		return FALSE;
	}

	// Resolve startup-only logging configuration before rotation and before
	// any producer thread can enqueue a diagnostic.
	const auto debugLogRetention = LoadDebugLogRetentionSetting();
	DEBUGLOG_INIT(
		debugLogRetention.count,
		debugLogRetention.diagnostic);

	CVideoProcessorDlg dlg;
	m_pMainWnd = &dlg;

	try
	{
		if (!CWinAppEx::InitInstance())
			throw std::runtime_error("Failed to initialize VideoProcessorApp");

		// Validate alpha display rules before creating the UI or capture graph.
		// A malformed rule must not silently select a fallback renderer profile.
		ValidateRendererConfigRules();

		// COINIT_MULTITHREADED was used in the Blackmagic SDK examples,
		// using that without further investigation
		if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
			throw std::runtime_error("Failed to initialize com objects");

		// Parse command line
		// https://docs.microsoft.com/en-us/cpp/c-runtime-library/argc-argv-wargv
		std::vector<std::wstring> configuredArguments = LoadConfiguredCommandLineArguments();

		int parsedArgumentCount = 0;
		LPWSTR* parsedArguments = CommandLineToArgvW(GetCommandLine(), &parsedArgumentCount);
		if (!parsedArguments)
			throw std::runtime_error("Failed to parse command line");

		std::vector<std::wstring> mergedArguments;
		mergedArguments.emplace_back(parsedArgumentCount > 0 ? parsedArguments[0] : L"VideoProcessor.exe");

		mergedArguments.insert(mergedArguments.end(), configuredArguments.begin(), configuredArguments.end());
		for (int i = 1; i < parsedArgumentCount; ++i)
			mergedArguments.emplace_back(parsedArguments[i]);

		std::vector<const wchar_t*> pArgs;
		pArgs.reserve(mergedArguments.size());
		for (auto& argument : mergedArguments)
		{
			if (IsCommandLineSwitch(argument.c_str()) &&
				!IsConfigSelectionArgument(argument.c_str()))
			{
				std::transform(argument.begin(), argument.end(), argument.begin(), towlower);
			}
		}
		for (size_t argumentIndex = 1; argumentIndex < mergedArguments.size(); ++argumentIndex)
		{
			if (HasCaseInsensitiveValue(mergedArguments[argumentIndex - 1].c_str()))
			{
				std::transform(mergedArguments[argumentIndex].begin(), mergedArguments[argumentIndex].end(),
					mergedArguments[argumentIndex].begin(), towupper);
			}
		}
		for (const auto& argument : mergedArguments)
			pArgs.push_back(argument.c_str());

		const int iNumOfArgs = static_cast<int>(pArgs.size());
		LocalFree(parsedArguments);
		ValidateCommandLineArguments(pArgs);

		for (int i = 1; i < iNumOfArgs; i++)
		{
			// /fullscreen
			bool booleanValue = false;
			if (ReadBooleanOption(pArgs.data(), i, iNumOfArgs, L"/fullscreen", booleanValue))
			{
				dlg.StartFullScreen(booleanValue);
			}

			// /windowedfullscreenmode
			if (ReadBooleanOption(pArgs.data(), i, iNumOfArgs, L"/windowedfullscreenmode", booleanValue))
			{
				dlg.WindowedFullScreenMode(booleanValue);
			}

			// /renderer "name"
			if (wcscmp(pArgs[i], L"/renderer") == 0 && (i + 1) < iNumOfArgs)
			{
				dlg.DefaultRendererName(pArgs[i + 1]);
			}

			// /set_queue "[value|32]"
			if (wcscmp(pArgs[i], L"/queue_size") == 0 && (i + 1) < iNumOfArgs)
			{
				dlg.SetQueueSize(pArgs[i + 1]);
			}

			if (wcscmp(pArgs[i], L"/reset_after_render_restart_seconds") == 0 &&
				(i + 1) < iNumOfArgs)
			{
				dlg.SetQueueResetDelaySeconds(pArgs[i + 1]);
			}

			if (wcscmp(pArgs[i], L"/reset_queue_too_large_percent") == 0 &&
				(i + 1) < iNumOfArgs)
			{
				dlg.SetQueueResetHighWaterPercent(pArgs[i + 1]);
			}


			// /set_queue "[useless with mini, up to 4 on Quad.. I mean, it is 'Quad'... ]"
			if (wcscmp(pArgs[i], L"/capture_device") == 0 && (i + 1) < iNumOfArgs)
			{
				dlg.SetCaptureDevice(pArgs[i + 1]);
				
			}


			// /frame_offset [value|"auto"]
			if (wcscmp(pArgs[i], L"/frame_offset") == 0 && (i + 1) < iNumOfArgs)
			{
				if (wcscmp(pArgs[i + 1], L"AUTO") == 0)
				{
					dlg.StartFrameOffsetAuto();
				}
				else
				{
					dlg.StartFrameOffset(pArgs[i + 1]);
				}
			}

			// Set video conversion overrides
			if (wcscmp(pArgs[i], L"/video_conversion") == 0 && (i + 1) < iNumOfArgs)
			{
				VideoConversionOverride videoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_NONE;

				if (wcscmp(pArgs[i + 1], L"V210_TO_P010") == 0)
				{
					videoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010;
				}
				else if (wcscmp(pArgs[i + 1], L"UYVY_TO_P010") == 0)
				{
					// UYVY_TO_P010 is an alias for V210_TO_P010 (smart auto-detection)
					videoConversionOverride = VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010;
				}
				else
				{
					throw std::runtime_error("Invalid option for /video_conversion");
				}

				dlg.DefaultVideoConversionOverride(videoConversionOverride);
			}

			// Set container colorspace
			if (wcscmp(pArgs[i], L"/container_colorspace") == 0 && (i + 1) < iNumOfArgs)
			{
				ColorSpace colorSpaceOption;

				if (wcscmp(pArgs[i + 1], L"BT2020") == 0)
				{
					colorSpaceOption = ColorSpace::BT_2020;
				}
				else if (wcscmp(pArgs[i + 1], L"P3_D65") == 0)
				{
					colorSpaceOption = ColorSpace::P3_D65;
				}
				else if (wcscmp(pArgs[i + 1], L"P3_DCI") == 0)
				{
					colorSpaceOption = ColorSpace::P3_DCI;
				}
				else if (wcscmp(pArgs[i + 1], L"P3_D60") == 0)
				{
					colorSpaceOption = ColorSpace::P3_D60;
				}
				else if (wcscmp(pArgs[i + 1], L"REC709") == 0)
				{
					colorSpaceOption = ColorSpace::REC_709;
				}
				else if (wcscmp(pArgs[i + 1], L"REC601_525") == 0)
				{
					colorSpaceOption = ColorSpace::REC_601_525;
				}
				else if (wcscmp(pArgs[i + 1], L"REC601_625") == 0)
				{
					colorSpaceOption = ColorSpace::REC_601_625;
				}
				else
				{
					throw std::runtime_error("Invalid option for /colorspace");
				}

				dlg.DefaultContainerColorSpace(colorSpaceOption);
			}

			// Set HDR color space
			if (wcscmp(pArgs[i], L"/hdr_colorspace") == 0 && (i + 1) < iNumOfArgs)
			{
				HdrColorspaceOptions hdrColorSpaceOption;

				if (wcscmp(pArgs[i + 1], L"FOLLOW_INPUT") == 0)
				{
					hdrColorSpaceOption = HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT;
				}
				else if (wcscmp(pArgs[i + 1], L"FOLLOW_INPUT_LLDV") == 0)
				{
					hdrColorSpaceOption = HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_INPUT_LLDV;
				}
				else if (wcscmp(pArgs[i + 1], L"FOLLOW_CONTAINER") == 0)
				{
					hdrColorSpaceOption = HdrColorspaceOptions::HDR_COLORSPACE_FOLLOW_CONTAINER;
				}
				else if (wcscmp(pArgs[i + 1], L"BT2020") == 0)
				{
					hdrColorSpaceOption = HdrColorspaceOptions::HDR_COLORSPACE_BT2020;
				}
				else if (wcscmp(pArgs[i + 1], L"P3") == 0)
				{
					hdrColorSpaceOption = HdrColorspaceOptions::HDR_COLORSPACE_P3;
				}
				else if (wcscmp(pArgs[i + 1], L"REC709") == 0)
				{
					hdrColorSpaceOption = HdrColorspaceOptions::HDR_COLORSPACE_REC709;
				}
				else
				{
					throw std::runtime_error("Invalid option for /hdr_colorspace");
				}

				dlg.DefaultHDRColorSpace(hdrColorSpaceOption);
			}

			// Set HDR luminance
			if (wcscmp(pArgs[i], L"/hdr_luminance") == 0 && (i + 1) < iNumOfArgs)
			{
				HdrLuminanceOptions hdrLuminanceOption;

				if (wcscmp(pArgs[i + 1], L"FOLLOW_INPUT") == 0)
				{
					hdrLuminanceOption = HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT;
				}
				else if (wcscmp(pArgs[i + 1], L"FOLLOW_INPUT_LLDV") == 0)
				{
					hdrLuminanceOption = HdrLuminanceOptions::HDR_LUMINANCE_FOLLOW_INPUT_LLDV;
				}
				else if (wcscmp(pArgs[i + 1], L"HDR_LUMINANCE_USER") == 0)
				{
					hdrLuminanceOption = HdrLuminanceOptions::HDR_LUMINANCE_USER;
				}
				else
				{
					throw std::runtime_error("Invalid option for /hdr_luminance");
				}

				dlg.DefaultHDRLuminance(hdrLuminanceOption);
			}

			// Set renderer start-stop time method
			if (wcscmp(pArgs[i], L"/renderer_start_stop_time_method") == 0 && (i + 1) < iNumOfArgs)
			{
				DirectShowStartStopTimeMethod dsssTimeMethod;

				if (wcscmp(pArgs[i + 1], L"CLOCK_SMART") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART;
				}
				else if (wcscmp(pArgs[i + 1], L"CLOCK_SMART2") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2;
				}
				else if (wcscmp(pArgs[i + 1], L"CLOCK_THEO") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_THEO;
				}
				else if (wcscmp(pArgs[i + 1], L"CLOCK_CLOCK") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK;
				}
				else if (wcscmp(pArgs[i + 1], L"THEO_THEO") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_THEO_THEO;
				}
				else if (wcscmp(pArgs[i + 1], L"RATIONAL_RATIONAL") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL;
				}
				else if (wcscmp(pArgs[i + 1], L"CLOCK_RATIONAL") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL;
				}
				else if (wcscmp(pArgs[i + 1], L"CLOCK_NONE") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_NONE;
				}
				else if (wcscmp(pArgs[i + 1], L"THEO_NONE") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_THEO_NONE;
				}
				else if (wcscmp(pArgs[i + 1], L"NONE") == 0)
				{
					dsssTimeMethod = DirectShowStartStopTimeMethod::DS_SSTM_NONE;
				}
				else
				{
					throw std::runtime_error("Invalid option for /renderer_start_stop_time_method");
				}

				dlg.DefaultRendererStartStopTimeMethod(dsssTimeMethod);
			}

			// Set nominal range
			if (wcscmp(pArgs[i], L"/renderer_nominal_range") == 0 && (i + 1) < iNumOfArgs)
			{
				DXVA_NominalRange nominalRange;

				if (wcscmp(pArgs[i + 1], L"AUTO") == 0)
				{
					nominalRange = DXVA_NominalRange::DXVA_NominalRange_Unknown;
				}
				else if (wcscmp(pArgs[i + 1], L"FULL") == 0)
				{
					nominalRange = DXVA_NominalRange::DXVA_NominalRange_0_255;
				}
				else if (wcscmp(pArgs[i + 1], L"LIMITED") == 0)
				{
					nominalRange = DXVA_NominalRange::DXVA_NominalRange_16_235;
				}
				else if (wcscmp(pArgs[i + 1], L"SMALL") == 0)
				{
					nominalRange = DXVA_NominalRange::DXVA_NominalRange_48_208;
				}
				else
				{
					throw std::runtime_error("Invalid option for /renderer_nominal_range");
				}

				dlg.DefaultRendererNominalRange(nominalRange);
			}

			// Set transfer function
			if (wcscmp(pArgs[i], L"/renderer_transfer_function") == 0 && (i + 1) < iNumOfArgs)
			{
				DXVA_VideoTransferFunction transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_Unknown;

				if (wcscmp(pArgs[i + 1], L"AUTO") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_Unknown;
				}
				else if (wcscmp(pArgs[i + 1], L"PQ") == 0)
				{
					transferFunction = DIRECTSHOW_VIDEOTRANSFUNC_2084;
				}
				else if (wcscmp(pArgs[i + 1], L"REC709") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_709;
				}
				else if (wcscmp(pArgs[i + 1], L"BT2020_CONST") == 0)
				{
					transferFunction = DIRECTSHOW_VIDEOTRANSFUNC_2020_const;
				}

				else if (wcscmp(pArgs[i + 1], L"GAMMA_1.8") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_18;
				}
				else if (wcscmp(pArgs[i + 1], L"GAMMA_2.0") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_20;
				}
				else if (wcscmp(pArgs[i + 1], L"GAMMA_2.2") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22;
				}
				else if (wcscmp(pArgs[i + 1], L"GAMMA_2.6") == 0)
				{
					transferFunction = DIRECTSHOW_VIDEOTRANSFUNC_26;
				}
				else if (wcscmp(pArgs[i + 1], L"GAMMA_2.8") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_28;
				}

				else if (wcscmp(pArgs[i + 1], L"LINEAR_RGB") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_10;
				}
				else if (wcscmp(pArgs[i + 1], L"204M") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_240M;
				}
				else if (wcscmp(pArgs[i + 1], L"8BIT_GAMMA_2.2") == 0)
				{
					transferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_22_8bit_sRGB;
				}
				else if (wcscmp(pArgs[i + 1], L"LOG_100_1") == 0)
				{
					transferFunction = DIRECTSHOW_VIDEOTRANSFUNC_Log_100;
				}
				else if (wcscmp(pArgs[i + 1], L"LOG_316_1") == 0)
				{
					transferFunction = DIRECTSHOW_VIDEOTRANSFUNC_Log_316;
				}
				else if (wcscmp(pArgs[i + 1], L"REC709") == 0)
				{
					transferFunction = DIRECTSHOW_VIDEOTRANSFUNC_709_sym;
				}
				else if (wcscmp(pArgs[i + 1], L"BT2020") == 0)
				{
					transferFunction = DIRECTSHOW_VIDEOTRANSFUNC_2020;
				}
				else if (wcscmp(pArgs[i + 1], L"HYBRID_LOG_GAMMA") == 0)
				{
					transferFunction = DIRECTSHOW_VIDEOTRANSFUNC_HLG;
				}
				else
				{
					throw std::runtime_error("Invalid option for /renderer_transfer_function");
				}

				dlg.DefaultRendererTransferFunction(transferFunction);
			}

			// Set transfer matrix
			if (wcscmp(pArgs[i], L"/renderer_transfer_matrix") == 0 && (i + 1) < iNumOfArgs)
			{
				DXVA_VideoTransferMatrix transferMatrix = DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_Unknown;

				if (wcscmp(pArgs[i + 1], L"AUTO") == 0)
				{
					transferMatrix = DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_Unknown;
				}
				else if (wcscmp(pArgs[i + 1], L"BT2020_10") == 0)
				{
					transferMatrix = DIRECTSHOW_VIDEOTRANSFERMATRIX_BT2020_10;
				}
				else if (wcscmp(pArgs[i + 1], L"BT2020_12") == 0)
				{
					transferMatrix = DIRECTSHOW_VIDEOTRANSFERMATRIX_BT2020_12;
				}
				else if (wcscmp(pArgs[i + 1], L"BT709") == 0)
				{
					transferMatrix = DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_BT709;
				}
				else if (wcscmp(pArgs[i + 1], L"BT601") == 0)
				{
					transferMatrix = DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_BT601;
				}
				else if (wcscmp(pArgs[i + 1], L"240M") == 0)
				{
					transferMatrix = DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_SMPTE240M;
				}
				else if (wcscmp(pArgs[i + 1], L"FCC") == 0)
				{
					transferMatrix = DIRECTSHOW_VIDEOTRANSFERMATRIX_FCC;
				}
				else if (wcscmp(pArgs[i + 1], L"YCGCO") == 0)
				{
					transferMatrix = DIRECTSHOW_VIDEOTRANSFERMATRIX_YCgCo;
				}
				else
				{
					throw std::runtime_error("Invalid option for /renderer_transfer_matrix");
				}

				dlg.DefaultRendererTransferMatrix(transferMatrix);
			}

			// Set primaries
			if (wcscmp(pArgs[i], L"/renderer_primaries") == 0 && (i + 1) < iNumOfArgs)
			{
				DXVA_VideoPrimaries primaries = DXVA_VideoPrimaries::DXVA_VideoPrimaries_Unknown;

				if (wcscmp(pArgs[i + 1], L"AUTO") == 0)
				{
					primaries = DXVA_VideoPrimaries::DXVA_VideoPrimaries_Unknown;
				}
				else if (wcscmp(pArgs[i + 1], L"BT2020") == 0)
				{
					primaries = DIRECTSHOW_VIDEOPRIMARIES_BT2020;
				}
				else if (wcscmp(pArgs[i + 1], L"DCI-P3") == 0)
				{
					primaries = DIRECTSHOW_VIDEOPRIMARIES_DCI_P3;
				}
				else if (wcscmp(pArgs[i + 1], L"BT709") == 0)
				{
					primaries = DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT709;
				}
				else if (wcscmp(pArgs[i + 1], L"NTSC_SYSM") == 0)
				{
					primaries = DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT470_2_SysM;
				}
				else if (wcscmp(pArgs[i + 1], L"NTSC_SYSBG") == 0)
				{
					primaries = DXVA_VideoPrimaries::DXVA_VideoPrimaries_BT470_2_SysBG;
				}
				else if (wcscmp(pArgs[i + 1], L"CIE1931_ZYX") == 0)
				{
					primaries = DIRECTSHOW_VIDEOPRIMARIES_XYZ;
				}
				else if (wcscmp(pArgs[i + 1], L"ACES") == 0)
				{
					primaries = DIRECTSHOW_VIDEOPRIMARIES_ACES;
				}
				else
				{
					throw std::runtime_error("Invalid option for /renderer_primaries");
				}

				dlg.DefaultRendererPrimaries(primaries);
			}


			// hide the UI except the video window
			if (ReadBooleanOption(pArgs.data(), i, iNumOfArgs, L"/noui", booleanValue) ||
				ReadBooleanOption(pArgs.data(), i, iNumOfArgs, L"/czeddie", booleanValue))
			{
				dlg.HideUI(booleanValue);
			}

			// scene-aware timing correction
			if (ReadBooleanOption(pArgs.data(), i, iNumOfArgs,
				L"/disable_detection_features", booleanValue))
			{
				dlg.DisableDetectionFeatures(booleanValue);
			}

			if (ReadBooleanOption(pArgs.data(), i, iNumOfArgs, L"/scene_detect", booleanValue) ||
				ReadBooleanOption(pArgs.data(), i, iNumOfArgs, L"/scene", booleanValue))
			{
				dlg.SceneDetect(booleanValue);
			}

			if (IsCommandLineOption(
				pArgs[i], L"/subtitle_reposition"))
			{
				SubtitleRepositionMode subtitleMode =
					SubtitleRepositionMode::BASIC;
				if (i + 1 < iNumOfArgs &&
					!IsCommandLineSwitch(pArgs[i + 1]))
				{
					if (!TryParseSubtitleRepositionMode(
						pArgs[i + 1], subtitleMode))
						throw std::runtime_error(
							"Invalid /subtitle_reposition: expected "
							"true, false, basic, or advanced");
					++i;
				}
				dlg.SubtitleRepositioning(subtitleMode);
			}

			if (wcscmp(pArgs[i], L"/scene_correction_mode") == 0 &&
				(i + 1) < iNumOfArgs)
			{
				dlg.SceneCorrectionUpstreamSample(
					_wcsicmp(pArgs[i + 1], L"ADVANCED") == 0 ||
					_wcsicmp(pArgs[i + 1], L"VP_REPEAT") == 0 ||
					_wcsicmp(pArgs[i + 1], L"UPSTREAM_SAMPLE") == 0);
			}

			if (ReadBooleanOption(pArgs.data(), i, iNumOfArgs,
				L"/scene_correction_basic", booleanValue))
			{
				dlg.SceneCorrectionUpstreamSample(!booleanValue);
			}

			// Opt-in LLDV detection for DeckLink's BT.2020 + SDR reporting.
			if (ReadBooleanOption(pArgs.data(), i, iNumOfArgs, L"/newlldv", booleanValue))
			{
				dlg.EnableNewLldvHeuristic(booleanValue);
			}

			if (wcscmp(pArgs[i], L"/lldv_max_cll") == 0 && (i + 1) < iNumOfArgs)
			{
				double value = 0.0;
				TryParseFiniteNonNegativeDouble(pArgs[i + 1], value);
				dlg.SetLldvMaxCll(value);
			}

			if (wcscmp(pArgs[i], L"/lldv_max_fall") == 0 && (i + 1) < iNumOfArgs)
			{
				double value = 0.0;
				TryParseFiniteNonNegativeDouble(pArgs[i + 1], value);
				dlg.SetLldvMaxFall(value);
			}

			if (wcscmp(pArgs[i], L"/lldv_mastering_min_luminance") == 0 &&
				(i + 1) < iNumOfArgs)
			{
				double value = 0.0;
				TryParseFiniteNonNegativeDouble(pArgs[i + 1], value);
				dlg.SetLldvMasteringMinLuminance(value);
			}

			if (wcscmp(pArgs[i], L"/lldv_mastering_max_luminance") == 0 &&
				(i + 1) < iNumOfArgs)
			{
				double value = 0.0;
				TryParseFiniteNonNegativeDouble(pArgs[i + 1], value);
				dlg.SetLldvMasteringMaxLuminance(value);
			}

			// start minimized
			if (ReadBooleanOption(pArgs.data(), i, iNumOfArgs, L"/startminimized", booleanValue))
			{
				dlg.StartMinimized(booleanValue);
			}

		}

		// Set set ourselves to high prio.
		if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
			throw std::runtime_error("Failed to set process priority");

		dlg.DoModal();
		
	}
	catch (std::runtime_error& e)
	{
		dlg.EndDialog(IDABORT);

		size_t size = strlen(e.what()) + 1;
		wchar_t* wtext = new wchar_t[size];
		size_t outSize;
		mbstowcs_s(&outSize, wtext, size, e.what(), size - 1);

		MessageBox(nullptr, wtext, TEXT("Fatal error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

		delete[] wtext;
	}

	CoUninitialize();

	// Shutdown async debug logger
	DEBUGLOG_SHUTDOWN();

	return FALSE;
}

// Function to check if a CString contains only numeric characters
bool is_number(const CString& str) {
	for (int i = 0; i < str.GetLength(); i++) {
		if (!_istdigit(str[i])) { // Check if character is not a digit
			return false;
		}
	}
	return !str.IsEmpty(); // Ensure it's not an empty string
}

/*// Function to split CString into a vector of integers with validation
std::vector<int> split_cstring_ints(const CString& input, const CString& delimiter = _T(",")) {
	std::vector<int> numbers;
	int curPos = 0;
	CString token = input.Tokenize(delimiter, curPos);

	while (!token.IsEmpty()) {
		if (!is_number(token)) { // Validation check
			return {}; // Return empty vector if invalid token found
		}
		numbers.push_back(_ttoi(token)); // Convert CString to int
		token = input.Tokenize(delimiter, curPos);
	}

	return numbers;
}
*/


// Only here for debugging purposes where the application is compiled as a console application.
int main() {
	return _tWinMain(GetModuleHandle(NULL), NULL, GetCommandLine(), SW_SHOW);
}
