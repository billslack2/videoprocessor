#pragma once
#include <SubtitleBoxLookahead.h>
#include <AnalysisDiagnosticHooks.h>
#include <DebugLog.h>
#include <algorithm>
#include <atomic>
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
        if (result.independentBarAnalysis && result.text.detected && !result.text.held &&
            !result.text.workLimit) {
            const auto raw=nativeEvidence.available ? nativeEvidence : ExtractActivePictureEvidence(source);
            // Prepared queued pixels use a format generation for cache ownership;
            // the proof contract is bound to the immutable transport identity.
            auto proofSource=source;proofSource.generation=result.identity.transportGeneration;
            result.subtitleArProof=EvaluateSubtitleAssistedVerticalBarProof(proofSource,raw,result,
                result.identity,result.policyGeneration,result.continuityGeneration,result.scriptHint);
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
        result.analysisMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return result;
    }
private:
    SubtitleBoxDetector m_scanner;
    uint64_t m_analysisDomainKey=0;
    bool m_capturedDiagnostic=false;
};

// Bounded CPU-only work. Jobs own their source storage; rendering never waits
// for a scan or consumes a result for another source frame or policy epoch.
class SubtitleMeasurementWorker {
public:
    struct Diagnostics {
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
    void Trace(const SubtitleBoxObservation& key,const char* stage,const char* reason="",uint64_t mismatch=0) const noexcept {
        auto r=AnalysisTraceRecord(key.identity,this,stage,reason);r.kind=AnalysisDiagnosticRecordKindV1::Worker;
        r.policyGeneration=key.policyGeneration;r.continuityGeneration=key.continuityGeneration;
        r.nearBarDistance=key.nearBarDistance;r.optimizationMode=key.optimizationMode;r.scriptHint=static_cast<uint32_t>(key.scriptHint);
        r.sourceWidth=key.width;r.sourceHeight=key.height;r.discontinuity=key.discontinuity;
        r.flags=(key.analyzed?1u:0u)|(key.text.detected?2u:0u)|(key.sharedPicture.required?4u:0u)|
            (key.sharedPicture.AvailableFor(key.identity,key.width,key.height)?8u:0u)|(key.pictureAuthorityUnavailableForMeasurement?16u:0u);
        r.counters[0]=mismatch;r.counters[1]=key.sharedPicture.identity.acceptedSequence;
        r.resultCode=key.text.lineCount;AnalysisTraceRect(r.subtitle,key.text.bounds);
        r.metrics[0]=key.analysisMs;r.metrics[1]=key.pictureTop;r.metrics[2]=key.pictureBottom;
        AnalysisDiagnosticTrace::Emit(r);
    }
    using Work=std::function<SubtitleBoxObservation(SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&)>;
    ~SubtitleMeasurementWorker() { Stop(); }
    static bool SameKey(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
        const bool samePicture=a.sharedPicture.Matches(b.sharedPicture);
        // A worker request with no usable shared snapshot follows the same
        // exact-frame raw-bar path regardless of which unavailable snapshot
        // was published. This lets active/pending work coalesce safely; a
        // newly available authority still requires a separate scan.
        const bool sameUnavailablePictureScan=
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
    // The head has advanced; queued scans for older transport frames cannot
    // become current. The active worker is left alone and remains nonblocking.
    void BeginSourceWindow(const ActivePictureFrameIdentity& head) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_windowHead.transportGeneration>head.transportGeneration ||
            (m_windowHead.transportGeneration==head.transportGeneration &&
                m_windowHead.acceptedSequence>=head.acceptedSequence))return;
        m_windowHead=head;
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
    bool TryTake(const SubtitleBoxObservation& key,SubtitleBoxObservation& out) {
        return ReadReady(key,out,true,false);
    }
    void Submit(const SubtitleBoxObservation& key,Work work) {
        auto requestKey=key;
        // QueueSubtitleMeasurement submits only valid prepared sources. Mark
        // the request's scan path here so active and pending jobs can coalesce
        // before Measure has produced an observation.
        requestKey.pictureAuthorityUnavailableForMeasurement=
            requestKey.sharedPicture.required &&
            !requestKey.sharedPicture.AvailableFor(
                requestKey.identity,requestKey.width,requestKey.height);
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_stopping)return;
        if(requestKey.identity.transportGeneration<m_windowHead.transportGeneration ||
            (requestKey.identity.transportGeneration==m_windowHead.transportGeneration &&
                requestKey.identity.acceptedSequence<m_windowHead.acceptedSequence)) {
            Trace(requestKey,"worker-drop","source-window-expired");
            ++m_diagnostics.expired;return;
        }
        if(m_active && SameKey(m_activeKey,requestKey)){Trace(requestKey,"worker-dedup","active");++m_diagnostics.deduplicated;return;}
        for(const auto& job:m_pending)if(SameKey(job.key,requestKey)){Trace(requestKey,"worker-dedup","queued");++m_diagnostics.deduplicated;return;}
        for(const auto& ready:m_ready)if(SameKey(ready,requestKey)){Trace(requestKey,"worker-dedup","ready");++m_diagnostics.deduplicated;return;}
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
        if(!m_thread.joinable())m_thread=std::thread([this]{ Run(); });
        m_changed.notify_one();
    }
    void Stop() {
        {std::lock_guard<std::mutex> lock(m_mutex);m_stopping=true;m_pending.clear();}
        m_changed.notify_all();
        if(m_thread.joinable())m_thread.join();
    }
private:
    bool ReadReady(const SubtitleBoxObservation& key,SubtitleBoxObservation& out,bool consume,bool backlogRetirementOnly) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t mismatches=0;
        for(auto i=m_ready.begin();i!=m_ready.end();++i) {
            auto candidate=*i;
            if (!SubtitleBoxLookahead::RebindIndependentPicture(candidate,key.sharedPicture)) {
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
            out=std::move(candidate);
            if(consume)m_ready.erase(i);
            else ++m_diagnostics.exactReads;
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
            {std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait(lock,[this]{return m_stopping || !m_pending.empty();});
                if(m_stopping)return;
                job=std::move(m_pending.front());m_pending.pop_front();
                m_active=true;m_activeKey=job.key;
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
                m_active=false;
                if(!m_stopping) {
                    if(m_ready.size()>=16)m_ready.pop_front();
                    m_ready.push_back(std::move(result));
                }
            }
        }
    }
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
        const auto key=preview.current;
        if(preview.available) {
            if(key.pendingRefresh)return false; // A repeated presentation cannot renew the budget.
            if(key.analyzed && !key.glyphLocalTop && !key.glyphLocalBottom &&
                key.text.detected && key.barAuthority && !key.text.workLimit) {
                m_reference=preview;m_samples.clear();m_elapsed=0;m_last=key.identity;
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
        preview.current.sharedPicture=key.sharedPicture;
        preview.current.barEvidence=bars;preview.current.barTrackingAuthority=false;
        preview.current.currentAnchorUsesTrackedEdge=false;preview.current.hasBarTrackingReference=false;
        preview.current.analysisMs=0;preview.current.analysisRefresh=false;
        preview.current.pendingRefresh=true;
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
