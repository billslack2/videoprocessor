#pragma once
#include <SourcePictureMeasurement.h>
#include <SourcePictureEquivalenceToken.h>
#include <memory>

// Production uses one source measurement producer. Kept as a named policy
// predicate for existing adapters; environment variables cannot select a rival
// geometry producer. Crop permission and subtitle lifetime remain separate.
bool SourcePictureDetectorEnabled();
// Immutable current measured-edge samples. A match is only a compatible
// physical hypothesis; it is not crop permission or a new physical measurement.
class SourcePictureBoundaryReference {
public:
    static std::shared_ptr<const SourcePictureBoundaryReference> Capture(
        const AnalysisLumaSource&,const ActivePictureFrameIdentity&,const SourcePictureObservation&,
        const ActivePictureBounds* compatibleHypothesis=nullptr,const AnalysisEvaluationBudget* budget=nullptr);
    bool MatchesCurrent(const AnalysisLumaSource&,const ActivePictureFrameIdentity&,
        const SourcePictureObservation*,const AnalysisEvaluationBudget* budget=nullptr) const;
    const ActivePictureBounds& MeasuredBounds()const{return measured_;}
    const ActivePictureBounds& BoundHypothesis()const{return hypothesis_;}
    const ActivePictureFrameIdentity& SeedIdentity()const{return identity_;}
private:
    struct Pixel {int x=0,y=0;AnalysisLumaSample value;};
    ActivePictureFrameIdentity identity_;
    AnalysisLumaSource interpretation_; // Metadata only; no frame pointer/owner retained.
    ActivePictureBounds measured_,hypothesis_;
    std::array<int,4> windows_{};
    std::vector<Pixel> samples_;
};
struct SourcePictureSupportRequest {
    // Nominations owned by the existing ordered/context-bound episode. The
    // worker only verifies this current source and never mutates the episode.
    std::shared_ptr<const SourcePictureBoundaryReference> boundary;
    bool retainedSupportAvailable=false,physicalHypothesisAvailable=false;
    ActivePictureBounds retainedSupport,physicalHypothesis;
};
bool CurrentSourceTransitionSupportsHypothesis(const SourcePictureObservation&,
    const ActivePictureBounds&);

struct SourcePictureSupportResult {
    SourcePictureEquivalenceToken sourceEquivalence;
    CurrentSourceSupportEvidence currentSupport;
    std::shared_ptr<const SourcePictureBoundaryReference> boundaryReference;
    bool boundaryEncodingEquivalent=false,physicalHypothesisCompatible=false;
    ActivePictureBounds retainedPhysicalHypothesis;
};
struct SourcePictureDetectionResult {
    SourcePictureEquivalenceToken sourceEquivalence;
    ActivePictureEvidence evidence;
    // Optional worker result; transport this pointer through existing results.
    // No sample planes or temporal state are copied into ActivePictureEvidence.
    std::shared_ptr<const SourcePictureSupportResult> support;
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
    const ActivePictureFrameIdentity& identity,const SourcePictureSupportRequest* supportRequest=nullptr);
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
