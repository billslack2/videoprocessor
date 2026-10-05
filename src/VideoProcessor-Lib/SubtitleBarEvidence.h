#pragma once
#include <AnalysisLumaSource.h>
#include <SubtitleBoxDetector.h>
#include <algorithm>
#include <array>
#include <cstdlib>

// Subtitle-only current-pixel evidence, never permission to crop or hide pixels.
// Each edge is proved independently; caption strokes may occupy a sparse part of
// a flat bar, but a wide connected picture region must not become a subtitle ROI.
struct SubtitleBarEvidence
{
    bool available = false;
    int top = 0, bottom = 0, topDepth = 0, bottomDepth = 0;
    int topFloor = 0, bottomFloor = 0, topSupport = 0, bottomSupport = 0;
    int topBoundarySupport = 0, bottomBoundarySupport = 0;
    int topMaxIntrusion = 0, bottomMaxIntrusion = 0;
    double topBlackFraction = 0.0, bottomBlackFraction = 0.0;
    size_t samples = 0;
    bool topRecoveredAtShallowerEdge = false, bottomRecoveredAtShallowerEdge = false;
    // Reference-only proof is deliberately separate from acquisition authority.
    // The caller owns the lifetime and identity of a previously strong plane.
    bool topRevalidatedAtReference = false, bottomRevalidatedAtReference = false;
    int referenceTop = 0, referenceBottom = 0;
    int topReferenceBoundarySupport = 0, bottomReferenceBoundarySupport = 0;
    int topReferenceMaxIntrusion = 0, bottomReferenceMaxIntrusion = 0;
    double topReferenceBlackFraction = 0.0, bottomReferenceBlackFraction = 0.0;
    const char* reason = "invalid-source";
};

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
                       double& fraction,int& boundarySupport,int& maxIntrusion) {
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
        // If either flank is obscured, do not infer a new boundary from an
        // interior edge alone.
        constexpr int FlankColumns=Columns/8;
        std::array<int,2*FlankColumns> flankRuns{};
        for(int i=0;i<FlankColumns;++i) {
            flankRuns[i]=runs[i];
            flankRuns[FlankColumns+i]=runs[Columns-FlankColumns+i];
        }
        std::nth_element(flankRuns.begin(),flankRuns.begin()+FlankColumns,flankRuns.end());
        depth=flankRuns[FlankColumns];
        int leftSupport=0,rightSupport=0;
        for(int i=0;i<Columns;++i) {
            if(std::abs(runs[i]-depth)>tolerance)continue;
            ++support;
            if(i<FlankColumns)++leftSupport;
            if(i>=Columns-FlankColumns)++rightSupport;
        }
        if(depth<2*step+1 || depth>=limit || support*5<Columns ||
            leftSupport<FlankColumns*3/4 || rightSupport<FlankColumns*3/4)return false;

        // A threshold crossing alone is not an encoded edge: smooth dark scene
        // gradients also cross the black threshold. Require abrupt native-row
        // contrast at distributed columns close to this frame's proposed plane.
        unsigned boundaryZones=0;
        int leftBoundarySupport=0,rightBoundarySupport=0;
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
                if(i<FlankColumns)++leftBoundarySupport;
                if(i>=Columns-FlankColumns)++rightBoundarySupport;
            }
        }
        int zones=0;for(unsigned bits=boundaryZones;bits;bits>>=1)zones+=bits&1;
        if(boundarySupport<8 || zones<2 ||
            leftBoundarySupport<4 || rightBoundarySupport<4)return false;
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
        if (fraction>=0.85 && maxIntrusion>=Columns/5)
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
                if(candidateFraction>=0.85 && candidateMaxIntrusion<Columns/5) {
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
        return fraction>=0.85 && maxIntrusion<Columns/5;
    };
    const bool top=inspect(false,result.topFloor,result.topDepth,result.topSupport,
        result.topBlackFraction,result.topBoundarySupport,result.topMaxIntrusion);
    const bool bottom=inspect(true,result.bottomFloor,result.bottomDepth,result.bottomSupport,
        result.bottomBlackFraction,result.bottomBoundarySupport,result.bottomMaxIntrusion);
    if(top)result.top=result.topDepth;
    if(bottom)result.bottom=source.height-result.bottomDepth;
    result.available=top||bottom;
    result.reason=top?(bottom?"current-flat-independent-bars":"current-flat-top-bar"):
        (bottom?"current-flat-bottom-bar":"no-current-flat-bar");
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
