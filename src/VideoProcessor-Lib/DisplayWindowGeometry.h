#pragma once
#include <windows.h>

namespace DisplayWindowGeometry
{
    // Confine physical-coordinate awareness to the fullscreen surface. The
    // legacy MFC configuration/main window keeps its existing DPI behavior.
    class PhysicalCoordinates
    {
    public:
        explicit PhysicalCoordinates(bool enabled = true)
        {
            if (!enabled) return;
            const HMODULE user32 = GetModuleHandleW(L"user32.dll");
            set_ = reinterpret_cast<SetContext>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
            if (set_)
            {
                previous_ = set_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                if (!previous_) previous_ = set_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
            }
        }
        ~PhysicalCoordinates() { if (set_ && previous_) set_(previous_); }
        PhysicalCoordinates(const PhysicalCoordinates&) = delete;
        PhysicalCoordinates& operator=(const PhysicalCoordinates&) = delete;
    private:
        using SetContext = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
        SetContext set_ = nullptr;
        DPI_AWARENESS_CONTEXT previous_ = nullptr;
    };

    inline BOOL ClientRect(HWND window, RECT* rect)
    {
        using GetContext = DPI_AWARENESS_CONTEXT(WINAPI*)(HWND);
        using SetContext = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        static const auto get = reinterpret_cast<GetContext>(GetProcAddress(user32, "GetWindowDpiAwarenessContext"));
        static const auto set = reinterpret_cast<SetContext>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        const auto context = get ? get(window) : nullptr;
        const auto previous = set && context ? set(context) : nullptr;
        const BOOL result = GetClientRect(window, rect);
        if (set && previous) set(previous);
        return result;
    }
}
