#pragma once
#include <SubtitleBoxDetector.h>
#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <SubtitleBarEvidence.h>
#include <algorithm>
#include <chrono>
#include <limits>

// A strongly acquired bar plane may be carried forward only when every new
// frame freshly revalidates that exact plane from its own pixels. The identity
// advances on a successful weak-edge frame while the geometry stays fixed.
struct SubtitleBarTrackingReference
{
    ActivePictureFrameIdentity identity;
    int width = 0, height = 0, pictureTop = 0, pictureBottom = 0;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    bool valid = false;

    bool IsImmediatePredecessor(const ActivePictureFrameIdentity& next,
        int nextWidth, int nextHeight, uint64_t nextPolicyGeneration = 0,
        uint64_t nextContinuityGeneration = 0) const
    {
        return valid && width == nextWidth && height == nextHeight &&
            policyGeneration == nextPolicyGeneration &&
            continuityGeneration == nextContinuityGeneration &&
            identity.transportGeneration == next.transportGeneration &&
            identity.sourceFormatGeneration == next.sourceFormatGeneration &&
            identity.viewportGeneration == next.viewportGeneration &&
            identity.rendererGeneration == next.rendererGeneration &&
            identity.acceptedSequence + 1 == next.acceptedSequence;
    }
};

// A measurement belongs to source pixels, never retained presentation geometry.
struct SubtitleBoxObservation
{
    ActivePictureFrameIdentity identity;
    SubtitleBoxResult text;
    int width = 0, height = 0, pictureTop = 0, pictureBottom = 0;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    bool analyzed = false, barAuthority = false, barTrackingAuthority = false;
    bool currentAnchorUsesTrackedEdge = false, discontinuity = false;
    bool trackedBottomHypothesisAttempted = false, trackedBottomHypothesisDetected = false;
    bool trackedBottomHypothesisCrosses = false, trackedBottomHypothesisProved = false;
    SubtitleBoxRect trackedBottomHypothesisAnchor;
    int trackedBottomHypothesisSupport = 0, trackedBottomHypothesisMaxIntrusion = 0;
    double trackedBottomHypothesisBlackFraction = 0.0;
    bool hasBarTrackingReference = false;
    ActivePictureFrameIdentity barTrackingReferenceIdentity;
    int barTrackingReferenceTop = 0, barTrackingReferenceBottom = 0;
    // Preserve the reference supplied to every attempt, including misses. A
    // cached negative result is stale as soon as a new predecessor becomes
    // available and may no longer be reused for that incoming frame.
    SubtitleBarTrackingReference incomingBarTrackingReference;
    double analysisMs = 0.0;
    SubtitleBarEvidence barEvidence;
    std::shared_ptr<const SubtitleInkSnapshot> ink;
};

struct SubtitleBoxPreview
{
    SubtitleBoxObservation current;
    SubtitleBoxResult text;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    unsigned matchingFrames = 0;
    bool available = false, futureAvailable = false, currentLinesConfirmed = false;
    double newScanMs = 0.0;
    unsigned newScanFrames = 0;
    // Preserve the full queued lookahead for fixed-pixel row confirmation.
    // Dropout bridging below remains explicitly capped at two frames.
    std::array<SubtitleBoxObservation,7> following;
    unsigned followingCount = 0;
};

namespace SubtitleBoxLookahead
{
    constexpr size_t MaxFrames = 8;

    inline bool HasBarEvidence(const SubtitleBoxObservation& observation)
    {
        return observation.barAuthority || observation.barTrackingAuthority;
    }

    inline SubtitleBarTrackingReference AdvanceBarTrackingReference(
        const SubtitleBoxObservation& observation)
    {
        SubtitleBarTrackingReference result;
        if (!observation.analyzed || observation.discontinuity ||
            !HasBarEvidence(observation)) return result;
        result.identity = observation.identity;
        result.width = observation.width; result.height = observation.height;
        result.pictureTop = observation.pictureTop; result.pictureBottom = observation.pictureBottom;
        result.policyGeneration = observation.policyGeneration;
        result.continuityGeneration = observation.continuityGeneration;
        result.valid = result.pictureTop > 0 || result.pictureBottom < result.height;
        return result;
    }

    inline SubtitleBoxObservation Measure(SubtitleBoxDetector& scanner,
        const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
        const ActivePictureFrameIdentity& identity, bool discontinuity,
        const SubtitleBarTrackingReference& priorBar = {},
        uint64_t policyGeneration = 0, uint64_t continuityGeneration = 0)
    {
        SubtitleBoxObservation result;
        const auto start = std::chrono::steady_clock::now();
        result.identity = identity; result.analyzed = true;
        result.width = source.width; result.height = source.height;
        result.policyGeneration = policyGeneration;
        result.continuityGeneration = continuityGeneration;
        result.discontinuity = discontinuity;
        result.incomingBarTrackingReference = priorBar;
        // Raw crop evidence is retained by the caller for its own purpose. Its
        // connected-picture veto cannot decide whether edge-crossing text exists.
        (void)raw;
        result.barEvidence = ExtractSubtitleBarEvidence(source);
        const auto& bounds = result.barEvidence;
        result.barAuthority = bounds.available;
        result.pictureTop = bounds.top; result.pictureBottom = bounds.bottom;
        if (!discontinuity && priorBar.IsImmediatePredecessor(identity,source.width,source.height,
            policyGeneration,continuityGeneration))
        {
            result.hasBarTrackingReference = true;
            result.barTrackingReferenceIdentity = priorBar.identity;
            result.barTrackingReferenceTop = priorBar.pictureTop;
            result.barTrackingReferenceBottom = priorBar.pictureBottom;
            result.barEvidence = RevalidateSubtitleBarEvidence(source,bounds,
                priorBar.pictureTop,priorBar.pictureBottom);
            result.barTrackingAuthority = result.barEvidence.topRevalidatedAtReference ||
                result.barEvidence.bottomRevalidatedAtReference;
            if (result.barEvidence.topRevalidatedAtReference)
                result.pictureTop = priorBar.pictureTop;
            if (result.barEvidence.bottomRevalidatedAtReference)
                result.pictureBottom = priorBar.pictureBottom;
        }
        // If a current caption itself crosses a previously proved lower bar
        // plane, its bright strokes can trip the generic long-intrusion veto.
        // Scan provisionally at that old plane, then let only detector-owned
        // glyph pixels explain the intrusion. The independent masked check
        // still requires fresh distributed edge evidence and a mostly black
        // unmasked bar. This path never changes native active-picture bounds.
        bool provisionalAtTrackedBottom = false;
        if (!discontinuity && priorBar.IsImmediatePredecessor(identity,source.width,source.height,
                policyGeneration,continuityGeneration) && priorBar.pictureBottom<source.height &&
            !result.barEvidence.bottomRevalidatedAtReference &&
            bounds.bottom==source.height &&
            !(bounds.bottom<source.height &&
                std::abs(bounds.bottom-priorBar.pictureBottom)>(std::max)(2,source.height/540)))
        {
            result.trackedBottomHypothesisAttempted=true;
            scanner.Reset();
            const auto provisional=scanner.Analyze(source,result.pictureTop,priorBar.pictureBottom,
                identity.acceptedSequence,identity.viewportGeneration);
            const auto provisionalInk=scanner.InkSnapshot();
            result.trackedBottomHypothesisDetected=provisional.detected;
            result.trackedBottomHypothesisAnchor=provisional.anchor;
            result.trackedBottomHypothesisCrosses=provisional.anchor.Valid() &&
                provisional.anchor.top<priorBar.pictureBottom &&
                provisional.anchor.bottom>=priorBar.pictureBottom;
            if (result.trackedBottomHypothesisDetected && result.trackedBottomHypothesisCrosses && provisionalInk)
            {
                auto masked=RevalidateSubtitleBarEvidence(source,bounds,priorBar.pictureTop,
                    priorBar.pictureBottom,provisionalInk.get(),&provisional.anchor);
                result.trackedBottomHypothesisSupport=masked.bottomReferenceBoundarySupport;
                result.trackedBottomHypothesisBlackFraction=masked.bottomReferenceBlackFraction;
                result.trackedBottomHypothesisMaxIntrusion=masked.bottomReferenceMaxIntrusion;
                result.trackedBottomHypothesisProved=masked.bottomRevalidatedAtReference;
                if (masked.bottomRevalidatedAtReference)
                {
                    result.barEvidence=masked;
                    result.barTrackingAuthority=true;
                    result.pictureBottom=priorBar.pictureBottom;
                    result.text=provisional;
                    result.ink=provisionalInk;
                    provisionalAtTrackedBottom=true;
                }
            }
        }
        if (result.barAuthority || result.barTrackingAuthority)
        {
            if (!provisionalAtTrackedBottom) {
                scanner.Reset();
                result.text = scanner.Analyze(source, result.pictureTop, result.pictureBottom,
                    identity.acceptedSequence, identity.viewportGeneration);
                result.ink = scanner.InkSnapshot();
            }
            const auto& anchor=result.text.anchor;
            // Any glyph ink in a tracked bar depends on that boundary, even if
            // the whole line sits well inside the bar instead of crossing it.
            const bool anchorInTrackedTopBar=anchor.Valid() && anchor.top<result.pictureTop;
            const bool anchorInTrackedBottomBar=anchor.Valid() && anchor.bottom>result.pictureBottom;
            const bool topStrongAtAnchor=bounds.top>0 && bounds.top==result.pictureTop;
            const bool bottomStrongAtAnchor=bounds.bottom<source.height &&
                bounds.bottom==result.pictureBottom;
            result.currentAnchorUsesTrackedEdge=
                (result.barEvidence.topRevalidatedAtReference && !topStrongAtAnchor &&
                    anchorInTrackedTopBar) ||
                (result.barEvidence.bottomRevalidatedAtReference && !bottomStrongAtAnchor &&
                    anchorInTrackedBottomBar);
        }
        result.analysisMs = std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        return result;
    }

    inline bool CanReuseMeasurement(const SubtitleBoxObservation& observation,
        const ActivePictureFrameIdentity& identity, int width, int height,
        uint64_t policyGeneration, uint64_t continuityGeneration,
        bool discontinuity, const SubtitleBarTrackingReference& currentReference)
    {
        if (!observation.analyzed || !SameActivePictureFrameIdentity(observation.identity,identity) ||
            observation.width != width || observation.height != height ||
            observation.policyGeneration != policyGeneration ||
            observation.continuityGeneration != continuityGeneration ||
            observation.discontinuity != discontinuity) return false;
        const auto& measuredReference = observation.incomingBarTrackingReference;
        if (measuredReference.valid != currentReference.valid) return false;
        if (measuredReference.valid &&
            (measuredReference.width != currentReference.width ||
            measuredReference.height != currentReference.height ||
            measuredReference.pictureTop != currentReference.pictureTop ||
            measuredReference.pictureBottom != currentReference.pictureBottom ||
            measuredReference.policyGeneration != currentReference.policyGeneration ||
            measuredReference.continuityGeneration != currentReference.continuityGeneration ||
            !SameActivePictureFrameIdentity(measuredReference.identity,currentReference.identity))) return false;
        if (!observation.barTrackingAuthority) return true;
        return observation.hasBarTrackingReference &&
            currentReference.IsImmediatePredecessor(identity,width,height,
                policyGeneration,continuityGeneration) &&
            SameActivePictureFrameIdentity(observation.barTrackingReferenceIdentity,
                currentReference.identity) &&
            observation.barTrackingReferenceTop == currentReference.pictureTop &&
            observation.barTrackingReferenceBottom == currentReference.pictureBottom;
    }

    inline bool SameContext(const SubtitleBoxObservation& a, const SubtitleBoxObservation& b)
    {
        const auto& x = a.identity; const auto& y = b.identity;
        return x.transportGeneration == y.transportGeneration &&
            x.sourceFormatGeneration == y.sourceFormatGeneration &&
            x.viewportGeneration == y.viewportGeneration && x.rendererGeneration == y.rendererGeneration &&
            a.width == b.width && a.height == b.height &&
            // A measured encoded edge can wobble by about half a percent as
            // scene pixels change. Current strong or fixed-plane current-pixel
            // proof is required on both frames; a lost/changed edge breaks it.
            (a.pictureTop>0)==(b.pictureTop>0) &&
            (a.pictureBottom<a.height)==(b.pictureBottom<b.height) &&
            std::abs(a.pictureTop-b.pictureTop)<(std::max)(1,a.height/180) &&
            std::abs(a.pictureBottom-b.pictureBottom)<(std::max)(1,a.height/180) &&
            HasBarEvidence(a) && HasBarEvidence(b);
    }

    template<size_t N>
    inline bool SimilarSignature(const std::array<uint64_t, N>& a,
        const std::array<uint64_t, N>& b)
    {
        int difference = 0, total = 0;
        auto bits = [](uint64_t value) { int n = 0; for (; value; value &= value - 1) ++n; return n; };
        for (size_t i = 0; i < a.size(); ++i)
        { difference += bits(a[i] ^ b[i]); total += bits(a[i] | b[i]); }
        return total > 0 && difference * 100 <= total * 25;
    }

    inline bool SimilarSignature(const std::array<uint8_t, 1024>& a,
        const std::array<uint8_t, 1024>& b)
    {
        unsigned difference = 0, total = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            difference += static_cast<unsigned>(std::abs(int(a[i])-int(b[i])));
            total += (std::max)(a[i],b[i]);
        }
        return total > 0 && difference * 100 <= total * 25;
    }

    inline bool SameLine(const SubtitleBoxRect& a, const SubtitleBoxRect& b, int tolerance)
    {
        return a.Valid() && b.Valid() && std::abs(a.left-b.left) <= tolerance &&
            std::abs(a.right-b.right) <= tolerance && std::abs(a.top-b.top) <= tolerance &&
            std::abs(a.bottom-b.bottom) <= tolerance;
    }

    inline bool ContainsCurrentInk(const SubtitleBoxRect& box, const SubtitleBoxResult& text)
    {
        if (!box.Valid()) return false;
        for (int i = 0; i < text.lineCount; ++i)
        {
            const auto& line = text.lineBounds[i];
            if (line.left < box.left || line.top < box.top ||
                line.right > box.right || line.bottom > box.bottom) return false;
        }
        return true;
    }

    inline bool ContainsMatchedInk(const SubtitleBoxRect& box, const SubtitleBoxResult& current,
        const SubtitleBoxResult& accepted, int tolerance)
    {
        for (int i = 0; i < current.lineCount; ++i)
            for (int j = 0; j < accepted.lineCount; ++j)
                if (SameLine(current.lineBounds[i],accepted.lineBounds[j],tolerance) &&
                    SimilarSignature(current.lineSignatures[i],accepted.lineSignatures[j]))
                {
                    const auto& line = current.lineBounds[i];
                    if (line.left < box.left || line.top < box.top ||
                        line.right > box.right || line.bottom > box.bottom) return false;
                }
        return true;
    }

    inline bool SameCue(const SubtitleBoxObservation& a, const SubtitleBoxObservation& b)
    {
        if (!SameContext(a,b) || !a.text.detected || !b.text.detected) return false;
        const int tolerance = (std::max)(4, a.height / 180);
        // Every common-height line must agree. An unchanged bottom line cannot
        // lend confirmation to a changed upper line. Missing lines may be added.
        int matches = 0;
        for (int i = 0; i < a.text.lineCount; ++i)
            for (int j = 0; j < b.text.lineCount; ++j)
            {
                const auto& x = a.text.lineBounds[i]; const auto& y = b.text.lineBounds[j];
                if (std::abs(x.top-y.top) > tolerance || std::abs(x.bottom-y.bottom) > tolerance) continue;
                if (!SameLine(x,y,tolerance) ||
                    !SimilarSignature(a.text.lineSignatures[i], b.text.lineSignatures[j])) return false;
                ++matches;
            }
        return matches > 0 && matches == (std::min)(a.text.lineCount,b.text.lineCount);
    }

    inline int BarAnchorLine(const SubtitleBoxObservation& observation)
    {
        int best=-1,bestIntrusion=0;
        for(int i=0;i<observation.text.lineCount;++i) {
            const auto& line=observation.text.lineBounds[i];
            const int intrusion=(std::max)(0,observation.pictureTop-line.top)+
                (std::max)(0,line.bottom-observation.pictureBottom);
            if(intrusion>bestIntrusion) {best=i;bestIntrusion=intrusion;}
        }
        return best;
    }

    // Confirm one subtitle row independently at fixed source coordinates.
    // This lets a stable bar-anchored caption survive a separately grouped
    // scene row, while rejecting rows whose bright pixels drift between frames.
    inline bool SameLinePixels(const SubtitleBoxObservation& a, int aLine,
        const SubtitleBoxObservation& b, int bLine)
    {
        if (!SameContext(a,b) || !a.ink || !b.ink || aLine<0 || bLine<0 ||
            aLine>=a.text.lineCount || bLine>=b.text.lineCount) return false;
        const auto& old=*a.ink; const auto& now=*b.ink;
        if (old.width!=now.width || old.height!=now.height || old.step<=0 ||
            old.step!=now.step || old.sourceRows!=now.sourceRows) return false;
        const auto& ar=a.text.lineBounds[aLine]; const auto& br=b.text.lineBounds[bLine];
        const int tolerance=(std::max)(4,a.height/180);
        if (!ar.Valid() || !br.Valid() || std::abs(ar.top-br.top)>tolerance ||
            std::abs(ar.bottom-br.bottom)>tolerance) return false;
        const int left=(std::max)(0,(std::min)(ar.left,br.left)/old.step);
        const int right=(std::min)(old.width,((std::max)(ar.right,br.right)+old.step-1)/old.step);
        const int top=(std::max)(0,(std::min)(ar.top,br.top)/old.step);
        const int bottom=(std::min)(old.height,((std::max)(ar.bottom,br.bottom)+old.step-1)/old.step);
        unsigned ownedInk=0,covered=0;
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
            if(!old.Get(x,y,true)) continue;
            ++ownedInk;
            covered+=now.Get(x,y);
        }
        return ownedInk>=4 && covered*100>=ownedInk*90;
    }

    inline bool SameCrowdedLine(const SubtitleBoxRect& a,const SubtitleBoxRect& b,int yTolerance)
    {
        return a.Valid() && b.Valid() && std::abs(a.left-b.left)<=2 &&
            std::abs(a.right-b.right)<=2 && std::abs(a.top-b.top)<=yTolerance &&
            std::abs(a.bottom-b.bottom)<=yTolerance;
    }

    inline double OwnedLinePixelCoverage(const SubtitleBoxObservation& reference,int line,
        const SubtitleBoxObservation& current)
    {
        if(!SameContext(reference,current) || !reference.ink || !current.ink || line<0 ||
            line>=reference.text.lineCount) return 0.0;
        const auto& old=*reference.ink;const auto& now=*current.ink;
        if(old.width!=now.width || old.height!=now.height || old.step<=0 || old.step!=now.step ||
            old.sourceRows!=now.sourceRows) return 0.0;
        const auto& bounds=reference.text.lineBounds[line];
        const int left=(std::max)(0,bounds.left/old.step);
        const int right=(std::min)(old.width,(bounds.right+old.step-1)/old.step);
        const int top=(std::max)(0,bounds.top/old.step);
        const int bottom=(std::min)(old.height,(bounds.bottom+old.step-1)/old.step);
        unsigned owned=0,covered=0;
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)
            if(old.Get(x,y,true)) {++owned;covered+=now.Get(x,y);}
        return owned>=4?double(covered)/owned:0.0;
    }

    inline bool OwnedLinePixelsVisible(const SubtitleBoxObservation& reference,int line,
        const SubtitleBoxObservation& current)
    {
        return OwnedLinePixelCoverage(reference,line,current)>=0.90;
    }

    // Find new glyph-like components just outside a cached line. Coordinates
    // are converted from source pixels through the snapshot's sampled rows;
    // previously present scene details are ignored.
    inline bool FindNewInkExtension(const SubtitleInkSnapshot& reference,
        const SubtitleInkSnapshot& current, const SubtitleBoxRect& bounds,
        int sourceWidth, int sourceHeight, SubtitleBoxRect& extension)
    {
        extension={};
        if (!bounds.Valid()) return true;
        if (reference.width!=current.width || reference.height!=current.height ||
            reference.step<=0 || reference.step!=current.step ||
            reference.sourceRows.size()!=size_t(reference.height) ||
            current.sourceRows.size()!=size_t(current.height)) return true;
        const int leftLine=(std::max)(0,bounds.left/current.step);
        const int rightLine=(std::min)(current.width,(bounds.right+current.step-1)/current.step);
        const auto first=std::lower_bound(current.sourceRows.begin(),current.sourceRows.end(),bounds.top);
        const auto last=std::lower_bound(current.sourceRows.begin(),current.sourceRows.end(),bounds.bottom);
        const int topLine=int(first-current.sourceRows.begin());
        const int bottomLine=int(last-current.sourceRows.begin());
        const int lineHeight=bottomLine-topLine;
        if (leftLine>=rightLine || topLine<0 || bottomLine>current.height || lineHeight<=0) return true;
        const int left=(std::max)(0,leftLine-8*lineHeight);
        const int right=(std::min)(current.width,rightLine+8*lineHeight);
        const int top=(std::max)(0,topLine-2*lineHeight-1);
        const int bottom=(std::min)(current.height,bottomLine+2*lineHeight+1);
        if (left>=right || top>=bottom) return true;
        const int roiWidth=right-left,roiHeight=bottom-top;
        // Connected words in Arabic can span several nominal glyph heights.
        // Keep the candidate bounded by both the cue line scale and frame width;
        // baseline, density, novelty, and cue checks below still decide whether
        // it belongs to the established subtitle.
        const int maximumComponentWidth=(std::min)(current.width*9/10,
            (std::max)(lineHeight*8,8));
        std::vector<uint8_t> visited(size_t(roiWidth)*roiHeight,0);
        std::vector<int> pending;
        std::vector<SubtitleBoxRect> candidates;
        auto scanSide=[&](int xStart,int xEnd,int yStart,int yEnd) {
        for (int y=yStart;y<yEnd;++y)
            for (int x=xStart;x<xEnd;++x)
            {
                if (!current.Get(x,y)) continue;
                const size_t start=size_t(y-top)*roiWidth+(x-left);
                if (visited[start]) continue;
                visited[start]=1;pending.clear();pending.push_back(int(start));
                int minX=x,maxX=x,minY=y,maxY=y,pixels=0,newPixels=0;
                for (size_t at=0;at<pending.size();++at)
                {
                    const int index=pending[at],cx=left+index%roiWidth,cy=top+index/roiWidth;
                    ++pixels;minX=(std::min)(minX,cx);maxX=(std::max)(maxX,cx);
                    minY=(std::min)(minY,cy);maxY=(std::max)(maxY,cy);
                    const int sourceY=current.sourceRows[size_t(cy)];
                    const auto oldRow=std::lower_bound(reference.sourceRows.begin(),
                        reference.sourceRows.end(),sourceY);
                    const bool existed=oldRow!=reference.sourceRows.end() && *oldRow==sourceY &&
                        reference.Get(cx,int(oldRow-reference.sourceRows.begin()));
                    newPixels+=!existed;
                    for (int dy=-1;dy<=1;++dy)for (int dx=-1;dx<=1;++dx)
                    {
                        const int nx=cx+dx,ny=cy+dy;
                        if (nx<xStart || nx>=xEnd || ny<top || ny>=bottom || !current.Get(nx,ny)) continue;
                        const size_t next=size_t(ny-top)*roiWidth+(nx-left);
                        if (!visited[next]) {visited[next]=1;pending.push_back(int(next));}
                    }
                }
                const int componentWidth=maxX-minX+1,componentHeight=maxY-minY+1;
                const int boxArea=componentWidth*componentHeight;
                if (pixels>=4 && componentHeight>=(std::max)(3,lineHeight/2) &&
                    componentWidth>=1 && componentWidth<=maximumComponentWidth &&
                    pixels*100>=boxArea*10 && pixels*100<=boxArea*100 &&
                    newPixels*100>=pixels*70)
                {
                    candidates.push_back({minX,minY,maxX+1,maxY+1});
                }
            }
        };
        scanSide(left,leftLine,top,bottom);
        scanSide(rightLine,right,top,bottom);
        scanSide(leftLine,rightLine,top,topLine);
        scanSide(leftLine,rightLine,bottomLine,bottom);
        // A stable bright scene fragment is not automatically a new word.
        // Side extensions share the acquired baseline and plausible word gap.
        // A new row needs at least two aligned components; the full detector
        // remains responsible for independently acquiring singleton lines.
        for(const auto& seed:candidates) {
            const int seedHeight=seed.bottom-seed.top;
            if(seedHeight*3<lineHeight*2 || seedHeight>lineHeight*2)continue;
            SubtitleBoxRect group=seed;
            int count=0,nearestGap=sourceWidth;
            std::vector<uint8_t> joined(candidates.size(),0);
            bool grew=true;
            while(grew) {
                grew=false;
                for(size_t i=0;i<candidates.size();++i) {
                    const auto& candidate=candidates[i];
                    const int candidateHeight=candidate.bottom-candidate.top;
                    const int groupGap=(std::max)(group.left-candidate.right,candidate.left-group.right);
                    if(joined[i] || groupGap>lineHeight*3 || candidateHeight*3<lineHeight*2 ||
                        candidateHeight>lineHeight*2 ||
                        std::abs(candidate.top+candidate.bottom-seed.top-seed.bottom)>lineHeight*2/3)continue;
                    joined[i]=1;grew=true;++count;
                    group={(std::min)(group.left,candidate.left),(std::min)(group.top,candidate.top),
                        (std::max)(group.right,candidate.right),(std::max)(group.bottom,candidate.bottom)};
                    const int gap=(std::max)(leftLine-candidate.right,candidate.left-rightLine);
                    nearestGap=(std::min)(nearestGap,gap);
                }
            }
            const bool sameBaseline=std::abs(group.top+group.bottom-topLine-bottomLine)<=lineHeight*2/3;
            if(group.right-group.left>=current.width*9/10)continue;
            if(sameBaseline) {
                if(nearestGap>lineHeight*3)continue;
            } else {
                const int rowGap=(std::max)(topLine-group.bottom,group.top-bottomLine);
                if(count<2 || group.right-group.left<lineHeight || rowGap>lineHeight*2 ||
                    std::abs(group.left+group.right-leftLine-rightLine)>
                        (std::max)(lineHeight*8,(rightLine-leftLine)*2/3))continue;
            }
            const SubtitleBoxRect sourceBounds{
                (std::max)(0,group.left*current.step),current.sourceRows[size_t(group.top)],
                (std::min)(sourceWidth,group.right*current.step),
                (std::min)(sourceHeight,current.sourceRows[size_t(group.bottom-1)]+current.step)};
            if(!extension.Valid())extension=sourceBounds;
            else extension={(std::min)(extension.left,sourceBounds.left),(std::min)(extension.top,sourceBounds.top),
                (std::max)(extension.right,sourceBounds.right),(std::max)(extension.bottom,sourceBounds.bottom)};
        }
        return extension.Valid();
    }

    // Revalidate the actual acquisition pixels at fixed source coordinates.
    // Normalized signatures alone can hide one changed word in a long line.
    // Owned ink supplies positive glyph evidence. Compare glyph interiors,
    // including holes, without treating changing inter-glyph picture pixels as
    // subtitle identity. Newly appearing glyph-sized components still reject.
    // Small overlapping horizontal windows
    // prevent unrelated unchanged words from diluting a local text change.
    inline bool SameInkAtReference(const SubtitleBoxObservation& reference,
        const SubtitleBoxObservation& current)
    {
        if (!SameContext(reference,current) || !reference.ink || !current.ink ||
            !reference.text.detected || reference.text.workLimit || current.text.workLimit ||
            reference.text.lineCount<1 || reference.text.lineCount>3) return false;
        const auto& old=*reference.ink; const auto& now=*current.ink;
        const size_t words=(size_t(old.width)*old.height+63)/64;
        if (old.width<=0 || old.height<=0 || old.width>960 || old.height>540 || old.step<=0 ||
            old.width!=now.width || old.height!=now.height || old.step!=now.step ||
            old.width!=(reference.width+old.step-1)/old.step ||
            old.height!=(reference.height+old.step-1)/old.step ||
            old.sourceRows.size()!=size_t(old.height) || now.sourceRows.size()!=size_t(now.height) ||
            old.rawInk.size()!=words || old.ownedInk.size()!=words || now.rawInk.size()!=words) return false;
        struct Counts { unsigned owned=0,covered=0,different=0,total=0; };
        unsigned currentBarInk=0;
        for (int line=0;line<reference.text.lineCount;++line)
        {
            const auto& bounds=reference.text.lineBounds[line];
            if (!bounds.Valid()) return false;
            const int left=(std::max)(0,bounds.left/old.step);
            const int right=(std::min)(old.width,(bounds.right+old.step-1)/old.step);
            const int top=(std::max)(0,bounds.top/old.step);
            const int bottom=(std::min)(old.height,(bounds.bottom+old.step-1)/old.step);
            if (left>=right || top>=bottom) return false;
            const int roiWidth=right-left,roiHeight=bottom-top;
            std::vector<uint8_t> glyphEnvelope(size_t(roiWidth)*roiHeight,0);
            std::vector<uint8_t> visited(glyphEnvelope.size(),0);
            std::vector<int> pending;
            // Ownership is fixed at acquisition. Flood each owned component
            // to preserve its interior holes while excluding surrounding scene.
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                const size_t start=size_t(y-top)*roiWidth+x-left;
                if(visited[start] || !old.Get(x,y,true))continue;
                visited[start]=1;pending.clear();pending.push_back(int(start));
                int minX=x,maxX=x,minY=y,maxY=y;
                unsigned owned=0,covered=0;
                for(size_t at=0;at<pending.size();++at) {
                    const int index=pending[at],cx=left+index%roiWidth,cy=top+index/roiWidth;
                    minX=(std::min)(minX,cx);maxX=(std::max)(maxX,cx);
                    minY=(std::min)(minY,cy);maxY=(std::max)(maxY,cy);++owned;
                    const auto row=std::lower_bound(now.sourceRows.begin(),now.sourceRows.end(),old.sourceRows[cy]);
                    covered+=row!=now.sourceRows.end() && *row==old.sourceRows[cy] &&
                        now.Get(cx,int(row-now.sourceRows.begin()));
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                        const int nx=cx+dx,ny=cy+dy;
                        if(nx<left || nx>=right || ny<top || ny>=bottom || !old.Get(nx,ny,true))continue;
                        const size_t next=size_t(ny-top)*roiWidth+nx-left;
                        if(!visited[next]) {visited[next]=1;pending.push_back(int(next));}
                    }
                }
                if(owned>=4 && covered*100<owned*85) {
                    return false;
                }
                for(int yy=minY;yy<=maxY;++yy)for(int xx=minX;xx<=maxX;++xx)
                    glyphEnvelope[size_t(yy-top)*roiWidth+xx-left]=1;
            }
            // A new glyph in previously empty line space cannot be silently
            // ignored as background. Small transient scene fragments are not
            // sufficient; require a component with the scale of this line.
            std::fill(visited.begin(),visited.end(),0);
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                const size_t start=size_t(y-top)*roiWidth+x-left;
                const auto row=std::lower_bound(now.sourceRows.begin(),now.sourceRows.end(),old.sourceRows[y]);
                const int nowY=row!=now.sourceRows.end() && *row==old.sourceRows[y] ? int(row-now.sourceRows.begin()) : -1;
                if(visited[start] || glyphEnvelope[start] || old.Get(x,y) || nowY<0 || !now.Get(x,nowY))continue;
                visited[start]=1;pending.clear();pending.push_back(int(start));
                int minX=x,maxX=x,minY=y,maxY=y;
                unsigned detachedPixels=0;
                int detachedTop=bottom,detachedBottom=top;
                for(size_t at=0;at<pending.size();++at) {
                    const int index=pending[at],cx=left+index%roiWidth,cy=top+index/roiWidth;
                    bool touchesOldGlyph=false;
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                        touchesOldGlyph=touchesOldGlyph || old.Get(cx+dx,cy+dy,true);
                    if(!touchesOldGlyph) {
                        ++detachedPixels;
                        detachedTop=(std::min)(detachedTop,cy);
                        detachedBottom=(std::max)(detachedBottom,cy+1);
                    }
                    minX=(std::min)(minX,cx);maxX=(std::max)(maxX,cx);
                    minY=(std::min)(minY,cy);maxY=(std::max)(maxY,cy);
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                        const int nx=cx+dx,ny=cy+dy;
                        if(nx<left || nx>=right || ny<top || ny>=bottom)continue;
                        const size_t next=size_t(ny-top)*roiWidth+nx-left;
                        if(visited[next] || glyphEnvelope[next] || old.Get(nx,ny))continue;
                        const auto rr=std::lower_bound(now.sourceRows.begin(),now.sourceRows.end(),old.sourceRows[ny]);
                        if(rr==now.sourceRows.end() || *rr!=old.sourceRows[ny] || !now.Get(nx,int(rr-now.sourceRows.begin())))continue;
                        visited[next]=1;pending.push_back(int(next));
                    }
                }
                // Ignore only the one-sample fringe attached to acquired
                // strokes. A tall newly filled interior remains contradictory
                // even when its attached fringe outnumbers detached pixels.
                if(detachedPixels>=4 &&
                    detachedBottom-detachedTop>=(std::max)(3,roiHeight/2))return false;
            }
            std::vector<Counts> columns(size_t(right-left));
            Counts all;
            for (int y=top;y<bottom;++y)
            {
                const int sourceY=old.sourceRows[y];
                const auto found=std::lower_bound(now.sourceRows.begin(),now.sourceRows.end(),sourceY);
                const int row=found!=now.sourceRows.end() && *found==sourceY ?
                    int(found-now.sourceRows.begin()) : -1;
                for (int x=left;x<right;++x)
                {
                    const bool owned=old.Get(x,y,true), before=old.Get(x,y);
                    // A shifted explicitly sampled boundary row is not evidence
                    // for the same source pixels. Do not interpolate a glyph.
                    if (row<0) { if (owned || before) return false; continue; }
                    const bool after=now.Get(x,row);
                    auto& c=columns[size_t(x-left)];
                    c.owned+=owned; c.covered+=owned&&after;
                    if(glyphEnvelope[size_t(y-top)*roiWidth+x-left]) {
                        c.different+=before!=after; c.total+=before||after;
                    }
                    if (owned && after)
                    {
                        bool barMatch=false;
                        for (int dy=-1;dy<=1;++dy)
                            for (int dx=-1;dx<=1;++dx)
                                if (now.Get(x+dx,row+dy))
                                {
                                    const int matchedRow=row+dy;
                                    if (matchedRow>=0 && matchedRow<now.height &&
                                        (now.sourceRows[size_t(matchedRow)]<current.pictureTop ||
                                         now.sourceRows[size_t(matchedRow)]>=current.pictureBottom))
                                        barMatch=true;
                                }
                        currentBarInk+=barMatch;
                    }
                }
            }
            for (const auto& c:columns) {
                all.owned+=c.owned; all.covered+=c.covered;
                all.different+=c.different; all.total+=c.total;
            }
            if (all.owned<6 || all.covered*100<all.owned*95 ||
                all.different*100>all.total*8) {
                return false;
            }
            const int window=(std::max)(4,bottom-top);
            for (int start=0;start<right-left;start+=(std::max)(1,window/2))
            {
                Counts local;
                for (int x=start;x<(std::min)(right-left,start+window);++x) {
                    const auto& c=columns[size_t(x)];
                    local.owned+=c.owned;local.covered+=c.covered;
                    local.different+=c.different;local.total+=c.total;
                }
                if (local.owned>=6 && (local.covered*100<local.owned*85 ||
                    local.different*100>local.total*15)) {
                    return false;
                }
            }
        }
        // A picture-side pattern alone cannot preserve subtitle eligibility.
        return currentBarInk>0;
    }

    inline SubtitleBoxPreview Resolve(const SubtitleBoxObservation* frames, size_t count,
        uint64_t policy, uint64_t continuity)
    {
        SubtitleBoxPreview result;
        if (!frames || !count || !frames[0].analyzed) return result;
        result.available = true; result.current = frames[0]; result.text = frames[0].text;
        result.policyGeneration = policy; result.continuityGeneration = continuity;
        result.futureAvailable = count > 1;
        result.followingCount=static_cast<unsigned>((std::min)(count-1,result.following.size()));
        for (unsigned i=0;i<result.followingCount;++i) result.following[i]=frames[i+1];
        if (!HasBarEvidence(frames[0]) || !frames[0].text.detected) { result.text = {}; return result; }
        result.matchingFrames = 1;
        for (size_t i = 1; i < (std::min)(count,MaxFrames); ++i)
        {
            const auto& next = frames[i];
            bool cueMatches=SameCue(frames[0],next) && SameCue(frames[i-1],next);
            // The only relaxed path is a crowded grouping (three or more
            // candidate rows). Require two independently matching rows and
            // the bar anchor; ordinary one/two-row cue changes keep the
            // original all-lines SameCue rule.
            if(!cueMatches && frames[0].text.lineCount>=3 && next.text.lineCount>=2 &&
                next.analyzed && !next.discontinuity &&
                next.identity.acceptedSequence==frames[i-1].identity.acceptedSequence+1 &&
                SameContext(frames[0],next) && next.text.detected && HasBarEvidence(next)) {
                const int currentAnchor=BarAnchorLine(frames[0]);
                int nextAnchor=-1, confirmedRows=0;
                const int tol=(std::max)(4,frames[0].height/180);
                for(int b=0;b<next.text.lineCount;++b)
                    if((next.text.lineBounds[b].top<next.pictureTop ||
                        next.text.lineBounds[b].bottom>next.pictureBottom) &&
                        (nextAnchor<0 || (std::max)(next.pictureTop-next.text.lineBounds[b].top,0)+
                            (std::max)(next.text.lineBounds[b].bottom-next.pictureBottom,0)>
                            (std::max)(next.pictureTop-next.text.lineBounds[nextAnchor].top,0)+
                            (std::max)(next.text.lineBounds[nextAnchor].bottom-next.pictureBottom,0))) nextAnchor=b;
                bool anchorMatch=currentAnchor>=0 && nextAnchor>=0 &&
                    SameLine(frames[0].text.lineBounds[currentAnchor],next.text.lineBounds[nextAnchor],tol) &&
                    SimilarSignature(frames[0].text.lineSignatures[currentAnchor],next.text.lineSignatures[nextAnchor]) &&
                    SameLinePixels(frames[0],currentAnchor,next,nextAnchor);
                if(anchorMatch) {
                    for(int a=0;a<frames[0].text.lineCount;++a) for(int b=0;b<next.text.lineCount;++b)
                        if(a!=currentAnchor && b!=nextAnchor &&
                            SameCrowdedLine(frames[0].text.lineBounds[a],next.text.lineBounds[b],tol) &&
                            SimilarSignature(frames[0].text.lineSignatures[a],next.text.lineSignatures[b]) &&
                            SameLinePixels(frames[0],a,next,b)) { ++confirmedRows; break; }
                    cueMatches=confirmedRows>=1;
                }
            }
            if (!next.analyzed || next.discontinuity ||
                next.identity.acceptedSequence != frames[i-1].identity.acceptedSequence + 1 ||
                !cueMatches) break;
            ++result.matchingFrames;
        }
        const int tolerance = (std::max)(4,frames[0].height/180);
        size_t crowdedFarthest=0;int crowdedAnchorLine=-1;
        if(frames[0].text.lineCount>=3) {
            crowdedAnchorLine=BarAnchorLine(frames[0]);
            if(crowdedAnchorLine>=0) for(size_t i=1;i<(std::min)(count,MaxFrames);++i) {
                const auto& prev=frames[i-1];const auto& next=frames[i];
                if(!next.analyzed || next.discontinuity || !next.text.detected || !HasBarEvidence(next) ||
                    !SameContext(frames[0],next) ||
                    next.identity.acceptedSequence!=prev.identity.acceptedSequence+1) break;
                bool anchorVisible=false;
                for(int line=0;line<next.text.lineCount;++line)
                    if((next.text.lineBounds[line].top<next.pictureTop ||
                        next.text.lineBounds[line].bottom>next.pictureBottom) &&
                        SameLine(frames[0].text.lineBounds[crowdedAnchorLine],next.text.lineBounds[line],tolerance) &&
                        SimilarSignature(frames[0].text.lineSignatures[crowdedAnchorLine],next.text.lineSignatures[line]) &&
                        SameLinePixels(frames[0],crowdedAnchorLine,next,line)) {anchorVisible=true;break;}
                if(!anchorVisible) break;
                crowdedFarthest=i;
            }
        }
        result.currentLinesConfirmed = true;
        std::array<int,3> acceptedLines{}; int acceptedCount=0;
        int nearestLine=-1;unsigned nearestGap=(std::numeric_limits<unsigned>::max)();
        if(frames[0].text.lineCount>=3 && crowdedAnchorLine>=0) {
            const auto& anchor=frames[0].text.lineBounds[crowdedAnchorLine];
            for(int line=0;line<frames[0].text.lineCount;++line) if(line!=crowdedAnchorLine) {
                const auto& candidate=frames[0].text.lineBounds[line];
                const unsigned gap=candidate.bottom<=anchor.top?unsigned(anchor.top-candidate.bottom):
                    (anchor.bottom<=candidate.top?unsigned(candidate.top-anchor.bottom):0u);
                if(gap<nearestGap) {nearestGap=gap;nearestLine=line;}
            }
        }
        for (int line = 0; line < frames[0].text.lineCount; ++line)
        {
            unsigned support = 0;
            const size_t supportEnd=(frames[0].text.lineCount>=3)?crowdedFarthest+1:result.matchingFrames;
            for (size_t frame = 0; frame < supportEnd; ++frame)
                for (int other = 0; other < frames[frame].text.lineCount; ++other)
                    if ((frames[0].text.lineCount>=3?
                        SameCrowdedLine(frames[0].text.lineBounds[line],frames[frame].text.lineBounds[other],tolerance):
                        SameLine(frames[0].text.lineBounds[line],frames[frame].text.lineBounds[other],tolerance)) &&
                        SimilarSignature(frames[0].text.lineSignatures[line],frames[frame].text.lineSignatures[other]) &&
                        (frames[0].text.lineCount<3 || SameLinePixels(frames[0],line,frames[frame],other)))
                    { ++support; break; }
            const unsigned required=(frames[0].text.lineCount>=3 && supportEnd>=4)?3u:2u;
            const double farthestCoverage=frames[0].text.lineCount<3?1.0:
                OwnedLinePixelCoverage(frames[0],line,frames[crowdedFarthest]);
            const double minimumCoverage=(line==crowdedAnchorLine)?0.90:0.975;
            const bool fullHorizon=supportEnd>=4;
            const int anchorHeight=crowdedAnchorLine>=0?
                frames[0].text.lineBounds[crowdedAnchorLine].bottom-
                    frames[0].text.lineBounds[crowdedAnchorLine].top:0;
            const int candidateHeight=frames[0].text.lineBounds[line].bottom-
                frames[0].text.lineBounds[line].top;
            const bool nearestInShortWindow=line==nearestLine &&
                nearestGap<=unsigned((std::max)(anchorHeight,candidateHeight)*3/2) &&
                farthestCoverage>=0.90;
            const bool farthestPixels=frames[0].text.lineCount<3 ||
                (line==crowdedAnchorLine?farthestCoverage>=minimumCoverage:
                    (fullHorizon?farthestCoverage>=minimumCoverage:nearestInShortWindow));
            if(support>=required && farthestPixels) acceptedLines[acceptedCount++]=line;
            else result.currentLinesConfirmed=false;
        }
        if(frames[0].text.lineCount>=3 && acceptedCount==0) {
            result.text={};
        } else if(frames[0].text.lineCount>=3 && acceptedCount<frames[0].text.lineCount) {
            result.text.bounds=frames[0].text.lineBounds[acceptedLines[0]];
            result.text.lineCount=0;
            for(int k=0;k<acceptedCount;++k) {
                const int line=acceptedLines[k];
                result.text.lineBounds[k]=frames[0].text.lineBounds[line];
                result.text.lineSignatures[k]=frames[0].text.lineSignatures[line];
                const auto& r=frames[0].text.lineBounds[line]; auto& box=result.text.bounds;
                box={(std::min)(box.left,r.left),(std::min)(box.top,r.top),
                    (std::max)(box.right,r.right),(std::max)(box.bottom,r.bottom)};
                ++result.text.lineCount;
            }
            result.text.anchor=frames[0].text.anchor;
        }
        if(frames[0].text.lineCount>=3) result.currentLinesConfirmed=acceptedCount>0;
        // In the crowded-grouping path the current frame is authoritative
        // after per-row pruning. The legacy whole-box future union would
        // reintroduce the very transient row this gate removed.
        if(frames[0].text.lineCount>=3) return result;
        for (size_t i = 1; i < result.matchingFrames; ++i)
        {
            const auto& next = frames[i];
            bool companionsConfirmed = true;
            // A transient picture-side stroke group must not enlarge the box
            // just because the actual bar subtitle persists beneath it.
            for (int line = 0; line < next.text.lineCount; ++line)
            {
                bool presentNow = false;
                for (int current = 0; current < frames[0].text.lineCount; ++current)
                    presentNow = presentNow || (SameLine(next.text.lineBounds[line],
                        frames[0].text.lineBounds[current],tolerance) &&
                        SimilarSignature(next.text.lineSignatures[line],frames[0].text.lineSignatures[current]));
                if (presentNow) continue;
                unsigned support = 0;
                for (size_t frame = 0; frame < result.matchingFrames; ++frame)
                    for (int other = 0; other < frames[frame].text.lineCount; ++other)
                        if (SameLine(next.text.lineBounds[line],frames[frame].text.lineBounds[other],tolerance) &&
                            SimilarSignature(next.text.lineSignatures[line],frames[frame].text.lineSignatures[other]))
                        { ++support; break; }
                companionsConfirmed = companionsConfirmed && support >= 2;
                if(frames[0].text.lineCount>=2 && frames[0].text.lineCount<3 &&
                    next.text.lineCount>frames[0].text.lineCount) {
                    // A future-only picture row may enlarge a two-line current
                    // cue only after a complete four-observation window proves
                    // both the current pixels and a stable near-exact row track.
                    unsigned trackSupport=0;
                    if(result.matchingFrames>=4 &&
                        OwnedLinePixelCoverage(next,line,frames[0])>=0.975 &&
                        OwnedLinePixelCoverage(next,line,frames[result.matchingFrames-1])>=0.975) {
                        for(size_t frame=1;frame<result.matchingFrames;++frame)
                            for(int other=0;other<frames[frame].text.lineCount;++other)
                                if(SameCrowdedLine(next.text.lineBounds[line],
                                    frames[frame].text.lineBounds[other],tolerance) &&
                                    SimilarSignature(next.text.lineSignatures[line],
                                        frames[frame].text.lineSignatures[other])) {
                                    ++trackSupport;break;
                                }
                    }
                    companionsConfirmed=companionsConfirmed && trackSupport>=3;
                }
            }
            if (!companionsConfirmed) continue;
            // Future grouping may reveal an already present line, but future
            // pixels must never create a missing line on the current frame.
            if (next.text.lineCount>frames[0].text.lineCount &&
                !SameInkAtReference(next,frames[0])) continue;
            const auto& r = next.text.bounds;
            auto& box = result.text.bounds;
            box = {(std::min)(box.left,r.left),(std::min)(box.top,r.top),
                (std::max)(box.right,r.right),(std::max)(box.bottom,r.bottom)};
            if (next.text.lineCount > result.text.lineCount)
            {
                result.text.lineCount = next.text.lineCount;
                result.text.lineBounds = next.text.lineBounds;
                result.text.lineSignatures = next.text.lineSignatures;
                result.text.signature = next.text.signature;
            }
        }
        return result;
    }

    inline bool IsCurrent(const SubtitleBoxPreview& result,
        const ActivePictureFrameIdentity& identity, uint64_t policy, uint64_t continuity)
    {
        return result.available && SameActivePictureFrameIdentity(result.current.identity,identity) &&
            result.policyGeneration == policy && result.continuityGeneration == continuity;
    }
}

// Only presentation advances this state. Looking into future pixels never moves
// the current cue forward, and repeated output cannot manufacture confirmation.
class SubtitleBoxPresentation
{
public:
    void Reset() {
        m_previous = {}; m_reference = {}; m_inkReference = {}; m_result = {};
        m_barTrackingReference = {};
        m_expansionEvidence.clear();
        m_hasPrevious = false; m_bridgedFrames = 0;
    }
    SubtitleBarTrackingReference BarTrackingReferenceFor(
        const ActivePictureFrameIdentity& next, int width, int height,
        uint64_t policyGeneration = 0, uint64_t continuityGeneration = 0) const
    {
        return m_barTrackingReference.IsImmediatePredecessor(next,width,height,
            policyGeneration,continuityGeneration)
            ? m_barTrackingReference : SubtitleBarTrackingReference{};
    }
    bool ReusePreviewForRepeatedSourceFrame(const ActivePictureFrameIdentity& identity,
        int width, int height, bool discontinuity,
        uint64_t policyGeneration, uint64_t continuityGeneration,
        SubtitleBoxPreview& preview) const
    {
        if (!m_hasPrevious || !m_previous.available ||
            !SameActivePictureFrameIdentity(m_previous.current.identity,identity) ||
            m_previous.current.width != width || m_previous.current.height != height ||
            m_previous.current.discontinuity != discontinuity ||
            m_previous.policyGeneration != policyGeneration ||
            m_previous.continuityGeneration != continuityGeneration) return false;
        preview = m_previous;
        preview.newScanMs = 0.0;
        preview.newScanFrames = 0;
        return true;
    }
    SubtitleBoxResult Consume(const SubtitleBoxPreview& preview)
    {
        if (!preview.available) { Reset(); return {}; }
        if (m_hasPrevious && SameActivePictureFrameIdentity(m_previous.current.identity,preview.current.identity) &&
            m_previous.policyGeneration == preview.policyGeneration &&
            m_previous.continuityGeneration == preview.continuityGeneration) return m_result;
        bool same = m_hasPrevious && !preview.current.discontinuity &&
            m_previous.policyGeneration == preview.policyGeneration &&
            m_previous.continuityGeneration == preview.continuityGeneration &&
            preview.current.identity.acceptedSequence == m_previous.current.identity.acceptedSequence + 1 &&
            SubtitleBoxLookahead::SameCue(m_previous.current,preview.current) &&
            SubtitleBoxLookahead::SameCue(m_reference,preview.current) &&
            preview.text.lineCount >= m_result.lineCount && m_result.detected &&
            ((preview.text.lineCount > m_result.lineCount &&
                (!preview.futureAvailable || preview.currentLinesConfirmed ||
                    SubtitleBoxLookahead::ContainsMatchedInk(m_result.bounds,preview.current.text,m_result,
                (std::max)(4,preview.current.height/180)))) ||
                SubtitleBoxLookahead::ContainsCurrentInk(m_result.bounds,preview.current.text));
        const bool currentTemplatePixelsMatch=
            SubtitleBoxLookahead::SameInkAtReference(m_inkReference,preview.current);
        // Coarse grouping signatures preserve geometry, but cannot overrule a
        // changed local glyph at the fixed acquisition coordinates.
        if(m_hasPrevious && m_result.detected && m_inkReference.ink && !currentTemplatePixelsMatch)same=false;
        const bool groupingMatches=SubtitleBoxLookahead::SameCue(m_reference,preview.current);
        // An established glyph template belongs to CURRENT pixels. Detector
        // grouping may jump or fail while those exact strokes remain visible;
        // do not let a future grouping vote move the box away from current ink.
        // Changed/missing glyphs, a bar/context break, and discontinuities still
        // invalidate this fixed-coordinate proof. Newly added lines are handled
        // separately below and need their own current-plus-future evidence.
        bool templateStillVisible=m_hasPrevious && m_result.detected &&
            !preview.current.discontinuity &&
            preview.current.identity.acceptedSequence==m_previous.current.identity.acceptedSequence+1 &&
            m_previous.policyGeneration==preview.policyGeneration &&
            m_previous.continuityGeneration==preview.continuityGeneration &&
            SubtitleBoxLookahead::SameContext(m_inkReference,preview.current) &&
            // A total detector miss can be ambiguous when new glyph-like ink
            // has appeared outside the learned lines (such as a newly split
            // second line). Keep the template through a miss only when no such
            // current extension is present.
            (preview.current.text.detected || NoNewInkOutsideReference(preview.current)) &&
            // A surviving lower line can match even after the missing upper
            // line changed. Require the complete fixed-coordinate ink mask to
            // agree before holding the old multi-line cue through regrouping.
            currentTemplatePixelsMatch;
        const bool expansionStillVisible=ExpansionStillVisible(preview);
        if(!expansionStillVisible) { templateStillVisible=false; same=false; }
        const bool templateHeld=templateStillVisible &&
            (!groupingMatches || preview.current.text.lineCount<m_result.lineCount || preview.text.lineCount==0);
        if (templateStillVisible) same=true;
        auto result = preview.text;
        const bool trackedCueConfirmed = preview.current.barTrackingAuthority &&
            preview.current.text.detected && preview.matchingFrames >= 2 &&
            preview.currentLinesConfirmed;
        if (!SubtitleBoxLookahead::HasBarEvidence(preview.current) ||
            (!preview.current.text.detected && !templateStillVisible) ||
            (!preview.current.barAuthority && !same && !trackedCueConfirmed) ||
            (preview.current.currentAnchorUsesTrackedEdge && !same && !trackedCueConfirmed) ||
            (!same && preview.futureAvailable &&
                (preview.matchingFrames < 2 || !preview.currentLinesConfirmed))) result = {};
        if (templateHeld) { result=m_result;result.held=true;result.revised=false; }
        const bool bridged=!result.detected && CanBridge(preview);
        if (bridged) {
            result=m_result; result.held=true; result.revised=false;
            ++m_bridgedFrames;
        }
        if (result.detected && !bridged && !templateHeld)
        {
            result.held=false; m_bridgedFrames=0;
            if (same)
            {
                // Fresh matching strokes still support the established cue,
                // but one unmatched companion cannot enlarge its stable box.
                if (preview.futureAvailable && !preview.currentLinesConfirmed &&
                    result.lineCount > m_result.lineCount) result = m_result;
                const auto& old = m_result.bounds;
                if (result.lineCount > m_result.lineCount)
                {
                    result.bounds = {(std::min)(old.left,result.bounds.left),(std::min)(old.top,result.bounds.top),
                        (std::max)(old.right,result.bounds.right),(std::max)(old.bottom,result.bounds.bottom)};
                    m_reference.text = result;
                    m_inkReference = {};
                }
                else result.bounds = old;
                result.lineCount = (std::max)(result.lineCount,m_result.lineCount);
                result.cue = m_result.cue; result.observations = m_result.observations + 1;
                result.revised = !SubtitleBoxLookahead::SameLine(old,result.bounds,0);
            }
            else
            {
                result.cue = ++m_nextCue; result.observations = 1; result.revised = false;
                m_reference = preview.current; m_reference.text = result;
                m_inkReference = preview.current;
                m_expansionEvidence.clear();
            }
        }
        // Discovery remains active even when segmentation reports exactly the
        // old lines. A new word or second line outside the acquired glyph mask
        // must either be proved in current and next pixels or bypass this frame.
        if(result.detected && same && m_inkReference.ink && currentTemplatePixelsMatch) {
            SubtitleBoxRect novel;
            if(GatherNewInkExtension(*preview.current.ink,novel)) {
                SubtitleBoxRect extension;
                if(FindConfirmedInkExtension(preview,extension)) {
                    const auto old=result.bounds;
                    result.bounds={(std::min)(old.left,extension.left),(std::min)(old.top,extension.top),
                        (std::max)(old.right,extension.right),(std::max)(old.bottom,extension.bottom)};
                    result.revised=!SubtitleBoxLookahead::SameLine(old,result.bounds,0);
                    if(result.revised)result.held=false;
                    CaptureExtensionInk(preview.current,extension);
                } else {
                    // An unconfirmed scene candidate cannot revoke a complete
                    // current subtitle. Continue discovery on the next frame.
                }
            }
        }
        if (result.detected && !bridged && !templateHeld) result.held=false;
        if (!result.detected) {
            m_inkReference={};m_expansionEvidence.clear();
            m_bridgedFrames=0;
        }
        else if (!bridged && (!m_inkReference.ink ||
            m_inkReference.text.lineCount!=result.lineCount) &&
            preview.current.text.lineCount==result.lineCount && preview.current.ink &&
            SubtitleBoxLookahead::SameCue(m_reference,preview.current))
            m_inkReference=preview.current;
        m_previous = preview;
        // Keep only accepted cue evidence as the temporal reference. Rejected
        // transient companions must not poison the next frame's comparison.
        m_previous.current.text = result;
        m_result = result; m_hasPrevious = true;
        m_barTrackingReference = SubtitleBoxLookahead::AdvanceBarTrackingReference(preview.current);
        return result;
    }
private:
    bool GatherNewInkExtension(const SubtitleInkSnapshot& current,SubtitleBoxRect& extension) const
    {
        extension={};
        if(!m_inkReference.ink || !m_inkReference.text.detected)return false;
        auto reference=*m_inkReference.ink;
        // Already accepted extensions are part of discovery's reference, while
        // their own fixed masks are independently revalidated every frame.
        for(const auto& learned:m_expansionEvidence) {
            if(!learned.ink || learned.ink->rawInk.size()!=reference.rawInk.size() ||
                learned.ink->sourceRows!=reference.sourceRows)continue;
            for(size_t i=0;i<reference.rawInk.size();++i)reference.rawInk[i]|=learned.ink->rawInk[i];
        }
        for(int line=0;line<m_inkReference.text.lineCount && line<3;++line) {
            SubtitleBoxRect found;
            if(!SubtitleBoxLookahead::FindNewInkExtension(reference,current,
                m_inkReference.text.lineBounds[line],m_inkReference.width,m_inkReference.height,found) ||
                !found.Valid())continue;
            if(!extension.Valid())extension=found;
            else extension={(std::min)(extension.left,found.left),(std::min)(extension.top,found.top),
                (std::max)(extension.right,found.right),(std::max)(extension.bottom,found.bottom)};
        }
        return extension.Valid();
    }
    bool NoNewInkOutsideReference(const SubtitleBoxObservation& current) const
    {
        if(!m_inkReference.ink || !current.ink || !m_inkReference.text.detected)return false;
        SubtitleBoxRect extension;
        return !GatherNewInkExtension(*current.ink,extension);
    }
    struct ExpansionEvidence {
        std::shared_ptr<const SubtitleInkSnapshot> ink;
        SubtitleBoxRect bounds;
    };
    bool ExtensionInkComponentsMatch(const SubtitleInkSnapshot& expected,
        const SubtitleInkSnapshot& current,const SubtitleBoxRect& bounds) const
    {
        if(expected.width!=current.width || expected.height!=current.height ||
            expected.step<=0 || expected.step!=current.step ||
            expected.sourceRows.size()!=size_t(expected.height) ||
            current.sourceRows.size()!=size_t(current.height))return false;
        const int left=(std::max)(0,bounds.left/expected.step);
        const int right=(std::min)(expected.width,(bounds.right+expected.step-1)/expected.step);
        const auto first=std::lower_bound(expected.sourceRows.begin(),expected.sourceRows.end(),bounds.top);
        const auto last=std::lower_bound(expected.sourceRows.begin(),expected.sourceRows.end(),bounds.bottom);
        const int top=int(first-expected.sourceRows.begin());
        const int bottom=int(last-expected.sourceRows.begin());
        if(left>=right || top>=bottom || bottom>expected.height)return false;
        const int roiWidth=right-left,roiHeight=bottom-top;
        std::vector<uint8_t> visited(size_t(roiWidth)*roiHeight,0);
        std::vector<int> currentRows(size_t(roiHeight),-1),pending;
        for(int y=top;y<bottom;++y) {
            const int sourceY=expected.sourceRows[size_t(y)];
            const auto row=std::lower_bound(current.sourceRows.begin(),current.sourceRows.end(),sourceY);
            if(row!=current.sourceRows.end() && *row==sourceY)
                currentRows[size_t(y-top)]=int(row-current.sourceRows.begin());
        }
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
            if(!expected.Get(x,y))continue;
            const size_t start=size_t(y-top)*roiWidth+(x-left);
            if(visited[start])continue;
            visited[start]=1;pending.clear();pending.push_back(int(start));
            unsigned pixels=0,available=0,covered=0;
            for(size_t at=0;at<pending.size();++at) {
                const int index=pending[at],cx=left+index%roiWidth,cy=top+index/roiWidth;
                ++pixels;
                const int currentY=currentRows[size_t(cy-top)];
                if(currentY>=0) {
                    ++available;
                    covered+=current.Get(cx,currentY);
                }
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    if(dx==0 && dy==0)continue;
                    const int nx=cx+dx,ny=cy+dy;
                    if(nx<left || nx>=right || ny<top || ny>=bottom ||
                        !expected.Get(nx,ny))continue;
                    const size_t next=size_t(ny-top)*roiWidth+(nx-left);
                    if(!visited[next]) {visited[next]=1;pending.push_back(int(next));}
                }
            }
            // A removed glyph must not hide inside the global line coverage.
            // Tiny isolated fragments are ignored; accepted extension
            // components already contain at least four raw-ink pixels.
            if(pixels>=4 && available>=4 && covered*100<available*85)return false;
        }
        return true;
    }
    bool SameExtensionInk(const std::shared_ptr<const SubtitleInkSnapshot>& reference,
        const std::shared_ptr<const SubtitleInkSnapshot>& current,const SubtitleBoxRect& bounds,
        unsigned minimumCoverageBasisPoints=8500) const
    {
        if(!reference || !current || !bounds.Valid() || reference->width!=current->width ||
            reference->height!=current->height || reference->step!=current->step ||
            reference->step<=0 || reference->sourceRows.size()!=size_t(reference->height) ||
            current->sourceRows.size()!=size_t(current->height)) return false;
        const auto novel=BuildNovelExtensionInk(*current,bounds);
        if(!novel) return false;
        const int left=(std::max)(0,bounds.left/reference->step);
        const int right=(std::min)(reference->width,(bounds.right+reference->step-1)/reference->step);
        unsigned owned=0,covered=0,different=0,total=0;
        for(int y=0;y<reference->height;++y) {
            const int sourceY=reference->sourceRows[size_t(y)];
            if(sourceY<bounds.top || sourceY>=bounds.bottom) continue;
            const auto rowIt=std::lower_bound(current->sourceRows.begin(),current->sourceRows.end(),sourceY);
            if(rowIt==current->sourceRows.end() || *rowIt!=sourceY) continue;
            const int row=int(rowIt-current->sourceRows.begin());
            for(int x=left;x<right;++x) {
                const bool before=reference->Get(x,y),after=novel->Get(x,row);
                owned+=before;covered+=before&&after;different+=before!=after;total+=before||after;
            }
        }
        return owned>=6 && covered*10000>=owned*minimumCoverageBasisPoints && total>0 && different*100<=total*20 &&
            ExtensionInkComponentsMatch(*reference,*novel,bounds);
    }
    bool ExtensionInkIsCompatibleSubset(const std::shared_ptr<const SubtitleInkSnapshot>& expected,
        const std::shared_ptr<const SubtitleInkSnapshot>& current,const SubtitleBoxRect& bounds) const
    {
        if(!expected || !current || !m_inkReference.ink || !bounds.Valid() ||
            expected->width!=current->width || expected->height!=current->height ||
            expected->step!=current->step || expected->step<=0 ||
            expected->sourceRows.size()!=size_t(expected->height) ||
            current->sourceRows.size()!=size_t(current->height) ||
            m_inkReference.ink->sourceRows.size()!=size_t(expected->height))return false;
        const int left=(std::max)(0,bounds.left/current->step);
        const int right=(std::min)(current->width,(bounds.right+current->step-1)/current->step);
        for(int y=0;y<current->height;++y) {
            const int sourceY=current->sourceRows[size_t(y)];
            if(sourceY<bounds.top || sourceY>=bounds.bottom)continue;
            const auto baseRow=std::lower_bound(m_inkReference.ink->sourceRows.begin(),
                m_inkReference.ink->sourceRows.end(),sourceY);
            const auto expectedRow=std::lower_bound(expected->sourceRows.begin(),
                expected->sourceRows.end(),sourceY);
            if(baseRow==m_inkReference.ink->sourceRows.end() || *baseRow!=sourceY ||
                expectedRow==expected->sourceRows.end() || *expectedRow!=sourceY)continue;
            const int oldY=int(baseRow-m_inkReference.ink->sourceRows.begin());
            const int knownY=int(expectedRow-expected->sourceRows.begin());
            for(int x=left;x<right;++x) {
                const int sourceX=x*current->step;
                bool inAcquiredLine=false;
                for(int line=0;line<m_inkReference.text.lineCount && line<3;++line) {
                    const auto& lineBounds=m_inkReference.text.lineBounds[line];
                    if(lineBounds.Valid() && sourceX>=lineBounds.left && sourceX<lineBounds.right &&
                        sourceY>=lineBounds.top && sourceY<lineBounds.bottom) {
                        inAcquiredLine=true;break;
                    }
                }
                if(inAcquiredLine || m_inkReference.ink->Get(x,oldY) || !current->Get(x,y))continue;
                // A one-frame dropout may remove learned pixels, but it may
                // not introduce any glyph strokes outside the accepted mask.
                if(!expected->Get(x,knownY))return false;
            }
        }
        return true;
    }
    bool ExpansionStillVisible(const SubtitleBoxPreview& preview) const
    {
        for(const auto& evidence:m_expansionEvidence) {
            if(SameExtensionInk(evidence.ink,preview.current.ink,evidence.bounds))continue;
            // Permit a one-frame mask dropout when no conflicting replacement
            // strokes are present and the immediately following source frame
            // restores the acquired glyphs. Changed glyphs must take precedence.
            if(preview.current.ink &&
                !ExtensionInkIsCompatibleSubset(evidence.ink,preview.current.ink,evidence.bounds))return false;
            if(!SameExtensionInkInFollowing(preview,evidence))return false;
        }
        return true;
    }
    std::shared_ptr<SubtitleInkSnapshot> BuildNovelExtensionInk(
        const SubtitleInkSnapshot& source,const SubtitleBoxRect& bounds) const
    {
        if(source.width<=0 || source.height<=0 || source.step<=0) return {};
        const size_t words=(size_t(source.width)*source.height+63)/64;
        if(source.rawInk.size()!=words) return {};
        if(!m_inkReference.ink || !bounds.Valid() || source.width!=m_inkReference.ink->width ||
            source.height!=m_inkReference.ink->height || source.step!=m_inkReference.ink->step ||
            source.sourceRows.size()!=size_t(source.height) ||
            m_inkReference.ink->sourceRows.size()!=size_t(source.height)) return {};
        auto novel=std::make_shared<SubtitleInkSnapshot>(source);
        std::fill(novel->rawInk.begin(),novel->rawInk.end(),0);
        novel->ownedInk.assign(words,0);
        const int left=(std::max)(0,bounds.left/source.step);
        const int right=(std::min)(source.width,(bounds.right+source.step-1)/source.step);
        auto insideAcquiredLine=[&](int x,int sourceY) {
            const int sourceX=x*source.step;
            for(int line=0;line<m_inkReference.text.lineCount && line<3;++line) {
                const auto& lineBounds=m_inkReference.text.lineBounds[line];
                if(lineBounds.Valid() && sourceX>=lineBounds.left && sourceX<lineBounds.right &&
                    sourceY>=lineBounds.top && sourceY<lineBounds.bottom)return true;
            }
            return false;
        };
        unsigned count=0;
        for(int y=0;y<source.height;++y) {
            const int sourceY=source.sourceRows[size_t(y)];
            if(sourceY<bounds.top || sourceY>=bounds.bottom) continue;
            const auto oldRow=std::lower_bound(m_inkReference.ink->sourceRows.begin(),
                m_inkReference.ink->sourceRows.end(),sourceY);
            if(oldRow==m_inkReference.ink->sourceRows.end() || *oldRow!=sourceY) continue;
            const int oldY=int(oldRow-m_inkReference.ink->sourceRows.begin());
            for(int x=left;x<right;++x) if(source.Get(x,y) &&
                !m_inkReference.ink->Get(x,oldY) && !insideAcquiredLine(x,sourceY)) {
                const size_t bit=size_t(y)*source.width+x;
                novel->rawInk[bit/64]|=uint64_t{1}<<(bit%64);
                novel->ownedInk[bit/64]|=uint64_t{1}<<(bit%64);++count;
            }
        }
        return count>=6 ? novel : std::shared_ptr<SubtitleInkSnapshot>{};
    }
    bool SameExtensionInkInFollowing(const SubtitleBoxPreview& preview,
        const ExpansionEvidence& evidence) const
    {
        if(!preview.futureAvailable || !preview.followingCount) return false;
        const auto& next=preview.following[0];
        return next.analyzed && !next.discontinuity && !next.text.workLimit && next.ink &&
            next.identity.acceptedSequence==preview.current.identity.acceptedSequence+1 &&
            SubtitleBoxLookahead::SameContext(m_reference,next) &&
            SameExtensionInk(evidence.ink,next.ink,evidence.bounds);
    }
    void CaptureExtensionInk(const SubtitleBoxObservation& source,const SubtitleBoxRect& bounds)
    {
        if(!source.ink)return;
        auto captured=BuildNovelExtensionInk(*source.ink,bounds);
        if(!captured)return;
        for(auto& evidence:m_expansionEvidence) {
            if(!evidence.ink || evidence.ink->width!=captured->width ||
                evidence.ink->height!=captured->height || evidence.ink->step!=captured->step ||
                evidence.ink->sourceRows!=captured->sourceRows ||
                evidence.ink->rawInk.size()!=captured->rawInk.size() ||
                evidence.ink->ownedInk.size()!=captured->ownedInk.size())continue;
            for(size_t i=0;i<captured->rawInk.size();++i) {
                captured->rawInk[i]|=evidence.ink->rawInk[i];
                captured->ownedInk[i]|=evidence.ink->ownedInk[i];
            }
            const SubtitleBoxRect accumulatedBounds{
                (std::min)(evidence.bounds.left,bounds.left),
                (std::min)(evidence.bounds.top,bounds.top),
                (std::max)(evidence.bounds.right,bounds.right),
                (std::max)(evidence.bounds.bottom,bounds.bottom)};
            evidence={std::move(captured),accumulatedBounds};
            return;
        }
        m_expansionEvidence.push_back({std::move(captured),bounds});
    }
    bool FindConfirmedInkExtension(const SubtitleBoxPreview& preview,
        SubtitleBoxRect& extension) const
    {
        extension={};
        if(!m_inkReference.ink || !preview.futureAvailable || preview.followingCount==0 ||
            !preview.current.ink) return false;
        SubtitleBoxRect now;
        if(!GatherNewInkExtension(*preview.current.ink,now)) return false;
        const auto& next=preview.following[0];
        if(!next.analyzed || next.discontinuity || next.text.workLimit || !next.ink ||
            next.identity.acceptedSequence!=preview.current.identity.acceptedSequence+1 ||
            !SubtitleBoxLookahead::SameContext(m_reference,next) ||
            !SubtitleBoxLookahead::SameInkAtReference(m_inkReference,next)) return false;
        SubtitleBoxRect later;
        if(!GatherNewInkExtension(*next.ink,later)) return false;
        const int tolerance=(std::max)(4,preview.current.height/180);
        if(std::abs(now.left-later.left)>tolerance || std::abs(now.top-later.top)>tolerance ||
            std::abs(now.right-later.right)>tolerance || std::abs(now.bottom-later.bottom)>tolerance)
            return false;
        auto candidate=BuildNovelExtensionInk(*preview.current.ink,now);
        if(!candidate || !SameExtensionInk(candidate,next.ink,now)) return false;
        // A new vertical row can be a bright scene edge above or below a
        // correctly measured subtitle. Require the entire configured
        // lookahead horizon and fixed-pixel agreement before letting it grow
        // the presentation envelope. Horizontal word extensions retain the
        // two-frame fast path because they stay on an acquired baseline.
        const bool addsRow=now.top<m_inkReference.text.bounds.top ||
            now.bottom>m_inkReference.text.bounds.bottom;
        if(addsRow) {
            if(preview.followingCount<3)return false;
            SubtitleBoxRect expected=now;
            for(unsigned i=0;i<3;++i) {
                const auto& observed=preview.following[i];
                if(!observed.analyzed || observed.discontinuity || observed.text.workLimit || !observed.ink ||
                    observed.identity.acceptedSequence!=preview.current.identity.acceptedSequence+i+1 ||
                    !SubtitleBoxLookahead::SameContext(m_reference,observed))return false;
                for(int line=0;line<m_inkReference.text.lineCount && line<3;++line)
                    if(!SubtitleBoxLookahead::OwnedLinePixelsVisible(m_inkReference,line,observed))return false;
                SubtitleBoxRect laterRow;
                if(!GatherNewInkExtension(*observed.ink,laterRow) ||
                    std::abs(expected.left-laterRow.left)>2 || std::abs(expected.top-laterRow.top)>2 ||
                    std::abs(expected.right-laterRow.right)>2 || std::abs(expected.bottom-laterRow.bottom)>2 ||
                    !SameExtensionInk(candidate,observed.ink,now,9750))return false;
            }
        }
        extension=now;
        return true;
    }
    bool CanBridge(const SubtitleBoxPreview& preview) const
    {
        if (!m_hasPrevious || !m_result.detected || m_bridgedFrames>=2 ||
            !preview.futureAvailable || preview.current.discontinuity ||
            !ExpansionStillVisible(preview) ||
            (m_expansionEvidence.empty() && !NoNewInkOutsideReference(preview.current)) ||
            m_previous.policyGeneration!=preview.policyGeneration ||
            m_previous.continuityGeneration!=preview.continuityGeneration ||
            preview.current.identity.acceptedSequence!=m_previous.current.identity.acceptedSequence+1 ||
            m_inkReference.text.lineCount!=m_result.lineCount ||
            !SubtitleBoxLookahead::SameContext(m_reference,preview.current) ||
            !SubtitleBoxLookahead::SameInkAtReference(m_inkReference,preview.current)) return false;
        const auto* prior=&preview.current;
        const unsigned available=(std::min)(preview.followingCount,2-m_bridgedFrames);
        for (unsigned i=0;i<available;++i)
        {
            const auto& next=preview.following[i];
            if (!next.analyzed || next.discontinuity || next.text.workLimit ||
                next.identity.acceptedSequence!=prior->identity.acceptedSequence+1 ||
                !SubtitleBoxLookahead::SameContext(m_reference,next) ||
                !SubtitleBoxLookahead::SameInkAtReference(m_inkReference,next) ||
                !ExpansionInkMatches(next.ink)) return false;
            if (next.text.lineCount==m_result.lineCount &&
                SubtitleBoxLookahead::SameCue(m_reference,next) &&
                SubtitleBoxLookahead::ContainsCurrentInk(m_result.bounds,next.text)) return true;
            prior=&next;
        }
        return false;
    }
    bool ExpansionInkMatches(const std::shared_ptr<const SubtitleInkSnapshot>& current) const
    {
        for(const auto& evidence:m_expansionEvidence)
            if(!SameExtensionInk(evidence.ink,current,evidence.bounds))return false;
        return true;
    }
    SubtitleBoxPreview m_previous;
    SubtitleBoxObservation m_reference, m_inkReference;
    std::vector<ExpansionEvidence> m_expansionEvidence;
    unsigned m_bridgedFrames = 0;
    SubtitleBoxResult m_result;
    SubtitleBarTrackingReference m_barTrackingReference;
    uint64_t m_nextCue = 0;
    bool m_hasPrevious = false;
};
