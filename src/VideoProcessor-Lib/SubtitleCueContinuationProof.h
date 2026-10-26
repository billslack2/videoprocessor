#pragma once
#include <SubtitleBoxDetector.h>
#include <algorithm>
#include <ActivePictureDecisionTimeline.h>

// Current-pixel continuation only. This is not a detector or AR measurement.
struct SubtitleCueContinuation {
    bool valid=false;
    // Glyph continuity only: never certifies picture edges or cleanup pixels.
    bool localGlyphsOnly=false;
    ActivePictureFrameIdentity identity, templateIdentity, previousIdentity;
    uint64_t policyGeneration=0, continuityGeneration=0;
    ActivePictureBounds physicalBase;
    SubtitleBoxResult accepted;
    std::shared_ptr<const SubtitleInkSnapshot> ink;
    SubtitleBoxRect sampledBounds, backingVerifiedBounds;
    int pictureTop=0,pictureBottom=0;
    unsigned samples=0;
    // First disagreeing native sample. Band bits: acquired/raw/native picture;
    // pixel bits: reference ink/current ink/reference black/current black.
    int mismatchX=-1,mismatchY=-1;
    unsigned mismatchBandMask=0,mismatchPixelMask=0;
    double elapsedMs=0;
    const char* reason="not-eligible";

    static bool SameRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    }
    static bool Contains(const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return !b.Valid() || (a.Valid() && a.left<=b.left && a.top<=b.top && a.right>=b.right && a.bottom>=b.bottom);
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
    // Local current glyph ownership can reuse a cached opposite plane, but
    // never changes that geometry or relaxes the independently current cue side.
    bool MatchesCachedGlyphPicture(int cachedTop,int cachedBottom) const {
        if(!valid)return false;
        if(cachedTop==pictureTop && cachedBottom==pictureBottom)return true;
        const int height=physicalBase.rasterHeight,width=physicalBase.rasterWidth;
        if(!localGlyphsOnly || backingVerifiedBounds.Valid() || accepted.sourcePanel.Valid() ||
            accepted.capturePanel.Valid() || accepted.provenSourcePanel.Valid() ||
            accepted.lineCount<1 || accepted.lineCount>3 || width<=0 || height<=0 ||
            pictureTop<=0 || pictureBottom>=height || pictureTop>=pictureBottom ||
            cachedTop<=0 || cachedBottom>=height || cachedTop>=cachedBottom ||
            physicalBase.top<=0 || physicalBase.bottom>=height || physicalBase.top>=physicalBase.bottom)
            return false;
        bool top=true,bottom=true;
        for(int i=0;i<accepted.lineCount;++i) {
            const auto& line=accepted.lineBounds[i];
            if(!line.Valid() || line.left<0 || line.right>width || line.top<0 || line.bottom>height ||
                accepted.lineCapturePanels[i].Valid())return false;
            top &= line.bottom<=(std::min)({cachedTop,pictureTop,physicalBase.top});
            bottom &= line.top>=(std::max)({cachedBottom,pictureBottom,physicalBase.bottom});
        }
        return top!=bottom && (top?cachedTop==pictureTop:cachedBottom==pictureBottom);
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
