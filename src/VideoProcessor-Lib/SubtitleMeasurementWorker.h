#pragma once
#include <SubtitleBoxLookahead.h>
#include <AnalysisDiagnosticHooks.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <fstream>
#include <cstdlib>
#include <mutex>
#include <thread>

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
        // Dark-scene diagnostics must not be vetoed by the classification being
        // investigated. Wait through startup before this explicit capture.
        if(!m_capturedDiagnostic && !capturePath.empty() && source.IsValid() &&
            (captureDark ? key.identity.acceptedSequence>=240 :
                !EvaluateActivePictureGlobalNearBlack(source).nearBlack)) {
            m_capturedDiagnostic=true;
            std::ofstream pixels(capturePath,std::ios::binary);
            pixels.write(reinterpret_cast<const char*>(source.data),source.dataBytes);
            std::ofstream metadata(std::string(capturePath)+".txt");
            metadata<<source.width<<' '<<source.height<<' '<<source.rowBytes<<' '<<source.chromaRowBytes<<' '
                <<static_cast<int>(source.format)<<' '<<static_cast<int>(source.encoding)<<' '
                <<static_cast<int>(source.colorspace)<<' '<<result.pictureTop<<' '<<result.pictureBottom<<' '
                <<static_cast<int>(key.scriptHint)<<'\n';
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
            a.width==b.width && a.height==b.height && a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode && a.scriptHint==b.scriptHint &&
            a.policyGeneration==b.policyGeneration &&
            (a.continuityGeneration==b.continuityGeneration ||
                (!a.pictureAuthorityUnavailableForMeasurement &&
                    !b.pictureAuthorityUnavailableForMeasurement &&
                    !a.independentBarAnalysis && !b.independentBarAnalysis &&
                    a.sharedPicture.required && b.sharedPicture.required)) &&
            a.discontinuity==b.discontinuity && (samePicture || sameUnavailablePictureScan);
    }
    bool TryTake(const SubtitleBoxObservation& key,SubtitleBoxObservation& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t mismatches=0;
        for(auto i=m_ready.begin();i!=m_ready.end();++i) {
            auto candidate=*i;
            if (!SubtitleBoxLookahead::RebindIndependentPicture(candidate,key.sharedPicture) ||
                !SameKey(candidate,key)) {
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
            if(!SubtitleBoxLookahead::RebindIndependentMeasurement(candidate,key.continuityGeneration)){mismatches|=512ull;continue;}
            Trace(candidate,"worker-consumed","exact-key-accepted");out=std::move(candidate);m_ready.erase(i);return true;
        }
        Trace(key,"worker-miss",m_ready.empty()?"no-ready-results":"no-compatible-result",mismatches);
        ++m_diagnostics.requestMisses;return false;
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
        if(m_active && SameKey(m_activeKey,requestKey)){Trace(requestKey,"worker-dedup","active");return;}
        for(const auto& job:m_pending)if(SameKey(job.key,requestKey)){Trace(requestKey,"worker-dedup","queued");return;}
        for(const auto& ready:m_ready)if(SameKey(ready,requestKey)){Trace(requestKey,"worker-dedup","ready");return;}
        if(m_pending.size()>=8){Trace(m_pending.front().key,"worker-drop","pending-capacity");m_pending.pop_front();++m_diagnostics.dropped;}
        Trace(requestKey,"worker-submit");
        m_pending.push_back({requestKey,std::move(work)});
        if(!m_thread.joinable())m_thread=std::thread([this]{ Run(); });
        m_changed.notify_one();
    }
    void Stop() {
        {std::lock_guard<std::mutex> lock(m_mutex);m_stopping=true;m_pending.clear();}
        m_changed.notify_all();
        if(m_thread.joinable())m_thread.join();
    }
private:
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
    SubtitleBoxObservation m_activeKey;
    bool m_active=false,m_stopping=false;
};


// A late asynchronous result is not negative subtitle evidence. Bridge only a
// bounded delay, with fresh bar proof and unchanged native pixels in every
// accepted glyph rectangle. No OCR, spatial search, wait or GPU readback here.
class SubtitlePendingMeasurementGuard {
public:
    void Reset() { m_reference={};m_samples.clear();m_elapsed=0;m_last={}; }
    const char* DecisionReason() const { return m_reason; }
    // Only accepted extraction may seed the late-worker fallback. A held cue
    // can be more complete than either current grouping or lookahead pruning;
    // never turn those partial measurements into a smaller replacement cue.
    bool RememberAccepted(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const AnalysisLumaSource& source,double frameMs,int holdMs) {
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
        if(!preview.available || !preview.current.analyzed || preview.current.pendingRefresh ||
            preview.current.discontinuity || !preview.current.barAuthority ||
            !source.IsValid() || source.width!=preview.current.width || source.height!=preview.current.height ||
            !accepted.detected || accepted.workLimit || accepted.lineCount<1 || accepted.lineCount>3 ||
            !covers(preview.current.text) || !covers(preview.text)) { return Reject("accepted-seed-incomplete"); }
        auto seed=preview;seed.current.text=accepted;seed.text=accepted;
        Resolve(seed,source,frameMs,holdMs);
        return !m_samples.empty();
    }
    bool Resolve(SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        double frameMs,int holdMs) {
        const auto key=preview.current;
        if(preview.available) {
            if(key.pendingRefresh)return false; // A repeated presentation cannot renew the budget.
            if(key.analyzed && key.text.detected && key.barAuthority && !key.text.workLimit) {
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
        if(key.discontinuity || !source.IsValid() ||
            source.width!=key.width || source.height!=key.height || a.transportGeneration!=b.transportGeneration ||
            a.sourceFormatGeneration!=b.sourceFormatGeneration || a.analysisDomainKey!=b.analysisDomainKey ||
            a.viewportGeneration!=b.viewportGeneration ||
            a.rendererGeneration!=b.rendererGeneration || key.width!=old.width || key.height!=old.height ||
            key.nearBarDistance!=old.nearBarDistance || key.scriptHint!=old.scriptHint || key.policyGeneration!=old.policyGeneration ||
            key.continuityGeneration!=old.continuityGeneration) return Reject("pending-context-changed");
        for(const auto& sample:m_samples) {
            AnalysisLumaSample now;
            if(!source.Sample(sample.x,sample.y,now) ||
                std::abs(int(now.luma)-sample.value.luma)>8 ||
                std::abs(int(now.chromaU)-sample.value.chromaU)>8 ||
                std::abs(int(now.chromaV)-sample.value.chromaV)>8) return Reject("current-pixels-changed");
        }
        SubtitleBarEvidence bars;
        if(key.sharedPicture.required) {
            if(!key.sharedPicture.Matches(old.sharedPicture) ||
                !key.sharedPicture.AvailableFor(key.identity,source.width,source.height))
                return Reject("shared-picture-changed");
            // Matching retained Classic geometry does not prove that its bar
            // planes still exist on this frame. Pending reuse needs the same
            // current-pixel proof as a complete shared measurement, and the
            // effective analysis planes must equal the accepted cue's planes.
            bars=ResolveCurrentSubtitleAnalysisBars(source,ExtractSubtitleBarEvidence(source),
                key.sharedPicture.bounds.top,key.sharedPicture.bounds.bottom);
            // No fresh detector ink exists on a pending frame. A fixed-plane
            // revalidation could otherwise mistake the old glyphs themselves
            // for an encoded edge after the physical bar disappears.
            if(!bars.available || bars.topRevalidatedAtReference ||
                bars.bottomRevalidatedAtReference ||
                bars.top!=old.pictureTop || bars.bottom!=old.pictureBottom)
                return Reject("current-bars-changed");
        } else {
            bars=ExtractSubtitleBarEvidence(source);
            if(!bars.available || bars.top!=old.pictureTop || bars.bottom!=old.pictureBottom) return Reject("current-bars-changed");
        }
        m_elapsed+=elapsedMs;m_last=b;
        preview=m_reference;preview.current.identity=b;preview.current.discontinuity=false;
        preview.current.sharedPicture=key.sharedPicture;
        preview.current.barEvidence=bars;preview.current.barTrackingAuthority=false;
        preview.current.currentAnchorUsesTrackedEdge=false;preview.current.hasBarTrackingReference=false;
        preview.current.analysisMs=0;preview.current.pendingRefresh=true;
        preview.futureAvailable=false;preview.followingCount=0;preview.matchingFrames=1;
        preview.currentLinesConfirmed=false;preview.newScanMs=0;preview.newScanFrames=0;
        m_reason="verified-current-pixels";
        return true;
    }
private:
    bool Reject(const char* reason) { Reset();m_reason=reason;return false; }
    const char* m_reason="reset";
    struct Sample { int x,y;AnalysisLumaSample value; };
    SubtitleBoxPreview m_reference;
    ActivePictureFrameIdentity m_last;
    std::vector<Sample> m_samples;
    double m_elapsed=0;
};
