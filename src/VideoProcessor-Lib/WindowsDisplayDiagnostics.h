#pragma once
#include <Windows.h>
struct IDXGISwapChain;

namespace WindowsDisplayDiagnostics
{
    // Read-only, bounded transition snapshot. Not a per-frame operation.
    // All failures are diagnostic; never change renderer behavior or throw.
    void Log(HWND window, const char* phase, const wchar_t* renderer,
        const void* instance, IDXGISwapChain* swapchain = nullptr) noexcept;
}
