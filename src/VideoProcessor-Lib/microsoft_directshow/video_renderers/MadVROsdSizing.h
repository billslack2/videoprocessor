#pragma once

#include <cmath>
#include <algorithm>
#include <initializer_list>

// madVR bitmap positions and sizes are in its OsdGetVideoRects coordinate
// space. That space can differ from both the monitor raster and the logical
// child-window size (DPI virtualization and windowed NLS scope canvases).
namespace MadVROsdSizing
{
struct Mapping
{
    double monitorWidth = 0.0;
    double monitorHeight = 0.0;
    double physicalHostWidth = 0.0;
    double physicalHostHeight = 0.0;
    double logicalHostWidth = 0.0;
    double logicalHostHeight = 0.0;
    double logicalChildWidth = 0.0;
    double logicalChildHeight = 0.0;
    double osdWidth = 0.0;
    double osdHeight = 0.0;
    bool fullscreen = false;
    // Independent IMadVRInfo displayModeSize; never the masked visible rect.
    double unmaskedOutputWidth = 0.0;
    double unmaskedOutputHeight = 0.0;
};

struct Screen
{
    float fittedHeight = 0.0f;
    float outputHeight = 0.0f;
    bool valid = false;
};

inline bool Positive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

inline Screen Resolve(const Mapping& mapping, bool configured, double screenAspect)
{
    Screen result;
    for (const double value : {mapping.monitorWidth, mapping.monitorHeight,
        mapping.physicalHostWidth, mapping.physicalHostHeight,
        mapping.logicalHostWidth, mapping.logicalHostHeight,
        mapping.logicalChildWidth, mapping.logicalChildHeight,
        mapping.osdWidth, mapping.osdHeight})
        if (!Positive(value)) return result;
    if (configured && !Positive(screenAspect)) return result;

    double xUnitsPerPixel = 0.0;
    double yUnitsPerPixel = 0.0;
    if (mapping.fullscreen)
    {
        // madVR's fullscreen fullOutputRect is the masked visible rectangle;
        // masking does not rescale bitmap coordinates with MASKING_AWARE. Use
        // its independently reported unmasked display mode, not visible width
        // or height, to establish raster units per physical desktop pixel.
        if (!Positive(mapping.unmaskedOutputWidth) ||
            !Positive(mapping.unmaskedOutputHeight)) return result;
        xUnitsPerPixel = mapping.unmaskedOutputWidth / mapping.monitorWidth;
        yUnitsPerPixel = mapping.unmaskedOutputHeight / mapping.monitorHeight;
    }
    else
    {
        // madVR ignores screen masking in windowed mode. Both logical sizes
        // come from the same graph-owner DPI context. Convert the actual child
        // window to physical desktop pixels before interpreting its OSD units.
        const double childWidth = mapping.physicalHostWidth *
            mapping.logicalChildWidth / mapping.logicalHostWidth;
        const double childHeight = mapping.physicalHostHeight *
            mapping.logicalChildHeight / mapping.logicalHostHeight;
        xUnitsPerPixel = mapping.osdWidth / childWidth;
        yUnitsPerPixel = mapping.osdHeight / childHeight;
    }
    if (!Positive(xUnitsPerPixel) || !Positive(yUnitsPerPixel)) return result;
    const double relativeMismatch = std::fabs(xUnitsPerPixel - yUnitsPerPixel) /
        (std::max)(xUnitsPerPixel, yUnitsPerPixel);
    if (relativeMismatch > 0.01) return result;
    // Match VP's render-output reference policy in windowed mode: fit the
    // configured screen once to the HOST canvas before NLS reduces the child.
    // A smaller window is a smaller reference canvas, not an optical zoom.
    const double referenceWidth = mapping.fullscreen ? mapping.monitorWidth : mapping.physicalHostWidth;
    const double referenceHeight = mapping.fullscreen ? mapping.monitorHeight : mapping.physicalHostHeight;
    const double physicalScreenHeight = configured
        ? (std::min)(referenceHeight, referenceWidth / screenAspect)
        : referenceHeight;
    const double fittedHeight = physicalScreenHeight * yUnitsPerPixel;
    const double outputHeight = referenceHeight * yUnitsPerPixel;
    if (!Positive(fittedHeight) || !Positive(outputHeight) ||
        fittedHeight > 1000000.0 || outputHeight > 1000000.0) return result;
    result.fittedHeight = static_cast<float>(fittedHeight);
    result.outputHeight = static_cast<float>(outputHeight);
    result.valid = true;
    return result;
}
}
