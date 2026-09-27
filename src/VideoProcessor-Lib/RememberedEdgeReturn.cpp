#include <pch.h>
#include "RememberedEdgeReturn.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>
#include <cstring>

RememberedEdgeReturnMode ResolveRememberedEdgeReturnMode(const char* option)
{
    if (!option || !*option) return RememberedEdgeReturnMode::GUARDED;
    if (std::strcmp(option, "guarded") == 0) return RememberedEdgeReturnMode::GUARDED;
    if (std::strcmp(option, "experimental") == 0) return RememberedEdgeReturnMode::EXPERIMENTAL;
    if (std::strcmp(option, "shadow") == 0) return RememberedEdgeReturnMode::SHADOW;
    return RememberedEdgeReturnMode::OFF;
}

namespace {
constexpr int kColumns = 96;
constexpr int kAdjacentRows = 8;
constexpr int kMatchedEdgeChromaTolerance = 12; // Only one decoded chroma row at the proven edge.
constexpr int kProfileTolerance = 4; // Normalized 10-bit codes, not the generic black cutoff.
int Quantile(std::vector<int> values, double fraction)
{
    const size_t index = static_cast<size_t>((values.size()-1)*fraction);
    std::nth_element(values.begin(), values.begin()+index, values.end());
    return values[index];
}
bool ValidVertical(const ActivePictureBounds& b, const AnalysisLumaSource& s)
{
    return b.rasterWidth == s.width && b.rasterHeight == s.height &&
        b.left == 0 && b.right == s.width && b.top >= 0 && b.top < b.bottom &&
        b.bottom <= s.height;
}
}

static RememberedEdgeReturnResult InspectRememberedEdgeReturnImpl(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination,
    uint64_t sourceSequence, uint64_t tickMs, RememberedEdgeReturnShadowResult* shadow,
    bool retainBoundary = false)
{
    RememberedEdgeReturnResult result;
    result.evidence = raw;
    const auto& target = nomination.rememberedBounds;
    const auto& base = nomination.establishedBase;
    const auto& axes = raw.axisEvidence;
    const bool top = nomination.matchedEdge == RememberedEdge::TOP;
    const auto& observed = raw.proposedBounds;
    const bool validPartial = observed.rasterWidth == source.width && observed.rasterHeight == source.height &&
        observed.left >= 0 && observed.left < observed.right && observed.right <= source.width &&
        observed.top >= 0 && observed.top < observed.bottom && observed.bottom <= source.height;
    const bool cleanHorizontal = axes.horizontal.scanComplete && !axes.horizontal.barCandidate &&
        !axes.horizontal.FailedBar() && axes.horizontal.state != ActivePictureAxisState::TRUSTED_BARS;
    const bool partialHorizontal = shadow && axes.horizontal.scanComplete && axes.horizontal.FailedBar() &&
        axes.horizontal.reason == ActivePictureAxisReason::BAR_EDGE_REJECTED;

    if ((shadow ? !nomination.shadowOnly : nomination.shadowOnly) ||
        !nomination.available || !nomination.historyId || nomination.confirmedSceneCount < 2 ||
        (nomination.matchedEdge != RememberedEdge::TOP && nomination.matchedEdge != RememberedEdge::BOTTOM) ||
        !source.IsValid() || source.width < 320 || source.height < 180 || !source.generation ||
        nomination.sourceGeneration != source.generation || !sourceSequence ||
        nomination.sourceSequence != sourceSequence || nomination.timestampMs != tickMs ||
        !raw.available || raw.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        raw.classification != ActivePictureClassification::PROVISIONAL ||
        !(shadow ? validPartial : ValidVertical(raw.proposedBounds, source)) || !ValidVertical(target, source) ||
        !ValidVertical(base, source) || target.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        target.top <= base.top || target.bottom >= base.bottom ||
        target.top < 2*kAdjacentRows || source.height-target.bottom < 2*kAdjacentRows ||
        (static_cast<double>(base.bottom-base.top)/(target.bottom-target.top)-1.0)*100.0 <=
            ActivePictureTransitionModel::STABLE_ASPECT_DEADBAND_PERCENT ||
        (!cleanHorizontal && !partialHorizontal) ||
        !axes.vertical.scanComplete ||
        (axes.vertical.reason != ActivePictureAxisReason::BAR_ASYMMETRY &&
            !(shadow && axes.vertical.reason == ActivePictureAxisReason::BAR_EDGE_REJECTED)) ||
        !axes.vertical.FailedBar() ||
        !(top ? raw.top.trusted : raw.bottom.trusted) ||
        nomination.observedEdgeCoordinate != (top ? target.top : target.bottom) ||
        (top ? raw.proposedBounds.top != target.top ||
                (shadow ? raw.proposedBounds.bottom > target.bottom : raw.proposedBounds.bottom >= target.bottom)
             : raw.proposedBounds.bottom != target.bottom ||
                (shadow ? raw.proposedBounds.top < target.top : raw.proposedBounds.top <= target.top)))
        return result;

    // The learned target and all its current witnesses remain unchanged. The
    // comparison retains uncertain source rows rather than reclassifying them
    // as black. Even coordinates retain two rows, odd coordinates retain three.
    auto guarded = target;
    guarded.top = ((target.top-2)/2)*2;
    guarded.bottom = ((target.bottom+3)/2)*2;
    guarded.aspectRatio = static_cast<double>(guarded.right-guarded.left)/(guarded.bottom-guarded.top);
    const auto& presentation = retainBoundary ? guarded : target;
    if (retainBoundary && (!shadow || !ValidVertical(guarded, source) ||
        guarded.top <= base.top || guarded.bottom >= base.bottom ||
        target.top-guarded.top < 2 || target.top-guarded.top > 3 ||
        guarded.bottom-target.bottom < 2 || guarded.bottom-target.bottom > 3))
    {
        result.reason = "retained-envelope-outside-established-base";
        return result;
    }
    if (shadow)
    {
        shadow->presentation = presentation;
        shadow->retainedTopRows = target.top-presentation.top;
        shadow->retainedBottomRows = presentation.bottom-target.bottom;
    }

    auto sample = [&](int x, int y, AnalysisLumaSample& value) {
        if (!source.Sample(x,y,value)) return false;
        ++result.samples;
        return true;
    };
    auto xAt = [&](int i) { return i*(source.width-1)/(kColumns-1); };
    const int barDepth = top ? target.top : source.height-target.bottom;
    std::vector<int> ys, us, vs;
    ys.reserve(8*kColumns); us.reserve(8*kColumns); vs.reserve(8*kColumns);
    result.reason = "reference-sampling-failed";
    // Only the currently independently trusted bar defines the reference.
    // Leave its inner eight rows out; those rows are checked as exterior later.
    for (int d=0; d<8; ++d)
    {
        const int depth = (2*d+1)*(barDepth-kAdjacentRows)/16;
        const int y = top ? depth : source.height-1-depth;
        for (int i=0; i<kColumns; ++i)
        {
            AnalysisLumaSample value;
            if (!sample(xAt(i),y,value)) return result;
            ys.push_back(value.luma); us.push_back(value.chromaU); vs.push_back(value.chromaV);
        }
    }
    const int yMedian=Quantile(ys,.5), uMedian=Quantile(us,.5), vMedian=Quantile(vs,.5);
    result.referenceY=yMedian; result.referenceU=uMedian; result.referenceV=vMedian;
    result.referenceDispersion=Quantile(ys,.9)-Quantile(ys,.1);
    result.reason = "reference-not-clean";
    if (yMedian > 80 || result.referenceDispersion > 4 ||
        Quantile(us,.9)-Quantile(us,.1) > 4 || Quantile(vs,.9)-Quantile(vs,.1) > 4 ||
        std::abs(uMedian-512)>8 || std::abs(vMedian-512)>8)
        return result;
    auto matches = [&](int y, int u, int v) {
        return std::abs(y-yMedian)<=kProfileTolerance &&
            std::abs(u-uMedian)<=kProfileTolerance && std::abs(v-vMedian)<=kProfileTolerance;
    };
    for (size_t i=0; i<ys.size(); ++i)
        if (!matches(ys[i],us[i],vs[i])) return result;

    // Recheck distributed positive evidence at the matched edge, without asking
    // the missing edge to have positive contrast. All crop coordinates stay fixed.
    result.reason = "matched-edge-sampling-failed";
    auto& zones = result.matchedEdgeZones;
    const int insideY = top ? target.top+2 : target.bottom-3;
    const int requiredDelta = barDepth < source.height/20 ? 18 : 10;
    for (int i=0; i<kColumns; ++i)
    {
        AnalysisLumaSample value;
        if (!sample(xAt(i),insideY,value)) return result;
        if (static_cast<int>(value.luma)-yMedian >= requiredDelta)
        { ++result.matchedEdgeSupport; ++zones[i/(kColumns/4)]; }
    }
    if (shadow)
    {
        // Diagnostic only: retain localized contrast at the exact opposite edge,
        // even when the current broad edge-support test would reject this frame.
        // Count each column once, using the same luma/profile deltas as above.
        result.reason = "opposite-edge-sampling-failed";
        for (int i=0; i<kColumns; ++i)
        {
            bool luma = false, chroma = false;
            for (int depth : {0, 2, 6})
            {
                const int y = top ? target.bottom-1-depth : target.top+depth;
                AnalysisLumaSample value;
                if (!sample(xAt(i), y, value)) return result;
                const int dy = static_cast<int>(value.luma)-yMedian;
                const int duv = std::max(std::abs(static_cast<int>(value.chromaU)-uMedian),
                    std::abs(static_cast<int>(value.chromaV)-vMedian));
                shadow->oppositeMaxLumaDelta = std::max(shadow->oppositeMaxLumaDelta, std::abs(dy));
                shadow->oppositeMaxChromaDelta = std::max(shadow->oppositeMaxChromaDelta, duv);
                luma = luma || dy >= requiredDelta;
                chroma = chroma || duv > kProfileTolerance;
            }
            if (luma) { ++shadow->oppositeLumaSupport; ++shadow->oppositeLumaZones[i/(kColumns/4)]; }
            if (chroma) { ++shadow->oppositeChromaSupport; ++shadow->oppositeChromaZones[i/(kColumns/4)]; }
        }
    }
    result.reason = "matched-edge-not-distributed";
    if (result.matchedEdgeSupport < kColumns/2 ||
        std::any_of(std::begin(zones),std::end(zones),[](int n){return n<6;})) return result;

    result.reason = "excluded-band-sampling-failed";
    // Inspect BOTH full prospective bars, with every one of the first eight
    // excluded rows at each picture boundary. This catches one/two-row content
    // between the old +/-2/3 diagnostic witnesses. The deeper grid is bounded.
    for (bool upper : {true,false})
    {
        const int first=upper ? 0 : presentation.bottom;
        const int last=upper ? presentation.top : source.height;
        std::vector<int> rows;
        for (int d=0;d<kAdjacentRows;++d) {
            rows.push_back(upper ? last-1-d : first+d);
            rows.push_back(upper ? first+d : last-1-d);
        }
        // Use the existing detector's dense row cadence here. A short caption
        // can fall between a fixed-depth grid's rows during an uncertain return.
        const int rowStep = std::max(1, source.height/540);
        // Padding must not move the grid phase and lose previously inspected
        // pixels farther into a bar. Keep nominal deep rows that are still
        // discarded, plus the new boundary's contiguous rows collected above.
        const int nominalFirst = upper ? 0 : target.bottom;
        for (int y=nominalFirst; y<last; y+=rowStep)
            if (y>=first) rows.push_back(y);
        std::sort(rows.begin(),rows.end());
        rows.erase(std::unique(rows.begin(),rows.end()),rows.end());
        for (int y:rows) for (int i=0;i<kColumns;++i)
        {
            AnalysisLumaSample value;
            if (!sample(xAt(i),y,value)) return result;
            ++result.exteriorSamples;
            result.maxLumaDelta = std::max(result.maxLumaDelta, std::abs(static_cast<int>(value.luma)-yMedian));
            result.maxChromaDelta = std::max(result.maxChromaDelta, std::max(
                std::abs(static_cast<int>(value.chromaU)-uMedian), std::abs(static_cast<int>(value.chromaV)-vMedian)));
            if (!matches(value.luma,value.chromaU,value.chromaV)) {
                // Qualified history fixes the rectangle; this allowance cannot invent an edge.
                // Only the independently matched edge can have a small chroma fringe. P010
                // shares chroma across a pair of luma rows, including at odd edge positions.
                // Keep luma strict and never extend the allowance into the uncertain bar.
                const int adjacentExcludedY = top ? target.top-1 : target.bottom;
                const bool sameChromaRow = source.format == AnalysisLumaFormat::P010 ?
                    y/2 == adjacentExcludedY/2 : y == adjacentExcludedY;
                const bool edgeFringe = upper == top && sameChromaRow &&
                    std::abs(static_cast<int>(value.luma)-yMedian) <= kProfileTolerance &&
                    std::abs(static_cast<int>(value.chromaU)-uMedian) <= kMatchedEdgeChromaTolerance &&
                    std::abs(static_cast<int>(value.chromaV)-vMedian) <= kMatchedEdgeChromaTolerance;
                if (edgeFringe) {
                    if (!result.edgeFringeSamples) result.firstFringeY=y;
                    result.lastFringeY=y;
                    ++result.edgeFringeSamples;
                } else {
                    if (!result.mismatches) { result.firstMismatchX=xAt(i); result.firstMismatchY=y; }
                    result.lastMismatchY=y;
                    ++result.mismatches;
                    // Classify relative to the same nominal uncertainty strip
                    // even in the exact comparison. These counters grant no veto
                    // exception; a guarded comparison still rejects every mismatch
                    // in its discarded region, including the new adjacent row.
                    const bool withinRetainedStrip = upper ? y >= guarded.top : y < guarded.bottom;
                    if (withinRetainedStrip) ++result.boundaryMismatchSamples;
                    else ++result.deepMismatchSamples;
                }
            }
        }
    }
    result.reason = "excluded-band-profile-mismatch";
    if (result.mismatches) return result;

    const auto retention = EvaluateActivePicturePresentationRetention(source,presentation);
    result.samples += retention.lumaSamples + retention.chromaSamples;
    result.reason = "current-retention-veto";
    if (!retention.analysisValid || !retention.presentationValid ||
        !retention.currentlyPixelSafe || retention.outwardVisibleBoundsAvailable)
        return result;

    if (shadow)
    {
        shadow->wouldVerify = true;
        result.reason = retainBoundary ? "shadow-retained-boundary-profile-verified" :
            "shadow-current-profile-verified-known-return";
        return result; // Never construct authority-bearing evidence for diagnostics.
    }
    result.candidateAvailable = true;
    result.evidence.trustedBounds = target;
    result.evidence.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
    result.evidence.authorityOrigin = ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
    result.evidence.rememberedEdgeReturnProof.available = true;
    result.evidence.rememberedEdgeReturnProof.nomination = nomination;
    result.evidence.rememberedEdgeReturnProof.sourceSequence = sourceSequence;
    result.evidence.rememberedEdgeReturnProof.timestampMs = tickMs;
    result.evidence.reason = "qualified remembered edge return with current bar-profile proof";
    result.reason = "current-profile-verified-known-return";
    return result;
}

RememberedEdgeReturnResult InspectRememberedEdgeReturn(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination, uint64_t sourceSequence, uint64_t tickMs)
{
    if (nomination.guarded) return {};
    return InspectRememberedEdgeReturnImpl(source, raw, nomination, sourceSequence, tickMs, nullptr);
}
static RememberedEdgeReturnShadowResult InspectRememberedEdgeReturnShadowImpl(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination, uint64_t sourceSequence, uint64_t tickMs,
    bool retainBoundary)
{
    RememberedEdgeReturnShadowResult result;
    result.sourceGeneration = source.generation;
    result.sourceSequence = sourceSequence;
    result.timestampMs = tickMs;
    result.target = nomination.rememberedBounds;
    result.matchedEdge = nomination.matchedEdge;
    const auto inspected = InspectRememberedEdgeReturnImpl(source, raw, nomination, sourceSequence, tickMs, &result, retainBoundary);
    static_cast<RememberedEdgeReturnMetrics&>(result) = inspected;
    return result;
}

static RememberedEdgeReturnShadowWindowResult InspectRememberedEdgeReturnShadowWindowImpl(
    const RememberedEdgeReturnNomination& nomination,
    const RememberedEdgeReturnShadowSample* samples, size_t sampleCount,
    uint32_t configuredFuture, uint32_t actualFuture, bool retainBoundary)
{
    RememberedEdgeReturnShadowWindowResult result;
    result.effectiveFuture = (std::min)((std::min)(configuredFuture, actualFuture),
        uint32_t{ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES});
    result.expectedFrames = size_t{result.effectiveFuture} + 1;
    if (samples && sampleCount) result.firstIdentity = samples[0].identity;
    if (!result.effectiveFuture)
    {
        result.reason = "insufficient-future-frames";
        return result;
    }
    if (!samples || sampleCount < result.expectedFrames)
    {
        result.reason = "unavailable-window";
        return result;
    }
    result.lastIdentity = samples[result.expectedFrames-1].identity;
    const auto reject = [&](size_t index, const char* reason) {
        result.failedIndex = static_cast<int>(index);
        result.failureIdentity = samples[index].identity;
        result.reason = reason;
    };
    const auto& first = samples[0];
    const auto& firstId = first.identity;
    if (!nomination.available || !nomination.shadowOnly || !nomination.historyId ||
        !nomination.historyRevision || nomination.confirmedSceneCount < 2 ||
        !nomination.sourceGeneration || !nomination.sourceSequence || !nomination.timestampMs ||
        !nomination.lastIndependentNativeSequence ||
        nomination.lastIndependentNativeSequence >= nomination.sourceSequence ||
        !nomination.lastIndependentNativeTickMs ||
        nomination.timestampMs < nomination.lastIndependentNativeTickMs ||
        nomination.timestampMs-nomination.lastIndependentNativeTickMs >
            ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_AGE_MS)
    {
        reject(0, "ineligible-fixed-nomination");
        return result;
    }
    if (firstId.acceptedSequence != nomination.sourceSequence || first.timestampMs != nomination.timestampMs)
    {
        reject(0, "nomination-current-frame-mismatch");
        return result;
    }
    if (firstId.acceptedSequence > UINT64_MAX-result.effectiveFuture ||
        firstId.sourceFrameNumber > UINT64_MAX-result.effectiveFuture)
    {
        reject(0, "source-sequence-overflow");
        return result;
    }
    // Validate the entire selected window before accessing any pixels. An absent
    // or unrelated later frame cannot be skipped, even if earlier pixels pass.
    for (size_t i=0; i<result.expectedFrames; ++i)
    {
        const auto& sample = samples[i];
        const auto& identity = sample.identity;
        if (!sample.available || !sample.source.IsValid() || !sample.raw.available)
        {
            reject(i, "unavailable-frame");
            return result;
        }
        if (sample.cadenceRepeat || sample.discontinuity)
        {
            reject(i, "repeat-or-discontinuity");
            return result;
        }
        if (identity.transportGeneration != nomination.sourceGeneration ||
            !identity.sourceFormatGeneration || identity.sourceFormatGeneration != nomination.sourceFormatGeneration ||
            identity.rendererGeneration != nomination.rendererGeneration ||
            identity.viewportGeneration != nomination.viewportGeneration ||
            sample.policyGeneration != nomination.policyGeneration)
        {
            reject(i, "frame-context-mismatch");
            return result;
        }
        if (identity.acceptedSequence != firstId.acceptedSequence+i ||
            identity.sourceFrameNumber != firstId.sourceFrameNumber+i)
        {
            reject(i, "noncontiguous-source-sequence");
            return result;
        }
        if (!identity.captureTimestamp ||
            (i && identity.captureTimestamp <= samples[i-1].identity.captureTimestamp) ||
            sample.timestampMs < first.timestampMs ||
            (i && sample.timestampMs < samples[i-1].timestampMs))
        {
            reject(i, "nonmonotonic-frame-time");
            return result;
        }
        const bool transport = sample.sourceGenerationDomain == RememberedShadowSourceGeneration::TRANSPORT;
        const bool format = sample.sourceGenerationDomain == RememberedShadowSourceGeneration::FORMAT;
        if ((!transport && !format) || sample.source.generation !=
            (transport ? identity.transportGeneration : identity.sourceFormatGeneration))
        {
            reject(i, "source-generation-domain-mismatch");
            return result;
        }
        if (sample.source.width != nomination.rememberedBounds.rasterWidth ||
            sample.source.height != nomination.rememberedBounds.rasterHeight ||
            sample.timestampMs-nomination.lastIndependentNativeTickMs >
                ActivePictureTransitionModel::REMEMBERED_RETURN_MAX_AGE_MS)
        {
            reject(i, "source-raster-or-history-mismatch");
            return result;
        }
    }
    for (size_t i=0; i<result.expectedFrames; ++i)
    {
        const auto& sample = samples[i];
        // Qualified history and the exact target remain fixed. Only this frame's
        // sequence/time are rebound; no observed rectangle is learned or merged.
        auto currentNomination = nomination;
        currentNomination.sourceSequence = sample.identity.acceptedSequence;
        currentNomination.timestampMs = sample.timestampMs;
        auto source = sample.source;
        source.generation = sample.identity.transportGeneration;
        const auto inspection = InspectRememberedEdgeReturnShadowImpl(source, sample.raw,
            currentNomination, sample.identity.acceptedSequence, sample.timestampMs, retainBoundary);
        ++result.inspectedFrames;
        result.totalSamples += inspection.samples;
        if (i == 0)
        {
            result.target = inspection.target;
            result.presentation = inspection.presentation;
            result.retainedTopRows = inspection.retainedTopRows;
            result.retainedBottomRows = inspection.retainedBottomRows;
        }
        if (retainBoundary && inspection.wouldVerify &&
            (inspection.presentation.left != result.presentation.left ||
             inspection.presentation.top != result.presentation.top ||
             inspection.presentation.right != result.presentation.right ||
             inspection.presentation.bottom != result.presentation.bottom ||
             inspection.presentation.rasterWidth != result.presentation.rasterWidth ||
             inspection.presentation.rasterHeight != result.presentation.rasterHeight ||
             inspection.presentation.trustedBarAxes != result.presentation.trustedBarAxes ||
             inspection.retainedTopRows != result.retainedTopRows ||
             inspection.retainedBottomRows != result.retainedBottomRows))
        {
            reject(i, "retained-envelope-changed-within-window");
            result.failureInspection = inspection;
            return result;
        }
        if (!inspection.wouldVerify)
        {
            reject(i, "current-pixel-verification-failed");
            result.failureInspection = inspection;
            return result;
        }
        ++result.passedFrames;
    }
    result.allPass = true;
    result.reason = retainBoundary ? "consecutive-retained-boundary-shadow-window-verified" :
        "consecutive-shadow-window-verified";
    return result;
}

RememberedEdgeReturnShadowResult InspectRememberedEdgeReturnGuardedShadow(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination, uint64_t sourceSequence, uint64_t tickMs)
{
    return InspectRememberedEdgeReturnShadowImpl(source, raw, nomination, sourceSequence, tickMs, true);
}
RememberedEdgeReturnShadowWindowResult InspectRememberedEdgeReturnGuardedShadowWindow(
    const RememberedEdgeReturnNomination& nomination,
    const RememberedEdgeReturnShadowSample* samples, size_t sampleCount,
    uint32_t configuredFuture, uint32_t actualFuture)
{
    return InspectRememberedEdgeReturnShadowWindowImpl(nomination, samples, sampleCount, configuredFuture, actualFuture, true);
}

RememberedEdgeReturnShadowResult InspectRememberedEdgeReturnShadow(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination, uint64_t sourceSequence, uint64_t tickMs)
{
    return InspectRememberedEdgeReturnShadowImpl(source, raw, nomination, sourceSequence, tickMs, false);
}
RememberedEdgeReturnShadowWindowResult InspectRememberedEdgeReturnShadowWindow(
    const RememberedEdgeReturnNomination& nomination,
    const RememberedEdgeReturnShadowSample* samples, size_t sampleCount,
    uint32_t configuredFuture, uint32_t actualFuture)
{
    return InspectRememberedEdgeReturnShadowWindowImpl(nomination, samples, sampleCount, configuredFuture, actualFuture, false);
}

namespace {
bool SameGuardedBounds(const ActivePictureBounds& a, const ActivePictureBounds& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom &&
        a.rasterWidth == b.rasterWidth && a.rasterHeight == b.rasterHeight && a.trustedBarAxes == b.trustedBarAxes;
}
bool SameGuardedNomination(const RememberedEdgeReturnNomination& a, const RememberedEdgeReturnNomination& b)
{
    return a.available && b.available && a.guarded && b.guarded && !a.shadowOnly && !b.shadowOnly &&
        SameGuardedBounds(a.establishedBase,b.establishedBase) && SameGuardedBounds(a.rememberedBounds,b.rememberedBounds) &&
        a.historyId == b.historyId && a.historyRevision == b.historyRevision && a.sourceGeneration == b.sourceGeneration &&
        a.confirmedSceneCount == b.confirmedSceneCount && a.lastIndependentNativeSequence == b.lastIndependentNativeSequence &&
        a.lastIndependentNativeTickMs == b.lastIndependentNativeTickMs && a.matchedEdge == b.matchedEdge &&
        a.observedEdgeCoordinate == b.observedEdgeCoordinate && a.sceneId == b.sceneId &&
        a.rendererGeneration == b.rendererGeneration && a.viewportGeneration == b.viewportGeneration &&
        a.sourceFormatGeneration == b.sourceFormatGeneration && a.policyGeneration == b.policyGeneration;
}
ActivePictureEvidence GuardedEvidence(const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination, const ActivePictureBounds& presentation)
{
    auto evidence = raw;
    evidence.trustedBounds = presentation;
    evidence.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
    evidence.authorityOrigin = ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
    auto& proof = evidence.rememberedEdgeReturnProof;
    proof.available = proof.guarded = true;
    proof.nomination = nomination;
    proof.presentationBounds = presentation;
    proof.sourceSequence = nomination.sourceSequence;
    proof.timestampMs = nomination.timestampMs;
    evidence.reason = "qualified remembered edge return with retained boundary profile proof";
    return evidence;
}
}
RememberedEdgeReturnResult InspectGuardedRememberedEdgeReturn(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination, uint64_t sourceSequence, uint64_t tickMs)
{
    RememberedEdgeReturnResult result;
    result.evidence = raw;
    if (!nomination.guarded || nomination.shadowOnly || raw.sparseTransitionProof.available ||
        raw.rememberedEdgeReturnProof.available) return result;
    // Share the strict verifier; its diagnostic result is converted to authority
    // only by this explicitly opted-in API, preserving the original nomination.
    auto diagnosticNomination = nomination;
    diagnosticNomination.shadowOnly = true;
    const auto inspection = InspectRememberedEdgeReturnGuardedShadow(source,raw,diagnosticNomination,sourceSequence,tickMs);
    static_cast<RememberedEdgeReturnMetrics&>(result) = inspection;
    if (!inspection.wouldVerify) return result;
    result.candidateAvailable = true;
    result.evidence = GuardedEvidence(raw,nomination,inspection.presentation);
    result.reason = "guarded-current-profile-verified-known-return";
    return result;
}
GuardedRememberedEdgeReturnCertificate BuildGuardedRememberedEdgeReturnCertificate(
    const ActivePictureTransitionModel& model, const RememberedEdgeReturnNomination& nomination,
    const RememberedEdgeReturnShadowSample* samples, size_t sampleCount,
    uint32_t configuredFuture, uint32_t actualFuture, uint64_t continuityGeneration)
{
    GuardedRememberedEdgeReturnCertificate result;
    if (!nomination.available || !nomination.guarded || nomination.shadowOnly || !continuityGeneration)
        return result;
    auto diagnosticNomination = nomination;
    diagnosticNomination.shadowOnly = true;
    result.diagnostic = InspectRememberedEdgeReturnGuardedShadowWindow(diagnosticNomination,
        samples,sampleCount,configuredFuture,actualFuture);
    result.reason = result.diagnostic.reason;
    if (!result.diagnostic.allPass || result.diagnostic.expectedFrames > result.identities.size()) return result;
    result.nomination = nomination;
    result.frameCount = result.diagnostic.expectedFrames;
    result.configuredFuture = configuredFuture;
    result.actualFuture = actualFuture;
    result.continuityGeneration = continuityGeneration;
    result.policyGeneration = nomination.policyGeneration;
    result.createdTickMs = nomination.timestampMs;
    for (size_t i=0; i<result.frameCount; ++i)
    {
        if (samples[i].raw.sparseTransitionProof.available || samples[i].raw.rememberedEdgeReturnProof.available)
        { result.reason = "raw-frame-already-inferred"; return result; }
        auto frameNomination = nomination;
        frameNomination.sourceSequence = samples[i].identity.acceptedSequence;
        frameNomination.timestampMs = samples[i].timestampMs;
        const auto evidence = GuardedEvidence(samples[i].raw,frameNomination,result.diagnostic.presentation);
        if (i == 0) result.currentEvidence = evidence;
        result.identities[i] = samples[i].identity;
        result.observations[i] = MakeActivePictureObservation(evidence,samples[i].identity.acceptedSequence,60.0);
    }
    // Revalidate qualification and normal confirmation against an untouched
    // model copy. Pixel verification alone cannot nominate or commit history.
    auto trial = model;
    if (!trial.AdoptGuardedRememberedReturn(result.observations.data(),result.frameCount,&result.decision))
    { result.reason = "live-history-or-confirmation-rejected"; return result; }
    result.available = true;
    result.reason = "guarded-buffered-certificate-verified";
    return result;
}
bool ValidateAndAdoptGuardedRememberedEdgeReturn(
    ActivePictureTransitionModel& model, const GuardedRememberedEdgeReturnCertificate& certificate,
    const AnalysisLumaSource& currentSource, const ActivePictureEvidence& currentRaw,
    const ActivePictureFrameIdentity& currentIdentity, const RememberedEdgeReturnContext& currentContext,
    uint64_t continuityGeneration, ActivePictureTransitionDecision* outDecision, ActivePictureEvidence* outEvidence)
{
    const auto& nomination = certificate.nomination;
    const size_t expected = size_t(std::min(std::min(certificate.configuredFuture,certificate.actualFuture),
        uint32_t{ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES}))+1;
    if (!certificate.available || !certificate.diagnostic.allPass ||
        certificate.frameCount < ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS ||
        certificate.frameCount > certificate.identities.size() || certificate.frameCount != expected ||
        certificate.diagnostic.expectedFrames != expected || certificate.diagnostic.passedFrames != expected ||
        !nomination.available || !nomination.guarded || nomination.shadowOnly ||
        !currentContext.enabled || !currentContext.guardedEnabled || currentContext.shadowOnly ||
        currentContext.cadenceRepeat || currentContext.discontinuity ||
        certificate.continuityGeneration != continuityGeneration || !continuityGeneration ||
        certificate.policyGeneration != currentContext.policyGeneration ||
        certificate.createdTickMs != nomination.timestampMs || currentContext.timestampMs < certificate.createdTickMs ||
        currentContext.timestampMs-certificate.createdTickMs > 500 ||
        !SameActivePictureFrameIdentity(certificate.identities[0],currentIdentity) ||
        currentIdentity.transportGeneration != nomination.sourceGeneration || currentSource.generation != nomination.sourceGeneration ||
        currentContext.sourceGeneration != nomination.sourceGeneration || currentContext.sceneId != nomination.sceneId ||
        currentContext.sourceSequence != nomination.sourceSequence || currentIdentity.acceptedSequence != nomination.sourceSequence ||
        currentContext.rendererGeneration != nomination.rendererGeneration ||
        currentContext.viewportGeneration != nomination.viewportGeneration ||
        currentContext.sourceFormatGeneration != nomination.sourceFormatGeneration ||
        currentContext.policyGeneration != nomination.policyGeneration ||
        currentIdentity.acceptedSequence > UINT64_MAX-(expected-1) ||
        currentIdentity.sourceFrameNumber > UINT64_MAX-(expected-1)) return false;
    auto observations = certificate.observations;
    for (size_t i=0; i<expected; ++i)
    {
        const auto& identity = certificate.identities[i];
        const auto& observation = certificate.observations[i];
        const auto& proof = observation.rememberedEdgeReturnProof;
        if (identity.transportGeneration != nomination.sourceGeneration ||
            identity.sourceFormatGeneration != nomination.sourceFormatGeneration ||
            identity.rendererGeneration != nomination.rendererGeneration || identity.viewportGeneration != nomination.viewportGeneration ||
            identity.acceptedSequence != currentIdentity.acceptedSequence+i ||
            identity.sourceFrameNumber != currentIdentity.sourceFrameNumber+i || !identity.captureTimestamp ||
            (i && identity.captureTimestamp <= certificate.identities[i-1].captureTimestamp) ||
            observation.frameNumber != identity.acceptedSequence || observation.transitionDeferred ||
            !proof.available || !proof.guarded || !SameGuardedNomination(proof.nomination,nomination) ||
            proof.sourceSequence != identity.acceptedSequence || proof.nomination.sourceSequence != identity.acceptedSequence ||
            proof.timestampMs != proof.nomination.timestampMs || proof.timestampMs < certificate.createdTickMs ||
            proof.timestampMs > currentContext.timestampMs ||
            !SameGuardedBounds(proof.presentationBounds,certificate.diagnostic.presentation) ||
            !SameGuardedBounds(observation.bounds,certificate.diagnostic.presentation)) return false;
        // This changes inspection time only. Captured identity timestamps and
        // independent source sequences remain exactly as originally verified.
        observations[i].rememberedEdgeReturnProof.timestampMs = currentContext.timestampMs;
        observations[i].rememberedEdgeReturnProof.nomination.timestampMs = currentContext.timestampMs;
    }
    auto currentNomination = nomination;
    currentNomination.timestampMs = currentContext.timestampMs;
    const auto current = InspectGuardedRememberedEdgeReturn(currentSource,currentRaw,currentNomination,
        currentIdentity.acceptedSequence,currentContext.timestampMs);
    if (!current.candidateAvailable ||
        !SameGuardedBounds(current.evidence.trustedBounds,certificate.currentEvidence.trustedBounds) ||
        !SameGuardedBounds(current.evidence.trustedBounds,certificate.diagnostic.presentation)) return false;
    observations[0] = MakeActivePictureObservation(current.evidence,currentIdentity.acceptedSequence,observations[0].framesPerSecond);
    auto trial = model;
    trial.SetRememberedEdgeReturnContext(currentContext);
    ActivePictureTransitionDecision decision;
    if (!trial.AdoptGuardedRememberedReturn(observations.data(),expected,&decision)) return false;
    model = trial;
    if (outDecision) *outDecision = decision;
    if (outEvidence) *outEvidence = current.evidence;
    return true;
}
