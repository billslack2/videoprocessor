#include <pch.h>
#include <SourcePictureDetector.h>
#include <OwnedSourcePictureObservationCache.h>
#include <AnalysisDiagnosticHooks.h>
#include <DebugLog.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

bool SourcePictureDetectorEnabled() {
    // One production geometry producer. A process environment cannot silently
    // restore the superseded nomination/OSD observer and its conflicting rules.
    // Legacy extraction remains an explicit reference API for offline tests.
    return true;
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
static SourcePictureDetectionResult VerifySourcePictureObservation(const AnalysisLumaSource& source,
    std::shared_ptr<const SourcePictureObservation> observation,const AnalysisEvaluationBudget* budget) {
    SourcePictureDetectionResult result;
    result.evidence=MissingSourcePictureEvidence(source);
    result.observation=std::move(observation);
    if(!result.observation)return result;
    const auto& facts=*result.observation;
    auto& evidence=result.evidence;
    evidence.reason=facts.reason;
    evidence.physicalEdges.reason=facts.reason;
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

SourcePictureDetectionResult DetectSourcePictureForWorker(const AnalysisLumaSource& source,
    const AnalysisEvaluationBudget* budget) {
    return VerifySourcePictureObservation(source,
        std::make_shared<const SourcePictureObservation>(MeasureSourcePicture(source,budget)),budget);
}

SourcePictureDetectionResult DetectPreparedSourcePictureBounded(const AnalysisLumaSource& source,
    const ActivePictureFrameIdentity& identity) {
    const auto started=std::chrono::steady_clock::now();
    auto deadline=started+std::chrono::milliseconds(24);
    AnalysisEvaluationBudget budget([](void* value) {
        return std::chrono::steady_clock::now()>=*static_cast<std::chrono::steady_clock::time_point*>(value);
    },&deadline);
    SourcePictureDetectionResult result;
    static thread_local OwnedSourcePictureObservationCache factsCache;
    try {
        const auto lookupStarted=std::chrono::steady_clock::now();
        auto ready=factsCache.Find(source,identity);
        const double lookupMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-lookupStarted).count();
        if(ready) {
            // Byte equality proves only geometry facts. Current exclusion and
            // subtitle verification still run with this frame's own identity.
            result=VerifySourcePictureObservation(source,std::move(ready),&budget);
            result.geometryReused=true;
        } else {
            result=DetectSourcePictureForWorker(source,&budget);
            if(result.observation && result.observation->complete)
                factsCache.Store(source,identity,result.observation);
        }
        result.geometryLookupMs=lookupMs;
    }
    catch(const AnalysisEvaluationCancelled&) {
        result.evidence=MissingSourcePictureEvidence(source,"source-picture-worker-budget-cancelled");
    }
    // Bind only an explicitly valid current source generation. Both queued
    // format-generation and live transport-generation adapters are supported.
    if(source.IsValid() && identity.transportGeneration && identity.sourceFormatGeneration &&
        identity.acceptedSequence && identity.analysisDomainKey && source.generation &&
        (source.generation==identity.transportGeneration || source.generation==identity.sourceFormatGeneration)) {
        result.evidence.currentSourceIdentity=identity;
        result.evidence.currentSourceIdentityAvailable=true;
    }
    auto trace=AnalysisTraceRecord(identity,nullptr,"source-picture-active",result.evidence.physicalEdges.reason);
    trace.resultCode=static_cast<int32_t>(result.evidence.detector);
    trace.flags=(result.evidence.available?1u:0u)|(result.observation?2u:0u);
    trace.metrics[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    trace.metrics[1]=result.geometryLookupMs;
    if(result.geometryReused)trace.flags|=64u;
    if(result.observation) {
        const auto& facts=*result.observation;
        trace.flags|=(facts.complete?4u:0u)|(facts.rectangleAvailable?8u:0u)|
            (facts.canvasSupported?16u:0u)|(facts.blankMarginsUnresolved?32u:0u);
        trace.flags|=facts.topologyComplete?512u:0u;
        if(!facts.topologyComplete && facts.components.size()>=512)trace.flags|=1024u;
        trace.counters[0]=facts.samples;trace.counters[1]=facts.components.size();
        trace.counters[2]=static_cast<uint64_t>(facts.outcome);
        const auto& dark=facts.darkTerminal;
        trace.flags|=(dark.evaluated?128u:0u)|(dark.accepted?256u:0u);
        trace.counters[3]=dark.evaluated?uint64_t(dark.side+1):0;
        trace.counters[4]=dark.supportingRays;trace.counters[5]=dark.supportingRegions;
        trace.counters[6]=static_cast<uint64_t>(source.format);trace.counters[7]=static_cast<uint64_t>(source.encoding);
        trace.metrics[2]=facts.blackReference.floor;trace.metrics[3]=facts.blackReference.threshold;
        trace.metrics[4]=dark.earlyMean;trace.metrics[5]=dark.earlyScore;
        trace.metrics[6]=dark.integratedMean;trace.metrics[7]=dark.integratedScore;
        for(unsigned i=0;i<4;++i)trace.metrics[8+i]=dark.regionalMean[i];
        trace.metrics[12]=static_cast<int>(source.range);trace.metrics[13]=static_cast<int>(source.colorspace);
        trace.metrics[14]=dark.lateMean;trace.metrics[15]=facts.lumaQuantum;
        AnalysisTraceRect(trace.measured,facts.measuredBounds);
    }
    if(result.observation) {
        const auto& contact=result.observation->rasterContact;
        trace.flags|=(contact.opposed?2048u:0u)|(contact.sparseEvaluated?4096u:0u)|(contact.sparseAccepted?8192u:0u)|
            (contact.insetVeto?16384u:0u)|(contact.glyphVeto?32768u:0u);
        trace.metrics[16]=contact.peakContrast;trace.metrics[17]=contact.contactPatches;
        trace.metrics[18]=contact.textureRegions;trace.metrics[19]=contact.sparseSide+1;
        for(unsigned i=0;i<4;++i)trace.metrics[20+i]=contact.textureSupport[i];
    }
    AnalysisTraceRect(trace.published,result.evidence.trustedBounds);
    // Low-rate experimental health telemetry separates completed source facts
    // from accepted crop authority; no pixels or presentation decisions change.
    struct Health {
        uint64_t tick=0,generation=0,jobs=0,measured=0,canvas=0,verified=0,budget=0,reused=0;
        double totalMs=0,peakMs=0,lookupMs=0;
    };
    static thread_local Health health;
    const auto now=GetTickCount64();
    if(!health.tick || health.generation!=source.generation) {
        health={};health.tick=now;health.generation=source.generation;
    }
    ++health.jobs;health.reused+=result.geometryReused;health.lookupMs+=result.geometryLookupMs;
    health.measured+=result.evidence.physicalEdges.estimateAvailable;
    health.canvas+=result.evidence.detector==ActivePictureDetectorKind::SourceSpatialCanvas;
    health.verified+=result.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED;
    health.budget+=result.evidence.reason.find("budget")!=std::string::npos;
    health.totalMs+=trace.metrics[0];health.peakMs=std::max(health.peakMs,trace.metrics[0]);
    if(now-health.tick>=2000) {
        const auto& edge=result.evidence.physicalEdges.estimate;
        DebugLog::Log("VP spatial detector interval: worker=%lu format=%s generation=%llu source=%llu jobs=%llu measured=%llu canvas=%llu unknown=%llu raw_verified=%llu budget_exhausted=%llu geometry_reused=%llu lookup_avg_ms=%.3f retained_bytes=%llu avg_ms=%.3f peak_ms=%.3f last_physical=%d,%d-%d,%d last_reason=%s",
            GetCurrentThreadId(),AnalysisLumaFormatName(source),source.generation,identity.acceptedSequence,
            health.jobs,health.measured,health.canvas,health.jobs-health.measured-health.canvas,health.verified,
            health.budget,health.reused,health.lookupMs/health.jobs,static_cast<unsigned long long>(factsCache.RetainedSourceBytes()),health.totalMs/health.jobs,health.peakMs,edge.left,edge.top,edge.right,edge.bottom,result.evidence.reason.c_str());
        health={};health.tick=now;health.generation=source.generation;
    }
    AnalysisDiagnosticTrace::Emit(trace);return result;
}
