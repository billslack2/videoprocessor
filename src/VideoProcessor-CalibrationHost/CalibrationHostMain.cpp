// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// VideoProcessor-CalibrationHost: a TEST TOOL. It hosts the real VP renderer DLL without capture hardware, the way
// LibplaceboPluginProxyTests does (a hidden 320x180 popup window, a synthetic SDR video state), with
// VP_CALIBRATION_PIPE=1 in its own environment, so VideoProcessor-Calibrate.exe can be tested against a real
// renderer from another process. Nothing is shown on screen: the window is never made visible, refresh-rate
// switching is off and the state is SDR (no HDR or display-mode change).
//
//   VideoProcessor-CalibrationHost.exe --config <renderer cfg> [--seconds <n>] [--beta]
//
// It prints "host: rendering pid=<n> pipe=<name>" once the renderer runs, then keeps it alive for --seconds
// (default 120) or until Ctrl+C, and exits 0 after a clean stop. Run it from a folder holding a copy of the runtime
// (this exe, vprenderer\ and its DLLs) so every relative path resolves inside that folder.
#include <pch.h>

#include <ConfigFile.h>
#include <UnifiedProfileRuntime.h>
#include <VideoState.h>
#include <vprenderer/LibplaceboRendererPluginApi.h>
#include <VP-V/modules/output/calibration_lut/host/CalibrationPipe.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace
{
	std::atomic<bool> g_stop{ false };
	std::mutex g_logMutex;
	std::ofstream g_log;

	BOOL WINAPI OnConsoleControl(DWORD type)
	{
		if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
		{
			g_stop.store(true);
			return TRUE;
		}
		return FALSE;
	}

	void __cdecl RendererLog(const char* message)
	{
		std::lock_guard<std::mutex> guard(g_logMutex);
		if (g_log) g_log << message << std::endl;
		if (strstr(message, "Calibration")) std::cout << "renderer: " << message << std::endl;
	}

	struct HostCallback : IRendererCallback
	{
		std::atomic<RendererState> state{ RENDERSTATE_UNKNOWN };
		void OnRendererState(RendererState value, uint32_t) override { state = value; }
		void OnRendererDetailString(const CString&, uint32_t) override {}
	};

	void PumpMessages()
	{
		MSG message;
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}

	std::wstring ExeFolder()
	{
		wchar_t path[MAX_PATH] = {};
		const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring folder(path, n);
		return folder.substr(0, folder.find_last_of(L"\\/"));
	}
}

int wmain(int argc, wchar_t** argv)
{
	std::string config;
	int seconds = 120;
	bool beta = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::wstring a = argv[i];
		if (a == L"--config" && i + 1 < argc) config = std::string(CW2A(argv[++i], CP_ACP));
		else if (a == L"--seconds" && i + 1 < argc) seconds = _wtoi(argv[++i]);
		else if (a == L"--beta") beta = true;
		else { std::cerr << "usage: VideoProcessor-CalibrationHost --config <renderer cfg> [--seconds n] [--beta]\n"; return 2; }
	}
	if (config.empty()) { std::cerr << "--config is required\n"; return 2; }
	SetConsoleCtrlHandler(OnConsoleControl, TRUE);
	SetEnvironmentVariableW(L"VP_CALIBRATION_PIPE", L"1");

	const std::wstring root = ExeFolder();
	g_log.open(root + L"\\calibration-host-renderer.log", std::ios::trunc);
	const std::wstring pluginPath = root + (beta ? L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll" :
		L"\\vprenderer\\VideoProcessorVPRenderer.dll");
	HMODULE plugin = LoadLibraryExW(pluginPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!plugin) { std::cerr << "cannot load the renderer DLL (" << GetLastError() << ")\n"; return 3; }
	const auto version = reinterpret_cast<VideoProcessorLibplaceboGetApiVersionFn>(GetProcAddress(plugin, VP_LIBPLACEBO_VERSION_EXPORT));
	const auto create = reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(GetProcAddress(plugin, VP_LIBPLACEBO_CREATE_EXPORT));
	const auto destroy = reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(GetProcAddress(plugin, VP_LIBPLACEBO_DESTROY_EXPORT));
	if (!version || !create || !destroy || version() != VP_LIBPLACEBO_PLUGIN_API_VERSION)
	{
		std::cerr << "renderer DLL exports or API version mismatch\n";
		FreeLibrary(plugin);
		return 3;
	}

	// Never shown: WS_POPUP without WS_VISIBLE.
	HWND window = CreateWindowExW(0, L"STATIC", L"VideoProcessor calibration host", WS_POPUP, 0, 0, 320, 180,
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	HostCallback callback;
	IVideoRenderer* renderer = window ? create(&callback, 1, window, nullptr, false, 1,
		VideoConversionOverride::VIDEOCONVERSION_NONE, config.c_str(), RendererLog) : nullptr;
	int result = 0;
	if (!renderer)
	{
		std::cerr << "the renderer could not be created\n";
		result = 3;
	}
	else
	{
		ConfigFile file;
		UnifiedProfileRuntime::Runtime runtime;
		std::string error;
		CString active;
		bool restart = false, reset = false;
		if (!file.Load(config) || !runtime.Initialize(file, {}, error) ||
			!renderer->ApplyApplicationState(*runtime.GetSnapshot(), active, restart, reset))
		{
			std::cerr << "the renderer configuration was not accepted: " << error << "\n";
			result = 3;
		}
		else
		{
			VideoStateComPtr state = new VideoState();
			state->valid = true;
			state->displayMode = std::make_shared<DisplayMode>(320, 180, false, 60000, 1001);
			state->videoFrameEncoding = VideoFrameEncoding::BGRA_8BIT;
			state->eotf = EOTF::SDR;
			state->colorspace = ColorSpace::REC_709;
			renderer->OnVideoState(state);
			renderer->Build();
			renderer->Start();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			while (callback.state != RENDERSTATE_RENDERING && callback.state != RENDERSTATE_FAILED &&
				std::chrono::steady_clock::now() < deadline && !g_stop.load())
			{
				PumpMessages();
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			if (callback.state != RENDERSTATE_RENDERING)
			{
				std::cerr << "the renderer did not reach RENDERING (state " << static_cast<int>(callback.state.load()) << ")\n";
				result = 4;
			}
			else
			{
				std::wcout << L"host: rendering pid=" << GetCurrentProcessId() << L" pipe=" << CalibrationPipe::DefaultPipeName()
					<< std::endl;
				const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
				while (!g_stop.load() && std::chrono::steady_clock::now() < end && callback.state == RENDERSTATE_RENDERING)
				{
					PumpMessages();
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				std::cout << "host: stopping (state " << static_cast<int>(callback.state.load()) << ")" << std::endl;
			}
		}
		renderer->Stop();
		destroy(renderer);
	}
	if (window) DestroyWindow(window);
	FreeLibrary(plugin);
	std::cout << "host: exit " << result << std::endl;
	return result;
}
