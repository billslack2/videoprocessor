/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <ApplicationShutdownPolicy.h>

#include "FullscreenVideoWindow.h"
#include "VideoProcessorDlg.h"

namespace
{
bool ForwardFullscreenShortcut(HWND fullscreen, UINT message,
	WPARAM virtualKey, LPARAM keyData)
{
	CWnd* const mainWindow = AfxGetApp() ? AfxGetApp()->GetMainWnd() : nullptr;
	if (!mainWindow || !::IsWindow(mainWindow->GetSafeHwnd()))
		return false;
	BYTE modifiers = 0;
	if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0)
		modifiers |= FCONTROL;
	if ((::GetKeyState(VK_MENU) & 0x8000) != 0)
		modifiers |= FALT;
	if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0)
		modifiers |= FSHIFT;
	// The raw fullscreen host is not an MFC child, so its keys never pass
	// through CVideoProcessorDlg::PreTranslateMessage. Route scalar input to
	// the dialog's common configured-command dispatcher while this key message
	// is still being handled; no render or cross-process wait is involved.
	const LRESULT routed = mainWindow->SendMessage(
		WM_MESSAGE_EXTERNAL_SHORTCUT, virtualKey, modifiers);
	DebugLog::Log(
		"Fullscreen keyboard forwarding: message=0x%04x vk=0x%02x modifiers=0x%02x host=%p main=%p routed=%d repeat=%d",
		message, static_cast<unsigned int>(virtualKey),
		static_cast<unsigned int>(modifiers), reinterpret_cast<void*>(fullscreen),
		reinterpret_cast<void*>(mainWindow->GetSafeHwnd()), routed ? 1 : 0,
		(static_cast<ULONG_PTR>(keyData) & (1ull << 30)) != 0 ? 1 : 0);
	return routed != 0;
}
}


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
    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
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
        WS_POPUP,
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

	// Renderer/fullscreen construction may occur seconds after the user opened
	// Config (including while a combo popup owns foreground). Reveal the host
	// without activation; normal mouse interaction may still activate it later.
	ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

}


void FullscreenVideoWindow::Create(HMONITOR hmon, HWND parentWindow)
{
    //
    // Register the window class
    //

    WNDCLASS wc = { 0 };

    wc.lpfnWndProc = FullscreenVideoWindow::WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
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
        WS_POPUP,
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

	ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
}


void FullscreenVideoWindow::OnClose()
{
	// Redirect Alt-F4 to close the main application window instead
	CWnd* pMainWindow = AfxGetApp()->GetMainWnd();
	DebugLog::Log(
		"Fullscreen keyboard route: action=close-forward host=%p main=%p foreground=%p focus=%p",
		reinterpret_cast<void*>(m_hwnd),
		reinterpret_cast<void*>(pMainWindow ? pMainWindow->GetSafeHwnd() : nullptr),
		reinterpret_cast<void*>(GetForegroundWindow()),
		reinterpret_cast<void*>(GetFocus()));
	if (pMainWindow && IsWindow(pMainWindow->GetSafeHwnd()))
	{
		pMainWindow->PostMessage(WM_CLOSE);
	}
}


LRESULT __forceinline FullscreenVideoWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) &&
		(wParam == 'I' || wParam == VK_F4))
	{
		DebugLog::Log(
			"Fullscreen keyboard route: message=0x%04x vk=0x%02x ctrl=%d alt=%d host=%p owner=%p root=%p foreground=%p focus=%p",
			uMsg,
			static_cast<unsigned int>(wParam),
			(GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0,
			(GetKeyState(VK_MENU) & 0x8000) ? 1 : 0,
			reinterpret_cast<void*>(m_hwnd),
			reinterpret_cast<void*>(GetWindow(m_hwnd, GW_OWNER)),
			reinterpret_cast<void*>(GetAncestor(m_hwnd, GA_ROOT)),
			reinterpret_cast<void*>(GetForegroundWindow()),
			reinterpret_cast<void*>(GetFocus()));
	}
    switch (uMsg)
    {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		// Keep system close unconditional. It cannot depend on an accelerator
		// table, Config foreground ownership, or MFC discovering this raw HWND.
		if (uMsg == WM_SYSKEYDOWN && wParam == VK_F4 &&
			(::GetKeyState(VK_MENU) & 0x8000) != 0)
		{
			OnClose();
			return 0;
		}
		if (ForwardFullscreenShortcut(m_hwnd, uMsg, wParam, lParam))
			return 0;
		break;

    case WM_SETCURSOR:
        // A fullscreen video surface should never display the pointer.  Handle
        // this at the host-window boundary so every renderer gets the same
        // behavior and Windows cannot restore the class cursor on mouse move.
        // SetCursor is local to this message dispatch and avoids unbalancing
        // the process-wide ShowCursor display counter.
        if (LOWORD(lParam) == HTCLIENT)
        {
            ::SetCursor(nullptr);
            return TRUE;
        }
        break;

	case WM_ERASEBKGND:
    {
        RECT client{};
        GetClientRect(m_hwnd, &client);
        FillRect(
            reinterpret_cast<HDC>(wParam),
            &client,
            reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
		return 1;
	}

	case WM_SIZE:
	{
		CWnd* const mainWindow = AfxGetApp() ? AfxGetApp()->GetMainWnd() :
			nullptr;
		const BOOL posted = mainWindow &&
			::IsWindow(mainWindow->GetSafeHwnd()) &&
			::PostMessage(mainWindow->GetSafeHwnd(),
				WM_MESSAGE_FULLSCREEN_HOST_RESIZED,
				reinterpret_cast<WPARAM>(m_hwnd), lParam);
		DebugLog::Log(
			"Fullscreen host WM_SIZE: host=%p size=%dx%d posted=%d error=%lu",
			reinterpret_cast<void*>(m_hwnd), LOWORD(lParam), HIWORD(lParam),
			posted ? 1 : 0, posted ? 0UL : ::GetLastError());
		break;
	}

	case WM_CLOSE:
		OnClose();
		return 0;

	case WM_SYSCOMMAND:
		if (ApplicationShutdownPolicy::IsCloseSystemCommand(wParam))
		{
			OnClose();
			return 0;
		}
		break;

    case WM_DESTROY:
        return 0;  // no PostQuitMessage, not it's own thread

    }

    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}

