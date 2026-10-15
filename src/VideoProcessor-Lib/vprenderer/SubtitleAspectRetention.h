#pragma once
#include <SubtitlePreviewMode.h>
#include <SubtitleBoxLookahead.h>
#include <cstring>
#include <vprenderer/AlphaSourceCropPolicy.h>

// Crop evidence, admission and recovery always run through the shared policy.
// Moved styles always undo Classic's final same-size picture translation.
// Picture placement is a mode choice, independent of cue detection or shader
// preparation. The evidence parameters remain for source compatibility; they
// do not grant caption absence, cleanup, or active-picture authority.
inline ActivePictureBounds SubtitlePresentationBounds(const AlphaSourceCrop::Decision& decision,
    SubtitlePreviewMode mode, bool replacementPrepared, bool noCaptionConfirmed,
    bool discardedStripClear = false)
{
    (void)replacementPrepared;
    (void)noCaptionConfirmed;
    (void)discardedStripClear;
    auto bounds=decision.sourceBounds;
    if(SubtitlePreviewOverridesClassicHandling(mode) && decision.applyCrop && decision.verticallyTranslated) {
        bounds.top-=decision.verticalTranslationPixels;
        bounds.bottom-=decision.verticalTranslationPixels;
    }
    return bounds;
}

// Output-time pose may advance Classic's translation only while the selected
// packet still displays that exact translated crop. Advanced modes always
// use the physical crop.
inline bool SubtitlePresentationKeepsClassicTranslation(
    const AlphaSourceCrop::Decision& decision, const ActivePictureBounds& finalBounds)
{
    const auto& shared=decision.sourceBounds;
    return decision.applyCrop && decision.verticallyTranslated &&
        decision.verticalTranslationPixels != 0 &&
        finalBounds.left==shared.left && finalBounds.top==shared.top &&
        finalBounds.right==shared.right && finalBounds.bottom==shared.bottom &&
        finalBounds.rasterWidth==shared.rasterWidth &&
        finalBounds.rasterHeight==shared.rasterHeight;
}
// An advanced subtitle mode owns the picture pose during a gradual picture
// transition. Classic's translation state may not veto an otherwise current
// moving-picture hold. Keep FIT and every other competing owner intact.
inline bool RouteAdvancedMovingPictureHold(
    AlphaSourceCrop::Input& input, SubtitlePreviewMode mode)
{
    if (!SubtitlePreviewOverridesClassicHandling(mode) || !input.movingPictureTransition)
        return false;

    auto routed = input;
    routed.verticalTranslationActive = false;
    routed.verticalTranslationConfirmationPending = false;
    routed.verticalTranslationBaseRetentionActive = false;
    routed.verticalTranslationEngageBaseRetentionActive = false;
    if (!AlphaSourceCrop::HasCurrentMovingPictureHold(routed))
        return false;

    input = routed;
    return true;
}
// Only a completed scan that found no bright ink proves an empty source cue.
// Rejected text candidates, refresh gaps, and held cues remain unknown.
inline bool ConfirmedSubtitleAbsentForPresentation(
    const SubtitleBoxObservation& current, const SubtitleBoxResult& accepted,
    const ActivePictureFrameIdentity& selectedIdentity)
{
    return SameAnalysisFrameIdentity(current.identity,selectedIdentity) &&
        current.barAuthority && current.analyzed && !current.pendingRefresh &&
        !current.text.workLimit && !current.text.detected &&
        current.text.diagnosticReason &&
        std::strcmp(current.text.diagnosticReason,"no-bright-ink")==0 &&
        !accepted.detected && !accepted.held;
}