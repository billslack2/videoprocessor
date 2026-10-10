#pragma once

#include "AlphaSourceCropPolicy.h"
#include <SubtitleCutPaste.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif

namespace AlphaSourceCrop
{
// A certificate for this composed frame only. It never acquires geometry, changes
// the shared Decision, or supplies observations to the logical AR state machine.
struct SubtitlePresentationEvidenceInput
{
    Decision admitted;
    ActivePictureBounds logical;
    ActivePictureClassification classification = ActivePictureClassification::UNAVAILABLE;
    CropPresentationAdmissionState priorAdmission;
    ActivePictureFrameIdentity identity;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    const SubtitleBoxObservation* observation = nullptr;
    const SubtitleBoxResult* acceptedCue = nullptr;
    SubtitleCutPasteGeometry move;
    int backgroundMode = 3;
    bool compositionSucceeded = false, sourceDiscontinuity = false;
    bool transitionBlocked = false, recoveryBlocked = false, conflictingOwnership = false;
};

struct SubtitlePresentationEvidence
{
    bool accepted = false;
    ActivePictureBounds bounds;
    ActivePicturePresentationRetentionEvidence retention;
    size_t samples = 0;
    const char* reason = "not-evaluated";
};

inline SubtitlePresentationEvidence EvaluateSubtitlePresentationEvidence(
    const SubtitlePresentationEvidenceInput& input, const AnalysisLumaSource& source)
{
    SubtitlePresentationEvidence result;
    auto reject = [&](const char* reason) { result.reason = reason; return result; };
    auto same = [](const ActivePictureBounds& a, const ActivePictureBounds& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight &&
            a.trustedBarAxes==b.trustedBarAxes;
    };
    auto sameRect = [](const SubtitleBoxRect& a, const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    };
    auto contains = [](const SubtitleBoxRect& a, const SubtitleBoxRect& b) {
        return a.Valid() && b.Valid() && a.left<=b.left && a.top<=b.top &&
            a.right>=b.right && a.bottom>=b.bottom;
    };
    const auto& b=input.logical;
    const auto& decision=input.admitted;
    const auto& move=input.move;
    if (!source.IsValid() || source.width<64 || source.height<48 ||
        source.width>8192 || source.height>4320 || !input.identity.acceptedSequence ||
        source.generation!=input.identity.transportGeneration ||
        input.sourceDiscontinuity || input.transitionBlocked || input.recoveryBlocked ||
        input.conflictingOwnership) return reject("context-or-transition-blocked");
    if (input.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
        b.trustedBarAxes!=ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        b.rasterWidth!=source.width || b.rasterHeight!=source.height ||
        b.left!=0 || b.right!=source.width || b.top<0 || b.bottom>source.height ||
        b.top>=b.bottom || (b.top==0 && b.bottom==source.height))
        return reject("logical-geometry-unproved");
    if (!input.priorAdmission.available || !same(input.priorAdmission.trustedCrop,b) ||
        input.priorAdmission.sourceGeneration!=input.identity.transportGeneration ||
        input.priorAdmission.presentationEpoch!=input.identity.viewportGeneration)
        return reject("prior-admission-mismatch");
    const auto& outward=decision.sourceBounds;
    if (!decision.applyCrop || !decision.outwardExpanded || decision.verticallyTranslated ||
        decision.verticalTranslationPixels || decision.owner!=DecisionOwner::OUTWARD_FIT ||
        decision.withdrawalCause!=WithdrawalCause::NONE ||
        outward.rasterWidth!=source.width || outward.rasterHeight!=source.height ||
        outward.left!=b.left || outward.right!=b.right || outward.top<0 ||
        outward.bottom>source.height || outward.top>b.top || outward.bottom<b.bottom ||
        (outward.top==b.top && outward.bottom==b.bottom))
        return reject("shared-decision-not-outward-fit");
    if (!input.compositionSucceeded || !move.valid ||
        (input.backgroundMode!=3 && input.backgroundMode!=5) ||
        move.pictureTop!=b.top || move.pictureBottom!=b.bottom)
        return reject("composition-not-certified");
    const SubtitleBoxRect raster{0,0,source.width,source.height};
    const SubtitleBoxRect logical{b.left,b.top,b.right,b.bottom};
    if (!contains(raster,move.source) || !contains(move.source,move.content) ||
        !contains(logical,move.destination)) return reject("invalid-composition-footprint");
    // The GLSL panel includes six source pixels on each side in modes 3/5.
    // Attached backgrounds extend to the physical picture edge, not beyond it.
    auto panel=move.destination;
    panel.left-=6; panel.right+=6;
    const bool fromTop=move.source.top+move.source.bottom<move.pictureTop+move.pictureBottom;
    if (move.extendToBar) {
        if (fromTop) panel.top=(std::min)(panel.top,move.pictureTop);
        else panel.bottom=(std::max)(panel.bottom,move.pictureBottom);
    }
    if (!contains(logical,panel)) return reject("destination-outside-logical-picture");
    if (!input.observation || !input.acceptedCue) return reject("missing-current-cue");
    const auto& observed=*input.observation;
    const auto& accepted=*input.acceptedCue;
    if (!SameActivePictureFrameIdentity(observed.identity,input.identity) ||
        observed.policyGeneration!=input.policyGeneration ||
        observed.continuityGeneration!=input.continuityGeneration ||
        observed.width!=source.width || observed.height!=source.height ||
        !observed.analyzed || !observed.barAuthority || observed.pictureTop!=b.top || observed.pictureBottom!=b.bottom ||
        observed.discontinuity || observed.pendingRefresh || observed.analysisRefresh ||
        !observed.sharedPicture.AvailableFor(input.identity,source.width,source.height) ||
        !same(observed.sharedPicture.bounds,b) ||
        !observed.text.detected || observed.text.held || observed.text.workLimit ||
        !accepted.detected || accepted.held || accepted.workLimit ||
        !sameRect(accepted.bounds,observed.text.bounds) ||
        !sameRect(move.content,SubtitleGlyphCaptureBounds(accepted,source.width,source.height)) || accepted.lineCount<1 || accepted.lineCount>3 ||
        accepted.lineCount!=observed.text.lineCount)
        return reject("current-cue-not-exact");
    const auto captureLines=SubtitleGlyphCaptureLines(accepted,source.width,source.height);
    for (int line=0;line<accepted.lineCount;++line) {
        if (!sameRect(accepted.lineBounds[line],observed.text.lineBounds[line]) ||
            !sameRect(move.glyphLines[line],captureLines[line]) ||
            !contains(move.content,observed.text.lineBounds[line]))
            return reject("capture-lines-not-current");
    }
    if (!observed.ink || !observed.ink->rawEvidenceComplete) return reject("missing-current-owned-ink");
    const auto& ink=*observed.ink;
    const int step=SubtitleBoxDetector::SamplingStep(source.width,source.height);
    const size_t words=(size_t(ink.width)*size_t(ink.height)+63)/64;
    if (ink.step!=step || ink.width!=(source.width+step-1)/step ||
        ink.height!=(source.height+step-1)/step || ink.sourceRows.size()!=size_t(ink.height) ||
        ink.rawInk.size()!=words || ink.ownedInk.size()!=words || ink.blackBacking.size()!=words)
        return reject("invalid-current-ink-grid");
    unsigned currentOwned=0;
    int previousY=-1;
    for (int sy=0;sy<ink.height;++sy) {
        const int y=ink.sourceRows[sy];
        if (y<0 || y>=source.height || y<=previousY) return reject("invalid-current-ink-rows");
        previousY=y;
        if (y>=b.top && y<b.bottom) continue;
        for (int sx=0;sx<ink.width;++sx) {
            if (!ink.Get(sx,sy,true)) continue;
            const int x=sx*step;
            if (!ink.Get(sx,sy) || x<move.content.left || x>=move.content.right ||
                y<move.content.top || y>=move.content.bottom)
                return reject("owned-ink-outside-erasure");
            AnalysisLumaSample pixel;
            ++result.samples;
            if (!source.Sample(x,y,pixel) || pixel.luma<=104 ||
                std::abs(int(pixel.chromaU)-512)>32 || std::abs(int(pixel.chromaV)-512)>32)
                return reject("owned-ink-not-in-current-source");
            ++currentOwned;
        }
    }
    if (currentOwned<4) return reject("insufficient-current-bar-ink");
    // Only the intersection of source and unpadded content is unconditionally
    // erased in these shader modes. Generated cleanup/layout rectangles never
    // grant AR evidence or extend this footprint into source picture pixels.
    ActivePictureBounds erased{move.content.left,move.content.top,move.content.right,
        move.content.bottom,source.width,source.height};
    const auto raw=EvaluateActivePicturePresentationRetention(source,b);
    if (!raw.analysisValid || !raw.presentationValid || raw.globalNearBlack ||
        raw.activePicture.classification==ActivePictureClassification::FULL_RASTER_TRUSTED)
        return reject("raw-current-picture-conflict");
    result.retention=EvaluateActivePicturePresentationRetentionExcludingRelocatedOverlay(
        source,b,raw,erased,0);
    result.samples+=result.retention.lumaSamples;
    if (!result.retention.relocatedOverlayExclusionEvaluated ||
        !result.retention.excludedBandsPixelSafe)
        return reject("remaining-bands-not-pixel-safe");
    // Sparse grids cannot certify a one-row line or small scene feature was
    // absent. Audit native excluded pixels, with a hard bounded work budget.
    const bool planar=source.format==AnalysisLumaFormat::P010 || source.format==AnalysisLumaFormat::P210;
    constexpr size_t PlanarSampleBudget=4*1024*1024, GenericSampleBudget=256*1024;
    const size_t bandPixels=size_t(source.width)*size_t(b.top+source.height-b.bottom);
    if (bandPixels>(planar?PlanarSampleBudget:GenericSampleBudget)) return reject("native-band-budget");
    auto code=[](const uint8_t* p) { return uint16_t((unsigned(p[0]) | (unsigned(p[1])<<8))>>6); };
#if defined(_M_X64) || defined(__x86_64__)
    // SSE2 is part of the x64 architecture. Each block covers exactly eight
    // luma pixels and their four interleaved chroma pairs; no subsampling of proof.
    const __m128i lumaLimit=_mm_set1_epi16(static_cast<short>(raw.presentationBlackThreshold));
    const __m128i chromaLow=_mm_set1_epi16(480),chromaHigh=_mm_set1_epi16(544);
#endif
    for (int y=0;y<source.height;++y) {
        if (y>=b.top && y<b.bottom) continue;
        // IsValid above already bounds both pitched planes. Unaligned loads
        // preserve support for arbitrary row pitches and byte alignment.
        const auto* luma=planar ? source.data+size_t(y)*source.rowBytes : nullptr;
        const auto* chroma=planar ? source.data+size_t(source.height)*source.rowBytes+
            size_t(source.format==AnalysisLumaFormat::P210?y:y/2)*source.chromaRowBytes : nullptr;
        auto safeScalar=[&](int x) {
            AnalysisLumaSample pixel;
            ++result.samples;
            if (planar) {
                pixel.luma=code(luma+size_t(x)*2);
                pixel.chromaU=code(chroma+size_t(x/2)*4);
                pixel.chromaV=code(chroma+size_t(x/2)*4+2);
            } else if (!source.Sample(x,y,pixel)) return false;
            return pixel.luma<=raw.presentationBlackThreshold &&
                std::abs(int(pixel.chromaU)-512)<=32 && std::abs(int(pixel.chromaV)-512)<=32;
        };
        auto safeSpan=[&](int first,int last) {
            int x=first;
#if defined(_M_X64) || defined(__x86_64__)
            if (planar) {
                // Chroma groups start at even source x. An odd segment start
                // owns only one pixel of its pair and must be inspected scalar.
                if ((x&1) && x<last) { if (!safeScalar(x)) return false; ++x; }
                for (;x+8<=last;x+=8) {
                    const __m128i ys=_mm_srli_epi16(_mm_loadu_si128(
                        reinterpret_cast<const __m128i*>(luma+size_t(x)*2)),6);
                    const __m128i uv=_mm_srli_epi16(_mm_loadu_si128(
                        reinterpret_cast<const __m128i*>(chroma+size_t(x)*2)),6);
                    const __m128i bad=_mm_or_si128(_mm_cmpgt_epi16(ys,lumaLimit),
                        _mm_or_si128(_mm_cmpgt_epi16(chromaLow,uv),_mm_cmpgt_epi16(uv,chromaHigh)));
                    result.samples+=8;
                    if (_mm_movemask_epi8(bad)) return false;
                }
            }
#endif
            for (;x<last;++x) if (!safeScalar(x)) return false;
            return true;
        };
        // Split only at the actual certified erasure, never round its edges to
        // SIMD width. A one-pixel residual on either side must still veto cropping.
        const bool erasedRow=y>=erased.top && y<erased.bottom;
        if (erasedRow) {
            if (!safeSpan(0,erased.left) || !safeSpan(erased.right,source.width))
                return reject("native-content-outside-erasure");
        } else if (!safeSpan(0,source.width)) return reject("native-content-outside-erasure");
    }
    result.accepted=true;
    result.bounds=b;
    result.reason="current-composition-preserves-established-picture";
    return result;
}
}
