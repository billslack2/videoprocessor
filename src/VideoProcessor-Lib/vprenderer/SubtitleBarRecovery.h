#pragma once
#include <SubtitleBarEvidence.h>
#include <ActivePictureEvidence.h>
#include <SubtitleBoxLookahead.h>

// Inspection only. Remembered geometry must be re-proved by BOTH physical
// edges in this frame; it is not permission to crop or reuse old subtitle ink.
inline ActivePictureBounds RecoverSubtitleInspectionBars(const AnalysisLumaSource& source,
    const ActivePictureBounds& trusted, uint64_t trustedGeneration,
    ActivePictureClassification classification, bool enabled, bool blocked)
{
    if (!enabled || blocked || !source.IsValid() || !source.generation ||
        trustedGeneration != source.generation ||
        (classification != ActivePictureClassification::PROVISIONAL &&
         classification != ActivePictureClassification::BAR_CROP_TRUSTED) ||
        trusted.rasterWidth != source.width || trusted.rasterHeight != source.height ||
        trusted.left != 0 || trusted.right != source.width ||
        trusted.top <= 0 || trusted.bottom >= source.height || trusted.top >= trusted.bottom)
        return {};
    const auto bars = ExtractSubtitleBarEvidence(source);
    const int tolerance = (std::max)(2, 2 * (source.height / 540));
    const bool independentlyProven = bars.available && bars.top > 0 &&
        bars.bottom < source.height && trusted.top <= bars.top &&
        trusted.bottom >= bars.bottom && std::abs(bars.top - trusted.top) <= tolerance &&
        std::abs(bars.bottom - trusted.bottom) <= tolerance;
    if (!independentlyProven)
    {
        // Dark scenery can hide the first non-black run at the outer flanks.
        // Revisit only this already trusted plane using fresh, unmasked pixels:
        // both edges still need distributed abrupt contrast and safe black bands.
        // A newly proved different edge outranks history inside revalidation.
        // This authorizes subtitle inspection, never crop or caption ownership.
        const auto reference = RevalidateSubtitleBarEvidence(source, bars,
            trusted.top, trusted.bottom);
        if (!reference.topRevalidatedAtReference || !reference.bottomRevalidatedAtReference)
            return {};
    }
    // Keep the same Classic contract and caption placement. It may include a
    // sampling fringe of extra black, but must contain all current picture rows.
    return trusted;
}
// A completed exact-frame worker proof already inspected both physical edges
// and every sampled pixel in the guarded excluded bands. Reuse that proof for
// dense subtitle inspection when the ordinary first-run extractor sees the
// physical edge just outside its tolerance for the guarded logical rectangle.
// This never grants crop authority or carries a proof to another source frame.
inline ActivePictureBounds RecoverSubtitleInspectionBarsFromWorkerProof(
    const AnalysisLumaSource& source, const ActivePictureBounds& trusted,
    uint64_t trustedGeneration, ActivePictureClassification classification,
    const SubtitleAssistedVerticalBarProof& proof,
    const ActivePictureFrameIdentity& identity, uint64_t policyGeneration,
    uint64_t continuityGeneration, SubtitleScriptHint scriptHint,
    bool enabled, bool blocked)
{
    if (!enabled || blocked || trustedGeneration != source.generation ||
        (classification != ActivePictureClassification::PROVISIONAL &&
         classification != ActivePictureClassification::BAR_CROP_TRUSTED) ||
        !proof.IsCurrentFor(source, identity, policyGeneration,
            continuityGeneration, scriptHint) ||
        proof.evidence.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        proof.evidence.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
        proof.evidence.axisEvidence.vertical.state != ActivePictureAxisState::TRUSTED_BARS ||
        !proof.evidence.top.trusted || !proof.evidence.bottom.trusted ||
        trusted.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        trusted.rasterWidth != source.width || trusted.rasterHeight != source.height ||
        trusted.left != 0 || trusted.right != source.width ||
        trusted.top <= 0 || trusted.bottom >= source.height || trusted.top >= trusted.bottom)
        return {};

    const auto sameBounds = [](const ActivePictureBounds& a,
                               const ActivePictureBounds& b) {
        return a.left == b.left && a.top == b.top &&
            a.right == b.right && a.bottom == b.bottom &&
            a.rasterWidth == b.rasterWidth && a.rasterHeight == b.rasterHeight &&
            a.trustedBarAxes == b.trustedBarAxes;
    };
    if (!sameBounds(trusted, proof.candidate) ||
        !sameBounds(trusted, proof.evidence.trustedBounds))
        return {};

    const auto& axes = proof.evidence.axisEvidence;
    if (axes.verticalCropProfileEvaluated)
    {
        if (!axes.verticalCropProfileClean || !axes.verticalCropGlyphExclusions ||
            !proof.evidence.verticalBarGuardProfile.completed ||
            !proof.evidence.verticalBarGuardProfile.clean ||
            axes.sidePictureWidth != source.width ||
            axes.sidePictureHeight != source.height ||
            axes.sidePictureTop <= 0 ||
            axes.sidePictureBottom >= source.height ||
            axes.sidePictureTop >= axes.sidePictureBottom)
            return {};
        const int step = (std::max)(2, source.height / 540);
        const int guardedTop = ((axes.sidePictureTop - 2 * step + 1) / 2) * 2;
        const int guardedBottom = (std::min)(source.height,
            ((axes.sidePictureBottom + 2 * step) / 2) * 2);
        if (trusted.top != guardedTop || trusted.bottom != guardedBottom ||
            axes.verticalCropGuardTop != axes.sidePictureTop - trusted.top ||
            axes.verticalCropGuardBottom != trusted.bottom - axes.sidePictureBottom)
            return {};
    }
    return trusted;
}

// A source-context-bound nomination may survive a presentation lease expiry.
// The worker still has to prove physical edges and glyphs from every new frame.
inline SubtitleInspectionCandidate NominateSubtitleInspectionCandidate(
    const AnalysisLumaSource& source,const ActivePictureBounds& native,
    uint64_t nativeSourceGeneration,uint64_t nativeSourceFormat,
    ActivePictureAuthorityOrigin origin,ActivePictureClassification classification,
    uint64_t nativeGeometryRevision,const ActivePictureFrameIdentity& identity,
    uint64_t policy,uint64_t continuity,bool enabled,bool blocked)
{
    SubtitleInspectionCandidate result;
    if(!enabled || blocked || !source.IsValid() || source.generation!=identity.transportGeneration ||
        nativeSourceGeneration!=identity.transportGeneration || nativeSourceFormat!=identity.sourceFormatGeneration ||
        origin!=ActivePictureAuthorityOrigin::NATIVE || classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
        native.trustedBarAxes!=ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        native.rasterWidth!=source.width || native.rasterHeight!=source.height ||
        native.left!=0 || native.right!=source.width || native.top<=0 || native.bottom>=source.height ||
        native.top>=native.bottom)return result;
    result.valid=true;result.bounds=native;
    result.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;result.bounds.aspectRatio=0.0;
    result.originIdentity=identity;result.policyGeneration=policy;result.continuityGeneration=continuity;
    result.nativeGeometryRevision=nativeGeometryRevision;
    if(!result.AvailableFor(identity,source.width,source.height,policy,continuity))return {};
    return result;
}