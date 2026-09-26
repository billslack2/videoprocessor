#include "pch.h"
#include "LocalBoundaryRefinement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
    int Coordinate(int index, int extent, int count)
    {
        return static_cast<int>(static_cast<int64_t>(index) * (extent - 1) / (count - 1));
    }

    int ChromaDelta(const AnalysisLumaSample& sample)
    {
        return (std::max)(std::abs(static_cast<int>(sample.chromaU) - 512),
            std::abs(static_cast<int>(sample.chromaV) - 512));
    }

    bool Signal(const AnalysisLumaSample& sample, int threshold)
    {
        return sample.luma > threshold || ChromaDelta(sample) > 8;
    }

    bool ExteriorViolation(const AnalysisLumaSample& sample, int threshold)
    {
        return sample.luma > threshold || ChromaDelta(sample) > 32;
    }

    bool SupportedPrecision(VideoFrameEncoding encoding)
    {
        switch (encoding)
        {
        case VideoFrameEncoding::V210:
        case VideoFrameEncoding::R210:
        case VideoFrameEncoding::R10b:
        case VideoFrameEncoding::R10l:
        case VideoFrameEncoding::R12B:
        case VideoFrameEncoding::R12L:
            return true;
        default:
            return false;
        }
    }

    bool CoarseQualified(const LocalBoundaryDiagnosticEdge& edge)
    {
        return edge.refinedMask.none() && edge.supportedSamples >= 3 &&
            edge.connectedRuns > 0 && edge.inwardContinuity >= 0.75;
    }

    struct DensePoint
    {
        bool supported = false;
        std::array<bool, 3> inward{};
        int lumaDelta = 0, chromaDelta = 0;
    };

    // A patch retains its one original coarse-column identity. Requiring its
    // measured coarse point in the fine run prevents a different nearby feature
    // from certifying the nominated witness.
    bool InspectPatch(const AnalysisLumaSource& source,
        const ActivePictureDiagnosticGrid& grid, int row, int column, bool top,
        int threshold, LocalBoundaryDiagnosticEdge& edge,
        LocalBoundaryRefinementResult& result, bool& failed)
    {
        const int direction = top ? 1 : -1;
        if (row - direction < 0 || row - direction >= grid.rows ||
            row + 3 * direction < 0 || row + 3 * direction >= grid.rows)
            return false;
        const int center = Coordinate(column, source.width, grid.columns);
        const int first = column == 0 ? 0 :
            (Coordinate(column - 1, source.width, grid.columns) + center) / 2 + 1;
        const int last = column + 1 == grid.columns ? source.width - 1 :
            (center + Coordinate(column + 1, source.width, grid.columns)) / 2;
        const int minimumRun = (std::max)(4, ((source.width - 1) / (grid.columns - 1)) / 4);
        if (last - first + 1 < minimumRun) return false;
        const size_t required = static_cast<size_t>(last - first + 1) * 5;
        if (required > LOCAL_BOUNDARY_REFINEMENT_MAX_SAMPLES - result.sourceSamples)
        {
            failed = true;
            result.reason = "refinement-budget-exhausted";
            return false;
        }
        std::vector<DensePoint> points(static_cast<size_t>(last - first + 1));
        for (int x = first; x <= last; ++x)
        {
            std::array<AnalysisLumaSample, 5> samples;
            const std::array<int, 5> rowIndices{row, row - direction,
                row + direction, row + 2 * direction, row + 3 * direction};
            for (size_t index = 0; index < samples.size(); ++index)
            {
                ++result.sourceSamples;
                if (!source.Sample(x, Coordinate(rowIndices[index], source.height, grid.rows), samples[index]))
                {
                    failed = true;
                    result.reason = "refinement-source-unavailable";
                    return false;
                }
            }
            auto& point = points[static_cast<size_t>(x - first)];
            point.lumaDelta = static_cast<int>(samples[0].luma) - samples[1].luma;
            point.chromaDelta = (std::max)(
                std::abs(static_cast<int>(samples[0].chromaU) - samples[1].chromaU),
                std::abs(static_cast<int>(samples[0].chromaV) - samples[1].chromaV));
            point.supported = Signal(samples[0], threshold) &&
                !ExteriorViolation(samples[1], threshold) &&
                (point.lumaDelta >= 8 || point.chromaDelta >= 8);
            for (size_t depth = 0; depth < point.inward.size(); ++depth)
                point.inward[depth] = Signal(samples[depth + 2], threshold);
        }
        const int centerIndex = center - first;
        if (!points[centerIndex].supported) return false;
        int runFirst = centerIndex, runLast = centerIndex;
        while (runFirst > 0 && points[runFirst - 1].supported) --runFirst;
        while (runLast + 1 < static_cast<int>(points.size()) && points[runLast + 1].supported) ++runLast;
        const int run = runLast - runFirst + 1;
        if (run < minimumRun) return false;
        std::array<int, 3> inward{};
        int lumaDelta = 0, chromaDelta = 0;
        for (int point = runFirst; point <= runLast; ++point)
        {
            for (size_t depth = 0; depth < inward.size(); ++depth)
                inward[depth] += points[point].inward[depth] ? 1 : 0;
            lumaDelta = (std::max)(lumaDelta, points[point].lumaDelta);
            chromaDelta = (std::max)(chromaDelta, points[point].chromaDelta);
        }
        const double continuity = static_cast<double>(inward[0] + inward[1] + inward[2]) / (3 * run);
        if (continuity < 0.75) return false;
        const auto& coarse = grid.samples[static_cast<size_t>(row) * grid.columns + column];
        const bool luma = coarse.luma > threshold, color = ChromaDelta(coarse) > 8;
        edge.supportMask[column] = true;
        edge.refinedMask[column] = true;
        edge.lumaMask[column] = luma;
        edge.colorMask[column] = color;
        ++edge.supportedSamples;
        if (luma && color) ++edge.lumaAndColorSamples;
        else if (luma) ++edge.lumaOnlySamples;
        else if (color) ++edge.colorOnlySamples;
        edge.refinedMinimumRun = edge.refinedMinimumRun == 0 ? run : (std::min)(edge.refinedMinimumRun, run);
        edge.inwardContinuity = edge.supportedSamples == 1 ? continuity :
            (std::min)(edge.inwardContinuity, continuity);
        for (size_t depth = 0; depth < inward.size(); ++depth)
            edge.inwardSupportedSamples[depth] += inward[depth] * 4 >= run * 3 ? 1 : 0;
        edge.maxLumaDelta = (std::max)(edge.maxLumaDelta, lumaDelta);
        edge.maxChromaDelta = (std::max)(edge.maxChromaDelta, chromaDelta);
        return true;
    }

    bool RefineEdge(const AnalysisLumaSource& source, const ActivePictureDiagnosticGrid& grid,
        const LocalBoundaryDiagnosticResult& original, bool top,
        LocalBoundaryDiagnosticEdge& refined, LocalBoundaryRefinementResult& result, bool& failed)
    {
        const auto& old = top ? original.topEdge : original.bottomEdge;
        const int nominated = top ? source.height - original.bottom : source.height - original.top - 1;
        const int first = top ? 1 : grid.rows - 2;
        const int end = old.row;
        const int direction = top ? 1 : -1;
        for (int row = first; top ? row < end : row > end; row += direction)
        {
            const int y = Coordinate(row, source.height, grid.rows);
            if (std::abs(static_cast<int64_t>(y) - nominated) > static_cast<int64_t>(original.rowStep) * 2)
                continue;
            LocalBoundaryDiagnosticEdge edge;
            edge.row = row;
            edge.coordinate = y;
            const size_t initialSamples = result.sourceSamples;
            for (int column = 0; column < grid.columns; ++column)
            {
                if (!Signal(grid.samples[static_cast<size_t>(row) * grid.columns + column], original.blackThreshold))
                    continue;
                InspectPatch(source, grid, row, column, top, original.blackThreshold, edge, result, failed);
                if (failed) return false;
            }
            if (edge.supportedSamples == 0) continue;
            edge.refinementSamples = result.sourceSamples - initialSamples;
            refined = edge;
            return true; // Outermost actually supported sampled row, never mirror geometry.
        }
        return false;
    }

    void RecountExterior(const ActivePictureDiagnosticGrid& grid, LocalBoundaryDiagnosticResult& result)
    {
        result.excludedSamples = result.excludedViolations = 0;
        result.topExcludedSamples = result.bottomExcludedSamples = 0;
        result.topExcludedViolations = result.bottomExcludedViolations = 0;
        for (int row = 0; row < grid.rows; ++row)
        {
            if (row >= result.topEdge.row && row <= result.bottomEdge.row) continue;
            const bool top = row < result.topEdge.row;
            for (int column = 0; column < grid.columns; ++column)
            {
                const bool violation = ExteriorViolation(grid.samples[static_cast<size_t>(row) * grid.columns + column], result.blackThreshold);
                ++result.excludedSamples;
                result.excludedViolations += violation ? 1 : 0;
                if (top) { ++result.topExcludedSamples; result.topExcludedViolations += violation ? 1 : 0; }
                else { ++result.bottomExcludedSamples; result.bottomExcludedViolations += violation ? 1 : 0; }
            }
        }
        result.sampledBarsClean = result.topExcludedSamples != 0 && result.bottomExcludedSamples != 0 &&
            result.excludedViolations == 0;
    }
}

LocalBoundaryRefinementResult RefineLocalBoundaryEdges(const AnalysisLumaSource& source,
    const ActivePictureDiagnosticGrid& grid, const LocalBoundaryDiagnosticResult& observation)
{
    LocalBoundaryRefinementResult result;
    result.observation = observation;
    if (!source.IsValid() || !observation.available || !observation.candidateAvailable ||
        source.width != observation.width || source.height != observation.height ||
        grid.columns < 2 || grid.columns > 128 || grid.rows < 2 || grid.rows > 270 ||
        grid.columns > source.width || grid.rows > source.height ||
        grid.columns != observation.columns || grid.rows != observation.rows ||
        grid.samples.size() != static_cast<size_t>(grid.columns) * grid.rows ||
        observation.rowStep <= 0 || observation.topEdge.row < 0 ||
        observation.bottomEdge.row >= grid.rows || observation.topEdge.row > observation.bottomEdge.row ||
        observation.blackThreshold < 0 || observation.blackThreshold > 104)
    {
        result.reason = "refinement-invalid-source";
        return result;
    }
    if (!SupportedPrecision(source.encoding) || !observation.sourcePrecisionKnown || !observation.precisionSupported)
    {
        result.reason = "refinement-precision-unsupported";
        return result;
    }
    const bool topStrong = CoarseQualified(observation.topEdge) &&
        observation.topExcludedSamples != 0 && observation.topExcludedViolations == 0;
    const bool bottomStrong = CoarseQualified(observation.bottomEdge) &&
        observation.bottomExcludedSamples != 0 && observation.bottomExcludedViolations == 0;
    if (!topStrong && !bottomStrong)
    {
        result.reason = "refinement-no-coarse-opposing-edge";
        return result;
    }
    if (observation.sampledBarsClean && topStrong && bottomStrong &&
        std::abs(static_cast<int64_t>(observation.top) - (source.height - observation.bottom)) <=
            static_cast<int64_t>(observation.rowStep) * 2)
        return result;
    result.evaluated = true;
    bool failed = false, changed = false;
    auto refined = observation;
    if (topStrong)
        changed = RefineEdge(source, grid, observation, false, refined.bottomEdge, result, failed);
    if (!failed && !changed && bottomStrong)
        changed = RefineEdge(source, grid, observation, true, refined.topEdge, result, failed);
    if (failed) return result;
    if (!changed)
    {
        result.reason = "refinement-no-supported-opposing-row";
        return result;
    }
    refined.top = refined.topEdge.coordinate;
    refined.bottom = refined.bottomEdge.coordinate + 1;
    if (std::abs(static_cast<int64_t>(refined.top) - (source.height - refined.bottom)) >
        static_cast<int64_t>(refined.rowStep) * 2)
    {
        result.reason = "refinement-opposing-inset-asymmetry";
        return result;
    }
    RecountExterior(grid, refined);
    if (!refined.sampledBarsClean)
    {
        result.reason = "refinement-exterior-not-clean";
        return result;
    }
    result.observation = refined;
    result.applied = true;
    result.reason = "refinement-measured-opposing-edge";
    return result;
}


