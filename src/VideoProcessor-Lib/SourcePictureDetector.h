#pragma once
#include <SourcePictureMeasurement.h>
#include <memory>

// Production uses one source measurement producer. Kept as a named policy
// predicate for existing adapters; environment variables cannot select a rival
// geometry producer. Crop permission and subtitle lifetime remain separate.
bool SourcePictureDetectorEnabled();
struct SourcePictureDetectionResult {
    ActivePictureEvidence evidence;
    std::shared_ptr<const SourcePictureObservation> observation;
    bool geometryReused = false;
    double geometryLookupMs = 0.0;
};
ActivePictureEvidence MissingSourcePictureEvidence(const AnalysisLumaSource& source,
    const char* reason = "source-spatial-measurement-not-ready");
// Worker-only at runtime. Pure explicit entry point for native replay/tests.
SourcePictureDetectionResult DetectSourcePictureForWorker(const AnalysisLumaSource& source,
    const AnalysisEvaluationBudget* budget = nullptr);

// Runtime worker adapter; fixed cooperative budget, no rate limiter or waiting.
SourcePictureDetectionResult DetectPreparedSourcePictureBounded(const AnalysisLumaSource& source,
    const ActivePictureFrameIdentity& identity);
inline bool PreparedSourceDetectorCurrent(const ActivePictureEvidence& evidence) {
    return evidence.detector!=ActivePictureDetectorKind::Legacy;
}

// Completion is scheduling state, never crop authority. A complete negative
// geometry observation has no candidate verifier to wait for.
inline bool SourcePictureMeasurementWorkComplete(const ActivePictureEvidence& evidence) {
    if(!evidence.physicalEdges.evaluated)return false;
    return evidence.detector==ActivePictureDetectorKind::SourceSpatialUnavailable ||
        evidence.sourceCandidateVerificationComplete;
}
inline bool SourcePictureMeasurementCanComplete(const ActivePictureEvidence& prior,
    const ActivePictureEvidence& next) {
    const auto& a=prior.physicalEdges;const auto& b=next.physicalEdges;
    if(!a.evaluated)return b.evaluated;
    const auto same=[](const ActivePictureBounds& x,const ActivePictureBounds& y) {
        return x.left==y.left && x.top==y.top && x.right==y.right && x.bottom==y.bottom &&
            x.rasterWidth==y.rasterWidth && x.rasterHeight==y.rasterHeight &&
            x.aspectRatio==y.aspectRatio && x.trustedBarAxes==y.trustedBarAxes;
    };
    return b.evaluated && !prior.sourceCandidateVerificationComplete &&
        next.sourceCandidateVerificationComplete && prior.detector==next.detector &&
        a.sourceGeneration==b.sourceGeneration && a.estimateAvailable==b.estimateAvailable &&
        a.confidence==b.confidence && same(a.estimate,b.estimate);
}
