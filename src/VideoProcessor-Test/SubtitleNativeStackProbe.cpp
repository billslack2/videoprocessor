// Test-only executable: never packaged or deployed. Run from the test output folder.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#include <SubtitleCurrentCue.h>
#include <SubtitleMeasurementWorker.h>
#include <SubtitleStackProbeIdentity.h>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
constexpr size_t StackReserve=1024u*1024u;
// Exact failed43fb6cba DLL frames decoded from the preserved crash dump:
// RenderLoop197080 + RenderLocked586728. This models caller pressure; the
// production renderer/runtime smoke remains a separate acceptance gate.
constexpr size_t CallerPressure=197080u+586728u;
struct State {
    std::vector<uint16_t> pixels;
    AnalysisLumaSource source;
    ActivePictureBounds base{0,64,640,296,640,360};
    SubtitleCueContinuationGuard guard;
    SubtitlePendingMeasurementGuard pending;
    SubtitleBoxPresentation tracker;
    std::array<SubtitleBoxObservation,2> observations;
    SubtitleBoxPreview acquired,current,raw,working;
    PreparedCurrentSubtitleCue prepared;
    CurrentSubtitleCueDecision decision;
    uint64_t cue=0;
    int mode=0;
};
ActivePictureFrameIdentity Identity(uint64_t sequence) {return {1,sequence,sequence,sequence*41708,2,3,4};}
void Require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void Prepare(State& s,const AnalysisLumaSource& source,size_t maxSamples) {
#ifdef VP_SUBTITLE_STACK_BASELINE_RETURN_API
    s.prepared=PrepareCurrentSubtitleCue(s.guard,s.working,source,s.base,true,250,41.708,maxSamples);
#else
    PrepareCurrentSubtitleCueInto(s.prepared,s.guard,s.working,source,s.base,true,250,41.708,maxSamples);
#endif
}
void Consume(State& s,const AnalysisLumaSource& source,bool verified) {
#ifdef VP_SUBTITLE_STACK_BASELINE_RETURN_API
    s.decision=ConsumePreparedCurrentSubtitleCue(s.guard,s.tracker,s.working,source,s.base,true,250,41.708,s.prepared,verified);
#else
    ConsumePreparedCurrentSubtitleCueInto(s.decision,s.guard,s.tracker,s.working,source,s.base,true,250,41.708,s.prepared,verified);
#endif
}
__declspec(noinline) void Initialize(State& s) {
    s.pixels.assign(640u*360u*2u,uint16_t(512<<6));
    for(int y=0;y<360;++y)for(int x=0;x<640;++x)s.pixels[y*640+x]=uint16_t((y>=64 && y<296?300:64)<<6);
    for(int row=0;row<2;++row)for(int glyph=0;glyph<12;++glyph) {
        const int left=240+glyph*13,top=298+row*28;
        for(int y=top;y<top+14;++y)for(int x=left;x<left+8;++x)
            s.pixels[y*640+x]=uint16_t(((x>=left+2 && x<left+6 && y>=top+2 && y<top+12)?64:700)<<6);
    }
    s.source.data=reinterpret_cast<const uint8_t*>(s.pixels.data());s.source.dataBytes=s.pixels.size()*2;
    s.source.width=640;s.source.height=360;s.source.rowBytes=s.source.chromaRowBytes=1280;
    s.source.format=AnalysisLumaFormat::P210;s.source.generation=1;
    for(size_t i=0;i<2;++i) {
        SubtitleBoxDetector detector;
        s.observations[i]=SubtitleBoxLookahead::Measure(detector,s.source,ExtractActivePictureEvidence(s.source),Identity(i+1),false,{},11,21);
    }
    s.acquired=SubtitleBoxLookahead::Resolve(s.observations.data(),2,11,21);
    s.current=SubtitleBoxLookahead::Resolve(&s.observations[1],1,11,21);
    s.working=s.acquired;Prepare(s,s.source,SubtitleCurrentCueInspectionSampleLimit);Consume(s,s.source,false);
    Require(s.decision.accepted.detected,"acquisition failed");
    s.working=s.current;Prepare(s,s.source,SubtitleCurrentCueInspectionSampleLimit);Consume(s,s.source,false);
    Require(s.decision.accepted.detected && s.guard.HasReference(),"current reference failed");s.cue=s.decision.accepted.cue;
    Require(s.pending.RememberAccepted(s.current,s.decision.accepted,s.source,41.708,250),"pending seed failed");
    s.raw=s.current;s.raw.available=false;s.raw.current.analyzed=false;s.raw.current.identity=Identity(3);
    if(s.mode==0) {s.guard.Reset();s.pending.Reset();}
    if(s.mode==3)for(int y=298;y<312;++y)for(int x=240;x<248;++x)s.pixels[y*640+x]=uint16_t(64<<6);
}
__declspec(noinline) DWORD Execute(State& s) {
    ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);char local;
    Require(high-low==StackReserve,"test thread reserve must equal shipped1MiB reserve");
    std::cout<<"mode="<<s.mode<<" stack_reserve="<<high-low<<" caller_pressure="<<CallerPressure
        <<" remaining="<<reinterpret_cast<ULONG_PTR>(&local)-low<<std::endl;
    for(int attempt=0;attempt<(s.mode==1?3:1);++attempt) {
        AnalysisLumaReadBudget budget{SubtitleCurrentCueFrameSampleLimit};auto source=s.source;source.readBudget=&budget;
        s.working=s.raw;
        const bool prioritize=s.guard.CanInspectCurrentObservation(s.working.current);
        Prepare(s,source,s.mode==1?size_t(1):SubtitleCurrentCueInspectionSampleLimit);
        bool verified=false;
        if(!prioritize || s.prepared.proof.RetainsCandidateFor(s.working.current.identity,11,21))
            verified=s.pending.Resolve(s.working,source,41.708,250);
        Consume(s,source,verified);
        const bool expected=s.mode==1 || s.mode==2;
        Require(s.decision.accepted.detected==expected,"unexpected cue admission");
        if(expected)Require(s.decision.accepted.cue==s.cue,"cue identity changed");
        if(s.mode==1)Require(!s.decision.proof.valid && verified,"test must exercise incomplete pending/raw fallback");
        if(s.mode==2)Require(s.decision.proof.completeCurrentCue,"test must exercise COMPLETE current proof");
        std::cout<<"attempt="<<attempt<<" detected="<<s.decision.accepted.detected<<" proof="<<s.decision.proof.reason
            <<" tracker="<<s.tracker.DecisionReason()<<" reads="<<budget.used<<std::endl;
    }
    return 0;
}
__declspec(noinline) DWORD WINAPI Run(void* context) {
    volatile char* pressure=static_cast<volatile char*>(_alloca(CallerPressure));
    for(size_t i=0;i<CallerPressure;i+=4096)pressure[i]=1;pressure[CallerPressure-1]=1;
    try {
        const auto result=Execute(*static_cast<State*>(context));
        return pressure[CallerPressure-1]==1?result:7;
    } catch(const std::exception& error) {std::cout<<"FAIL "<<error.what()<<std::endl;return 12;}
}
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    std::ofstream log;
    struct Redirect {std::streambuf* previous=std::cout.rdbuf();~Redirect(){std::cout.rdbuf(previous);}} redirect;
    if(argc>1) {log.open(argv[1],std::ios::out|std::ios::trunc);if(!log)return 8;std::cout.rdbuf(log.rdbuf());}
    std::cout<<"source="<<VP_SUBTITLE_STACK_SOURCE_COMMIT<<" build="<<__DATE__<<" "<<__TIME__<<std::endl;
    const int first=argc>2?_wtoi(argv[2]):0,last=argc>2?first:3;
    try {
        for(int mode=first;mode<=last;++mode) {
            auto state=std::make_unique<State>();state->mode=mode;Initialize(*state);
            HANDLE thread=CreateThread(nullptr,StackReserve,&Run,state.get(),STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
            Require(thread!=nullptr,"CreateThread failed");const auto wait=WaitForSingleObject(thread,20000);
            if(wait!=WAIT_OBJECT_0) {
                std::cout<<"FAIL native thread timeout/wait"<<std::endl;
                // Exit the isolated child before its still-running worker can
                // observe destruction of heap fixture state.
                TerminateProcess(GetCurrentProcess(),9);return 9;
            }
            DWORD exitCode=10;GetExitCodeThread(thread,&exitCode);CloseHandle(thread);if(exitCode)return int(exitCode);
        }
        if(argc>2)std::cout<<"PASS mode="<<first<<std::endl;
        else std::cout<<"PASS unavailable-reset incomplete-fallback complete-proof contradiction"<<std::endl;
    } catch(const std::exception& error) {std::cout<<"FAIL "<<error.what()<<std::endl;return 11;}
    return 0;
}
