#pragma once

#include "LocalBoundaryDiagnostics.h"

// Explicit opt-in startup and inward-transition experiments. Sparse moving artwork
// can satisfy these
// measurements: neither the feature window nor this guard proves semantics.
// The normal transition and presentation policies remain the crop publishers.
constexpr uint64_t SPARSE_BOUNDARY_DISCOVERY_INTERVAL_MS = 200;

// This owner survives profile/analysis resets. Only a genuinely new nonzero
// source generation can reopen an experiment after a format was published.
class SparseBoundaryStartupGate
{
public:
    void ObserveSource(uint64_t generation)
    {
        if (generation && generation != m_generation)
        {
            m_generation = generation;
            m_closed = false;
        }
    }
    void OnPublication(uint64_t generation)
    {
        if (generation && generation == m_generation) m_closed = true;
    }
    bool IsOpen(uint64_t generation) const
    {
        return generation && generation == m_generation && !m_closed;
    }
private:
    uint64_t m_generation = 0;
    bool m_closed = false;
};

struct SparseBoundaryCropExperimentInput
{
    bool enabled = false;
    bool startupEligible = false;
    bool sceneCut = false;
    LocalBoundaryDiagnosticContext context;
    // Separate opt-in mode, never combined with startupEligible. The base must
    // be current, native, full width and unchanged throughout the proof window.
    bool transitionEligible = false;
    ActivePictureBounds establishedBase;
    uint64_t establishedBaseSourceGeneration = 0;
    ActivePictureAuthorityOrigin establishedBaseOrigin = ActivePictureAuthorityOrigin::NATIVE;
};

struct SparseBoundaryCropExperimentResult
{
    bool candidateAvailable = false;
    bool current = false;
    bool discoverySampled = false;
    bool referenceAvailable = false;
    bool reset = false;
    const char* reason = "disabled";
    uint64_t referenceId = 0;
    ActivePictureBounds referenceBounds;
    LocalBoundaryAnchorTelemetry fixedExterior;
    ActivePicturePresentationRetentionEvidence retention;
    // Exact raw input unless candidateAvailable; raw axis/edge measurements
    // remain unchanged even when the explicit experiment supplies geometry.
    ActivePictureEvidence evidence;
    size_t discoverySamples = 0; // Source reads for the full bounded grid.
    // Additional source reads, zero when the discovery grid is reused.
    size_t fixedExteriorSamples = 0;
    LocalBoundaryDiagnosticTelemetry diagnostic;
};

class SparseBoundaryCropExperiment
{
public:
    // Call once for every live accepted source frame, including repeats, while
    // enabled. No queue preview may advance this discovery state. Source and
    // context generations must match. A repeated sequence cannot acquire.
    SparseBoundaryCropExperimentResult Observe(const AnalysisLumaSource& source,
        const ActivePictureEvidence& raw,
        const SparseBoundaryCropExperimentInput& input);
    void Reset();

private:
    LocalBoundaryDiagnosticSession m_discovery;
    LocalBoundaryAnchorTelemetry m_reference;
    ActivePictureBounds m_bounds;
    LocalBoundaryDiagnosticContext m_context;
    bool m_hasContext = false;
    bool m_transitionMode = false;
    ActivePictureBounds m_establishedBase;

    uint64_t m_nextDiscoveryTick = 0;
    int m_width = 0, m_height = 0, m_columns = 0, m_rows = 0;
};
