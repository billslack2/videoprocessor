#pragma once

#include <cmath>
#include <algorithm>
#include "AlphaSourceCropPolicy.h"

// Physical aspects describe the projected image. Rectangles describe output
// pixels before the lens. Never feed the raster aspect into crop/NLS policy.
namespace AnamorphicPresentation
{
    inline bool ValidLens(double lens)
    {
        return std::isfinite(lens) && lens >= 0.5 && lens <= 2.0;
    }

    inline double PhysicalTarget(bool configured, double screen,
        double outputAspect, double lens)
    {
        const double aspect = configured ? screen : outputAspect * lens;
        return ValidLens(lens) && std::isfinite(aspect) && aspect > 0.0 ? aspect : 0.0;
    }

    struct Screen
    {
        AlphaSourceCrop::PresentationRect rect;
        double physicalAspect = 0.0;
        double rasterAspect = 0.0;
        int effectivePadding = 0;
        bool valid = false;
    };

    inline Screen FitScreen(bool configured, double screen, double lens,
        const AlphaSourceCrop::PresentationRect& available,
        AlphaSourceCrop::VerticalPictureAlignment alignment, int padding)
    {
        Screen result;
        const double height = available.bottom - available.top;
        if (height <= 0.0 || !ValidLens(lens)) return result;
        result.physicalAspect = PhysicalTarget(configured, screen,
            (available.right - available.left) / height, lens);
        result.rasterAspect = result.physicalAspect / lens;
        const auto fit = AlphaSourceCrop::FitAspect(result.rasterAspect, available, alignment);
        if (!fit.valid) return result;
        result.rect = fit.picture;
        result.valid = true;
        if (fit.unusedAxis == AlphaSourceCrop::UnusedSpaceAxis::VERTICAL &&
            alignment != AlphaSourceCrop::VerticalPictureAlignment::CENTER && padding > 0)
        {
            const bool top = alignment == AlphaSourceCrop::VerticalPictureAlignment::TOP;
            const double slack = top ? available.bottom - result.rect.bottom :
                result.rect.top - available.top;
            result.effectivePadding = std::max(0, std::min(padding,
                static_cast<int>(std::floor(slack))));
            const double shift = top ? result.effectivePadding : -result.effectivePadding;
            result.rect.top += shift;
            result.rect.bottom += shift;
        }
        return result;
    }

    inline AlphaSourceCrop::CenteredFitDecision FitPicture(double sourceAspect,
        double lens, const Screen& screen,
        AlphaSourceCrop::VerticalPictureAlignment alignment)
    {
        if (!screen.valid || !ValidLens(lens)) return {};
        return AlphaSourceCrop::FitAspect(sourceAspect / lens, screen.rect, alignment);
    }

    // Insets remain output pixels, as before. Fit these dimensions uniformly
    // after compensation so the physical lens restores the bitmap proportions.
    inline float OverlayWidth(float width, double lens)
    {
        return ValidLens(lens) ? static_cast<float>(width / lens) : width;
    }
}
