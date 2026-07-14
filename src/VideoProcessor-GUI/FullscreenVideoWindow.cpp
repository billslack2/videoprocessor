/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "FullscreenVideoWindow.h"

#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <cmath>

#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

FullscreenVideoWindow::FullscreenVideoWindow()
{
}


FullscreenVideoWindow::~FullscreenVideoWindow()
{
    if (m_hwnd)
    {
        ::DestroyWindow(m_hwnd);
        ::UnregisterClassW(FULLSCREEN_WINDOW_CLASS_NAME, GetModuleHandle(nullptr));
    }
}



static bool GetOutputForWindow(HWND hwnd, ComPtr<IDXGIOutput>& outOutput)
{
    HMONITOR targetMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;

    for (UINT a = 0;; a++)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;

        for (UINT o = 0;; o++)
        {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND)
                break;

            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)))
                continue;

            if (desc.Monitor == targetMon)
            {
                outOutput = output;
                return true;
            }
        }
    }
    return false;
}

static double MeasureRefreshHz_WaitForVBlank(HWND hwnd, int warmupVBlanks = 5, int sampleVBlanks = 240)
{
    ComPtr<IDXGIOutput> output;
    if (!GetOutputForWindow(hwnd, output))
        return 0.0;

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);

    // Warm up: sync onto vblank boundary
    for (int i = 0; i < warmupVBlanks; i++)
        output->WaitForVBlank();

    LARGE_INTEGER t0{}, t1{};
    QueryPerformanceCounter(&t0);

    for (int i = 0; i < sampleVBlanks; i++)
        output->WaitForVBlank();

    QueryPerformanceCounter(&t1);

    const double seconds = double(t1.QuadPart - t0.QuadPart) / double(freq.QuadPart);
    if (seconds <= 0.0)
        return 0.0;

    // sampleVBlanks vblanks elapsed => Hz = vblanks / seconds
    return double(sampleVBlanks) / seconds;
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

    double measuredRefreshRate = MeasureRefreshHz_WaitForVBlank(m_hwnd);
    DebugLog::Log("Calculated measuredRefreshRate = % .2f Hz", measuredRefreshRate);

    
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
}


void FullscreenVideoWindow::OnClose()
{
	// Redirect Alt-F4 to close the main application window instead
	CWnd* pMainWindow = AfxGetApp()->GetMainWnd();
	if (pMainWindow && IsWindow(pMainWindow->GetSafeHwnd()))
	{
		pMainWindow->PostMessage(WM_CLOSE);
	}
}


LRESULT __forceinline FullscreenVideoWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
	case WM_CLOSE:
		OnClose();
		return 0;

    case WM_DESTROY:
        return 0;  // no PostQuitMessage, not it's own thread

    }

    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}

