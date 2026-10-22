#pragma once

#include <IRenderer.h>
#include <atlstr.h>
#include <cmath>
#include <algorithm>

namespace HdrOsdFormatting
{
    inline bool Known(double value, bool allowZero = true)
    {
        return std::isfinite(value) && (allowZero ? value >= 0.0 : value > 0.0);
    }
    inline CString Nits(double value, int subNitDecimals = 5, bool allowZero = true)
    {
        if (!Known(value, allowZero)) return _T("---");
        CString text;
        // Bound the representation even for malformed finite metadata.
        if (value >= 1000000.0) text.Format(_T("%.2g"), value);
        else if (value >= 100.0) text.Format(_T("%.0f"), value);
        else if (value >= 1.0) text.Format(_T("%.2f"), value);
        else text.Format(_T("%.*f"), (std::max)(0, (std::min)(5, subNitDecimals)), value);
        if (text.Find(_T('.')) >= 0 && text.FindOneOf(_T("eE")) < 0)
        {
            text.TrimRight(_T('0')); text.TrimRight(_T('.'));
        }
        return text;
    }
    inline CString Mastering(double minimum, double maximum)
    {
        if (!Known(minimum) || !Known(maximum, false) || minimum >= maximum) return _T("---");
        return Nits(minimum) + _T("-") + Nits(maximum);
    }
    inline CString Hdr10(double cll, double fall, double minimum, double maximum)
    {
        CString result;
        result.Format(_T("CLL %s FALL %s | M %s"),
            Nits(cll, 5, false).GetString(), Nits(fall, 5, false).GetString(),
            Mastering(minimum, maximum).GetString());
        return result;
    }
    inline CString Static(const RendererHdrStatus& status)
    {
        if (!status.hdr) return {};
        if (!status.dolbyVision)
            return Hdr10(status.maxCll, status.maxFall, status.masteringMin, status.masteringMax);
        return _T("DV | M ") + Mastering(status.masteringMin, status.masteringMax);
    }
    inline CString Measured(const RendererHdrStatus& status)
    {
        if (!status.hdr) return {};
        CString detected = status.peakDetectionEnabled ? _T("---") : _T("off");
        if (status.peakDetectionEnabled && status.detectedValid)
        {
            detected.Format(_T("peak %s"), Nits(status.detectedPeakNits, 3).GetString());
            // Stable libplacebo does not expose the maxRGB linear average;
            // zero is its documented unavailable sentinel for that field.
            if (Known(status.detectedLinearAvgNits, false))
                detected.AppendFormat(_T(" / avg %s"), Nits(status.detectedLinearAvgNits, 3).GetString());
        }
        if (status.sceneValid)
        {
            CString result;
            result.Format(_T("%s | %s %s / %s"), detected.GetString(),
                status.dolbyVision ? _T("L1") : _T("frame"),
                Nits(status.scenePeakNits, 3).GetString(), Nits(status.sceneAvgNits, 3).GetString());
            return result;
        }
        return detected;
    }
}
