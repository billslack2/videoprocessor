#pragma once
#include <AnalysisCompletionSignal.h>
#include <SourcePictureDetector.h>
#include <SubtitleBoxLookahead.h>
#include <SubtitleCadenceGuard.h>
#include <AnalysisDiagnosticHooks.h>
#include <DebugLog.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
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

inline bool TryClaimSubtitleDiagnosticCapture(bool proofFailureMode)
{
    (void)proofFailureMode; // All capture modes write the same module-owned file.
    return !SubtitleDiagnosticCaptureClaimed().exchange(true);
}

// Every source frame gets independent geometry and raw ink. Rechecking only
// old glyph rectangles cannot discover a new word/companion outside them, and
// copied ink must never count as fresh lookahead confirmation. Safe same-frame
// sampled-pixel/buffer reuse remains controlled by the detector's existing mode.
class SubtitleMeasurementSampler {
public:
    void Reset() { m_scanner.Reset();m_analysisDomainKey=0; }
    SubtitleBoxObservation Measure(const AnalysisLumaSource& source,const SubtitleBoxObservation& key,
        const SubtitleBarTrackingReference& prior,double /*fps*/,
        const ActivePictureEvidence& nativeEvidence = {}) {
        const auto started=std::chrono::steady_clock::now();
        if(m_analysisDomainKey!=key.identity.analysisDomainKey) {
            m_scanner.Reset();m_analysisDomainKey=key.identity.analysisDomainKey;
        }
        m_scanner.SetOptimizationMode(key.optimizationMode);
        m_scanner.SetScriptHint(key.scriptHint);
        m_scanner.SetNearBarDistance(key.nearBarDistance);
        auto result=SubtitleBoxLookahead::Measure(m_scanner,source,{},key.identity,key.discontinuity,
            prior,key.policyGeneration,key.continuityGeneration,key.sharedPicture);
        result.analysisStride=key.analysisStride;
        const bool currentInspectionMask=SourcePictureDetectorEnabled() &&
            SubtitleBoxLookahead::HasCurrentInspectionGlyphMeasurement(result);
        if ((!result.inspectionCandidateUsed || currentInspectionMask) && result.text.detected && !result.text.held && !result.text.workLimit) {
            // Geometry and current glyph ownership are independent facts. Active
            // mode verifies the spatial nominee through its occlusion; a caption
            // never nominates a replacement rectangle.
            auto proofSource=source;proofSource.generation=result.identity.transportGeneration;
            if(SourcePictureDetectorEnabled()) {
                result.subtitleArProof=VerifySuppliedActivePictureCandidateWithSubtitle(proofSource,nativeEvidence,result,
                    result.identity,result.policyGeneration,result.continuityGeneration,result.scriptHint);
                auto trace=AnalysisTraceRecord(result.identity,nullptr,"source-picture-caption-proof",result.subtitleArProof.reason);
                trace.policyGeneration=result.policyGeneration;trace.continuityGeneration=result.continuityGeneration;
                trace.resultCode=static_cast<int32_t>(result.subtitleArProof.evidence.detector);
                trace.flags=(result.subtitleArProof.accepted?1u:0u)|(nativeEvidence.physicalEdges.estimateAvailable?2u:0u);
                AnalysisTraceRect(trace.measured,nativeEvidence.physicalEdges.estimate);
                AnalysisTraceRect(trace.published,result.subtitleArProof.candidate);
                AnalysisDiagnosticTrace::Emit(trace);
            } else if(result.independentBarAnalysis) {
                const auto raw=nativeEvidence.available ? nativeEvidence : ExtractActivePictureEvidence(source);
                result.subtitleArProof=EvaluateSubtitleAssistedVerticalBarProof(proofSource,raw,result,
                    result.identity,result.policyGeneration,result.continuityGeneration,result.scriptHint);
            }
            if (result.subtitleArProof.accepted)
                result.subtitleArDarkness=EvaluateActivePictureArDarkness(proofSource,result.subtitleArProof.evidence);
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
        if(SubtitleDiagnosticCaptureClaimed().load())
            m_capturedDiagnostic=true;
        if(!m_capturedDiagnostic && !capturePath.empty() && source.IsValid() &&
            ShouldCaptureSubtitleDiagnosticFrame(true,true,captureOnProofFailure,captureDark,
                key.identity.acceptedSequence,
                captureOnProofFailure ? false : EvaluateActivePictureGlobalNearBlack(source).nearBlack,
                captureOnProofFailure && IsSubtitleProofFailureCaptureCandidate(source,key,result)) &&
            TryClaimSubtitleDiagnosticCapture(captureOnProofFailure)) {
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
        result.analysisMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return result;
    }
private:
    SubtitleBoxDetector m_scanner;
    uint64_t m_analysisDomainKey=0;
    bool m_capturedDiagnostic=false;
};

// Bounded CPU-only work. Jobs own their source storage. Current-source preparation
// may await completion within its enqueue-derived deadline; presentation never
// consumes a result for another source frame or policy epoch.
class SubtitleMeasurementWorker {
public:
    struct Diagnostics {
        uint64_t fullScans=0,refreshes=0,failed=0,dropped=0,requestMisses=0;
        uint64_t submitted=0,exactReads=0,deduplicated=0,expired=0,staleCompleted=0;
        double fullMs=0,fullPeakMs=0,refreshMs=0,refreshPeakMs=0,queueWaitMs=0,queueWaitPeakMs=0;
        size_t pending=0,ready=0,workerLimit=1,activeWorkers=0;bool active=false,stopping=false;
    };
    Diagnostics TakeDiagnostics() {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto out=m_diagnostics;out.pending=m_pending.size();out.ready=m_ready.size();out.workerLimit=m_workerLimit;
        for(const auto& active:m_activeJobs)if(active.active)++out.activeWorkers;
        out.active=out.activeWorkers!=0;out.stopping=m_stopping;
        m_diagnostics={};return out;
    }
    void Trace(const SubtitleBoxObservation& key,const char* stage,const char* reason="",uint64_t mismatch=0) const noexcept {
        auto r=AnalysisTraceRecord(key.identity,this,stage,reason);r.kind=AnalysisDiagnosticRecordKindV1::Worker;
        r.policyGeneration=key.policyGeneration;r.continuityGeneration=key.continuityGeneration;
        r.nearBarDistance=key.nearBarDistance;r.optimizationMode=key.optimizationMode;r.scriptHint=static_cast<uint32_t>(key.scriptHint);
        r.sourceWidth=key.width;r.sourceHeight=key.height;r.discontinuity=key.discontinuity;
        r.flags=(key.analyzed?1u:0u)|(key.text.detected?2u:0u)|(key.sharedPicture.required?4u:0u)|
            (key.sharedPicture.AvailableFor(key.identity,key.width,key.height)?8u:0u)|(key.pictureAuthorityUnavailableForMeasurement?16u:0u)|
            (key.sharedPicture.inspectionCandidate.AvailableFor(key.identity,key.width,key.height,key.policyGeneration,key.continuityGeneration)?32u:0u)|
            (key.inspectionCandidateUsed?64u:0u);
        r.counters[0]=mismatch;r.counters[1]=key.sharedPicture.identity.acceptedSequence;
        r.counters[2]=key.sharedPicture.inspectionCandidate.originIdentity.acceptedSequence;
        r.counters[3]=key.sharedPicture.inspectionCandidate.nativeGeometryRevision;
        AnalysisTraceRect(r.candidate,key.sharedPicture.inspectionCandidate.bounds);
        r.resultCode=key.text.lineCount;AnalysisTraceRect(r.subtitle,key.text.bounds);
        r.metrics[0]=key.analysisMs;r.metrics[1]=key.pictureTop;r.metrics[2]=key.pictureBottom;
        AnalysisDiagnosticTrace::Emit(r);
    }
    using Work=std::function<SubtitleBoxObservation(SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&)>;
    // Input cadence owns analysis capacity. Display refresh/repeated outputs
    // cannot add workers or change source observations. A lower cadence parks
    // worker1 after its current independent job, without interrupting pixels.
    void SetInputRateHz(double inputHz) {
        {std::lock_guard<std::mutex> lock(m_mutex);
            if(m_stopping)return;
            m_workerLimit=std::isfinite(inputHz) && inputHz>30.0?2u:1u;
        }
        m_changed.notify_all();
    }
    ~SubtitleMeasurementWorker() { Stop(); }
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
            a.analysisStride==b.analysisStride &&
            a.width==b.width && a.height==b.height && a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode && a.scriptHint==b.scriptHint &&
            a.policyGeneration==b.policyGeneration &&
            (a.continuityGeneration==b.continuityGeneration ||
                (!a.pictureAuthorityUnavailableForMeasurement &&
                    !b.pictureAuthorityUnavailableForMeasurement &&
                    !a.independentBarAnalysis && !b.independentBarAnalysis &&
                    a.sharedPicture.required && b.sharedPicture.required)) &&
            a.discontinuity==b.discontinuity && (samePicture || sameUnavailablePictureScan);
    }
    // WORK ownership is weaker than READY admission: an already-running raw
    // full-source scan need not run twice merely because native geometry became
    // available. Keep its original key/closure and validate the completed result
    // through the strict completed-result read/rebind path. An incompatible completed
    // result cannot suppress a later canonical request.
    static bool SamePendingFullSourceWork(const SubtitleBoxObservation& existing,
        const SubtitleBoxObservation& incoming) {
        const auto ordinary=[](const SubtitleBoxObservation& key) {
            return !key.analyzed && !key.analysisRefresh && !key.pendingRefresh &&
                !key.cadenceSkipped && !key.discontinuity && !key.inspectionCandidateUsed &&
                !key.sharedPicture.inspectionCandidate.valid && !key.glyphLocalTop && !key.glyphLocalBottom &&
                !key.barTrackingAuthority && !key.currentAnchorUsesTrackedEdge &&
                !key.hasBarTrackingReference && !key.incomingBarTrackingReference.valid &&
                !key.trackedBottomHypothesisAttempted;
        };
        return ordinary(existing) && ordinary(incoming) &&
            existing.identity.acceptedSequence && existing.identity.analysisDomainKey &&
            SameAnalysisFrameIdentity(existing.identity,incoming.identity) &&
            existing.width>0 && existing.height>0 && existing.width==incoming.width && existing.height==incoming.height &&
            existing.analysisStride==1 && incoming.analysisStride==1 &&
            existing.nearBarDistance==incoming.nearBarDistance && existing.optimizationMode==incoming.optimizationMode &&
            existing.scriptHint==incoming.scriptHint && existing.policyGeneration==incoming.policyGeneration &&
            existing.continuityGeneration==incoming.continuityGeneration &&
            existing.pictureAuthorityUnavailableForMeasurement && existing.sharedPicture.required &&
            !existing.sharedPicture.AvailableFor(existing.identity,existing.width,existing.height) &&
            incoming.sharedPicture.required &&
            incoming.sharedPicture.AvailableFor(incoming.identity,incoming.width,incoming.height);
    }
    // The head has advanced; queued scans for older transport frames cannot
    // become current. The active worker is left alone and remains nonblocking.
    void BeginSourceWindow(const ActivePictureFrameIdentity& head) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_windowHead.transportGeneration>head.transportGeneration ||
            (m_windowHead.transportGeneration==head.transportGeneration &&
                m_windowHead.acceptedSequence>=head.acceptedSequence))return;
        m_windowHead=head;
        PruneRawHistory();
        for(auto i=m_pending.begin();i!=m_pending.end();) {
            const auto& identity=i->key.identity;
            if(identity.transportGeneration<head.transportGeneration ||
                (identity.transportGeneration==head.transportGeneration &&
                    identity.acceptedSequence<head.acceptedSequence)) {
                Trace(i->key,"worker-drop","source-window-expired");
                i=m_pending.erase(i);
                ++m_diagnostics.expired;
            } else ++i;
        }
    }
    // Lookahead and presentation can inspect the same exact-frame result.
    // Rebinding modifies only this copy; the cached observation is immutable.
    bool TryRead(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,
        bool backlogRetirementOnly=false) {
        return ReadReady(key,out,false,backlogRetirementOnly);
    }
    // Observation-only query used by the queue owner to request an exact
    // timeline witness. It grants no reuse and never changes cached evidence.
    static bool CompleteIndependentScanForKey(const SubtitleBoxObservation& scan,const SubtitleBoxObservation& key,
        bool requireDetected=true) {
        const auto& b=scan.barEvidence;
        return SameAnalysisFrameIdentity(scan.identity,key.identity) && scan.policyGeneration==key.policyGeneration &&
            scan.width==key.width && scan.height==key.height && scan.nearBarDistance==key.nearBarDistance &&
            scan.optimizationMode==key.optimizationMode && scan.scriptHint==key.scriptHint &&
            scan.analysisStride==1 && key.analysisStride==1 && !scan.discontinuity && !key.discontinuity &&
            scan.analyzed && scan.independentBarAnalysis && scan.barAuthority && b.available &&
            !scan.pendingRefresh && !scan.analysisRefresh && !scan.barTrackingAuthority &&
            !scan.currentAnchorUsesTrackedEdge && !scan.glyphLocalTop && !scan.glyphLocalBottom &&
            !scan.inspectionCandidateUsed && !b.topRevalidatedAtReference && !b.topRevalidatedNearby &&
            !b.bottomRevalidatedAtReference && !b.bottomRevalidatedNearby && !b.captionBackedTop && !b.captionBackedBottom &&
            scan.pictureTop==b.top && scan.pictureBottom==b.bottom &&
            (!requireDetected || scan.text.detected) && !scan.text.held && !scan.text.workLimit && !scan.text.confirmationPending &&
            scan.text.topSearchComplete && scan.text.bottomSearchComplete && scan.ink &&
            scan.ink->rawEvidenceComplete && scan.ink->paletteValid;
    }
    bool PeekIndependentContinuity(const SubtitleBoxObservation& key,uint64_t& continuity) {
        continuity=0;std::lock_guard<std::mutex> lock(m_mutex);
        for(auto i=m_ready.rbegin();i!=m_ready.rend();++i)
            if(CompleteIndependentScanForKey(*i,key)) {continuity=i->continuityGeneration;return true;}
        return false;
    }
    bool TryReadOwnedScan(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,
        const OwnedScanBacklogWitness& witness,uint64_t timelinePolicy) {
        return ReadReady(key,out,false,false,&witness,timelinePolicy);
    }
    bool TryTake(const SubtitleBoxObservation& key,SubtitleBoxObservation& out) {
        return ReadReady(key,out,true,false);
    }
    // Historical RAW observations are separate from current READY admission.
    // A bound empty snapshot withdraws prior history instead of reusing it.
    std::unique_ptr<SubtitleMeasuredRawPredecessors> ReadMeasuredRawPredecessors(
        const SubtitleBoxObservation& currentKey) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto snapshot=std::make_unique<SubtitleMeasuredRawPredecessors>();
        std::vector<SubtitleBoxObservation> frames;
        PruneRawHistory();
        if(RawResultEligible(currentKey,currentKey) && RawVersionOwns(currentKey)) {
            const SubtitleBoxObservation* next=&currentKey;size_t exportedBytes=0;
            for(uint64_t offset=1;offset<=3 && currentKey.identity.acceptedSequence>=offset;++offset) {
                const uint64_t wanted=currentKey.identity.acceptedSequence-offset;
                std::unique_ptr<SubtitleBoxObservation> found;
                const auto inspect=[&](const SubtitleBoxObservation& candidate) {
                    const size_t bytes=RawInkBytes(candidate);
                    if(candidate.identity.acceptedSequence!=wanted || !RawVersionOwns(candidate) ||
                        !RawResultEligible(candidate,candidate) || bytes>16u*1024u*1024u ||
                        exportedBytes+bytes>48u*1024u*1024u)return;
                    auto copy=std::make_unique<SubtitleBoxObservation>(candidate);
                    if(!copy->sharedPicture.Matches(currentKey.sharedPicture)) {
                        // Independent RAW pixels may have completed before the
                        // canonical plane existed. Rebind only the COPY, using
                        // the existing strict path and identical physical bars.
                        if(copy->pictureTop!=currentKey.pictureTop || copy->pictureBottom!=currentKey.pictureBottom ||
                            !SubtitleBoxLookahead::RebindIndependentPicture(*copy,currentKey.sharedPicture))return;
                    }
                    if(SubtitleBoxLookahead::ConsecutiveCrowdedRawContext(*copy,*next))found=std::move(copy);
                };
                for(const auto& candidate:m_ready)inspect(candidate);
                for(const auto& candidate:m_pastRaw)inspect(candidate);
                if(!found)break;
                exportedBytes+=RawInkBytes(*found);frames.push_back(std::move(*found));next=&frames.back();
            }
            std::reverse(frames.begin(),frames.end());
            for(auto& frame:frames) {frame.subtitleArProof={};frame.subtitleArDarkness={};}
        }
        snapshot->Record(currentKey,std::move(frames));
        return snapshot;
    }
    // Metadata-only query; the queue owner must independently qualify the
    // returned old continuity with an exact-source timeline witness.
    static bool RequiredFullSourceWork(const SubtitleBoxObservation& key) {
        return key.sharedPicture.required && key.width>0 && key.height>0 && key.analysisStride==1 && !key.analyzed &&
            !key.discontinuity && !key.pendingRefresh && !key.analysisRefresh && !key.cadenceSkipped &&
            !key.sharedPicture.inspectionCandidate.valid && !key.inspectionCandidateUsed &&
            !key.glyphLocalTop && !key.glyphLocalBottom && !key.barTrackingAuthority &&
            !key.currentAnchorUsesTrackedEdge && !key.trackedBottomHypothesisAttempted;
    }
    static bool SameOwnedWorkSource(const SubtitleBoxObservation& existing,const SubtitleBoxObservation& incoming) {
        return RequiredFullSourceWork(existing) && RequiredFullSourceWork(incoming) && existing.continuityGeneration &&
            existing.continuityGeneration<incoming.continuityGeneration &&
            existing.identity.acceptedSequence && existing.identity.analysisDomainKey &&
            SameAnalysisFrameIdentity(existing.identity,incoming.identity) &&
            RawSettingsMatch(existing,incoming,false);
    }
    bool PeekOwnedWorkContinuity(const SubtitleBoxObservation& incoming,uint64_t& oldContinuity) {
        oldContinuity=0;std::lock_guard<std::mutex> lock(m_mutex);
        if(m_stopping)return false;
        const auto inspect=[&](const SubtitleBoxObservation& key) {
            if(SameOwnedWorkSource(key,incoming))oldContinuity=(std::max)(oldContinuity,key.continuityGeneration);
        };
        for(const auto& active:m_activeJobs)if(active.active)inspect(active.key);
        for(const auto& job:m_pending)inspect(job.key);
        return oldContinuity!=0;
    }
    static bool CanCoalesceOwnedWork(const SubtitleBoxObservation& existing,const SubtitleBoxObservation& incoming,
        const OwnedScanBacklogWitness* witness,uint64_t timelinePolicy) {
        return witness && SameOwnedWorkSource(existing,incoming) &&
            witness->Matches(incoming.identity,timelinePolicy,existing.continuityGeneration,incoming.continuityGeneration);
    }
    static bool CanCoalesceOwnedCompleted(const SubtitleBoxObservation& original,const SubtitleBoxObservation& incoming,
        const OwnedScanBacklogWitness* witness,uint64_t timelinePolicy) {
        if(!witness || !RequiredFullSourceWork(incoming) || !original.sharedPicture.required ||
            original.sharedPicture.inspectionCandidate.valid || original.continuityGeneration>=incoming.continuityGeneration ||
            !CompleteIndependentScanForKey(original,incoming) || !original.text.bounds.Valid() || original.text.lineCount<=0 ||
            !witness->Matches(incoming.identity,timelinePolicy,original.continuityGeneration,incoming.continuityGeneration))return false;
        // A completion may race the queue owner's peek/witness round trip.
        // Coalesce only when the unchanged READY path can actually return this
        // original source under the same typed witness. Never alter the cache.
        auto copy=std::make_unique<SubtitleBoxObservation>(original);
        return SubtitleBoxLookahead::RebindIndependentPicture(*copy,incoming.sharedPicture) &&
            SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(*copy,incoming.continuityGeneration,true) &&
            SameKey(*copy,incoming);
    }
    bool Submit(const SubtitleBoxObservation& key,Work work,
        const OwnedScanBacklogWitness* ownedWitness=nullptr,uint64_t timelinePolicy=0,bool nonblocking=false,
        std::shared_ptr<AnalysisCompletionSignal> completion={}) {
        auto requestKey=key;
        // QueueSubtitleMeasurement submits only valid prepared sources. Mark
        // the request's scan path here so active and pending jobs can coalesce
        // before Measure has produced an observation.
        requestKey.pictureAuthorityUnavailableForMeasurement=
            requestKey.sharedPicture.required &&
            !requestKey.sharedPicture.AvailableFor(
                requestKey.identity,requestKey.width,requestKey.height);
        std::unique_lock<std::mutex> lock(m_mutex,std::defer_lock);
        if(nonblocking) {if(!lock.try_lock())return false;}else lock.lock();
        if(m_stopping)return false;
        if(requestKey.identity.transportGeneration<m_windowHead.transportGeneration ||
            (requestKey.identity.transportGeneration==m_windowHead.transportGeneration &&
                requestKey.identity.acceptedSequence<m_windowHead.acceptedSequence)) {
            Trace(requestKey,"worker-drop","source-window-expired");
            ++m_diagnostics.expired;return false;
        }
        for(const auto& active:m_activeJobs)
            if(active.active && (SameKey(active.key,requestKey) || SamePendingFullSourceWork(active.key,requestKey) ||
                CanCoalesceOwnedWork(active.key,requestKey,ownedWitness,timelinePolicy))) {
                Trace(requestKey,"worker-dedup","active");++m_diagnostics.deduplicated;return false;
            }
        for(const auto& job:m_pending)if(SameKey(job.key,requestKey) || SamePendingFullSourceWork(job.key,requestKey) ||
            CanCoalesceOwnedWork(job.key,requestKey,ownedWitness,timelinePolicy)){Trace(requestKey,"worker-dedup","queued");++m_diagnostics.deduplicated;return false;}
        for(const auto& ready:m_ready)if(SameKey(ready,requestKey) ||
            CanCoalesceOwnedCompleted(ready,requestKey,ownedWitness,timelinePolicy)){Trace(requestKey,"worker-dedup","ready");++m_diagnostics.deduplicated;return false;}
        if(m_pending.size()>=8) {
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
                Trace(leastUseful->key,"worker-drop","lower-priority-replaced");
                m_pending.erase(leastUseful);
            } else {
                Trace(requestKey,"worker-drop","future-capacity");
                ++m_diagnostics.dropped;
                return false;
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
        const uint64_t rawRevision=RecordAcceptedRawJob(requestKey);
        m_pending.insert(place,{requestKey,std::move(work),rawRevision,std::move(completion)});
        ++m_diagnostics.submitted;
        for(size_t index=0;index<m_workerLimit;++index)
            if(!m_threads[index].joinable())m_threads[index]=std::thread([this,index]{ Run(index); });
        m_changed.notify_all();
        return true;
    }
    void Stop() {
        {std::lock_guard<std::mutex> lock(m_mutex);m_stopping=true;m_pending.clear();m_pastRaw.clear();m_rawVersions.clear();}
        m_changed.notify_all();
        for(auto& thread:m_threads)if(thread.joinable())thread.join();
    }
private:
    struct RawVersion {
        SubtitleBoxObservation key;
        uint64_t revision=0;
        bool ambiguous=false,complete=false;
        const SubtitleInkSnapshot* ink=nullptr;
        std::unique_ptr<SubtitleBoxObservation> original;
    };
    static bool RawSettingsMatch(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b,bool sameContinuity=true) {
        const auto& x=a.identity;const auto& y=b.identity;
        return x.transportGeneration==y.transportGeneration && x.sourceFormatGeneration==y.sourceFormatGeneration &&
            x.analysisDomainKey==y.analysisDomainKey && x.viewportGeneration==y.viewportGeneration &&
            x.rendererGeneration==y.rendererGeneration && a.policyGeneration==b.policyGeneration &&
            (!sameContinuity || a.continuityGeneration==b.continuityGeneration) && a.width==b.width && a.height==b.height &&
            a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode &&
            a.scriptHint==b.scriptHint && a.analysisStride==b.analysisStride;
    }
    static bool SameRawSource(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
        return SameAnalysisFrameIdentity(a.identity,b.identity) && RawSettingsMatch(a,b);
    }
    static bool RawResultEligible(const SubtitleBoxObservation& result,const SubtitleBoxObservation& jobKey) {
        return SameRawSource(result,jobKey) && result.sharedPicture.Matches(jobKey.sharedPicture) &&
            SubtitleBoxLookahead::CompleteConsecutiveCrowdedRaw(result) && CompleteIndependentScanForKey(result,jobKey) &&
            !result.cadenceSkipped;
    }
    static size_t RawInkBytes(const SubtitleBoxObservation& raw) {
        constexpr size_t limit=16u*1024u*1024u;
        if(!raw.ink)return limit+1;
        size_t total=sizeof(raw)+sizeof(SubtitleInkSnapshot);
        const auto add=[&](size_t count,size_t width) {
            if(count>(limit-total)/width)return false;total+=count*width;return true;
        };
        if(!add(raw.ink->sourceRows.capacity(),sizeof(int)) || !add(raw.ink->rawInk.capacity(),sizeof(uint64_t)) ||
            !add(raw.ink->ownedInk.capacity(),sizeof(uint64_t)) || !add(raw.ink->blackBacking.capacity(),sizeof(uint64_t)))return limit+1;
        return total;
    }
    bool RawWindowContext(const ActivePictureFrameIdentity& id) const {
        return m_windowHead.transportGeneration && id.transportGeneration==m_windowHead.transportGeneration &&
            id.sourceFormatGeneration==m_windowHead.sourceFormatGeneration &&
            id.viewportGeneration==m_windowHead.viewportGeneration && id.rendererGeneration==m_windowHead.rendererGeneration &&
            (!m_windowHead.analysisDomainKey || id.analysisDomainKey==m_windowHead.analysisDomainKey);
    }
    void PruneRawHistory() {
        if(!m_windowHead.transportGeneration)return;
        const uint64_t floor=m_windowHead.acceptedSequence>3?m_windowHead.acceptedSequence-3:0;
        for(auto i=m_pastRaw.begin();i!=m_pastRaw.end();) {
            if(!RawWindowContext(i->identity) || i->identity.acceptedSequence<floor ||
                i->identity.acceptedSequence>=m_windowHead.acceptedSequence)i=m_pastRaw.erase(i);else ++i;
        }
        for(auto i=m_rawVersions.begin();i!=m_rawVersions.end();) {
            const auto& id=i->key.identity;
            if(!RawWindowContext(id) || id.acceptedSequence<floor ||
                (id.acceptedSequence>m_windowHead.acceptedSequence &&
                 id.acceptedSequence-m_windowHead.acceptedSequence>ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES+2))
                i=m_rawVersions.erase(i);else ++i;
        }
    }
    uint64_t RecordAcceptedRawJob(const SubtitleBoxObservation& key) {
        PruneRawHistory();
        // A changed current detector epoch cannot inherit older RAW history.
        if(m_windowHead.acceptedSequence==key.identity.acceptedSequence && RawWindowContext(key.identity)) {
            if(m_rawSettingsKnown && !RawSettingsMatch(m_rawSettings,key)) {
                for(auto i=m_pastRaw.begin();i!=m_pastRaw.end();)if(!RawSettingsMatch(*i,key))i=m_pastRaw.erase(i);else ++i;
                for(auto i=m_rawVersions.begin();i!=m_rawVersions.end();)if(!RawSettingsMatch(i->key,key))i=m_rawVersions.erase(i);else ++i;
            }
            m_rawSettings=key;m_rawSettings.text={};m_rawSettings.ink.reset();m_rawSettingsKnown=true;
        }
        const uint64_t revision=++m_nextRawRevision;
        for(auto& v:m_rawVersions)if(SameRawSource(v.key,key)) {
            // A distinct accepted interpretation withdraws prior history. Calls
            // deduplicated above never reach this point and add no version.
            v.revision=revision;v.ambiguous=true;v.complete=false;v.ink=nullptr;v.original.reset();
            for(auto i=m_pastRaw.begin();i!=m_pastRaw.end();)if(SameRawSource(*i,key))i=m_pastRaw.erase(i);else ++i;
            return revision;
        }
        RawVersion version;version.key=key;version.key.text={};version.key.ink.reset();version.revision=revision;
        m_rawVersions.push_back(std::move(version));
        // Metadata only; completed future work cannot evict nearer past sources.
        while(m_rawVersions.size()>32) {
            auto oldest=m_rawVersions.begin();
            for(auto i=m_rawVersions.begin();i!=m_rawVersions.end();++i)
                if(i->key.identity.acceptedSequence<oldest->key.identity.acceptedSequence)oldest=i;
            m_rawVersions.erase(oldest);
        }
        return revision;
    }
    bool RawVersionOwns(const SubtitleBoxObservation& raw) const {
        for(const auto& v:m_rawVersions)if(SameRawSource(v.key,raw)) {
            if(v.ambiguous || !v.complete || v.ink!=raw.ink.get() || !v.original)return false;
            auto original=std::make_unique<SubtitleBoxObservation>(*v.original);original->ink=raw.ink;
            if(!original->sharedPicture.Matches(raw.sharedPicture) &&
                (original->pictureTop!=raw.pictureTop || original->pictureBottom!=raw.pictureBottom ||
                 !SubtitleBoxLookahead::RebindIndependentPicture(*original,raw.sharedPicture)))return false;
            return SubtitleBoxLookahead::SameRepeatedCrowdedRaw(*original,raw);
        }
        return false;
    }
    void RecordCompletedRaw(const SubtitleBoxObservation& key,uint64_t revision,const SubtitleBoxObservation& result) {
        PruneRawHistory();
        RawVersion* version=nullptr;
        for(auto& v:m_rawVersions)if(SameRawSource(v.key,key)) {version=&v;break;}
        if(!version || version->revision!=revision || version->ambiguous)return;
        version->complete=RawResultEligible(result,key);version->ink=version->complete?result.ink.get():nullptr;
        if(version->complete) {
            version->original=std::make_unique<SubtitleBoxObservation>(result);
            version->original->ink.reset(); // Shape/provenance only; do not retain an unbudgeted bitmap owner.
        } else version->original.reset();
        if(!version->complete || !RawWindowContext(result.identity) ||
            result.identity.acceptedSequence>=m_windowHead.acceptedSequence ||
            m_windowHead.acceptedSequence-result.identity.acceptedSequence>3 || RawInkBytes(result)>16u*1024u*1024u)return;
        m_pastRaw.push_back(result);
        std::sort(m_pastRaw.begin(),m_pastRaw.end(),[](const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
            return a.identity.acceptedSequence<b.identity.acceptedSequence;
        });
        size_t bytes=0;for(const auto& raw:m_pastRaw)bytes+=RawInkBytes(raw);
        while(m_pastRaw.size()>3 || bytes>48u*1024u*1024u) {bytes-=RawInkBytes(m_pastRaw.front());m_pastRaw.pop_front();}
    }
    bool ReadReady(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,bool consume,bool backlogRetirementOnly,
        const OwnedScanBacklogWitness* ownedWitness=nullptr,uint64_t timelinePolicy=0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t mismatches=0;
        for(auto i=m_ready.begin();i!=m_ready.end();++i) {
            auto candidate=*i;
            // A nominated scan is not independent-bar proof. Reuse only its
            // exact completed glyph work when the SAME plane becomes canonical.
            // The cache keeps the original record; only this read copy is rebound.
            SubtitleBoxLookahead::RebindCompletedInspectedPlane(candidate,key.sharedPicture);
            const bool newCanonicalPlane=candidate.pictureAuthorityUnavailableForMeasurement &&
                candidate.sharedPicture.required && key.sharedPicture.required &&
                !candidate.sharedPicture.AvailableFor(candidate.identity,candidate.width,candidate.height) &&
                key.sharedPicture.AvailableFor(key.identity,key.width,key.height) &&
                !candidate.sharedPicture.Matches(key.sharedPicture);
            // Coalescing WORK must not turn an incomplete raw scan into the
            // canonical request's permanent answer. The original same-key
            // observation remains readable as UNKNOWN; a new-plane miss lets
            // the caller submit the canonical job without changing its cache.
            if(newCanonicalPlane && (!CompleteIndependentScanForKey(candidate,key,false) ||
                (candidate.text.detected ? (!candidate.text.bounds.Valid() || candidate.text.lineCount<=0) :
                    candidate.text.lineCount!=0))) {
                mismatches|=1024ull;continue;
            }
            if (!SubtitleBoxLookahead::RebindIndependentPicture(candidate,key.sharedPicture)) {
                mismatches|=16ull;continue;
            }
            const bool ownedBacklog=ownedWitness && CompleteIndependentScanForKey(candidate,key) &&
                ownedWitness->Matches(key.identity,timelinePolicy,candidate.continuityGeneration,key.continuityGeneration);
            bool reboundFromBacklog=false;
            if(candidate.continuityGeneration!=key.continuityGeneration &&
                (backlogRetirementOnly || ownedBacklog)) {
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
                reboundFromBacklog?(ownedBacklog?"owned-source-backlog-scan-rebound":"backlog-current-scan-rebound"):"exact-key-accepted");
            out=std::move(candidate);
            if(consume)m_ready.erase(i);
            else ++m_diagnostics.exactReads;
            return true;
        }
        Trace(key,"worker-miss",m_ready.empty()?"no-ready-results":"no-compatible-result",mismatches);
        ++m_diagnostics.requestMisses;return false;
    }
    struct Job {SubtitleBoxObservation key;Work work;uint64_t rawRevision=0;std::shared_ptr<AnalysisCompletionSignal> completion;std::chrono::steady_clock::time_point queued=std::chrono::steady_clock::now();};
    void Run(size_t workerIndex) {
        SubtitleMeasurementSampler scanner;
        SubtitleBarTrackingReference previous;
        for(;;) {
            Job job;
            {std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait(lock,[this,workerIndex]{
                    return m_stopping || (workerIndex<m_workerLimit && !m_pending.empty());
                });
                if(m_stopping)return;
                job=std::move(m_pending.front());m_pending.pop_front();
                m_activeJobs[workerIndex].active=true;m_activeJobs[workerIndex].key=job.key;
            }
            if(job.key.discontinuity || !previous.IsImmediatePredecessor(job.key.identity,
                job.key.width,job.key.height,job.key.policyGeneration,job.key.continuityGeneration,job.key.scriptHint)) {
                previous={};scanner.Reset();
            }
            Trace(job.key,"worker-start");
            const auto workStart=std::chrono::steady_clock::now();
            const double waited=std::chrono::duration<double,std::milli>(workStart-job.queued).count();
            SubtitleBoxObservation result=job.key;
            try {result=job.work(scanner,previous);previous=SubtitleBoxLookahead::AdvanceBarTrackingReference(result);} catch(...) {result.analyzed=false;}
            Trace(result,"worker-complete",result.analyzed?result.text.diagnosticReason:"analysis-failed");
            {std::lock_guard<std::mutex> lock(m_mutex);
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-workStart).count();
                m_diagnostics.queueWaitMs+=waited;m_diagnostics.queueWaitPeakMs=(std::max)(m_diagnostics.queueWaitPeakMs,waited);
                if(!result.analyzed)++m_diagnostics.failed;
                else if(result.analysisRefresh){++m_diagnostics.refreshes;m_diagnostics.refreshMs+=ms;m_diagnostics.refreshPeakMs=(std::max)(m_diagnostics.refreshPeakMs,ms);}
                else {++m_diagnostics.fullScans;m_diagnostics.fullMs+=ms;m_diagnostics.fullPeakMs=(std::max)(m_diagnostics.fullPeakMs,ms);}
                m_activeJobs[workerIndex]={};
                if(!m_stopping)RecordCompletedRaw(job.key,job.rawRevision,result);
                const bool stale=m_windowHead.transportGeneration &&
                    (result.identity.transportGeneration<m_windowHead.transportGeneration ||
                     (result.identity.transportGeneration==m_windowHead.transportGeneration &&
                      result.identity.acceptedSequence<m_windowHead.acceptedSequence));
                if(!m_stopping && stale) {
                    ++m_diagnostics.staleCompleted;Trace(result,"worker-drop","completed-source-window-expired");
                } else if(!m_stopping) {
                    if(m_ready.size()>=16)m_ready.pop_front();
                    m_ready.push_back(std::move(result));
                }
            }
            if(job.completion)job.completion->Notify();
        }
    }
    Diagnostics m_diagnostics;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::array<std::thread,2> m_threads;
    std::deque<Job> m_pending;
    std::deque<SubtitleBoxObservation> m_ready;
    std::deque<SubtitleBoxObservation> m_pastRaw;
    std::deque<RawVersion> m_rawVersions;
    SubtitleBoxObservation m_rawSettings;
    uint64_t m_nextRawRevision=0;bool m_rawSettingsKnown=false;
    ActivePictureFrameIdentity m_windowHead;
    struct ActiveJob { bool active=false;SubtitleBoxObservation key; };
    std::array<ActiveJob,2> m_activeJobs;
    size_t m_workerLimit=1;
    bool m_stopping=false;
};


// A late asynchronous result is not negative subtitle evidence. Bridge only a
// bounded delay, with fresh bar proof and unchanged native pixels in every
// accepted glyph rectangle. No OCR, spatial search, wait or GPU readback here.
class SubtitlePendingMeasurementGuard {
public:
    enum class ReadyRearmRejectCode : uint8_t {
        NotAttempted, Passed, NoEligibleFreshPositive, FootprintDelegated,
        SeedOrMode, SourceLayout, CurrentFlags, BarEvidence, PhysicalBounds,
        PolicyGeneration, ContinuityGeneration, IdentityContext,
        CueIdentity, CueBounds, CuePanelsCapture, CueSignatures, CueLines, CueEdges,
        Sequence, InkPalette, InkGrid, RawMask, OwnedMask, GlyphPixels,
        KnownPixels, BackingPixels
    };
    enum ReadyRearmLayoutMismatch : uint32_t {
        ReadyLayoutCueIdentity=1u<<0, ReadyLayoutBounds=1u<<1,
        ReadyLayoutPanels=1u<<2, ReadyLayoutCapture=1u<<3,
        ReadyLayoutSignatures=1u<<4, ReadyLayoutLines=1u<<5, ReadyLayoutEdges=1u<<6
    };
    static const char* ReadyRearmRejectReasonName(ReadyRearmRejectCode code) {
        switch(code) {
        case ReadyRearmRejectCode::NotAttempted:return "not-attempted";
        case ReadyRearmRejectCode::Passed:return "passed";
        case ReadyRearmRejectCode::NoEligibleFreshPositive:return "no-eligible-fresh-positive";
        case ReadyRearmRejectCode::FootprintDelegated:return "footprint-delegated";
        case ReadyRearmRejectCode::SeedOrMode:return "seed-or-mode";
        case ReadyRearmRejectCode::SourceLayout:return "source-layout";
        case ReadyRearmRejectCode::CurrentFlags:return "current-flags";
        case ReadyRearmRejectCode::BarEvidence:return "bar-evidence";
        case ReadyRearmRejectCode::PhysicalBounds:return "physical-bounds";
        case ReadyRearmRejectCode::PolicyGeneration:return "policy-generation";
        case ReadyRearmRejectCode::ContinuityGeneration:return "continuity-generation";
        case ReadyRearmRejectCode::IdentityContext:return "identity-context";
        case ReadyRearmRejectCode::CueIdentity:return "cue-identity";
        case ReadyRearmRejectCode::CueBounds:return "cue-bounds";
        case ReadyRearmRejectCode::CuePanelsCapture:return "cue-panels-capture";
        case ReadyRearmRejectCode::CueSignatures:return "cue-signatures";
        case ReadyRearmRejectCode::CueLines:return "cue-lines";
        case ReadyRearmRejectCode::CueEdges:return "cue-edges";
        case ReadyRearmRejectCode::Sequence:return "sequence";
        case ReadyRearmRejectCode::InkPalette:return "ink-palette";
        case ReadyRearmRejectCode::InkGrid:return "ink-grid";
        case ReadyRearmRejectCode::RawMask:return "raw-mask";
        case ReadyRearmRejectCode::OwnedMask:return "owned-mask";
        case ReadyRearmRejectCode::GlyphPixels:return "glyph-pixels";
        case ReadyRearmRejectCode::KnownPixels:return "known-pixels";
        case ReadyRearmRejectCode::BackingPixels:return "backing-pixels";
        }
        return "unknown";
    }
    enum ContextMismatch : uint32_t {
        ContextDiscontinuity=1u<<0, ContextSource=1u<<1,
        ContextDimensions=1u<<2, ContextTransport=1u<<3,
        ContextFormat=1u<<4, ContextDomain=1u<<5,
        ContextViewport=1u<<6, ContextRenderer=1u<<7,
        ContextNearBar=1u<<8, ContextScript=1u<<9,
        ContextPolicy=1u<<10, ContextContinuity=1u<<11, ContextOptimization=1u<<12
    };
    struct Diagnostics {
        ReadyRearmRejectCode readyRearmRejectCode=ReadyRearmRejectCode::NotAttempted;
        uint32_t readyRearmLayoutMismatchBits=0;
        uint32_t contextMismatch=0;
        size_t glyphSamplesChecked=0;
        size_t currentBandSamplesChecked=0,backingSamplesChecked=0;
        size_t seedBandSamplesChecked=0;
        size_t readyRearmMaskWordsChecked=0,readyRearmGlyphSamplesChecked=0;
        size_t readyRearmBackingSamplesChecked=0,readyRearmKnownSamplesChecked=0;
        int glyphMismatchX=-1, glyphMismatchY=-1;
        bool sharedPictureMatches=false, sharedPictureCurrent=false;
        bool barsAvailable=false, topRevalidated=false, bottomRevalidated=false;
        bool backlogReassociated=false;
        uint64_t lastVerifiedSource=0,tokenSource=0,referenceContinuity=0,currentContinuity=0;
        bool backlogTokenAvailable=false;
        int expectedTop=0, expectedBottom=0, measuredTop=0, measuredBottom=0;
    };
    void Reset() { m_reference={};m_samples.clear();m_ownedSamples.clear();m_backingSamples.clear();
        m_knownInkSamples.clear();m_paletteFringe.clear();
        m_currentPixels.clear();m_currentSampled.clear();
        m_contrastSeedPixels.clear();m_contrastSeedDirections.clear();
        m_contrastSeedTopEnd=m_contrastSeedBottomStart=m_contrastSeedPictureTop=m_contrastSeedPictureBottom=0;
        m_contrastSeedWidth=m_contrastSeedHeight=0;
        m_currentInkProofSeeded=false;m_currentFootprintRequired=false;m_sourceLayout={};m_elapsed=0;m_last={};m_diagnostics={};
        m_certifiedSeed={};m_certifiedPolicy=m_certifiedContinuity=0;m_consumedCertificate={}; }
    const char* DecisionReason() const { return m_reason; }
    const Diagnostics& DecisionDiagnostics() const { return m_diagnostics; }
    ActivePictureFrameIdentity LastVerifiedIdentity() const { return m_last; }
    ActivePictureFrameIdentity AcceptedSeedIdentity() const { return m_reference.current.identity; }
    double PendingElapsedMilliseconds() const { return m_elapsed; }
    uint64_t ReferenceContinuity() const { return m_reference.continuityGeneration; }
    // Only accepted extraction may seed the late-worker fallback. A held cue
    // can be more complete than either current grouping or lookahead pruning;
    // never turn those partial measurements into a smaller replacement cue.
    bool RememberAccepted(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const AnalysisLumaSource& source,double frameMs,int holdMs,
        const SubtitleAcceptedCueSeedCertificate* existingCueCertificate=nullptr,
        const SubtitleCurrentPreparedCueCertificate* heldCurrentCue=nullptr,
        const ActivePictureBounds* physicalBase=nullptr,
        const SubtitleCadenceGuard* currentFootprint=nullptr) {
        m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::NotAttempted;
        m_diagnostics.readyRearmLayoutMismatchBits=0;
        m_diagnostics.seedBandSamplesChecked=0;
        m_diagnostics.readyRearmMaskWordsChecked=0;
        m_diagnostics.readyRearmGlyphSamplesChecked=0;
        m_diagnostics.readyRearmBackingSamplesChecked=0;
        m_diagnostics.readyRearmKnownSamplesChecked=0;
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
        if(accepted.held) {
            // A held pose cannot become a new seed. Full CURRENT prepared-cue
            // proof may preserve the ORIGINAL acquired seed and its samples,
            // without changing either its source or elapsed pending budget.
            const auto& old=m_reference.current;const auto& a=m_last;const auto& b=current.identity;
            const auto sameRect=[](const SubtitleBoxRect& x,const SubtitleBoxRect& y) {
                return x.left==y.left && x.top==y.top && x.right==y.right && x.bottom==y.bottom;
            };
            const auto sameBase=[](const ActivePictureBounds& x,const ActivePictureBounds& y) {
                return x.left==y.left && x.top==y.top && x.right==y.right && x.bottom==y.bottom &&
                    x.rasterWidth==y.rasterWidth && x.rasterHeight==y.rasterHeight;
            };
            bool sameShape=m_reference.text.detected && m_reference.text.cue==accepted.cue &&
                m_reference.text.lineCount==accepted.lineCount &&
                m_reference.text.capturePanelMeasured==accepted.capturePanelMeasured &&
                sameRect(m_reference.text.bounds,accepted.bounds) &&
                sameRect(m_reference.text.sourcePanel,accepted.sourcePanel) &&
                sameRect(m_reference.text.provenSourcePanel,accepted.provenSourcePanel) &&
                sameRect(m_reference.text.capturePanel,accepted.capturePanel);
            for(int line=0;sameShape && line<accepted.lineCount && line<3;++line)
                sameShape=sameRect(m_reference.text.lineBounds[line],accepted.lineBounds[line]) &&
                    sameRect(m_reference.text.linePanels[line],accepted.linePanels[line]) &&
                    sameRect(m_reference.text.provenLinePanels[line],accepted.provenLinePanels[line]) &&
                    sameRect(m_reference.text.lineCapturePanels[line],accepted.lineCapturePanels[line]);
            // The prepared certificate proves CURRENT glyph ownership, not
            // equality of its raw backing with the old seed's sampled card.
            // A larger/new card must never be represented by the old geometry.
            bool sameBacking=sameShape;
            const int rowTolerance=(std::max)(4,current.height/180);
            for(int line=0;sameBacking && line<m_reference.text.lineCount && line<3;++line) {
                bool matched=false;
                for(int rawLine=0;rawLine<current.text.lineCount && rawLine<3;++rawLine)
                    matched|=SubtitleBoxLookahead::SameCrowdedLine(old.text.lineBounds[line],current.text.lineBounds[rawLine],rowTolerance) &&
                        SubtitleBoxLookahead::SameCrowdedBacking(old,line,current,rawLine,0);
                sameBacking=matched;
            }
            const bool context=preview.available && current.analyzed && !current.pendingRefresh &&
                source.IsValid() && source.width==current.width && source.height==current.height &&
                HasPixelSeed() && sameShape && sameBacking && covers(current.text) &&
                heldCurrentCue && physicalBase && heldCurrentCue->Matches(current,accepted,*physicalBase) &&
                SubtitleBoxLookahead::SameContext(old,current) &&
                old.policyGeneration==current.policyGeneration && old.continuityGeneration==current.continuityGeneration &&
                preview.policyGeneration==current.policyGeneration && preview.continuityGeneration==current.continuityGeneration &&
                old.optimizationMode==current.optimizationMode && old.pictureTop==current.pictureTop && old.pictureBottom==current.pictureBottom &&
                old.sharedPicture.Matches(current.sharedPicture) &&
                sameBase(old.sharedPicture.bounds,*physicalBase) &&
                b.acceptedSequence>a.acceptedSequence && a.sourceFrameNumber && b.sourceFrameNumber>a.sourceFrameNumber &&
                b.sourceFrameNumber-a.sourceFrameNumber==b.acceptedSequence-a.acceptedSequence &&
                b.captureTimestamp>a.captureTimestamp;
            if(!context)return Reject("held-current-seed-unqualified");
            const double period=std::isfinite(frameMs)&&frameMs>0?frameMs:1000.0/60;
            if(m_elapsed+period*double(b.acceptedSequence-a.acceptedSequence)>(std::max)(0,(std::min)(1000,holdMs)))
                return Reject("held-current-seed-expired");
            m_reason="held-current-proof-preserves-original-seed";
            return false;
        }
        const bool independentCurrentCue=current.independentBarAnalysis && !current.analysisRefresh &&
            !current.barTrackingAuthority && !current.currentAnchorUsesTrackedEdge &&
            current.barEvidence.available && !current.barEvidence.topRevalidatedAtReference &&
            !current.barEvidence.topRevalidatedNearby && !current.barEvidence.bottomRevalidatedAtReference &&
            !current.barEvidence.bottomRevalidatedNearby && !current.barEvidence.captionBackedTop &&
            !current.barEvidence.captionBackedBottom &&
            current.pictureTop==current.barEvidence.top && current.pictureBottom==current.barEvidence.bottom &&
            current.ink && current.ink->rawEvidenceComplete &&
            !current.text.held && !accepted.held && !accepted.revised;
        const bool resolvedCovers=covers(preview.text);
        const bool certifiedExistingCue=independentCurrentCue && existingCueCertificate &&
            existingCueCertificate->Matches(preview,accepted);
        // A complete current grouping already met the normal seed eligibility;
        // the tracker certificate is needed only when pruning omitted that cue.
        const bool currentCompletePositive=independentCurrentCue && resolvedCovers;
        const bool eligibleSameCueRearm=certifiedExistingCue || currentCompletePositive;
        const bool consumedCertificate=independentCurrentCue && m_consumedCertificate.Matches(preview,accepted);
        m_diagnostics.readyRearmRejectCode=eligibleSameCueRearm ? ReadyRearmRejectCode::NotAttempted :
            ReadyRearmRejectCode::NoEligibleFreshPositive;
        if(!preview.available || !preview.current.analyzed || preview.current.pendingRefresh ||
            preview.current.discontinuity || preview.current.glyphLocalTop || preview.current.glyphLocalBottom ||
            !preview.current.barAuthority ||
            !source.IsValid() || source.width!=preview.current.width || source.height!=preview.current.height ||
            !accepted.detected || accepted.confirmationPending || accepted.workLimit || accepted.lineCount<1 || accepted.lineCount>3 ||
            !covers(preview.current.text) || (!resolvedCovers && !certifiedExistingCue && !consumedCertificate)) {
            return Reject("accepted-seed-incomplete");
        }
        if(!resolvedCovers && consumedCertificate) {
            // The tracker intentionally exports no new certificate on a repeated
            // presentation. Preserve the exact prior seed without renewing time.
            m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::Sequence;
            m_reason="accepted-certificate-already-consumed";return false;
        }
        const auto& old=m_certifiedSeed;const auto& now=current.identity;
        if(!resolvedCovers && certifiedExistingCue && old.acceptedSequence &&
            old.transportGeneration==now.transportGeneration && old.sourceFormatGeneration==now.sourceFormatGeneration &&
            old.analysisDomainKey==now.analysisDomainKey && old.viewportGeneration==now.viewportGeneration &&
            old.rendererGeneration==now.rendererGeneration && m_certifiedPolicy==preview.policyGeneration &&
            m_certifiedContinuity==preview.continuityGeneration && now.acceptedSequence<=old.acceptedSequence) {
            // Replaying the same exported certificate cannot renew the lease.
            m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::Sequence;
            m_reason="accepted-certificate-already-consumed";return false;
        }
        if(HasPixelSeed() && eligibleSameCueRearm && now.acceptedSequence<=m_last.acceptedSequence) {
            const bool sameSource=SameAnalysisFrameIdentity(now,m_last);
            m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::Sequence;
            m_reason=sameSource ? (certifiedExistingCue ? "accepted-certificate-already-consumed" :
                "accepted-current-positive-already-consumed") : "accepted-current-positive-not-forward";
            return false;
        }
        if(eligibleSameCueRearm &&
            TryRearmAcceptedSameCue(preview,accepted,source,frameMs,currentFootprint))return true;
        if(eligibleSameCueRearm && currentFootprint)
            m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::FootprintDelegated;
        const auto readyRearmReject=m_diagnostics.readyRearmRejectCode;
        const auto readyRearmLayoutMismatchBits=m_diagnostics.readyRearmLayoutMismatchBits;
        auto seed=preview;seed.current.text=accepted;seed.text=accepted;
        Resolve(seed,source,frameMs,holdMs,nullptr,false,currentFootprint);
        m_diagnostics.readyRearmRejectCode=readyRearmReject;
        m_diagnostics.readyRearmLayoutMismatchBits=readyRearmLayoutMismatchBits;
        if(HasPixelSeed() && !resolvedCovers && certifiedExistingCue) {
            m_certifiedSeed=now;m_certifiedPolicy=preview.policyGeneration;
            m_certifiedContinuity=preview.continuityGeneration;
            m_consumedCertificate=*existingCueCertificate;
            m_reason="accepted-existing-cue-certified";
        }
        return HasPixelSeed();
    }
    // Consume already verified cadence pixels only after the tracker accepts
    // the exact same cue. Advance the last-verified source, never the seed,
    // native samples, continuity generation or original pending budget.
    bool AdvanceVerifiedCadence(const SubtitleCadenceProof& cadence,
        const SubtitleBoxResult& accepted,double frameMs,int holdMs) {
        const auto& p=cadence.continuation;const auto& v=cadence.preview;
        const auto& c=v.current;const auto& old=m_reference.current;
        const auto sameContext=[&]() {
            const auto& a=old.identity;const auto& b=c.identity;
            return a.transportGeneration==b.transportGeneration &&
                a.sourceFormatGeneration==b.sourceFormatGeneration && a.analysisDomainKey==b.analysisDomainKey &&
                a.viewportGeneration==b.viewportGeneration && a.rendererGeneration==b.rendererGeneration &&
                old.width==c.width && old.height==c.height && old.nearBarDistance==c.nearBarDistance &&
                old.scriptHint==c.scriptHint && old.optimizationMode==c.optimizationMode &&
                old.pictureTop==c.pictureTop && old.pictureBottom==c.pictureBottom &&
                old.policyGeneration==c.policyGeneration && old.continuityGeneration==c.continuityGeneration &&
                m_reference.policyGeneration==v.policyGeneration && m_reference.continuityGeneration==v.continuityGeneration;
        };
        if(!HasPixelSeed() || !cadence.valid || !v.available || !c.analyzed ||
            !c.cadenceSkipped || c.analysisStride!=2 || !c.pendingRefresh ||
            c.analysisRefresh || c.discontinuity || c.text.workLimit || accepted.workLimit ||
            !p.valid || p.localGlyphsOnly || !p.samples || !cadence.samples ||
            !p.MatchesCue(accepted) || !p.MatchesCue(m_reference.text) ||
            !SameAnalysisFrameIdentity(p.identity,c.identity) || !sameContext() ||
            p.policyGeneration!=v.policyGeneration || p.continuityGeneration!=v.continuityGeneration ||
            !p.Matches(c.identity,v.policyGeneration,v.continuityGeneration,old.sharedPicture.bounds) ||
            !c.sharedPicture.AvailableFor(c.identity,c.width,c.height) ||
            !c.sharedPicture.Matches(old.sharedPicture) ||
            !std::isfinite(frameMs) || frameMs<=0 || !std::isfinite(p.elapsedMs) ||
            std::abs(p.elapsedMs-frameMs)>0.001)
            return Reject("cadence-advance-context-changed");
        const double pendingBudget=(std::max)(0,(std::min)(1000,holdMs));
        if(m_elapsed>pendingBudget)return Reject("pending-budget-expired");
        if(SameAnalysisFrameIdentity(m_last,c.identity))return true; // Retry spends no time twice.
        if(!SameAnalysisFrameIdentity(p.previousIdentity,m_last) ||
            !SameAnalysisFrameIdentity(p.templateIdentity,m_last) ||
            m_last.acceptedSequence==UINT64_MAX || c.identity.acceptedSequence!=m_last.acceptedSequence+1 ||
            !m_last.sourceFrameNumber || m_last.sourceFrameNumber==UINT64_MAX ||
            c.identity.sourceFrameNumber!=m_last.sourceFrameNumber+1 ||
            c.identity.captureTimestamp<=m_last.captureTimestamp)
            return Reject("cadence-advance-source-gap");
        if(m_elapsed+frameMs>pendingBudget)
            return Reject("pending-budget-expired");
        m_elapsed+=frameMs;m_last=c.identity;m_reason="cadence-last-source-advanced";
        return true;
    }
    bool Resolve(SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        double frameMs,int holdMs,
        const ActivePictureFrameIdentity* backlogTemplateIdentity=nullptr,
        bool backlogRetirementOnly=false,
        const SubtitleCadenceGuard* currentFootprint=nullptr) {
        struct FootprintReceiptScope {
            const SubtitleCadenceGuard* guard=nullptr;
            bool keep=false;
            ~FootprintReceiptScope(){if(guard && !keep)guard->ClearCurrentFootprintProtectionReceipt();}
        } receiptScope{currentFootprint,false};
        if(currentFootprint)currentFootprint->ClearCurrentFootprintProtectionReceipt();
        m_diagnostics={};
        const auto key=preview.current;
        m_diagnostics.lastVerifiedSource=m_last.acceptedSequence;
        m_diagnostics.tokenSource=backlogTemplateIdentity?backlogTemplateIdentity->acceptedSequence:0;
        m_diagnostics.backlogTokenAvailable=backlogRetirementOnly && backlogTemplateIdentity;
        m_diagnostics.referenceContinuity=m_reference.continuityGeneration;
        m_diagnostics.currentContinuity=preview.continuityGeneration;
        if(preview.available) {
            if(key.pendingRefresh)return false; // A repeated presentation cannot renew the budget.
            if(key.analyzed && !key.glyphLocalTop && !key.glyphLocalBottom &&
                key.text.detected && key.barAuthority && !key.text.workLimit) {
                const bool requireFootprint=currentFootprint!=nullptr;
                if(requireFootprint && !currentFootprint->MatchesAcceptedSeed(
                    preview,preview.text,source,key.sharedPicture.bounds))
                    return Reject("accepted-current-footprint-seed-unavailable");
                m_reference=preview;m_samples.clear();m_elapsed=0;m_last=key.identity;
                m_ownedSamples.clear();m_backingSamples.clear();m_knownInkSamples.clear();m_paletteFringe.clear();
                m_currentInkProofSeeded=false;m_currentFootprintRequired=false;
                m_sourceLayout={};
                const auto currentInkSeed=SeedCurrentInkProof(preview,source,!requireFootprint,currentFootprint);
                if(currentInkSeed==CurrentInkSeedState::Seeded) {
                    m_currentFootprintRequired=requireFootprint;
                    m_reason="accepted-current-ink-seed";
                    return false;
                }
                if(requireFootprint)
                    return Reject("accepted-current-footprint-seed-unavailable");
                if(currentInkSeed==CurrentInkSeedState::Failed)
                    return Reject("accepted-current-ink-seed-unavailable");
                const int step=SubtitleBoxDetector::SamplingStep(source.width,source.height);
                for(int line=0;line<key.text.lineCount && line<3;++line) {
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
        std::shared_ptr<SubtitleInkSnapshot> currentInkForOutput;
        frameMs=std::isfinite(frameMs)&&frameMs>0?frameMs:1000.0/60;
        if(!HasPixelSeed())return Reject("no-accepted-seed");
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
        if(m_currentInkProofSeeded) {
            const char* proofFailure="current-ink-proof-unavailable";
            if(m_currentFootprintRequired!=static_cast<bool>(currentFootprint))
                return Reject("current-footprint-mode-changed");
            if(!BuildCurrentInkProof(source,key,currentInkForOutput,proofFailure,currentFootprint)) {
                if(currentFootprint)currentFootprint->ClearCurrentFootprintProtectionReceipt();
                return Reject(proofFailure);
            }
            // This current-source continuation witness is not a detector pass.
            // Keep rawEvidenceComplete clear so it cannot mint current semantic,
            // acquisition, absence, or native authority.
        } else {
            if(m_currentFootprintRequired || currentFootprint)
                return Reject("current-footprint-required-unavailable");
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
            }
        }
        SubtitleBarEvidence bars;
        m_diagnostics.expectedTop=old.pictureTop;
        m_diagnostics.expectedBottom=old.pictureBottom;
        if(key.sharedPicture.required) {
            m_diagnostics.sharedPictureMatches=key.sharedPicture.Matches(old.sharedPicture);
            m_diagnostics.sharedPictureCurrent=
                key.sharedPicture.AvailableFor(key.identity,source.width,source.height);
            if(!m_diagnostics.sharedPictureMatches || !m_diagnostics.sharedPictureCurrent)
                return Reject("shared-picture-changed");
            // Matching retained Classic geometry does not prove that its bar
            // planes still exist on this frame. Pending reuse needs the same
            // current-pixel proof as a complete shared measurement, and the
            // effective analysis planes must equal the accepted cue's planes.
            // Pending render-thread reuse cannot admit nearby/reference proof;
            // leave the extra search to the asynchronous full measurement.
            bars=ResolveCurrentSubtitleAnalysisBars(source,ExtractSubtitleBarEvidence(source),
                key.sharedPicture.bounds.top,key.sharedPicture.bounds.bottom,nullptr,nullptr,false);
            m_diagnostics.barsAvailable=bars.available;
            m_diagnostics.topRevalidated=bars.topRevalidatedAtReference;
            m_diagnostics.bottomRevalidated=bars.bottomRevalidatedAtReference;
            m_diagnostics.measuredTop=bars.top;
            m_diagnostics.measuredBottom=bars.bottom;
            // No fresh detector ink exists on a pending frame. A fixed-plane
            // revalidation could otherwise mistake the old glyphs themselves
            // for an encoded edge after the physical bar disappears.
            if(!bars.available || bars.topRevalidatedAtReference || bars.topRevalidatedNearby ||
                bars.bottomRevalidatedAtReference ||
                bars.bottomRevalidatedNearby ||
                bars.top!=old.pictureTop || bars.bottom!=old.pictureBottom)
                return Reject("current-bars-changed");
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
        if(currentInkForOutput)preview.current.ink=std::move(currentInkForOutput);
        preview.current.sharedPicture=key.sharedPicture;
        preview.current.barEvidence=bars;preview.current.barTrackingAuthority=false;
        preview.current.currentAnchorUsesTrackedEdge=false;preview.current.hasBarTrackingReference=false;
        preview.current.analysisMs=0;preview.current.analysisRefresh=false;
        preview.current.pendingRefresh=true;
        preview.current.incomingBarTrackingReference={};
        preview.current.subtitleArProof={};preview.current.subtitleArDarkness={};
        preview.current.barStripClearance={};
        if(m_currentFootprintRequired) {
            // Current operation pixels are proved; no new physical boundary
            // measurement or caption absence has been performed on this source.
            preview.current.independentBarAnalysis=false;
            preview.current.barAuthority=false;preview.current.barEvidence={};
        }
        preview.futureAvailable=false;preview.following={};preview.followingCount=0;preview.matchingFrames=1;
        preview.currentLinesConfirmed=false;preview.newScanMs=0;preview.newScanFrames=0;
        m_diagnostics.backlogReassociated=backlogReassociation;
        m_reason=backlogReassociation ? "backlog-verified-current-pixels" : "verified-current-pixels";
        receiptScope.keep=currentFootprint!=nullptr;
        return true;
    }
private:
    bool HasPixelSeed() const { return !m_samples.empty() || m_currentInkProofSeeded; }
    static bool SameCueRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    }
    static bool SameRearmPhysicalBase(const ActivePictureBounds& a,const ActivePictureBounds& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight &&
            a.aspectRatio==b.aspectRatio && a.trustedBarAxes==b.trustedBarAxes;
    }
    static bool SameCueLayout(const SubtitleBoxResult& a,const SubtitleBoxResult& b,bool compareCue=true) {
        if(!a.detected || !b.detected || (compareCue && a.cue!=b.cue) || a.lineCount!=b.lineCount ||
            a.capturePanelMeasured!=b.capturePanelMeasured ||
            !SameCueRect(a.bounds,b.bounds) || !SameCueRect(a.anchor,b.anchor) ||
            !SameCueRect(a.sourcePanel,b.sourcePanel) || !SameCueRect(a.provenSourcePanel,b.provenSourcePanel) ||
            !SameCueRect(a.capturePanel,b.capturePanel) || a.signature!=b.signature ||
            a.anchorSignature!=b.anchorSignature || a.topSearchComplete!=b.topSearchComplete ||
            a.bottomSearchComplete!=b.bottomSearchComplete ||
            a.nearBarEligibilityMeasured!=b.nearBarEligibilityMeasured ||
            a.nearBarEligible!=b.nearBarEligible)return false;
        for(int line=0;line<a.lineCount && line<3;++line)
            if(!SameCueRect(a.lineBounds[line],b.lineBounds[line]) ||
                a.lineSignatures[line]!=b.lineSignatures[line] ||
                !SameCueRect(a.linePanels[line],b.linePanels[line]) ||
                !SameCueRect(a.provenLinePanels[line],b.provenLinePanels[line]) ||
                !SameCueRect(a.lineCapturePanels[line],b.lineCapturePanels[line]) ||
                a.panelTopEdges[line].y!=b.panelTopEdges[line].y ||
                a.panelTopEdges[line].left!=b.panelTopEdges[line].left ||
                a.panelTopEdges[line].right!=b.panelTopEdges[line].right ||
                a.panelTopEdges[line].supportBasisPoints!=b.panelTopEdges[line].supportBasisPoints)return false;
        return true;
    }
    static uint32_t CueLayoutMismatchBits(const SubtitleBoxResult& a,const SubtitleBoxResult& b,
        bool compareCue=true) {
        uint32_t bits=0;
        if(!a.detected || !b.detected || (compareCue && a.cue!=b.cue))bits|=ReadyLayoutCueIdentity;
        if(a.lineCount!=b.lineCount)bits|=ReadyLayoutLines;
        if(!SameCueRect(a.bounds,b.bounds) || !SameCueRect(a.anchor,b.anchor))bits|=ReadyLayoutBounds;
        if(!SameCueRect(a.sourcePanel,b.sourcePanel) || !SameCueRect(a.provenSourcePanel,b.provenSourcePanel))
            bits|=ReadyLayoutPanels;
        if(a.capturePanelMeasured!=b.capturePanelMeasured || !SameCueRect(a.capturePanel,b.capturePanel))
            bits|=ReadyLayoutCapture;
        if(a.signature!=b.signature || a.anchorSignature!=b.anchorSignature ||
            a.lineSignatures!=b.lineSignatures)bits|=ReadyLayoutSignatures;
        for(int line=0;line<a.lineCount && line<3;++line) {
            if(!SameCueRect(a.linePanels[line],b.linePanels[line]) ||
                !SameCueRect(a.provenLinePanels[line],b.provenLinePanels[line]))bits|=ReadyLayoutPanels;
            if(!SameCueRect(a.lineCapturePanels[line],b.lineCapturePanels[line]))bits|=ReadyLayoutCapture;
            if(!SameCueRect(a.lineBounds[line],b.lineBounds[line]))bits|=ReadyLayoutLines;
            if(a.panelTopEdges[line].y!=b.panelTopEdges[line].y ||
                a.panelTopEdges[line].left!=b.panelTopEdges[line].left ||
                a.panelTopEdges[line].right!=b.panelTopEdges[line].right ||
                a.panelTopEdges[line].supportBasisPoints!=b.panelTopEdges[line].supportBasisPoints)
                bits|=ReadyLayoutEdges;
        }
        if(a.topSearchComplete!=b.topSearchComplete || a.bottomSearchComplete!=b.bottomSearchComplete ||
            a.nearBarEligibilityMeasured!=b.nearBarEligibilityMeasured || a.nearBarEligible!=b.nearBarEligible)
            bits|=ReadyLayoutEdges;
        return bits;
    }
    static ReadyRearmRejectCode CueLayoutCodeForMismatch(uint32_t bits) {
        if(bits&ReadyLayoutCueIdentity)return ReadyRearmRejectCode::CueIdentity;
        if(bits&ReadyLayoutBounds)return ReadyRearmRejectCode::CueBounds;
        if(bits&(ReadyLayoutPanels|ReadyLayoutCapture))return ReadyRearmRejectCode::CuePanelsCapture;
        if(bits&ReadyLayoutSignatures)return ReadyRearmRejectCode::CueSignatures;
        if(bits&ReadyLayoutLines)return ReadyRearmRejectCode::CueLines;
        if(bits&ReadyLayoutEdges)return ReadyRearmRejectCode::CueEdges;
        return ReadyRearmRejectCode::NotAttempted;
    }
    bool TryRearmAcceptedSameCue(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const AnalysisLumaSource& source,double frameMs,const SubtitleCadenceGuard* currentFootprint) {
        m_diagnostics.readyRearmMaskWordsChecked=0;
        m_diagnostics.readyRearmGlyphSamplesChecked=0;
        m_diagnostics.readyRearmKnownSamplesChecked=0;
        m_diagnostics.readyRearmBackingSamplesChecked=0;
        m_diagnostics.readyRearmLayoutMismatchBits=0;
        const auto reject=[&](ReadyRearmRejectCode code) {
            m_diagnostics.readyRearmRejectCode=code;return false;
        };
        if(!m_currentInkProofSeeded || m_currentFootprintRequired || currentFootprint ||
            !m_samples.empty() || !std::isfinite(frameMs) || frameMs<=0)
            return reject(ReadyRearmRejectCode::SeedOrMode);
        const auto& old=m_reference.current;const auto& current=preview.current;
        const auto oldInk=old.ink;const auto ink=current.ink;
        const auto& a=m_last;const auto& b=current.identity;
        if(!oldInk || !ink)return reject(ReadyRearmRejectCode::InkGrid);
        if(!source.IsValid() || !SameSourceLayout(source,m_sourceLayout) ||
            source.width!=old.width || source.height!=old.height || current.width!=old.width || current.height!=old.height)
            return reject(ReadyRearmRejectCode::SourceLayout);
        if(!current.analyzed || (current.analysisStride!=1 && current.analysisStride!=2) || current.cadenceSkipped ||
            current.analysisRefresh || current.pendingRefresh || current.discontinuity ||
            current.glyphLocalTop || current.glyphLocalBottom || current.text.held ||
            current.text.confirmationPending || current.text.revised || current.text.workLimit)
            return reject(ReadyRearmRejectCode::CurrentFlags);
        if(!current.independentBarAnalysis || !current.barAuthority || current.barTrackingAuthority ||
            current.currentAnchorUsesTrackedEdge || !current.barEvidence.available ||
            current.barEvidence.topRevalidatedAtReference || current.barEvidence.topRevalidatedNearby ||
            current.barEvidence.bottomRevalidatedAtReference || current.barEvidence.bottomRevalidatedNearby ||
            current.barEvidence.captionBackedTop || current.barEvidence.captionBackedBottom)
            return reject(ReadyRearmRejectCode::BarEvidence);
        if(current.pictureTop!=current.barEvidence.top || current.pictureBottom!=current.barEvidence.bottom ||
            current.pictureTop!=old.pictureTop || current.pictureBottom!=old.pictureBottom)
            return reject(ReadyRearmRejectCode::PhysicalBounds);
        if(current.policyGeneration!=preview.policyGeneration || old.policyGeneration!=preview.policyGeneration ||
            m_reference.policyGeneration!=preview.policyGeneration)
            return reject(ReadyRearmRejectCode::PolicyGeneration);
        if(current.continuityGeneration!=preview.continuityGeneration || old.continuityGeneration!=preview.continuityGeneration ||
            m_reference.continuityGeneration!=preview.continuityGeneration)
            return reject(ReadyRearmRejectCode::ContinuityGeneration);
        if(!SubtitleBoxLookahead::SameContext(old,current))return reject(ReadyRearmRejectCode::IdentityContext);
        if(!SameRearmPhysicalBase(old.sharedPicture.bounds,current.sharedPicture.bounds))
            return reject(ReadyRearmRejectCode::PhysicalBounds);
        if(!current.sharedPicture.AvailableFor(current.identity,current.width,current.height) ||
            current.sharedPicture.required!=old.sharedPicture.required || !old.sharedPicture.Matches(current.sharedPicture))
            return reject(ReadyRearmRejectCode::IdentityContext);
        if(!current.text.topSearchComplete || !current.text.bottomSearchComplete)
            return reject(ReadyRearmRejectCode::CueEdges);
        m_diagnostics.readyRearmLayoutMismatchBits=
            CueLayoutMismatchBits(m_reference.text,current.text,false) |
            CueLayoutMismatchBits(m_reference.text,accepted,true) |
            CueLayoutMismatchBits(current.text,accepted,false) |
            CueLayoutMismatchBits(m_reference.text,m_reference.current.text,false);
        if(m_diagnostics.readyRearmLayoutMismatchBits)
            return reject(CueLayoutCodeForMismatch(m_diagnostics.readyRearmLayoutMismatchBits));
        if(b.acceptedSequence<=a.acceptedSequence || !b.sourceFrameNumber || !a.sourceFrameNumber ||
            b.sourceFrameNumber<=a.sourceFrameNumber ||
            b.sourceFrameNumber-a.sourceFrameNumber!=b.acceptedSequence-a.acceptedSequence ||
            b.captureTimestamp<=a.captureTimestamp)return reject(ReadyRearmRejectCode::Sequence);
        if(!ink->rawEvidenceComplete || !ink->paletteValid || !oldInk->rawEvidenceComplete || !oldInk->paletteValid)
            return reject(ReadyRearmRejectCode::InkPalette);
        if(ink->width!=oldInk->width || ink->height!=oldInk->height || ink->step!=oldInk->step ||
            ink->sourceRows!=oldInk->sourceRows)return reject(ReadyRearmRejectCode::InkGrid);
        if(ink->inkFloor!=oldInk->inkFloor || ink->inkU!=oldInk->inkU || ink->inkV!=oldInk->inkV ||
            ink->blackLimit!=oldInk->blackLimit)return reject(ReadyRearmRejectCode::InkPalette);

        const size_t words=(size_t(oldInk->width)*oldInk->height+63)/64;
        if(oldInk->width<=0 || oldInk->height<=0 || oldInk->step<=0 ||
            oldInk->rawInk.size()!=words || oldInk->ownedInk.size()!=words ||
            ink->rawInk.size()!=words || ink->ownedInk.size()!=words ||
            oldInk->blackBacking.size()!=words || ink->blackBacking.size()!=words)
            return reject(ReadyRearmRejectCode::InkGrid);
        for(size_t word=0;word<words;++word) {
            ++m_diagnostics.readyRearmMaskWordsChecked;
            // A ready rearm uses the full current detector mask as a cheap
            // source-current novelty check. Any raw or owned-mask change falls
            // through to the normal complete seed path.
            if(oldInk->rawInk[word]!=ink->rawInk[word])return reject(ReadyRearmRejectCode::RawMask);
            if(oldInk->ownedInk[word]!=ink->ownedInk[word])return reject(ReadyRearmRejectCode::OwnedMask);
        }

        const ValidatedAnalysisLumaSampler sampler(source);
        if(!sampler.IsValid())return reject(ReadyRearmRejectCode::SourceLayout);
        for(const auto& glyph:m_ownedSamples) {
            if(glyph.x<0 || glyph.y<0 || glyph.x>=ink->width || glyph.y>=ink->height ||
                !ink->Get(glyph.x,glyph.y,true))return reject(ReadyRearmRejectCode::GlyphPixels);
            AnalysisLumaSample now;
            if(!sampler.Sample(glyph.x*ink->step,ink->sourceRows[size_t(glyph.y)],now))
                return reject(ReadyRearmRejectCode::GlyphPixels);
            ++m_diagnostics.readyRearmGlyphSamplesChecked;
            if(std::abs(int(now.luma)-glyph.value.luma)>8 ||
                std::abs(int(now.chromaU)-glyph.value.chromaU)>8 ||
                std::abs(int(now.chromaV)-glyph.value.chromaV)>8 || now.luma<ink->inkFloor ||
                std::abs(int(now.chromaU)-ink->inkU)>80 || std::abs(int(now.chromaV)-ink->inkV)>80)
                return reject(ReadyRearmRejectCode::GlyphPixels);
        }
        for(const auto& known:m_knownInkSamples) {
            if(known.x<0 || known.y<0 || known.x>=ink->width || known.y>=ink->height)
                return reject(ReadyRearmRejectCode::KnownPixels);
            AnalysisLumaSample now;
            if(!sampler.Sample(known.x*ink->step,ink->sourceRows[size_t(known.y)],now))
                return reject(ReadyRearmRejectCode::KnownPixels);
            ++m_diagnostics.readyRearmKnownSamplesChecked;
            if(std::abs(int(now.luma)-known.value.luma)>8 ||
                std::abs(int(now.chromaU)-known.value.chromaU)>8 ||
                std::abs(int(now.chromaV)-known.value.chromaV)>8)return reject(ReadyRearmRejectCode::KnownPixels);
        }
        for(const auto& backing:m_backingSamples) {
            if(backing.x<0 || backing.y<0 || backing.x>=ink->width || backing.y>=ink->height)
                return reject(ReadyRearmRejectCode::BackingPixels);
            AnalysisLumaSample now;
            if(!sampler.Sample(backing.x*ink->step,ink->sourceRows[size_t(backing.y)],now))
                return reject(ReadyRearmRejectCode::BackingPixels);
            ++m_diagnostics.readyRearmBackingSamplesChecked;
            const bool inEncodedBar=backing.y<m_contrastSeedTopEnd || backing.y>=m_contrastSeedBottomStart;
            const bool black=now.luma<=ink->blackLimit && (!inEncodedBar ||
                (std::abs(int(now.chromaU)-512)<=48 && std::abs(int(now.chromaV)-512)<=48));
            if(black!=backing.black)return reject(ReadyRearmRejectCode::BackingPixels);
        }
        m_elapsed=0;m_last=b;m_reason="accepted-same-cue-current-scan-rearmed";
        m_diagnostics.readyRearmRejectCode=ReadyRearmRejectCode::Passed;
        m_diagnostics.lastVerifiedSource=b.acceptedSequence;
        return true;
    }
    struct OwnedSample { int x=0,y=0;AnalysisLumaSample value{}; };
    struct BackingSample { int x=0,y=0;bool black=false; };
    struct KnownInkSample { int x=0,y=0;AnalysisLumaSample value{}; };
    enum class CurrentInkSeedState { NotApplicable, Seeded, Failed };
    struct SourceLayout {
        AnalysisLumaFormat format=AnalysisLumaFormat::P010;
        VideoFrameEncoding encoding=VideoFrameEncoding::UNKNOWN;
        ColorSpace colorspace=ColorSpace::UNKNOWN;
        AnalysisLumaRange range=AnalysisLumaRange::Unknown;
        size_t rowBytes=0,chromaRowBytes=0;
        uint64_t generation=0;
        uint8_t rgbBitAlignment=0;
    };
    static constexpr size_t CurrentInkSampleBudget=327680;
    static bool SameSourceLayout(const AnalysisLumaSource& source,const SourceLayout& layout) {
        return source.format==layout.format && source.encoding==layout.encoding &&
            source.colorspace==layout.colorspace && source.range==layout.range &&
            source.rowBytes==layout.rowBytes && source.chromaRowBytes==layout.chromaRowBytes &&
            source.generation==layout.generation && source.rgbBitAlignment==layout.rgbBitAlignment;
    }
    static SourceLayout CaptureSourceLayout(const AnalysisLumaSource& source) {
        SourceLayout layout;layout.format=source.format;layout.encoding=source.encoding;
        layout.colorspace=source.colorspace;layout.range=source.range;layout.rowBytes=source.rowBytes;
        layout.chromaRowBytes=source.chromaRowBytes;layout.generation=source.generation;
        layout.rgbBitAlignment=source.rgbBitAlignment;return layout;
    }
    static bool InCurrentBand(int y,int height,int topEnd,int bottomStart) {
        return y>=0 && y<height && (y<topEnd || y>=bottomStart);
    }
    static size_t CurrentBandCellCount(int width,int height,int topEnd,int bottomStart) {
        const size_t rows=topEnd>=bottomStart?size_t(height):size_t(topEnd)+size_t(height-bottomStart);
        return rows*size_t(width);
    }
    static size_t CurrentBandIndex(int x,int y,int width,int height,int topEnd,int bottomStart) {
        if(topEnd>=bottomStart)return size_t(y)*size_t(width)+size_t(x);
        const size_t row=y<topEnd?size_t(y):size_t(topEnd+y-bottomStart);
        return row*size_t(width)+size_t(x);
    }
    bool CurrentInkGeometry(const SubtitleBoxObservation& observation,
        const SubtitleInkSnapshot& ink,int& topEnd,int& bottomStart) const {
        const int width=observation.width,height=observation.height,step=ink.step;
        if(width<1 || height<1 || step<1 || step>8 || ink.width!=(width+step-1)/step ||
            ink.height!=(height+step-1)/step || step!=SubtitleBoxDetector::SamplingStep(width,height) ||
            ink.sourceRows.size()!=size_t(ink.height) || observation.pictureTop<0 ||
            observation.pictureBottom>height || observation.pictureTop>=observation.pictureBottom ||
            observation.text.topSearchComplete!=(observation.pictureTop>0) ||
            observation.text.bottomSearchComplete!=(observation.pictureBottom<height))return false;
        const int top=(observation.pictureTop+step-1)/step;
        const int bottom=observation.pictureBottom<height?
            (std::min)(ink.height-1,(observation.pictureBottom+step-1)/step):ink.height;
        const int depth=(std::max)(24,ink.height/7)+
            (observation.nearBarDistance+step-1)/step;
        topEnd=observation.pictureTop>0?(std::min)(ink.height,top+depth):0;
        bottomStart=observation.pictureBottom<height?(std::max)(0,bottom-depth):ink.height;
        for(int y=0;y<ink.height;++y) {
            const int expected=observation.pictureTop>0 && y==top-1?observation.pictureTop-1:
                (observation.pictureBottom<height && y==bottom?observation.pictureBottom:
                    (std::min)(height-1,y*step));
            if(ink.sourceRows[size_t(y)]!=expected)return false;
        }
        return true;
    }
    CurrentInkSeedState SeedCurrentInkProof(const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        bool seedDirectionalBaseline=true,const SubtitleCadenceGuard* currentFootprint=nullptr) {
        const auto& current=preview.current;const auto& text=current.text;const auto ink=current.ink;
        if(!preview.available || !current.analyzed || current.pendingRefresh || current.analysisRefresh ||
            current.discontinuity || !current.independentBarAnalysis || !current.barAuthority ||
            current.barTrackingAuthority || current.currentAnchorUsesTrackedEdge ||
            (seedDirectionalBaseline?current.analysisStride!=1:
                (current.analysisStride!=1 && current.analysisStride!=2)) || current.cadenceSkipped ||
            (!seedDirectionalBaseline && !currentFootprint) || !current.barEvidence.available ||
            current.pictureTop!=current.barEvidence.top || current.pictureBottom!=current.barEvidence.bottom ||
            current.barEvidence.captionBackedTop || current.barEvidence.captionBackedBottom ||
            !text.detected || text.held || text.confirmationPending || text.revised || text.workLimit ||
            text.lineCount<1 || text.lineCount>3 ||
            !ink || !ink->rawEvidenceComplete ||
            !ink->paletteValid || !source.IsValid() || source.width!=current.width ||
            source.height!=current.height || ink->inkFloor<0 || ink->inkFloor>1023 ||
            ink->inkU<0 || ink->inkU>1023 || ink->inkV<0 || ink->inkV>1023 ||
            ink->blackLimit<0 || ink->blackLimit>1023)return CurrentInkSeedState::NotApplicable;
        if(!text.topSearchComplete || !text.bottomSearchComplete)return CurrentInkSeedState::Failed;
        int topEnd=0,bottomStart=0;
        if(!CurrentInkGeometry(current,*ink,topEnd,bottomStart))return CurrentInkSeedState::Failed;
        const size_t area=size_t(ink->width)*size_t(ink->height),words=(area+63)/64;
        if(ink->rawInk.size()!=words || ink->ownedInk.size()!=words || ink->blackBacking.size()!=words)
            return CurrentInkSeedState::Failed;
        const ValidatedAnalysisLumaSampler sampler(source);
        if(!sampler.IsValid())return CurrentInkSeedState::Failed;
        const int top=(current.pictureTop+ink->step-1)/ink->step;
        const int bottom=current.pictureBottom<current.height?
            (std::min)(ink->height-1,(current.pictureBottom+ink->step-1)/ink->step):ink->height;
        const size_t bandCells=CurrentBandCellCount(ink->width,ink->height,topEnd,bottomStart);
        if(!bandCells || bandCells>CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
        m_contrastSeedPixels.clear();m_contrastSeedDirections.clear();
        m_contrastSeedTopEnd=m_contrastSeedBottomStart=m_contrastSeedPictureTop=m_contrastSeedPictureBottom=0;
        m_contrastSeedWidth=m_contrastSeedHeight=0;
        if(seedDirectionalBaseline) {
            m_contrastSeedPixels.assign(bandCells,AnalysisLumaSample{});
            m_contrastSeedDirections.assign(bandCells,0);
            m_contrastSeedTopEnd=topEnd;m_contrastSeedBottomStart=bottomStart;
            m_contrastSeedPictureTop=top;m_contrastSeedPictureBottom=bottom;
            m_contrastSeedWidth=ink->width;m_contrastSeedHeight=ink->height;
            for(int y=0;y<ink->height;++y)if(InCurrentBand(y,ink->height,topEnd,bottomStart))
                for(int x=0;x<ink->width;++x) {
                    const size_t at=CurrentBandIndex(x,y,ink->width,ink->height,topEnd,bottomStart);
                    if(at>=m_contrastSeedPixels.size() ||
                        !sampler.Sample(x*ink->step,ink->sourceRows[size_t(y)],m_contrastSeedPixels[at]))
                        return CurrentInkSeedState::Failed;
                    ++m_diagnostics.seedBandSamplesChecked;
                }
            constexpr int contrastX[4]={-1,1,0,0},contrastY[4]={0,0,-1,1};
            for(int y=0;y<ink->height;++y)if(InCurrentBand(y,ink->height,topEnd,bottomStart))
                for(int x=1;x<ink->width-1;++x) {
                    const size_t at=CurrentBandIndex(x,y,ink->width,ink->height,topEnd,bottomStart);
                    const auto& point=m_contrastSeedPixels[at];
                    const bool pointInPicture=y>=top && y<bottom;
                    for(int direction=0;direction<4;++direction) {
                        const int nx=x+contrastX[direction],ny=y+contrastY[direction];
                        if(!InCurrentBand(ny,ink->height,topEnd,bottomStart))continue;
                        const bool neighborInPicture=ny>=top && ny<bottom;
                        if(pointInPicture!=neighborInPicture)continue;
                        const size_t neighborAt=CurrentBandIndex(nx,ny,ink->width,ink->height,topEnd,bottomStart);
                        const auto& neighbor=m_contrastSeedPixels[neighborAt];
                        if(int(point.luma)-int(neighbor.luma)>=48)
                            m_contrastSeedDirections[at]|=uint8_t(1u<<direction);
                        const bool alternatePalette=(std::abs(int(point.chromaU)-ink->inkU)>80 ||
                            std::abs(int(point.chromaV)-ink->inkV)>80) &&
                            (std::abs(int(point.chromaU)-int(neighbor.chromaU))>=64 ||
                             std::abs(int(point.chromaV)-int(neighbor.chromaV))>=64);
                        if(alternatePalette)m_contrastSeedDirections[at]|=uint8_t(1u<<(direction+4));
                    }
                }
        }
        else if(!currentFootprint->MatchesAcceptedSeed(preview,text,source,current.sharedPicture.bounds))
            return CurrentInkSeedState::Failed;
        const auto sampleSeed=[&](int x,int y,AnalysisLumaSample& value) {
            if(x<0 || y<0 || x>=ink->width || y>=ink->height)return false;
            if(seedDirectionalBaseline && InCurrentBand(y,ink->height,topEnd,bottomStart)) {
                const size_t at=CurrentBandIndex(x,y,ink->width,ink->height,topEnd,bottomStart);
                if(at>=m_contrastSeedPixels.size())return false;
                value=m_contrastSeedPixels[at];return true;
            }
            return sampler.Sample(x*ink->step,ink->sourceRows[size_t(y)],value);
        };
        std::vector<KnownInkSample> knownInk;knownInk.reserve(4096);
        for(int y=0;y<ink->height;++y)for(int x=0;x<ink->width;++x)if(ink->Get(x,y) &&
            (!currentFootprint || currentFootprint->ContainsFootprintSample(x*ink->step,ink->sourceRows[size_t(y)]))) {
            if(knownInk.size()>=CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
            AnalysisLumaSample sample;
            if(!sampleSeed(x,y,sample))return CurrentInkSeedState::Failed;
            knownInk.push_back({x,y,sample});
        }
        std::vector<uint64_t> paletteFringe(words,0);
        size_t fringeSamples=0;
        for(int y=0;y<ink->height;++y)if(y<topEnd || y>=bottomStart)for(int x=1;x<ink->width-1;++x) {
            if(ink->Get(x,y))continue;
            if(currentFootprint && !currentFootprint->ContainsFootprintSample(x*ink->step,ink->sourceRows[size_t(y)]))continue;
            bool nextToKnownRaw=false;
            for(int dy=-1;dy<=1 && !nextToKnownRaw;++dy)for(int dx=-1;dx<=1;++dx)
                if((dx || dy) && ink->Get(x+dx,y+dy)){nextToKnownRaw=true;break;}
            if(!nextToKnownRaw)continue;
            if(++fringeSamples>CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
            AnalysisLumaSample sample;
            if(!sampleSeed(x,y,sample))return CurrentInkSeedState::Failed;
            if(sample.luma>=ink->inkFloor-80 &&
                std::abs(int(sample.chromaU)-ink->inkU)<=80 &&
                std::abs(int(sample.chromaV)-ink->inkV)<=80) {
                const size_t at=size_t(y)*ink->width+x;
                paletteFringe[at/64]|=uint64_t{1}<<(at%64);
                if(knownInk.size()>=CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
                knownInk.push_back({x,y,sample});
            }
        }
        std::vector<OwnedSample> owned;owned.reserve(4096);
        std::array<unsigned,3> ownedPerLine{};
        for(int line=0;line<text.lineCount;++line) {
            const auto& bounds=text.lineBounds[line];
            if(!bounds.Valid() || bounds.left<0 || bounds.top<0 ||
                bounds.right>current.width || bounds.bottom>current.height)return CurrentInkSeedState::Failed;
            for(int y=0;y<ink->height;++y) {
                const int py=ink->sourceRows[size_t(y)];
                if(py<bounds.top || py>=bounds.bottom)continue;
                for(int x=0;x<ink->width;++x) {
                    const int px=x*ink->step;
                    if(px<bounds.left || px>=bounds.right || !ink->Get(x,y,true))continue;
                    if(!ink->Get(x,y) || owned.size()>=32768 ||
                        (currentFootprint && !currentFootprint->ContainsFootprintSample(px,py)))
                        return CurrentInkSeedState::Failed;
                    AnalysisLumaSample value;
                    if(!sampleSeed(x,y,value) || value.luma<ink->inkFloor ||
                        std::abs(int(value.chromaU)-ink->inkU)>80 ||
                        std::abs(int(value.chromaV)-ink->inkV)>80)return CurrentInkSeedState::Failed;
                    owned.push_back({x,y,value});++ownedPerLine[size_t(line)];
                }
            }
            if(ownedPerLine[size_t(line)]<4)return CurrentInkSeedState::Failed;
        }
        std::vector<uint8_t> support(area,0);
        const auto addSupport=[&](const SubtitleBoxRect& bounds) {
            if(!bounds.Valid())return true;
            if(bounds.left<0 || bounds.top<0 || bounds.right>current.width || bounds.bottom>current.height)
                return false;
            for(int y=0;y<ink->height;++y) {
                const int py=ink->sourceRows[size_t(y)];
                if(py<bounds.top || py>=bounds.bottom)continue;
                for(int x=0;x<ink->width;++x) {
                    const int px=x*ink->step;
                    if(px>=bounds.left && px<bounds.right)support[size_t(y)*ink->width+x]=1;
                }
            }
            return true;
        };
        if(!addSupport(text.sourcePanel) || !addSupport(text.provenSourcePanel) ||
            !addSupport(text.capturePanel))return CurrentInkSeedState::Failed;
        for(int line=0;line<3;++line)
            if(!addSupport(text.linePanels[line]) || !addSupport(text.provenLinePanels[line]) ||
                !addSupport(text.lineCapturePanels[line]))return CurrentInkSeedState::Failed;
        std::vector<BackingSample> backing;backing.reserve(4096);
        for(int y=0;y<ink->height;++y)for(int x=0;x<ink->width;++x) {
            if(!support[size_t(y)*ink->width+x] ||
                (currentFootprint && !currentFootprint->ContainsFootprintSample(x*ink->step,ink->sourceRows[size_t(y)])))continue;
            if(y<topEnd || y>=bottomStart)backing.push_back({x,y,ink->Black(x,y)});
            else {
                AnalysisLumaSample value;
                if(!sampleSeed(x,y,value))return CurrentInkSeedState::Failed;
                backing.push_back({x,y,value.luma<=ink->blackLimit});
            }
            if(backing.size()>CurrentInkSampleBudget)return CurrentInkSeedState::Failed;
        }
        m_ownedSamples=std::move(owned);m_backingSamples=std::move(backing);
        m_knownInkSamples=std::move(knownInk);
        m_paletteFringe=std::move(paletteFringe);
        m_sourceLayout=CaptureSourceLayout(source);m_currentInkProofSeeded=true;
        return CurrentInkSeedState::Seeded;
    }
    bool BuildCurrentInkProof(const AnalysisLumaSource& source,const SubtitleBoxObservation& current,
        std::shared_ptr<SubtitleInkSnapshot>& currentInk,const char*& reason,
        const SubtitleCadenceGuard* currentFootprint) {
        const auto& old=m_reference.current;const auto oldInk=old.ink;
        if(!m_currentInkProofSeeded || !oldInk || !SameSourceLayout(source,m_sourceLayout) ||
            source.width!=old.width || source.height!=old.height ||
            current.width!=old.width || current.height!=old.height) {
            reason="current-source-layout-changed";return false;
        }
        if(m_currentFootprintRequired) {
            if(!currentFootprint) {reason="current-footprint-required-unavailable";return false;}
            const auto verified=currentFootprint->VerifyCurrentFootprint(
                current,source,old,m_reference.text,old.sharedPicture.bounds);
            if(!verified.valid) {
                m_diagnostics.glyphMismatchX=verified.mismatchX;
                m_diagnostics.glyphMismatchY=verified.mismatchY;
                m_diagnostics.currentBandSamplesChecked=verified.samples;
                reason=verified.reason;return false;
            }
        } else if(currentFootprint) {
            reason="current-footprint-mode-changed";return false;
        }
        int topEnd=0,bottomStart=0;
        if(!CurrentInkGeometry(old,*oldInk,topEnd,bottomStart)) {
            reason="current-search-coverage-unavailable";return false;
        }
        const ValidatedAnalysisLumaSampler sampler(source);
        if(!sampler.IsValid()) {reason="current-source-layout-changed";return false;}
        const size_t area=size_t(oldInk->width)*size_t(oldInk->height),words=(area+63)/64;
        if(area>1048576 || oldInk->rawInk.size()!=words || oldInk->ownedInk.size()!=words ||
            oldInk->blackBacking.size()!=words) {reason="current-ink-grid-invalid";return false;}
        auto fresh=std::make_shared<SubtitleInkSnapshot>();
        fresh->width=oldInk->width;fresh->height=oldInk->height;fresh->step=oldInk->step;
        fresh->sourceRows=oldInk->sourceRows;fresh->inkFloor=oldInk->inkFloor;
        fresh->inkU=oldInk->inkU;fresh->inkV=oldInk->inkV;fresh->blackLimit=oldInk->blackLimit;
        fresh->paletteValid=true;fresh->rawEvidenceComplete=false;
        fresh->rawInk.assign(words,0);fresh->ownedInk.assign(words,0);fresh->blackBacking.assign(words,0);
        const int top=(old.pictureTop+fresh->step-1)/fresh->step;
        const int bottom=old.pictureBottom<old.height?
            (std::min)(fresh->height-1,(old.pictureBottom+fresh->step-1)/fresh->step):fresh->height;
        m_currentPixels.resize(area);m_currentSampled.resize(area);
        std::fill(m_currentSampled.begin(),m_currentSampled.end(),uint8_t(0));
        auto& pixels=m_currentPixels;auto& sampled=m_currentSampled;
        size_t count=0;
        const auto read=[&](int x,int y,AnalysisLumaSample& value)->bool {
            if(x<0 || y<0 || x>=fresh->width || y>=fresh->height)return false;
            const size_t at=size_t(y)*fresh->width+x;
            if(!sampled[at]) {
                if(count>=CurrentInkSampleBudget)return false;
                if(!sampler.Sample(x*fresh->step,fresh->sourceRows[size_t(y)],pixels[at]))return false;
                sampled[at]=1;++count;
                m_diagnostics.currentBandSamplesChecked=count;
                if(pixels[at].luma<=fresh->blackLimit)
                    fresh->blackBacking[at/64]|=uint64_t{1}<<(at%64);
            }
            value=pixels[at];return true;
        };
        for(int y=0;y<fresh->height;++y)if((y<topEnd || y>=bottomStart) &&
            (!m_currentFootprintRequired || y<top || y>=bottom))
            for(int x=0;x<fresh->width;++x) {
                AnalysisLumaSample unused;if(!read(x,y,unused)) {
                    reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;
                }
            }
        for(const auto& backing:m_backingSamples) {
            if(m_currentFootprintRequired && currentFootprint &&
                !currentFootprint->ContainsVerifiedFootprintSample(
                    backing.x*fresh->step,fresh->sourceRows[size_t(backing.y)]))continue;
            AnalysisLumaSample value;
            if(!read(backing.x,backing.y,value)) {
                reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-backing-sample-failed";return false;
            }
            ++m_diagnostics.backingSamplesChecked;
            const size_t at=size_t(backing.y)*fresh->width+backing.x;
            const bool black=value.luma<=fresh->blackLimit;
            if(black!=backing.black) {
                m_diagnostics.glyphMismatchX=backing.x*fresh->step;
                m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(backing.y)];
                reason="current-black-backing-changed";return false;
            }
            if(black)fresh->blackBacking[at/64]|=uint64_t{1}<<(at%64);
            else fresh->blackBacking[at/64]&=~(uint64_t{1}<<(at%64));
        }
        for(const auto& known:m_knownInkSamples) {
            if(m_currentFootprintRequired && currentFootprint &&
                !currentFootprint->ContainsVerifiedFootprintSample(
                    known.x*fresh->step,fresh->sourceRows[size_t(known.y)]))continue;
            AnalysisLumaSample value;
            if(!read(known.x,known.y,value)) {
                reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-known-ink-sample-failed";return false;
            }
            if(std::abs(int(value.luma)-known.value.luma)>8 ||
                std::abs(int(value.chromaU)-known.value.chromaU)>8 ||
                std::abs(int(value.chromaV)-known.value.chromaV)>8) {
                m_diagnostics.glyphMismatchX=known.x*fresh->step;
                m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(known.y)];
                reason="current-known-ink-changed";return false;
            }
        }
        const size_t bandCells=CurrentBandCellCount(fresh->width,fresh->height,topEnd,bottomStart);
        if(!m_currentFootprintRequired && (m_contrastSeedWidth!=fresh->width || m_contrastSeedHeight!=fresh->height ||
            m_contrastSeedTopEnd!=topEnd || m_contrastSeedBottomStart!=bottomStart ||
            m_contrastSeedPictureTop!=top || m_contrastSeedPictureBottom!=bottom ||
            m_contrastSeedPixels.size()!=bandCells || m_contrastSeedDirections.size()!=bandCells)) {
            reason="current-contrast-coverage-changed";return false;
        }
        const int radius=(std::max)(3,fresh->height/180);
        for(int y=0;y<fresh->height;++y)if(y<topEnd || y>=bottomStart)for(int x=1;x<fresh->width-1;++x) {
            const bool inNativeFootprint=m_currentFootprintRequired && currentFootprint &&
                currentFootprint->ContainsVerifiedFootprintSample(x*fresh->step,fresh->sourceRows[size_t(y)]);
            if(m_currentFootprintRequired && y>=top && y<bottom && !inNativeFootprint)continue;
            AnalysisLumaSample point;
            if(!read(x,y,point)) {reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;}
            if(point.luma<fresh->inkFloor || std::abs(int(point.chromaU)-fresh->inkU)>80 ||
                std::abs(int(point.chromaV)-fresh->inkV)>80)continue;
            bool raw=y<top || y>=bottom;
            if(!raw) {
                const int x0=(std::max)(0,x-radius),x1=(std::min)(fresh->width-1,x+radius);
                const int y0=(std::max)(y<topEnd?0:bottomStart,y-radius);
                const int y1=(std::min)(y<topEnd?topEnd-1:fresh->height-1,y+radius);
                int dark=point.luma;
                for(int xx=x0;xx<=x1;++xx) {AnalysisLumaSample n;if(!read(xx,y,n)) {
                    reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;}
                    dark=(std::min)(dark,int(n.luma));}
                for(int yy=y0;yy<=y1;++yy) {AnalysisLumaSample n;if(!read(x,yy,n)) {
                    reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-ink-sample-failed";return false;}
                    dark=(std::min)(dark,int(n.luma));}
                raw=int(point.luma)-dark>=48; // Conservative lower bound; false positives reject rather than acquire ink.
            }
            if(!raw)continue;
            const size_t at=size_t(y)*fresh->width+x;const uint64_t bit=uint64_t{1}<<(at%64);
            if(!oldInk->Get(x,y) && (!m_currentFootprintRequired || inNativeFootprint)) {
                m_diagnostics.glyphMismatchX=x*fresh->step;
                m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(y)];
                reason="new-current-raw-ink";return false;
            }
            fresh->rawInk[at/64]|=bit;
        }
        for(const auto& sample:m_ownedSamples) {
            if(m_currentFootprintRequired && currentFootprint &&
                !currentFootprint->ContainsVerifiedFootprintSample(
                    sample.x*fresh->step,fresh->sourceRows[size_t(sample.y)])) {
                reason="current-owned-glyph-protected";return false;
            }
            AnalysisLumaSample now;
            if(!read(sample.x,sample.y,now)) {
                reason=count>=CurrentInkSampleBudget?"current-ink-sample-budget":"current-glyph-sample-failed";return false;
            }
            ++m_diagnostics.glyphSamplesChecked;
            if(std::abs(int(now.luma)-sample.value.luma)>8 ||
                std::abs(int(now.chromaU)-sample.value.chromaU)>8 ||
                std::abs(int(now.chromaV)-sample.value.chromaV)>8 ||
                now.luma<fresh->inkFloor ||
                std::abs(int(now.chromaU)-fresh->inkU)>80 ||
                std::abs(int(now.chromaV)-fresh->inkV)>80) {
                m_diagnostics.glyphMismatchX=sample.x*fresh->step;
                m_diagnostics.glyphMismatchY=fresh->sourceRows[size_t(sample.y)];
                reason="current-owned-glyph-changed";return false;
            }
            const size_t at=size_t(sample.y)*fresh->width+sample.x;
            // Exact current pixels at previously detector-owned positions are
            // valid composition inputs even when a crowded caption row falls
            // outside this source's current subtitle search bands. This only
            // refreshes partial output ink; rawEvidenceComplete stays false.
            const uint64_t bit=uint64_t{1}<<(at%64);
            fresh->rawInk[at/64]|=bit;
            fresh->ownedInk[at/64]|=uint64_t{1}<<(at%64);
        }
        if(m_currentFootprintRequired) {
            if(HasNonblackEncodedBarPixels(*oldInk,pixels,currentFootprint,top,bottom,topEnd,bottomStart,
                m_diagnostics.glyphMismatchX,m_diagnostics.glyphMismatchY)) {
                reason="new-current-bar-ink";return false;
            }
        } else if(HasNovelPotentialInk(*oldInk,m_paletteFringe,pixels,m_contrastSeedPixels,m_contrastSeedDirections,
            top,bottom,topEnd,bottomStart,
            m_diagnostics.glyphMismatchX,m_diagnostics.glyphMismatchY)) {
            reason="new-current-contrast-ink";return false;
        }
        currentInk=std::move(fresh);
        reason="current-owned-cue-verified";return true;
    }
    bool HasNonblackEncodedBarPixels(const SubtitleInkSnapshot& reference,
        const std::vector<AnalysisLumaSample>& currentPixels,const SubtitleCadenceGuard* currentFootprint,
        int top,int bottom,int topEnd,int bottomStart,int& mismatchX,int& mismatchY) const {
        const int width=reference.width,height=reference.height;
        const size_t bandCells=CurrentBandCellCount(width,height,topEnd,bottomStart);
        if(!currentFootprint || currentPixels.size()!=size_t(width)*height || !bandCells)return true;
        for(int y=0;y<height;++y)if(InCurrentBand(y,height,topEnd,bottomStart) && (y<top || y>=bottom))
            for(int x=0;x<width;++x) {
                if(currentFootprint->ContainsFootprintSample(x*reference.step,reference.sourceRows[size_t(y)]))
                    continue; // The pure native witness checked this exact sample.
                const auto& sample=currentPixels[size_t(y)*width+x];
                const bool black=sample.luma<=reference.blackLimit &&
                    std::abs(int(sample.chromaU)-512)<=48 && std::abs(int(sample.chromaV)-512)<=48;
                if(black)continue;
                mismatchX=x*reference.step;mismatchY=reference.sourceRows[size_t(y)];return true;
            }
        return false;
    }
    static bool HasNovelPotentialInk(const SubtitleInkSnapshot& reference,
        const std::vector<uint64_t>& paletteFringe,
        const std::vector<AnalysisLumaSample>& pixels,
        const std::vector<AnalysisLumaSample>& seedPixels,const std::vector<uint8_t>& seedDirections,
        int top,int bottom,int topEnd,int bottomStart,
        int& mismatchX,int& mismatchY) {
        const int width=reference.width,height=reference.height;
        const size_t area=size_t(width)*size_t(height);
        const size_t bandCells=CurrentBandCellCount(width,height,topEnd,bottomStart);
        if(pixels.size()!=area || seedPixels.size()!=bandCells || seedDirections.size()!=bandCells)return true;
        const auto inBand=[&](int y){return InCurrentBand(y,height,topEnd,bottomStart);};
        const auto sameSample=[](const AnalysisLumaSample& a,const AnalysisLumaSample& b) {
            return std::abs(int(a.luma)-int(b.luma))<=8 &&
                std::abs(int(a.chromaU)-int(b.chromaU))<=8 &&
                std::abs(int(a.chromaV)-int(b.chromaV))<=8;
        };
        const auto knownPaletteFringe=[&](int x,int y) {
            if(reference.Get(x,y))return true;
            const size_t at=size_t(y)*width+x;
            if(at/64>=paletteFringe.size() || !(paletteFringe[at/64]&(uint64_t{1}<<(at%64))))return false;
            const auto& point=pixels[size_t(y)*width+x];
            if(point.luma<reference.inkFloor-80 ||
                std::abs(int(point.chromaU)-reference.inkU)>80 ||
                std::abs(int(point.chromaV)-reference.inkV)>80)return false;
            return true;
        };
        constexpr int neighborX[4]={-1,1,0,0},neighborY[4]={0,0,-1,1};
        for(int y=0;y<height;++y)if(inBand(y))for(int x=1;x<width-1;++x) {
            // Existing raw and accepted-palette fringe cells have their own
            // exact current-source YUV checks. Do not let their changed
            // non-owned neighbors turn a legitimate moving margin into a new
            // subtitle; newly changed neighbors are still tested as centers.
            if(knownPaletteFringe(x,y))continue;
            const auto& point=pixels[size_t(y)*width+x];
            // In encoded bars, any newly observed nonblack sample outside the
            // accepted raw mask may be a differently colored or dimmer cue.
            // Reject it even when a single grid cell cannot form a component.
            if((y<top || y>=bottom) && point.luma>reference.blackLimit) {
                mismatchX=x*reference.step;mismatchY=reference.sourceRows[size_t(y)];return true;
            }
            for(int direction=0;direction<4;++direction) {
                const int nx=x+neighborX[direction],ny=y+neighborY[direction];
                if(!inBand(ny))continue;
                const bool pointInPicture=y>=top && y<bottom;
                const bool neighborInPicture=ny>=top && ny<bottom;
                if(pointInPicture!=neighborInPicture)continue;
                const auto& neighbor=pixels[size_t(ny)*width+nx];
                const bool brightLocalInk=int(point.luma)-int(neighbor.luma)>=48;
                const bool alternatePalette=(std::abs(int(point.chromaU)-reference.inkU)>80 ||
                    std::abs(int(point.chromaV)-reference.inkV)>80) &&
                    (std::abs(int(point.chromaU)-int(neighbor.chromaU))>=64 ||
                     std::abs(int(point.chromaV)-int(neighbor.chromaV))>=64);
                if(!brightLocalInk && !alternatePalette)continue;
                const size_t pointAt=CurrentBandIndex(x,y,width,height,topEnd,bottomStart);
                const size_t neighborAt=CurrentBandIndex(nx,ny,width,height,topEnd,bottomStart);
                if(pointAt>=seedPixels.size() || neighborAt>=seedPixels.size() || pointAt>=seedDirections.size()) {
                    mismatchX=x*reference.step;mismatchY=reference.sourceRows[size_t(y)];return true;
                }
                const uint8_t baseline=seedDirections[pointAt];
                const bool sameSamples=sameSample(point,seedPixels[pointAt]) &&
                    sameSample(neighbor,seedPixels[neighborAt]);
                const bool existingBright=!brightLocalInk ||
                    ((baseline&(uint8_t(1u<<direction))) && sameSamples);
                const bool existingAlternate=!alternatePalette ||
                    ((baseline&(uint8_t(1u<<(direction+4)))) && sameSamples);
                if(!existingBright || !existingAlternate) {
                    mismatchX=x*reference.step;mismatchY=reference.sourceRows[size_t(y)];return true;
                }
            }
        }
        return false;
    }
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
    std::vector<OwnedSample> m_ownedSamples;
    std::vector<BackingSample> m_backingSamples;
    std::vector<KnownInkSample> m_knownInkSamples;
    std::vector<uint64_t> m_paletteFringe;
    // Reused per pending frame. Every sampled mark is reset before reuse, and
    // pixel values are read only after the corresponding mark is set current.
    std::vector<AnalysisLumaSample> m_currentPixels;
    std::vector<uint8_t> m_currentSampled;
    // Compact immutable seed snapshot of every sampled search-band pixel and
    // its four directional baseline contrast classifications. High nibbles
    // store alternate-chroma directions; low nibbles store bright-edge ones.
    std::vector<AnalysisLumaSample> m_contrastSeedPixels;
    std::vector<uint8_t> m_contrastSeedDirections;
    int m_contrastSeedTopEnd=0,m_contrastSeedBottomStart=0;
    int m_contrastSeedPictureTop=0,m_contrastSeedPictureBottom=0;
    int m_contrastSeedWidth=0,m_contrastSeedHeight=0;
    SourceLayout m_sourceLayout{};
    bool m_currentInkProofSeeded=false,m_currentFootprintRequired=false;
    SubtitleBoxPreview m_reference;
    SubtitleAcceptedCueSeedCertificate m_consumedCertificate;
    ActivePictureFrameIdentity m_last,m_certifiedSeed;
    uint64_t m_certifiedPolicy=0,m_certifiedContinuity=0;
    std::vector<Sample> m_samples;
    double m_elapsed=0;
};
