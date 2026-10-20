#pragma once
#include <SubtitlePreviewMode.h>
#include <SubtitleCutPaste.h>
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
    return !captionPresentationHeld && fill.applied && presentation.applyCrop &&
        !fixedCropAspectConfigured && !nlsRequested && !protectedCaptionFit &&
        !presentation.outwardExpanded &&
        (!presentation.verticallyTranslated ||
            (SubtitlePreviewOverridesClassicHandling(mode) &&
             presentation.owner==AlphaSourceCrop::DecisionOwner::VERTICAL_TRANSLATION)) &&
        !crop.movingPictureTransition && !recoveryActive &&
        origin==ActivePictureAuthorityOrigin::NATIVE && exactNativeBounds;
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
        current.barAuthority && !SubtitleAbsenceIsUnknown(current) &&
        !current.text.detected &&
        current.text.diagnosticReason &&
        std::strcmp(current.text.diagnosticReason,"no-bright-ink")==0 &&
        !accepted.detected && !accepted.held;
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
    ActivePictureBounds physicalBase;
    uint64_t policyGeneration=0,continuityGeneration=0;
    bool sourcePrepared = false, pendingCurrentPixelsVerified=false;
    size_t maxOwnershipSamples=262144;
};
struct QualifiedSubtitlePresentationEvidence
{
    ActivePicturePresentationRetentionEvidence retention;
    ActivePictureFrameIdentity identity;
    ActivePictureBounds physicalBase;
    ActivePictureBounds erasedSourceBounds;
    QualifiedSubtitlePresentationStatus status=QualifiedSubtitlePresentationStatus::Unknown;
    bool qualified = false;
    bool renewsHold=false;
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
inline QualifiedSubtitlePresentationEvidence EvaluateQualifiedSubtitlePresentationEvidence(
    const AnalysisLumaSource& source, const ActivePicturePresentationRetentionEvidence& raw,
    const QualifiedSubtitlePresentationInput& input)
{
    QualifiedSubtitlePresentationEvidence result;
    result.retention=raw;result.identity=input.currentIdentity;result.physicalBase=input.physicalBase;
    const auto reject=[&](const char* reason,QualifiedSubtitlePresentationStatus status=QualifiedSubtitlePresentationStatus::Unknown) {
        result.reason=reason;result.status=status;return result;
    };
    if(!SubtitlePreviewOverridesClassicHandling(input.mode))
        return reject("mode-not-relocated",QualifiedSubtitlePresentationStatus::NotApplicable);
    if(!input.sourcePrepared || !input.observation || !input.accepted || !input.composition)
        return reject("source-not-prepared");
    const auto& observation=*input.observation;
    if(observation.glyphLocalTop || observation.glyphLocalBottom)
        return reject("glyph-local-evidence-is-not-ar-proof");
    const auto& accepted=*input.accepted;
    const auto& move=*input.composition;
    const auto& base=input.physicalBase;
    const auto* continuation=input.continuation;
    if(continuation && !continuation->Matches(input.currentIdentity,input.policyGeneration,
        input.continuityGeneration,accepted,base))
        return reject("continuation-context-mismatch");
    if(continuation && CurrentPictureRejectsSubtitleContinuation(raw.activePicture,base))
        return reject("independent-picture-expansion",QualifiedSubtitlePresentationStatus::Conflict);
    // A verified existing cue supplies only current local glyph ownership. The
    // worker observation and raw picture evidence remain the measurement of this
    // frame; a continuation cannot acquire or renew active-picture authority.
    const auto& text=continuation ? continuation->accepted : observation.text;
    const int pictureTop=continuation ? continuation->pictureTop : observation.pictureTop;
    const int pictureBottom=continuation ? continuation->pictureBottom : observation.pictureBottom;
    if(raw.activePicture.available && raw.activePicture.classification==ActivePictureClassification::FULL_RASTER_TRUSTED)
        return reject("independent-full-raster-picture",QualifiedSubtitlePresentationStatus::Conflict);
    if(!SameAnalysisFrameIdentity(observation.identity,input.currentIdentity) ||
        observation.policyGeneration!=input.policyGeneration || observation.continuityGeneration!=input.continuityGeneration ||
        observation.discontinuity || !observation.analyzed || !observation.barAuthority ||
        (observation.pendingRefresh && !input.pendingCurrentPixelsVerified) || text.workLimit ||
        !text.detected || !text.nearBarEligibilityMeasured || !text.nearBarEligible ||
        !accepted.detected || (accepted.held && !continuation) || !accepted.bounds.Valid() || !text.bounds.Valid() ||
        text.lineCount<1 || text.lineCount>3 || !move.valid)
        return reject("current-caption-proof-unavailable");
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
        if(!captured || !contains(move.content,measured))
            return reject("incomplete-current-glyph-capture");
    }
    if(!source.IsValid() || source.generation!=input.currentIdentity.transportGeneration || observation.width!=source.width || observation.height!=source.height ||
        base.rasterWidth!=source.width || base.rasterHeight!=source.height ||
        base.left<0 || base.top<0 || base.right>source.width || base.bottom>source.height ||
        base.left>=base.right || base.top>=base.bottom ||
        pictureTop<0 || pictureBottom>source.height ||
        pictureTop>=pictureBottom ||
        move.pictureTop!=base.top || move.pictureBottom!=base.bottom ||
        !raw.analysisValid || !raw.presentationValid || raw.presentationBlackThreshold<=0 ||
        raw.excludedTop.barPixels!=base.top || raw.excludedBottom.barPixels!=source.height-base.bottom ||
        raw.excludedLeft.barPixels!=base.left || raw.excludedRight.barPixels!=source.width-base.right)
        return reject("physical-base-mismatch");
    // FitSubtitleToVisiblePicture reconciles these independent edge estimates.
    // The composed physical edges and current raw bands must still match exactly;
    // native ownership and residual-band inspection below validate every exclusion.
    if(std::abs(pictureTop-base.top)>SubtitlePhysicalEdgeReconciliationTolerance ||
        std::abs(pictureBottom-base.bottom)>SubtitlePhysicalEdgeReconciliationTolerance)
        return reject("subtitle-observation-edge-mismatch");
    const SubtitleBoxRect baseRect{base.left,base.top,base.right,base.bottom};
    if(!contains(baseRect,move.destination))return reject("destination-outside-picture");
    const auto& box=move.content;
    if(!box.Valid() || box.left<base.left || box.right>base.right || box.top<0 || box.bottom>source.height)
        return reject("invalid-erasure-footprint");
    if(box.top>=base.top && box.bottom<=base.bottom)
        return reject("no-erased-bar-overlap",QualifiedSubtitlePresentationStatus::NotApplicable);
    // Continuation grids cover a bounded region, not the detector's full search
    // bands. Every capture cell used below must lie in that freshly sampled ROI.
    if(continuation && !contains(continuation->sampledBounds,box))
        return reject("continuation-ownership-outside-sampled-region");
    const auto* ink=continuation ? continuation->ink.get() : observation.ink.get();
    if(!ink || (!ink->rawEvidenceComplete && !continuation) || !ink->paletteValid || ink->step<1 || ink->step>8 ||
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
    if(outsideRows*size_t(bw)+extraSamples>input.maxOwnershipSamples)
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
    if(const char* reason=inspect(box.top,(std::min)(box.bottom,base.top)))
        return reject(reason,std::strcmp(reason,"ownership-sample-unavailable")==0 ?
            QualifiedSubtitlePresentationStatus::Unknown : QualifiedSubtitlePresentationStatus::Conflict);
    if(const char* reason=inspect((std::max)(box.top,base.bottom),box.bottom))
        return reject(reason,std::strcmp(reason,"ownership-sample-unavailable")==0 ?
            QualifiedSubtitlePresentationStatus::Unknown : QualifiedSubtitlePresentationStatus::Conflict);
    const ActivePictureBounds erased{box.left,box.top,box.right,box.bottom,source.width,source.height,
        double(bw)/(box.bottom-box.top),ActivePictureBounds::BarAxes::NONE};
    const auto projected=EvaluateActivePicturePresentationRetentionExcludingRelocatedOverlay(source,base,raw,erased,0);
    if(!projected.relocatedOverlayExclusionEvaluated)return reject("outside-proof-unavailable");
    if(!projected.excludedBandsPixelSafe)
        return reject("unowned-picture-outside-erasure",QualifiedSubtitlePresentationStatus::Conflict);
    result.retention=projected;result.erasedSourceBounds=erased;
    // Native picture classification remains raw. Exact owned erasure plus clear
    // remaining bands proves only that this physical presentation can be kept.
    result.retention.currentlyPixelSafe=true;
    result.status=QualifiedSubtitlePresentationStatus::Qualified;
    result.qualified=true;
    result.renewsHold=!observation.pendingRefresh && !continuation;
    result.reason=continuation ? "current-owned-continuing-caption" : "current-owned-relocated-caption";
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
    if(!proof.qualified || !proof.renewsHold || !SameAnalysisFrameIdentity(proof.identity,selectedIdentity))
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
