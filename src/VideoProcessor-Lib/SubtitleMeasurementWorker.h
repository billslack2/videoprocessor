#pragma once
#include <SubtitleBoxLookahead.h>
#include <SubtitlePlanarBandSnapshot.h>
#include "vprenderer/ExperimentalAspectActive.h"
#include <SubtitleContinuitySampling.h>
#include <AnalysisDiagnosticHooks.h>
#include <DebugLog.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iterator>
#include <fstream>
#include <cstdlib>
#include <mutex>
#include <thread>

// The optional fault capture stays diagnostic: a current worker observation and
// a current shared picture are both required. This never grants crop authority.
inline bool IsSubtitleProofFailureCaptureCandidate(const AnalysisLumaSource& source,
    const SubtitleBoxObservation& key,const SubtitleBoxObservation& result)
{
    constexpr size_t MaxFaultCaptureBytes=64u*1024u*1024u;
    if(!source.IsValid() || source.dataBytes>MaxFaultCaptureBytes ||
        !key.sharedPicture.required ||
        !key.sharedPicture.AvailableFor(key.identity,source.width,source.height) ||
        !SameAnalysisFrameIdentity(key.identity,result.identity) ||
        !result.sharedPicture.Matches(key.sharedPicture) || !result.analyzed ||
        !result.sharedProbeAttempted || !result.sharedProbeDetected ||
        result.sharedProbeWorkLimit || result.text.workLimit ||
        result.text.detected || result.pendingRefresh || result.discontinuity)
        return false;
    const auto& shared=key.sharedPicture.bounds;
    return (shared.top>0 && result.pictureTop==0) ||
        (shared.bottom<source.height && result.pictureBottom==source.height);
}

inline bool ShouldCaptureSubtitleDiagnosticFrame(bool pathConfigured,bool sourceValid,
    bool proofFailureMode,bool allowDarkStartupCapture,uint64_t acceptedSequence,
    bool globalNearBlack,bool proofFailureCandidate)
{
    if(!pathConfigured || !sourceValid)return false;
    return proofFailureMode ? proofFailureCandidate :
        (allowDarkStartupCapture ? acceptedSequence>=240 : !globalNearBlack);
}

// One claim per linked module. The renderer DLL owns this sampler in normal use.
inline std::atomic_bool& SubtitleDiagnosticCaptureClaimed()
{
    static std::atomic_bool claimed{false};
    return claimed;
}

// Every source frame gets independent geometry and raw ink. Rechecking only
// old glyph rectangles cannot discover a new word/companion outside them, and
// copied ink must never count as fresh lookahead confirmation. Safe same-frame
// sampled-pixel/buffer reuse remains controlled by the detector's existing mode.
class SubtitleMeasurementSampler {
public:
    SubtitleBoxDetector::Diagnostics TakeDetectorDiagnostics() {return m_scanner.TakeDiagnostics();}
    SubtitleBoxObservation Measure(const AnalysisLumaSource& source,const SubtitleBoxObservation& key,
        const SubtitleBarTrackingReference& prior,double /*fps*/,
        const ActivePictureEvidence& nativeEvidence = {}, bool preparePublishedCaption = false,
        const ActivePictureFrameIdentity* nativeEvidenceIdentity = nullptr) {
        const auto started=std::chrono::steady_clock::now();
        if(m_analysisDomainKey!=key.identity.analysisDomainKey) {
            m_scanner.Reset();m_analysisDomainKey=key.identity.analysisDomainKey;
        }
        m_scanner.SetOptimizationMode(key.optimizationMode);
        m_scanner.SetScriptHint(key.scriptHint);
        m_scanner.SetNearBarDistance(key.nearBarDistance);
        auto currentPair=[&]() -> ActivePictureEvidence {
            if(nativeEvidenceIdentity && SameAnalysisFrameIdentity(*nativeEvidenceIdentity,key.identity))return nativeEvidence;
            if(!preparePublishedCaption)return {};
            if(m_haveCurrentPair && SameAnalysisFrameIdentity(m_currentPairIdentity,key.identity))return m_currentPair;
            m_currentPair=ExperimentalAspect::MeasureActiveTrial(source,key.identity);
            m_currentPairIdentity=key.identity;m_haveCurrentPair=true;
            return m_currentPair;
        };
        auto result=SubtitleBoxLookahead::Measure(m_scanner,source,{},key.identity,key.discontinuity,
            prior,key.policyGeneration,key.continuityGeneration,key.sharedPicture,currentPair);
        result.analysisStride=key.analysisStride;
        // Presentation consumes the AR owner's planes. Keep the existing
        // independent subtitle-assisted AR measurement separate: it may report
        // a real source aperture change but never replaces subtitle geometry.
        auto physical=result;
        bool regroupedOnPhysicalPlanes = false;
        if(result.sharedNativeGeometry) {
            const auto& bars=result.subtitleOnlyCurrentPairedSupport?result.legacyIndependentBars:result.barEvidence;
            physical.barEvidence=bars;physical.barAuthority=bars.available;
            physical.independentBarAnalysis=bars.available && bars.top>0 && bars.bottom<source.height;
            physical.pictureTop=bars.top;physical.pictureBottom=bars.bottom;
            physical.sharedNativeGeometry=false;physical.sharedNativePhysicalSupport=false;
            physical.inspectionCandidateUsed=false;
            physical.glyphLocalTop=physical.glyphLocalBottom=false;
            physical.glyphLocalVerifiedBounds={};physical.glyphLocalProofReason="independent-ar-measurement";
            physical.text.detected=result.sharedProbeDetected && !result.sharedProbeWorkLimit;
            const int tolerance=(std::max)(1,source.height/180);
            if(physical.independentBarAnalysis &&
                (std::abs(bars.top-result.pictureTop)>=tolerance ||
                 std::abs(bars.bottom-result.pictureBottom)>=tolerance)) {
                // Only changed native geometry needs another grouping pass.
                // Stable cues reuse this exact source's already measured ink.
                m_scanner.Reset();
                physical.text=m_scanner.Analyze(source,bars.top,bars.bottom,
                    key.identity.acceptedSequence,key.identity.viewportGeneration);
                physical.ink=m_scanner.InkSnapshot();
                regroupedOnPhysicalPlanes = true;
            }
        }
        if (!physical.inspectionCandidateUsed && physical.independentBarAnalysis && physical.text.detected && !physical.text.held &&
            !physical.text.workLimit) {
            const auto raw=nativeEvidence.available ? nativeEvidence : ExtractActivePictureEvidence(source);
            auto proofSource=source;proofSource.generation=result.identity.transportGeneration;
            result.subtitleArProof=EvaluateSubtitleAssistedVerticalBarProof(proofSource,raw,physical,
                result.identity,result.policyGeneration,result.continuityGeneration,result.scriptHint);
            if (result.subtitleArProof.accepted) {
                result.subtitleArDarkness=EvaluateActivePictureArDarkness(proofSource,result.subtitleArProof.evidence);
                // Keep only the grouping actually performed on the new planes.
                // The original result still belongs to the old shared picture.
                const auto& text = physical.text;
                if (preparePublishedCaption && regroupedOnPhysicalPlanes && !result.inspectionCandidateUsed) {
                    auto caption = std::make_shared<SubtitleBoxObservation>();
                    caption->identity = physical.identity;
                    caption->width = physical.width;
                    caption->height = physical.height;
                    caption->policyGeneration = physical.policyGeneration;
                    caption->continuityGeneration = physical.continuityGeneration;
                    caption->scriptHint = physical.scriptHint;
                    caption->nearBarDistance = physical.nearBarDistance;
                    caption->optimizationMode = physical.optimizationMode;
                    caption->analysisStride = result.analysisStride;
                    caption->analyzed = true;
                    caption->barAuthority = true;
                    caption->barEvidence = physical.barEvidence;
                    caption->pictureTop = physical.pictureTop;
                    caption->pictureBottom = physical.pictureBottom;
                    caption->text = text;
                    caption->ink = physical.ink;
                    AttachCanonicalSubtitleContinuityInk(*caption, source);
                    result.publishedPictureCaption = caption;
                    const auto& proof=result.subtitleArProof;
                    auto physicalBounds=proof.candidate;
                    physicalBounds.top=caption->pictureTop & ~1;
                    physicalBounds.bottom=(std::min)(source.height,(caption->pictureBottom+1) & ~1);
                    const bool currentGuard=SubtitleCaptionScanGuardMatchesPhysicalBounds(proof,physicalBounds);
                    const bool nominalProof=proof.candidate.top==physicalBounds.top &&
                        proof.candidate.bottom==physicalBounds.bottom;
                    const bool precedingGuard=!currentGuard && nominalProof && m_lastGuardHint &&
                        SubtitleBoxLookahead::GuardHintMatchesCurrentPhysical(*m_lastGuardHint,result,
                            physicalBounds.top,physicalBounds.bottom);
                    const auto& guard=precedingGuard?m_lastGuardHint->guard:proof.candidate;
                    const bool needGuardScan=precedingGuard || (currentGuard &&
                        (!SubtitleBoxLookahead::CompletePublishedCaption(*caption) ||
                         SubtitleBoxLookahead::CaptionNeedsPublishedPlaneGrouping(*caption,guard.top,guard.bottom)));
                    const char* guardOutcome=currentGuard || nominalProof ? "physical-grouping-transferable" : "physical-planes-outside-proof";
                    if(needGuardScan) {
                        // One bounded worker pass on the candidate planes. An immediate predecessor
                        // may nominate coordinates only after this source proves the same physical bars.
                        auto guarded=std::make_shared<SubtitleBoxObservation>(*caption);
                        m_scanner.Reset();
                        guarded->pictureTop=guard.top;
                        guarded->pictureBottom=guard.bottom;
                        guarded->text=m_scanner.Analyze(source,guard.top,guard.bottom,
                            key.identity.acceptedSequence,key.identity.viewportGeneration);
                        guarded->ink=m_scanner.InkSnapshot();
                        guarded->continuityEvidence.reset();
                        if(SubtitleBoxLookahead::CompletePublishedCaption(*guarded)) {
                            AttachCanonicalSubtitleContinuityInk(*guarded,source);
                            if(precedingGuard)guarded->guardScanHint=m_lastGuardHint;
                            result.publishedGuardCaption=std::move(guarded);
                            guardOutcome="exact-guard-ready";
                        } else {
                            guardOutcome=guarded->text.diagnosticReason?guarded->text.diagnosticReason:"incomplete";
                        }
                    }
                    DEBUGLOG_ENHANCED("VP subtitle alternate: stage=prepare reason=%s mono_us=%lld generation=%llu "
                        "source=%llu domain=%llu policy=%llu continuity=%llu physical=%d,%d proof=%d,%d candidate=%d,%d "
                        "guard_origin=%llu guard_certified=%d guard_required=%d guard_ready=%d physical_box=%d,%d-%d,%d",
                        guardOutcome,std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count(),
                        key.identity.transportGeneration,key.identity.acceptedSequence,key.identity.analysisDomainKey,
                        key.policyGeneration,key.continuityGeneration,caption->pictureTop,caption->pictureBottom,
                        proof.candidate.top,proof.candidate.bottom,guard.top,guard.bottom,
                        precedingGuard?m_lastGuardHint->physical.identity.acceptedSequence:key.identity.acceptedSequence,
                        currentGuard?1:0,needGuardScan?1:0,result.publishedGuardCaption?1:0,
                        text.bounds.left,text.bounds.top,text.bounds.right,text.bounds.bottom);
                    if(currentGuard) {
                        auto hint=std::make_shared<SubtitleGuardScanHint>();
                        hint->physical={key.identity,key.width,key.height,physicalBounds.top,physicalBounds.bottom,
                            key.policyGeneration,key.continuityGeneration,key.scriptHint,true};
                        hint->guard=guard;
                        hint->nearBarDistance=key.nearBarDistance;
                        hint->optimizationMode=key.optimizationMode;
                        hint->analysisStride=key.analysisStride;
                        m_lastGuardHint=std::move(hint);
                    }
                }
            }
        }
        // Explicit per-process diagnostic opt-in: save one native
        // input without changing capture, subtitle settings or presentation.
        static const std::string capturePath=[] {
            char* value=nullptr;size_t size=0;
            _dupenv_s(&value,&size,"VP_SUBTITLE_CAPTURE_PATH");
            const std::string path=value?value:"";std::free(value);return path;
        }();
        static const bool captureDark=[] {
            char* value=nullptr;size_t size=0;
            _dupenv_s(&value,&size,"VP_SUBTITLE_CAPTURE_ALLOW_DARK");
            const bool enabled=value && std::string(value)=="1";
            std::free(value);return enabled;
        }();
        static const bool captureOnProofFailure=[] {
            char* value=nullptr;size_t size=0;
            _dupenv_s(&value,&size,"VP_SUBTITLE_CAPTURE_ON_PROOF_FAILURE");
            const bool enabled=value && std::string(value)=="1";
            std::free(value);return enabled;
        }();
        // Dark-scene diagnostics must not be vetoed by the classification being
        // investigated. Fault mode waits for the exact current worker failure.
        if(captureOnProofFailure && SubtitleDiagnosticCaptureClaimed().load())
            m_capturedDiagnostic=true;
        if(!m_capturedDiagnostic && !capturePath.empty() && source.IsValid() &&
            ShouldCaptureSubtitleDiagnosticFrame(true,true,captureOnProofFailure,captureDark,
                key.identity.acceptedSequence,
                captureOnProofFailure ? false : EvaluateActivePictureGlobalNearBlack(source).nearBlack,
                captureOnProofFailure && IsSubtitleProofFailureCaptureCandidate(source,key,result)) &&
            (!captureOnProofFailure || !SubtitleDiagnosticCaptureClaimed().exchange(true))) {
            m_capturedDiagnostic=true;
            std::ofstream pixels(capturePath,std::ios::binary);
            pixels.write(reinterpret_cast<const char*>(source.data),source.dataBytes);
            std::ofstream metadata(std::string(capturePath)+".txt");
            metadata<<source.width<<' '<<source.height<<' '<<source.rowBytes<<' '<<source.chromaRowBytes<<' '
                <<static_cast<int>(source.format)<<' '<<static_cast<int>(source.encoding)<<' '
                <<static_cast<int>(source.colorspace)<<' '<<result.pictureTop<<' '<<result.pictureBottom<<' '
                <<static_cast<int>(key.scriptHint)<<' '<<static_cast<int>(source.range)<<'\n';
            if(captureOnProofFailure) {
                const auto& id=key.identity;
                const auto& shared=key.sharedPicture.bounds;
                const auto& prior=result.incomingBarTrackingReference;
                const auto& bars=result.barEvidence;
                std::ofstream diagnostic(std::string(capturePath)+".diagnostic.txt");
                diagnostic<<"capture_mode=proof_failure\n"
                    <<"identity transport="<<id.transportGeneration
                    <<" sequence="<<id.acceptedSequence
                    <<" source_frame="<<id.sourceFrameNumber
                    <<" capture_timestamp="<<id.captureTimestamp
                    <<" format="<<id.sourceFormatGeneration
                    <<" domain="<<id.analysisDomainKey
                    <<" viewport="<<id.viewportGeneration
                    <<" renderer="<<id.rendererGeneration<<'\n'
                    <<"policy="<<key.policyGeneration
                    <<" continuity="<<key.continuityGeneration
                    <<" near_bar="<<key.nearBarDistance
                    <<" optimization="<<key.optimizationMode
                    <<" script="<<static_cast<int>(key.scriptHint)<<'\n'
                    <<"shared_sequence="<<key.sharedPicture.identity.acceptedSequence
                    <<" shared_bounds="<<shared.left<<','<<shared.top<<','<<shared.right<<','<<shared.bottom<<'\n'
                    <<"prior valid="<<prior.valid
                    <<" sequence="<<prior.identity.acceptedSequence
                    <<" edges="<<prior.pictureTop<<','<<prior.pictureBottom<<'\n'
                    <<"result analyzed="<<result.analyzed
                    <<" bar_authority="<<result.barAuthority
                    <<" independent="<<result.independentBarAnalysis
                    <<" picture="<<result.pictureTop<<','<<result.pictureBottom
                    <<" text_detected="<<result.text.detected
                    <<" text_work_limit="<<result.text.workLimit
                    <<" text_reason="<<(result.text.diagnosticReason?result.text.diagnosticReason:"")
                    <<" components="<<result.text.diagnosticComponentCount
                    <<" near_bar_gap="<<result.text.diagnosticNearBarGap
                    <<" top_search_complete="<<result.text.topSearchComplete
                    <<" bottom_search_complete="<<result.text.bottomSearchComplete<<'\n'
                    <<"probe attempted="<<result.sharedProbeAttempted
                    <<" detected="<<result.sharedProbeDetected
                    <<" work_limit="<<result.sharedProbeWorkLimit
                    <<" reason="<<(result.sharedProbeReason?result.sharedProbeReason:"")
                    <<" anchor="<<result.sharedProbeAnchor.left<<','<<result.sharedProbeAnchor.top<<','
                    <<result.sharedProbeAnchor.right<<','<<result.sharedProbeAnchor.bottom<<'\n'
                    <<"bars reason="<<(bars.reason?bars.reason:"")
                    <<" available="<<bars.available
                    <<" edges="<<bars.top<<','<<bars.bottom
                    <<" support="<<bars.topSupport<<','<<bars.bottomSupport
                    <<" boundary_support="<<bars.topBoundarySupport<<','<<bars.bottomBoundarySupport
                    <<" black_fraction="<<bars.topBlackFraction<<','<<bars.bottomBlackFraction
                    <<" max_intrusion="<<bars.topMaxIntrusion<<','<<bars.bottomMaxIntrusion
                    <<" reference_support="<<bars.topReferenceBoundarySupport<<','<<bars.bottomReferenceBoundarySupport
                    <<" reference_fraction="<<bars.topReferenceBlackFraction<<','<<bars.bottomReferenceBlackFraction
                    <<" reference_intrusion="<<bars.topReferenceMaxIntrusion<<','<<bars.bottomReferenceMaxIntrusion
                    <<" reference_exact="<<bars.topRevalidatedAtReference<<','<<bars.bottomRevalidatedAtReference
                    <<" reference_nearby="<<bars.topRevalidatedNearby<<','<<bars.bottomRevalidatedNearby
                    <<" nearby_delta="<<bars.topReferenceDelta<<','<<bars.bottomReferenceDelta
                    <<" nearby_probes="<<bars.nearbyProbes
                    <<" samples="<<bars.samples<<'\n';
                pixels.flush();metadata.flush();diagnostic.flush();
                DebugLog::Log("Subtitle fault capture: success=%d bytes=%zu sequence=%llu domain=%llu",
                    pixels.good() && metadata.good() && diagnostic.good() ? 1 : 0,
                    source.dataBytes,
                    static_cast<unsigned long long>(key.identity.acceptedSequence),
                    static_cast<unsigned long long>(key.identity.analysisDomainKey));
            }
        }
        AttachCanonicalSubtitleContinuityInk(result,source);
        // Keep one speculative layout seed on the optional worker. Only a later
        // match to a cue actually accepted by presentation can use this result.
        bandSeed={};
        if (preparePublishedCaption && key.compareSkippedCueBands && key.analysisStride==2 && result.analyzed &&
            !result.discontinuity && !result.inspectionCandidateUsed && result.sharedNativeGeometry &&
            result.barAuthority && result.text.detected && !result.text.held && !result.text.workLimit &&
            result.pictureTop>0 && result.pictureBottom<source.height && result.text.bounds.Valid()) {
            const bool top=result.text.bounds.bottom<=result.pictureTop;
            const bool bottom=result.text.bounds.top>=result.pictureBottom;
            if (top!=bottom) {
                // Compare the complete mattes and the adjacent placement region.
                // The final renderer separately checks its actual destination fits.
                const int inset=source.height/8;
                bandTopEnd=(result.pictureTop+(top?inset:2)+1)&~1;
                bandBottomStart=(result.pictureBottom-(bottom?inset:2))&~1;
                if (bandSnapshot.Capture(source,bandTopEnd,bandBottomStart)) {
                    bandSeed=key.identity;bandPolicy=key.policyGeneration;
                    bandContinuity=key.continuityGeneration;bandScript=key.scriptHint;
                    bandNear=key.nearBarDistance;bandOptimization=key.optimizationMode;
                }
            }
        }
        result.analysisMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return result;
    }
    bool HasCadenceBandSeed() const { return bandSeed.acceptedSequence!=0; }
    SubtitleBoxObservation CompareCadenceBands(const AnalysisLumaSource& source,
        const SubtitleBoxObservation& key) const {
        auto result=key;
        const auto& id=key.identity;
        if (!key.compareSkippedCueBands || !key.cadenceSkipped || key.analysisStride!=2 || key.discontinuity || !HasCadenceBandSeed() ||
            id.acceptedSequence<=bandSeed.acceptedSequence || id.acceptedSequence-bandSeed.acceptedSequence!=1 ||
            id.captureTimestamp<=bandSeed.captureTimestamp || id.transportGeneration!=bandSeed.transportGeneration ||
            id.sourceFormatGeneration!=bandSeed.sourceFormatGeneration || id.analysisDomainKey!=bandSeed.analysisDomainKey ||
            id.rendererGeneration!=bandSeed.rendererGeneration || id.viewportGeneration!=bandSeed.viewportGeneration ||
            key.policyGeneration!=bandPolicy || key.continuityGeneration!=bandContinuity ||
            key.scriptHint!=bandScript || key.nearBarDistance!=bandNear || key.optimizationMode!=bandOptimization) return result;
        result.bandComparisonComplete=true;
        if (bandSnapshot.Matches(source)) {
            result.matchingBandTemplate=bandSeed;
            result.bandComparedSource=source;result.bandComparedOwner=source.backingOwner;
            result.bandComparedSource.backingOwner.reset();
            result.verifiedTopEnd=bandTopEnd;result.verifiedBottomStart=bandBottomStart;
        }
        result.text.diagnosticReason=result.matchingBandTemplate.acceptedSequence?
            "exact-current-cue-bands":"current-cue-bands-changed";
        return result;
    }
private:
    SubtitlePlanarBandSnapshot bandSnapshot;
    ActivePictureFrameIdentity bandSeed{};
    uint64_t bandPolicy=0,bandContinuity=0;
    SubtitleScriptHint bandScript=SubtitleScriptHint::Latin;
    int bandTopEnd=0,bandBottomStart=0,bandNear=0,bandOptimization=0;
    SubtitleBoxDetector m_scanner;
    std::shared_ptr<const SubtitleGuardScanHint> m_lastGuardHint;
    uint64_t m_analysisDomainKey=0;
    bool m_haveCurrentPair=false;
    ActivePictureFrameIdentity m_currentPairIdentity{};
    ActivePictureEvidence m_currentPair;
    bool m_capturedDiagnostic=false;
};

// Bounded CPU-only work. Jobs own their source storage; rendering never waits
// for a scan or consumes a result for another source frame or policy epoch.
class SubtitleMeasurementWorker {
public:
    struct Diagnostics {
        // Completed-job cohort totals, not CPU time or exact wall-window utilization.
        // Detect duration is nested in Analyze, which is nested in job elapsed.
        uint64_t completedJobs=0,successfulJobs=0,jobsWithoutAnalyze=0;
        uint64_t analyzeCalls=0,detectPasses=0,rejectedInputs=0,sameFrameReuses=0;
        double analyzeElapsedMs=0,analyzePeakMs=0,detectElapsedMs=0,detectPeakMs=0;
        double jobElapsedMs=0,jobPeakMs=0,failedJobElapsedMs=0;
        // Legacy completion classifications retained for older callers only.
        uint64_t fullScans=0,refreshes=0,failed=0,dropped=0,requestMisses=0;
        uint64_t submitted=0,exactReads=0,deduplicated=0,expired=0;
        double fullMs=0,fullPeakMs=0,refreshMs=0,refreshPeakMs=0,queueWaitMs=0,queueWaitPeakMs=0;
        size_t pending=0,ready=0;bool active=false;
    };
    Diagnostics TakeDiagnostics() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto out=m_diagnostics;out.pending=m_pending.size();out.ready=m_ready.size();out.active=m_active;
        m_diagnostics={};return out;
    }
    // Join worker and renderer records across threads on one monotonic clock.
    static int64_t TraceMonotonicUs() noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void Trace(const SubtitleBoxObservation& key,const char* stage,const char* reason="",uint64_t mismatch=0) const noexcept {
        auto r=AnalysisTraceRecord(key.identity,this,stage,reason);r.kind=AnalysisDiagnosticRecordKindV1::Worker;
        r.policyGeneration=key.policyGeneration;r.continuityGeneration=key.continuityGeneration;
        r.nearBarDistance=key.nearBarDistance;r.optimizationMode=key.optimizationMode;r.scriptHint=static_cast<uint32_t>(key.scriptHint);
        r.sourceWidth=key.width;r.sourceHeight=key.height;r.discontinuity=key.discontinuity;
        r.flags=(key.analyzed?1u:0u)|(key.text.detected?2u:0u)|(key.sharedPicture.required?4u:0u)|
            (key.sharedPicture.AvailableFor(key.identity,key.width,key.height)?8u:0u)|(key.pictureAuthorityUnavailableForMeasurement?16u:0u)|
            (key.sharedPicture.inspectionCandidate.AvailableFor(key.identity,key.width,key.height,key.policyGeneration,key.continuityGeneration)?32u:0u)|
            (key.inspectionCandidateUsed?64u:0u)|(key.sharedNativeGeometry?128u:0u)|
            (key.sharedNativePhysicalSupport?256u:0u)|(SubtitleBoxLookahead::HasCurrentSharedGlyphSupport(key)?512u:0u)|
            (key.pendingBarGlyphsVerified?1024u:0u)|(key.pendingBarGlyphsLocal?2048u:0u);
        r.counters[0]=mismatch;r.counters[1]=key.sharedPicture.identity.acceptedSequence;
        r.counters[2]=key.sharedPicture.inspectionCandidate.originIdentity.acceptedSequence;
        r.counters[3]=key.sharedPicture.inspectionCandidate.nativeGeometryRevision;
        AnalysisTraceRect(r.candidate,key.sharedPicture.inspectionCandidate.bounds);
        r.resultCode=key.text.lineCount;AnalysisTraceRect(r.subtitle,key.text.bounds);
        r.metrics[0]=key.analysisMs;r.metrics[1]=key.pictureTop;r.metrics[2]=key.pictureBottom;
        r.metrics[3]=key.glyphLocalMismatchX;r.metrics[4]=key.glyphLocalMismatchY;r.metrics[5]=key.glyphLocalMismatchLuma;
        AnalysisDiagnosticTrace::Emit(r);
    }
    using Work=std::function<SubtitleBoxObservation(SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&)>;
    ~SubtitleMeasurementWorker() { Stop(); }
    bool HasCadenceBandSeed() const { return m_bandSeedReady.load(std::memory_order_relaxed); }
    static bool SameKey(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
        if((a.sharedPicture.inspectionCandidate.valid &&
                !a.sharedPicture.inspectionCandidate.AvailableFor(a.identity,a.width,a.height,a.policyGeneration,a.continuityGeneration)) ||
            (b.sharedPicture.inspectionCandidate.valid &&
                !b.sharedPicture.inspectionCandidate.AvailableFor(b.identity,b.width,b.height,b.policyGeneration,b.continuityGeneration)))
            return false;
        const bool samePicture=a.sharedPicture.Matches(b.sharedPicture);
        // A worker request with no usable shared snapshot follows the same
        // exact-frame raw-bar path regardless of which unavailable snapshot
        // was published. This lets active/pending work coalesce safely; a
        // newly available authority still requires a separate scan.
        const bool sameUnavailablePictureScan=
            !a.sharedPicture.inspectionCandidate.valid && !b.sharedPicture.inspectionCandidate.valid &&
            a.pictureAuthorityUnavailableForMeasurement &&
            b.pictureAuthorityUnavailableForMeasurement &&
            a.sharedPicture.required && b.sharedPicture.required &&
            !a.sharedPicture.AvailableFor(a.identity,a.width,a.height) &&
            !b.sharedPicture.AvailableFor(b.identity,b.width,b.height);
        return SameAnalysisFrameIdentity(a.identity,b.identity) &&
            a.analysisStride==b.analysisStride && a.compareSkippedCueBands==b.compareSkippedCueBands &&
            a.width==b.width && a.height==b.height && a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode && a.scriptHint==b.scriptHint &&
            a.policyGeneration==b.policyGeneration &&
            (a.continuityGeneration==b.continuityGeneration ||
                (!a.pictureAuthorityUnavailableForMeasurement &&
                    !b.pictureAuthorityUnavailableForMeasurement &&
                    !a.independentBarAnalysis && !b.independentBarAnalysis &&
                    a.sharedPicture.required && b.sharedPicture.required)) &&
            a.discontinuity==b.discontinuity && (samePicture || sameUnavailablePictureScan);
    }
    // The head has advanced; queued scans for older transport frames cannot
    // become current. The active worker is left alone and remains nonblocking.
    void BeginSourceWindow(const ActivePictureFrameIdentity& head) {
        LifecycleEvents<PendingCapacity> events;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_windowHead.transportGeneration>head.transportGeneration ||
            (m_windowHead.transportGeneration==head.transportGeneration &&
                m_windowHead.acceptedSequence>=head.acceptedSequence))return;
        m_windowHead=head;
        for(auto i=m_pending.begin();i!=m_pending.end();) {
            const auto& identity=i->key.identity;
            if(BeforeWindow(identity,head)) {
                events.Add(i->key,"drop","source-window-expired");
                Trace(i->key,"worker-drop","source-window-expired");
                i=m_pending.erase(i);
                ++m_diagnostics.expired;
            } else ++i;
        }
    }
    // Lookahead and presentation can inspect the same exact-frame result.
    // Rebinding modifies only this copy; the cached observation is immutable.
    bool TryRead(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,
        bool backlogRetirementOnly=false, const AnalysisLumaSource* publishedSource=nullptr,
        const char** alternateOutcome=nullptr) {
        return ReadReady(key,out,false,backlogRetirementOnly,publishedSource,alternateOutcome);
    }
    bool TryTake(const SubtitleBoxObservation& key,SubtitleBoxObservation& out) {
        return ReadReady(key,out,true,false,nullptr,nullptr);
    }
    void Submit(const SubtitleBoxObservation& key,Work work) {
        LifecycleEvents<2> events;
        auto requestKey=key;
        // QueueSubtitleMeasurement submits only valid prepared sources. Mark
        // the request's scan path here so active and pending jobs can coalesce
        // before Measure has produced an observation.
        requestKey.pictureAuthorityUnavailableForMeasurement=
            requestKey.sharedPicture.required &&
            !requestKey.sharedPicture.AvailableFor(
                requestKey.identity,requestKey.width,requestKey.height);
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_stopping) {
            events.Add(requestKey,"reject","stopping");
            return;
        }
        if(BeforeWindow(requestKey.identity,m_windowHead)) {
            events.Add(requestKey,"reject","source-window-expired");
            Trace(requestKey,"worker-drop","source-window-expired");
            ++m_diagnostics.expired;return;
        }
        auto duplicate=[&](const char* reason) {
            events.Add(requestKey,"skip",reason);
            Trace(requestKey,"worker-dedup",reason);
            ++m_diagnostics.deduplicated;
        };
        if(m_active && SameKey(m_activeKey,requestKey))return duplicate("active");
        for(const auto& job:m_pending)if(SameKey(job.key,requestKey))return duplicate("queued");
        for(const auto& ready:m_ready)if(SameKey(ready,requestKey))return duplicate("ready");
        if(m_pending.size()>=PendingCapacity) {
            // A speculative comparison must never evict a queued detector scan.
            if (requestKey.cadenceSkipped) {
                events.Add(requestKey,"drop","comparison-capacity");
                ++m_diagnostics.dropped;return;
            }
            // Keep the nearest accepted source. A new current-frame request may
            // replace the farthest future; a later future cannot evict the head.
            auto leastUseful=m_pending.begin();
            for(auto i=std::next(m_pending.begin());i!=m_pending.end();++i) {
                const auto& x=i->key.identity;
                const auto& worst=leastUseful->key.identity;
                if(x.transportGeneration<worst.transportGeneration ||
                    (x.transportGeneration==worst.transportGeneration &&
                        x.acceptedSequence>worst.acceptedSequence))
                    leastUseful=i;
            }
            const auto& worst=leastUseful->key.identity;
            const auto& incoming=requestKey.identity;
            if(incoming.transportGeneration>worst.transportGeneration ||
                (incoming.transportGeneration==worst.transportGeneration &&
                    incoming.acceptedSequence<worst.acceptedSequence)) {
                events.Add(leastUseful->key,"drop","lower-priority-replaced");
                Trace(leastUseful->key,"worker-drop","lower-priority-replaced");
                m_pending.erase(leastUseful);
            } else {
                events.Add(requestKey,"reject","future-capacity");
                Trace(requestKey,"worker-drop","future-capacity");
                ++m_diagnostics.dropped;
                return;
            }
            ++m_diagnostics.dropped;
        }
        Trace(requestKey,"worker-submit");
        // Execute newer transport generations first, then the nearest source
        // sequence. Equal-frame contexts keep submission order.
        const auto place=std::find_if(m_pending.begin(),m_pending.end(),
            [&requestKey](const Job& queued) {
                const auto& incoming=requestKey.identity;
                const auto& existing=queued.key.identity;
                return incoming.transportGeneration>existing.transportGeneration ||
                    (incoming.transportGeneration==existing.transportGeneration &&
                        incoming.acceptedSequence<existing.acceptedSequence);
            });
        m_pending.insert(place,{requestKey,std::move(work)});
        ++m_diagnostics.submitted;
        events.Add(requestKey,"submit","accepted");
        if(!m_thread.joinable())m_thread=std::thread([this]{ Run(); });
        m_changed.notify_one();
    }
    void Stop() {
        {
            LifecycleEvents<PendingCapacity> events;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping=true;
            if(DebugLog::IsEnhancedLoggingEnabled())
                for(const auto& job:m_pending)events.Add(job.key,"drop","stopping");
            m_pending.clear();
        }
        m_changed.notify_all();
        if(m_thread.joinable())m_thread.join();
    }
private:
    static constexpr size_t PendingCapacity=8;

    static bool BeforeWindow(const ActivePictureFrameIdentity& source,const ActivePictureFrameIdentity& head) {
        return source.transportGeneration<head.transportGeneration ||
            (source.transportGeneration==head.transportGeneration && source.acceptedSequence<head.acceptedSequence);
    }

    // Capture the event at the queue transition. Format and enqueue its text after
    // releasing the worker mutex. When disabled, leave all record storage untouched.
    template<size_t Capacity>
    class LifecycleEvents {
        struct Event {
            const char* stage;
            const char* reason;
            int64_t monoUs;
            uint64_t generation,source,frame,capture,format,renderer,viewport,domain,policy,continuity;
            uint64_t pictureSource,candidateSource,candidateRevision;
            int left,top,right,bottom;
            int width,height,nearBar,optimization,script;
            unsigned stride;
            bool discontinuity,pictureRequired,pictureUnavailable,candidate;
            int candidateTop,candidateBottom;
            bool haveResult,analyzed,detected,workLimit,alternate;
            int pictureTop,pictureBottom,lines;
            double waitMs,workMs;
            char analysisReason[48];
        };
        std::array<Event,Capacity> m_events;
        size_t m_count=0;
    public:
        void Add(const SubtitleBoxObservation& key,const char* stage,const char* reason,
            const SubtitleBoxObservation* result=nullptr,double waitMs=0,double workMs=0) {
            if(!DebugLog::IsEnhancedLoggingEnabled())return;
            const auto& id=key.identity;
            const auto& shared=key.sharedPicture;
            const auto& hint=shared.inspectionCandidate;
            auto& event=m_events[m_count++];
            event={
                stage,reason,TraceMonotonicUs(),id.transportGeneration,id.acceptedSequence,id.sourceFrameNumber,
                id.captureTimestamp,id.sourceFormatGeneration,id.rendererGeneration,id.viewportGeneration,
                id.analysisDomainKey,key.policyGeneration,key.continuityGeneration,shared.identity.acceptedSequence,
                hint.originIdentity.acceptedSequence,hint.nativeGeometryRevision,
                shared.bounds.left,shared.bounds.top,shared.bounds.right,shared.bounds.bottom,
                key.width,key.height,key.nearBarDistance,key.optimizationMode,static_cast<int>(key.scriptHint),
                key.analysisStride,key.discontinuity,shared.required,key.pictureAuthorityUnavailableForMeasurement,
                hint.valid,hint.bounds.top,hint.bounds.bottom,result!=nullptr,result && result->analyzed,result && result->text.detected,
                result && result->text.workLimit,result && result->publishedPictureCaption,
                result?result->pictureTop:0,result?result->pictureBottom:0,result?result->text.lineCount:0,waitMs,workMs,{}
            };
            if(result)AnalysisTraceText(event.analysisReason,result->analyzed?result->text.diagnosticReason:"analysis-failed");
        }
        ~LifecycleEvents() {
            for(size_t i=0;i<m_count;++i) {
                const auto& e=m_events[i];
                DebugLog::Log("VP subtitle worker: stage=%s reason=%s mono_us=%lld generation=%llu "
                    "source=%llu source_frame=%llu capture_timestamp=%llu format_generation=%llu "
                    "renderer_generation=%llu viewport=%llu domain=%llu policy=%llu continuity=%llu "
                    "width=%d height=%d stride=%u near_bar=%d optimization=%d script=%d discontinuity=%d "
                    "shared_source=%llu shared_bounds=%d,%d,%d,%d shared_required=%d shared_unavailable=%d "
                    "candidate=%d candidate_source=%llu candidate_revision=%llu candidate_planes=%d,%d "
                    "result_present=%d analyzed=%d detected=%d work_limit=%d alternate=%d result_planes=%d,%d lines=%d analysis_reason=%s "
                    "queue_wait_ms=%.3f work_ms=%.3f",
                    e.stage,e.reason,e.monoUs,e.generation,e.source,e.frame,e.capture,e.format,e.renderer,
                    e.viewport,e.domain,e.policy,e.continuity,e.width,e.height,e.stride,e.nearBar,e.optimization,
                    e.script,e.discontinuity,e.pictureSource,e.left,e.top,e.right,e.bottom,e.pictureRequired,
                    e.pictureUnavailable,e.candidate,e.candidateSource,e.candidateRevision,e.candidateTop,
                    e.candidateBottom,e.haveResult,e.analyzed,e.detected,e.workLimit,e.alternate,e.pictureTop,
                    e.pictureBottom,e.lines,e.haveResult?e.analysisReason:"none",e.waitMs,e.workMs);
            }
        }
    };

    bool ReadReady(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,bool consume,
        bool backlogRetirementOnly,const AnalysisLumaSource* publishedSource,const char** alternateOutcome) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(alternateOutcome)*alternateOutcome=nullptr;
        uint64_t mismatches=0;
        bool foundPublishedCaption=false;
        for(auto i=m_ready.begin();i!=m_ready.end();++i) {
            auto candidate=*i;
            if (!SubtitleBoxLookahead::RebindIndependentPicture(candidate,key.sharedPicture)) {
                // The post-publication read can select a completed alternate
                // even when the caller never received the old shared preview.
                // Prefer a ready scan requested on the published planes.
                if (publishedSource && !foundPublishedCaption && candidate.publishedPictureCaption &&
                    candidate.identity.transportGeneration==key.identity.transportGeneration &&
                    candidate.identity.acceptedSequence==key.identity.acceptedSequence &&
                    SubtitleBoxLookahead::UsePublishedPictureCaption(candidate,key.sharedPicture,
                        *publishedSource,key.identity,key.policyGeneration,key.continuityGeneration,key.scriptHint,
                        alternateOutcome)) {
                    if(SameKey(candidate,key)) {
                        out=std::move(candidate);
                        foundPublishedCaption=true;
                    } else if(alternateOutcome) {
                        *alternateOutcome="worker-request-key-mismatch";
                    }
                }
                mismatches|=16ull;continue;
            }
            bool reboundFromBacklog=false;
            if(candidate.continuityGeneration!=key.continuityGeneration &&
                backlogRetirementOnly) {
                // Preserve all immutable key fields. Only exact-source, fully
                // independent glyph/bar scans can carry across this drop.
                const bool sameSource=SameAnalysisFrameIdentity(candidate.identity,key.identity) &&
                    candidate.policyGeneration==key.policyGeneration &&
                    candidate.scriptHint==key.scriptHint &&
                    candidate.nearBarDistance==key.nearBarDistance &&
                    candidate.optimizationMode==key.optimizationMode &&
                    candidate.width==key.width && candidate.height==key.height &&
                    candidate.discontinuity==key.discontinuity &&
                    candidate.sharedPicture.Matches(key.sharedPicture);
                reboundFromBacklog=sameSource &&
                    SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(
                        candidate,key.continuityGeneration,true);
            }
            if (!SameKey(candidate,key)) {
                mismatches|=(!SameActivePictureFrameIdentity(candidate.identity,key.identity)?1ull:0ull)|
                    (candidate.identity.analysisDomainKey!=key.identity.analysisDomainKey?2ull:0ull)|
                    (candidate.policyGeneration!=key.policyGeneration?4ull:0ull)|
                    (candidate.continuityGeneration!=key.continuityGeneration?8ull:0ull)|
                    (!candidate.sharedPicture.Matches(key.sharedPicture)?16ull:0ull)|
                    (candidate.scriptHint!=key.scriptHint?32ull:0ull)|
                    (candidate.discontinuity!=key.discontinuity?64ull:0ull)|
                    (candidate.nearBarDistance!=key.nearBarDistance || candidate.optimizationMode!=key.optimizationMode?128ull:0ull)|
                    (candidate.width!=key.width || candidate.height!=key.height?256ull:0ull);
                continue;
            }
            if(!reboundFromBacklog &&
                !SubtitleBoxLookahead::RebindIndependentMeasurement(candidate,key.continuityGeneration)){mismatches|=512ull;continue;}
            Trace(candidate,consume?"worker-consumed":"worker-read",
                reboundFromBacklog?"backlog-current-scan-rebound":"exact-key-accepted");
            if(alternateOutcome && *alternateOutcome)*alternateOutcome="direct-scan-preferred";
            out=std::move(candidate);
            if(consume)m_ready.erase(i);
            else ++m_diagnostics.exactReads;
            return true;
        }
        if (foundPublishedCaption) {
            Trace(out,"worker-read","published-picture-caption");
            ++m_diagnostics.exactReads;
            return true;
        }
        Trace(key,"worker-miss",m_ready.empty()?"no-ready-results":"no-compatible-result",mismatches);
        ++m_diagnostics.requestMisses;return false;
    }
    struct Job {SubtitleBoxObservation key;Work work;std::chrono::steady_clock::time_point queued=std::chrono::steady_clock::now();};
    void Run() {
        SubtitleMeasurementSampler scanner;
        SubtitleBarTrackingReference previous;
        for(;;) {
            Job job;
            {
                LifecycleEvents<1> events;
                std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait(lock,[this]{return m_stopping || !m_pending.empty();});
                if(m_stopping)return;
                job=std::move(m_pending.front());m_pending.pop_front();
                m_active=true;m_activeKey=job.key;
                events.Add(job.key,"start","dequeued");
            }
            Trace(job.key,"worker-start");
            const auto workStart=std::chrono::steady_clock::now();
            const double waited=std::chrono::duration<double,std::milli>(workStart-job.queued).count();
            SubtitleBoxObservation result=job.key;
            try {
                result=job.work(scanner,previous);
                if (!job.key.cadenceSkipped) previous=SubtitleBoxLookahead::AdvanceBarTrackingReference(result);
                m_bandSeedReady.store(scanner.HasCadenceBandSeed(),std::memory_order_relaxed);
            } catch(...) {result.analyzed=false; m_bandSeedReady.store(false,std::memory_order_relaxed);}
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-workStart).count();
            const auto detector=scanner.TakeDetectorDiagnostics();
            Trace(result,"worker-complete",result.analyzed || result.bandComparisonComplete ?
                result.text.diagnosticReason : (result.cadenceSkipped?"no-band-comparison":"analysis-failed"));
            {
                LifecycleEvents<2> events;
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_diagnostics.completedJobs;
                if(result.analyzed)++m_diagnostics.successfulJobs;
                else m_diagnostics.failedJobElapsedMs+=ms;
                if(!detector.analyzeCalls)++m_diagnostics.jobsWithoutAnalyze;
                m_diagnostics.analyzeCalls+=detector.analyzeCalls;
                m_diagnostics.detectPasses+=detector.detectPasses;
                m_diagnostics.rejectedInputs+=detector.rejectedInputs;
                m_diagnostics.sameFrameReuses+=detector.sameFrameReuses;
                m_diagnostics.analyzeElapsedMs+=detector.analyzeElapsedMs;
                m_diagnostics.analyzePeakMs=(std::max)(m_diagnostics.analyzePeakMs,detector.analyzePeakMs);
                m_diagnostics.detectElapsedMs+=detector.detectElapsedMs;
                m_diagnostics.detectPeakMs=(std::max)(m_diagnostics.detectPeakMs,detector.detectPeakMs);
                m_diagnostics.jobElapsedMs+=ms;
                m_diagnostics.jobPeakMs=(std::max)(m_diagnostics.jobPeakMs,ms);
                m_diagnostics.queueWaitMs+=waited;m_diagnostics.queueWaitPeakMs=(std::max)(m_diagnostics.queueWaitPeakMs,waited);
                if(!result.analyzed) {if(!job.key.cadenceSkipped)++m_diagnostics.failed;}
                else if(result.analysisRefresh){++m_diagnostics.refreshes;m_diagnostics.refreshMs+=ms;m_diagnostics.refreshPeakMs=(std::max)(m_diagnostics.refreshPeakMs,ms);}
                else {++m_diagnostics.fullScans;m_diagnostics.fullMs+=ms;m_diagnostics.fullPeakMs=(std::max)(m_diagnostics.fullPeakMs,ms);}
                m_active=false;
                if(!m_stopping) {
                    // A skipped-frame comparison must not displace a completed detector scan.
                    const auto sameKind=[&](const SubtitleBoxObservation& item) {
                        return item.cadenceSkipped==job.key.cadenceSkipped;
                    };
                    if(std::count_if(m_ready.begin(),m_ready.end(),sameKind)>=(job.key.cadenceSkipped?4:16)) {
                        const auto oldest=std::find_if(m_ready.begin(),m_ready.end(),sameKind);
                        events.Add(*oldest,"evict","ready-capacity");m_ready.erase(oldest);
                    }
                    m_ready.push_back(std::move(result));
                    if(DebugLog::IsEnhancedLoggingEnabled()) {
                        const char* reason=BeforeWindow(job.key.identity,m_windowHead)?"cached-after-window":"cached";
                        events.Add(job.key,"complete",reason,&m_ready.back(),waited,ms);
                    }
                } else {
                    events.Add(job.key,"complete","discarded-stopping",&result,waited,ms);
                }
            }
        }
    }
    std::atomic<bool> m_bandSeedReady{false};
    Diagnostics m_diagnostics;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::thread m_thread;
    std::deque<Job> m_pending;
    std::deque<SubtitleBoxObservation> m_ready;
    ActivePictureFrameIdentity m_windowHead;
    SubtitleBoxObservation m_activeKey;
    bool m_active=false,m_stopping=false;
};


// A late asynchronous result is not negative subtitle evidence. Bridge only a
// bounded delay, with fresh bar proof and unchanged native pixels in every
// accepted glyph rectangle. No OCR, spatial search, wait or GPU readback here.
class SubtitlePendingMeasurementGuard {
public:
    enum ContextMismatch : uint32_t {
        ContextDiscontinuity=1u<<0, ContextSource=1u<<1,
        ContextDimensions=1u<<2, ContextTransport=1u<<3,
        ContextFormat=1u<<4, ContextDomain=1u<<5,
        ContextViewport=1u<<6, ContextRenderer=1u<<7,
        ContextNearBar=1u<<8, ContextScript=1u<<9,
        ContextPolicy=1u<<10, ContextContinuity=1u<<11, ContextOptimization=1u<<12
    };
    struct Diagnostics {
        uint32_t contextMismatch=0;
        size_t glyphSamplesChecked=0;
        int glyphMismatchX=-1, glyphMismatchY=-1;
        bool sharedPictureMatches=false, sharedPictureCurrent=false;
        bool barsAvailable=false, topRevalidated=false, bottomRevalidated=false;
        bool backlogReassociated=false;
        int expectedTop=0, expectedBottom=0, measuredTop=0, measuredBottom=0;
    };
    void Reset() { m_reference={};m_samples.clear();m_elapsed=0;m_last={};m_diagnostics={};
        m_certifiedSeed={};m_certifiedPolicy=m_certifiedContinuity=0;m_consumedCertificate={}; }
    const char* DecisionReason() const { return m_reason; }
    const Diagnostics& DecisionDiagnostics() const { return m_diagnostics; }
    // Only accepted extraction may seed the late-worker fallback. A held cue
    // can be more complete than either current grouping or lookahead pruning;
    // never turn those partial measurements into a smaller replacement cue.
    bool RememberAccepted(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const AnalysisLumaSource& source,double frameMs,int holdMs,
        const SubtitleAcceptedCueSeedCertificate* existingCueCertificate=nullptr) {
        const auto contains=[](const SubtitleBoxRect& outer,const SubtitleBoxRect& inner) {
            return outer.Valid() && inner.Valid() && outer.left<=inner.left &&
                outer.top<=inner.top && outer.right>=inner.right && outer.bottom>=inner.bottom;
        };
        const auto covers=[&](const SubtitleBoxResult& measured) {
            if(!measured.detected || measured.workLimit || measured.lineCount<accepted.lineCount ||
                measured.lineCount>3)return false;
            for(int line=0;line<accepted.lineCount;++line) {
                const auto& glyph=accepted.lineBounds[line];bool found=false;
                for(int other=0;other<measured.lineCount;++other)
                    found|=contains(measured.lineBounds[other],glyph);
                if(!found)return false;
            }
            const auto& oldCard=accepted.capturePanelMeasured?accepted.capturePanel:accepted.sourcePanel;
            const auto& newCard=measured.capturePanelMeasured?measured.capturePanel:measured.sourcePanel;
            return !oldCard.Valid() || contains(newCard,oldCard);
        };
        // A completed independent scan may restore an ALREADY acquired cue
        // after backlog retirement even though Resolve has no future rows yet.
        // Only the tracker's exact current-cue certificate can replace that
        // future grouping vote; raw current extraction must still cover all rows.
        const auto& current=preview.current;
        const bool independentCurrentCue=current.independentBarAnalysis && !current.analysisRefresh &&
            !current.barTrackingAuthority && !current.currentAnchorUsesTrackedEdge &&
            current.barEvidence.available && !current.barEvidence.topRevalidatedAtReference &&
            !current.barEvidence.topRevalidatedNearby && !current.barEvidence.bottomRevalidatedAtReference &&
            !current.barEvidence.bottomRevalidatedNearby && !current.barEvidence.captionBackedTop &&
            !current.barEvidence.captionBackedBottom &&
            current.pictureTop==current.barEvidence.top && current.pictureBottom==current.barEvidence.bottom &&
            current.ink && current.ink->rawEvidenceComplete &&
            !current.text.held && !accepted.held && !accepted.revised;
        const bool certifiedExistingCue=independentCurrentCue && existingCueCertificate &&
            existingCueCertificate->Matches(preview,accepted);
        const bool consumedCertificate=independentCurrentCue && m_consumedCertificate.Matches(preview,accepted);
        const bool resolvedCovers=covers(preview.text);
        const bool localBarSeed=SubtitleBoxLookahead::CanSeedSharedLocalBarGlyphs(current);
        if(!preview.available || !preview.current.analyzed || preview.current.pendingRefresh ||
            preview.current.discontinuity ||
            (!localBarSeed && (preview.current.glyphLocalTop || preview.current.glyphLocalBottom || !preview.current.barAuthority)) ||
            !source.IsValid() || source.width!=preview.current.width || source.height!=preview.current.height ||
            !accepted.detected || accepted.confirmationPending || accepted.workLimit || accepted.lineCount<1 || accepted.lineCount>3 ||
            !covers(preview.current.text) || (!resolvedCovers && !certifiedExistingCue && !consumedCertificate)) {
            return Reject("accepted-seed-incomplete");
        }
        if(!resolvedCovers && consumedCertificate) {
            // The tracker intentionally exports no new certificate on a repeated
            // presentation. Preserve the exact prior seed without renewing time.
            m_reason="accepted-certificate-already-consumed";return false;
        }
        const auto& old=m_certifiedSeed;const auto& now=current.identity;
        if(!resolvedCovers && certifiedExistingCue && old.acceptedSequence &&
            old.transportGeneration==now.transportGeneration && old.sourceFormatGeneration==now.sourceFormatGeneration &&
            old.analysisDomainKey==now.analysisDomainKey && old.viewportGeneration==now.viewportGeneration &&
            old.rendererGeneration==now.rendererGeneration && m_certifiedPolicy==preview.policyGeneration &&
            m_certifiedContinuity==preview.continuityGeneration && now.acceptedSequence<=old.acceptedSequence) {
            // Replaying the same exported certificate cannot renew the lease.
            m_reason="accepted-certificate-already-consumed";return false;
        }
        auto seed=preview;seed.current.text=accepted;seed.text=accepted;
        Resolve(seed,source,frameMs,holdMs);
        if(!m_samples.empty() && !resolvedCovers && certifiedExistingCue) {
            m_certifiedSeed=now;m_certifiedPolicy=preview.policyGeneration;
            m_certifiedContinuity=preview.continuityGeneration;
            m_consumedCertificate=*existingCueCertificate;
            m_reason="accepted-existing-cue-certified";
        }
        return !m_samples.empty();
    }
    bool Resolve(SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        double frameMs,int holdMs,
        const ActivePictureFrameIdentity* backlogTemplateIdentity=nullptr,
        bool backlogRetirementOnly=false) {
        m_diagnostics={};
        auto key=preview.current;
        if(!key.analyzed)SubtitleBoxLookahead::BindNativeGeometryHint(key);
        if(preview.available) {
            if(key.pendingRefresh)return false; // A repeated presentation cannot renew the budget.
            const bool localSeed=SubtitleBoxLookahead::CanSeedSharedLocalBarGlyphs(key);
            if(key.analyzed && (localSeed || (!key.glyphLocalTop && !key.glyphLocalBottom && key.barAuthority)) &&
                key.text.detected && !key.text.workLimit) {
                m_reference=preview;m_samples.clear();m_elapsed=0;m_last=key.identity;
                const int step=SubtitleBoxDetector::SamplingStep(source.width,source.height);
                if(localSeed) {
                    const auto& roi=key.glyphLocalVerifiedBounds;
                    if(!key.ink || key.ink->step!=step || key.ink->sourceRows.size()!=size_t(key.ink->height))
                        return Reject("seed-local-grid-unavailable");
                    int priorY=-1;
                    for(int y:key.ink->sourceRows) {
                        if(y<=priorY || y<0 || y>=source.height)return Reject("seed-local-grid-unavailable");
                        priorY=y;
                        if(y<roi.top || y>=roi.bottom)continue;
                        for(int x=((roi.left+step-1)/step)*step;x<roi.right;x+=step) {
                            AnalysisLumaSample value;
                            if(m_samples.size()>=32768 || !source.Sample(x,y,value))return Reject("seed-sample-unavailable");
                            m_samples.push_back({x,y,value});
                        }
                    }
                    if(m_samples.empty())return Reject("seed-local-grid-unavailable");
                } else for(int line=0;line<key.text.lineCount && line<3;++line) {
                    const auto r=key.text.lineBounds[line];
                    for(int y=(std::max)(0,r.top-step);y<(std::min)(source.height,r.bottom+step);y+=step)
                        for(int x=(std::max)(0,r.left-step);x<(std::min)(source.width,r.right+step);x+=step) {
                            AnalysisLumaSample value;
                            if(m_samples.size()>=32768 || !source.Sample(x,y,value)) return Reject("seed-sample-unavailable");
                            m_samples.push_back({x,y,value});
                        }
                }
                m_reason="accepted-seed";
            } else Reject("seed-not-detected");
            return false;
        }
        const auto& old=m_reference.current;
        const auto& a=m_last;const auto& b=key.identity;
        frameMs=std::isfinite(frameMs)&&frameMs>0?frameMs:1000.0/60;
        if(m_samples.empty())return Reject("no-accepted-seed");
        // Presentation may skip captured frames under load. Current native
        // pixels still prove the fixed accepted cue, but spend the full source
        // time gap and never permit repeats, backwards frames or epoch changes.
        if(b.acceptedSequence<=a.acceptedSequence)return Reject("non-forward-source");
        const double elapsedMs=frameMs*double(b.acceptedSequence-a.acceptedSequence);
        if(m_elapsed+elapsedMs>(std::max)(0,(std::min)(1000,holdMs)))return Reject("pending-budget-expired");
        uint32_t context=0;
        if(key.discontinuity)context|=ContextDiscontinuity;
        if(!source.IsValid())context|=ContextSource;
        if(source.width!=key.width || source.height!=key.height ||
            key.width!=old.width || key.height!=old.height)context|=ContextDimensions;
        if(a.transportGeneration!=b.transportGeneration)context|=ContextTransport;
        if(a.sourceFormatGeneration!=b.sourceFormatGeneration)context|=ContextFormat;
        if(a.analysisDomainKey!=b.analysisDomainKey)context|=ContextDomain;
        if(a.viewportGeneration!=b.viewportGeneration)context|=ContextViewport;
        if(a.rendererGeneration!=b.rendererGeneration)context|=ContextRenderer;
        if(key.nearBarDistance!=old.nearBarDistance)context|=ContextNearBar;
        if(key.scriptHint!=old.scriptHint)context|=ContextScript;
        if(key.optimizationMode!=old.optimizationMode)context|=ContextOptimization;
        if(key.policyGeneration!=old.policyGeneration ||
            preview.policyGeneration!=key.policyGeneration)context|=ContextPolicy;
        if(key.continuityGeneration!=old.continuityGeneration ||
            preview.continuityGeneration!=key.continuityGeneration)context|=ContextContinuity;
        m_diagnostics.contextMismatch=context;
        // The timeline witness authorizes only this queue-only continuity
        // change. It is tied to the last pixel-validated source, which advances
        // on pending frames; the original seed and its lease age do not renew.
        const uint64_t sourceGap=b.acceptedSequence-a.acceptedSequence;
        const bool backlogReassociation=context==ContextContinuity &&
            backlogRetirementOnly && backlogTemplateIdentity &&
            SameAnalysisFrameIdentity(a,*backlogTemplateIdentity) &&
            old.continuityGeneration && key.continuityGeneration &&
            preview.continuityGeneration==key.continuityGeneration &&
            key.continuityGeneration>old.continuityGeneration &&
            b.captureTimestamp>a.captureTimestamp &&
            (!(a.sourceFrameNumber || b.sourceFrameNumber) ||
                (a.sourceFrameNumber && b.sourceFrameNumber>a.sourceFrameNumber &&
                 b.sourceFrameNumber-a.sourceFrameNumber==sourceGap));
        if(context && !backlogReassociation)return Reject("pending-context-changed");
        // A completed native-band comparison is a current-source certificate,
        // not an old pixel buffer. Never fall back to sparse old samples when
        // a completed comparison disagrees or belongs to another template.
        if(key.bandComparisonComplete) {
            const auto& compared=key.bandComparedSource;
            // Queued analysis uses the format generation and the live view uses
            // the transport generation of the same immutable frame (SeedQueued).
            const bool matchingGeneration=compared.generation==source.generation ||
                (compared.generation==key.identity.sourceFormatGeneration &&
                 source.generation==key.identity.transportGeneration);
            const bool sameSource=matchingGeneration &&
                !key.bandComparedOwner.owner_before(source.backingOwner) &&
                !source.backingOwner.owner_before(key.bandComparedOwner) &&
                compared.data && compared.data==source.data &&
                compared.dataBytes==source.dataBytes && compared.width==source.width &&
                compared.height==source.height && compared.rowBytes==source.rowBytes &&
                compared.chromaRowBytes==source.chromaRowBytes && compared.format==source.format &&
                compared.encoding==source.encoding && compared.colorspace==source.colorspace &&
                compared.range==source.range &&
                compared.rgbBitAlignment==source.rgbBitAlignment && !compared.backingOwner;
            if(!sameSource)return Reject("current-band-source-mismatch");
            if(context || !SameAnalysisFrameIdentity(key.matchingBandTemplate,old.identity) ||
                !key.sharedPicture.required || !key.sharedPicture.Matches(old.sharedPicture) ||
                !SubtitleBoxLookahead::HasSharedNativeGeometry(key) ||
                !SubtitleBoxLookahead::HasSharedNativeGeometry(old) ||
                key.pictureTop!=old.pictureTop || key.pictureBottom!=old.pictureBottom)
                return Reject("current-band-template-mismatch");
            auto current=m_reference;
            auto& pending=current.current;
            pending.identity=b;pending.sharedPicture=key.sharedPicture;
            pending.sharedNativeGeometry=true;pending.inspectionCandidateUsed=key.inspectionCandidateUsed;
            pending.pictureTop=key.pictureTop;pending.pictureBottom=key.pictureBottom;
            pending.policyGeneration=key.policyGeneration;pending.continuityGeneration=key.continuityGeneration;
            pending.pendingRefresh=true;pending.pendingBarGlyphsVerified=true;
            pending.pendingBarGlyphsLocal=false;pending.pendingExactBandGlyphsVerified=true;
            pending.bandComparisonComplete=true;pending.matchingBandTemplate=key.matchingBandTemplate;
            pending.verifiedTopEnd=key.verifiedTopEnd;pending.verifiedBottomStart=key.verifiedBottomStart;
            pending.bandComparedSource=key.bandComparedSource;pending.bandComparedOwner=key.bandComparedOwner;
            pending.discontinuity=false;pending.analysisRefresh=false;
            pending.barEvidence={};pending.barAuthority=false;pending.independentBarAnalysis=false;
            pending.sharedNativePhysicalSupport=false;pending.barTrackingAuthority=false;
            pending.currentAnchorUsesTrackedEdge=false;pending.hasBarTrackingReference=false;
            pending.glyphLocalTop=pending.glyphLocalBottom=false;pending.glyphLocalVerifiedBounds={};
            pending.glyphSupportIdentity=b;pending.glyphSupportPolicy=key.policyGeneration;
            pending.glyphSupportContinuity=key.continuityGeneration;
            pending.glyphSupportTop=key.pictureTop;pending.glyphSupportBottom=key.pictureBottom;
            pending.text.topSearchComplete=pending.text.bottomSearchComplete=false;
            pending.incomingBarTrackingReference={};pending.subtitleArProof={};pending.subtitleArDarkness={};
            pending.barStripClearance={};pending.analysisMs=0;
            if(!SubtitleBoxLookahead::HasPendingSharedBarGlyphSupport(pending))
                return Reject("current-band-glyph-coverage-unavailable");
            m_elapsed+=elapsedMs;m_last=b;
            current.futureAvailable=false;current.following={};current.followingCount=0;current.matchingFrames=1;
            current.currentLinesConfirmed=false;current.newScanMs=0;current.newScanFrames=0;
            preview=std::move(current);m_reason="verified-current-native-bands";return true;
        }
        const bool localReference=SubtitleBoxLookahead::CanSeedSharedLocalBarGlyphs(old);
        int currentLocalBlackFloor=1023;
        for(const auto& sample:m_samples) {
            AnalysisLumaSample now;
            ++m_diagnostics.glyphSamplesChecked;
            if(!source.Sample(sample.x,sample.y,now) ||
                std::abs(int(now.luma)-sample.value.luma)>8 ||
                std::abs(int(now.chromaU)-sample.value.chromaU)>8 ||
                std::abs(int(now.chromaV)-sample.value.chromaV)>8) {
                m_diagnostics.glyphMismatchX=sample.x;
                m_diagnostics.glyphMismatchY=sample.y;
                return Reject("current-pixels-changed");
            }
            if(localReference && now.luma<=96 && std::abs(int(now.chromaU)-512)<=48 &&
                std::abs(int(now.chromaV)-512)<=48)
                currentLocalBlackFloor=(std::min)(currentLocalBlackFloor,int(now.luma));
        }
        if(localReference && currentLocalBlackFloor>96)return Reject("current-local-backing-unavailable");
        SubtitleBarEvidence bars;
        bool pendingSharedBar=false;
        m_diagnostics.expectedTop=old.pictureTop;
        m_diagnostics.expectedBottom=old.pictureBottom;
        if(key.sharedPicture.required) {
            m_diagnostics.sharedPictureMatches=key.sharedPicture.Matches(old.sharedPicture);
            m_diagnostics.sharedPictureCurrent=SubtitleBoxLookahead::HasSharedNativeGeometry(key);
            const int tolerance=(std::max)(1,source.height/180);
            const auto& oldBounds=old.inspectionCandidateUsed?old.sharedPicture.inspectionCandidate.bounds:old.sharedPicture.bounds;
            const auto& nextBounds=key.inspectionCandidateUsed?key.sharedPicture.inspectionCandidate.bounds:key.sharedPicture.bounds;
            const bool stableHint=oldBounds.left==nextBounds.left && oldBounds.right==nextBounds.right &&
                oldBounds.rasterWidth==nextBounds.rasterWidth && oldBounds.rasterHeight==nextBounds.rasterHeight &&
                SubtitleBoxLookahead::HasSharedNativeGeometry(old) &&
                m_diagnostics.sharedPictureCurrent &&
                std::abs(old.pictureTop-key.pictureTop)<tolerance &&
                std::abs(old.pictureBottom-key.pictureBottom)<tolerance;
            if(!stableHint && (!m_diagnostics.sharedPictureMatches ||
                !key.sharedPicture.AvailableFor(key.identity,source.width,source.height)))
                return Reject("shared-picture-changed");
            const auto physical=localReference?SubtitleBarEvidence{}:ExtractSubtitleBarEvidence(source);
            if(stableHint) {
                auto pending=old;
                pending.identity=b;pending.sharedPicture=key.sharedPicture;
                pending.sharedNativeGeometry=true;pending.inspectionCandidateUsed=key.inspectionCandidateUsed;
                pending.pictureTop=key.pictureTop;pending.pictureBottom=key.pictureBottom;
                pending.policyGeneration=key.policyGeneration;pending.continuityGeneration=key.continuityGeneration;
                pending.pendingRefresh=true;pending.pendingBarGlyphsVerified=true;
                pending.pendingBarGlyphsLocal=localReference;
                if(localReference) {pending.independentBarAnalysis=false;pending.sharedNativePhysicalSupport=false;
                    pending.glyphLocalBlackFloor=currentLocalBlackFloor;}
                pending.discontinuity=false;pending.analysisRefresh=false;
                pending.barEvidence=physical;pending.barAuthority=physical.available;
                pending.barTrackingAuthority=false;pending.currentAnchorUsesTrackedEdge=false;
                pending.glyphSupportIdentity=b;pending.glyphSupportPolicy=key.policyGeneration;
                pending.glyphSupportContinuity=key.continuityGeneration;
                pending.glyphSupportTop=key.pictureTop;pending.glyphSupportBottom=key.pictureBottom;
                pendingSharedBar=SubtitleBoxLookahead::HasPendingSharedBarGlyphSupport(pending);
            }
            if(localReference && !pendingSharedBar)return Reject("pending-local-geometry-changed");
            if(pendingSharedBar)bars=physical;
            else {
                if(!m_diagnostics.sharedPictureMatches ||
                    !key.sharedPicture.AvailableFor(key.identity,source.width,source.height))
                    return Reject("shared-picture-changed");
                bars=ResolveCurrentSubtitleAnalysisBars(source,physical,
                    key.sharedPicture.bounds.top,key.sharedPicture.bounds.bottom,nullptr,nullptr,false);
                if(!bars.available || bars.topRevalidatedAtReference || bars.topRevalidatedNearby ||
                    bars.bottomRevalidatedAtReference || bars.bottomRevalidatedNearby ||
                    bars.top!=old.pictureTop || bars.bottom!=old.pictureBottom)
                    return Reject("current-bars-changed");
            }
            m_diagnostics.barsAvailable=bars.available;
            m_diagnostics.topRevalidated=bars.topRevalidatedAtReference;
            m_diagnostics.bottomRevalidated=bars.bottomRevalidatedAtReference;
            m_diagnostics.measuredTop=bars.top;m_diagnostics.measuredBottom=bars.bottom;
        } else {
            bars=ExtractSubtitleBarEvidence(source);
            m_diagnostics.barsAvailable=bars.available;
            m_diagnostics.topRevalidated=bars.topRevalidatedAtReference;
            m_diagnostics.bottomRevalidated=bars.bottomRevalidatedAtReference;
            m_diagnostics.measuredTop=bars.top;
            m_diagnostics.measuredBottom=bars.bottom;
            if(!bars.available || bars.top!=old.pictureTop || bars.bottom!=old.pictureBottom) return Reject("current-bars-changed");
        }
        m_elapsed+=elapsedMs;m_last=b;
        // Rebase only a validated backlog continuity change after unchanged
        // CURRENT glyphs and physical bars pass. Ordinary pending refreshes
        // preserve the accepted wrapper metadata, samples and elapsed time.
        if(backlogReassociation) {
            m_reference.current.continuityGeneration=key.continuityGeneration;
            m_reference.continuityGeneration=key.continuityGeneration;
        }
        preview=m_reference;preview.current.identity=b;preview.current.discontinuity=false;
        preview.current.sharedPicture=key.sharedPicture;
        preview.current.barEvidence=bars;preview.current.barTrackingAuthority=false;
        preview.current.currentAnchorUsesTrackedEdge=false;preview.current.hasBarTrackingReference=false;
        preview.current.analysisMs=0;preview.current.analysisRefresh=false;
        preview.current.pendingRefresh=true;
        preview.current.pendingBarGlyphsVerified=pendingSharedBar;
        preview.current.pendingBarGlyphsLocal=pendingSharedBar && localReference;
        if(pendingSharedBar) {
            preview.current.sharedNativeGeometry=true;
            preview.current.sharedNativePhysicalSupport=false;
            preview.current.inspectionCandidateUsed=key.inspectionCandidateUsed;
            preview.current.pictureTop=key.pictureTop;preview.current.pictureBottom=key.pictureBottom;
            preview.current.barAuthority=bars.available;preview.current.independentBarAnalysis=false;
            preview.current.glyphLocalTop=preview.current.glyphLocalBottom=false;
            if(!localReference)preview.current.glyphLocalVerifiedBounds={};
            else preview.current.glyphLocalBlackFloor=currentLocalBlackFloor;
            preview.current.glyphSupportIdentity=b;
            preview.current.glyphSupportPolicy=key.policyGeneration;
            preview.current.glyphSupportContinuity=key.continuityGeneration;
            preview.current.glyphSupportTop=key.pictureTop;preview.current.glyphSupportBottom=key.pictureBottom;
            preview.current.text.topSearchComplete=preview.current.text.bottomSearchComplete=false;
        }
        preview.current.incomingBarTrackingReference={};
        preview.current.subtitleArProof={};preview.current.subtitleArDarkness={};
        preview.current.barStripClearance={};
        preview.futureAvailable=false;preview.following={};preview.followingCount=0;preview.matchingFrames=1;
        preview.currentLinesConfirmed=false;preview.newScanMs=0;preview.newScanFrames=0;
        m_diagnostics.backlogReassociated=backlogReassociation;
        m_reason=backlogReassociation ? "backlog-verified-current-pixels" : "verified-current-pixels";
        return true;
    }
private:
    bool Reject(const char* reason) {
        const auto diagnostics=m_diagnostics;
        const auto certified=m_certifiedSeed;const auto policy=m_certifiedPolicy,continuity=m_certifiedContinuity;
        const auto certificate=m_consumedCertificate;
        Reset();
        m_certifiedSeed=certified;m_certifiedPolicy=policy;m_certifiedContinuity=continuity;
        m_consumedCertificate=certificate;
        m_diagnostics=diagnostics;
        m_reason=reason;
        return false;
    }
    const char* m_reason="reset";
    Diagnostics m_diagnostics;
    struct Sample { int x,y;AnalysisLumaSample value; };
    SubtitleBoxPreview m_reference;
    SubtitleAcceptedCueSeedCertificate m_consumedCertificate;
    ActivePictureFrameIdentity m_last,m_certifiedSeed;
    uint64_t m_certifiedPolicy=0,m_certifiedContinuity=0;
    std::vector<Sample> m_samples;
    double m_elapsed=0;
};
