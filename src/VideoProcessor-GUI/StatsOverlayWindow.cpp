/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "StatsOverlayWindow.h"
#include <algorithm>
#include <cmath>
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
	, m_windowHeight(610)
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
	int y = monitorInfo.rcMonitor.bottom - MARGIN_BOTTOM - m_windowHeight;

	// Create the overlay window with layered window style for transparency
	m_hwnd = CreateWindowEx(
		WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
		WINDOW_CLASS_NAME,
		TEXT("Stats Overlay"),
		WS_POPUP,
		x, y, WINDOW_WIDTH, m_windowHeight,
		nullptr,                      // No parent window
		nullptr,                      // No menu
		GetModuleHandle(nullptr),     // hInstance
		(LPVOID)this);                // lpParam

	if (!m_hwnd)
	{
		return false;
	}

	// Set up layered window for transparency
	SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 120, LWA_ALPHA); // 120/255 = ~47% opacity (50% more transparent than 240/255)

	// Create fonts - match MadVR stats overlay style but 50% larger
	HDC hdc = GetDC(m_hwnd);
	if (!hdc)
	{
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
		return false;
	}
	
	// Reduce font size by 10%: 25 -> 23 pixels (25 * 0.9 = 22.5, rounded to 23)
	int fontHeight = 23;

	m_font = CreateFont(
		fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));

	m_boldFont = CreateFont(
		fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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
	int requiredHeight = 0;
	{
		std::lock_guard<std::mutex> lock(m_statsMutex);
		m_stats = stats;
		requiredHeight = CalculateRequiredHeight(m_stats);
	}

	if (requiredHeight != m_windowHeight)
	{
		m_windowHeight = requiredHeight;
		UpdatePosition(m_parentHwnd);
	}

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
	int y = monitorInfo.rcMonitor.bottom - MARGIN_BOTTOM - m_windowHeight;
	y = std::max(y, static_cast<int>(monitorInfo.rcMonitor.top));

	SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, WINDOW_WIDTH, m_windowHeight,
		SWP_NOACTIVATE);
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

	// Create semi-transparent background (no border)
	HBRUSH brush = CreateSolidBrush(BACKGROUND_COLOR);
	FillRect(hdc, &rect, brush);
	DeleteObject(brush);

	// Border removed as requested
}

void StatsOverlayWindow::DrawStats(HDC hdc)
{
	std::lock_guard<std::mutex> lock(m_statsMutex);

	SetBkMode(hdc, TRANSPARENT);
	HFONT oldFont = (HFONT)SelectObject(hdc, m_font);

	int y = PADDING;
	SetTextColor(hdc, TEXT_COLOR);

	// Fixed-width format with consistent spacing - use 18 chars for label, then value
	CString line;

	// Resolution
	line.Format(TEXT("Resolution:       %-s"), m_stats.resolution.IsEmpty() ? TEXT("---") : m_stats.resolution);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Refresh rate
	line.Format(TEXT("Refresh:          %.6f Hz"), m_stats.refreshRate);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Actual display refresh period as reported by the Desktop Window Manager.
	if (m_stats.displayRefreshRate > 0.0)
		line.Format(TEXT("- Display Rate:   %.6f Hz%s"), m_stats.displayRefreshRate,
			m_stats.displayRefreshRateOverridden ? TEXT(" *") : TEXT(""));
	else
		line.Format(TEXT("- Display Rate:   ---"));
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Measured refresh rate (calculated from frame arrivals)
	if (m_stats.measuredRefreshRate > 0.0)
	{
		line.Format(TEXT("- Est. Rate:      %.6f Hz"), m_stats.measuredRefreshRate);
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
		
		// PPM deviation between theoretical and measured rates
		line.Format(TEXT("- Est. PPM:       %+d ppm"), m_stats.ppmDeviation);
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
	}

	// PPM Correction
	if (m_stats.hasPPMCorrection || (!m_stats.ppmSource.IsEmpty() && m_stats.ppmSource != TEXT("N/A")))
	{
		line.Format(TEXT("- Applied PPM:    %+d ppm"), m_stats.ppmCorrection);
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
	}

	// Corrected refresh rate (only for rational modes with PPM correction)
	if ((m_stats.method == TEXT("Rational-Rational") || m_stats.method == TEXT("Clock-Rational")) &&
		m_stats.hasPPMCorrection)
	{
		// Calculate corrected rate: nominal + (nominal * PPM / 1000000)
		double correctedRate = m_stats.refreshRate * (1.0 + (double) (m_stats.ppmCorrection*-1) / 1000000.0); //TODO: thats a hacky way to calc by applying -1; oh well
		line.Format(TEXT("- Delivery Rate:  %.6f Hz"), correctedRate);
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
	}

	// EOTF
	line.Format(TEXT("EOTF:             %-s"), m_stats.eotf.IsEmpty() ? TEXT("---") : m_stats.eotf);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Colorspace
	line.Format(TEXT("Colorspace:       %-s"), m_stats.colorspace.IsEmpty() ? TEXT("---") : m_stats.colorspace);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Pixel Format
	line.Format(TEXT("Pixel Format:     %-s"), m_stats.pixelFormat.IsEmpty() ? TEXT("---") : m_stats.pixelFormat);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	if (!m_stats.outputMode.IsEmpty())
	{
		const int separator = m_stats.outputMode.Find(TEXT(" -> "));
		const CString requested = separator >= 0
			? m_stats.outputMode.Left(separator) : m_stats.outputMode;
		const CString actual = separator >= 0
			? m_stats.outputMode.Mid(separator + 4) : TEXT("---");
		line.Format(TEXT("Out Req:          %-s"),
			static_cast<LPCTSTR>(requested));
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
		line.Format(TEXT("Out Actual:       %-s"),
			static_cast<LPCTSTR>(actual));
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
	}

	// Video Conversion
	line.Format(TEXT("Video Conv:       %-s"), m_stats.videoConversion.IsEmpty() ? TEXT("---") : m_stats.videoConversion);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	line.Format(TEXT("Shader Rule:      %-s"),
		m_stats.activeShaderRule.IsEmpty() ? TEXT("None") :
		static_cast<LPCTSTR>(m_stats.activeShaderRule));
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	if (m_stats.activeShaders.empty())
		line.Format(TEXT("Shaders:          None"));
	else
		line.Format(TEXT("Shaders:          %zu Active"), m_stats.activeShaders.size());
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	for (const CString& shader : m_stats.activeShaders)
	{
		line.Format(TEXT(" - %-s"), static_cast<LPCTSTR>(shader));
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
	}
	
	// Conversion Performance (show if available)
	if (m_stats.hasConversionData)
	{
		// Calculate frame time and conversion percentage
		double frameTimeMs = 1000.0 / m_stats.refreshRate;
		double currentConvMs = m_stats.currentConversionTimeUs / 1000.0;
		double conversionPct = (currentConvMs / frameTimeMs) * 100.0;
	
		
			line.Format(TEXT("Conv Time:        %.2f ms"), 
				currentConvMs, conversionPct);
		
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
		
		// 10-second average and max on one line (convert μs to ms)
		line.Format(TEXT("10s Avg/Max:      %.2f / %.2f ms"), 
			m_stats.avgConversionTime10s / 1000.0, m_stats.maxConversionTime10s / 1000.0);
		DrawText(hdc, line, PADDING, y);
		y += LINE_HEIGHT;
	}

	// Separator
	y += 4;

	// Method
	line.Format(TEXT("Method:           %-s"), m_stats.method.IsEmpty() ? TEXT("---") : m_stats.method);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;



	// Frame Offset
	line.Format(TEXT("Offset:           %d ms"), m_stats.frameOffsetMs);

	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Separator
	y += 4;

	// VP Latency
	line.Format(TEXT("VP Lat:           %.2f ms"), m_stats.entryLatencyMs);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// DS Latency
	line.Format(TEXT("DS Lat:           %.2f ms"), m_stats.exitLatencyMs);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Separator
	y += 4;

	// Queue info
	line.Format(TEXT("Queue R/C/T:      %zu/%zu/%zu/%zu%s"),
		m_stats.rawQueueSize, m_stats.convertedQueueSize,
		m_stats.currentQueueSize, m_stats.maxQueueSize,
		m_stats.isQueueFull ? TEXT(" [FULL]") : TEXT(""));
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Frame stats
	line.Format(TEXT("VFrames:          %llu"), m_stats.rendererCapturedFrames);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	line.Format(TEXT("Dropped:          %llu/%llu"), m_stats.capturedDroppedFrames, m_stats.queueDroppedFrames);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	line.Format(TEXT("Scene Mode:       %-s"),
		m_stats.sceneDetectMode.IsEmpty() ? TEXT("Off") :
		static_cast<LPCTSTR>(m_stats.sceneDetectMode));
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	const bool sceneModeOff = m_stats.sceneDetectMode.IsEmpty() ||
		m_stats.sceneDetectMode.CompareNoCase(TEXT("Off")) == 0;
	if (sceneModeOff)
		line.Format(TEXT(" - Status:        None"));
	else if (!m_stats.sceneTimingReady)
		line.Format(TEXT(" - Status:        Warming"));
	else if (!m_stats.sceneTimingRatesCompatible)
		line.Format(TEXT(" - Status:        Unavailable"));
	else
		line.Format(TEXT(" - Status:        Ready"));
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// Only advertise a correction that is concrete and close enough to be
	// meaningful. A multi-day estimate is effectively no actionable plan.
	constexpr double kMaximumPlanSeconds = 24.0 * 60.0 * 60.0;
	const bool hasActionablePlan =
		!sceneModeOff &&
		m_stats.sceneTimingReady &&
		m_stats.sceneTimingRatesCompatible &&
		m_stats.sceneCorrectionPredictionValid &&
		std::isfinite(m_stats.sceneSecondsUntilCorrection) &&
		std::fabs(m_stats.sceneSecondsUntilCorrection) <= kMaximumPlanSeconds;
	// For five seconds after a correction, use the Plan row to confirm how the
	// scene boundary lined up with its deadline. It then automatically returns
	// to the live signed countdown for the next correction.
	constexpr uint64_t kCorrectionResultVisibilityMs = 5000;
	constexpr double kOnTimeToleranceSeconds = 1.0;
	const uint64_t nowTick = GetTickCount64();
	const bool showLastCorrection =
		!sceneModeOff &&
		m_stats.sceneLastCorrectionValid &&
		nowTick >= m_stats.sceneLastCorrectionTick &&
		(nowTick - m_stats.sceneLastCorrectionTick) <= kCorrectionResultVisibilityMs;
	if (showLastCorrection)
	{
		const TCHAR* action = m_stats.sceneLastCorrectionAction > 0 ?
			TEXT("Repeat") : TEXT("Drop");
		const double timing = m_stats.sceneLastCorrectionSecondsFromDeadline;
		if (std::fabs(timing) <= kOnTimeToleranceSeconds)
			line.Format(TEXT(" - Plan:          %s On-Time"), action);
		else
			line.Format(TEXT(" - Plan:          %s %s (%s)"), action,
				timing > 0.0 ? TEXT("Early") : TEXT("Late"),
				static_cast<LPCTSTR>(FormatTime(std::fabs(timing))));
	}
	else if (hasActionablePlan)
	{
		line.Format(TEXT(" - Plan:          %s in %s"),
			m_stats.sceneCorrectionAction > 0 ? TEXT("Repeat") : TEXT("Drop"),
			static_cast<LPCTSTR>(FormatTime(m_stats.sceneSecondsUntilCorrection)));
	}
	else
	{
		line.Format(TEXT(" - Plan:          None"));
	}
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	// These are source-side actions at detected scene boundaries.
	line.Format(TEXT(" - Action D/R:    %llu / %llu"),
		m_stats.sceneDetectCorrectionDrops,
		m_stats.sceneDetectCorrectionRepeats);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	line.Format(TEXT(" - Detected:      %llu"), m_stats.sceneDetectDetected);
	DrawText(hdc, line, PADDING, y);
	y += LINE_HEIGHT;

	SelectObject(hdc, oldFont);
}

int StatsOverlayWindow::CalculateRequiredHeight(const StatsData& stats) const
{
	// Nineteen rows are always rendered. The remaining rows mirror the exact
	// optional conditions in DrawStats so the background follows its content.
	size_t lineCount = 19;
	if (stats.measuredRefreshRate > 0.0)
		lineCount += 2;
	if (stats.hasPPMCorrection ||
		(!stats.ppmSource.IsEmpty() && stats.ppmSource != TEXT("N/A")))
	{
		++lineCount;
	}
	if ((stats.method == TEXT("Rational-Rational") ||
		stats.method == TEXT("Clock-Rational")) && stats.hasPPMCorrection)
	{
		++lineCount;
	}
	if (stats.hasConversionData)
		lineCount += 2;
	if (!stats.outputMode.IsEmpty())
		lineCount += 2;

	// The selected rule and shader summary are always visible, followed by one row per active
	// shader. DrawStats also contains three four-pixel section separators.
	lineCount += 2 + stats.activeShaders.size();
	return PADDING * 2 + static_cast<int>(lineCount) * LINE_HEIGHT + 12;
}

void StatsOverlayWindow::DrawText(HDC hdc, const CString& text, int x, int y)
{
	TextOut(hdc, x, y, text, text.GetLength());
}

CString StatsOverlayWindow::FormatTime(double seconds)
{
	CString result;
	const bool negative = seconds < 0.0;
	const uint64_t totalSeconds = static_cast<uint64_t>(
		std::ceil(std::fabs(seconds)));
	const uint64_t hours = totalSeconds / 3600;
	const uint64_t minutes = (totalSeconds % 3600) / 60;
	const uint64_t secs = totalSeconds % 60;
	if (hours > 0)
		result.Format(negative ? TEXT("-%lluh%llum%llus") : TEXT("%lluh%llum%llus"), hours, minutes, secs);
	else if (minutes > 0)
		result.Format(negative ? TEXT("-%llum%llus") : TEXT("%llum%llus"), minutes, secs);
	else
		result.Format(negative ? TEXT("-%llus") : TEXT("%llus"), secs);

	return result;
}

CString StatsOverlayWindow::FormatQueueStatus()
{
	CString result;
	result.Format(TEXT("Queue R/C/T: %zu/%zu/%zu/%zu"),
		m_stats.rawQueueSize, m_stats.convertedQueueSize,
		m_stats.currentQueueSize, m_stats.maxQueueSize);
	return result;
}
