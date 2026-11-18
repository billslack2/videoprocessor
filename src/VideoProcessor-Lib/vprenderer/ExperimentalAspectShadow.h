#pragma once
#include "../ExperimentalAspectDetector.h"
#include <AnalysisFrame.h>
#include <ActivePictureDecisionTimeline.h>
#include <DebugLog.h>
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

// VP-0230: no result accessor and no crop/subtitle/presentation authority.
// One retained source TOTAL (pending or active); busy/contended offers are dropped.
class ExperimentalAspectShadow {
    using Clock=std::chrono::steady_clock;
    struct Job {
        PreparedAnalysisFrame frame;
        ActivePictureFrameIdentity identity;
        ActivePictureBounds baseline;
        ActivePictureClassification classification=ActivePictureClassification::UNAVAILABLE;
        bool baselineCurrent=false;
        Clock::time_point offered;
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    Job pending;
    bool enabled=false, stopping=false, busy=false;
    Clock::time_point nextOffer{}; // render-thread only
    std::atomic<uint64_t> dropped{0}, submitPeakUs{0};

    void Run() {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        for(;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock,[&]{return stopping||busy;});
                if(stopping)return;
                job=std::move(pending);
            }
            const auto start=Clock::now();
            const double ageMs=std::chrono::duration<double,std::milli>(start-job.offered).count();
            ExperimentalAspect::Result result;
            result.reason="stale-job";
            if(ageMs<=100) {
                if(job.frame.source.range==AnalysisLumaRange::Unknown)result.reason="unknown-code-range";
                else {
                    const auto deadline=start+std::chrono::milliseconds(2);
                    result=ExperimentalAspect::Measure(job.frame.source.width,job.frame.source.height,
                        job.frame.source.range==AnalysisLumaRange::Full?0:64,
                        [&](int x,int y,int& v){AnalysisLumaSample s;if(!job.frame.source.Sample(x,y,s))return false;v=s.luma;return true;},
                        [&]{return Clock::now()<deadline;});
                }
            }
            const auto& r=result;const auto& id=job.identity;const auto& b=job.baseline;
            const double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            DebugLog::Log("[AR-SHADOW] generation=%llu source=%llu capture=%llu format=%llu domain=%llu viewport=%llu renderer=%llu raster=%dx%d range=%u kind=%u complete=%d bounds=%d,%d,%d,%d score=%.3f full=%.3f symmetry=%.3f ratio=%.3f reason=%s samples=%zu wall_ms=%.3f age_ms=%.3f dropped=%llu submit_peak_us=%llu baseline_current=%d baseline_kind=%u baseline_bounds=%d,%d,%d,%d edges=L(%d,%d,%.2f,%.1f,%.1f,%.2f,%.2f)T(%d,%d,%.2f,%.1f,%.1f,%.2f,%.2f)R(%d,%d,%.2f,%.1f,%.1f,%.2f,%.2f)B(%d,%d,%.2f,%.1f,%.1f,%.2f,%.2f)",
                id.transportGeneration,id.acceptedSequence,id.captureTimestamp,id.sourceFormatGeneration,id.analysisDomainKey,id.viewportGeneration,id.rendererGeneration,
                job.frame.source.width,job.frame.source.height,unsigned(job.frame.source.range),unsigned(r.kind),r.complete,r.left,r.top,r.right,r.bottom,
                r.score,r.fullScore,r.symmetry,r.ratioSupport,r.reason,r.samples,elapsed,ageMs,dropped.load(),submitPeakUs.load(),job.baselineCurrent,unsigned(job.classification),b.left,b.top,b.right,b.bottom,
                r.edges[0].depth,r.edges[0].uncertainty,r.edges[0].support,r.edges[0].contrast,r.edges[0].noise,r.edges[0].darkness,r.edges[0].score,
                r.edges[1].depth,r.edges[1].uncertainty,r.edges[1].support,r.edges[1].contrast,r.edges[1].noise,r.edges[1].darkness,r.edges[1].score,
                r.edges[2].depth,r.edges[2].uncertainty,r.edges[2].support,r.edges[2].contrast,r.edges[2].noise,r.edges[2].darkness,r.edges[2].score,
                r.edges[3].depth,r.edges[3].uncertainty,r.edges[3].support,r.edges[3].contrast,r.edges[3].noise,r.edges[3].darkness,r.edges[3].score);
            // Release potentially large last-owned source memory on the worker.
            job={};
            {std::lock_guard<std::mutex> lock(mutex);busy=false;}
        }
    }
public:
    ExperimentalAspectShadow() {
        char mode[16]{};
        if(GetEnvironmentVariableA("VP_AR_EXPERIMENT",mode,sizeof(mode))!=6 || std::strcmp(mode,"shadow")!=0)return;
        try {worker=std::thread([this]{Run();});enabled=true;}
        catch (...) {enabled=false;} // Optional diagnostics cannot prevent playback.
    }
    ~ExperimentalAspectShadow() {
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
        wake.notify_one();if(worker.joinable())worker.join();
    }
    void Offer(const PreparedAnalysisFrame& frame,ActivePictureFrameIdentity identity,
        const ActivePictureBounds& baseline,ActivePictureClassification classification,bool baselineCurrent) {
        if(!enabled)return;
        const auto start=Clock::now();
        if(start<nextOffer)return;
        nextOffer=start+std::chrono::milliseconds(250);
        if(!frame.IsValid())return;
        std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
        if(!lock.owns_lock()||busy){++dropped;return;}
        identity.analysisDomainKey=frame.domainKey;
        pending={frame,identity,baseline,classification,baselineCurrent,start};busy=true;
        lock.unlock();wake.notify_one();
        const auto us=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count());
        if(us>submitPeakUs.load())submitPeakUs.store(us);
    }
};
