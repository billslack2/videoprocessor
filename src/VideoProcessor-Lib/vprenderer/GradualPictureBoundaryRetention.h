#pragma once
#include "BufferedPictureExpansion.h"
#include <SceneDetector.h>
#include <algorithm>
#include <cmath>

namespace AlphaSourceCrop
{
struct GradualPictureBoundaryRetentionDiagnostics
{
    const char* reason = "not-evaluated";
    const char* pixelReason = "not-evaluated";
    bool pixelProofEvaluated = false;
    bool broadOpposingPicture = false;
    bool nativeDistributedPicture = false;
    bool stepsEvaluated = false;
    int topStep = 0, bottomStep = 0, scanStep = 0;
    uint64_t previousSequence = 0, observedNativeSequence = 0, currentSequence = 0;
    ActivePictureBounds previousNative, currentNative;
};

// A darkness notification may occur inside an already proved authored ramp.
// Preserve only that episode after exact current native geometry corroborates
// its trajectory. This grants no acquisition, endpoint or caption authority.
inline MovingPictureTransitionState RetainMovingPictureAtDarknessBoundary(
    const MovingPictureTransitionState& previous, const SceneDetectorResult& boundary,
    const ActivePictureFrameIdentity& identity, const TransitionAdmissionInput& current,
    GradualPictureBoundaryRetentionDiagnostics* diagnostic = nullptr)
{
    if (diagnostic) {
        *diagnostic = {};
        diagnostic->previousSequence = previous.identity.acceptedSequence;
        diagnostic->observedNativeSequence = previous.observedNativeSourceSequence;
        diagnostic->currentSequence = identity.acceptedSequence;
        diagnostic->previousNative = previous.observedNative;
        diagnostic->currentNative = current.evidence.trustedBounds;
    }
    const auto reject = [&](const char* reason) {
        if (diagnostic) diagnostic->reason = reason;
        return MovingPictureTransitionState{};
    };
    const auto& old=previous.identity;
    if (!previous.active || previous.awaitingPublication) return reject("inactive-episode");
    if (!boundary.safeBoundary || !boundary.nearBlackEntry || boundary.hardCutCandidate ||
        boundary.hardCutConfirmed || !boundary.differenceEvaluated || boundary.sampleCount==0)
        return reject("boundary-kind");
    if (boundary.generation!=identity.transportGeneration || boundary.sourceSequence!=identity.acceptedSequence)
        return reject("boundary-identity");
    if (identity.transportGeneration==0 || identity.sourceFormatGeneration==0 ||
        identity.rendererGeneration==0 || old.transportGeneration!=identity.transportGeneration ||
        old.sourceFormatGeneration!=identity.sourceFormatGeneration ||
        old.analysisDomainKey!=identity.analysisDomainKey || old.viewportGeneration!=identity.viewportGeneration ||
        old.rendererGeneration!=identity.rendererGeneration) return reject("source-context");
    if (old.acceptedSequence==0 || old.acceptedSequence==UINT64_MAX ||
        identity.acceptedSequence!=old.acceptedSequence+1 || old.sourceFrameNumber==0 ||
        old.sourceFrameNumber==UINT64_MAX || identity.sourceFrameNumber!=old.sourceFrameNumber+1 ||
        old.captureTimestamp==0 || identity.captureTimestamp<=old.captureTimestamp)
        return reject("nonadjacent-source");
    if (!current.compatiblePresentation || !current.trustedGeometryAvailable)
        return reject("presentation-unavailable");
    if (current.sourceGeneration!=identity.transportGeneration ||
        current.trustedGeneration!=identity.transportGeneration || current.sourceSequence!=identity.acceptedSequence)
        return reject("measurement-identity");
    if (!current.retention.analysisValid || !current.retention.presentationValid ||
        current.retention.EffectiveArNearBlack()) return reject("measurement-unavailable-or-dark");
    if (!current.evidence.available || current.evidence.authorityOrigin!=ActivePictureAuthorityOrigin::NATIVE ||
        current.evidence.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
        current.evidence.axisEvidence.HasFailedBar()) return reject("native-authority");
    const auto same=[](const ActivePictureBounds& a,const ActivePictureBounds& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight && a.trustedBarAxes==b.trustedBarAxes;
    };
    const auto valid=[](const ActivePictureBounds& b) {
        return b.rasterWidth>0 && b.rasterHeight>0 && b.left==0 && b.right==b.rasterWidth &&
            b.top>0 && b.bottom<b.rasterHeight && b.bottom>b.top && b.top%2==0 && b.bottom%2==0 &&
            b.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM;
    };
    const auto& base=previous.base;
    // The certified last edge may be many sources behind during an uncertain
    // strip interval. Only the immediately preceding measured native position
    // can certify a small step across this scene notification.
    const auto& observed=previous.observedNative;
    const auto& measured=current.evidence.trustedBounds;
    const auto& vertical=current.evidence.axisEvidence.vertical;
    if (!valid(base)) return reject("invalid-base");
    if (!valid(observed)) return reject("invalid-native-predecessor");
    if (previous.observedNativeSourceSequence!=old.acceptedSequence)
        return reject("native-predecessor-sequence");
    if (!valid(measured)) return reject("invalid-native-geometry");
    if (!same(base,current.trustedGeometry)) return reject("logical-base-mismatch");
    if (!same(base,current.presentationBeforeObservation)) return reject("presentation-base-mismatch");
    if (!same(measured,current.outwardCandidate)) return reject("outward-candidate-mismatch");
    // Raw proposals intentionally carry no trusted-axis authority. Compare
    // their geometry only; authority is checked on measured/vertical below.
    if (measured.left!=current.evidence.proposedBounds.left ||
        measured.top!=current.evidence.proposedBounds.top ||
        measured.right!=current.evidence.proposedBounds.right ||
        measured.bottom!=current.evidence.proposedBounds.bottom ||
        measured.rasterWidth!=current.evidence.proposedBounds.rasterWidth ||
        measured.rasterHeight!=current.evidence.proposedBounds.rasterHeight) return reject("raw-proposal-mismatch");
    if (!vertical.scanComplete || vertical.state!=ActivePictureAxisState::TRUSTED_BARS)
        return reject("native-axis-authority");
    if (measured.rasterWidth!=base.rasterWidth || measured.rasterHeight!=base.rasterHeight ||
        observed.rasterWidth!=base.rasterWidth || observed.rasterHeight!=base.rasterHeight ||
        measured.top>=base.top || measured.bottom<=base.bottom) return reject("native-geometry");
    const int topStep=observed.top-measured.top, bottomStep=measured.bottom-observed.bottom;
    const int scanStep=(std::max)(2,base.rasterHeight/540);
    if (diagnostic) {
        diagnostic->stepsEvaluated = true;
        diagnostic->topStep = topStep; diagnostic->bottomStep = bottomStep; diagnostic->scanStep = scanStep;
    }
    // A static shot or discontinuous edge jump cannot borrow the old ramp.
    if (topStep<0 || bottomStep<0 || topStep>scanStep || bottomStep>scanStep ||
        (topStep==0 && bottomStep==0)) return reject("edge-step");
    if (!current.retention.expansionStripsAvailable ||
        !same(current.retention.expansionBase,base) ||
        !same(current.retention.expansionCandidate,measured)) return reject("strip-identity");
    const auto pixels=ConfirmOutwardPictureTransition({},base,measured,current.retention,
        current.sourceGeneration,current.sourceSequence);
    if (diagnostic) {
        diagnostic->pixelProofEvaluated = true;
        diagnostic->broadOpposingPicture = pixels.broadOpposingPicture;
        diagnostic->nativeDistributedPicture = pixels.nativeDistributedPicture;
        diagnostic->pixelReason = pixels.diagnosticReason;
    }
    if (!pixels.broadOpposingPicture && !pixels.nativeDistributedPicture) return reject("pixel-proof");
    auto retained=previous;
    retained.directionalChanges=0;
    retained.quietSamples=0;
    retained.quietAnchor=measured;
    if (diagnostic) diagnostic->reason = "retained";
    return retained;
}
}
