#pragma once
#include "ActivePictureEvidence.h"

// Pure current-frame inspection. Qualified history nominates; the actual bands
// must still match the independently measured clean bar. Never learns history.
struct RememberedEdgeReturnResult
{
    bool candidateAvailable = false;
    ActivePictureEvidence evidence;
    const char* reason = "ineligible-context";
    size_t samples = 0, exteriorSamples = 0;
    int mismatches = 0, matchedEdgeSupport = 0;
    int edgeFringeSamples = 0, firstFringeY = -1, lastFringeY = -1;
    int lastMismatchY = -1;
    int firstMismatchX = -1, firstMismatchY = -1, maxLumaDelta = 0, maxChromaDelta = 0;
    double referenceY = 0, referenceU = 0, referenceV = 0, referenceDispersion = 0;
};
RememberedEdgeReturnResult InspectRememberedEdgeReturn(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination,
    uint64_t sourceSequence, uint64_t tickMs);
