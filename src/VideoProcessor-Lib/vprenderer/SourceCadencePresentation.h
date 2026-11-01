#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <vprenderer/SubtitleAspectRetention.h>

// Dedicated source-cadence rate; cumulative transport timing remains separate.
inline double SourceCadenceInputRate(double measuredRate,uint64_t frameDuration,uint64_t timeScale)
{
    // Capture-rate estimation starts at zero and publishes only every five
    // seconds. It must not govern the source's already-declared frame period.
    (void)measuredRate;
    return frameDuration && timeScale ? double(timeScale)/double(frameDuration) : 0.0;
}

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

// A current held pose may certify final pixels after actual erasure checks.
// This permission is deliberately separate from reusable source analysis: it
// cannot seal a SourcePreparationReceipt, acquire a cue, or renew its lease.
inline bool SourceCadenceHeldFinalPixelsComplete(const SubtitleBoxObservation& observation,
    const SubtitleBoxResult& accepted,const ActivePictureFrameIdentity& identity,
    const ActivePictureBounds& physicalBase,const SubtitleCurrentPreparedCueCertificate* certificate,
    const QualifiedSubtitlePresentationEvidence& prepared,SubtitlePreviewMode mode,
    bool sourcePrepared,bool compositionSucceeded,bool workIncomplete)
{
    return sourcePrepared && compositionSucceeded && !workIncomplete &&
        SubtitlePreviewOverridesClassicHandling(mode) && accepted.held &&
        SourceCadenceObservationComplete(observation,identity) && certificate &&
        certificate->Matches(observation,accepted,physicalBase) &&
        prepared.qualified && prepared.preparedErasureOnly && !prepared.renewsHold &&
        SameAnalysisFrameIdentity(prepared.identity,identity) &&
        SameSubtitlePresentationBounds(prepared.physicalBase,physicalBase);
}

// Context of final display-referred pixels. Source sequence and PTS are kept
// separately: retirement of the next input cannot turn it into a new anchor.
enum class SourceCadencePreparationState { None,Missing,Pending,Ready,Failed };
struct SourceCadencePresentationContext {
    const void* gpu=nullptr;
    uint64_t generation=0,sourceFormat=0,analysisDomain=0,viewport=0,renderer=0;
    uint64_t processing=0,policy=0,continuity=0,reset=0,scene=0;
    uint64_t outputContract=0,outputEpoch=0,startupRevision=0;
    uint64_t frameDuration=0,timeScale=0;
    int subtitleMode=0,transfer=0,colorspace=0;
    uint64_t preparationKey=0;int requestedSubtitleMode=0;
    SourceCadencePreparationState preparationState=SourceCadencePreparationState::None;
    bool preparedCaptionPresented=false;
    bool configuredScreen=false,interlaced=false,temporal=false,deferredReset=false;
    bool Matches(const SourceCadencePresentationContext& b) const {
        return gpu && gpu==b.gpu && generation && generation==b.generation &&
            sourceFormat && sourceFormat==b.sourceFormat && analysisDomain && analysisDomain==b.analysisDomain &&
            viewport==b.viewport && renderer==b.renderer && processing==b.processing &&
            policy==b.policy && continuity==b.continuity && reset==b.reset && scene==b.scene &&
            outputContract && outputContract==b.outputContract && outputEpoch==b.outputEpoch &&
            startupRevision==b.startupRevision && frameDuration && frameDuration==b.frameDuration &&
            timeScale && timeScale==b.timeScale && subtitleMode==b.subtitleMode &&
            preparationKey==b.preparationKey && requestedSubtitleMode==b.requestedSubtitleMode &&
            transfer==b.transfer && colorspace==b.colorspace && configuredScreen==b.configuredScreen &&
            !interlaced && !b.interlaced && !temporal && !b.temporal && !deferredReset && !b.deferredReset;
    }
};

class SourceCadencePresentation {
    SourceCadencePresentationContext context{};
    uint64_t sequence=0;
    double pts=0;
    bool candidate=false,committed=false,retired=false,warmup=false;
public:
    void Reset() { context={};sequence=0;pts=0;candidate=committed=retired=warmup=false; }
    // Capture is provisional. A failed render/copy/present cannot grant reuse.
    void Candidate(const SourceCadencePresentationContext& c,uint64_t source,double sourcePts,
        bool analysisComplete,bool captured) {
        Reset();context=c;sequence=source;pts=sourcePts;
        candidate=analysisComplete && captured && source && std::isfinite(sourcePts) && c.Matches(c);
    }
    static bool WarmupContextEligible(const SourceCadencePresentationContext& c) {
        return c.preparationKey && (c.requestedSubtitleMode==2 || c.requestedSubtitleMode==3) &&
            c.subtitleMode==0 && !c.preparedCaptionPresented &&
            (c.preparationState==SourceCadencePreparationState::Missing ||
             c.preparationState==SourceCadencePreparationState::Pending);
    }
    // A distinct final-image grant; it never makes source/caption analysis complete.
    void WarmupCandidate(const SourceCadencePresentationContext& c,uint64_t source,double sourcePts,
        bool nativePresentationQualified,bool captured) {
        Reset();context=c;sequence=source;pts=sourcePts;warmup=true;
        candidate=WarmupContextEligible(c) && nativePresentationQualified && captured && source &&
            std::isfinite(sourcePts) && c.Matches(c);
    }
    // Single-shot completion of Candidate. Calling twice invalidates the grant.
    void Submitted(bool success) { committed=candidate && success;candidate=false; }
    bool CanRepeat(const SourceCadencePresentationContext& current,double inputHz,
        uint64_t nextSource,double nextPts,bool discontinuity=false) const {
        bool matches=context.Matches(current);
        if(warmup) {
            auto selectedContext=current;selectedContext.subtitleMode=context.subtitleMode;
            // Still-building inputs render and cache their permitted fresh raw
            // output. Reserve this one-use bridge for the first Ready input;
            // an earlier Pending UNKNOWN must not spend it before that boundary.
            const bool firstReady=current.preparationState==SourceCadencePreparationState::Ready &&
                current.subtitleMode==current.requestedSubtitleMode;
            matches=WarmupContextEligible(context) && !current.preparedCaptionPresented &&
                firstReady && context.Matches(selectedContext);
        }
        if(!committed || retired || discontinuity || !matches ||
            !std::isfinite(inputHz) || inputHz<=30.0 ||
            sequence==(std::numeric_limits<uint64_t>::max)() || nextSource!=sequence+1 ||
            !std::isfinite(nextPts) || nextPts<=pts)
            return false;
        const double retainedHz=double(context.timeScale)/context.frameDuration;
        return std::abs(inputHz-retainedHz)<(std::max)(0.001,retainedHz*0.00001) &&
            nextPts-pts<=1.5/inputHz;
    }
    void RepeatSubmitted(bool success) { if(success)retired=true;else Reset(); }
    bool DisplayedIsWarmup() const { return committed && warmup; }
    uint64_t DisplayedSource() const { return committed?sequence:0; }
    double DisplayedPts() const { return committed?pts:0; }
};
