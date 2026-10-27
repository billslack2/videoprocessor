#pragma once
#include <RendererLiveness.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

// Accumulation belongs to the render thread. Only a small completed POD snapshot
// crosses to the UI; no renderer lock, GPU query, allocation or wait is needed.
// RecordDraw receives total active draw CPU/wait time; RecordPreparation is its
// per-call subtitle preparation subset, with an independent denominator.
class ProcessingPerformanceWindow {
public:
    struct Context {
        uint64_t generation=0, processingEpoch=0, mode=0;
        bool operator==(const Context& other) const {
            return generation==other.generation && processingEpoch==other.processingEpoch && mode==other.mode;
        }
    };
    static constexpr uint64_t WindowMilliseconds=2000, MaximumAgeMilliseconds=4000;
    void RecordPreparation(Context context,uint64_t now,double milliseconds) {
        Begin(context,now);preparation.Add(milliseconds);PublishIfDue(now);
    }
    void RecordDraw(Context context,uint64_t now,bool reused,double milliseconds) {
        Begin(context,now);(reused?repeat:source).Add(milliseconds);PublishIfDue(now);
    }
    void PublishIfDue(uint64_t now) {
        if(!initialized || now<start || now-start<WindowMilliseconds)return;
        RendererProcessingPerformance next;
        next.available=preparation.count+source.count+repeat.count>0;
        next.windowMilliseconds=now-start;
        next.preparationCalls=preparation.count;next.sourceDraws=source.count;next.reusedDraws=repeat.count;
        next.preparationCpu=preparation.Snapshot();next.sourceDrawCpu=source.Snapshot();next.reusedDrawCpu=repeat.Snapshot();
        { std::lock_guard<std::mutex> lock(publicationMutex);published=next;publishedTick=now; }
        preparation={};source={};repeat={};start=now;
    }
    RendererProcessingPerformance Snapshot(uint64_t now) const {
        std::unique_lock<std::mutex> lock(publicationMutex,std::try_to_lock);
        if(!lock.owns_lock() || !published.available || now<publishedTick ||
            now-publishedTick>MaximumAgeMilliseconds)return {};
        return published;
    }
private:
    struct Cost {
        uint64_t count=0;double total=0,last=0,peak=0;
        void Add(double ms) {
            if(!std::isfinite(ms)||ms<0)return;
            ++count;total+=ms;last=ms;peak=(std::max)(peak,ms);
        }
        RendererRenderLoadStat Snapshot() const {
            RendererRenderLoadStat result;result.last=last;result.average=count?total/count:0;result.peak=peak;return result;
        }
    };
    void Begin(Context next,uint64_t now) {
        if(!initialized || !(context==next) || now<lastTick) {
            initialized=true;context=next;start=now;preparation={};source={};repeat={};
            std::lock_guard<std::mutex> lock(publicationMutex);published={};publishedTick=0;
        }
        lastTick=now;
    }
    bool initialized=false;Context context;uint64_t start=0,lastTick=0;
    Cost preparation,source,repeat;
    mutable std::mutex publicationMutex;
    RendererProcessingPerformance published;uint64_t publishedTick=0;
};
