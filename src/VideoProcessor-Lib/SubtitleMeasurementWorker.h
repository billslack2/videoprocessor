#pragma once
#include <SubtitleBoxLookahead.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

// Every source frame gets independent geometry and raw ink. Rechecking only
// old glyph rectangles cannot discover a new word/companion outside them, and
// copied ink must never count as fresh lookahead confirmation. Safe same-frame
// sampled-pixel/buffer reuse remains controlled by the detector's existing mode.
class SubtitleMeasurementSampler {
public:
    SubtitleBoxObservation Measure(const AnalysisLumaSource& source,const SubtitleBoxObservation& key,
        const SubtitleBarTrackingReference& prior,double /*fps*/) {
        const auto started=std::chrono::steady_clock::now();
        m_scanner.SetOptimizationMode(key.optimizationMode);
        m_scanner.SetNearBarDistance(key.nearBarDistance);
        auto result=SubtitleBoxLookahead::Measure(m_scanner,source,{},key.identity,key.discontinuity,
            prior,key.policyGeneration,key.continuityGeneration);
        result.analysisMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return result;
    }
private:
    SubtitleBoxDetector m_scanner;
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
    using Work=std::function<SubtitleBoxObservation(SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&)>;
    ~SubtitleMeasurementWorker() { Stop(); }
    static bool SameKey(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
        return SameActivePictureFrameIdentity(a.identity,b.identity) &&
            a.width==b.width && a.height==b.height && a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode &&
            a.policyGeneration==b.policyGeneration && a.continuityGeneration==b.continuityGeneration &&
            a.discontinuity==b.discontinuity;
    }
    bool TryTake(const SubtitleBoxObservation& key,SubtitleBoxObservation& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for(auto i=m_ready.begin();i!=m_ready.end();++i) if(SameKey(*i,key)) {
            out=std::move(*i);m_ready.erase(i);return true;
        }
        ++m_diagnostics.requestMisses;return false;
    }
    void Submit(const SubtitleBoxObservation& key,Work work) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_stopping)return;
        if(m_active && SameKey(m_activeKey,key))return;
        for(const auto& job:m_pending)if(SameKey(job.key,key))return;
        for(const auto& ready:m_ready)if(SameKey(ready,key))return;
        if(m_pending.size()>=8){m_pending.pop_front();++m_diagnostics.dropped;}
        m_pending.push_back({key,std::move(work)});
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
            const auto workStart=std::chrono::steady_clock::now();
            const double waited=std::chrono::duration<double,std::milli>(workStart-job.queued).count();
            SubtitleBoxObservation result=job.key;
            try {result=job.work(scanner,previous);previous=SubtitleBoxLookahead::AdvanceBarTrackingReference(result);} catch(...) {result.analyzed=false;}
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
                            if(m_samples.size()>=32768 || !source.Sample(x,y,value)) { Reset();return false; }
                            m_samples.push_back({x,y,value});
                        }
                }
            } else Reset();
            return false;
        }
        const auto& old=m_reference.current;
        const auto& a=m_last;const auto& b=key.identity;
        frameMs=std::isfinite(frameMs)&&frameMs>0?frameMs:1000.0/60;
        if(m_samples.empty() || key.discontinuity || !source.IsValid() ||
            a.acceptedSequence+1!=b.acceptedSequence || a.transportGeneration!=b.transportGeneration ||
            a.sourceFormatGeneration!=b.sourceFormatGeneration || a.viewportGeneration!=b.viewportGeneration ||
            a.rendererGeneration!=b.rendererGeneration || key.width!=old.width || key.height!=old.height ||
            key.nearBarDistance!=old.nearBarDistance || key.policyGeneration!=old.policyGeneration ||
            key.continuityGeneration!=old.continuityGeneration ||
            m_elapsed+frameMs>(std::max)(0,(std::min)(1000,holdMs))) { Reset();return false; }
        for(const auto& sample:m_samples) {
            AnalysisLumaSample now;
            if(!source.Sample(sample.x,sample.y,now) ||
                std::abs(int(now.luma)-sample.value.luma)>8 ||
                std::abs(int(now.chromaU)-sample.value.chromaU)>8 ||
                std::abs(int(now.chromaV)-sample.value.chromaV)>8) {Reset();return false;}
        }
        const auto bars=ExtractSubtitleBarEvidence(source);
        if(!bars.available || bars.top!=old.pictureTop || bars.bottom!=old.pictureBottom) {Reset();return false;}
        m_elapsed+=frameMs;m_last=b;
        preview=m_reference;preview.current.identity=b;preview.current.discontinuity=false;
        preview.current.barEvidence=bars;preview.current.barTrackingAuthority=false;
        preview.current.currentAnchorUsesTrackedEdge=false;preview.current.hasBarTrackingReference=false;
        preview.current.analysisMs=0;preview.current.pendingRefresh=true;
        preview.futureAvailable=false;preview.followingCount=0;preview.matchingFrames=1;
        preview.currentLinesConfirmed=false;preview.newScanMs=0;preview.newScanFrames=0;
        return true;
    }
private:
    struct Sample { int x,y;AnalysisLumaSample value; };
    SubtitleBoxPreview m_reference;
    ActivePictureFrameIdentity m_last;
    std::vector<Sample> m_samples;
    double m_elapsed=0;
};
