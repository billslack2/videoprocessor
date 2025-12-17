/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <Windows.h>
#include <atlstr.h>
#include <mutex>

// Simple stats data structure - no complex types that need initialization
struct StatsData
{
	// Queue stats
	size_t currentQueueSize = 0;
	size_t maxQueueSize = 0;
	bool isQueueFull = false;

	// Reset tracking - use simple types only
	uint64_t framesSinceReset = 0;
	double secondsSinceReset = 0.0;
	size_t maxQueueSizeSinceReset = 0;
	uint64_t capturedFramesAtReset = 0;
	ULONGLONG lastResetTickCount = 0;  // Use GetTickCount64() instead of chrono

	// Latency
	double entryLatencyMs = 0.0;
	double exitLatencyMs = 0.0;

	// Frame counts
	uint64_t capturedFrames = 0;
	uint64_t capturedDroppedFrames = 0;
	uint64_t queueDroppedFrames = 0;

	// Video info
	double refreshRate = 0.0;
	CString videoConversion;

	void OnReset()
	{
		lastResetTickCount = GetTickCount64();
		framesSinceReset = 0;
		maxQueueSizeSinceReset = currentQueueSize;
		capturedFramesAtReset = 0;
	}

	void UpdateTimeSinceReset()
	{
		if (lastResetTickCount > 0)
		{
			ULONGLONG elapsed = GetTickCount64() - lastResetTickCount;
			secondsSinceReset = elapsed / 1000.0;
		}
	}

	void UpdateMaxQueueSize()
	{
		if (currentQueueSize > maxQueueSizeSinceReset)
			maxQueueSizeSinceReset = currentQueueSize;
	}
};

class StatsOverlayWindow
{
public:
	StatsOverlayWindow();
	~StatsOverlayWindow();

	// Lifecycle
	bool Create(HWND parentHwnd);
	void Destroy();

	// Visibility
	void Show(bool show);
	void Toggle();
	bool IsVisible() const { return m_isVisible; }
	bool IsCreated() const { return m_isCreated; }

	// Data update
	void UpdateStats(const StatsData& stats);

	// Position update
	void UpdatePosition(HWND parentHwnd);

private:
	// Window procedure
	static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	// Drawing
	void OnPaint(HDC hdc);
	void DrawBackground(HDC hdc);
	void DrawStats(HDC hdc);
	void DrawText(HDC hdc, const CString& text, int x, int y);

	// Formatting helpers
	CString FormatTime(double seconds);
	CString FormatQueueStatus();

	// Force redraw
	void ForceRedraw();

	// Class registration
	static void RegisterWindowClass();
	static bool s_classRegistered;
	static const TCHAR* WINDOW_CLASS_NAME;

	// Window handles and state
	HWND m_hwnd;
	HWND m_parentHwnd;
	bool m_isVisible;
	bool m_isCreated;

	// Fonts
	HFONT m_font;
	HFONT m_boldFont;

	// Stats data with mutex protection
	std::mutex m_statsMutex;
	StatsData m_stats;

	// Window constants
	static const int MARGIN_RIGHT = 100;
	static const int MARGIN_BOTTOM = 300;
	static const int WINDOW_WIDTH = 400;
	static const int WINDOW_HEIGHT = 350;
	static const int PADDING = 10;
	static const int LINE_HEIGHT = 22;

	// Colors - MadVR style
	static const COLORREF BACKGROUND_COLOR = RGB(50, 50, 50);
	static const COLORREF TEXT_COLOR = RGB(220, 220, 220);
	static const COLORREF HIGHLIGHT_COLOR = RGB(220, 220, 220);
};