#pragma once

#include <ActivePictureDecisionTimeline.h>
#include <AnalysisLumaSource.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <cstdint>

// One immutable source can produce several output draws. This receipt seals
// successful source work; it never grants a new picture/subtitle observation.
enum class SourceRepeatKind { NewSource, InsertedOutput, CadenceRepeat };

struct SourcePreparationContext {
    ActivePictureFrameIdentity identity{};
    AnalysisLumaSource source{}, queuedSource{};
    const void* gpu=nullptr;
    uint64_t processingEpoch=0,policyGeneration=0,continuityGeneration=0;
    uint64_t resetEpoch=0,viewportEpoch=0,sceneGeneration=0,outputContract=0;
    int subtitleMode=0;
    bool configuredScreen=false,statisticsAnalysis=false,discontinuity=false;

    static bool SameSource(const AnalysisLumaSource& a,const AnalysisLumaSource& b) {
        return a.IsValid() && b.IsValid() && a.backingOwner && b.backingOwner &&
            a.data==b.data && a.dataBytes==b.dataBytes &&
            a.width==b.width && a.height==b.height && a.rowBytes==b.rowBytes &&
            a.chromaRowBytes==b.chromaRowBytes && a.format==b.format &&
            a.encoding==b.encoding && a.colorspace==b.colorspace &&
            a.generation==b.generation && a.rgbBitAlignment==b.rgbBitAlignment &&
            a.range==b.range && a.backingOwner.get()==b.backingOwner.get() &&
            !a.backingOwner.owner_before(b.backingOwner) &&
            !b.backingOwner.owner_before(a.backingOwner);
    }
    bool Matches(const SourcePreparationContext& b) const {
        // The event belongs to this immutable captured source. NewSource always
        // rejects reuse; after its reset and successful preparation, repeated
        // draws of the exact same event may reuse the sealed result.
        return discontinuity==b.discontinuity && identity.transportGeneration && identity.acceptedSequence &&
            identity.analysisDomainKey && gpu && gpu==b.gpu &&
            SameAnalysisFrameIdentity(identity,b.identity) &&
            SameSource(source,b.source) && SameSource(queuedSource,b.queuedSource) &&
            processingEpoch==b.processingEpoch && policyGeneration==b.policyGeneration &&
            continuityGeneration==b.continuityGeneration && resetEpoch==b.resetEpoch &&
            viewportEpoch==b.viewportEpoch && sceneGeneration==b.sceneGeneration &&
            outputContract==b.outputContract && subtitleMode==b.subtitleMode &&
            configuredScreen==b.configuredScreen && statisticsAnalysis==b.statisticsAnalysis;
    }
};

// A completed pixel observation can still depend on an earlier source's
// bounded presentation hold. Keep that source retryable until normal policy
// has released the hold; this does not extend or renew its original timer.
inline bool SourceAnalysisHasTimedDependency(
    const AlphaSourceCrop::VerticalBarPresentationState& vertical,
    uint64_t sourceSequence,bool sceneHold,bool ambiguityHold) {
    return sceneHold || ambiguityHold ||
        (vertical.action!=AlphaSourceCrop::VerticalBarPresentationAction::NONE &&
         vertical.sourceSequence!=sourceSequence);
}
inline bool SourceBarInspectionNeedsExpiryRefresh(
    const AlphaSourceCrop::VerticalBarPresentationState& vertical,
    uint64_t currentTick,uint64_t holdMs,uint64_t sourceSequence) {
    return vertical.action!=AlphaSourceCrop::VerticalBarPresentationAction::NONE &&
        vertical.sourceSequence!=sourceSequence &&
        !AlphaSourceCrop::IsVerticalBarPresentationActive(vertical,currentTick,holdMs,sourceSequence);
}

struct SourceBarInspectionSchedule {
    bool scheduled=false,evaluated=false;
};
struct SourceBarInspectionCursor {
    uint64_t generation=0,sequence=0;
    bool scheduled=false;
};
inline SourceBarInspectionSchedule ScheduleSourceBarInspection(
    uint64_t& sourceOrdinal,SourceBarInspectionCursor& cursor,
    uint64_t generation,uint64_t sequence,bool authorityCurrent,bool forceAnalysis) {
    SourceBarInspectionSchedule result;
    if(!authorityCurrent || !generation || !sequence)return result;
    if(cursor.generation!=generation || cursor.sequence!=sequence) {
        cursor.generation=generation;cursor.sequence=sequence;
        cursor.scheduled=++sourceOrdinal%3==0;
    }
    result.scheduled=cursor.scheduled;
    result.evaluated=forceAnalysis || result.scheduled;
    return result;
}

struct SourcePreparationReuse { bool upload=false,analysis=false,subtitle=false; };

// A complete progressive packet and its frozen source mapping already own this
// exact source's subtitle result. An AR-only cadence retry must not overwrite
// that fact with a duplicate preparation; other incomplete pairs still retry.
inline bool ReuseConclusiveSubtitlePacketForSourceRetry(const SourcePreparationReuse& reuse,
    SourceRepeatKind repeat,bool cacheMatches,bool subtitleConclusive,bool completePacket,
    bool ownedCompletePacket,bool sourceSnapshotAvailable) {
    return repeat==SourceRepeatKind::CadenceRepeat && reuse.upload && cacheMatches &&
        subtitleConclusive && completePacket && ownedCompletePacket && sourceSnapshotAvailable;
}

class SourcePreparationReceipt {
    SourcePreparationContext context;
    bool completed=false,analysisConclusive=false,subtitleConclusive=false;
public:
    bool ReuseUpload(const SourcePreparationContext& current,SourceRepeatKind repeat) const {
        return repeat!=SourceRepeatKind::NewSource && completed && context.Matches(current);
    }
    // A late subtitle worker can also change the picture boundary. Retry both
    // dependent stages on genuine cadence repeats until both are conclusive.
    bool ReuseAnalysis(const SourcePreparationContext& current,SourceRepeatKind repeat) const {
        return ReuseUpload(current,repeat) &&
            (repeat==SourceRepeatKind::InsertedOutput || (analysisConclusive && subtitleConclusive));
    }
    bool ReuseSubtitle(const SourcePreparationContext& current,SourceRepeatKind repeat) const {
        return ReuseUpload(current,repeat) &&
            (repeat==SourceRepeatKind::InsertedOutput || (analysisConclusive && subtitleConclusive));
    }
    // Invalidate before any fallible work. The caller may consume this returned
    // eligibility during the invocation, but only Commit publishes it again.
    SourcePreparationReuse Begin(const SourcePreparationContext& current,SourceRepeatKind repeat) {
        SourcePreparationReuse reuse{ReuseUpload(current,repeat),ReuseAnalysis(current,repeat),
            ReuseSubtitle(current,repeat)};
        Reset();
        return reuse;
    }
    // Call only after upload and source preparation/capture succeeded. UNKNOWN
    // attempts can serve inserted draws, but remain retryable when the original
    // source is presented again and an exact worker result may have arrived.
    void Commit(const SourcePreparationContext& current,bool analysisComplete,bool subtitleComplete) {
        context=current;
        analysisConclusive=analysisComplete;subtitleConclusive=subtitleComplete;
        completed=context.Matches(context);
    }
    void Reset() { completed=analysisConclusive=subtitleConclusive=false;context={}; }
};
