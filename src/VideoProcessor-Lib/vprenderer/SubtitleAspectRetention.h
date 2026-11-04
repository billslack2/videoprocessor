#pragma once
#include <SubtitlePreviewMode.h>
#include <SubtitleCutPaste.h>
#include <SubtitleBoxLookahead.h>
#include <SubtitleCurrentPreparedCueCertificate.h>
#include <cstring>
#include <vprenderer/AlphaSourceCropPolicy.h>

// Black/Reconstruction use the unshifted native presentation. Apply this mode
// choice BEFORE evaluation, recovery and admission so the exact submitted pose
// and its certificate agree. Raw content/FIT/picture evidence remains intact.
inline AlphaSourceCrop::Input CanonicalSubtitleModeCropInput(
    AlphaSourceCrop::Input input,SubtitlePreviewMode mode)
{
    input.subtitlePresentationMode=mode==SubtitlePreviewMode::BlackBackground
        ? AlphaSourceCrop::PreparedNativeMode::BLACK
        : mode==SubtitlePreviewMode::GeneratedGrayBackground
            ? AlphaSourceCrop::PreparedNativeMode::RECONSTRUCTION : AlphaSourceCrop::PreparedNativeMode::NONE;
    if(SubtitlePreviewOverridesClassicHandling(mode)) {
        input.verticalTranslationActive=false;
        input.verticalTranslationPixels=0;
        input.verticalTranslationConfirmationPending=false;
        input.verticalTranslationBaseRetentionActive=false;
        input.verticalTranslationEngageBaseRetentionActive=false;
    }
    return input;
}

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

// Record the actual optional fill after mode-specific picture placement.
// This is a presentation witness only; submission still validates the selected
// source identity, native admission, and exact displayed horizontal fill.
inline bool CanRecordSubtitleOptionalAspectFill(const AlphaSourceCrop::Input& crop,
    const AlphaSourceCrop::Decision& presentation,
    const AlphaSourceCrop::AspectLimitFillDecision& fill, SubtitlePreviewMode mode,
    bool captionPresentationHeld, bool fixedCropAspectConfigured, bool nlsRequested,
    bool protectedCaptionFit, bool recoveryActive, ActivePictureAuthorityOrigin origin)
{
    const auto& actual= presentation.sourceBounds;
    const auto& native= crop.geometry;
    const bool exactNativeBounds=actual.left==native.left && actual.top==native.top &&
        actual.right==native.right && actual.bottom==native.bottom &&
        actual.rasterWidth==native.rasterWidth && actual.rasterHeight==native.rasterHeight;
    // Advanced styles have already undone the shared Classic translation.
    // The exact-native check below forbids certifying any remaining shift or
    // expanded viewport; mode alone is never evidence of a native presentation.
    // The requested advanced policy owns the unshifted native picture even
    // while its effective shader mode is None. A caption lease still protects
    // glyph/erasure continuity, but cannot veto recording that actual fill.
    // Exact native bounds and every normal fill/owner gate remain mandatory.
    const bool advancedNativePicture=exactNativeBounds &&
        (crop.subtitlePresentationMode==AlphaSourceCrop::PreparedNativeMode::BLACK ||
         crop.subtitlePresentationMode==AlphaSourceCrop::PreparedNativeMode::RECONSTRUCTION);
    return !presentation.rawSubtitleFallback &&
        (!captionPresentationHeld || advancedNativePicture || fill.retainedSubmittedFill) && fill.applied && presentation.applyCrop &&
        !fixedCropAspectConfigured && !nlsRequested && !protectedCaptionFit &&
        !presentation.outwardExpanded &&
        (!presentation.verticallyTranslated ||
            (SubtitlePreviewOverridesClassicHandling(mode) &&
             presentation.owner==AlphaSourceCrop::DecisionOwner::VERTICAL_TRANSLATION)) &&
        !crop.movingPictureTransition && (!recoveryActive || fill.retainedSubmittedFill) &&
        origin==ActivePictureAuthorityOrigin::NATIVE && exactNativeBounds;
}

// Carry the final optional fill with the exact selected native source. This
// qualifies presentation only; retained-fill selection is still checked again
// against the live successful-submission witness before it can be displayed.
inline AlphaSourceCrop::CropPresentationAdmissionState QualifySubtitleConfiguredNativeFill(
    AlphaSourceCrop::CropPresentationAdmissionState admission,const AlphaSourceCrop::Input& crop,
    const AlphaSourceCrop::Decision& presentation,const AlphaSourceCrop::AspectLimitFillDecision& fill,
    bool captionPresentationHeld,bool unknownCaptionHeld)
{
    const bool advanced=crop.subtitlePresentationMode==AlphaSourceCrop::PreparedNativeMode::BLACK ||
        crop.subtitlePresentationMode==AlphaSourceCrop::PreparedNativeMode::RECONSTRUCTION;
    const auto same=[](const ActivePictureBounds& a,const ActivePictureBounds& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;
    };
    // Reusing an already submitted optional fill still selects CURRENT native
    // pixels. Advanced picture policy has just issued a fresh exact-native
    // certificate, so qualify the current retained fill as well. No old source
    // identity, transformed pose or unrecorded rectangle can use this bridge.
    const bool retainedAdvancedNative=advanced && fill.applied && admission.optionalFillRetained &&
        admission.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE &&
        admission.sourceGeneration==crop.frameSourceGeneration &&
        admission.presentationEpoch==crop.framePresentationEpoch &&
        admission.optionalFillSourceSequence==crop.frameSourceSequence &&
        same(admission.optionalFillSourceBounds,fill.sourceBounds) &&
        presentation.owner==AlphaSourceCrop::DecisionOwner::TRUSTED_CROP &&
        !presentation.verticallyTranslated && !presentation.outwardExpanded &&
        same(presentation.sourceBounds,crop.geometry) && same(admission.trustedCrop,crop.geometry) &&
        admission.nativePresentation.available &&
        admission.nativePresentation.origin==AlphaSourceCrop::NativePresentationOrigin::EXACT_NATIVE &&
        admission.nativePresentation.sourceGeneration==crop.frameSourceGeneration &&
        admission.nativePresentation.sourceSequence==crop.frameSourceSequence &&
        admission.nativePresentation.presentationEpoch==crop.framePresentationEpoch &&
        same(admission.nativePresentation.nativeBase,crop.geometry) &&
        same(admission.nativePresentation.approvedBounds,crop.geometry);
    if(!presentation.rawSubtitleFallback && admission.available && admission.acceptedNativeCrop.available &&
        admission.optionalFillApplied && (!fill.retainedSubmittedFill || retainedAdvancedNative) &&
        admission.nativePresentation.origin!=AlphaSourceCrop::NativePresentationOrigin::CONFIRMED_TRANSLATION &&
        admission.nativePresentation.origin!=AlphaSourceCrop::NativePresentationOrigin::TRANSLATED_CONFIGURED_FILL &&
        !presentation.nativePresentation.captionPreservation.available && !captionPresentationHeld &&
        (!unknownCaptionHeld || advanced) && same(admission.acceptedNativeCrop.bounds,crop.geometry)) {
        const auto lease=admission.nativePresentation.captionTranslationMeasurement;
        const auto mode=admission.nativePresentation.preparedNativeMode;
        const auto source=admission.nativePresentation.preparedNativeSourceSequence;
        admission.nativePresentation={true,AlphaSourceCrop::NativePresentationOrigin::CONFIGURED_FILL,
            admission.acceptedNativeCrop.bounds,fill.applied?fill.sourceBounds:presentation.sourceBounds,
            crop.frameSourceGeneration,crop.frameSourceSequence,crop.framePresentationEpoch};
        admission.nativePresentation.captionTranslationMeasurement=lease;
        admission.nativePresentation.preparedNativeMode=mode;
        admission.nativePresentation.preparedNativeSourceSequence=source;
    }
    return admission;
}

// Missing analysis does not revoke an already displayed fill. Only a fresh,
// accepted caption footprint can prove that its horizontal trim hides text.
// Moved styles use their successfully composed destination, not the erased
// source card. Classic/unprepared output still displays the native footprint.
inline bool ConfirmedSubtitleConflictsWithHorizontalFill(bool confirmed, bool measured,
    SubtitlePreviewMode mode, bool composed, const ActivePictureBounds& nativeCaption,
    const SubtitleCutPasteGeometry& relocated, const ActivePictureBounds& filled)
{
    if (!confirmed || !measured) return false;
    if (filled.right <= filled.left || filled.bottom <= filled.top) return false;
    if (SubtitlePreviewOverridesClassicHandling(mode) && composed && relocated.valid) {
        if (!std::isfinite(relocated.glyphScale) || relocated.glyphScale <= 0.0f ||
            !std::isfinite(relocated.glyphTranslateX)) return true;
        bool hasGlyphs=false,conflict=false;
        const auto check=[&](const SubtitleBoxRect& glyphs) {
            if (!glyphs.Valid()) return;
            hasGlyphs=true;
            const float left=relocated.centeredGlyphMapping
                ? relocated.glyphScale*glyphs.left+relocated.glyphTranslateX
                : float(glyphs.left+relocated.destination.left-relocated.source.left);
            const float right=relocated.centeredGlyphMapping
                ? relocated.glyphScale*glyphs.right+relocated.glyphTranslateX
                : float(glyphs.right+relocated.destination.left-relocated.source.left);
            conflict=conflict || left < filled.left || right > filled.right;
        };
        for (const auto& line:relocated.glyphLines) check(line);
        if (!hasGlyphs) check(relocated.content);
        return !hasGlyphs || conflict;
    }
    if (nativeCaption.right <= nativeCaption.left || nativeCaption.bottom <= nativeCaption.top)
        return true;
    return nativeCaption.left < filled.left || nativeCaption.right > filled.right;
}

struct RetainedNativeFillSelection
{
    bool deauthorized=false;
    ActivePictureBounds sourceBounds;
    AlphaSourceCrop::CropPresentationAdmissionState selectedAdmission;
    bool nativeCropAvailable=false;
};

// Preparing a source is not submission. Check the selected fill against the
// live successful-submission certificate before using its optional side trim.
// The dry-run commit below modifies only a local copy; actual state changes
// still happen exclusively after a successful renderer submission.
inline RetainedNativeFillSelection RevalidateRetainedNativeFillSelection(
    const AlphaSourceCrop::SubmittedNativeScopeAnchorUpdateInput& selected)
{
    RetainedNativeFillSelection result;
    result.sourceBounds=selected.submittedBounds;
    result.selectedAdmission=selected.selectedAdmission;
    result.nativeCropAvailable=selected.nativeCropAvailable;
    if (!selected.selectedAdmission.optionalFillRetained) return result;
    auto proposal=selected;
    proposal.frameSubmitted=true;
    const auto checked=AlphaSourceCrop::CommitSubmittedNativeScopeAnchor(proposal);
    const auto& actual=selected.submittedBounds;
    const auto& witnessed=checked.submittedOptionalFill;
    const bool current=checked.submittedOptionalFillAvailable &&
        checked.sourceGeneration==selected.sourceGeneration &&
        checked.presentationEpoch==selected.presentationEpoch &&
        checked.submittedOptionalFillSourceSequence==selected.sourceSequence &&
        checked.lastSubmittedSourceSequence==selected.previous.lastSubmittedSourceSequence &&
        witnessed.left==actual.left && witnessed.top==actual.top &&
        witnessed.right==actual.right && witnessed.bottom==actual.bottom;
    if (current) return result;
    result.deauthorized=true;
    result.sourceBounds=selected.nativeCrop;
    result.selectedAdmission.optionalFillApplied=false;
    result.selectedAdmission.optionalFillRetained=false;
    result.selectedAdmission.optionalFillSourceBounds={};
    result.selectedAdmission.optionalFillSourceSequence=0;
    result.selectedAdmission.optionalFillScreenAspect=0.0;
    // Restoring an old prepared native rectangle is not fresh native authority.
    result.nativeCropAvailable=false;
    return result;
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
// moving-picture hold. An independently proved captioned ramp also supersedes
// its legacy vertical FIT; horizontal FIT and every other owner remain intact.
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
    if (routed.movingPictureHold.subtitleAssistedBars && routed.movingPictureHold.verticalFitOnly)
    {
        routed.verticalFitConfirmationPending = false;
        routed.movingPictureHold.competingPresentation = false;
    }
    if (!AlphaSourceCrop::HasCurrentMovingPictureHold(routed))
        return false;

    input = routed;
    return true;
}
// A completed scan may have omitted one side after losing its physical edge.
// Such a negative result is unknown, not evidence that all subtitles vanished.
inline bool SubtitleAbsenceIsUnknown(const SubtitleBoxObservation& current)
{
    return !current.analyzed || current.pendingRefresh || current.text.workLimit ||
        (!current.text.detected &&
            (!current.text.topSearchComplete || !current.text.bottomSearchComplete));
}
// Only a completed scan that covered both edge regions and found no bright ink
// proves an empty source cue.
// Rejected text candidates, refresh gaps, and held cues remain unknown.
inline bool ConfirmedSubtitleAbsentForPresentation(
    const SubtitleBoxObservation& current, const SubtitleBoxResult& accepted,
    const ActivePictureFrameIdentity& selectedIdentity)
{
    return SameAnalysisFrameIdentity(current.identity,selectedIdentity) &&
        !current.inspectionCandidateUsed && current.barAuthority && !SubtitleAbsenceIsUnknown(current) &&
        !current.text.detected &&
        current.text.diagnosticReason &&
        std::strcmp(current.text.diagnosticReason,"no-bright-ink")==0 &&
        !accepted.detected && !accepted.held;
}
// This is the renderer's current-caption gate, shared with pipeline regressions.
// Inspection nominees may recover glyph display and separately proved cleanup,
// but cannot originate a CURRENT_CAPTION presentation/FIT owner.
inline ActivePictureBounds MeasureCurrentCaptionForPresentation(
    const SubtitleBoxObservation& current,const SubtitleBoxResult& accepted,
    const ActivePictureFrameIdentity& identity,bool barAuthority,int width,int height)
{
    const auto& text=current.text;
    if(!barAuthority || current.inspectionCandidateUsed || current.glyphLocalTop || current.glyphLocalBottom ||
        !SameAnalysisFrameIdentity(current.identity,identity) || !current.analyzed || current.pendingRefresh ||
        !text.detected || text.workLimit || !text.nearBarEligibilityMeasured || !text.nearBarEligible ||
        !accepted.detected || accepted.held || accepted.confirmationPending || accepted.observations<1 ||
        !accepted.bounds.Valid() || !text.bounds.Valid())return {};
    SubtitleBoxRect caption=accepted.bounds;
    caption.left=(std::min)(caption.left,text.bounds.left);
    caption.top=(std::min)(caption.top,text.bounds.top);
    caption.right=(std::max)(caption.right,text.bounds.right);
    caption.bottom=(std::max)(caption.bottom,text.bounds.bottom);
    if(text.provenSourcePanel.Valid()) {
        caption.left=(std::min)(caption.left,text.provenSourcePanel.left);
        caption.top=(std::min)(caption.top,text.provenSourcePanel.top);
        caption.right=(std::max)(caption.right,text.provenSourcePanel.right);
        caption.bottom=(std::max)(caption.bottom,text.provenSourcePanel.bottom);
    }
    return {caption.left,caption.top,caption.right,caption.bottom,width,height,
        static_cast<double>(caption.right-caption.left)/(std::max)(1,caption.bottom-caption.top),
        ActivePictureBounds::BarAxes::NONE};
}
// Frame-local presentation evidence only. Raw detector evidence is never modified.
enum class QualifiedSubtitlePresentationStatus { NotApplicable, Unknown, Qualified, Conflict };
struct QualifiedSubtitlePresentationInput
{
    SubtitlePreviewMode mode = SubtitlePreviewMode::None;
    ActivePictureFrameIdentity currentIdentity;
    const SubtitleBoxObservation* observation = nullptr;
    const SubtitleBoxResult* accepted = nullptr;
    const SubtitleCutPasteGeometry* composition = nullptr;
    const SubtitleCueContinuation* continuation = nullptr;
    const SubtitleCurrentPreparedCueCertificate* currentPreparedCue = nullptr;
    // Per-frame receipt minted only after current pending pixels were verified,
    // the exact prepared geometry was protected, and the compositor succeeded.
    // It permits the prepared-erasure proof to survive a missing bar plane; it
    // does not carry semantic, native-picture, or lease authority.
    const class PendingPreparedSubtitleErasureReceipt* pendingPreparedErasure = nullptr;
    ActivePictureBounds physicalBase;
    // Current renderer-owned output authority. A queued worker's geometry hint
    // may predate this source packet even when its measured pixels are current.
    SubtitlePictureAuthority currentDisplayPicture;
    uint64_t policyGeneration=0,continuityGeneration=0;
    bool sourcePrepared = false, pendingCurrentPixelsVerified=false;
    size_t maxOwnershipSamples=262144;
};

// Exact-current handoff from the pending-footprint verifier to presentation
// ownership. The renderer records this only after the one-use cleanup receipt
// is applied and the resulting geometry is actually composited. Identity,
// cue, source ink, display snapshot, policy and operation are all pinned so a
// receipt from a prior source or a different prepared operation cannot pass.
class PendingPreparedSubtitleErasureReceipt
{
public:
    bool Record(const SubtitleBoxObservation& current,const SubtitleBoxResult& accepted,
        const ActivePictureBounds& base,const SubtitlePictureAuthority& display,
        const SubtitleCutPasteGeometry& geometry,SubtitlePreviewMode mode,
        uint64_t policy,uint64_t continuity,bool pendingOperationApplied)
    {
        Clear();
        if(!pendingOperationApplied || !SubtitlePreviewOverridesClassicHandling(mode) ||
            !current.analyzed || current.discontinuity ||
            !current.pendingRefresh || current.analysisRefresh || current.cadenceSkipped ||
            (current.analysisStride!=1 && current.analysisStride!=2) ||
            current.barAuthority || current.independentBarAnalysis || !current.ink ||
            !current.ink->paletteValid || current.ink->rawEvidenceComplete ||
            !accepted.detected || !accepted.held || accepted.confirmationPending || accepted.workLimit ||
            accepted.revised || !accepted.cue || current.text.cue!=accepted.cue ||
            current.policyGeneration!=policy || current.continuityGeneration!=continuity ||
            current.width<=0 || current.height<=0 || base.rasterWidth!=current.width ||
            base.rasterHeight!=current.height || base.left<0 || base.top<0 || base.right>current.width ||
            base.bottom>current.height || base.left>=base.right || base.top>=base.bottom || !geometry.valid ||
            !display.AvailableFor(current.identity,current.width,current.height) ||
            !SameAnalysisFrameIdentity(display.identity,current.identity) ||
            !SamePendingPreparedBounds(display.bounds,base) ||
            !current.sharedPicture.AvailableFor(current.identity,current.width,current.height) ||
            !SamePendingPreparedBounds(current.sharedPicture.bounds,base) ||
            !current.sharedPicture.Matches(display))return false;
        m_valid=true;m_identity=current.identity;m_policy=policy;m_continuity=continuity;
        m_width=current.width;m_height=current.height;m_base=base;m_display=display;
        m_cue=accepted;m_geometry=geometry;m_mode=mode;m_ink=current.ink.get();
        return true;
    }

    void Clear()
    {
        m_valid=false;m_identity={};m_policy=m_continuity=0;m_width=m_height=0;
        m_base={};m_display={};m_cue={};m_geometry={};m_mode=SubtitlePreviewMode::None;m_ink=nullptr;
    }

    bool Matches(const SubtitleBoxObservation& current,const SubtitleBoxResult& accepted,
        const ActivePictureBounds& base,const SubtitlePictureAuthority& display,
        const SubtitleCutPasteGeometry& geometry,SubtitlePreviewMode mode,
        uint64_t policy,uint64_t continuity) const
    {
        return MatchFailure(current,accepted,base,display,geometry,mode,policy,continuity)==nullptr;
    }

    // Computed-only diagnostics for the pending ownership join. These are
    // useful when a current-source pixel receipt was verified but its exact
    // prepared geometry could not be carried through the compositor. They do
    // not alter the receipt or grant any authority.
    const char* MatchFailure(const SubtitleBoxObservation& current,const SubtitleBoxResult& accepted,
        const ActivePictureBounds& base,const SubtitlePictureAuthority& display,
        const SubtitleCutPasteGeometry& geometry,SubtitlePreviewMode mode,
        uint64_t policy,uint64_t continuity) const
    {
        if(!m_valid)return "receipt-not-recorded";
        if(mode!=m_mode)return "receipt-mode-mismatch";
        if(policy!=m_policy || current.policyGeneration!=m_policy)return "receipt-policy-mismatch";
        if(continuity!=m_continuity || current.continuityGeneration!=m_continuity)return "receipt-continuity-mismatch";
        if(!SameAnalysisFrameIdentity(current.identity,m_identity))return "receipt-source-identity-mismatch";
        if(current.width!=m_width || current.height!=m_height)return "receipt-raster-mismatch";
        if(!SamePendingPreparedBounds(base,m_base))return "receipt-native-base-mismatch";
        if(!SamePendingPreparedBounds(display.bounds,m_display.bounds) ||
            !SameAnalysisFrameIdentity(display.identity,m_identity) || !display.Matches(m_display))
            return "receipt-display-mismatch";
        if(!current.sharedPicture.AvailableFor(current.identity,current.width,current.height) ||
            !SamePendingPreparedBounds(current.sharedPicture.bounds,m_base) || !current.sharedPicture.Matches(display))
            return "receipt-shared-picture-mismatch";
        if(!current.analyzed || current.discontinuity || !current.pendingRefresh || current.analysisRefresh ||
            current.cadenceSkipped || (current.analysisStride!=1 && current.analysisStride!=2) ||
            current.barAuthority || current.independentBarAnalysis)
            return "receipt-observation-state-mismatch";
        if(!current.ink || current.ink.get()!=m_ink || !current.ink->paletteValid || current.ink->rawEvidenceComplete)
            return "receipt-current-ink-mismatch";
        if(current.text.cue!=m_cue.cue || !accepted.detected || !accepted.held || accepted.confirmationPending ||
            accepted.workLimit || accepted.revised || !SamePendingPreparedCue(accepted,m_cue))
            return "receipt-accepted-cue-mismatch";
        if(!SamePendingPreparedGeometry(geometry,m_geometry))return "receipt-composition-mismatch";
        return nullptr;
    }

private:
    static bool SamePendingPreparedRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b)
    { return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom; }
    static bool SamePendingPreparedBounds(const ActivePictureBounds& a,const ActivePictureBounds& b)
    {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight &&
            a.aspectRatio==b.aspectRatio && a.trustedBarAxes==b.trustedBarAxes;
    }
    static bool SamePendingPreparedCue(const SubtitleBoxResult& a,const SubtitleBoxResult& b)
    {
        if(a.cue!=b.cue || a.observations!=b.observations || a.lineCount!=b.lineCount ||
            a.detected!=b.detected || a.held!=b.held || a.confirmationPending!=b.confirmationPending ||
            a.revised!=b.revised || a.workLimit!=b.workLimit ||
            a.capturePanelMeasured!=b.capturePanelMeasured ||
            a.nearBarEligibilityMeasured!=b.nearBarEligibilityMeasured ||
            a.nearBarEligible!=b.nearBarEligible || a.topSearchComplete!=b.topSearchComplete ||
            a.bottomSearchComplete!=b.bottomSearchComplete ||
            !SamePendingPreparedRect(a.bounds,b.bounds) || !SamePendingPreparedRect(a.anchor,b.anchor) ||
            !SamePendingPreparedRect(a.sourcePanel,b.sourcePanel) ||
            !SamePendingPreparedRect(a.provenSourcePanel,b.provenSourcePanel) ||
            !SamePendingPreparedRect(a.capturePanel,b.capturePanel) ||
            a.signature!=b.signature || a.anchorSignature!=b.anchorSignature)return false;
        for(size_t i=0;i<a.lineBounds.size();++i)
            if(!SamePendingPreparedRect(a.lineBounds[i],b.lineBounds[i]) ||
                a.lineSignatures[i]!=b.lineSignatures[i] ||
                !SamePendingPreparedRect(a.linePanels[i],b.linePanels[i]) ||
                !SamePendingPreparedRect(a.provenLinePanels[i],b.provenLinePanels[i]) ||
                !SamePendingPreparedRect(a.lineCapturePanels[i],b.lineCapturePanels[i]) ||
                a.panelTopEdges[i].y!=b.panelTopEdges[i].y ||
                a.panelTopEdges[i].left!=b.panelTopEdges[i].left ||
                a.panelTopEdges[i].right!=b.panelTopEdges[i].right ||
                a.panelTopEdges[i].supportBasisPoints!=b.panelTopEdges[i].supportBasisPoints)return false;
        return true;
    }
    static bool SamePendingPreparedGeometry(const SubtitleCutPasteGeometry& a,const SubtitleCutPasteGeometry& b)
    {
        if(!SamePendingPreparedRect(a.source,b.source) || !SamePendingPreparedRect(a.destination,b.destination) ||
            !SamePendingPreparedRect(a.content,b.content) || !SamePendingPreparedRect(a.generatedCleanup,b.generatedCleanup) ||
            !SamePendingPreparedRect(a.pictureCapture,b.pictureCapture) ||
            !SamePendingPreparedRect(a.pendingProtectedCleanup,b.pendingProtectedCleanup) ||
            !SamePendingPreparedRect(a.generatedCleanupExclusion,b.generatedCleanupExclusion) ||
            !SamePendingPreparedRect(a.cleanupLimit,b.cleanupLimit) ||
            a.generatedCleanupRowCount!=b.generatedCleanupRowCount ||
            a.generatedCleanupRowCount>a.generatedCleanupRows.size() ||
            a.glyphScale!=b.glyphScale || a.glyphTranslateX!=b.glyphTranslateX ||
            a.glyphTranslateY!=b.glyphTranslateY || a.centeredGlyphMapping!=b.centeredGlyphMapping ||
            a.roundedCorners!=b.roundedCorners || a.extendToBar!=b.extendToBar ||
            a.pictureIntrusion!=b.pictureIntrusion || a.backgroundGuarded!=b.backgroundGuarded ||
            a.pictureTop!=b.pictureTop || a.pictureBottom!=b.pictureBottom ||
            a.displayPictureLeft!=b.displayPictureLeft || a.displayPictureTop!=b.displayPictureTop ||
            a.displayPictureRight!=b.displayPictureRight || a.displayPictureBottom!=b.displayPictureBottom ||
            a.valid!=b.valid || a.fromTopBar!=b.fromTopBar ||
            !SamePendingPreparedRect(a.placementLimits,b.placementLimits) ||
            !SamePendingPreparedRect(a.layoutContent,b.layoutContent) ||
            !SamePendingPreparedRect(a.layoutCoverage,b.layoutCoverage))return false;
        for(size_t i=0;i<a.glyphLines.size();++i)
            if(!SamePendingPreparedRect(a.glyphLines[i],b.glyphLines[i]))return false;
        for(unsigned i=0;i<a.generatedCleanupRowCount;++i)
            if(!SamePendingPreparedRect(a.generatedCleanupRows[i],b.generatedCleanupRows[i]))return false;
        return true;
    }

    bool m_valid=false;
    ActivePictureFrameIdentity m_identity;
    uint64_t m_policy=0,m_continuity=0;
    int m_width=0,m_height=0;
    ActivePictureBounds m_base;
    SubtitlePictureAuthority m_display;
    SubtitleBoxResult m_cue;
    SubtitleCutPasteGeometry m_geometry;
    SubtitlePreviewMode m_mode=SubtitlePreviewMode::None;
    const SubtitleInkSnapshot* m_ink=nullptr;
};
// Diagnostic-only facts, filled at the existing rejection branch. Zero masks
// are unevaluated unless kind identifies that branch; they grant no authority.
enum class SubtitlePresentationRejectionKind : uint8_t { None, GlyphCapture, PhysicalBase, ObservationEdge };
struct SubtitlePresentationRejectionFacts
{
    SubtitlePresentationRejectionKind kind=SubtitlePresentationRejectionKind::None;
    int rawLine=-1;
    SubtitleBoxRect rawCapture;
    std::array<uint8_t,3> glyphFailures{};
    uint8_t contentFailure=0;
    uint32_t physicalFailures=0,guardFacts=0;
};
// Bits: invalid outer=1, invalid inner=2, left=4, top=8, right=16, bottom=32.
inline uint8_t SubtitleCaptureContainmentFailures(const SubtitleBoxRect& outer,const SubtitleBoxRect& inner)
{
    return (!outer.Valid()?1u:0u)|(!inner.Valid()?2u:0u)|
        (outer.left>inner.left?4u:0u)|(outer.top>inner.top?8u:0u)|
        (outer.right<inner.right?16u:0u)|(outer.bottom<inner.bottom?32u:0u);
}
struct QualifiedSubtitlePresentationEvidence
{
    ActivePicturePresentationRetentionEvidence retention;
    ActivePictureFrameIdentity identity;
    ActivePictureBounds physicalBase;
    ActivePictureBounds erasedSourceBounds;
    QualifiedSubtitlePresentationStatus status=QualifiedSubtitlePresentationStatus::Unknown;
    bool qualified = false;
    // Successful current caption composition is independent of clean remaining
    // source bands. It does not by itself permit the native viewport.
    bool currentCompositionVerified=false;
    SubtitlePreviewMode currentCompositionMode=SubtitlePreviewMode::None;
    bool renewsHold=false;
    // Current compositor output only; never native ownership or a renewable lease.
    bool preparedErasureOnly=false;
    SubtitlePresentationRejectionFacts rejection;
    size_t ownershipSamples = 0;
    const char* reason = "not-qualified";
};
inline bool SameSubtitlePresentationBounds(const ActivePictureBounds& a,const ActivePictureBounds& b)
{
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
        a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;
}
// Only affirmative, independently measured picture expansion can veto a cue
// continuation. A missing detector edge is not evidence of expansion.
inline bool CurrentPictureRejectsSubtitleContinuation(const ActivePictureEvidence& picture,
    const ActivePictureBounds& base)
{
    if(!picture.available || picture.authorityOrigin!=ActivePictureAuthorityOrigin::NATIVE)
        return false;
    if(picture.classification==ActivePictureClassification::FULL_RASTER_TRUSTED)
        return true;
    const auto& observed=picture.trustedBounds;
    return picture.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
        observed.rasterWidth==base.rasterWidth && observed.rasterHeight==base.rasterHeight &&
        observed.left<observed.right && observed.top<observed.bottom &&
        (observed.top<base.top-SubtitlePhysicalEdgeReconciliationTolerance ||
         observed.bottom>base.bottom+SubtitlePhysicalEdgeReconciliationTolerance);
}
inline QualifiedSubtitlePresentationEvidence EvaluateSubtitlePresentationEvidence(
    const AnalysisLumaSource& source, const ActivePicturePresentationRetentionEvidence& raw,
    const QualifiedSubtitlePresentationInput& input, bool preparedErasureOnly)
{
    QualifiedSubtitlePresentationEvidence result;
    result.retention=raw;result.identity=input.currentIdentity;result.physicalBase=input.physicalBase;
    result.rejection.guardFacts=preparedErasureOnly?4u:0u;
    const auto reject=[&](const char* reason,QualifiedSubtitlePresentationStatus status=QualifiedSubtitlePresentationStatus::Unknown) {
        result.reason=reason;result.status=status;return result;
    };
    if(!SubtitlePreviewOverridesClassicHandling(input.mode))
        return reject("mode-not-relocated",QualifiedSubtitlePresentationStatus::NotApplicable);
    if(!input.sourcePrepared || !input.observation || !input.accepted || !input.composition)
        return reject("source-not-prepared");
    const auto& observation=*input.observation;
    // Retained coordinates can nominate a fresh subtitle scan after the
    // presentation lease expires. That scan cannot certify crop or retention.
    if(observation.inspectionCandidateUsed)
        return reject("inspection-candidate-is-not-picture-proof");
    const bool glyphLocal=observation.glyphLocalTop || observation.glyphLocalBottom;
    if(glyphLocal && !preparedErasureOnly)
        return reject("glyph-local-evidence-is-not-ar-proof");
    const auto& accepted=*input.accepted;
    if(accepted.confirmationPending && !preparedErasureOnly)
        return reject("provisional-caption-is-not-semantic-proof");
    const auto& move=*input.composition;
    const auto& base=input.physicalBase;
    const auto* continuation=input.continuation;
    if(continuation && !continuation->Matches(input.currentIdentity,input.policyGeneration,
        input.continuityGeneration,accepted,base))
        return reject("continuation-context-mismatch");
    // Fixed-plane glyph continuity may bridge an unavailable unrelated edge,
    // but cannot certify physical bars, source erasure or a new crop owner.
    if(continuation && continuation->localGlyphsOnly)
        return reject("local-glyph-continuation-is-not-picture-proof");
    if((continuation || preparedErasureOnly) &&
        CurrentPictureRejectsSubtitleContinuation(raw.activePicture,base))
        return reject("independent-picture-expansion",QualifiedSubtitlePresentationStatus::Conflict);
    // A verified existing cue supplies only current local glyph ownership. The
    // worker observation and raw picture evidence remain the measurement of this
    // frame; a continuation cannot acquire or renew active-picture authority.
    const auto& text=continuation ? continuation->accepted : observation.text;
    const int pictureTop=continuation ? continuation->pictureTop : observation.pictureTop;
    const int pictureBottom=continuation ? continuation->pictureBottom : observation.pictureBottom;
    if(raw.activePicture.available && raw.activePicture.classification==ActivePictureClassification::FULL_RASTER_TRUSTED)
        return reject("independent-full-raster-picture",QualifiedSubtitlePresentationStatus::Conflict);
    // Cadence verifies this source's glyphs and actual cleanup rows, then
    // deliberately strips independent bar/AR authority. An actually prepared
    // packet may certify only its current erasure output through that proof;
    // it cannot acquire semantic glyph ownership or renew a submitted lease.
    const bool currentVerifiedNonlocalCadence=[&] {
        if(!preparedErasureOnly || !continuation || continuation->localGlyphsOnly || glyphLocal ||
            !observation.cadenceSkipped || observation.analysisStride!=2 || !observation.pendingRefresh ||
            observation.analysisRefresh || observation.independentBarAnalysis || observation.barAuthority ||
            observation.text.workLimit || accepted.workLimit || !continuation->samples ||
            !std::isfinite(continuation->elapsedMs) || continuation->elapsedMs<=0 || continuation->elapsedMs>250 ||
            !observation.ink || observation.ink!=continuation->ink ||
            !continuation->ink->paletteValid || continuation->ink->rawEvidenceComplete)
            return false;
        auto previous=continuation->templateIdentity;
        const auto& current=input.currentIdentity;
        if(!SameAnalysisFrameIdentity(continuation->previousIdentity,previous) ||
            !previous.acceptedSequence || current.acceptedSequence<=previous.acceptedSequence ||
            current.acceptedSequence-previous.acceptedSequence!=1 ||
            current.sourceFrameNumber<=previous.sourceFrameNumber || current.captureTimestamp<=previous.captureTimestamp)
            return false;
        previous.acceptedSequence=current.acceptedSequence;
        previous.sourceFrameNumber=current.sourceFrameNumber;previous.captureTimestamp=current.captureTimestamp;
        const auto& display=input.currentDisplayPicture;
        return SameAnalysisFrameIdentity(previous,current) &&
            display.AvailableFor(current,source.width,source.height) &&
            SameAnalysisFrameIdentity(display.identity,current) && SameSubtitlePresentationBounds(display.bounds,base) &&
            observation.sharedPicture.AvailableFor(current,source.width,source.height) &&
            SameAnalysisFrameIdentity(observation.sharedPicture.identity,current) &&
            observation.sharedPicture.Matches(display);
    }();
    const bool currentHeldPreparedErasure=preparedErasureOnly && input.currentPreparedCue &&
        input.currentPreparedCue->Matches(observation,accepted,base);
    // A pending footprint can keep only a successfully prepared, exact-current
    // erasure. Its receipt is minted after the one-use cleanup projection and
    // actual compositor pass; it cannot stand in for bar/native evidence or a
    // fresh semantic cue acquisition.
    const bool currentVerifiedPreparedFootprint=preparedErasureOnly && input.pendingCurrentPixelsVerified &&
        input.pendingPreparedErasure && observation.pendingRefresh && !observation.analysisRefresh &&
        !observation.cadenceSkipped && (observation.analysisStride==1 || observation.analysisStride==2) &&
        !observation.barAuthority && !observation.independentBarAnalysis && observation.ink &&
        observation.ink->paletteValid && !observation.ink->rawEvidenceComplete &&
        accepted.held && !accepted.confirmationPending && !accepted.workLimit &&
        input.pendingPreparedErasure->Matches(observation,accepted,base,input.currentDisplayPicture,
            move,input.mode,input.policyGeneration,input.continuityGeneration);
    const char* pendingFootprintFailure=nullptr;
    if(preparedErasureOnly && (input.pendingCurrentPixelsVerified || input.pendingPreparedErasure)) {
        if(!input.pendingCurrentPixelsVerified)pendingFootprintFailure="prepared-footprint-pixels-not-verified";
        else if(!input.pendingPreparedErasure)pendingFootprintFailure="prepared-footprint-receipt-missing";
        else if(!observation.pendingRefresh)pendingFootprintFailure="prepared-footprint-not-pending";
        else if(observation.analysisRefresh)pendingFootprintFailure="prepared-footprint-analysis-refresh";
        else if(observation.cadenceSkipped || (observation.analysisStride!=1 && observation.analysisStride!=2))
            pendingFootprintFailure="prepared-footprint-analysis-stride-mismatch";
        else if(observation.barAuthority || observation.independentBarAnalysis)
            pendingFootprintFailure="prepared-footprint-unexpected-bar-authority";
        else if(!observation.ink)pendingFootprintFailure="prepared-footprint-current-ink-missing";
        else if(!observation.ink->paletteValid)pendingFootprintFailure="prepared-footprint-current-palette-invalid";
        else if(observation.ink->rawEvidenceComplete)pendingFootprintFailure="prepared-footprint-raw-evidence-complete";
        else if(!accepted.held)pendingFootprintFailure="prepared-footprint-cue-not-held";
        else if(accepted.confirmationPending)pendingFootprintFailure="prepared-footprint-cue-confirmation-pending";
        else if(accepted.workLimit)pendingFootprintFailure="prepared-footprint-cue-work-limit";
        else pendingFootprintFailure=input.pendingPreparedErasure->MatchFailure(observation,accepted,base,
            input.currentDisplayPicture,move,input.mode,input.policyGeneration,input.continuityGeneration);
    }
    if(!SameAnalysisFrameIdentity(observation.identity,input.currentIdentity) ||
        observation.policyGeneration!=input.policyGeneration || observation.continuityGeneration!=input.continuityGeneration ||
        observation.discontinuity || !observation.analyzed ||
        (!observation.barAuthority && !currentVerifiedNonlocalCadence && !currentVerifiedPreparedFootprint) ||
        (observation.pendingRefresh && !input.pendingCurrentPixelsVerified &&
            !currentVerifiedNonlocalCadence && !currentVerifiedPreparedFootprint) || text.workLimit ||
        !text.detected || !text.nearBarEligibilityMeasured || !text.nearBarEligible ||
        !accepted.detected || (accepted.held && !continuation && !currentHeldPreparedErasure &&
            !currentVerifiedPreparedFootprint) || !accepted.bounds.Valid() || !text.bounds.Valid() ||
        text.lineCount<1 || text.lineCount>3 || !move.valid)
        return reject(pendingFootprintFailure ? pendingFootprintFailure : "current-caption-proof-unavailable");
    const auto contains=[](const SubtitleBoxRect& outer,const SubtitleBoxRect& inner) {
        return outer.Valid() && inner.Valid() && outer.left<=inner.left && outer.top<=inner.top &&
            outer.right>=inner.right && outer.bottom>=inner.bottom;
    };
    // Overall cue bounds include tracker margins and interline space. Capture
    // authority comes from the current measured lines and the geometry actually
    // composed for this packet, not containment of that broader rectangle.
    const auto currentCaptureLines=SubtitleGlyphCaptureLines(text,source.width,source.height);
    if(!contains(move.source,move.content))return reject("incomplete-caption-source");
    for(int line=0;line<text.lineCount;++line) {
        const auto& measured=currentCaptureLines[line];
        bool captured=false;
        for(const auto& prepared:move.glyphLines)
            captured |= contains(prepared,measured);
        if(!captured || !contains(move.content,measured)) {
            result.rejection.kind=SubtitlePresentationRejectionKind::GlyphCapture;
            result.rejection.rawLine=line;result.rejection.rawCapture=measured;
            for(size_t i=0;i<move.glyphLines.size();++i)
                result.rejection.glyphFailures[i]=SubtitleCaptureContainmentFailures(move.glyphLines[i],measured);
            result.rejection.contentFailure=SubtitleCaptureContainmentFailures(move.content,measured);
            return reject("incomplete-current-glyph-capture");
        }
    }
    // A strict AR profile can retain a small OUTWARD boundary guard while
    // the subtitle worker still measures the physical picture inside it. The
    // prepared compositor has cleared everything outside that sampled picture.
    // Accept that output fact only for this exact current display snapshot;
    // neither source sampling nor native glyph/AR authority is enlarged.
    const auto& display=input.currentDisplayPicture;
    // The native guard and subtitle sampler locate the same physical edge
    // independently. Include their existing reconciliation tolerance; keep the
    // exact current display witness and residual native pixel checks below.
    const int maxOutwardGuard=2*(std::max)(2,source.height/540)+
        SubtitlePhysicalEdgeReconciliationTolerance;
    const bool preparedOutwardGuard=preparedErasureOnly &&
        base.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM &&
        base.left==0 && base.right==source.width &&
        base.top>0 && base.bottom<source.height &&
        move.pictureTop>=base.top && move.pictureBottom<=base.bottom &&
        move.pictureTop<move.pictureBottom &&
        move.pictureTop-base.top<=maxOutwardGuard &&
        base.bottom-move.pictureBottom<=maxOutwardGuard &&
        display.AvailableFor(input.currentIdentity,source.width,source.height) &&
        SameAnalysisFrameIdentity(display.identity,input.currentIdentity) &&
        SameSubtitlePresentationBounds(display.bounds,base) &&
        observation.sharedPicture.Matches(display);
    const bool exactComposedPicture=move.pictureTop==base.top && move.pictureBottom==base.bottom;
    if(!source.IsValid() || source.generation!=input.currentIdentity.transportGeneration || observation.width!=source.width || observation.height!=source.height ||
        base.rasterWidth!=source.width || base.rasterHeight!=source.height ||
        base.left<0 || base.top<0 || base.right>source.width || base.bottom>source.height ||
        base.left>=base.right || base.top>=base.bottom ||
        pictureTop<0 || pictureBottom>source.height ||
        pictureTop>=pictureBottom ||
        (!exactComposedPicture && !preparedOutwardGuard) ||
        !raw.analysisValid || !raw.presentationValid || raw.presentationBlackThreshold<=0 ||
        raw.excludedTop.barPixels!=base.top || raw.excludedBottom.barPixels!=source.height-base.bottom ||
        raw.excludedLeft.barPixels!=base.left || raw.excludedRight.barPixels!=source.width-base.right)
    {
        auto& diagnostic=result.rejection;
        diagnostic.kind=SubtitlePresentationRejectionKind::PhysicalBase;
        // All cheap scalar clauses are evaluated here, only after rejection.
        // Bits0..13: source, generation, observation size, base raster, base
        // coordinates, raw picture, composed/guard picture, raw analysis,
        // raw presentation, black threshold, excluded top/bottom/left/right.
        diagnostic.physicalFailures=(!source.IsValid()?1u:0u)|
            (source.generation!=input.currentIdentity.transportGeneration?1u<<1:0u)|
            (observation.width!=source.width || observation.height!=source.height?1u<<2:0u)|
            (base.rasterWidth!=source.width || base.rasterHeight!=source.height?1u<<3:0u)|
            (base.left<0 || base.top<0 || base.right>source.width || base.bottom>source.height ||
                base.left>=base.right || base.top>=base.bottom?1u<<4:0u)|
            (pictureTop<0 || pictureBottom>source.height || pictureTop>=pictureBottom?1u<<5:0u)|
            (!exactComposedPicture && !preparedOutwardGuard?1u<<6:0u)|
            (!raw.analysisValid?1u<<7:0u)|(!raw.presentationValid?1u<<8:0u)|
            (raw.presentationBlackThreshold<=0?1u<<9:0u)|
            (raw.excludedTop.barPixels!=base.top?1u<<10:0u)|
            (raw.excludedBottom.barPixels!=source.height-base.bottom?1u<<11:0u)|
            (raw.excludedLeft.barPixels!=base.left?1u<<12:0u)|
            (raw.excludedRight.barPixels!=source.width-base.right?1u<<13:0u);
        diagnostic.guardFacts=(exactComposedPicture?1u:0u)|(preparedOutwardGuard?2u:0u)|
            (preparedErasureOnly?4u:0u)|
            (display.AvailableFor(input.currentIdentity,source.width,source.height)?8u:0u)|
            (SameAnalysisFrameIdentity(display.identity,input.currentIdentity)?16u:0u)|
            (SameSubtitlePresentationBounds(display.bounds,base)?32u:0u)|
            (observation.sharedPicture.Matches(display)?64u:0u);
        return reject("physical-base-mismatch");
    }
    // Keep the existing tight agreement between measurement and sampling.
    // A certified outward display guard retains rows; it must not reclassify
    // those rows as physical bars or change the subtitle's extraction boundary.
    if(std::abs(pictureTop-move.pictureTop)>SubtitlePhysicalEdgeReconciliationTolerance ||
        std::abs(pictureBottom-move.pictureBottom)>SubtitlePhysicalEdgeReconciliationTolerance)
    {
        result.rejection.kind=SubtitlePresentationRejectionKind::ObservationEdge;
        return reject("subtitle-observation-edge-mismatch");
    }
    const SubtitleBoxRect baseRect{base.left,base.top,base.right,base.bottom};
    if(!contains(baseRect,move.destination))return reject("destination-outside-picture");
    if(preparedErasureOnly) {
        // A temporal donor may overwrite generatedCleanup after the shader.
        // Keep that region and attached output strictly inside the physical
        // picture, so neither can overwrite the certified black source bars.
        if(move.generatedCleanup.Valid() && !contains(baseRect,move.generatedCleanup))
            return reject("prepared-cleanup-outside-picture");
        // Row masks are the actual erase spans for rounded backing lips. The
        // legacy common rectangle can be contained even when an individual
        // row crosses a cropped horizontal picture edge, so validate every
        // active row independently. A zero count keeps the legacy rectangle
        // path unchanged.
        if(move.generatedCleanupRowCount>move.generatedCleanupRows.size())
            return reject("prepared-cleanup-rows-invalid");
        for(unsigned rowIndex=0;rowIndex<move.generatedCleanupRowCount;++rowIndex) {
            const auto& row=move.generatedCleanupRows[rowIndex];
            if(!row.Valid() || row.top<baseRect.top || row.bottom>baseRect.bottom ||
                row.left<baseRect.left || row.right>baseRect.right)
                return reject("prepared-cleanup-row-outside-picture");
        }
        const bool displayPicture=move.displayPictureRight>move.displayPictureLeft &&
            move.displayPictureBottom>move.displayPictureTop;
        if(move.extendToBar && displayPicture &&
            (move.displayPictureTop<base.top || move.displayPictureBottom>base.bottom))
            return reject("prepared-panel-outside-picture");
    }
    const auto& box=move.content;
    if(!box.Valid() || box.left<base.left || box.right>base.right || box.top<0 || box.bottom>source.height)
        return reject("invalid-erasure-footprint");
    if(box.top>=base.top && box.bottom<=base.bottom)
        return reject("no-erased-bar-overlap",QualifiedSubtitlePresentationStatus::NotApplicable);
    if(glyphLocal && (observation.glyphLocalTop==observation.glyphLocalBottom ||
        observation.pendingRefresh || observation.analysisRefresh || text.held ||
        !contains(observation.glyphLocalVerifiedBounds,move.cleanupLimit) ||
        !contains(move.cleanupLimit,box)))
        return reject("prepared-local-erasure-unverified");
    // Continuation grids cover a bounded region, not the detector's full search
    // bands. Every capture cell used below must lie in that freshly sampled ROI.
    if(continuation && !contains(continuation->sampledBounds,box))
        return reject("continuation-ownership-outside-sampled-region");
    const auto* ink=continuation ? continuation->ink.get() : observation.ink.get();
    // A verified pending footprint proves its own exact current glyph/native
    // support. Its partial palette mask is shape metadata here, never a claim
    // that the detector's full raw search was complete.
    if(!ink || (!ink->rawEvidenceComplete && !continuation && !currentVerifiedPreparedFootprint) ||
        !ink->paletteValid || ink->step<1 || ink->step>8 ||
        ink->width<1 || ink->height<1 || ink->sourceRows.size()!=size_t(ink->height) ||
        ink->ownedInk.size()!=(size_t(ink->width)*ink->height+63)/64 ||
        !std::is_sorted(ink->sourceRows.begin(),ink->sourceRows.end()))
        return reject("current-owned-ink-unavailable");
    const int bw=box.right-box.left;
    const size_t outsideRows=size_t((std::max)(0,(std::min)(box.bottom,base.top)-box.top))+
        size_t((std::max)(0,box.bottom-(std::max)(box.top,base.bottom)));
    const int reconciledTop=(std::min)(pictureTop,base.top);
    const int reconciledBottom=(std::max)(pictureBottom,base.bottom);
    const auto reconciliationSamples=[&](int top,int bottom) {
        const int overlap=(std::max)(0,(std::min)(bottom,box.bottom)-(std::max)(top,box.top));
        return size_t(bottom-top)*size_t(source.width)-size_t(overlap)*size_t(bw);
    };
    const size_t extraSamples=reconciliationSamples(reconciledTop,base.top)+
        reconciliationSamples(base.bottom,reconciledBottom);
    // Prepared erasure does not inspect inside the cleared rectangle. Charge
    // only native rows actually read; its size cannot consume an unused budget.
    const size_t nativeSamples=extraSamples+(preparedErasureOnly?0:outsideRows*size_t(bw));
    if(nativeSamples>input.maxOwnershipSamples)
        return reject("ownership-budget");
    // Native validation is limited to the actual erased bar rectangle. A sample
    // grid cell is not ownership: every nonblack native component must connect
    // to an exact current owned-ink seed and stay inside the captured glyph line.
    const auto inspect=[&](int top,int bottom)->const char* {
        if(bottom<=top)return nullptr;
        const size_t area=size_t(bw)*size_t(bottom-top);
        std::vector<uint8_t> mask(area,0);
        for(int y=top;y<bottom;++y) {
            const auto row=std::lower_bound(ink->sourceRows.begin(),ink->sourceRows.end(),y);
            const int sy=row!=ink->sourceRows.end() && *row==y?int(row-ink->sourceRows.begin()):-1;
            for(int x=box.left;x<box.right;++x) {
                AnalysisLumaSample pixel;
                if(!source.Sample(x,y,pixel))return "ownership-sample-unavailable";
                ++result.ownershipSamples;
                const bool neutral=std::abs(int(pixel.chromaU)-512)<=32 && std::abs(int(pixel.chromaV)-512)<=32;
                if(neutral && pixel.luma<=raw.presentationBlackThreshold)continue;
                bool capturedLine=false;
                for(int line=0;line<text.lineCount && !capturedLine;++line) {
                    const auto& measured=currentCaptureLines[line];
                    if(x<measured.left || x>=measured.right || y<measured.top || y>=measured.bottom)continue;
                    for(const auto& capture:move.glyphLines)
                        if(capture.Valid() && x>=capture.left && x<capture.right && y>=capture.top && y<capture.bottom)
                            capturedLine=true;
                }
                if(!capturedLine)return "unowned-native-interline-content";
                if(std::abs(int(pixel.chromaU)-ink->inkU)>80 || std::abs(int(pixel.chromaV)-ink->inkV)>80)
                    return "unowned-native-color";
                // Connectivity cannot turn a broad scenery bridge into glyph
                // ownership. Limit native anti-alias fringe to one sampling step.
                bool nearOwned=false;
                auto firstRow=std::lower_bound(ink->sourceRows.begin(),ink->sourceRows.end(),y-ink->step);
                for(auto r=firstRow;r!=ink->sourceRows.end() && *r<=y+ink->step && !nearOwned;++r)
                    for(int sx=(std::max)(0,(x-ink->step+ink->step-1)/ink->step);
                        sx<ink->width && sx*ink->step<=x+ink->step;++sx)
                        nearOwned|=ink->Get(sx,int(r-ink->sourceRows.begin()),true);
                if(!nearOwned)return "unowned-native-glyph-excursion";
                const bool seed=sy>=0 && x%ink->step==0 && ink->Get(x/ink->step,sy,true);
                mask[size_t(y-top)*bw+x-box.left]=seed?2:1;
            }
        }
        std::vector<size_t> component;
        for(size_t origin=0;origin<area;++origin) {
            if(mask[origin]==0 || mask[origin]==3)continue;
            bool seeded=mask[origin]==2;
            component.clear();component.push_back(origin);mask[origin]=3;
            for(size_t q=0;q<component.size();++q) {
                const int x=int(component[q]%bw),y=int(component[q]/bw);
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    const int nx=x+dx,ny=y+dy;
                    if(nx<0 || nx>=bw || ny<0 || ny>=bottom-top)continue;
                    const size_t next=size_t(ny)*bw+nx;
                    if(mask[next]==1 || mask[next]==2) {
                        seeded|=mask[next]==2;mask[next]=3;component.push_back(next);
                    }
                }
            }
            if(!seeded)return "unowned-native-component";
        }
        return nullptr;
    };
    // The general residual bar check is sampled. Reconciliation may discard
    // only one row, so inspect those newly excluded rows at native resolution.
    // Pixels inside content are already covered by the owned-component scan;
    // every other pixel in this small strip must be neutral black.
    const auto inspectReconciliation=[&](int top,int bottom)->const char* {
        for(int y=top;y<bottom;++y)for(int x=0;x<source.width;++x) {
            if(x>=box.left && x<box.right && y>=box.top && y<box.bottom)continue;
            AnalysisLumaSample pixel;
            if(!source.Sample(x,y,pixel))return "ownership-sample-unavailable";
            ++result.ownershipSamples;
            if(pixel.luma>raw.presentationBlackThreshold ||
                std::abs(int(pixel.chromaU)-512)>32 || std::abs(int(pixel.chromaV)-512)>32)
                return "unowned-native-reconciled-edge";
        }
        return nullptr;
    };
    const char* reconciliationFailure=inspectReconciliation(reconciledTop,base.top);
    if(!reconciliationFailure)reconciliationFailure=inspectReconciliation(base.bottom,reconciledBottom);
    if(reconciliationFailure)return reject(reconciliationFailure,
        std::strcmp(reconciliationFailure,"ownership-sample-unavailable")==0 ?
        QualifiedSubtitlePresentationStatus::Unknown : QualifiedSubtitlePresentationStatus::Conflict);
    if(!preparedErasureOnly) {
        if(const char* reason=inspect(box.top,(std::min)(box.bottom,base.top)))
            return reject(reason,std::strcmp(reason,"ownership-sample-unavailable")==0 ?
                QualifiedSubtitlePresentationStatus::Unknown : QualifiedSubtitlePresentationStatus::Conflict);
        if(const char* reason=inspect((std::max)(box.top,base.bottom),box.bottom))
            return reject(reason,std::strcmp(reason,"ownership-sample-unavailable")==0 ?
                QualifiedSubtitlePresentationStatus::Unknown : QualifiedSubtitlePresentationStatus::Conflict);
    }
    const ActivePictureBounds erased{box.left,box.top,box.right,box.bottom,source.width,source.height,
        double(bw)/(box.bottom-box.top),ActivePictureBounds::BarAxes::NONE};
    // All current-source/cue/capture/geometry and owned-erasure checks above
    // passed. Record composition separately; residual UI below still vetoes
    // native-viewport permission as well as cleanup/retention.
    result.currentCompositionVerified=true;result.currentCompositionMode=input.mode;
    const auto projected=EvaluateActivePicturePresentationRetentionExcludingRelocatedOverlay(source,base,raw,erased,0);
    if(!projected.relocatedOverlayExclusionEvaluated)return reject("outside-proof-unavailable");
    if(!projected.excludedBandsPixelSafe)
        return reject("unowned-picture-outside-erasure",QualifiedSubtitlePresentationStatus::Conflict);
    result.retention=projected;result.erasedSourceBounds=erased;
    // Native picture classification remains raw. Exact owned erasure plus clear
    // remaining bands proves only that this physical presentation can be kept.
    if(!preparedErasureOnly)result.retention.currentlyPixelSafe=true;
    result.status=QualifiedSubtitlePresentationStatus::Qualified;
    result.qualified=true;
    result.preparedErasureOnly=preparedErasureOnly;
    result.renewsHold=!preparedErasureOnly && !accepted.confirmationPending && !observation.pendingRefresh && !continuation;
    result.reason=preparedErasureOnly ? "current-prepared-caption-erasure" :
        continuation ? "current-owned-continuing-caption" : "current-owned-relocated-caption";
    return result;
}
inline QualifiedSubtitlePresentationEvidence EvaluateQualifiedSubtitlePresentationEvidence(
    const AnalysisLumaSource& source,const ActivePicturePresentationRetentionEvidence& raw,
    const QualifiedSubtitlePresentationInput& input)
{
    return EvaluateSubtitlePresentationEvidence(source,raw,input,false);
}
// Black and Reconstruction clear content outside their composed physical picture
// to black in All/CleanupOnly. Once that exact source packet was prepared, pixels
// inside the cleared rectangle cannot request Classic picture movement. Inspect
// the remaining native bands normally; never convert this output fact into glyph
// ownership, native AR evidence, or a renewable submitted-caption lease.
inline QualifiedSubtitlePresentationEvidence EvaluatePreparedSubtitlePresentationEvidence(
    const AnalysisLumaSource& source,const ActivePicturePresentationRetentionEvidence& raw,
    const QualifiedSubtitlePresentationInput& input)
{
    return EvaluateSubtitlePresentationEvidence(source,raw,input,true);
}
inline AlphaSourceCrop::CurrentSubtitleComposition QualifyCurrentSubtitleCompositionPresentation(
    const QualifiedSubtitlePresentationEvidence& proof,
    const ActivePictureFrameIdentity& selectedIdentity,const ActivePictureBounds& nativeBase,
    SubtitlePreviewMode mode)
{
    AlphaSourceCrop::CurrentSubtitleComposition result;
    if(!proof.currentCompositionVerified || proof.currentCompositionMode!=mode ||
        !SubtitlePreviewOverridesClassicHandling(mode) ||
        !SameAnalysisFrameIdentity(proof.identity,selectedIdentity) ||
        !SameSubtitlePresentationBounds(proof.physicalBase,nativeBase))return result;
    result.available=true;result.identity=selectedIdentity;result.nativeBase=nativeBase;
    result.mode=mode==SubtitlePreviewMode::BlackBackground?AlphaSourceCrop::PreparedNativeMode::BLACK:
        AlphaSourceCrop::PreparedNativeMode::RECONSTRUCTION;
    return result;
}
inline AlphaSourceCrop::PreparedSubtitlePresentation QualifyPreparedSubtitleCropPresentation(
    const QualifiedSubtitlePresentationEvidence& proof,
    const ActivePictureFrameIdentity& selectedIdentity,const ActivePictureBounds& nativeBase)
{
    AlphaSourceCrop::PreparedSubtitlePresentation result;
    if (!proof.qualified || proof.status!=QualifiedSubtitlePresentationStatus::Qualified ||
        !SameAnalysisFrameIdentity(proof.identity,selectedIdentity) ||
        !SameSubtitlePresentationBounds(proof.physicalBase,nativeBase) ||
        !proof.retention.analysisValid || !proof.retention.excludedBandsPixelSafe ||
        proof.retention.outwardVisibleBoundsAvailable) return result;
    result.available=true;result.nativeBase=nativeBase;
    result.sourceGeneration=selectedIdentity.transportGeneration;
    result.sourceSequence=selectedIdentity.acceptedSequence;
    result.presentationEpoch=selectedIdentity.viewportGeneration;
    return result;
}

struct QualifiedSubtitlePresentationView
{
    ActivePicturePresentationRetentionEvidence retention;
    AlphaSourceCrop::PresentationEnvelopeExtentState envelope;
    ActivePictureBounds currentBounds;
    bool qualified=false;
};
inline QualifiedSubtitlePresentationView ProjectQualifiedSubtitlePresentationEvidence(
    const QualifiedSubtitlePresentationEvidence& proof,
    const AlphaSourceCrop::PresentationEnvelopeExtentState& rawHistory,
    const ActivePictureBounds& rawCurrent,const ActivePictureBounds& physicalBase,
    const ActivePictureFrameIdentity& selectedIdentity,
    const ActivePicturePresentationRetentionEvidence& raw)
{
    QualifiedSubtitlePresentationView result{raw,rawHistory,rawCurrent,false};
    if(!proof.qualified || proof.status!=QualifiedSubtitlePresentationStatus::Qualified ||
        !SameAnalysisFrameIdentity(proof.identity,selectedIdentity) ||
        !SameSubtitlePresentationBounds(proof.physicalBase,physicalBase))return result;
    result.retention=proof.retention;
    result.envelope={};result.currentBounds=physicalBase;result.qualified=true;
    return result;
}


// This is a submitted-picture lease, not subtitle ownership or erasure permission.
struct QualifiedSubtitlePresentationHoldState
{
    bool available=false;
    QualifiedSubtitlePresentationEvidence proof;
    ActivePictureBounds displayedBounds;
    SubtitlePreviewMode mode=SubtitlePreviewMode::None;
    uint64_t policyGeneration=0,continuityGeneration=0,verifiedTick=0,captureClockFrequency=0;
};
inline bool SameSubtitlePresentationContext(const ActivePictureFrameIdentity& a,const ActivePictureFrameIdentity& b)
{
    return a.transportGeneration==b.transportGeneration && a.sourceFormatGeneration==b.sourceFormatGeneration &&
        a.analysisDomainKey==b.analysisDomainKey && a.viewportGeneration==b.viewportGeneration &&
        a.rendererGeneration==b.rendererGeneration;
}
inline QualifiedSubtitlePresentationHoldState CommitQualifiedSubtitlePresentationHold(
    const QualifiedSubtitlePresentationHoldState& previous,const QualifiedSubtitlePresentationEvidence& proof,
    const ActivePictureFrameIdentity& selectedIdentity,const ActivePictureBounds& displayedBounds,
    SubtitlePreviewMode mode,uint64_t policy,uint64_t continuity,uint64_t nowTick,bool submitted,
    bool confirmedAbsent=false,uint64_t captureClockFrequency=0)
{
    if(!submitted)return previous;
    if(!captureClockFrequency || !SubtitlePreviewOverridesClassicHandling(mode) || confirmedAbsent ||
        proof.status==QualifiedSubtitlePresentationStatus::Conflict)return {};
    const bool sameContext=previous.available && previous.mode==mode && previous.policyGeneration==policy &&
        previous.continuityGeneration==continuity && previous.captureClockFrequency==captureClockFrequency && SameSubtitlePresentationContext(previous.proof.identity,selectedIdentity);
    if(!proof.qualified || !proof.renewsHold || proof.preparedErasureOnly ||
        !SameAnalysisFrameIdentity(proof.identity,selectedIdentity))
        return sameContext && SameSubtitlePresentationBounds(previous.displayedBounds,displayedBounds)
            ? previous:QualifiedSubtitlePresentationHoldState{};
    const auto& base=proof.physicalBase;
    const bool physicalDisplay=displayedBounds.rasterWidth==base.rasterWidth && displayedBounds.rasterHeight==base.rasterHeight &&
        displayedBounds.left>=base.left && displayedBounds.right<=base.right && displayedBounds.left<displayedBounds.right &&
        displayedBounds.top==base.top && displayedBounds.bottom==base.bottom;
    if(!physicalDisplay)return {};
    if(sameContext && selectedIdentity.acceptedSequence<=previous.proof.identity.acceptedSequence)
        return previous; // Repeated output never renews the source evidence lease.
    return {true,proof,displayedBounds,mode,policy,continuity,nowTick,captureClockFrequency};
}
struct QualifiedSubtitleUnknownHoldInput
{
    QualifiedSubtitlePresentationHoldState previous;
    ActivePictureFrameIdentity currentIdentity;
    ActivePictureBounds physicalBase;
    SubtitlePreviewMode mode=SubtitlePreviewMode::None;
    uint64_t policyGeneration=0,continuityGeneration=0,nowTick=0,holdMs=250,captureClockFrequency=0;
    bool measurementUnknown=false,confirmedAbsent=false,retentionCurrent=false;
    QualifiedSubtitlePresentationStatus currentStatus=QualifiedSubtitlePresentationStatus::Unknown;
};
struct QualifiedSubtitleUnknownHoldDecision
{
    bool retained=false;
    ActivePictureBounds bounds;
    uint64_t leaseAgeMs=0;
    const char* reason="no-submitted-qualified-caption";
};
inline QualifiedSubtitleUnknownHoldDecision EvaluateQualifiedSubtitleUnknownHold(
    const AnalysisLumaSource& source,const ActivePicturePresentationRetentionEvidence& raw,
    const QualifiedSubtitleUnknownHoldInput& input)
{
    QualifiedSubtitleUnknownHoldDecision result;
    const auto fail=[&](const char* reason){result.reason=reason;return result;};
    const auto& previous=input.previous;
    if(!previous.available || !previous.proof.qualified)return result;
    if(!input.captureClockFrequency || previous.captureClockFrequency!=input.captureClockFrequency ||
        !SubtitlePreviewOverridesClassicHandling(input.mode) || previous.mode!=input.mode ||
        previous.policyGeneration!=input.policyGeneration || previous.continuityGeneration!=input.continuityGeneration ||
        !SameSubtitlePresentationContext(previous.proof.identity,input.currentIdentity) ||
        !SameSubtitlePresentationBounds(previous.proof.physicalBase,input.physicalBase))
        return fail("unknown-hold-context-changed");
    if(!input.measurementUnknown || input.confirmedAbsent ||
        input.currentStatus!=QualifiedSubtitlePresentationStatus::Unknown)
        return fail("unknown-hold-not-unknown");
    const auto& old=previous.proof.identity;
    if(!old.captureTimestamp || input.currentIdentity.captureTimestamp<old.captureTimestamp ||
        input.currentIdentity.acceptedSequence<old.acceptedSequence)
        return fail("unknown-hold-nonmonotonic-source");
    const uint64_t elapsed=input.currentIdentity.captureTimestamp-old.captureTimestamp;
    const uint64_t frequency=input.captureClockFrequency;
    const uint64_t limitMs=(std::min)(input.holdMs,uint64_t(250));
    const uint64_t allowedTicks=(frequency/1000)*limitMs+((frequency%1000)*limitMs)/1000;
    result.leaseAgeMs=static_cast<uint64_t>((static_cast<long double>(elapsed)*1000)/frequency);
    if(elapsed>allowedTicks)
        return fail("unknown-hold-source-lease-expired");
    if(!input.retentionCurrent || !source.IsValid() || !raw.analysisValid || !raw.presentationValid)
        return fail("unknown-hold-current-residual-unavailable");
    if(raw.activePicture.available &&
        (raw.activePicture.classification==ActivePictureClassification::FULL_RASTER_TRUSTED ||
        (raw.activePicture.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
         !SameSubtitlePresentationBounds(raw.activePicture.trustedBounds,input.physicalBase))))
        return fail("unknown-hold-independent-picture-change");
    // This source has no current qualified erasure. An earlier erased footprint
    // can test independent contradictions, but cannot make its current raw
    // pixels disappear from the output. Only completed native-band inspection
    // of this exact current base may retain the old native viewport.
    if(!raw.rawBandSafetyEvaluated ||
        !SameSubtitlePresentationBounds(raw.rawBandSafetyBase,input.physicalBase))
        return fail("unknown-hold-current-raw-output-unavailable");
    if(!raw.rawBandLeftSafe || !raw.rawBandTopSafe ||
        !raw.rawBandRightSafe || !raw.rawBandBottomSafe)
        return fail("unknown-hold-current-raw-output-occupied");
    // The old footprint is used only to test for independent contradictions.
    // Neither this temporary result nor the old footprint is published as
    // current pixel safety, capture coverage, or cleanup authority.
    const auto residual=EvaluateActivePicturePresentationRetentionExcludingRelocatedOverlay(
        source,input.physicalBase,raw,previous.proof.erasedSourceBounds,0);
    if(!residual.relocatedOverlayExclusionEvaluated || !residual.excludedBandsPixelSafe)
        return fail("unknown-hold-unowned-picture-conflict");
    result.retained=true;result.bounds=previous.displayedBounds;
    result.reason="retain-submitted-picture-during-caption-unknown";
    return result;
}
