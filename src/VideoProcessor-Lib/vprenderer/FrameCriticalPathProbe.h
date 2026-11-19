#pragma once
#include <Windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <DebugLog.h>

// Opt-in, renderer-owned diagnostic. No pixel reads, queues, or scheduling changes.
// CPU is scheduled kernel+user time on THIS thread, not process CPU or GPU time.
class FrameCriticalPathProbe {
    struct Stamp { LONGLONG wall=0; ULONGLONG cpu=0; bool valid=false; };
    struct Distribution {
        std::array<double,512> values{};
        size_t count=0,stored=0; double sum=0,peak=0;
        void Add(double value) { ++count; sum+=value; peak=(std::max)(peak,value);
            if(stored<values.size())values[stored++]=value; }
        double Percentile(double p) { if(!stored)return 0;
            std::sort(values.begin(),values.begin()+stored);
            return values[static_cast<size_t>(std::ceil(p*stored))-1]; }
    };
    struct Pair { Distribution wall,cpu; };
    std::array<Pair,6> cost{};
    Distribution submitGap,callbackAge;
    LONGLONG previousSubmit=0; uint64_t previousSource=0;
    uint64_t repeats=0,skipped=0,backward=0;
    ULONGLONG startTick=0; LONGLONG frequency=0;
    uint64_t attempts=0,submitted=0,partial=0,cpuFailures=0,overBudget=0;
    uint64_t generation=0,firstSource=0,lastSource=0;
    bool enabled=false;
    static Stamp Read() { Stamp s; LARGE_INTEGER q{}; QueryPerformanceCounter(&q); s.wall=q.QuadPart;
        FILETIME created{},exited{},kernel{},user{};
        s.valid=GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user)!=FALSE;
        s.cpu=(ULONGLONG(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+
            (ULONGLONG(user.dwHighDateTime)<<32)+user.dwLowDateTime; return s; }
    void Add(size_t i,const Stamp& a,const Stamp& b) {
        cost[i].wall.Add(double(b.wall-a.wall)*1000.0/frequency);
        if(a.valid&&b.valid&&b.cpu>=a.cpu)cost[i].cpu.Add(double(b.cpu-a.cpu)/10000.0);
        else ++cpuFailures;
    }
    void Flush() {
        if(!attempts)return;
        const char* names[]={"preparation","render_wrapper","before_swap","swap","post_swap","total"};
        for(size_t i=0;i<cost.size();++i){auto& c=cost[i];
            DebugLog::Log("VP critical path: schema=1 scope=RenderLocked stage=%s generation=%llu first_source=%llu last_source=%llu attempts=%llu submitted=%llu partial=%llu samples=%zu percentile_samples=%zu overflow=%zu wall_mean_ms=%.4f wall_p95_ms=%.4f wall_p99_ms=%.4f wall_max_ms=%.4f cpu_samples=%zu cpu_mean_ms=%.4f cpu_p95_ms=%.4f cpu_p99_ms=%.4f cpu_max_ms=%.4f cpu_read_failures=%llu source_budget_exceeded=%llu coverage=renderer_only cpu_basis=thread_kernel_plus_user gpu_overlap=not_added",
                names[i],generation,firstSource,lastSource,attempts,submitted,partial,
                c.wall.count,c.wall.stored,c.wall.count-c.wall.stored,
                c.wall.count?c.wall.sum/c.wall.count:0,c.wall.Percentile(.95),c.wall.Percentile(.99),c.wall.peak,
                c.cpu.count,c.cpu.count?c.cpu.sum/c.cpu.count:0,c.cpu.Percentile(.95),c.cpu.Percentile(.99),c.cpu.peak,cpuFailures,overBudget);
        }
        DebugLog::Log("VP critical progression: schema=1 generation=%llu first_source=%llu last_source=%llu submissions=%llu repeats=%llu skipped_source_ids=%llu backward=%llu gap_samples=%zu gap_mean_ms=%.4f gap_p95_ms=%.4f gap_p99_ms=%.4f gap_max_ms=%.4f callback_age_samples=%zu callback_age_mean_ms=%.4f callback_age_p95_ms=%.4f callback_age_p99_ms=%.4f callback_age_max_ms=%.4f gap_overflow=%zu callback_age_overflow=%zu evidence=submit_boundary_not_scanout",
            generation,firstSource,lastSource,submitted,repeats,skipped,backward,
            submitGap.count,submitGap.count?submitGap.sum/submitGap.count:0,submitGap.Percentile(.95),submitGap.Percentile(.99),submitGap.peak,
            callbackAge.count,callbackAge.count?callbackAge.sum/callbackAge.count:0,callbackAge.Percentile(.95),callbackAge.Percentile(.99),callbackAge.peak,
            submitGap.count-submitGap.stored,callbackAge.count-callbackAge.stored);
        cost={}; attempts=submitted=partial=cpuFailures=overBudget=0;
        submitGap={};callbackAge={};repeats=skipped=backward=0;
    }
public:
    FrameCriticalPathProbe() { char value[8]{};
        enabled=GetEnvironmentVariableA("VP_CRITICAL_PATH",value,sizeof(value))==1&&value[0]=='1';
        LARGE_INTEGER q{}; QueryPerformanceFrequency(&q); frequency=q.QuadPart; }
    class Scope {
        FrameCriticalPathProbe& owner; std::array<Stamp,5> stamps{};
        size_t marked=0; bool success=false; double budget=0;
        uint64_t source=0; LONGLONG callback=0;
    public:
        Scope(FrameCriticalPathProbe& p,uint64_t gen,uint64_t sourceId,double hz,LONGLONG callbackQpc):owner(p),source(sourceId),callback(callbackQpc) {
            if(!owner.enabled)return;
            auto tick=GetTickCount64();
            // Logging belongs to the next scope's preparation, never omitted from total.
            stamps[0]=Read();
            if(owner.attempts&&(gen!=owner.generation||tick-owner.startTick>=5000))owner.Flush();
            if(gen!=owner.generation){owner.previousSubmit=0;owner.previousSource=0;}
            if(!owner.attempts){owner.startTick=tick;owner.generation=gen;owner.firstSource=source;}
            owner.lastSource=source; budget=hz>0?1000.0/hz:0;
        }
        void Mark() { if(owner.enabled&&marked<4)stamps[++marked]=Read(); }
        void Submitted(bool value) { success=value; }
        ~Scope() { if(!owner.enabled)return; const auto end=Read(); ++owner.attempts;
            owner.submitted+=success?1:0;
            if(success&&marked==4){const auto submit=stamps[3].wall;
                if(owner.previousSubmit&&submit>=owner.previousSubmit){
                    owner.submitGap.Add(double(submit-owner.previousSubmit)*1000.0/owner.frequency);
                    if(source==owner.previousSource)++owner.repeats;
                    else if(source<owner.previousSource)++owner.backward;
                    else owner.skipped+=source-owner.previousSource-1;
                }
                if(callback>0&&submit>=callback)owner.callbackAge.Add(double(submit-callback)*1000.0/owner.frequency);
                owner.previousSubmit=submit;owner.previousSource=source;
            }
            owner.Add(5,stamps[0],end);
            owner.overBudget+=budget>0&&double(end.wall-stamps[0].wall)*1000.0/owner.frequency>budget?1:0;
            if(marked==4){for(size_t i=0;i<4;++i)owner.Add(i,stamps[i],stamps[i+1]);owner.Add(4,stamps[4],end);}
            else ++owner.partial;
        }
    };
};
