#pragma once
#include <algorithm>
#include <cstdint>

// Render-thread-owned accounting only. No allocation, clock reads, or I/O.
// Source cadence and display scanout are different budgets, not interchangeable.
struct AnalysisPerformanceWindow {
    struct Cost {
        uint64_t count=0;double total=0,peak=0;
        void Add(double ms){++count;total+=ms;peak=(std::max)(peak,ms);}
        double Average() const {return count?total/count:0;}
    };
    Cost preview,active,preparation,render,swap;
    uint64_t attempts=0,completed=0,skipped=0,phaseKnown=0,deadlineOverruns=0;
    uint64_t sourceOverruns=0,displayOverruns=0;
    double deadlineOverrunPeakMs=0;
    void Preview(bool attempted,bool finished,bool phase,double elapsedMs,double lateMs){
        if(!attempted){++skipped;return;}
        ++attempts;completed+=finished?1:0;phaseKnown+=phase?1:0;preview.Add(elapsedMs);
        if(lateMs>0){++deadlineOverruns;deadlineOverrunPeakMs=(std::max)(deadlineOverrunPeakMs,lateMs);}
    }
    void Submission(double activeMs,double renderMs,double swapMs,double sourceHz,double displayHz){
        active.Add(activeMs);render.Add(renderMs);swap.Add(swapMs);
        sourceOverruns+=sourceHz>0 && activeMs>1000.0/sourceHz ? 1:0;
        displayOverruns+=displayHz>0 && activeMs>1000.0/displayHz ? 1:0;
    }
};
