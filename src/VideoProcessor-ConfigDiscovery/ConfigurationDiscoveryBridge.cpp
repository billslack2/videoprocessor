#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <ConfigurationDiscovery.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
wchar_t* CopyMultiString(const std::vector<std::wstring>& values)
{
    size_t characters = 1;
    for (const std::wstring& value : values) characters += value.size() + 1;
    auto* result = static_cast<wchar_t*>(CoTaskMemAlloc(characters * sizeof(wchar_t)));
    if (!result) return nullptr;
    wchar_t* output = result;
    for (const std::wstring& value : values)
    {
        std::memcpy(output, value.c_str(), value.size() * sizeof(wchar_t));
        output += value.size();
        *output++ = L'\0';
    }
    *output = L'\0';
    return result;
}
}

extern "C" __declspec(dllexport) wchar_t* __stdcall VPDiscoverCaptureDevices()
{
    return CopyMultiString(ConfigurationDiscovery::CaptureDeviceNames());
}

extern "C" __declspec(dllexport) wchar_t* __stdcall VPDiscoverCaptureConnections(
    const wchar_t* captureDeviceName)
{
    if (!captureDeviceName) return CopyMultiString({});
    return CopyMultiString(ConfigurationDiscovery::CaptureConnectionNames(
        captureDeviceName));
}

extern "C" __declspec(dllexport) wchar_t* __stdcall VPDiscoverRenderers(BOOL hideLegacyRenderers)
{
    return CopyMultiString(ConfigurationDiscovery::RendererNames(hideLegacyRenderers != FALSE));
}

extern "C" __declspec(dllexport) wchar_t* __stdcall VPDiscoverMonitors()
{
    return CopyMultiString(ConfigurationDiscovery::ActiveMonitorNames());
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
