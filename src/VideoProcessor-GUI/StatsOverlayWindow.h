/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <afxwin.h>

/**
 * Transparent overlay window that displays rendering statistics
 * Toggleable via Ctrl+I keyboard shortcut
 * Positioned in right center of screen (100px from right edge)
 */
class StatsOverlayWindow : public CWnd
{
public:
    StatsOverlayWindow();
    virtual ~StatsOverlayWindow();

    // Create the overlay window
    BOOL Create(CWnd* pParentWnd);

    // Toggle visibility
    void Toggle();
    bool IsVisible() const { return m_isVisible; }

    // Update statistics
    void UpdateStats(
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
        double timestampDriftMs);

protected:
    DECLARE_MESSAGE_MAP()

    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);

private:
    bool m_isVisible;

    // Font for text rendering
    CFont m_font;

    // Statistics to display
    size_t m_queueSize;
    size_t m_queueMax;
    double m_exitLatencyMs;
    double m_entryLatencyMs;
    uint64_t m_captureDrops;
    uint64_t m_queueDrops;
    int m_frameOffsetMs;
    int m_rationalOffset;
    uint64_t m_vFramesCaptured;
    double m_hardwareLatencyMs;
    CString m_clockDescription;
    CString m_startStopMethod;

    // Diagnostic statistics
    uint64_t m_discontinuityCount;
    uint64_t m_reAnchorCount;
    bool m_isQueueNearFull;
    double m_timestampDriftMs;
};
