/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <winnt.h>
extern "C" {
#include <libavutil/log.h>
}

#include <VideoProcessorDlg.h>
#include <VideoConversionOverride.h>
#include <DebugLog.h>
#include <ConfigFile.h>

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
  VideoProcessor-GUI.exe /help
  VideoProcessor-GUI.exe help

Options:
  /fullscreen
      Start fullscreen.

  /windowedfullscreenmode
      Use windowed fullscreen mode.

  /capture_device "name"
      Select the capture device.

  /renderer "name"
      Select the renderer.

  /queue_size [value|32]
      Set the renderer frame queue size.

  /scene_detect
      Prefer visually-safe scene boundaries when correcting late queued frames.

  /noui
      Hide the user interface; show video only.

  /startminimized
      Start the application minimized.

  /frame_offset [value|auto]
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
      Optional unified config file in the working directory.
      Command-line switches override matching config values.
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

bool TryGetFirstConfigString(const ConfigFile& config, const std::initializer_list<const char*> keys, std::string& value)
{
	for (const char* key : keys)
	{
		if (config.TryGetString("command_line", key, value))
			return true;
	}

	return false;
}

std::string ConfigLocation(const ConfigFile& config)
{
	return config.GetLoadedPath().empty() ? ConfigFile::DEFAULT_FILENAME : config.GetLoadedPath();
}

void ThrowIfConfigHasSyntaxWarnings(const ConfigFile& config)
{
	const auto& warnings = config.GetWarnings();
	if (warnings.empty())
		return;

	std::string error = "Invalid " + ConfigLocation(config) + " syntax:";
	for (const auto& warning : warnings)
		error += "\n" + warning;

	throw std::runtime_error(error);
}

void ValidateCommandLineConfigKeys(const ConfigFile& config)
{
	const auto* commandLineValues = config.GetSectionValues("command_line");
	if (!commandLineValues)
		return;

	const std::set<std::string> allowedKeys =
	{
		"fullscreen",
		"windowedfullscreenmode",
		"windowed_fullscreen_mode",
		"renderer",
		"queue_size",
		"capture_device",
		"frame_offset",
		"video_conversion",
		"container_colorspace",
		"hdr_colorspace",
		"hdr_luminance",
		"renderer_start_stop_time_method",
		"renderer_nominal_range",
		"renderer_transfer_function",
		"renderer_transfer_matrix",
		"renderer_primaries",
		"scene_detect",
		"scene",
		"noui",
		"no_ui",
		"startminimized",
		"start_minimized"
	};

	for (const auto& setting : *commandLineValues)
	{
		if (allowedKeys.find(setting.first) == allowedKeys.end())
			throw std::runtime_error("Unknown " + ConfigLocation(config) + " [command_line] key: " + setting.first);
	}
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

std::vector<std::wstring> LoadConfiguredCommandLineArguments()
{
	ConfigFile config;
	std::vector<std::wstring> arguments;
	if (!config.Load())
		return arguments;

	ThrowIfConfigHasSyntaxWarnings(config);
	ValidateCommandLineConfigKeys(config);

	if (!config.HasSection("command_line"))
		return arguments;

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
	AppendConfigBoolOption(arguments, config, { "noui", "no_ui" }, L"/noui");
	AppendConfigBoolOption(arguments, config, { "startminimized", "start_minimized" }, L"/startminimized");

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


void av_log_callback(void* ptr, int level, const char* fmt, va_list vargs)
{
	vprintf(fmt, vargs);
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

	// Setup ffmpeg logging
	av_log_set_callback(av_log_callback);
#ifdef _DEBUG
	av_log_set_level(AV_LOG_TRACE);
#endif

	// Initialize async debug logger
	DEBUGLOG_INIT();

	CVideoProcessorDlg dlg;
	m_pMainWnd = &dlg;

	try
	{
		if (!CWinAppEx::InitInstance())
			throw std::runtime_error("Failed to initialize VideoProcessorApp");

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
		mergedArguments.emplace_back(parsedArgumentCount > 0 ? parsedArguments[0] : L"VideoProcessor-GUI.exe");

		mergedArguments.insert(mergedArguments.end(), configuredArguments.begin(), configuredArguments.end());
		for (int i = 1; i < parsedArgumentCount; ++i)
			mergedArguments.emplace_back(parsedArguments[i]);

		std::vector<const wchar_t*> pArgs;
		pArgs.reserve(mergedArguments.size());
		for (const auto& argument : mergedArguments)
			pArgs.push_back(argument.c_str());

		const int iNumOfArgs = static_cast<int>(pArgs.size());
		LocalFree(parsedArguments);

		for (int i = 1; i < iNumOfArgs; i++)
		{
			// /fullscreen
			if (wcscmp(pArgs[i], L"/fullscreen") == 0)
			{
				dlg.StartFullScreen();
			}

			// /windowedfullscreenmode
			if (wcscmp(pArgs[i], L"/windowedfullscreenmode") == 0)
			{
				dlg.WindowedFullScreenMode();
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


			// /set_queue "[useless with mini, up to 4 on Quad.. I mean, it is 'Quad'... ]"
			if (wcscmp(pArgs[i], L"/capture_device") == 0 && (i + 1) < iNumOfArgs)
			{
				dlg.SetCaptureDevice(pArgs[i + 1]);
				
			}


			// /frame_offset [value|"auto"]
			if (wcscmp(pArgs[i], L"/frame_offset") == 0 && (i + 1) < iNumOfArgs)
			{
				if (wcscmp(pArgs[i + 1], L"auto") == 0)
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

				if (wcscmp(pArgs[i + 1], L"FULL") == 0)
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

				if (wcscmp(pArgs[i + 1], L"LINEAR_RGB") == 0)
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
				else if (wcscmp(pArgs[i + 1], L"YCgCo") == 0)
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
			if (wcscmp(pArgs[i], L"/noui") == 0 || wcscmp(pArgs[i], L"/czeddie") == 0)
			{
				dlg.HideUI();
			}

			// scene-aware timing correction
			if (wcscmp(pArgs[i], L"/scene_detect") == 0 || wcscmp(pArgs[i], L"/scene") == 0)
			{
				dlg.SceneDetect();
			}

			// start minimized
			if (wcscmp(pArgs[i], L"/startminimized") == 0)
			{
				dlg.StartMinimized();
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
