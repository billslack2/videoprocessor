#pragma once
#include <SubtitleCueContinuation.h>

struct CurrentSubtitleCueDecision {
    SubtitleCueContinuation proof;
    SubtitleBoxResult accepted;
};

// Shared production orchestration: source proof is decided before the tracker
// consumes this frame. No raw worker/bar/AR evidence is rewritten here.
inline CurrentSubtitleCueDecision ConsumeCurrentSubtitleCue(
    SubtitleCueContinuationGuard& guard,SubtitleBoxPresentation& presentation,
    const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
    const ActivePictureBounds& base,bool baseAvailable,int holdMs,double frameMs,
    bool pendingPixelsVerified=false,
    const ActivePictureFrameIdentity* backlogTemplateIdentity=nullptr,
    bool backlogRetirementOnly=false)
{
    CurrentSubtitleCueDecision out;
    if(!baseAvailable)guard.Reset();
    else if(guard.NeedsCurrentPixelRevalidation(preview) ||
        preview.current.pendingRefresh || !preview.current.analyzed) {
        out.proof=guard.Prepare(preview,source,base,holdMs,frameMs);
    }
    // COMPLETE proof has already compared fresh current glyphs. A second
    // refresh would spend the same frame's budget without adding authority.
    if(!out.proof.valid)presentation.RefreshReferencePixels(preview,source);
    out.accepted=presentation.Consume(preview,holdMs,frameMs,
        backlogTemplateIdentity,backlogRetirementOnly,pendingPixelsVerified,
        out.proof.valid?&out.proof:nullptr);
    if(out.accepted.confirmationPending)guard.Reset();
    else if(baseAvailable && out.accepted.detected && !out.proof.valid) {
        if(!guard.Remember(preview,out.accepted,base,source))
            guard.AdvanceVerified(preview,out.accepted,source,base,holdMs,frameMs);
    }
    else if(!out.accepted.detected)guard.Reset();
    return out;
}
