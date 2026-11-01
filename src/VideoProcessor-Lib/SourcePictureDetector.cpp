#include <pch.h>
#include <SourcePictureDetector.h>
#include <AnalysisDiagnosticHooks.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

bool SourcePictureDetectorEnabled() {
    static const bool enabled=[] {
        char* value=nullptr;size_t length=0;
        if(_dupenv_s(&value,&length,"VP_SOURCE_PICTURE_DETECTOR")!=0)return false;
        const bool selected=value && std::strcmp(value,"1")==0;
        std::free(value);return selected;
    }();
    return enabled;
}
ActivePictureEvidence MissingSourcePictureEvidence(const AnalysisLumaSource& source,const char* reason) {
    ActivePictureEvidence result;
    // Missing geometry is not zero-code black. Nominal range is available
    // without sampling; evaluated remains false until a source reference exists.
    result.blackReference.floor=source.range==AnalysisLumaRange::Full?0:64;
    result.blackReference.threshold=std::min(104,result.blackReference.floor+24);
    result.blackReference.observedFloor=result.blackReference.floor;
    result.blackReference.observedThreshold=result.blackReference.threshold;
    result.blackReference.mode=source.range==AnalysisLumaRange::Full?
        ActivePictureBlackReferenceMode::FULL_NOMINAL:ActivePictureBlackReferenceMode::LEGACY;
    result.detector=ActivePictureDetectorKind::SourceSpatialUnavailable;
    result.reason=reason;
    result.physicalEdges.sourceGeneration=source.generation;
    result.physicalEdges.reason=reason;
    return result;
}
SourcePictureDetectionResult DetectSourcePictureForWorker(const AnalysisLumaSource& source,
    const AnalysisEvaluationBudget* budget) {
    SourcePictureDetectionResult result;
    result.evidence=MissingSourcePictureEvidence(source);
    result.observation=std::make_shared<const SourcePictureObservation>(MeasureSourcePicture(source,budget));
    const auto& facts=*result.observation;
    auto& evidence=result.evidence;
    evidence.reason=facts.reason;
    if(facts.sourceGeneration==source.generation && facts.width==source.width && facts.height==source.height &&
        facts.blackReference.evaluated)evidence.blackReference=facts.blackReference;
    if(!source.IsValid() || !facts.complete || facts.sourceGeneration!=source.generation ||
        facts.width!=source.width || facts.height!=source.height)return result;
    const bool measured=facts.outcome==SourcePictureOutcome::Measured && facts.rectangleAvailable;
    const bool canvas=facts.outcome==SourcePictureOutcome::CanvasEstimate &&
        facts.canvasSupported && facts.blankMarginsUnresolved && facts.topologyComplete;
    if(!measured && !canvas) {
        evidence.physicalEdges.evaluated=true;
        evidence.physicalEdges.reason=facts.reason;
        return result;
    }
    // Canvas inference may retain the complete input, never assert exact physical
    // edges or certify a discarded margin. Measured geometry stays unrounded.
    ActivePictureBounds nominee=measured?facts.measuredBounds:ActivePictureBounds{};
    if(canvas) {
        nominee={0,0,source.width,source.height,source.width,source.height,
            double(source.width)/source.height,ActivePictureBounds::BarAxes::NONE};
    }
    try { evidence=VerifySuppliedActivePictureCandidate(source,nominee,budget); }
    catch(const AnalysisEvaluationCancelled&) {
        evidence=MissingSourcePictureEvidence(source,"source-spatial-candidate-verification-budget-exhausted");
    }
    if(!evidence.blackReference.evaluated && facts.blackReference.evaluated)
        evidence.blackReference=facts.blackReference;
    evidence.detector=measured?ActivePictureDetectorKind::SourceSpatialMeasured:ActivePictureDetectorKind::SourceSpatialCanvas;
    auto& physical=evidence.physicalEdges;
    physical.evaluated=true;physical.sourceGeneration=source.generation;physical.samples=facts.samples;
    physical.conservativeBounds=evidence.trustedBounds;
    physical.estimateAvailable=measured;
    physical.estimate=measured?facts.measuredBounds:ActivePictureBounds{};
    const bool full=measured && nominee.left==0 && nominee.top==0 &&
        nominee.right==source.width && nominee.bottom==source.height;
    physical.confidence=canvas?PhysicalPictureEdgeConfidence::SOURCE_CANVAS_ESTIMATE:
        full?PhysicalPictureEdgeConfidence::SOURCE_RASTER_EXTENT:PhysicalPictureEdgeConfidence::STRONG_ABRUPT_EDGE;
    physical.reason=facts.reason;
    return result;
}

SourcePictureDetectionResult DetectPreparedSourcePictureBounded(const AnalysisLumaSource& source,
    const ActivePictureFrameIdentity& identity) {
    const auto started=std::chrono::steady_clock::now();
    auto deadline=started+std::chrono::milliseconds(24);
    AnalysisEvaluationBudget budget([](void* value) {
        return std::chrono::steady_clock::now()>=*static_cast<std::chrono::steady_clock::time_point*>(value);
    },&deadline);
    SourcePictureDetectionResult result;
    try { result=DetectSourcePictureForWorker(source,&budget); }
    catch(const AnalysisEvaluationCancelled&) {
        result.evidence=MissingSourcePictureEvidence(source,"source-picture-worker-budget-cancelled");
    }
    auto trace=AnalysisTraceRecord(identity,nullptr,"source-picture-active",result.evidence.physicalEdges.reason);
    trace.resultCode=static_cast<int32_t>(result.evidence.detector);
    trace.flags=(result.evidence.available?1u:0u)|(result.observation?2u:0u);
    trace.metrics[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(result.observation) {
        const auto& facts=*result.observation;
        trace.flags|=(facts.complete?4u:0u)|(facts.rectangleAvailable?8u:0u)|
            (facts.canvasSupported?16u:0u)|(facts.blankMarginsUnresolved?32u:0u);
        trace.counters[0]=facts.samples;trace.counters[1]=facts.components.size();
        trace.counters[2]=static_cast<uint64_t>(facts.outcome);
        AnalysisTraceRect(trace.measured,facts.measuredBounds);
    }
    AnalysisTraceRect(trace.published,result.evidence.trustedBounds);
    AnalysisDiagnosticTrace::Emit(trace);return result;
}
