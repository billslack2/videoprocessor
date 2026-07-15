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
	// Video format info
	CString resolution;        // e.g., "3840x2160"
	double refreshRate = 0.0;  // Hz (legacy - still used for display)
	CString eotf;              // e.g., "PQ"
	CString colorspace;        // e.g., "BT.2020"
	CString pixelFormat;       // e.g., "P010"

	// Frame rate measurement and PPM tracking (NEW)
	double theoreticalRefreshRate = 0.0;  // Expected refresh rate from display mode (Hz)
	double measuredRefreshRate = 0.0;     // Actual measured frame rate (Hz)
	int ppmDeviation = 0;                 // PPM deviation between theoretical and measured

	// Renderer settings
	CString method;            // e.g., "Rational-Rational"
	int frameOffsetMs = 0;
	double hwLatencyMs = 0.0;  // Hardware latency

	// PPM Correction info (NEW)
	int ppmCorrection = 0;           // PPM value from VideoProcessor.cfg (e.g., 5, -3, 0)
	bool hasPPMCorrection = false;   // Whether PPM correction is being applied
	CString ppmSource;               // Source of PPM value (e.g., "VideoProcessor.cfg", "default")

	// Queue stats
	size_t currentQueueSize = 0;
	size_t maxQueueSize = 0;
	bool isQueueFull = false;

	// Latency
	double entryLatencyMs = 0.0;  // VP Latency
	double exitLatencyMs = 0.0;   // DS Latency

	// Frame counts
	uint64_t capturedFrames = 0;
	uint64_t capturedDroppedFrames = 0;
	uint64_t queueDroppedFrames = 0;
	uint64_t sceneDetectCorrectionDrops = 0;

	// Video info
	CString videoConversion;

	// Conversion performance (NEW - for V210?P010 etc.)
	double currentConversionTimeUs = 0.0;      // Latest conversion time in microseconds
	double avgConversionTime10s = 0.0;         // 10-second rolling average (?s)
	double maxConversionTime10s = 0.0;         // 10-second rolling maximum (?s)
	bool hasConversionData = false;            // Whether conversion performance data is available

	// Reset tracking - use simple types only
	uint64_t framesSinceReset = 0;
	double secondsSinceReset = 0.0;
	size_t maxQueueSizeSinceReset = 0;
	uint64_t capturedFramesAtReset = 0;
	ULONGLONG lastResetTickCount = 0;  // Use GetTickCount64() instead of chrono

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

	// Window constants - adjusted for 23px font and additional lines
	static const int MARGIN_RIGHT = 500;
	static const int MARGIN_BOTTOM = 320;    // Move up 20 pixels (was 300)
	static const int WINDOW_WIDTH = 420;     // Keep width the same
	static const int WINDOW_HEIGHT = 495;    // Extra row for Scene Detect correction statistics
	static const int PADDING = 10;
	static const int LINE_HEIGHT = 23;     // Match font size for better spacing

	// Colors - MadVR style with 15% darker background and bright white text
	static const COLORREF BACKGROUND_COLOR = RGB(43, 43, 43);  // 15% darker than RGB(50,50,50)
	static const COLORREF TEXT_COLOR = RGB(255, 255, 255);     // Bright white for better visibility
	static const COLORREF HIGHLIGHT_COLOR = RGB(255, 255, 255); // Bright white for highlights
};
