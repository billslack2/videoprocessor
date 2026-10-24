#pragma once
#include <SubtitleBoxDetector.h>
#include <ActivePictureDecisionTimeline.h>

// Hard synchronous read limits, including current native glyph and side scans.
// The frame-wide allowance also covers pending validation and the finite cap.
inline constexpr size_t SubtitleCurrentCueInspectionSampleLimit=393216;
inline constexpr size_t SubtitleCurrentCueFrameSampleLimit=524288;

// Current-pixel continuation only. This is not a detector or AR measurement.
struct SubtitleCueContinuation {
    bool valid=false,oppositeBarUnavailable=false;
    bool completeCurrentCue=false,fromTopBar=false;
    ActivePictureFrameIdentity identity, templateIdentity, previousIdentity;
    uint64_t policyGeneration=0, continuityGeneration=0;
    ActivePictureBounds physicalBase;
    SubtitleBoxResult accepted;
    std::shared_ptr<const SubtitleInkSnapshot> ink;
    // Complete CURRENT detector coverage; separate from the bounded acquired
    // glyph comparison in ink. Only fromTopBar's physical side is re-proved.
    std::shared_ptr<const SubtitleInkSnapshot> currentInk;
    SubtitleBoxRect verifiedSearch;
    int searchStep=0;
    SubtitleBoxRect sampledBounds, backingVerifiedBounds;
    int pictureTop=0,pictureBottom=0;
    unsigned samples=0;
    double elapsedMs=0;
    const char* reason="not-eligible";

    static bool SameRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    }
    static bool Contains(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return !b.Valid() || (a.Valid() && a.left<=b.left && a.top<=b.top && a.right>=b.right && a.bottom>=b.bottom);
    }
    bool MatchesSource(const ActivePictureFrameIdentity& current,uint64_t policy,uint64_t continuity) const {
        return completeCurrentCue && valid && ink && SameAnalysisFrameIdentity(identity,current) &&
            policyGeneration==policy && continuityGeneration==continuity;
    }
    bool MatchesSource(const ActivePictureFrameIdentity& current,uint64_t policy,uint64_t continuity,
        const SubtitleBoxResult& text) const {
        return MatchesSource(current,policy,continuity) && MatchesCue(text);
    }
    bool Matches(const ActivePictureFrameIdentity& current,uint64_t policy,uint64_t continuity,
        const ActivePictureBounds& base) const {
        return valid && ink && SameAnalysisFrameIdentity(identity,current) && policyGeneration==policy &&
            continuityGeneration==continuity && physicalBase.left==base.left && physicalBase.top==base.top &&
            physicalBase.right==base.right && physicalBase.bottom==base.bottom &&
            physicalBase.rasterWidth==base.rasterWidth && physicalBase.rasterHeight==base.rasterHeight;
    }
    bool Matches(const ActivePictureFrameIdentity& current,uint64_t policy,uint64_t continuity,
        const SubtitleBoxResult& text,const ActivePictureBounds& base) const {
        return Matches(current,policy,continuity,base) && MatchesCue(text);
    }
    bool MatchesCue(const SubtitleBoxResult& text) const {
        if(!valid || !text.detected || text.confirmationPending || accepted.confirmationPending || text.cue!=accepted.cue || text.lineCount!=accepted.lineCount ||
            !SameRect(text.bounds,accepted.bounds) || !SameRect(text.sourcePanel,accepted.sourcePanel) ||
            !SameRect(text.capturePanel,accepted.capturePanel) || text.capturePanelMeasured!=accepted.capturePanelMeasured)
            return false;
        for(int i=0;i<text.lineCount && i<3;++i)
            if(!SameRect(text.lineBounds[i],accepted.lineBounds[i]) ||
                !SameRect(text.lineCapturePanels[i],accepted.lineCapturePanels[i]))return false;
        return text.lineCount>0 && text.lineCount<=3;
    }
};
