#pragma once
#include <SourcePictureMeasurement.h>
#include <memory>

// Process-selected local experiment. This selects the source measurement producer,
// not a permission to discard pixels or a different subtitle lifetime policy.
bool SourcePictureDetectorEnabled();
struct SourcePictureDetectionResult {
    ActivePictureEvidence evidence;
    std::shared_ptr<const SourcePictureObservation> observation;
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
    return !SourcePictureDetectorEnabled() || evidence.detector!=ActivePictureDetectorKind::Legacy;
}
