// Test-only executable: never packaged or deployed. Run from the test output folder.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#include <SubtitleCurrentCue.h>
#include <SubtitleMeasurementWorker.h>
#include <SubtitleBackingLip.h>
#include <SubtitleCutPaste.h>
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
    SubtitleCutPastePresentation compositor;
    SubtitleBackingLipContinuity backing;
    SubtitleCutPasteGeometry geometry,applied;
    CurrentSubtitleBackingLipProof cap;
    uint64_t cue=0;
    int mode=0;
};
ActivePictureFrameIdentity Identity(uint64_t sequence) {return {1,sequence,sequence,sequence*41708,2,3,4};}
void Require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void Prepare(State& s,const AnalysisLumaSource& source,size_t maxSamples) {
#ifdef VP_SUBTITLE_STACK_BASELINE_RETURN_API
    s.prepared=PrepareCurrentSubtitleCue(s.guard,s.working,source,s.base,true,250,41.708,maxSamples);
#else
    const bool backlog=s.mode==5 && s.working.continuityGeneration==22;const auto witness=Identity(2);
    PrepareCurrentSubtitleCueInto(s.prepared,s.guard,s.working,source,s.base,true,250,41.708,maxSamples,
        backlog?&witness:nullptr,backlog);
#endif
}
void Consume(State& s,const AnalysisLumaSource& source,bool verified) {
#ifdef VP_SUBTITLE_STACK_BASELINE_RETURN_API
    s.decision=ConsumePreparedCurrentSubtitleCue(s.guard,s.tracker,s.working,source,s.base,true,250,41.708,s.prepared,verified);
#else
    const bool backlog=s.mode==5 && s.working.continuityGeneration==22;const auto witness=Identity(2);
    ConsumePreparedCurrentSubtitleCueInto(s.decision,s.guard,s.tracker,s.working,source,s.base,true,250,41.708,
        s.prepared,verified,backlog?&witness:nullptr,backlog);
#endif
}
__declspec(noinline) void Initialize(State& s) {
    if(s.mode==4) {
        s.base={0,384,3840,1776,3840,2160};
        s.pixels.assign(3840u*2160u*2u,uint16_t(512<<6));
        for(int y=0;y<2160;++y)for(int x=0;x<3840;++x)s.pixels[y*3840+x]=uint16_t((y>=384 && y<1776?300:64)<<6);
        const int left=(3840-(25*78+48))/2;
        for(int y=1746;y<1776;++y) {
            const int inset=(std::max)(0,12-(y-1746));
            for(int x=left-96+inset;x<left+25*78+48+96-inset;++x)s.pixels[y*3840+x]=uint16_t(64<<6);
        }
        for(int row=0;row<2;++row)for(int glyph=0;glyph<26;++glyph) {
            const int x0=left+glyph*78,y0=1778+row*108;
            for(int y=y0;y<y0+66;++y)for(int x=x0;x<x0+48;++x)
                s.pixels[y*3840+x]=uint16_t(((x>=x0+12 && x<x0+36 && y>=y0+12 && y<y0+54)?64:700)<<6);
        }
    } else {
    s.pixels.assign(640u*360u*2u,uint16_t(512<<6));
    for(int y=0;y<360;++y)for(int x=0;x<640;++x)s.pixels[y*640+x]=uint16_t((y>=64 && y<296?300:64)<<6);
    for(int row=0;row<2;++row)for(int glyph=0;glyph<12;++glyph) {
        const int left=240+glyph*13,top=298+row*28;
        for(int y=top;y<top+14;++y)for(int x=left;x<left+8;++x)
            s.pixels[y*640+x]=uint16_t(((x>=left+2 && x<left+6 && y>=top+2 && y<top+12)?64:700)<<6);
    }
    }
    s.source.data=reinterpret_cast<const uint8_t*>(s.pixels.data());s.source.dataBytes=s.pixels.size()*2;
    s.source.width=s.mode==4?3840:640;s.source.height=s.mode==4?2160:360;
    s.source.rowBytes=s.source.chromaRowBytes=s.source.width*2;
    s.source.format=AnalysisLumaFormat::P210;s.source.generation=1;
    for(size_t i=0;i<2;++i) {
        SubtitleBoxDetector detector;if(s.mode==4)detector.SetNearBarDistance(200);
        s.observations[i]=SubtitleBoxLookahead::Measure(detector,s.source,ExtractActivePictureEvidence(s.source),Identity(i+1),false,{},11,21);
    }
    s.acquired=SubtitleBoxLookahead::Resolve(s.observations.data(),2,11,21);
    s.current=SubtitleBoxLookahead::Resolve(&s.observations[1],1,11,21);
    s.working=s.acquired;Prepare(s,s.source,SubtitleCurrentCueInspectionSampleLimit);Consume(s,s.source,false);
    Require(s.decision.accepted.detected,"acquisition failed");
    s.working=s.current;Prepare(s,s.source,SubtitleCurrentCueInspectionSampleLimit);Consume(s,s.source,false);
    Require(s.decision.accepted.detected && s.guard.HasReference(),"current reference failed");s.cue=s.decision.accepted.cue;
    Require(s.pending.RememberAccepted(s.current,s.decision.accepted,s.source,41.708,250),"pending seed failed");
    if(s.mode==4) {
        SubtitleBarEvidence neutral;neutral.bottom=2160;
        const auto masked=RevalidateSubtitleBarEvidence(s.source,neutral,0,1776,s.current.current.ink.get(),&s.current.current.text.bounds);
        Require(!masked.bottomRevalidatedAtReference,"large cue must require envelope-only fallback");
        s.geometry=s.compositor.Consume(s.decision.accepted,s.current);
        s.cap=s.backing.Consume(s.source,s.current,s.decision.accepted,Identity(2),11,21,SubtitleBackingLipSampleLimit,&s.geometry);
        Require(s.cap.valid,"large cue initial cap failed");
    }
    s.raw=s.current;s.raw.available=false;s.raw.current.analyzed=false;s.raw.current.identity=Identity(3);
    if(s.mode==5) {s.raw.current.identity=Identity(7);s.raw.continuityGeneration=s.raw.current.continuityGeneration=22;}
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
        const auto witness=Identity(2);
        const bool prioritize=s.guard.CanInspectCurrentObservation(s.working.current) ||
            s.guard.CanInspectAfterBacklogRetirement(s.working.current,source,&witness,s.mode==5);
        Prepare(s,source,s.mode==1?size_t(1):SubtitleCurrentCueInspectionSampleLimit);
        bool verified=false;
        if(!prioritize || s.prepared.AllowsPendingRecovery(s.working))
            verified=s.pending.Resolve(s.working,source,41.708,250);
        Consume(s,source,verified);
        const bool expected=s.mode==1 || s.mode==2 || s.mode==4 || s.mode==5;
        Require(s.decision.accepted.detected==expected,"unexpected cue admission");
        if(expected)Require(s.decision.accepted.cue==s.cue,"cue identity changed");
        if(s.mode==1)Require(!s.decision.proof.valid && verified,"test must exercise incomplete pending/raw fallback");
        if(s.mode==2)Require(s.decision.proof.completeCurrentCue,"test must exercise COMPLETE current proof");
        if(s.mode==5) {
            Require(s.decision.proof.completeCurrentCue && s.guard.CanInspectCurrentObservation(s.working.current),
                "queue-only trial must commit only a complete selected-source proof");
            Require(!verified && !budget.exhausted,"queue-only complete proof must skip pending and stay bounded");
        }
        if(s.mode==4) {
            Require(s.decision.proof.completeCurrentCue,"large cue must complete exact-plane fallback");
            Require(s.decision.proof.verifiedSearch.top==1268 && s.decision.proof.verifiedSearch.bottom==2160,
                "large cue must preserve configured companion coverage");
            s.geometry=s.compositor.Consume(s.decision.accepted,s.working,{},0,nullptr,&s.decision.proof);
            s.cap=s.backing.Consume(source,s.working,s.decision.accepted,Identity(3),11,21,SubtitleBackingLipSampleLimit,&s.geometry,&s.decision.proof);
            const char* reason="not-called";
            s.applied=ApplyCurrentSubtitleBackingLip(s.geometry,s.cap,Identity(3),11,21,&reason);
            Require(s.cap.valid && std::strcmp(reason,"lip-current-cleanup-applied")==0,"large cue current cap was not applied");
            Require(!budget.exhausted && budget.used<SubtitleCurrentCueFrameSampleLimit,"large cue exceeded shared budget");
        }
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
    const int first=argc>2?_wtoi(argv[2]):0,last=argc>2?first:5;
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
        else std::cout<<"PASS unavailable-reset incomplete-fallback complete-proof contradiction envelope-plane-fallback queue-witness-complete"<<std::endl;
    } catch(const std::exception& error) {std::cout<<"FAIL "<<error.what()<<std::endl;return 11;}
    return 0;
}
