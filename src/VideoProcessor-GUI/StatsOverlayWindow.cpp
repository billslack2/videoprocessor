/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "StatsOverlayWindow.h"
#include <sstream>
#include <iomanip>

// Static members
const TCHAR* StatsOverlayWindow::WINDOW_CLASS_NAME = TEXT("VideoProcessorStatsOverlay");
bool StatsOverlayWindow::s_classRegistered = false;

StatsOverlayWindow::StatsOverlayWindow()
	: m_hwnd(nullptr)
	, m_parentHwnd(nullptr)
	, m_isVisible(false)
	, m_isCreated(false)
	, m_font(nullptr)
	, m_boldFont(nullptr)
{
}

StatsOverlayWindow::~StatsOverlayWindow()
{
	Destroy();
}

void StatsOverlayWindow::RegisterWindowClass()
{
	if (s_classRegistered)
		return;

	WNDCLASS wc = {};
	wc.lpfnWndProc = StaticWndProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpszClassName = WINDOW_CLASS_NAME;
	wc.hbrBackground = nullptr; // No background brush for transparency
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	if (RegisterClass(&wc))
	{
		s_classRegistered = true;
	}
	// If registration fails, s_classRegistered stays false and Create will fail gracefully
}

bool StatsOverlayWindow::Create(HWND parentHwnd)
{
	if (m_isCreated)
		return true;

	m_parentHwnd = parentHwnd;

	RegisterWindowClass();
	
	// If class registration failed, return false
	if (!s_classRegistered)
		return false;

	// Get the monitor info for screen-based positioning
	HMONITOR hMonitor = MonitorFromWindow(parentHwnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	if (!GetMonitorInfo(hMonitor, &monitorInfo))
	{
		// Fallback to primary monitor
		monitorInfo.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
		monitorInfo.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
	}

	// Position relative to the screen (100px from right, 300px from bottom)
	int x = monitorInfo.rcMonitor.right - MARGIN_RIGHT - WINDOW_WIDTH;
	int y = monitorInfo.rcMonitor.bottom - MARGIN_BOTTOM - WINDOW_HEIGHT;

	// Create the overlay window with layered window style for transparency
	m_hwnd = CreateWindowEx(
		WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
		WINDOW_CLASS_NAME,
		TEXT("Stats Overlay"),
		WS_POPUP,
		x, y, WINDOW_WIDTH, WINDOW_HEIGHT,
		nullptr,                      // No parent window
		nullptr,                      // No menu
		GetModuleHandle(nullptr),     // hInstance
		(LPVOID)this);                // lpParam

	if (!m_hwnd)
	{
		return false;
	}

	// Set up layered window for transparency
	SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 240, LWA_ALPHA); // 240/255 = ~94% opacity

	// Create fonts - match MadVR stats overlay style
	HDC hdc = GetDC(m_hwnd);
	if (!hdc)
	{
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
		return false;
	}
	
	// MadVR uses a monospace font, approximately 22-23 pixels tall (25% larger than before)
	int fontHeight = 22;

	m_font = CreateFont(
		fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));

	m_boldFont = CreateFont(
		fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));

	ReleaseDC(m_hwnd, hdc);

	if (!m_font || !m_boldFont)
	{
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
		return false;
	}

	m_isCreated = true;
	return true;
}

void StatsOverlayWindow::Destroy()
{
	if (m_hwnd)
	{
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
	}

	if (m_font)
	{
		DeleteObject(m_font);
		m_font = nullptr;
	}

	if (m_boldFont)
	{
		DeleteObject(m_boldFont);
		m_boldFont = nullptr;
	}

	m_isCreated = false;
	m_isVisible = false;
}

void StatsOverlayWindow::Show(bool show)
{
	if (!m_isCreated)
		return;

	m_isVisible = show;
	ShowWindow(m_hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);

	if (show)
	{
		// Update position when showing
		UpdatePosition(m_parentHwnd);
		ForceRedraw();
	}
}

void StatsOverlayWindow::Toggle()
{
	Show(!m_isVisible);
}

void StatsOverlayWindow::UpdateStats(const StatsData& stats)
{
	std::lock_guard<std::mutex> lock(m_statsMutex);
	m_stats = stats;

	if (m_isVisible)
	{
		ForceRedraw();
	}
}

void StatsOverlayWindow::ForceRedraw()
{
	if (m_hwnd && m_isVisible)
	{
		InvalidateRect(m_hwnd, nullptr, TRUE);
	}
}

void StatsOverlayWindow::UpdatePosition(HWND parentHwnd)
{
	if (!m_hwnd || !parentHwnd)
		return;

	// Get the monitor info for screen-based positioning
	HMONITOR hMonitor = MonitorFromWindow(parentHwnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	GetMonitorInfo(hMonitor, &monitorInfo);

	// Position relative to the screen (100px from right, 300px from bottom)
	int x = monitorInfo.rcMonitor.right - MARGIN_RIGHT - WINDOW_WIDTH;
	int y = monitorInfo.rcMonitor.bottom - MARGIN_BOTTOM - WINDOW_HEIGHT;

	SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

LRESULT CALLBACK StatsOverlayWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	StatsOverlayWindow* pThis = nullptr;

	if (msg == WM_CREATE)
	{
		CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
		pThis = reinterpret_cast<StatsOverlayWindow*>(pCreate->lpCreateParams);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
	}
	else
	{
		pThis = reinterpret_cast<StatsOverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	}

	if (pThis)
	{
		return pThis->WndProc(hwnd, msg, wParam, lParam);
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT StatsOverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		OnPaint(hdc);
		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_ERASEBKGND:
		return 1; // We handle background in OnPaint

	case WM_NCHITTEST:
		return HTTRANSPARENT; // Allow clicks to pass through

	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
}

void StatsOverlayWindow::OnPaint(HDC hdc)
{
	RECT clientRect;
	GetClientRect(m_hwnd, &clientRect);

	// Create memory DC for double buffering
	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
	HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

	// Draw background and stats
	DrawBackground(memDC);
	DrawStats(memDC);

	// Blit to screen
	BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

	// Cleanup
	SelectObject(memDC, oldBitmap);
	DeleteObject(memBitmap);
	DeleteDC(memDC);
}

void StatsOverlayWindow::DrawBackground(HDC hdc)
{
	RECT rect;
	GetClientRect(m_hwnd, &rect);

	// Create semi-transparent background
	HBRUSH brush = CreateSolidBrush(BACKGROUND_COLOR);
	FillRect(hdc, &rect, brush);
	DeleteObject(brush);

	// Draw border
	HPEN pen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
	HPEN oldPen = (HPEN)SelectObject(hdc, pen);

	MoveToEx(hdc, 0, 0, nullptr);
	LineTo(hdc, rect.right - 1, 0);
	LineTo(hdc, rect.right - 1, rect.bottom - 1);
	LineTo(hdc, 0, rect.bottom - 1);
	LineTo(hdc, 0, 0);

	SelectObject(hdc, oldPen);
	DeleteObject(pen);
}

void StatsOverlayWindow::DrawStats(HDC hdc)
{
	std::lock_guard<std::mutex> lock(m_statsMutex);

	SetBkMode(hdc, TRANSPARENT);
	HFONT oldFont = (HFONT)SelectObject(hdc, m_font);

	int y = PADDING;
	SetTextColor(hdc, TEXT_COLOR);

	// Fixed-width format with labels and values separated for alignment
	CString line;

	// Resolution
	line.Format(TEXT("Resolution:        %-20s"), m_stats.resolution.IsEmpty() ? TEXT("---") : m_stats.resolution);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Refresh rate
	line.Format(TEXT("Refresh:         %7.2f Hz"), m_stats.refreshRate);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// EOTF
	line.Format(TEXT("EOTF:              %-20s"), m_stats.eotf.IsEmpty() ? TEXT("---") : m_stats.eotf);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Colorspace
	line.Format(TEXT("Colorspace:        %-20s"), m_stats.colorspace.IsEmpty() ? TEXT("---") : m_stats.colorspace);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Pixel Format
	line.Format(TEXT("Pixel Format:      %-20s"), m_stats.pixelFormat.IsEmpty() ? TEXT("---") : m_stats.pixelFormat);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Separator
	y += 4;

	// Method
	line.Format(TEXT("Method:            %-20s"), m_stats.method.IsEmpty() ? TEXT("---") : m_stats.method);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Frame Offset
	line.Format(TEXT("Offset:           %7d ms"), m_stats.frameOffsetMs);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Separator
	y += 4;

	// VP Latency
	line.Format(TEXT("VP Lat:           %7.2f ms"), m_stats.entryLatencyMs);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// DS Latency
	line.Format(TEXT("DS Lat:           %7.2f ms"), m_stats.exitLatencyMs);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Separator
	y += 4;

	// Queue info
	line.Format(TEXT("Queue:             %llu/%llu %s"), m_stats.currentQueueSize, m_stats.maxQueueSize, m_stats.isQueueFull ? TEXT( "[FULL]") : TEXT(""));
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	//line.Format(TEXT("Queue Full:        %s"), m_stats.isQueueFull ? TEXT("YES") : TEXT("No "));
	//DrawText(hdc, line, PADDING, y);
	//y += LINE_HEIGHT;

	// Frame stats
	line.Format(TEXT("VFrames:           %llu"), m_stats.capturedFrames);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	line.Format(TEXT("Dropped:           %llu/%llu"), m_stats.capturedDroppedFrames, m_stats.queueDroppedFrames);
	DrawText(hdc, line, PADDING, y);

	SelectObject(hdc, oldFont);
}

void StatsOverlayWindow::DrawText(HDC hdc, const CString& text, int x, int y)
{
	TextOut(hdc, x, y, text, text.GetLength());
}

CString StatsOverlayWindow::FormatTime(double seconds)
{
	CString result;

	if (seconds < 60.0)
	{
		result.Format(TEXT("%.1fs"), seconds);
	}
	else if (seconds < 3600.0)
	{
		int minutes = (int)(seconds / 60.0);
		int secs = (int)(seconds - minutes * 60.0);
		result.Format(TEXT("%dm %ds"), minutes, secs);
	}
	else
	{
		int hours = (int)(seconds / 3600.0);
		int minutes = (int)((seconds - hours * 3600.0) / 60.0);
		result.Format(TEXT("%dh %dm"), hours, minutes);
	}

	return result;
}

CString StatsOverlayWindow::FormatQueueStatus()
{
	CString result;
	result.Format(TEXT("Queue: %zu / %zu"), m_stats.currentQueueSize, m_stats.maxQueueSize);
	return result;
}