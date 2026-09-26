#pragma once

#include "ActivePictureEvidence.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <deque>

// Diagnostic measurements only. These types intentionally carry neither an
// ActivePictureBounds contract nor a classification, trust, or confidence field.
// A moving object can produce precisely the same measurements as a picture edge.
constexpr size_t LOCAL_BOUNDARY_MAX_SAMPLES = 128u * 270u;
constexpr size_t LOCAL_BOUNDARY_HISTORY_MAX_FRAMES = 32;
constexpr uint64_t LOCAL_BOUNDARY_HISTORY_MAX_AGE_MS = 3000;
constexpr uint64_t LOCAL_BOUNDARY_HISTORY_MAX_GAP_MS = 500;

struct LocalBoundaryDiagnosticEdge
{
    int row = -1;
    int coordinate = -1;
    int connectedRuns = 0;
    int supportedSamples = 0;
    int lumaOnlySamples = 0;
    int colorOnlySamples = 0;
    int lumaAndColorSamples = 0;
    // Same-column change from the adjacent exterior sampled row (zero if absent).
    int maxLumaDelta = 0;
    int maxChromaDelta = 0;
    std::bitset<128> supportMask;
    std::bitset<128> lumaMask;
    std::bitset<128> colorMask;
    // Support at the SAME columns on each of the next three sampled inward rows.
    std::array<int, 3> inwardSupportedSamples{};
    double inwardContinuity = 0.0;
    // Explicit source-pixel refinement. Each bit still represents ONE original
    // grid column; dense neighboring samples never inflate spatial coverage.
    std::bitset<128> refinedMask;
    int refinedMinimumRun = 0;
    size_t refinementSamples = 0;
};

struct LocalBoundaryDiagnosticResult
{
    bool available = false;
    bool candidateAvailable = false;
    bool ambiguous = true; // Always true, including for apparently clean pairs.
    bool sourcePrecisionKnown = false;
    bool precisionSupported = false;
    int width = 0, height = 0, columns = 0, rows = 0;
    int top = 0, bottom = 0; // Coarse sampled visible interval; bottom is exclusive.
    int rowStep = 0;        // Maximum spacing between sampled rows, not exactness.
    int blackFloor = 0, blackThreshold = 0;
    size_t sampleCount = 0;
    size_t excludedSamples = 0, excludedViolations = 0;
    size_t topExcludedSamples = 0, bottomExcludedSamples = 0;
    size_t topExcludedViolations = 0, bottomExcludedViolations = 0;
    bool sampledBarsClean = false; // Only the sampled rows outside the interval.
    uint64_t fingerprint = 0;
    LocalBoundaryDiagnosticEdge topEdge, bottomEdge;
};

LocalBoundaryDiagnosticResult AnalyzeLocalBoundaryGrid(
    const ActivePictureDiagnosticGrid& grid, int width, int height);
LocalBoundaryDiagnosticResult SampleLocalBoundaryDiagnostics(
    const AnalysisLumaSource& source);

struct LocalBoundaryDiagnosticContext
{
    uint64_t sourceGeneration = 0;
    uint64_t rendererGeneration = 0;
    uint64_t viewportGeneration = 0;
    uint64_t sourceFormatGeneration = 0;
    uint64_t policyGeneration = 0;
    uint64_t continuityGeneration = 0;
    uint64_t sceneGeneration = 0;
    bool cadenceRepeat = false;
    uint64_t acceptedSequence = 0;
    uint64_t timestampMs = 0; // Monotonic time supplied by the caller.
};

struct LocalBoundaryDiagnosticHistorySummary
{
    bool available = false;
    bool ambiguous = true;
    bool reset = false;
    const char* resetReason = "none";
    size_t frameCount = 0;    // Distinct source sequences, including identical pixels.
    size_t distinctCount = 0; // Unique pixel fingerprints; never crop votes.
    uint64_t windowAgeMs = 0;
    int anchorTop = 0, anchorBottom = 0, anchorRowStep = 0;
    int width = 0, height = 0, columns = 0, rows = 0;
    std::bitset<128> topMask, bottomMask;
    size_t topUnionSamples = 0, bottomUnionSamples = 0;
    std::bitset<128> firstTopMask, firstBottomMask;
    size_t topDistinctMasks = 0, bottomDistinctMasks = 0;
    bool hypothesisWindowEligible = false;
    bool freshObservation = false;
};

class LocalBoundaryDiagnosticHistory
{
public:
    LocalBoundaryDiagnosticHistorySummary Observe(
        const LocalBoundaryDiagnosticResult& result,
        const LocalBoundaryDiagnosticContext& context);
    void Reset();

private:
    struct Entry
    {
        uint64_t timestampMs = 0, acceptedSequence = 0, fingerprint = 0;
        std::bitset<128> topMask, bottomMask;
        bool hypothesisEligible = false;
    };
    std::deque<Entry> m_entries;
    LocalBoundaryDiagnosticContext m_context{};
    bool m_hasContext = false;
    int m_width = 0, m_height = 0, m_columns = 0, m_rows = 0;
    int m_anchorTop = 0, m_anchorBottom = 0, m_anchorRowStep = 0;
};


// Experimental feature gates, not demonstrated safety limits or crop policy.
constexpr size_t LOCAL_BOUNDARY_HYPOTHESIS_MIN_FRAMES = 4;
constexpr uint64_t LOCAL_BOUNDARY_HYPOTHESIS_MIN_WINDOW_MS = 600;
constexpr uint64_t LOCAL_BOUNDARY_ANCHOR_MAX_AGE_MS = 15000;

struct LocalBoundaryAnchorHypothesis
{
    bool evaluated = false;
    bool qualifies = false;
    bool ambiguous = true;
    const char* reason = "not-evaluated";
    double topCoverage = 0.0, bottomCoverage = 0.0;
    double topNewCoverage = 0.0, bottomNewCoverage = 0.0;
};

// Feature-only replay is supported, but cannot validate a retained anchor's
// pixels. History masks and current metadata must belong to the same grid.
LocalBoundaryAnchorHypothesis EvaluateLocalBoundaryAnchorHypothesis(
    const LocalBoundaryDiagnosticResult& result,
    const LocalBoundaryDiagnosticHistorySummary& history);

struct LocalBoundaryAnchorTelemetry
{
    bool active = false;
    bool created = false;
    bool released = false;
    bool evaluated = false;
    bool current = false;
    bool sameFrame = false;
    bool ambiguous = true;
    const char* reason = "no-anchor";
    uint64_t anchorId = 0, createdAtMs = 0, ageMs = 0;
    uint64_t sourceSequence = 0;
    int top = 0, bottom = 0, topRow = -1, bottomRow = -1;
    int blackFloor = 0, blackThreshold = 0, rowStep = 0;
    int topExteriorLastY = -1, bottomExteriorFirstY = -1;
    // Inclusive unmeasured physical strips; first>last means empty. No pixel
    // in these gaps is claimed inspected. Adjacent EXTERIOR grid rows are kept.
    int topUnknownFirstY = 0, topUnknownLastY = -1;
    int bottomUnknownFirstY = 0, bottomUnknownLastY = -1;
    size_t topSamples = 0, bottomSamples = 0;
    size_t topViolations = 0, bottomViolations = 0;
    int topPeakLuma = 0, bottomPeakLuma = 0;
    int topPeakLumaDelta = 0, bottomPeakLumaDelta = 0;
    int topPeakChromaDelta = 0, bottomPeakChromaDelta = 0;
};

struct LocalBoundaryDiagnosticTelemetry
{
    LocalBoundaryDiagnosticResult observation;
    LocalBoundaryDiagnosticHistorySummary history;
    LocalBoundaryAnchorHypothesis hypothesis;
    LocalBoundaryAnchorTelemetry anchor;
    LocalBoundaryDiagnosticResult coarseObservation;
    bool refinementEvaluated = false, refinementApplied = false;
    size_t refinementSamples = 0;
    const char* refinementReason = "not-requested";
};

class LocalBoundaryDiagnosticSession
{
public:
    LocalBoundaryDiagnosticTelemetry Observe(const AnalysisLumaSource& source,
        const LocalBoundaryDiagnosticContext& context);
    LocalBoundaryDiagnosticTelemetry ObserveGrid(const ActivePictureDiagnosticGrid& grid,
        int width, int height, VideoFrameEncoding encoding,
        const LocalBoundaryDiagnosticContext& context,
        const AnalysisLumaSource* refinementSource = nullptr);
    void Reset();

private:
    LocalBoundaryDiagnosticHistory m_history;
    LocalBoundaryAnchorTelemetry m_anchor;
    LocalBoundaryDiagnosticContext m_context{};
    bool m_hasContext = false;
    int m_width = 0, m_height = 0, m_columns = 0, m_rows = 0;
    uint64_t m_nextAnchorId = 1;
};

// Independent measurement sessions, never inputs to crop policy. Both see the
// same source grid/context; only the refined session inspects extra source pixels.
struct LocalBoundaryDiagnosticComparisonTelemetry
{
    LocalBoundaryDiagnosticTelemetry coarse, refined;
};
class LocalBoundaryDiagnosticComparison
{
public:
    LocalBoundaryDiagnosticComparisonTelemetry Observe(const AnalysisLumaSource& source,
        const LocalBoundaryDiagnosticContext& context);
    void Reset();
private:
    LocalBoundaryDiagnosticSession m_coarse, m_refined;
};
