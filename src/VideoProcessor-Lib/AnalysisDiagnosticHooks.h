#pragma once
#include <AnalysisDiagnosticTrace.h>
#include <ActivePictureDecisionTimeline.h>
#include <cstring>
#include <memory>

// Bounded copies only: diagnostics must never scan pixels or mutate decisions.
template<size_t N> inline void AnalysisTraceText(char (&out)[N], const char* text) noexcept {
    if (!text) return;
    size_t n=0; while(n+1<N && text[n]) { out[n]=text[n]; ++n; } out[n]=0;
}
template<class Bounds> inline void AnalysisTraceRect(int32_t (&out)[4],const Bounds& b) noexcept {
    out[0]=b.left;out[1]=b.top;out[2]=b.right;out[3]=b.bottom;
}
inline AnalysisDiagnosticRecordV1 AnalysisTraceRecord(const ActivePictureFrameIdentity& id,
    const void* instance,const char* stage,const char* reason="") noexcept {
    AnalysisDiagnosticRecordV1 r;
    r.structSize=sizeof(r);r.kind=AnalysisDiagnosticRecordKindV1::Frame;
    r.frameIdentity=id.acceptedSequence;
    r.rendererInstanceKey=reinterpret_cast<uintptr_t>(instance);
    r.acceptedSequence=id.acceptedSequence;r.transportGeneration=id.transportGeneration;
    r.sourceFrameNumber=id.sourceFrameNumber;r.captureTimestamp=id.captureTimestamp;
    r.viewportGeneration=id.viewportGeneration;r.rendererGeneration=id.rendererGeneration;
    r.analysisDomainKey=id.analysisDomainKey;
    r.sourceFormatGeneration=id.sourceFormatGeneration;
    AnalysisTraceText(r.stage,stage);AnalysisTraceText(r.reason,reason);return r;
}
inline void* __cdecl RetainAnalysisTraceOwner(const void* context) noexcept {
    try { return new std::shared_ptr<const void>(*static_cast<const std::shared_ptr<const void>*>(context)); }
    catch(...) { return nullptr; }
}
inline void __cdecl ReleaseAnalysisTraceOwner(void* token) noexcept {
    delete static_cast<std::shared_ptr<const void>*>(token);
}
