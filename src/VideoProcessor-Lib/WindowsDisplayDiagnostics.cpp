#include "pch.h"
#include "WindowsDisplayDiagnostics.h"
#include "DebugLog.h"
#include <dxgi1_6.h>
#include <atlbase.h>
#include <atomic>
#include <vector>
#include <cstdint>
#include <algorithm>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")

namespace
{
    uint64_t Fingerprint(const void* data, size_t length)
    {
        uint64_t hash = 14695981039346656037ull;
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
        return hash;
    }

    void ReadTopology(const char* id, const wchar_t* device)
    {
        LONG result = ERROR_INSUFFICIENT_BUFFER;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths;
        std::vector<DISPLAYCONFIG_MODE_INFO> modes;
        UINT32 pathCount = 0, modeCount = 0;
        for (int retry = 0; retry < 3 && result == ERROR_INSUFFICIENT_BUFFER; ++retry)
        {
            result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
            if (result != ERROR_SUCCESS) break;
            if (pathCount > 16384 || modeCount > 16384) { result = ERROR_INVALID_DATA; break; }
            paths.resize(pathCount); modes.resize(modeCount);
            result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                &modeCount, modes.data(), nullptr);
        }
        DebugLog::Log("DISPLAY_STATE %s topology result=%ld paths=%u", id, result, pathCount);
        if (result != ERROR_SUCCESS) return;
        unsigned matched = 0;
        for (UINT32 i = 0; i < pathCount; ++i)
        {
            const auto& path = paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = path.sourceInfo.adapterId;
            source.header.id = path.sourceInfo.id;
            const LONG sourceResult = DisplayConfigGetDeviceInfo(&source.header);
            if (sourceResult != ERROR_SUCCESS)
            {
                DebugLog::Log("DISPLAY_STATE %s source_name path=%u result=%ld", id, i, sourceResult);
                continue;
            }
            if (_wcsicmp(source.viewGdiDeviceName, device) != 0) continue;
            ++matched;
            DebugLog::Log("DISPLAY_STATE %s path=%u adapter=%08X:%08X source=%u target=%u technology=%u refresh=%u/%u scan=%u flags=0x%X",
                id, i, path.targetInfo.adapterId.HighPart, path.targetInfo.adapterId.LowPart,
                path.sourceInfo.id, path.targetInfo.id, path.targetInfo.outputTechnology,
                path.targetInfo.refreshRate.Numerator, path.targetInfo.refreshRate.Denominator,
                path.targetInfo.scanLineOrdering, path.flags);
            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
            color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
            color.header.size = sizeof(color);
            color.header.adapterId = path.targetInfo.adapterId;
            color.header.id = path.targetInfo.id;
            const LONG colorResult = DisplayConfigGetDeviceInfo(&color.header);
            if (colorResult == ERROR_SUCCESS)
                DebugLog::Log("DISPLAY_STATE %s advanced_color target=%u supported=%u enabled=%u wide_enforced=%u force_disabled=%u encoding=%u bits=%u (OS report; enabled is not proof of HDR content)",
                    id, path.targetInfo.id, color.advancedColorSupported, color.advancedColorEnabled,
                    color.wideColorEnforced, color.advancedColorForceDisabled, color.colorEncoding, color.bitsPerColorChannel);
            else DebugLog::Log("DISPLAY_STATE %s advanced_color target=%u unavailable result=%ld", id, path.targetInfo.id, colorResult);
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
            white.header.size = sizeof(white);
            white.header.adapterId = path.targetInfo.adapterId;
            white.header.id = path.targetInfo.id;
            const LONG whiteResult = DisplayConfigGetDeviceInfo(&white.header);
            if (whiteResult == ERROR_SUCCESS)
                DebugLog::Log("DISPLAY_STATE %s windows_sdr_white target=%u raw=%u nits=%.4f (OS desktop setting, not VP tone-map target)",
                    id, path.targetInfo.id, white.SDRWhiteLevel, white.SDRWhiteLevel * 0.08);
            else DebugLog::Log("DISPLAY_STATE %s windows_sdr_white unavailable result=%ld", id, whiteResult);
        }
        DebugLog::Log("DISPLAY_STATE %s topology matched_paths=%u", id, matched);
    }

    void ReadOutput(const char* id, HMONITOR monitor, IDXGISwapChain* swapchain)
    {
        CComPtr<IDXGIOutput> output;
        BOOL exclusive = FALSE;
        HRESULT fullscreenResult = E_NOINTERFACE;
        if (swapchain)
        {
            CComPtr<IDXGIOutput> exclusiveOutput;
            fullscreenResult = swapchain->GetFullscreenState(&exclusive, &exclusiveOutput);
            const HRESULT containing = swapchain->GetContainingOutput(&output);
            DebugLog::Log("DISPLAY_STATE %s swapchain=%p fullscreen_result=0x%08lX exclusive=%d containing_output_result=0x%08lX",
                id, swapchain, fullscreenResult, exclusive, containing);
        }
        if (!output)
        {
            CComPtr<IDXGIFactory1> factory;
            const HRESULT factoryResult = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
            if (FAILED(factoryResult)) { DebugLog::Log("DISPLAY_STATE %s DXGI factory unavailable result=0x%08lX", id, factoryResult); return; }
            for (UINT a = 0; a < 64 && !output; ++a)
            {
                CComPtr<IDXGIAdapter1> adapter;
                if (FAILED(factory->EnumAdapters1(a, &adapter))) break;
                for (UINT o = 0; o < 64; ++o)
                {
                    CComPtr<IDXGIOutput> candidate;
                    if (FAILED(adapter->EnumOutputs(o, &candidate))) break;
                    DXGI_OUTPUT_DESC desc{};
                    if (SUCCEEDED(candidate->GetDesc(&desc)) && desc.Monitor == monitor) { output = candidate; break; }
                }
            }
        }
        if (!output) { DebugLog::Log("DISPLAY_STATE %s DXGI output unavailable", id); return; }
        DXGI_OUTPUT_DESC base{};
        const HRESULT baseResult = output->GetDesc(&base);
        DebugLog::Log("DISPLAY_STATE %s DXGI output result=0x%08lX device=%ls monitor=%p matches_window=%d", id,
            baseResult, SUCCEEDED(baseResult) ? base.DeviceName : L"unavailable", base.Monitor,
            SUCCEEDED(baseResult) && base.Monitor == monitor);
        CComQIPtr<IDXGIOutput6> output6(output);
        DXGI_OUTPUT_DESC1 desc{};
        const HRESULT result = output6 ? output6->GetDesc1(&desc) : E_NOINTERFACE;
        if (SUCCEEDED(result))
            DebugLog::Log("DISPLAY_STATE %s DXGI output_desc1 color_space=%u bits=%u min=%.7g max=%.7g full_frame=%.7g red=%.5f,%.5f green=%.5f,%.5f blue=%.5f,%.5f white=%.5f,%.5f (OS reported, not measured HDMI)",
                id, desc.ColorSpace, desc.BitsPerColor, desc.MinLuminance, desc.MaxLuminance, desc.MaxFullFrameLuminance,
                desc.RedPrimary[0], desc.RedPrimary[1], desc.GreenPrimary[0], desc.GreenPrimary[1],
                desc.BluePrimary[0], desc.BluePrimary[1], desc.WhitePoint[0], desc.WhitePoint[1]);
        else DebugLog::Log("DISPLAY_STATE %s DXGI output_desc1 unavailable result=0x%08lX", id, result);
        if (SUCCEEDED(fullscreenResult) && exclusive)
        {
            DXGI_GAMMA_CONTROL gamma{};
            const HRESULT gammaResult = output->GetGammaControl(&gamma);
            if (SUCCEEDED(gammaResult))
                DebugLog::Log("DISPLAY_STATE %s DXGI gamma hash=%016llX scale=%.6g,%.6g,%.6g offset=%.6g,%.6g,%.6g (reported curve, not measured)",
                    id, Fingerprint(&gamma, sizeof(gamma)), gamma.Scale.Red, gamma.Scale.Green, gamma.Scale.Blue,
                    gamma.Offset.Red, gamma.Offset.Green, gamma.Offset.Blue);
            else DebugLog::Log("DISPLAY_STATE %s DXGI gamma unavailable result=0x%08lX", id, gammaResult);
        }
        else DebugLog::Log("DISPLAY_STATE %s DXGI gamma not queried: exclusive-fullscreen ownership not established", id);
    }
}

void WindowsDisplayDiagnostics::Log(HWND window, const char* phase, const wchar_t* renderer,
    const void* instance, IDXGISwapChain* swapchain) noexcept
{
    const ULONGLONG started = GetTickCount64();
    static std::atomic<unsigned long> sequence{ 0 };
    char id[160]{};
    sprintf_s(id, "pid=%lu tid=%lu seq=%lu tick=%llu phase=%s", GetCurrentProcessId(),
        GetCurrentThreadId(), ++sequence, started, phase);
    try
    {
        DebugLog::Log("DISPLAY_STATE %s begin renderer=%ls instance=%p hwnd=%p read_only=1 wire_state=unverified",
            id, renderer, instance, window);
        if (!window || !IsWindow(window)) { DebugLog::Log("DISPLAY_STATE %s unavailable: invalid target window", id); return; }
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) { DebugLog::Log("DISPLAY_STATE %s monitor unavailable error=%lu", id, GetLastError()); return; }
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        const BOOL modeOk = EnumDisplaySettingsExW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode, 0);
        DebugLog::Log("DISPLAY_STATE %s monitor=%p device=%ls current_mode_available=%d size=%lux%lu refresh_integer=%lu bpp=%lu mode_flags=0x%lX",
            id, monitor, info.szDevice, modeOk, mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency, mode.dmBitsPerPel, mode.dmDisplayFlags);
        ReadTopology(id, info.szDevice);
        HDC dc = CreateDCW(L"DISPLAY", info.szDevice, nullptr, nullptr);
        if (dc)
        {
            WORD ramp[3][256]{};
            const BOOL ok = GetDeviceGammaRamp(dc, ramp);
            if (ok)
            {
                DebugLog::Log("DISPLAY_STATE %s GDI gamma hash=%016llX (legacy API report only; may not reflect advanced-color scanout)", id, Fingerprint(ramp, sizeof(ramp)));
                for (unsigned c = 0; c < 3; ++c)
                    DebugLog::Log("DISPLAY_STATE %s GDI gamma channel=%u code[0,16,64,128,235,255]=%u,%u,%u,%u,%u,%u", id, c,
                        ramp[c][0], ramp[c][16], ramp[c][64], ramp[c][128], ramp[c][235], ramp[c][255]);
            }
            else DebugLog::Log("DISPLAY_STATE %s GDI gamma unavailable", id);
            DeleteDC(dc);
        }
        else DebugLog::Log("DISPLAY_STATE %s display DC unavailable error=%lu", id, GetLastError());
        ReadOutput(id, monitor, swapchain);
        DebugLog::Log("DISPLAY_STATE %s end elapsed_ms=%llu", id, GetTickCount64() - started);
    }
    catch (...) { DebugLog::Log("DISPLAY_STATE %s snapshot failed; rendering unchanged", id); }
}
