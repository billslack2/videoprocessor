#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <vprenderer/SubtitleAspectRetention.h>

// A worker job may finish without searching text at all. Completion of that
// job is distinct from conclusive current caption or absence evidence.
inline bool SourceCadenceObservationComplete(const SubtitleBoxObservation& observed,
    const ActivePictureFrameIdentity& identity)
{
    return observed.analyzed && !observed.pendingRefresh && !observed.analysisRefresh &&
        !observed.text.held && !observed.text.workLimit &&
        SameAnalysisFrameIdentity(observed.identity,identity) &&
        ((observed.text.detected && observed.text.bounds.Valid()) ||
         ConfirmedSubtitleAbsentForPresentation(observed,observed.text,identity));
}

// Context of final display-referred pixels. Source sequence and PTS are kept
// separately: retirement of the next input cannot turn it into a new anchor.
struct SourceCadencePresentationContext {
    const void* gpu=nullptr;
    uint64_t generation=0,sourceFormat=0,analysisDomain=0,viewport=0,renderer=0;
    uint64_t processing=0,policy=0,continuity=0,reset=0,scene=0;
    uint64_t outputContract=0,outputEpoch=0,startupRevision=0;
    uint64_t frameDuration=0,timeScale=0;
    int subtitleMode=0,transfer=0,colorspace=0;
    bool configuredScreen=false,interlaced=false,temporal=false,deferredReset=false;
    bool Matches(const SourceCadencePresentationContext& b) const {
        return gpu && gpu==b.gpu && generation && generation==b.generation &&
            sourceFormat && sourceFormat==b.sourceFormat && analysisDomain && analysisDomain==b.analysisDomain &&
            viewport==b.viewport && renderer==b.renderer && processing==b.processing &&
            policy==b.policy && continuity==b.continuity && reset==b.reset && scene==b.scene &&
            outputContract && outputContract==b.outputContract && outputEpoch==b.outputEpoch &&
            startupRevision==b.startupRevision && frameDuration && frameDuration==b.frameDuration &&
            timeScale && timeScale==b.timeScale && subtitleMode==b.subtitleMode &&
            transfer==b.transfer && colorspace==b.colorspace && configuredScreen==b.configuredScreen &&
            !interlaced && !b.interlaced && !temporal && !b.temporal && !deferredReset && !b.deferredReset;
    }
};

class SourceCadencePresentation {
    SourceCadencePresentationContext context{};
    uint64_t sequence=0;
    double pts=0;
    bool candidate=false,committed=false,retired=false;
public:
    void Reset() { context={};sequence=0;pts=0;candidate=committed=retired=false; }
    // Capture is provisional. A failed render/copy/present cannot grant reuse.
    void Candidate(const SourceCadencePresentationContext& c,uint64_t source,double sourcePts,
        bool analysisComplete,bool captured) {
        Reset();context=c;sequence=source;pts=sourcePts;
        candidate=analysisComplete && captured && source && std::isfinite(sourcePts) && c.Matches(c);
    }
    // Single-shot completion of Candidate. Calling twice invalidates the grant.
    void Submitted(bool success) { committed=candidate && success;candidate=false; }
    bool CanRepeat(const SourceCadencePresentationContext& current,double inputHz,
        uint64_t nextSource,double nextPts,bool discontinuity=false) const {
        if(!committed || retired || discontinuity || !context.Matches(current) ||
            !std::isfinite(inputHz) || inputHz<=30.0 ||
            sequence==(std::numeric_limits<uint64_t>::max)() || nextSource!=sequence+1 ||
            !std::isfinite(nextPts) || nextPts<=pts)
            return false;
        const double retainedHz=double(context.timeScale)/context.frameDuration;
        return std::abs(inputHz-retainedHz)<(std::max)(0.001,retainedHz*0.00001) &&
            nextPts-pts<=1.5/inputHz;
    }
    void RepeatSubmitted(bool success) { if(success)retired=true;else Reset(); }
    uint64_t DisplayedSource() const { return committed?sequence:0; }
    double DisplayedPts() const { return committed?pts:0; }
};
