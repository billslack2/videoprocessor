#pragma once

#include <windows.h>
#include <atlbase.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <string>
#include <vector>
#include "DisplayRefreshRatePolicy.h"
#pragma comment(lib, "dxgi.lib")

// Monitor discovery, configuration and playback must resolve the same identity.
namespace DisplayMonitorInventory
{
    struct Monitor
    {
        HMONITOR handle = nullptr;
        std::wstring gdiName;
        std::wstring friendlyName;
        std::wstring label;
        bool primary = false;
        bool hasTarget = false;
        LUID targetAdapter{};
        UINT32 targetId = 0;
    };
    inline BOOL CALLBACK Collect(HMONITOR handle, HDC, LPRECT, LPARAM data)
    {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(handle, &info))
        {
            Monitor monitor;
            monitor.handle = handle;
            monitor.gdiName = info.szDevice;
            monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            reinterpret_cast<std::vector<Monitor>*>(data)->push_back(monitor);
        }
        return TRUE;
    }
    inline std::vector<Monitor> Enumerate()
    {
        std::vector<Monitor> monitors;
        EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&monitors));
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            UINT32 pathCount = 0, modeCount = 0;
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) break;
            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
            const LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount,
                paths.data(), &modeCount, modes.data(), nullptr);
            if (status == ERROR_INSUFFICIENT_BUFFER) continue;
            if (status != ERROR_SUCCESS) break;
            paths.resize(pathCount);
            for (const auto& path : paths)
            {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = path.sourceInfo.adapterId;
                source.header.id = path.sourceInfo.id;
                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof(target);
                target.header.adapterId = path.targetInfo.adapterId;
                target.header.id = path.targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                    DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
                for (auto& monitor : monitors)
                    if (_wcsicmp(monitor.gdiName.c_str(), source.viewGdiDeviceName) == 0)
                    {
                        monitor.friendlyName = target.monitorFriendlyDeviceName;
                        monitor.hasTarget = true;
                        monitor.targetAdapter = path.targetInfo.adapterId;
                        monitor.targetId = path.targetInfo.id;
                    }
            }
            break;
        }
        for (auto& monitor : monitors)
        {
            monitor.label = monitor.friendlyName.empty() ? monitor.gdiName : monitor.friendlyName;
            const auto duplicates = std::count_if(monitors.begin(), monitors.end(), [&](const Monitor& other) {
                return !monitor.friendlyName.empty() && _wcsicmp(monitor.friendlyName.c_str(), other.friendlyName.c_str()) == 0;
            });
            if (duplicates > 1) monitor.label += L" (" + monitor.gdiName + L")";
        }
        return monitors;
    }
    inline const Monitor* Resolve(const std::vector<Monitor>& monitors, const std::wstring& name)
    {
        if (name.empty() || _wcsicmp(name.c_str(), L"auto") == 0)
        {
            for (const auto& monitor : monitors) if (monitor.primary) return &monitor;
            return nullptr;
        }
        for (const auto& monitor : monitors)
            if (_wcsicmp(monitor.label.c_str(), name.c_str()) == 0 ||
                _wcsicmp(monitor.gdiName.c_str(), name.c_str()) == 0) return &monitor;
        const Monitor* match = nullptr;
        for (const auto& monitor : monitors)
            if (_wcsicmp(monitor.friendlyName.c_str(), name.c_str()) == 0)
            {
                if (match) return nullptr; // A duplicate friendly name needs an explicit output.
                match = &monitor;
            }
        return match;
    }
    struct Inventory
    {
        bool known = false;
        std::wstring monitor;
        std::string diagnostic;
        std::vector<DisplayOutputMode> modes;
    };
    inline void Append(std::vector<DisplayOutputMode>& modes, const DisplayOutputMode& candidate)
    {
        if (candidate.width < 100 || candidate.height < 100 || !candidate.refreshRate.numerator ||
            !candidate.refreshRate.denominator) return;
        for (const auto& mode : modes)
            if (mode.width == candidate.width && mode.height == candidate.height &&
                uint64_t(mode.refreshRate.numerator) * candidate.refreshRate.denominator ==
                uint64_t(candidate.refreshRate.numerator) * mode.refreshRate.denominator) return;
        if (modes.size() < 16384) modes.push_back(candidate);
    }
    inline Inventory Query(const std::wstring& name)
    {
        Inventory result;
        const auto monitors = Enumerate();
        const auto* monitor = Resolve(monitors, name);
        if (!monitor)
        {
            result.diagnostic = "The selected monitor is disconnected or its name is ambiguous.";
            return result;
        }
        result.monitor = monitor->label;
        bool queried = false;
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            CComPtr<IDXGIFactory1> factory;
            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            {
                bool found = false;
                for (UINT a = 0; !found; ++a)
                {
                    CComPtr<IDXGIAdapter1> adapter;
                    if (FAILED(factory->EnumAdapters1(a, &adapter))) break;
                    for (UINT o = 0;; ++o)
                    {
                        CComPtr<IDXGIOutput> output;
                        if (FAILED(adapter->EnumOutputs(o, &output))) break;
                        DXGI_OUTPUT_DESC description{};
                        if (FAILED(output->GetDesc(&description)) ||
                            _wcsicmp(description.DeviceName, monitor->gdiName.c_str()) != 0) continue;
                        found = true;
                        CComQIPtr<IDXGIOutput1> output1(output);
                        if (!output1) break;
                        for (const auto format : { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM })
                        {
                            UINT count = 0;
                            if (FAILED(output1->GetDisplayModeList1(format, 0, &count, nullptr)) || count > 16384) continue;
                            std::vector<DXGI_MODE_DESC1> modes(count);
                            if (count && FAILED(output1->GetDisplayModeList1(format, 0, &count, modes.data()))) continue;
                            queried = true;
                            modes.resize(count);
                            for (const auto& mode : modes)
                                if (mode.ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST &&
                                    mode.ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST)
                                    Append(result.modes, {mode.Width, mode.Height,
                                        {mode.RefreshRate.Numerator, mode.RefreshRate.Denominator}});
                        }
                        break;
                    }
                }
            }
        }
        if (SUCCEEDED(initialized)) CoUninitialize();
        // GDI supplements paths/drivers which do not expose DXGI modes.
        for (DWORD index = 0; index < 16384; ++index)
        {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsW(monitor->gdiName.c_str(), index, &mode)) break;
            queried = true;
            if (mode.dmBitsPerPel != 32 || (mode.dmDisplayFlags & DM_INTERLACED) != 0) continue;
            const DWORD rate = mode.dmDisplayFrequency;
            DisplayRefreshRational rational{rate, 1};
            if (rate == 23) rational = {24000, 1001};
            if (rate == 29) rational = {30000, 1001};
            if (rate == 59) rational = {60000, 1001};
            if (rate > 1) Append(result.modes, {mode.dmPelsWidth, mode.dmPelsHeight, rational});
        }
        result.known = queried;
        if (!queried) result.diagnostic = "Windows could not enumerate modes for the selected monitor.";
        return result;
    }
}
