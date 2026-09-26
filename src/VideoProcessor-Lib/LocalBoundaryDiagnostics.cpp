#include "pch.h"
#include "LocalBoundaryDiagnostics.h"
#include "LocalBoundaryRefinement.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <set>
#include <vector>

namespace
{
    int Coordinate(int index, int extent, int count)
    {
        return static_cast<int>(static_cast<int64_t>(index) * (extent - 1) / (count - 1));
    }

    int ChromaDelta(const AnalysisLumaSample& sample)
    {
        return std::max(std::abs(static_cast<int>(sample.chromaU) - 512),
            std::abs(static_cast<int>(sample.chromaV) - 512));
    }

    void HashValue(uint64_t& hash, uint32_t value)
    {
        // Stable byte order; a non-cryptographic duplicate hint, not frame identity.
        for (int byte = 0; byte < 4; ++byte)
        {
            hash ^= static_cast<uint8_t>(value >> (byte * 8));
            hash *= 1099511628211ULL;
        }
    }

    LocalBoundaryDiagnosticEdge InspectRow(const ActivePictureDiagnosticGrid& grid,
        int row, int height, int threshold)
    {
        LocalBoundaryDiagnosticEdge edge;
        edge.row = row;
        edge.coordinate = Coordinate(row, height, grid.rows);
        std::bitset<128> luma, color, signal;
        for (int column = 0; column < grid.columns; ++column)
        {
            const auto& sample = grid.samples[static_cast<size_t>(row) * grid.columns + column];
            luma[column] = sample.luma > threshold;
            color[column] = ChromaDelta(sample) > 8;
            signal[column] = luma[column] || color[column];
        }
        for (int column = 0; column < grid.columns;)
        {
            if (!signal[column])
            {
                ++column;
                continue;
            }
            const int start = column;
            while (column < grid.columns && signal[column]) ++column;
            if (column - start < 3) continue;
            ++edge.connectedRuns;
            for (int position = start; position < column; ++position)
            {
                edge.supportMask[position] = true;
                edge.lumaMask[position] = luma[position];
                edge.colorMask[position] = color[position];
                ++edge.supportedSamples;
                if (luma[position] && color[position]) ++edge.lumaAndColorSamples;
                else if (luma[position]) ++edge.lumaOnlySamples;
                else ++edge.colorOnlySamples;
            }
        }
        return edge;
    }

    void InspectAlignedInterior(const ActivePictureDiagnosticGrid& grid,
        const std::vector<LocalBoundaryDiagnosticEdge>& rows, bool top,
        LocalBoundaryDiagnosticEdge& edge)
    {
        const int direction = top ? 1 : -1;
        const int exteriorRow = edge.row - direction;
        if (exteriorRow >= 0 && exteriorRow < grid.rows)
        {
            for (int column = 0; column < grid.columns; ++column)
            {
                if (!edge.supportMask[column]) continue;
                const auto& inside = grid.samples[static_cast<size_t>(edge.row) * grid.columns + column];
                const auto& outside = grid.samples[static_cast<size_t>(exteriorRow) * grid.columns + column];
                edge.maxLumaDelta = std::max(edge.maxLumaDelta,
                    static_cast<int>(inside.luma) - static_cast<int>(outside.luma));
                edge.maxChromaDelta = std::max(edge.maxChromaDelta,
                    std::max(std::abs(static_cast<int>(inside.chromaU) - static_cast<int>(outside.chromaU)),
                        std::abs(static_cast<int>(inside.chromaV) - static_cast<int>(outside.chromaV))));
            }
        }
        int total = 0;
        for (int depth = 0; depth < 3; ++depth)
        {
            const int row = edge.row + direction * (depth + 1);
            if (row < 0 || row >= grid.rows) continue;
            edge.inwardSupportedSamples[depth] = static_cast<int>(
                (edge.supportMask & rows[row].supportMask).count());
            total += edge.inwardSupportedSamples[depth];
        }
        if (edge.supportedSamples != 0)
            edge.inwardContinuity = static_cast<double>(total) / (3 * edge.supportedSamples);
    }


    bool MasksFit(const std::bitset<128>& mask, int columns)
    {
        return columns > 0 && columns <= 128 && (mask >> columns).none();
    }

    bool ValidObservationMetadata(const LocalBoundaryDiagnosticResult& result)
    {
        if (!result.available || result.width < 2 || result.height < 2 ||
            result.columns < 2 || result.columns > 128 || result.columns > result.width ||
            result.rows < 2 || result.rows > 270 || result.rows > result.height ||
            result.sampleCount != static_cast<size_t>(result.columns) * result.rows ||
            result.sampleCount > LOCAL_BOUNDARY_MAX_SAMPLES ||
            result.rowStep != (static_cast<int64_t>(result.height) + result.rows - 3) / (result.rows - 1))
            return false;
        if (!result.candidateAvailable) return true;
        if (result.topEdge.row < 0 || result.bottomEdge.row < result.topEdge.row ||
            result.bottomEdge.row >= result.rows ||
            result.top != Coordinate(result.topEdge.row, result.height, result.rows) ||
            result.bottom != Coordinate(result.bottomEdge.row, result.height, result.rows) + 1 ||
            result.topEdge.coordinate != result.top || result.bottomEdge.coordinate != result.bottom - 1 ||
            result.blackFloor < 0 || result.blackFloor > 80 ||
            result.blackThreshold != std::min(104, result.blackFloor + 24) ||
            result.topExcludedSamples != static_cast<size_t>(result.topEdge.row) * result.columns ||
            result.bottomExcludedSamples != static_cast<size_t>(result.rows - result.bottomEdge.row - 1) * result.columns ||
            result.excludedSamples != result.topExcludedSamples + result.bottomExcludedSamples ||
            result.excludedViolations != result.topExcludedViolations + result.bottomExcludedViolations)
            return false;
        for (const auto* edge : { &result.topEdge, &result.bottomEdge })
            if (!MasksFit(edge->supportMask, result.columns) ||
                edge->supportedSamples != static_cast<int>(edge->supportMask.count()) ||
                !std::isfinite(edge->inwardContinuity) || edge->inwardContinuity < 0.0 ||
                edge->inwardContinuity > 1.0 ||
                !MasksFit(edge->refinedMask, result.columns) ||
                (edge->refinedMask.any() &&
                    (edge->refinedMask != edge->supportMask || edge->refinementSamples == 0 ||
                     edge->refinedMinimumRun < std::max(4, ((result.width - 1) / (result.columns - 1)) / 4))) ||
                (edge->refinedMask.none() && (edge->refinementSamples != 0 || edge->refinedMinimumRun != 0)))
                return false;
        return true;
    }

    const char* BasicHypothesisFailure(const LocalBoundaryDiagnosticResult& result)
    {
        if (!ValidObservationMetadata(result)) return "invalid-observation-metadata";
        if (!result.candidateAvailable) return "no-candidate";
        if (!result.sourcePrecisionKnown || !result.precisionSupported) return "precision-unavailable";
        if (!result.sampledBarsClean || result.excludedViolations != 0 ||
            result.topExcludedSamples == 0 || result.bottomExcludedSamples == 0)
            return "sampled-exterior-not-clean";
        // The opt-in source refinement may verify a physical patch narrower
        // than three coarse columns. Its explicit proof replaces only this
        // per-frame run requirement, never the original-bin temporal coverage.
        const auto supported = [](const LocalBoundaryDiagnosticEdge& edge) {
            return edge.supportedSamples >= 3 ||
                (edge.supportedSamples > 0 && edge.refinedMask == edge.supportMask &&
                 edge.refinementSamples > 0 && edge.refinedMinimumRun >= 4);
        };
        if (!supported(result.topEdge) || !supported(result.bottomEdge))
            return "edge-support";
        if (result.topEdge.inwardContinuity < 0.75 || result.bottomEdge.inwardContinuity < 0.75)
            return "inward-continuity";
        if (std::abs(static_cast<int64_t>(result.top) - (result.height - result.bottom)) >
            static_cast<int64_t>(result.rowStep) * 2)
            return "opposing-inset-asymmetry";
        return nullptr;
    }

    void SetPrecisionMetadata(LocalBoundaryDiagnosticResult& result, VideoFrameEncoding encoding)
    {
        result.sourcePrecisionKnown = encoding != VideoFrameEncoding::UNKNOWN;
        switch (encoding)
        {
        case VideoFrameEncoding::V210:
        case VideoFrameEncoding::R210:
        case VideoFrameEncoding::R10b:
        case VideoFrameEncoding::R10l:
        case VideoFrameEncoding::R12B:
        case VideoFrameEncoding::R12L:
            result.precisionSupported = true;
            break;
        default:
            result.precisionSupported = false;
            break;
        }
    }

    bool SameContext(const LocalBoundaryDiagnosticContext& left,
        const LocalBoundaryDiagnosticContext& right)
    {
        return left.sourceGeneration == right.sourceGeneration &&
            left.rendererGeneration == right.rendererGeneration &&
            left.viewportGeneration == right.viewportGeneration &&
            left.sourceFormatGeneration == right.sourceFormatGeneration &&
            left.policyGeneration == right.policyGeneration &&
            left.continuityGeneration == right.continuityGeneration &&
            left.sceneGeneration == right.sceneGeneration;
    }
}

LocalBoundaryDiagnosticResult AnalyzeLocalBoundaryGrid(
    const ActivePictureDiagnosticGrid& grid, int width, int height)
{
    LocalBoundaryDiagnosticResult result;
    if (width < 2 || height < 2 || grid.columns < 2 || grid.columns > 128 ||
        grid.rows < 2 || grid.rows > 270 || grid.columns > width || grid.rows > height ||
        grid.samples.size() != static_cast<size_t>(grid.columns) * grid.rows ||
        grid.samples.size() > LOCAL_BOUNDARY_MAX_SAMPLES)
        return result;

    for (const auto& sample : grid.samples)
        if (sample.luma > 1023 || sample.chromaU > 1023 || sample.chromaV > 1023)
            return result;

    result.available = true;
    result.width = width;
    result.height = height;
    result.columns = grid.columns;
    result.rows = grid.rows;
    result.sampleCount = grid.samples.size();
    // ceil((height - 1)/(rows - 1)); avoid overflow in arbitrary replay dimensions.
    result.rowStep = static_cast<int>((static_cast<int64_t>(height) + grid.rows - 3) / (grid.rows - 1));

    std::vector<int> perimeter;
    perimeter.reserve(static_cast<size_t>(grid.columns + grid.rows) * 2);
    for (int column = 0; column < grid.columns; ++column)
    {
        perimeter.push_back(grid.samples[column].luma);
        perimeter.push_back(grid.samples[static_cast<size_t>(grid.rows - 1) * grid.columns + column].luma);
    }
    for (int row = 1; row + 1 < grid.rows; ++row)
    {
        perimeter.push_back(grid.samples[static_cast<size_t>(row) * grid.columns].luma);
        perimeter.push_back(grid.samples[static_cast<size_t>(row) * grid.columns + grid.columns - 1].luma);
    }
    const size_t percentile = (perimeter.size() - 1) / 10;
    std::nth_element(perimeter.begin(), perimeter.begin() + percentile, perimeter.end());
    const int observedLow = perimeter[percentile];
    result.blackFloor = observedLow < 32 ? 0 : std::max(48, std::min(observedLow, 80));
    result.blackThreshold = std::min(104, result.blackFloor + 24);

    uint64_t fingerprint = 14695981039346656037ULL;
    HashValue(fingerprint, static_cast<uint32_t>(width));
    HashValue(fingerprint, static_cast<uint32_t>(height));
    HashValue(fingerprint, static_cast<uint32_t>(grid.columns));
    HashValue(fingerprint, static_cast<uint32_t>(grid.rows));
    for (const auto& sample : grid.samples)
    {
        HashValue(fingerprint, sample.luma);
        HashValue(fingerprint, sample.chromaU);
        HashValue(fingerprint, sample.chromaV);
    }
    result.fingerprint = fingerprint;

    std::vector<LocalBoundaryDiagnosticEdge> rows;
    rows.reserve(grid.rows);
    int first = -1, last = -1;
    for (int row = 0; row < grid.rows; ++row)
    {
        rows.push_back(InspectRow(grid, row, height, result.blackThreshold));
        if (rows.back().connectedRuns == 0) continue;
        if (first < 0) first = row;
        last = row;
    }
    if (first < 0) return result;

    result.candidateAvailable = true;
    result.top = Coordinate(first, height, grid.rows);
    result.bottom = Coordinate(last, height, grid.rows) + 1;
    result.topEdge = rows[first];
    result.bottomEdge = rows[last];
    InspectAlignedInterior(grid, rows, true, result.topEdge);
    InspectAlignedInterior(grid, rows, false, result.bottomEdge);
    for (int row = 0; row < grid.rows; ++row)
    {
        if (row >= first && row <= last) continue;
        for (int column = 0; column < grid.columns; ++column)
        {
            const auto& sample = grid.samples[static_cast<size_t>(row) * grid.columns + column];
            const bool violation = sample.luma > result.blackThreshold || ChromaDelta(sample) > 32;
            ++result.excludedSamples;
            result.excludedViolations += violation;
            if (row < first)
            {
                ++result.topExcludedSamples;
                result.topExcludedViolations += violation;
            }
            else
            {
                ++result.bottomExcludedSamples;
                result.bottomExcludedViolations += violation;
            }
        }
    }
    result.sampledBarsClean = result.topExcludedSamples != 0 &&
        result.bottomExcludedSamples != 0 && result.excludedViolations == 0;
    return result;
}

LocalBoundaryDiagnosticResult SampleLocalBoundaryDiagnostics(const AnalysisLumaSource& source)
{
    const auto grid = SampleActivePictureDiagnosticGrid(source);
    auto result = AnalyzeLocalBoundaryGrid(grid, source.width, source.height);
    SetPrecisionMetadata(result, source.encoding);
    return result;
}

void LocalBoundaryDiagnosticHistory::Reset()
{
    m_entries.clear();
    m_context = {};
    m_hasContext = false;
    m_width = m_height = m_columns = m_rows = 0;
    m_anchorTop = m_anchorBottom = m_anchorRowStep = 0;
}

LocalBoundaryDiagnosticHistorySummary LocalBoundaryDiagnosticHistory::Observe(
    const LocalBoundaryDiagnosticResult& result, const LocalBoundaryDiagnosticContext& context)
{
    LocalBoundaryDiagnosticHistorySummary summary;
    auto clear = [&](const char* reason)
    {
        Reset();
        summary.reset = true;
        summary.resetReason = reason;
    };
    if (!result.available || result.width < 2 || result.height < 2 ||
        result.columns < 2 || result.columns > 128 || result.rows < 2 || result.rows > 270 ||
        result.sampleCount == 0 || result.sampleCount > LOCAL_BOUNDARY_MAX_SAMPLES)
    {
        clear("invalid-source");
        return summary;
    }
    if (!result.candidateAvailable || result.top < 0 || result.bottom <= result.top ||
        result.bottom > result.height || result.rowStep <= 0)
    {
        clear("no-candidate");
        return summary;
    }
    if (result.excludedViolations != 0)
    {
        clear("excluded-sample-violation");
        return summary;
    }
    if (m_hasContext)
    {
        if (!SameContext(m_context, context)) clear("context-change");
        else if (result.width != m_width || result.height != m_height ||
            result.columns != m_columns || result.rows != m_rows || result.rowStep != m_anchorRowStep)
            clear("grid-change");
        else if (context.timestampMs < m_context.timestampMs) clear("time-backward");
        else if (context.acceptedSequence < m_context.acceptedSequence) clear("sequence-backward");
        else if (context.timestampMs - m_context.timestampMs > LOCAL_BOUNDARY_HISTORY_MAX_GAP_MS)
            clear("sample-gap");
        else if (std::abs(result.top - m_anchorTop) > m_anchorRowStep ||
            std::abs(result.bottom - m_anchorBottom) > m_anchorRowStep)
            clear("boundary-drift");
    }
    const bool repeatedSequence = context.cadenceRepeat ||
        (m_hasContext && context.acceptedSequence == m_context.acceptedSequence);
    const bool hadEntries = !m_entries.empty();
    while (!m_entries.empty() &&
        context.timestampMs - m_entries.front().timestampMs > LOCAL_BOUNDARY_HISTORY_MAX_AGE_MS)
        m_entries.pop_front();
    if (hadEntries && m_entries.empty() && !summary.reset)
    {
        summary.reset = true;
        summary.resetReason = "window-expired";
    }
    if (!m_hasContext || (m_entries.empty() && !repeatedSequence))
    {
        m_width = result.width;
        m_height = result.height;
        m_columns = result.columns;
        m_rows = result.rows;
        m_anchorTop = result.top;
        m_anchorBottom = result.bottom;
        m_anchorRowStep = result.rowStep;
    }
    // Same-frame polling cannot create an observation or refresh stored evidence.
    // Identical pixels at different source sequences remain in frameCount only.
    if (!repeatedSequence)
    {
        m_entries.push_back({ context.timestampMs, context.acceptedSequence,
            result.fingerprint, result.topEdge.supportMask, result.bottomEdge.supportMask,
            BasicHypothesisFailure(result) == nullptr });
        while (m_entries.size() > LOCAL_BOUNDARY_HISTORY_MAX_FRAMES) m_entries.pop_front();
    }
    m_context = context;
    m_hasContext = true;
    summary.available = !m_entries.empty();
    summary.frameCount = m_entries.size();
    summary.freshObservation = !repeatedSequence && !m_entries.empty();
    summary.anchorTop = m_anchorTop;
    summary.anchorBottom = m_anchorBottom;
    summary.anchorRowStep = m_anchorRowStep;
    summary.width = m_width;
    summary.height = m_height;
    summary.columns = m_columns;
    summary.rows = m_rows;
    if (!m_entries.empty()) summary.windowAgeMs = context.timestampMs - m_entries.front().timestampMs;
    if (!m_entries.empty())
    {
        summary.firstTopMask = m_entries.front().topMask;
        summary.firstBottomMask = m_entries.front().bottomMask;
        summary.hypothesisWindowEligible = true;
    }
    std::vector<std::bitset<128>> topMasks, bottomMasks;
    std::set<uint64_t> fingerprints;
    for (const auto& entry : m_entries)
    {
        summary.hypothesisWindowEligible = summary.hypothesisWindowEligible && entry.hypothesisEligible;
        if (std::find(topMasks.begin(), topMasks.end(), entry.topMask) == topMasks.end()) topMasks.push_back(entry.topMask);
        if (std::find(bottomMasks.begin(), bottomMasks.end(), entry.bottomMask) == bottomMasks.end()) bottomMasks.push_back(entry.bottomMask);
        if (!fingerprints.insert(entry.fingerprint).second) continue;
        summary.topMask |= entry.topMask;
        summary.bottomMask |= entry.bottomMask;
    }
    summary.distinctCount = fingerprints.size();
    summary.topDistinctMasks = topMasks.size();
    summary.bottomDistinctMasks = bottomMasks.size();
    summary.topUnionSamples = summary.topMask.count();
    summary.bottomUnionSamples = summary.bottomMask.count();
    return summary;
}



// BEGIN ANCHOR SESSION API
LocalBoundaryAnchorHypothesis EvaluateLocalBoundaryAnchorHypothesis(
    const LocalBoundaryDiagnosticResult& result,
    const LocalBoundaryDiagnosticHistorySummary& history)
{
    LocalBoundaryAnchorHypothesis hypothesis;
    const char* failure = BasicHypothesisFailure(result);
    if (failure)
    {
        hypothesis.reason = failure;
        hypothesis.evaluated = ValidObservationMetadata(result);
        return hypothesis;
    }
    if (!history.available || history.frameCount == 0 ||
        history.frameCount > LOCAL_BOUNDARY_HISTORY_MAX_FRAMES ||
        history.width != result.width || history.height != result.height ||
        history.columns != result.columns || history.rows != result.rows ||
        history.anchorRowStep != result.rowStep ||
        std::abs(static_cast<int64_t>(history.anchorTop) - result.top) > result.rowStep ||
        std::abs(static_cast<int64_t>(history.anchorBottom) - result.bottom) > result.rowStep ||
        !MasksFit(history.topMask, result.columns) || !MasksFit(history.bottomMask, result.columns) ||
        !MasksFit(history.firstTopMask, result.columns) || !MasksFit(history.firstBottomMask, result.columns) ||
        history.topUnionSamples != history.topMask.count() || history.bottomUnionSamples != history.bottomMask.count() ||
        history.firstTopMask.none() || history.firstBottomMask.none() ||
        (result.topEdge.supportMask & ~history.topMask).any() ||
        (result.bottomEdge.supportMask & ~history.bottomMask).any() ||
        (history.firstTopMask & ~history.topMask).any() || (history.firstBottomMask & ~history.bottomMask).any() ||
        history.topDistinctMasks == 0 || history.bottomDistinctMasks == 0 ||
        history.topDistinctMasks > history.frameCount || history.bottomDistinctMasks > history.frameCount)
    {
        hypothesis.reason = "incompatible-history-metadata";
        return hypothesis;
    }
    hypothesis.evaluated = true;
    hypothesis.topCoverage = static_cast<double>(history.topMask.count()) / result.columns;
    hypothesis.bottomCoverage = static_cast<double>(history.bottomMask.count()) / result.columns;
    hypothesis.topNewCoverage = static_cast<double>((history.topMask & ~history.firstTopMask).count()) / result.columns;
    hypothesis.bottomNewCoverage = static_cast<double>((history.bottomMask & ~history.firstBottomMask).count()) / result.columns;
    if (!history.freshObservation)
        hypothesis.reason = "same-frame";
    else if (!history.hypothesisWindowEligible)
        hypothesis.reason = "ineligible-window-sample";
    else if (history.frameCount < LOCAL_BOUNDARY_HYPOTHESIS_MIN_FRAMES ||
        history.windowAgeMs < LOCAL_BOUNDARY_HYPOTHESIS_MIN_WINDOW_MS ||
        history.windowAgeMs > LOCAL_BOUNDARY_HISTORY_MAX_AGE_MS)
        hypothesis.reason = "insufficient-bounded-window";
    else
    {
        const bool topBroad = hypothesis.topCoverage >= 0.25 && hypothesis.bottomCoverage >= 0.10 &&
            history.bottomDistinctMasks >= 3 && hypothesis.bottomNewCoverage >= 0.06;
        const bool bottomBroad = hypothesis.bottomCoverage >= 0.25 && hypothesis.topCoverage >= 0.10 &&
            history.topDistinctMasks >= 3 && hypothesis.topNewCoverage >= 0.06;
        hypothesis.qualifies = topBroad || bottomBroad;
        hypothesis.reason = hypothesis.qualifies ? "experimental-bilateral-spatial-hypothesis" : "insufficient-spatial-diversity";
    }
    return hypothesis;
}

namespace
{
    void InspectFixedAnchorExterior(const ActivePictureDiagnosticGrid& grid,
        LocalBoundaryAnchorTelemetry& anchor)
    {
        anchor.evaluated = true;
        anchor.topSamples = anchor.bottomSamples = 0;
        anchor.topViolations = anchor.bottomViolations = 0;
        anchor.topPeakLuma = anchor.bottomPeakLuma = 0;
        anchor.topPeakLumaDelta = anchor.bottomPeakLumaDelta = 0;
        anchor.topPeakChromaDelta = anchor.bottomPeakChromaDelta = 0;
        for (int row = 0; row < grid.rows; ++row)
        {
            if (row >= anchor.topRow && row <= anchor.bottomRow) continue;
            const bool top = row < anchor.topRow;
            for (int column = 0; column < grid.columns; ++column)
            {
                const auto& sample = grid.samples[static_cast<size_t>(row) * grid.columns + column];
                const int chromaDelta = ChromaDelta(sample);
                const bool violation = sample.luma > anchor.blackThreshold || chromaDelta > 32;
                auto& count = top ? anchor.topSamples : anchor.bottomSamples;
                auto& violations = top ? anchor.topViolations : anchor.bottomViolations;
                auto& peak = top ? anchor.topPeakLuma : anchor.bottomPeakLuma;
                auto& delta = top ? anchor.topPeakLumaDelta : anchor.bottomPeakLumaDelta;
                auto& uv = top ? anchor.topPeakChromaDelta : anchor.bottomPeakChromaDelta;
                ++count;
                violations += violation;
                peak = std::max(peak, static_cast<int>(sample.luma));
                delta = std::max(delta, static_cast<int>(sample.luma) - anchor.blackFloor);
                uv = std::max(uv, chromaDelta);
            }
        }
    }
}

void LocalBoundaryDiagnosticSession::Reset()
{
    m_history.Reset();
    m_anchor = {};
    m_context = {};
    m_hasContext = false;
    m_width = m_height = m_columns = m_rows = 0;
    // Anchor IDs remain monotonic for the lifetime of this session object.
}

LocalBoundaryDiagnosticTelemetry LocalBoundaryDiagnosticSession::Observe(
    const AnalysisLumaSource& source, const LocalBoundaryDiagnosticContext& context)
{
    const auto grid = SampleActivePictureDiagnosticGrid(source);
    return ObserveGrid(grid, source.width, source.height, source.encoding, context);
}

LocalBoundaryDiagnosticTelemetry LocalBoundaryDiagnosticSession::ObserveGrid(
    const ActivePictureDiagnosticGrid& grid, int width, int height, VideoFrameEncoding encoding,
    const LocalBoundaryDiagnosticContext& context,
    const AnalysisLumaSource* refinementSource)
{
    LocalBoundaryDiagnosticTelemetry telemetry;
    telemetry.observation = AnalyzeLocalBoundaryGrid(grid, width, height);
    SetPrecisionMetadata(telemetry.observation, encoding);
    telemetry.coarseObservation = telemetry.observation;
    if (refinementSource)
    {
        const auto refined = RefineLocalBoundaryEdges(*refinementSource, grid, telemetry.observation);
        telemetry.refinementEvaluated = refined.evaluated;
        telemetry.refinementApplied = refined.applied;
        telemetry.refinementSamples = refined.sourceSamples;
        telemetry.refinementReason = refined.reason;
        telemetry.observation = refined.observation;
    }
    const bool repeatedSequence = context.cadenceRepeat ||
        (m_hasContext && context.acceptedSequence == m_context.acceptedSequence);
    const char* resetReason = nullptr;
    if (!telemetry.observation.available) resetReason = "invalid-source";
    else if (m_hasContext)
    {
        if (!SameContext(m_context, context)) resetReason = "context-change";
        else if (width != m_width || height != m_height || grid.columns != m_columns || grid.rows != m_rows)
            resetReason = "grid-change";
        else if (context.timestampMs < m_context.timestampMs) resetReason = "time-backward";
        else if (context.acceptedSequence < m_context.acceptedSequence) resetReason = "sequence-backward";
        else if (context.timestampMs - m_context.timestampMs > LOCAL_BOUNDARY_HISTORY_MAX_GAP_MS)
            resetReason = "sample-gap";
    }
    if (!resetReason && m_anchor.active &&
        (!telemetry.observation.sourcePrecisionKnown || !telemetry.observation.precisionSupported))
        resetReason = "precision-unavailable";
    if (!resetReason && m_anchor.active &&
        context.timestampMs - m_anchor.createdAtMs >= LOCAL_BOUNDARY_ANCHOR_MAX_AGE_MS)
        resetReason = "anchor-expired";

    auto release = [&](const char* reason)
    {
        telemetry.anchor = m_anchor;
        telemetry.anchor.created = false;
        telemetry.anchor.released = m_anchor.active;
        telemetry.anchor.active = false;
        telemetry.anchor.current = false;
        telemetry.anchor.evaluated = false;
        telemetry.anchor.sameFrame = repeatedSequence;
        telemetry.anchor.reason = reason;
        if (m_anchor.active && context.timestampMs >= m_anchor.createdAtMs)
            telemetry.anchor.ageMs = context.timestampMs - m_anchor.createdAtMs;
        m_anchor = {};
        m_history.Reset();
        telemetry.history.reset = true;
        telemetry.history.resetReason = reason;
        telemetry.hypothesis.reason = reason;
    };
    if (resetReason)
        release(resetReason);
    else
    {
        if (m_anchor.active)
        {
            telemetry.anchor = m_anchor;
            telemetry.anchor.created = false;
            telemetry.anchor.released = false;
            telemetry.anchor.ageMs = context.timestampMs - m_anchor.createdAtMs;
            telemetry.anchor.sourceSequence = context.acceptedSequence;
            telemetry.anchor.sameFrame = repeatedSequence;
            telemetry.anchor.current = !repeatedSequence;
            InspectFixedAnchorExterior(grid, telemetry.anchor);
            if (telemetry.anchor.topViolations != 0 || telemetry.anchor.bottomViolations != 0)
            {
                // Preserve the actual failed measurement in telemetry; admission
                // cannot fall through and replace this anchor on the same call.
                telemetry.anchor.active = false;
                telemetry.anchor.released = true;
                telemetry.anchor.reason = "anchor-exterior-violation";
                m_anchor = {};
                m_history.Reset();
                telemetry.history.reset = true;
                telemetry.history.resetReason = telemetry.anchor.reason;
                telemetry.hypothesis.reason = telemetry.anchor.reason;
                resetReason = telemetry.anchor.reason;
            }
            else
                telemetry.anchor.reason = repeatedSequence ? "same-frame" : "original-sampled-exterior-clean";
        }
        if (!resetReason)
        {
            // Discovery may fail or move inward while the independently measured
            // original anchor remains unchanged. Do not conflate the two bands.
            const char* basicFailure = BasicHypothesisFailure(telemetry.observation);
            if (basicFailure)
            {
                m_history.Reset();
                telemetry.history.reset = true;
                telemetry.history.resetReason = basicFailure;
                telemetry.hypothesis = EvaluateLocalBoundaryAnchorHypothesis(telemetry.observation, telemetry.history);
            }
            else
            {
                auto historyContext = context;
                historyContext.cadenceRepeat = repeatedSequence;
                telemetry.history = m_history.Observe(telemetry.observation, historyContext);
                telemetry.hypothesis = EvaluateLocalBoundaryAnchorHypothesis(telemetry.observation, telemetry.history);
            }
            if (!m_anchor.active && !repeatedSequence && telemetry.hypothesis.qualifies)
            {
                const auto& observed = telemetry.observation;
                m_anchor = {};
                m_anchor.active = true;
                m_anchor.created = true;
                m_anchor.current = true;
                m_anchor.anchorId = m_nextAnchorId++;
                m_anchor.createdAtMs = context.timestampMs;
                m_anchor.sourceSequence = context.acceptedSequence;
                m_anchor.top = observed.top;
                m_anchor.bottom = observed.bottom;
                m_anchor.topRow = observed.topEdge.row;
                m_anchor.bottomRow = observed.bottomEdge.row;
                m_anchor.blackFloor = observed.blackFloor;
                m_anchor.blackThreshold = observed.blackThreshold;
                m_anchor.rowStep = observed.rowStep;
                m_anchor.topExteriorLastY = Coordinate(m_anchor.topRow - 1, height, grid.rows);
                m_anchor.bottomExteriorFirstY = Coordinate(m_anchor.bottomRow + 1, height, grid.rows);
                m_anchor.topUnknownFirstY = m_anchor.topExteriorLastY + 1;
                m_anchor.topUnknownLastY = observed.top - 1;
                m_anchor.bottomUnknownFirstY = observed.bottom;
                m_anchor.bottomUnknownLastY = m_anchor.bottomExteriorFirstY - 1;
                m_anchor.reason = "experimental-anchor-created";
                InspectFixedAnchorExterior(grid, m_anchor);
                telemetry.anchor = m_anchor;
            }
        }
    }
    m_context = context;
    m_hasContext = true;
    m_width = width;
    m_height = height;
    m_columns = grid.columns;
    m_rows = grid.rows;
    return telemetry;
}

LocalBoundaryDiagnosticComparisonTelemetry LocalBoundaryDiagnosticComparison::Observe(
    const AnalysisLumaSource& source, const LocalBoundaryDiagnosticContext& context)
{
    const auto grid = SampleActivePictureDiagnosticGrid(source);
    LocalBoundaryDiagnosticComparisonTelemetry result;
    result.coarse = m_coarse.ObserveGrid(grid, source.width, source.height, source.encoding, context);
    result.refined = m_refined.ObserveGrid(grid, source.width, source.height, source.encoding, context, &source);
    return result;
}
void LocalBoundaryDiagnosticComparison::Reset()
{
    m_coarse.Reset();
    m_refined.Reset();
}
