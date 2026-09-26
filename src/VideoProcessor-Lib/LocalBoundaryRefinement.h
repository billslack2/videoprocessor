#pragma once

#include "LocalBoundaryDiagnostics.h"

// Extra reads belong only to opt-in startup or diagnostic comparison. Fine pixels never
// count as extra coarse-column coverage or independent temporal observations.
constexpr size_t LOCAL_BOUNDARY_REFINEMENT_MAX_SAMPLES = 8192;

struct LocalBoundaryRefinementResult
{
    LocalBoundaryDiagnosticResult observation;
    bool evaluated = false;
    bool applied = false;
    size_t sourceSamples = 0;
    const char* reason = "not-needed";
};

LocalBoundaryRefinementResult RefineLocalBoundaryEdges(
    const AnalysisLumaSource& source, const ActivePictureDiagnosticGrid& grid,
    const LocalBoundaryDiagnosticResult& observation);
