/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include "pch.h"
#include "StatsOverlayWindow.h"

BEGIN_MESSAGE_MAP(StatsOverlayWindow, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

StatsOverlayWindow::StatsOverlayWindow() :
    m_isVisible(false),
    m_queueSize(0),
    m_queueMax(0),
    m_exitLatencyMs(0.0),
    m_entryLatencyMs(0.0),
    m_captureDrops(0),
    m_queueDrops(0),
    m_frameOffsetMs(0),
    m_rationalOffset(0),
    m_vFramesCaptured(0),
    m_hardwareLatencyMs(0.0),
    m_clockDescription(_T("")),
    m_startStopMethod(_T("")),
    m_discontinuityCount(0),
    m_reAnchorCount(0),
    m_isQueueNearFull(false),
    m_timestampDriftMs(0.0)
{
}

StatsOverlayWindow::~StatsOverlayWindow()
{
    if (m_font.GetSafeHandle())
        m_font.DeleteObject();
}

BOOL StatsOverlayWindow::Create(CWnd* pParentWnd)
{
    // Register window class with transparent background
    LPCTSTR className = AfxRegisterWndClass(
        CS_HREDRAW | CS_VREDRAW,
        ::LoadCursor(NULL, IDC_ARROW),
        (HBRUSH)GetStockObject(NULL_BRUSH),  // Transparent background
        NULL);

    // Create window: 350px wide, 400px tall (taller for diagnostic stats)
    // WS_EX_LAYERED allows transparency
    // WS_EX_TOPMOST keeps it on top of everything
    // WS_EX_TRANSPARENT allows clicks to pass through to video player
    // WS_EX_NOACTIVATE prevents it from stealing focus
    BOOL result = CreateEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        className,
        _T("Stats Overlay"),
        WS_POPUP,
        0, 0, 350, 400,  // Taller to accommodate diagnostic stats
        pParentWnd->GetSafeHwnd(),
        NULL);

    if (!result)
        return FALSE;

    // Set window to 70% opacity (semi-transparent so you can see through it)
    ::SetLayeredWindowAttributes(m_hWnd, 0, 180, LWA_ALPHA);  // 180/255 = ~70% opacity

    // Create font to match MadVR stats (smaller, non-bold, Consolas)
    m_font.CreateFont(
        24,                        // Height (50% larger than 16)
        0,                         // Width (0 = auto)
        0,                         // Escapement
        0,                         // Orientation
        FW_NORMAL,                 // Weight (normal, not bold)
        FALSE,                     // Italic
        FALSE,                     // Underline
        FALSE,                     // StrikeOut
        DEFAULT_CHARSET,           // CharSet
        OUT_DEFAULT_PRECIS,        // OutPrecision
        CLIP_DEFAULT_PRECIS,       // ClipPrecision
        CLEARTYPE_QUALITY,         // Quality (ClearType for readability)
        FIXED_PITCH | FF_MODERN,   // PitchAndFamily (monospace)
        _T("Consolas"));           // Facename

    return TRUE;
}

void StatsOverlayWindow::Toggle()
{
    m_isVisible = !m_isVisible;

    if (m_isVisible)
    {
        // Get the PRIMARY MONITOR dimensions (where MadVR is rendering)
        HMONITOR hMonitor = MonitorFromWindow(GetParent()->GetSafeHwnd(), MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);

        // Use the full monitor dimensions
        int monitorWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        int monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

        // Position: 100px from right edge, 300px from bottom
        int windowWidth = 350;
        int windowHeight = 400;  // Match new height
        int x = mi.rcMonitor.left + (monitorWidth - windowWidth - 100);  // 100px from right edge
        int y = mi.rcMonitor.top + (monitorHeight - windowHeight - 300);  // 300px from bottom

        SetWindowPos(&wndTopMost, x, y, windowWidth, windowHeight, SWP_NOACTIVATE);

        ShowWindow(SW_SHOWNOACTIVATE);
        Invalidate();
    }
    else
    {
        ShowWindow(SW_HIDE);
    }
}

void StatsOverlayWindow::UpdateStats(
    size_t queueSize,
    size_t queueMax,
    double exitLatencyMs,
    double entryLatencyMs,
    uint64_t captureDrops,
    uint64_t queueDrops,
    int frameOffsetMs,
    int rationalOffset,
    uint64_t vFramesCaptured,
    double hardwareLatencyMs,
    const CString& clockDescription,
    const CString& startStopMethod,
    uint64_t discontinuityCount,
    uint64_t reAnchorCount,
    bool isQueueNearFull,
    double timestampDriftMs)
{
    m_queueSize = queueSize;
    m_queueMax = queueMax;
    m_exitLatencyMs = exitLatencyMs;
    m_entryLatencyMs = entryLatencyMs;
    m_captureDrops = captureDrops;
    m_queueDrops = queueDrops;
    m_frameOffsetMs = frameOffsetMs;
    m_rationalOffset = rationalOffset;
    m_vFramesCaptured = vFramesCaptured;
    m_hardwareLatencyMs = hardwareLatencyMs;
    m_clockDescription = clockDescription;
    m_startStopMethod = startStopMethod;
    m_discontinuityCount = discontinuityCount;
    m_reAnchorCount = reAnchorCount;
    m_isQueueNearFull = isQueueNearFull;
    m_timestampDriftMs = timestampDriftMs;

    // Redraw if visible
    if (m_isVisible && m_hWnd)
    {
        // Re-assert topmost status (in case renderer restarted)
        SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Invalidate();
    }
}

void StatsOverlayWindow::OnPaint()
{
    CPaintDC dc(this);

    // Gray opaque background (RGB 64, 64, 64 - dark gray)
    CRect rect;
    GetClientRect(&rect);
    
    CBrush bgBrush(RGB(64, 64, 64));
    dc.FillRect(&rect, &bgBrush);

    // Set up text rendering
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(255, 255, 255));  // White text
    CFont* pOldFont = dc.SelectObject(&m_font);

    // Format stats text with diagnostic information
    CString stats;
    
    // Build queue status indicator  
    CString queueStatus = m_isQueueNearFull ? _T(" [FULL]") : _T("");

    stats.Format(
        _T("Clock: %s\n")
        _T("Method: %s\n")
        _T("V Frames: %I64u\n")
        _T("Queue:    %zu / %zu%s\n")
        _T("HW Lat:   %.1f ms\n")
        _T("VP Lat:   %.1f ms\n")
        _T("DS Lat:   %.1f ms\n")
        _T("Cap Drop: %I64u\n")
        _T("Que Drop: %I64u\n")
        _T("Offset:   %d ms\n")
        _T("--- CLOCK_RATIONAL Diagnostics ---\n")
        _T("Discontin: %I64u\n")
        _T("Re-Anchor: %I64u\n")
        _T("Drift:     %.2f ms\n"),
        m_clockDescription.IsEmpty() ? _T("Unknown") : m_clockDescription,
        m_startStopMethod.IsEmpty() ? _T("Unknown") : m_startStopMethod,
        m_vFramesCaptured,
        m_queueSize, m_queueMax, queueStatus,
        m_hardwareLatencyMs,
        m_entryLatencyMs,
        m_exitLatencyMs,
        m_captureDrops,
        m_queueDrops,
        m_frameOffsetMs,
        m_discontinuityCount,
        m_reAnchorCount,
        m_timestampDriftMs);

    // Draw text with some padding
    CRect textRect = rect;
    textRect.DeflateRect(10, 10);
    dc.DrawText(stats, &textRect, DT_LEFT | DT_TOP);

    dc.SelectObject(pOldFont);
}

BOOL StatsOverlayWindow::OnEraseBkgnd(CDC* pDC)
{
    // Don't erase background - we handle it in OnPaint
    return TRUE;
}
