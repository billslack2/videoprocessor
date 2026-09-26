#include <pch.h>
#include "RememberedEdgeReturn.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

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

RememberedEdgeReturnResult InspectRememberedEdgeReturn(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination,
    uint64_t sourceSequence, uint64_t tickMs)
{
    RememberedEdgeReturnResult result;
    result.evidence = raw;
    // REMEMBERED EDGE RETURN RED SEAM
    const auto& target = nomination.rememberedBounds;
    const auto& base = nomination.establishedBase;
    const auto& axes = raw.axisEvidence;
    const bool top = nomination.matchedEdge == RememberedEdge::TOP;
    if (!nomination.available || !nomination.historyId || nomination.confirmedSceneCount < 2 ||
        (nomination.matchedEdge != RememberedEdge::TOP && nomination.matchedEdge != RememberedEdge::BOTTOM) ||
        !source.IsValid() || source.width < 320 || source.height < 180 || !source.generation ||
        nomination.sourceGeneration != source.generation || !sourceSequence ||
        nomination.sourceSequence != sourceSequence || nomination.timestampMs != tickMs ||
        !raw.available || raw.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        raw.classification != ActivePictureClassification::PROVISIONAL ||
        !ValidVertical(raw.proposedBounds, source) || !ValidVertical(target, source) ||
        !ValidVertical(base, source) || target.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        target.top <= base.top || target.bottom >= base.bottom ||
        target.top < 2*kAdjacentRows || source.height-target.bottom < 2*kAdjacentRows ||
        (static_cast<double>(base.bottom-base.top)/(target.bottom-target.top)-1.0)*100.0 <=
            ActivePictureTransitionModel::STABLE_ASPECT_DEADBAND_PERCENT ||
        !axes.horizontal.scanComplete || axes.horizontal.barCandidate || axes.horizontal.FailedBar() ||
        axes.horizontal.state == ActivePictureAxisState::TRUSTED_BARS ||
        !axes.vertical.scanComplete || axes.vertical.reason != ActivePictureAxisReason::BAR_ASYMMETRY ||
        !axes.vertical.FailedBar() ||
        !(top ? raw.top.trusted : raw.bottom.trusted) ||
        nomination.observedEdgeCoordinate != (top ? target.top : target.bottom) ||
        (top ? raw.proposedBounds.top != target.top || raw.proposedBounds.bottom >= target.bottom
             : raw.proposedBounds.bottom != target.bottom || raw.proposedBounds.top <= target.top))
        return result;

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
    int zones[4] = {};
    const int insideY = top ? target.top+2 : target.bottom-3;
    const int requiredDelta = barDepth < source.height/20 ? 18 : 10;
    for (int i=0; i<kColumns; ++i)
    {
        AnalysisLumaSample value;
        if (!sample(xAt(i),insideY,value)) return result;
        if (static_cast<int>(value.luma)-yMedian >= requiredDelta)
        { ++result.matchedEdgeSupport; ++zones[i/(kColumns/4)]; }
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
        const int first=upper ? 0 : target.bottom;
        const int last=upper ? target.top : source.height;
        std::vector<int> rows;
        for (int d=0;d<kAdjacentRows;++d) {
            rows.push_back(upper ? last-1-d : first+d);
            rows.push_back(upper ? first+d : last-1-d);
        }
        // Use the existing detector's dense row cadence here. A short caption
        // can fall between a fixed-depth grid's rows during an uncertain return.
        const int rowStep = std::max(1, source.height/540);
        for (int y=first; y<last; y+=rowStep) rows.push_back(y);
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
                }
            }
        }
    }
    result.reason = "excluded-band-profile-mismatch";
    if (result.mismatches) return result;

    const auto retention = EvaluateActivePicturePresentationRetention(source,target);
    result.samples += retention.lumaSamples + retention.chromaSamples;
    result.reason = "current-retention-veto";
    if (!retention.analysisValid || !retention.presentationValid ||
        !retention.currentlyPixelSafe || retention.outwardVisibleBoundsAvailable)
        return result;

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
