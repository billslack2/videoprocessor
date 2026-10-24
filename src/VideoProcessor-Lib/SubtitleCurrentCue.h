#pragma once
#include <SubtitleCueContinuation.h>

struct CurrentSubtitleCueDecision {
    SubtitleCueContinuation proof;
    SubtitleBoxResult accepted;
};

// Preparation-call binding is separate from proof.physicalBase, which retains
// acquired source coordinates across unrelated global picture metadata changes.
// This token is synchronous: it keeps neither a read-budget pointer nor owner.
struct PreparedCurrentSubtitleCue {
    SubtitleCueContinuation proof;
    ActivePictureBounds inputBase;
    AnalysisLumaSource inputSource;
    bool inputBaseAvailable=false;
    bool MatchesInput(const AnalysisLumaSource& source,const ActivePictureBounds& base,bool available) const {
        return available==inputBaseAvailable && source.IsValid() &&
            base.left==inputBase.left && base.top==inputBase.top && base.right==inputBase.right &&
            base.bottom==inputBase.bottom && base.rasterWidth==inputBase.rasterWidth && base.rasterHeight==inputBase.rasterHeight &&
            source.data==inputSource.data && source.dataBytes==inputSource.dataBytes &&
            source.width==inputSource.width && source.height==inputSource.height &&
            source.rowBytes==inputSource.rowBytes && source.chromaRowBytes==inputSource.chromaRowBytes &&
            source.format==inputSource.format && source.encoding==inputSource.encoding &&
            source.range==inputSource.range && source.colorspace==inputSource.colorspace &&
            source.generation==inputSource.generation && source.rgbBitAlignment==inputSource.rgbBitAlignment;
    }
};

// Prepare once before optional pending work. A COMPLETE proof supersedes that
// weaker work; only an exact retained UNKNOWN may request pending fallback.
inline PreparedCurrentSubtitleCue PrepareCurrentSubtitleCue(
    SubtitleCueContinuationGuard& guard,const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
    const ActivePictureBounds& base,bool baseAvailable,int holdMs,double frameMs,
    size_t maxSamples=SubtitleCurrentCueInspectionSampleLimit) {
    PreparedCurrentSubtitleCue out;out.inputBase=base;out.inputBaseAvailable=baseAvailable;
    out.inputSource=source;out.inputSource.readBudget=nullptr;out.inputSource.backingOwner.reset();
    if(!baseAvailable){guard.Reset();return out;}
    if(guard.NeedsCurrentPixelRevalidation(preview) || preview.current.pendingRefresh || !preview.current.analyzed)
        out.proof=guard.Prepare(preview,source,base,holdMs,frameMs,maxSamples);
    return out;
}
// Consume the same source-bound preparation after optional pending resolution.
// This function never re-runs the current inspector or charges a second lease.
inline CurrentSubtitleCueDecision ConsumePreparedCurrentSubtitleCue(
    SubtitleCueContinuationGuard& guard,SubtitleBoxPresentation& presentation,
    const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
    const ActivePictureBounds& base,bool baseAvailable,int holdMs,double frameMs,
    const PreparedCurrentSubtitleCue& prepared,bool pendingPixelsVerified=false,
    const ActivePictureFrameIdentity* backlogTemplateIdentity=nullptr,
    bool backlogRetirementOnly=false)
{
    CurrentSubtitleCueDecision out;
    out.proof=prepared.proof;
    // Pending resolution may replace raw metadata, never this source binding.
    if((out.proof.valid || out.proof.candidateRetained) &&
       (!baseAvailable || !prepared.MatchesInput(source,base,baseAvailable) ||
        source.width!=preview.current.width || source.height!=preview.current.height ||
        source.generation!=preview.current.identity.transportGeneration ||
        !guard.CanInspectCurrentObservation(preview.current) ||
        !SameAnalysisFrameIdentity(out.proof.identity,preview.current.identity) ||
        out.proof.policyGeneration!=preview.policyGeneration ||
        out.proof.continuityGeneration!=preview.continuityGeneration ||
        preview.current.policyGeneration!=preview.policyGeneration ||
        preview.current.continuityGeneration!=preview.continuityGeneration)) {
        guard.Reset();out.proof={};out.proof.reason="prepared-source-context-changed";
    }
    if(out.proof.RetainsCandidateFor(preview.current.identity,preview.policyGeneration,
        preview.continuityGeneration)) {
        if(presentation.RetainCandidateForIncompleteInspection(preview,out.proof.previousIdentity)) {
            out.accepted=presentation.CurrentRawFallbackForIncompleteInspection(
                preview,holdMs,frameMs,pendingPixelsVerified);
            // Any displayed fallback is authorized solely by ordinary current
            // raw admission. The invalid token retains caches only on blanks.
            if(out.accepted.detected)out.proof.candidateRetained=false;
            return out;
        }
        // A retained inspector cannot rebind itself to a different selection.
        out.proof.candidateRetained=false;guard.Reset();
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
inline CurrentSubtitleCueDecision ConsumeCurrentSubtitleCue(
    SubtitleCueContinuationGuard& guard,SubtitleBoxPresentation& presentation,
    const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
    const ActivePictureBounds& base,bool baseAvailable,int holdMs,double frameMs,
    bool pendingPixelsVerified=false,const ActivePictureFrameIdentity* backlogTemplateIdentity=nullptr,
    bool backlogRetirementOnly=false) {
    const auto prepared=PrepareCurrentSubtitleCue(guard,preview,source,base,baseAvailable,holdMs,frameMs);
    return ConsumePreparedCurrentSubtitleCue(guard,presentation,preview,source,base,baseAvailable,holdMs,frameMs,
        prepared,pendingPixelsVerified,backlogTemplateIdentity,backlogRetirementOnly);
}
