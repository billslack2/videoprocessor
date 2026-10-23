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
    // A cheap geometric/history nomination, never pixel or crop authority.
    // Workers inspect pixels against this immutable reference; callers recheck
    // it against the live model before admitting the completed result.
    struct InwardCaptionReference
    {
        bool valid = false;
        ActivePictureBounds base, candidate, remembered;
    };
    InwardCaptionReference NominateInwardCaptionReference(
        const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
        const ActivePictureBounds& establishedBase,
        const ActivePictureTransitionModel& model);
    bool InwardCaptionReferenceIsCurrent(const InwardCaptionReference& reference,
        const ActivePictureBounds& establishedBase,
        const ActivePictureTransitionModel& model);
    InwardCaptionEvidence InspectInwardCaptionEvidence(
        const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
        const InwardCaptionReference& reference,
        const AnalysisEvaluationBudget* budget = nullptr);
    InwardCaptionEvidence InspectInwardCaptionEvidence(
        const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
        const ActivePictureBounds& establishedBase,
        const ActivePictureTransitionModel& model,
        const AnalysisEvaluationBudget* budget = nullptr);
}
