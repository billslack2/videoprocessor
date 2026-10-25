#pragma once
#include <AnalysisLumaSource.h>
#include <SubtitleBoxDetector.h>
#include <SubtitleCueSidePlane.h>
#include <algorithm>
#include <array>
#include <cstdlib>

// Subtitle-only current-pixel evidence, never permission to crop or hide pixels.
// Each edge is proved independently; caption strokes may occupy a sparse part of
// a flat bar, but a wide connected picture region must not become a subtitle ROI.
struct SubtitleBarEvidence
{
    bool available = false;
    bool captionBackedTop=false, captionBackedBottom=false;
    int top = 0, bottom = 0, topDepth = 0, bottomDepth = 0;
    int topFloor = 0, bottomFloor = 0, topSupport = 0, bottomSupport = 0;
    int topBoundarySupport = 0, bottomBoundarySupport = 0;
    // Failed current run clusters are coordinates only. They grant neither
    // measured-bar nor AR authority; subtitle recovery must independently
    // prove current glyph coverage and an edge outside the caption footprint.
    int topCuePlaneCandidate=0,bottomCuePlaneCandidate=0;
    int topMaxIntrusion = 0, bottomMaxIntrusion = 0;
    double topBlackFraction = 0.0, bottomBlackFraction = 0.0;
    size_t samples = 0;
    bool topRecoveredAtShallowerEdge = false, bottomRecoveredAtShallowerEdge = false;
    bool topDistributedBoundary = false, bottomDistributedBoundary = false;
    // Reference-only proof is deliberately separate from acquisition authority.
    // The caller owns the lifetime and identity of a previously strong plane.
    bool topRevalidatedAtReference = false, bottomRevalidatedAtReference = false;
    int referenceTop = 0, referenceBottom = 0;
    // A small retained-crop rounding offset can hide the physical edge from
    // the exact-plane probe. Nearby proof reports its OWN sampled plane; it
    // never certifies the retained crop or independent AR acquisition.
    bool topRevalidatedNearby = false, bottomRevalidatedNearby = false;
    int topReferenceDelta = 0, bottomReferenceDelta = 0;
    unsigned nearbyProbes = 0;
    int topReferenceBoundarySupport = 0, bottomReferenceBoundarySupport = 0;
    int topReferenceMaxIntrusion = 0, bottomReferenceMaxIntrusion = 0;
    double topReferenceBlackFraction = 0.0, bottomReferenceBlackFraction = 0.0;
    const char* reason = "invalid-source";
};

// Current complete caption ownership excludes its entire width from boundary
// voting, including black backing and antialiased capture fringes. It does not
// remove a single sample from the bar-body occupancy proof.
inline CurrentSubtitleCueSidePlaneProof ProveCurrentSubtitleCaptionPlane(
    const AnalysisLumaSource& source,const SubtitleBoxResult& text,
    const SubtitleInkSnapshot* ink,int plane,bool fromTopBar,size_t maxSamples)
{
    CurrentSubtitleCueSidePlaneProof rejected;rejected.plane=plane;rejected.fromTopBar=fromTopBar;
    rejected.reason="private-plane-current-caption-incomplete";
    if(!text.detected || text.held || text.confirmationPending || text.workLimit ||
        text.lineCount<1 || text.lineCount>3 || !text.configuredSearchComplete ||
        !(fromTopBar?text.topSearchComplete:text.bottomSearchComplete) || !ink ||
        !ink->paletteValid || !ink->rawEvidenceComplete ||
        !text.capturePanelMeasured || !text.capturePanel.Valid() || !text.sourcePanel.Valid())return rejected;
    SubtitleCuePlaneBoundaryExclusion exclusion{source.width,0};
    const auto include=[&](const SubtitleBoxRect& r) {
        if(!r.Valid() || r.left<0 || r.top<0 || r.right>source.width || r.bottom>source.height)return false;
        exclusion.left=(std::min)(exclusion.left,r.left);exclusion.right=(std::max)(exclusion.right,r.right);return true;
    };
    rejected.reason="private-plane-caption-outside-raster";
    if(!include(text.sourcePanel) || !include(text.capturePanel))return rejected;
    bool hasCueBarInk=false;
    for(int line=0;line<text.lineCount;++line) {
        if(!include(text.lineBounds[line]))return rejected;
        hasCueBarInk|=fromTopBar?text.lineBounds[line].top<plane:text.lineBounds[line].bottom>plane;
        if(text.linePanels[line].Valid() && !include(text.linePanels[line]))return rejected;
        if(text.lineCapturePanels[line].Valid() && !include(text.lineCapturePanels[line]))return rejected;
    }
    if(!hasCueBarInk){rejected.reason="private-plane-no-current-bar-ink";return rejected;}
    return ProveCurrentSubtitleCueSidePlane(source,plane,fromTopBar,maxSamples,&exclusion);
}
inline SubtitleBarEvidence ExtractSubtitleBarEvidence(const AnalysisLumaSource& source)
{
    SubtitleBarEvidence result;
    if (!source.IsValid() || source.width < 64 || source.height < 48 ||
        source.width > 8192 || source.height > 4320) return result;
    result.bottom=source.height;
    enum { Columns = 128 };
    const int step = (std::max)(1,source.height/540);
    const int limit = source.height/3;
    const int tolerance = (std::max)(2,step*2);
    std::array<int,Columns> xs{};
    for (int i=0;i<Columns;++i)
        xs[i]=(source.width*(4*Columns+96*(2*i+1)))/(200*Columns);
    auto sample = [&](int x,int y,AnalysisLumaSample& value) {
        ++result.samples;return source.Sample(x,y,value);
    };
    auto neutral = [](const AnalysisLumaSample& value) {
        return std::abs(int(value.chromaU)-512)<=32 && std::abs(int(value.chromaV)-512)<=32;
    };
    auto inspect = [&](bool bottom,int& floor,int& depth,int& support,
                       double& fraction,int& boundarySupport,int& maxIntrusion,bool distributed=false) {
        auto row=[&](int d){return bottom?source.height-1-d:d;};
        std::array<int,Columns*3> levels{};
        int n=0,neutralSamples=0;
        for(int d:{0,step,2*step}) for(int x:xs) {
            AnalysisLumaSample value;
            if(!sample(x,row(d),value))return false;
            levels[n++]=value.luma;neutralSamples+=neutral(value);
        }
        std::nth_element(levels.begin(),levels.begin()+n/2,levels.end());floor=levels[n/2];
        if(floor>96 || neutralSamples*100<n*95)return false;
        auto black=[&](const AnalysisLumaSample& value) {
            return std::abs(int(value.luma)-floor)<=12 && neutral(value);
        };
        std::array<int,Columns> runs{};
        for(int i=0;i<Columns;++i) {
            int first=limit;
            for(int d=0;d<limit;d+=step) {
                AnalysisLumaSample value;
                if(!sample(xs[i],row(d),value))return false;
                if(!black(value)) {
                    first=d;
                    // Refine only the sampled transition, preserving source rows.
                    for(int refine=(std::max)(0,d-step+1);refine<d;++refine) {
                        if(!sample(xs[i],row(refine),value))return false;
                        if(!black(value)) {first=refine;break;}
                    }
                    break;
                }
            }
            runs[i]=first;
        }
        // A centered opaque subtitle panel continues the black bar into the
        // picture. Its top edge may own most column runs, so the all-column
        // median would mistake the panel for the picture/bar boundary. Use
        // matching runs at both outside flanks to propose the plane instead.
        // The ordinary path requires both flanks. A failed edge may try the
        // stricter distributed proof below; an interior edge alone is insufficient.
        constexpr int FlankColumns=Columns/8;
        std::array<int,2*FlankColumns> flankRuns{};
        for(int i=0;i<FlankColumns;++i) {
            flankRuns[i]=runs[i];
            flankRuns[FlankColumns+i]=runs[Columns-FlankColumns+i];
        }
        std::nth_element(flankRuns.begin(),flankRuns.begin()+FlankColumns,flankRuns.end());
        depth=flankRuns[FlankColumns];
        if(distributed) {
            // Nominate the densest native run cluster, not a mirrored edge or
            // a subtitle rectangle. Only a single bounded alternative is tried.
            // Dark picture at an outer flank can otherwise move its median far
            // inside the image even when many other columns expose the edge.
            auto ordered=runs;
            std::sort(ordered.begin(),ordered.end());
            int bestBegin=0,bestCount=0;
            for(int begin=0,end=0;begin<Columns;++begin) {
                if(ordered[begin]<2*step+1 || ordered[begin]>=limit)continue;
                end=(std::max)(end,begin);
                while(end<Columns && ordered[end]<limit &&
                    ordered[end]-ordered[begin]<=tolerance)++end;
                if(end-begin>bestCount) {bestBegin=begin;bestCount=end-begin;}
            }
            if(bestCount<Columns/4)return false;
            depth=ordered[bestBegin+bestCount/2];
            if(bottom)result.bottomCuePlaneCandidate=source.height-depth;
            else result.topCuePlaneCandidate=depth;
        }
        support=boundarySupport=maxIntrusion=0;fraction=0.0;
        int leftSupport=0,rightSupport=0;
        for(int i=0;i<Columns;++i) {
            if(std::abs(runs[i]-depth)>tolerance)continue;
            ++support;
            if(i<FlankColumns)++leftSupport;
            if(i>=Columns-FlankColumns)++rightSupport;
        }
        if(depth<2*step+1 || depth>=limit || support<(distributed?Columns/4:(Columns+4)/5) ||
            (!distributed && (leftSupport<FlankColumns*3/4 ||
                rightSupport<FlankColumns*3/4)))return false;

        // A threshold crossing alone is not an encoded edge: smooth dark scene
        // gradients also cross the black threshold. Require abrupt native-row
        // contrast at distributed columns close to this frame's proposed plane.
        unsigned boundaryZones=0;
        int leftBoundarySupport=0,rightBoundarySupport=0;
        std::array<int,4> zoneSupport{};
        int firstBoundary=Columns,lastBoundary=-1;
        for(int i=0;i<Columns;++i) {
            if(std::abs(runs[i]-depth)>tolerance)continue;
            // Resampling can spread a sharp encoded edge over several rows.
            // Compare its strongest local rise with slopes outside that short
            // transition, rather than requiring a single unfiltered pixel jump.
            std::array<int,9> values{};
            for(int k=0;k<9;++k) {
                AnalysisLumaSample value;
                if(!sample(xs[i],row((std::max)(0,runs[i]-4+k)),value))return false;
                values[k]=value.luma;
            }
            int jump=0;
            for(int k=2;k<7;++k)jump=(std::max)(jump,values[k]-values[k-1]);
            const int neighboringRise=(std::max)({0,values[1]-values[0],values[8]-values[7]});
            if(jump>=8 && jump>neighboringRise*3/2+2) {
                ++boundarySupport;boundaryZones|=1u<<(i/32);
                ++zoneSupport[i/32];firstBoundary=(std::min)(firstBoundary,i);
                lastBoundary=i;
                if(i<FlankColumns)++leftBoundarySupport;
                if(i>=Columns-FlankColumns)++rightBoundarySupport;
            }
        }
        int zones=0;for(unsigned bits=boundaryZones;bits;bits>>=1)zones+=bits&1;
        if(distributed) {
            // Demand more independent sharp transitions than the normal path,
            // spread across at least a third of the raster and two populated
            // zones. A compact light/sign/card cannot establish a new plane.
            int populatedZones=0;
            for(int count:zoneSupport)populatedZones+=count>=4;
            if(boundarySupport<16 || populatedZones<2 ||
                lastBoundary-firstBoundary<Columns/3)return false;
        } else if(boundarySupport<8 || zones<2 ||
            leftBoundarySupport<4 || rightBoundarySupport<4)return false;
        const double requiredBlackFraction=distributed?0.97:0.85;
        size_t blackCount=0,total=0;
        for(int d=0;d<depth;d+=step) {
            int intrusion=0;
            for(int x:xs) {
                AnalysisLumaSample value;
                if(!sample(x,row(d),value))return false;
                const bool isBlack=black(value);
                ++total;blackCount+=isBlack;
                intrusion=isBlack?0:intrusion+1;
                maxIntrusion=(std::max)(maxIntrusion,intrusion);
            }
        }
        fraction=total?double(blackCount)/total:0.0;
        // A dark scene may shift the median run one or two rows into the
        // picture. If that alone trips the long-intrusion veto, recheck the
        // immediately shallower current-pixel planes. Never move deeper into
        // the picture, and keep the distributed transition evidence above.
        if (fraction>=requiredBlackFraction && maxIntrusion>=Columns/5)
        {
            const int medianDepth=depth;
            for (int offset=1;offset<=2 && maxIntrusion>=Columns/5;++offset)
            {
                const int candidate=medianDepth-offset;
                if(candidate<2*step+1) continue;
                int candidateSupport=0;
                for(int run:runs) candidateSupport+=std::abs(run-candidate)<=tolerance;
                if(candidateSupport*5<Columns) continue;
                size_t candidateBlack=0,candidateTotal=0;
                int candidateMaxIntrusion=0;
                for(int d=0;d<candidate;d+=step) {
                    int candidateIntrusion=0;
                    for(int x:xs) {
                        AnalysisLumaSample value;
                        if(!sample(x,row(d),value))return false;
                        const bool isBlack=black(value);
                        ++candidateTotal;candidateBlack+=isBlack;
                        candidateIntrusion=isBlack?0:candidateIntrusion+1;
                        candidateMaxIntrusion=(std::max)(candidateMaxIntrusion,candidateIntrusion);
                    }
                }
                const double candidateFraction=candidateTotal?
                    double(candidateBlack)/candidateTotal:0.0;
                if(candidateFraction>=requiredBlackFraction && candidateMaxIntrusion<Columns/5) {
                    depth=candidate;support=candidateSupport;
                    maxIntrusion=candidateMaxIntrusion;fraction=candidateFraction;
                    break;
                }
            }
            if(depth<medianDepth) {
                if(bottom) result.bottomRecoveredAtShallowerEdge=true;
                else result.topRecoveredAtShallowerEdge=true;
            }
        }
        // Glyph strokes have gaps; a contiguous intrusion spanning a fifth of
        // the raster is picture content even when the entire band is 85% black.
        return fraction>=requiredBlackFraction && maxIntrusion<Columns/5;
    };
    bool top=inspect(false,result.topFloor,result.topDepth,result.topSupport,
        result.topBlackFraction,result.topBoundarySupport,result.topMaxIntrusion);
    bool bottom=inspect(true,result.bottomFloor,result.bottomDepth,result.bottomSupport,
        result.bottomBlackFraction,result.bottomBoundarySupport,result.bottomMaxIntrusion);
    if(!top || !bottom) {
        auto fallback=result;
        const bool distributedTop=top || inspect(false,fallback.topFloor,fallback.topDepth,
            fallback.topSupport,fallback.topBlackFraction,fallback.topBoundarySupport,
            fallback.topMaxIntrusion,true);
        const bool distributedBottom=bottom || inspect(true,fallback.bottomFloor,fallback.bottomDepth,
            fallback.bottomSupport,fallback.bottomBlackFraction,fallback.bottomBoundarySupport,
            fallback.bottomMaxIntrusion,true);
        // A fallback edge is usable only with a separately measured opposing
        // plane. Symmetry is a veto, never a source of missing pixel evidence.
        // Keep ordinary asymmetric/single-edge behavior exactly as before.
        if(distributedTop && distributedBottom &&
            std::abs(fallback.topDepth-fallback.bottomDepth)<=tolerance &&
            fallback.topFloor<=80 && fallback.bottomFloor<=80 &&
            std::abs(fallback.topFloor-fallback.bottomFloor)<=12) {
            fallback.topDistributedBoundary=!top;
            fallback.bottomDistributedBoundary=!bottom;
            fallback.samples=result.samples;
            fallback.topRecoveredAtShallowerEdge=result.topRecoveredAtShallowerEdge;
            fallback.bottomRecoveredAtShallowerEdge=result.bottomRecoveredAtShallowerEdge;
            result=fallback;top=bottom=true;
        }
    }
    if(top)result.top=result.topDepth;
    if(bottom)result.bottom=source.height-result.bottomDepth;
    result.available=top||bottom;
    result.reason=top?(bottom?"current-flat-independent-bars":"current-flat-top-bar"):
        (bottom?"current-flat-bottom-bar":"no-current-flat-bar");
    if(result.topDistributedBoundary || result.bottomDistributedBoundary)
        result.reason="current-distributed-independent-bars";
    return result;
}

// Revisit a previously acquired plane when dark picture pixels obscure the
// first non-black run. This never creates ordinary acquisition authority: the
// caller must supply a valid same-context reference and separately authorize
// cue continuation. All evidence below comes from the current source pixels.
inline SubtitleBarEvidence RevalidateSubtitleBarEvidence(const AnalysisLumaSource& source,
    const SubtitleBarEvidence& current, int referenceTop, int referenceBottom,
    const SubtitleInkSnapshot* candidateInk = nullptr,
    const SubtitleBoxRect* candidateAnchor = nullptr)
{
    auto result=current;
    result.topRevalidatedAtReference=result.bottomRevalidatedAtReference=false;
    result.referenceTop=referenceTop;result.referenceBottom=referenceBottom;
    result.topReferenceBoundarySupport=result.bottomReferenceBoundarySupport=0;
    result.topReferenceMaxIntrusion=result.bottomReferenceMaxIntrusion=0;
    result.topReferenceBlackFraction=result.bottomReferenceBlackFraction=0.0;
    if(!source.IsValid() || source.width<64 || source.height<48 ||
        source.width>8192 || source.height>4320 || referenceTop<0 ||
        referenceBottom>source.height || referenceTop>=referenceBottom) return result;
    const int step=(std::max)(1,source.height/540);
    const int tolerance=(std::max)(2,step*2);
    // A provisional subtitle scan may identify the exact glyph strokes that
    // cross a previously proved bar plane. Ignore only detector-owned pixels
    // inside that crossing line; never mask its rectangular envelope.
    const int inkStep=SubtitleBoxDetector::SamplingStep(source.width,source.height);
    const bool maskCandidate=candidateInk && candidateAnchor && candidateAnchor->Valid() &&
        candidateInk->step==inkStep && candidateInk->width>0 && candidateInk->height>0 &&
        candidateInk->sourceRows.size()==size_t(candidateInk->height) &&
        candidateInk->ownedInk.size()==candidateInk->rawInk.size() && !candidateInk->rawInk.empty() &&
        candidateAnchor->left>=0 && candidateAnchor->right<=source.width &&
        candidateAnchor->top>=0 && candidateAnchor->bottom<=source.height &&
        ((referenceTop>0 && candidateAnchor->top<referenceTop && candidateAnchor->bottom>=referenceTop) ||
         (referenceBottom<source.height && candidateAnchor->top<referenceBottom &&
            candidateAnchor->bottom>=referenceBottom));
    auto ownedAt=[&](int x,int y) {
        if(!maskCandidate || x<candidateAnchor->left || x>=candidateAnchor->right ||
            y<candidateAnchor->top || y>=candidateAnchor->bottom) return false;
        const int sx=x/inkStep;
        const auto it=std::lower_bound(candidateInk->sourceRows.begin(),candidateInk->sourceRows.end(),y);
        int sy=it==candidateInk->sourceRows.end()?candidateInk->height-1:
            int(it-candidateInk->sourceRows.begin());
        if(sy>0 && sy<candidateInk->height &&
            std::abs(candidateInk->sourceRows[size_t(sy-1)]-y)<
            std::abs(candidateInk->sourceRows[size_t(sy)]-y)) --sy;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if(candidateInk->Get(sx+dx,sy+dy,true)) return true;
        return false;
    };
    // A freshly acquired different edge outranks history. In particular a
    // smaller picture must not keep a plausible-looking old interior bar plane.
    if((current.top>0 && (referenceTop==0 || std::abs(current.top-referenceTop)>tolerance)) ||
        (current.bottom<source.height && (referenceBottom==source.height ||
            std::abs(current.bottom-referenceBottom)>tolerance))) return result;
    enum { Columns=128 };
    std::array<int,Columns> xs{};
    for(int i=0;i<Columns;++i)
        xs[i]=(source.width*(4*Columns+96*(2*i+1)))/(200*Columns);
    auto sample=[&](int x,int y,AnalysisLumaSample& value) {
        ++result.samples;return source.Sample(x,y,value);
    };
    auto neutral=[](const AnalysisLumaSample& value) {
        return std::abs(int(value.chromaU)-512)<=32 && std::abs(int(value.chromaV)-512)<=32;
    };
    auto inspect=[&](bool bottom,int depth,int& boundarySupport,double& fraction,int& maxIntrusion) {
        if(depth<2*step+1 || depth>=source.height/3) return false;
        auto row=[&](int d){return bottom?source.height-1-d:d;};
        std::array<int,Columns*3> levels{};
        int n=0,neutralSamples=0;
        for(int d:{0,step,2*step})for(int x:xs) {
            AnalysisLumaSample value;if(!sample(x,row(d),value))return false;
            levels[n++]=value.luma;neutralSamples+=neutral(value);
        }
        std::nth_element(levels.begin(),levels.begin()+n/2,levels.end());
        const int floor=levels[n/2];
        if(floor>96 || neutralSamples*100<n*95)return false;
        auto black=[&](const AnalysisLumaSample& value) {
            return std::abs(int(value.luma)-floor)<=12 && neutral(value);
        };
        // Inspect this plane, not the endpoints of longer runs inside a dark
        // picture. A few distributed visible portions can prove an old edge.
        unsigned boundaryZones=0;
        for(int i=0;i<Columns;++i) {
            bool masked=false;
            std::array<int,9> values{};
            for(int k=0;k<9;++k) {
                AnalysisLumaSample value;
                const int y=row((std::max)(0,depth-4+k));
                if(!sample(xs[i],y,value))return false;
                values[k]=value.luma;
                masked |= ownedAt(xs[i],y);
            }
            if(masked) continue;
            int jump=0;
            for(int k=2;k<7;++k)jump=(std::max)(jump,values[k]-values[k-1]);
            const int neighboringRise=(std::max)({0,values[1]-values[0],values[8]-values[7]});
            if(jump>=8 && jump>neighboringRise*3/2+2) {
                ++boundarySupport;boundaryZones|=1u<<(i/32);
            }
        }
        int zones=0;for(unsigned bits=boundaryZones;bits;bits>>=1)zones+=bits&1;
        if(boundarySupport<8 || zones<2)return false;
        size_t blackCount=0,total=0,maskedCount=0;
        int maxMaskedRun=0;
        // Include the last native bar row even when it is off the regular
        // sampling phase: a one-row connected intrusion is still picture data.
        for(int d=0;d<depth;d=(d+step>=depth && d!=depth-1)?depth-1:d+step) {
            int intrusion=0,maskedRun=0;
            for(int x:xs) {
                const int y=row(d);
                AnalysisLumaSample value;if(!sample(x,y,value))return false;
                if(ownedAt(x,y)) {
                    ++maskedCount;++maskedRun;
                    maxMaskedRun=(std::max)(maxMaskedRun,maskedRun);intrusion=0;continue;
                }
                maskedRun=0;
                const bool isBlack=black(value);++total;blackCount+=isBlack;
                intrusion=isBlack?0:intrusion+1;
                maxIntrusion=(std::max)(maxIntrusion,intrusion);
            }
        }
        fraction=total?double(blackCount)/total:0.0;
        if(maskedCount*100>(total+maskedCount)*12 || maxMaskedRun>=Columns/5)return false;
        return fraction>=0.85 && maxIntrusion<Columns/5;
    };
    if(referenceTop>0) result.topRevalidatedAtReference=inspect(false,referenceTop,
        result.topReferenceBoundarySupport,result.topReferenceBlackFraction,result.topReferenceMaxIntrusion);
    if(referenceBottom<source.height) result.bottomRevalidatedAtReference=inspect(true,source.height-referenceBottom,
        result.bottomReferenceBoundarySupport,result.bottomReferenceBlackFraction,result.bottomReferenceMaxIntrusion);
    return result;
}

// Select subtitle analysis planes from this frame's pixels. A retained Classic
// picture supplies candidate planes, not pixel authority: a freshly proved
// different edge wins on its own side. The other side remains independently
// usable even if its counterpart cannot be proved. This never changes the
// presentation crop or grants active-picture authority.
inline SubtitleBarEvidence ResolveCurrentSubtitleAnalysisBars(const AnalysisLumaSource& source,
    const SubtitleBarEvidence& current, int referenceTop, int referenceBottom,
    const SubtitleInkSnapshot* candidateInk = nullptr,
    const SubtitleBoxRect* candidateAnchor = nullptr, bool allowNearby = true)
{
    SubtitleBarEvidence result=current;
    if(!source.IsValid() || referenceTop<0 || referenceBottom>source.height ||
        referenceTop>=referenceBottom) {
        result.available=false;result.top=0;result.bottom=source.height;
        result.reason="invalid-subtitle-analysis-bars";
        return result;
    }
    const bool currentTop=current.available && current.top>0 && current.top<source.height;
    const bool currentBottom=current.available && current.bottom>0 && current.bottom<source.height;
    result.top=currentTop?current.top:0;
    result.bottom=currentBottom?current.bottom:source.height;
    // Revalidate each missing side in isolation. RevalidateSubtitleBarEvidence
    // rejects a changed current edge globally, which is correct for retaining
    // an entire old aperture but must not discard the valid opposite side.
    if(!currentTop && referenceTop>0) {
        SubtitleBarEvidence neutral;neutral.bottom=source.height;
        const auto proved=RevalidateSubtitleBarEvidence(source,neutral,referenceTop,
            source.height,candidateInk,candidateAnchor);
        result.samples+=proved.samples;
        result.topReferenceBoundarySupport=proved.topReferenceBoundarySupport;
        result.topReferenceBlackFraction=proved.topReferenceBlackFraction;
        result.topReferenceMaxIntrusion=proved.topReferenceMaxIntrusion;
        if(proved.topRevalidatedAtReference) {
            result.top=referenceTop;result.topRevalidatedAtReference=true;
        }
    }
    if(!currentBottom && referenceBottom<source.height) {
        SubtitleBarEvidence neutral;neutral.bottom=source.height;
        const auto proved=RevalidateSubtitleBarEvidence(source,neutral,0,
            referenceBottom,candidateInk,candidateAnchor);
        result.samples+=proved.samples;
        result.bottomReferenceBoundarySupport=proved.bottomReferenceBoundarySupport;
        result.bottomReferenceBlackFraction=proved.bottomReferenceBlackFraction;
        result.bottomReferenceMaxIntrusion=proved.bottomReferenceMaxIntrusion;
        if(proved.bottomRevalidatedAtReference) {
            result.bottom=referenceBottom;result.bottomRevalidatedAtReference=true;
        }
    }
    // Retained AR estimates can differ from the currently visible encoded
    // edge by a few rows. On an unproved side, try at
    // most four nearby planes, shallowest bar first. Every candidate must
    // pass the unchanged current-pixel sharp-edge and full-bar proof. This
    // is a subtitle analysis aperture only: neither history nor symmetry is
    // allowed to supply pixels or override an independently measured edge.
    const int step=(std::max)(1,source.height/540);
    auto nearby=[&](bool bottom) {
        const int reference=bottom?referenceBottom:referenceTop;
        for(int depthOffset=-2*step;depthOffset<=2*step;depthOffset+=step) {
            if(depthOffset==0)continue; // Exact plane was already checked.
            const int plane=reference+(bottom?-depthOffset:depthOffset);
            if(plane<=0 || plane>=source.height ||
                (bottom?plane<=result.top:plane>=result.bottom))continue;
            SubtitleBarEvidence neutral;neutral.bottom=source.height;
            ++result.nearbyProbes;
            const auto proved=RevalidateSubtitleBarEvidence(source,neutral,
                bottom?0:plane,bottom?plane:source.height,candidateInk,candidateAnchor);
            result.samples+=proved.samples;
            if(bottom?proved.bottomRevalidatedAtReference:proved.topRevalidatedAtReference) {
                if(bottom) {
                    result.bottom=plane;result.bottomRevalidatedNearby=true;
                    result.bottomReferenceDelta=plane-reference;
                    result.bottomReferenceBoundarySupport=proved.bottomReferenceBoundarySupport;
                    result.bottomReferenceBlackFraction=proved.bottomReferenceBlackFraction;
                    result.bottomReferenceMaxIntrusion=proved.bottomReferenceMaxIntrusion;
                } else {
                    result.top=plane;result.topRevalidatedNearby=true;
                    result.topReferenceDelta=plane-reference;
                    result.topReferenceBoundarySupport=proved.topReferenceBoundarySupport;
                    result.topReferenceBlackFraction=proved.topReferenceBlackFraction;
                    result.topReferenceMaxIntrusion=proved.topReferenceMaxIntrusion;
                }
                return;
            }
        }
    };
    if(allowNearby && !currentTop && result.top==0 && referenceTop>0)nearby(false);
    if(allowNearby && !currentBottom && result.bottom==source.height && referenceBottom<source.height)nearby(true);
    result.referenceTop=referenceTop;result.referenceBottom=referenceBottom;
    result.available=result.top>0 || result.bottom<source.height;
    if(result.top>=result.bottom) {
        result.available=false;result.top=0;result.bottom=source.height;
        result.reason="conflicting-subtitle-analysis-bars";
    } else if(result.topRevalidatedNearby || result.bottomRevalidatedNearby)
        result.reason="current-pixel-nearby-subtitle-bar";
    else if(result.topRevalidatedAtReference || result.bottomRevalidatedAtReference)
        result.reason="current-pixel-revalidated-shared-bar";
    else if(result.available)
        result.reason="current-independent-subtitle-bars";
    else result.reason="no-current-subtitle-bar-proof";
    return result;
}

// When black scenery hides the encoded boundary, a current recognized caption
// can prove a smaller black strip containing itself. This is subtitle-only
// ownership, never crop authority or a reusable physical bar reference.
inline bool ProveSubtitleCaptionStrip(const AnalysisLumaSource& source,
    const SubtitleBoxResult& text,const SubtitleInkSnapshot* ink,bool bottom,int floor,int& plane)
{
    if(!text.detected || text.workLimit || !text.bounds.Valid() || !ink || !ink->rawEvidenceComplete ||
        floor>96 || text.lineCount<1 || text.lineCount>3)return false;
    const auto box=text.bounds;
    const int step=(std::max)(1,source.height/540),guard=2*step;
    plane=bottom?(std::max)(0,box.top-guard):(std::min)(source.height,box.bottom+guard);
    if(bottom?plane<source.height*2/3:plane>source.height/3)return false;
    // Reject side labels and broadly connected picture components. Ordinary
    // component/glyph checks already ran in the production detector.
    if(box.right-box.left<source.width/12 || box.right-box.left>source.width*4/5 ||
        std::abs(box.left+box.right-source.width)>source.width/3)return false;
    const int first=bottom?plane:0,last=bottom?source.height:plane;
    unsigned total=0,black=0,bad=0;
    for(int y=first;y<last;y+=step) {
        int sy=int(std::lower_bound(ink->sourceRows.begin(),ink->sourceRows.end(),y)-ink->sourceRows.begin());
        if(sy>=ink->height)sy=ink->height-1;
        unsigned rowBad=0,run=0,maxRun=0;
        for(int col=0;col<128;++col) {
            const int x=source.width*(4*128+96*(2*col+1))/(200*128);
            AnalysisLumaSample v;if(!source.Sample(x,y,v))return false;
            const bool neutral=std::abs(int(v.chromaU)-512)<=32 && std::abs(int(v.chromaV)-512)<=32;
            const bool isBlack=neutral && std::abs(int(v.luma)-floor)<=12;
            bool owned=false;
            if(!isBlack && neutral && x>=box.left && x<box.right && y>=box.top && y<box.bottom)
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)owned|=ink->Get(x/ink->step+dx,sy+dy,true);
            ++total;black+=isBlack;
            const bool unexplained=!isBlack && !owned;
            bad+=unexplained;rowBad+=unexplained;run=unexplained?run+1:0;maxRun=(std::max)(maxRun,run);
        }
        if(rowBad>6 || maxRun>3)return false;
    }
    return total && black*100>=total*85 && bad*100<=total;
}
