#pragma once
#include "../ActivePictureEvidence.h"

namespace AlphaSourceCrop
{
    // Fresh pixel evidence only; callers retain source identity, ordinary
    // temporal confirmation and atomic protection at publication.
    struct InwardCaptionEvidence
    {
        bool valid = false;
        ActivePictureEvidence picture;
        ActivePictureBounds protectedBounds;
        const char* reason = "not-evaluated";
    };
    InwardCaptionEvidence InspectInwardCaptionEvidence(
        const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
        const ActivePictureBounds& establishedBase,
        const ActivePictureTransitionModel& model);
}
