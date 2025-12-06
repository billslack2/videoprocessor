/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <resource.h>  // For ID_COMMAND_* defines

#include "FullscreenVideoWindow.h"

// Timer ID for delayed focus
#define TIMER_ID_FOCUS 100


FullscreenVideoWindow::FullscreenVideoWindow()
{
}


FullscreenVideoWindow::~FullscreenVideoWindow()
{
    if (m_hwnd)
    {
        ::KillTimer(m_hwnd, TIMER_ID_FOCUS);
        ::DestroyWindow(m_hwnd);
        ::UnregisterClassW(FULLSCREEN_WINDOW_CLASS_NAME, GetModuleHandle(nullptr));
    }
}


LRESULT CALLBACK FullscreenVideoWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    FullscreenVideoWindow* pThis = nullptr;

    if (uMsg == WM_NCCREATE)
    {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (FullscreenVideoWindow*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->m_hwnd = hwnd;
    }
    else
    {
        pThis = (FullscreenVideoWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (pThis)
        return pThis->HandleMessage(uMsg, wParam, lParam);

    // Return default action
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Same as create, but this time it's not exclusive
void FullscreenVideoWindow::CreateWindowedFullscreen(HMONITOR hmon, HWND parentWindow)
{
    //
    // Register the window class
    //

    WNDCLASS wc = { 0 };

    wc.lpfnWndProc = FullscreenVideoWindow::WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = FULLSCREEN_WINDOW_CLASS_NAME;

    RegisterClass(&wc);

    //
    // Create the window
    //

    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfo(hmon, &mi))
        throw std::runtime_error("Failed to get monitor info");

    LONG width = mi.rcMonitor.right - mi.rcMonitor.left;
#ifdef _DEBUG
    // When debugging it's REALLY handy to not cover the whole screen and capture key inputs...
   //  width = width  / 5;
#endif
    m_hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES | WS_EX_NOPARENTNOTIFY,
        FULLSCREEN_WINDOW_CLASS_NAME,
        TEXT("Waiting for renderer to start."),
        WS_POPUP | WS_VISIBLE,
        mi.rcMonitor.left,
        mi.rcMonitor.top,
        width,
        mi.rcMonitor.bottom - mi.rcMonitor.top,
        parentWindow,
        nullptr,  // hMenu
        GetModuleHandle(nullptr),  // Parent process
        this);


    if (!m_hwnd)
        throw std::runtime_error("Failed to create window");

    // Store parent for keyboard message forwarding
    m_parentWindow = parentWindow;

    // Show window
    ::ShowWindow(m_hwnd, SW_SHOW);
    ::UpdateWindow(m_hwnd);
    
    // Set a timer to force focus after 3 seconds (3000ms)
    // This gives the renderer time to fully initialize
    ::SetTimer(m_hwnd, TIMER_ID_FOCUS, 3000, nullptr);
}


void FullscreenVideoWindow::Create(HMONITOR hmon, HWND parentWindow)
{
    //
    // Register the window class
    //

    WNDCLASS wc = { 0 };

    wc.lpfnWndProc = FullscreenVideoWindow::WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = FULLSCREEN_WINDOW_CLASS_NAME;

    RegisterClass(&wc);

    //
    // Create the window
    //

    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfo(hmon, &mi))
        throw std::runtime_error("Failed to get monitor info");

    LONG width = mi.rcMonitor.right - mi.rcMonitor.left;
#ifdef _DEBUG
    // When debugging it's REALLY handy to not cover the whole screen and capture key inputs...
   //  width = width  / 5;
#endif
    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES | WS_EX_NOPARENTNOTIFY,
        FULLSCREEN_WINDOW_CLASS_NAME,
        TEXT("Waiting for renderer to start."),
        WS_POPUP | WS_VISIBLE,
        mi.rcMonitor.left,
        mi.rcMonitor.top,
        width,
        mi.rcMonitor.bottom - mi.rcMonitor.top,
        parentWindow,
        nullptr,  // hMenu
        GetModuleHandle(nullptr),  // Parent process
        this);

    
    if(!m_hwnd)
        throw std::runtime_error("Failed to create window");

    // Store parent for keyboard message forwarding
    m_parentWindow = parentWindow;

    // Show window
    ::ShowWindow(m_hwnd, SW_SHOW);
    ::UpdateWindow(m_hwnd);
    
    // Set a timer to force focus after 3 seconds (3000ms)
    // This gives the renderer time to fully initialize
    ::SetTimer(m_hwnd, TIMER_ID_FOCUS, 3000, nullptr);
}


LRESULT __forceinline FullscreenVideoWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_TIMER:
        if (wParam == TIMER_ID_FOCUS)
        {
            // Kill the timer - we only need this once
            ::KillTimer(m_hwnd, TIMER_ID_FOCUS);
            
            // Force this window to the foreground and give it focus
            // Use AllowSetForegroundWindow trick to ensure it works
            ::AllowSetForegroundWindow(ASFW_ANY);
            ::SetForegroundWindow(m_hwnd);
            ::SetFocus(m_hwnd);
            ::SetActiveWindow(m_hwnd);
            
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        // Clicking on the window should give it focus
        ::SetForegroundWindow(m_hwnd);
        ::SetFocus(m_hwnd);
        break;

    case WM_KEYDOWN:
        // Handle accelerator keys directly by posting WM_COMMAND to parent
        if (m_parentWindow)
        {
            bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            
            // Ctrl+I = Stats Overlay toggle
            if (ctrlDown && wParam == 'I')
            {
                ::PostMessage(m_parentWindow, WM_COMMAND, MAKEWPARAM(ID_COMMAND_STATS_OVERLAY, 0), 0);
                return 0;
            }
            // Ctrl+F = Fullscreen toggle
            if (ctrlDown && wParam == 'F')
            {
                ::PostMessage(m_parentWindow, WM_COMMAND, MAKEWPARAM(ID_COMMAND_FULLSCREEN_TOGGLE, 0), 0);
                return 0;
            }
            // Escape = Exit fullscreen
            if (wParam == VK_ESCAPE)
            {
                ::PostMessage(m_parentWindow, WM_COMMAND, MAKEWPARAM(ID_COMMAND_FULLSCREEN_EXIT, 0), 0);
                return 0;
            }
            // Ctrl+R = Renderer reset
            if (ctrlDown && wParam == 'R')
            {
                ::PostMessage(m_parentWindow, WM_COMMAND, MAKEWPARAM(ID_COMMAND_RENDERER_RESET, 0), 0);
                return 0;
            }
            // Ctrl+P = PQ set
            if (ctrlDown && wParam == 'P')
            {
                ::PostMessage(m_parentWindow, WM_COMMAND, MAKEWPARAM(ID_COMMAND_PQ_SET, 0), 0);
                return 0;
            }
            // Ctrl+A = Auto set
            if (ctrlDown && wParam == 'A')
            {
                ::PostMessage(m_parentWindow, WM_COMMAND, MAKEWPARAM(ID_COMMAND_AUTO_SET, 0), 0);
                return 0;
            }
        }
        break;

    case WM_DESTROY:
        ::KillTimer(m_hwnd, TIMER_ID_FOCUS);
        return 0;  // no PostQuitMessage, not it's own thread
    }

    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}
