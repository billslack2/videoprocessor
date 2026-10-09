#pragma once
#include <SubtitlePreviewMode.h>
#include <vprenderer/AlphaSourceCropPolicy.h>

// Crop evidence, admission and recovery always run through the shared policy.
// Moved styles only undo Classic's final same-size picture translation. This
// does not depend on detection/composition success and cannot change the aspect.
inline ActivePictureBounds SubtitlePresentationBounds(const AlphaSourceCrop::Decision& decision,
    SubtitlePreviewMode mode)
{
    auto bounds=decision.sourceBounds;
    if(SubtitlePreviewOverridesClassicHandling(mode) && decision.applyCrop && decision.verticallyTranslated) {
        bounds.top-=decision.verticalTranslationPixels;
        bounds.bottom-=decision.verticalTranslationPixels;
    }
    return bounds;
}
