#pragma once
// VP-0230: measured geometry. History/ratios rank hypotheses, never create edges.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace ExperimentalAspect {
enum class Kind { Unknown, Bars, FullRaster };
struct Edge { int depth=0, uncertainty=0, matte=0, matteP90=0; double support=0,contrast=0,noise=0,darkness=0,score=0; };
struct Prior { int width=0,height=0,left=0,top=0,right=0,bottom=0; bool valid=false; };
struct Result {
    Kind kind=Kind::Unknown;
    int left=0,top=0,right=0,bottom=0;
    Edge edges[4];
    double score=0,fullScore=0,symmetry=0,ratioSupport=0;
    size_t samples=0; bool complete=false;
    // Diagnostics only. Acceptance is one-based and cleared on veto/incomplete work.
    unsigned pairAttempts[2]{}, acceptedPairAttempt[2]{};
    // Structured current-pixel support ranks a full-extent hypothesis; an
    // unsupported edge remains unknown, never a fabricated picture certificate.
    bool fullExtentQualified=false, pictureEdgeSupported[4]{};
    double unpairedBoundarySupport[4]{}, fullExtentScore=0;
    const char* reason="invalid-source";
};
inline double Unit(double x){return (std::max)(0.0,(std::min)(1.0,x));}
// An abrupt inward rise can corroborate a raised matte. Compare it with both
// the exterior texture and the elevation above the source black level.
inline double MatteEvidence(double darkness, double rise, double spread, int floor, int black) {
    const double elevation = (std::max)(0, floor - black);
    return (std::max)(darkness, Unit((rise - 2 * spread) / (32 + elevation)));
}
template<size_t N> inline int Median(std::array<int,N> a,size_t n){std::sort(a.begin(),a.begin()+n);return a[n/2];}
inline double RatioSupport(double ar){
    constexpr double ratios[]={1.33,1.43,1.66,1.78,1.85,1.90,2,2.20,2.35,2.39,2.40,2.55,2.76};
    double support=0;for(double r:ratios)support=(std::max)(support,Unit(1-std::abs(ar/r-1)/.015));return support;
}
// Coarse profiles retain alternatives until opposing edges are considered together.
// Filtering is parallel to the edge; final coordinates come from native pixels.
template<class Read,class Allow>
Result Measure(int width,int height,int nominalBlack,Read read,Allow allow,
    size_t maxSamples=24000,const Prior* prior=nullptr){
    Result out;out.right=width;out.bottom=height;
    if(width<64||height<64||width>8192||height>8192||nominalBlack<0)return out;
    bool failed=false;
    auto pixel=[&](int x,int y){
        int v=0;if(failed)return v;
        if(out.samples>=maxSamples||((out.samples&63)==0&&!allow())){failed=true;out.reason="budget";return v;}
        ++out.samples;if(!read(x,y,v)){failed=true;out.reason="read-failed";}return v;
    };
    auto sample=[&](int side,int lane,int depth){
        const bool horizontal=side==1||side==3;
        const int extent=horizontal?height:width,across=horizontal?width:height;
        const int d=side>=2?extent-1-depth:depth;
        const int center=(lane+1)*across/10,parallel=(std::max)(1,across/1024);
        std::array<int,3> v{};
        for(int k=0;k<3;++k){const int a=(std::max)(0,(std::min)(across-1,center+(k-1)*parallel));v[k]=pixel(horizontal?a:d,horizontal?d:a);}
        return Median(v,3);
    };
    struct Candidate {Edge edge;int index=0;unsigned laneMask=0;};
    std::array<std::array<Candidate,12>,4> candidates{};
    std::array<int,4> sizes{},steps{};
    std::array<double,4> fullSupport{};
    std::array<bool,4> competingMatteBoundary{};
    for(int side=0;side<4&&!failed;++side){
        const int extent=(side==1||side==3)?height:width;
        const int step=(std::max)(2,extent/192),count=(std::min)(96,extent/3/step);steps[side]=step;
        std::array<std::array<int,96>,9> lines{},noise{},matte{},high{},contrast{};
        int full=0;
        for(int lane=0;lane<9&&!failed;++lane){
            auto& line=lines[lane];
            for(int i=0;i<count&&!failed;++i)line[i]=sample(side,lane,i*step);
            if(line[0]>nominalBlack+48&&line[2]>nominalBlack+48)++full;
            // Incremental sorted prefix avoids sorting every candidate's exterior again.
            std::array<int,96> sorted{};
            for(int i=0;i<count;++i){
                if(i>=2&&i<count-2){
                    matte[lane][i]=sorted[i/2];high[lane][i]=sorted[i*9/10];noise[lane][i]=sorted[i*9/10]-sorted[i/10];
                    contrast[lane][i]=(line[i]+line[i+1]-line[i-1]-line[i-2])/2;
                }
                int j=i;while(j>0&&sorted[j-1]>line[i]){sorted[j]=sorted[j-1];--j;}sorted[j]=line[i];
            }
        }
        fullSupport[side]=full/9.0;
        std::array<Candidate,96> options{};int n=0;
        for(int i=2;i<count-2&&!failed;++i){
            if((i&7)==0&&!allow()){failed=true;out.reason="budget";break;}
            std::array<int,9> strengths{},spreads{},floors{},highs{};int support=0;unsigned laneMask=0;
            int weakBoundarySupport=0;
            for(int lane=0;lane<9;++lane){
                const double dark=Unit((nominalBlack+96.0-matte[lane][i])/96.0);
                // A weak but highly coherent one-sided step competes with full
                // extent even when it cannot establish an opposing bar pair.
                if(contrast[lane][i]>=2 && contrast[lane][i]>=.2*noise[lane][i])
                    ++weakBoundarySupport;
                if(contrast[lane][i]>=4&&contrast[lane][i]/(8.0+noise[lane][i])*MatteEvidence(dark,contrast[lane][i],noise[lane][i],matte[lane][i],nominalBlack)>=1){
                    laneMask|=1u<<lane;strengths[support]=contrast[lane][i];spreads[support]=noise[lane][i];floors[support]=matte[lane][i];highs[support]=high[lane][i];++support;
                }
            }
            bool localizedWeakBoundary=false;
            if(weakBoundarySupport>=8 && i>=5 && i<count-5){
                std::array<int,9> center{},before{},after{},prefixNoise{};
                for(int lane=0;lane<9;++lane){
                    center[lane]=contrast[lane][i];
                    before[lane]=contrast[lane][i-3];
                    after[lane]=contrast[lane][i+3];
                    prefixNoise[lane]=noise[lane][i];
                }
                // A gradual spatial fade is picture ambiguity, not a coherent
                // matte boundary. The weak step must rise above nearby slopes.
                const int neighboring=(std::max)(0,(std::max)(Median(before,9),Median(after,9)));
                localizedWeakBoundary=Median(center,9)>=neighboring+(Median(prefixNoise,9)>4?2:1);
            }
            if(support>=3)competingMatteBoundary[side]=true;
            if(localizedWeakBoundary){
                std::array<int,9> prefixFloors{};
                for(int lane=0;lane<9;++lane)prefixFloors[lane]=matte[lane][i];
                // Texture across a raised bar does not erase its independently
                // measured inward boundary. Bright interior structure is not matte.
                if(Median(prefixFloors,9)<nominalBlack+96)competingMatteBoundary[side]=true;
            }
            if(support>=3 || localizedWeakBoundary)
                out.unpairedBoundarySupport[side]=(std::max)(out.unpairedBoundarySupport[side],
                    (std::max)(support,localizedWeakBoundary?weakBoundarySupport:0)/9.0);
            if(support<3)continue;
            auto& c=options[n++];c.index=i;c.laneMask=laneMask;auto& e=c.edge;e.depth=i*step;
            e.support=support/9.0;e.contrast=Median(strengths,support);e.noise=Median(spreads,support);
            e.matte=Median(floors,support);e.matteP90=Median(highs,support);
            e.darkness=Unit((nominalBlack+96.0-e.matte)/96.0);
            e.score=.35*e.support+.35*Unit(e.contrast/(12+2*e.noise))+.30*MatteEvidence(e.darkness,e.contrast,e.noise,e.matte,nominalBlack)-.10*Unit(e.uncertainty/double(step));
            // Stop this inward search once the clean outer strip has given
            // way to distributed picture. Leave isolated overlays and flat
            // stepped mattes to opposing-edge ranking and native refinement.
            if(support>=7 && e.matteP90<=nominalBlack+48 && e.noise<=12 && i+3<count){
                int pictureColumns=0;
                for(int depth=i+1;depth<=i+3;++depth){
                    std::array<int,9> inside{};
                    for(int lane=0;lane<9;++lane)inside[lane]=lines[lane][depth];
                    std::sort(inside.begin(),inside.end());
                    if(inside[2]>=e.matteP90+32 && inside[6]-inside[2]>=24)
                        ++pictureColumns;
                }
                if(pictureColumns==3)break;
            }
        }
        std::sort(options.begin(),options.begin()+n,[](const Candidate&a,const Candidate&b){return a.edge.score>b.edge.score;});
        // Keep spatially distinct alternatives; adjacent coarse positions describe one edge.
        for(int i=0;i<n&&sizes[side]<12;++i){
            bool duplicate=false;for(int j=0;j<sizes[side];++j)if(std::abs(options[i].index-candidates[side][j].index)<=1)duplicate=true;
            if(!duplicate)candidates[side][sizes[side]++]=options[i];
        }
    }
    auto scorePair=[&](const Edge& a,const Edge& b,int side){
        const int extent=side==0?width:height;
        if(a.support<3.0/9||b.support<3.0/9||(std::max)(a.support,b.support)<7.0/9)return 0.0;
        // Agreement is soft, but a wholly inconsistent pair cannot pass on contrast alone.
        const double symmetry=Unit(1-std::abs(a.depth-b.depth)/double((std::max)(8,extent/40)));
        double history=0;
        if(prior&&prior->valid&&prior->width==width&&prior->height==height){
            const int first=side==0?prior->left:prior->top,last=side==0?width-prior->right:height-prior->bottom;
            history=Unit(1-(std::abs(a.depth-first)+std::abs(b.depth-last))/double((std::max)(8,extent/40)));
        }
        const double ar=side==0?double(width-a.depth-b.depth)/height:double(width)/(height-a.depth-b.depth);
        return Unit(.75*(a.score+b.score)/2+.20*symmetry+.05*history+.05*RatioSupport(ar));
    };
    bool accepted[2]={false,false};double pairScores[2]{};
    std::array<std::array<Edge,12>,4> refined{};
    std::array<std::array<bool,12>,4> refinedReady{};
    auto refine = [&](int side, int choice) -> const Edge& {
        auto& edge = refined[side][choice];
        if (refinedReady[side][choice]) return edge;
        refinedReady[side][choice] = true;
        const int step = steps[side];
        const auto& candidate = candidates[side][choice];
        edge = candidate.edge;
        std::array<int,9> positions{};
        int found = 0;
        for (int lane = 0; lane < 9 && !failed; ++lane) {
            if (!(candidate.laneMask & (1u << lane))) continue;
            const int begin = (candidate.index - 2) * step;
            const int end = (candidate.index + 1) * step;
            int previous = sample(side, lane, begin), strongest = 0, position = 0;
            for (int depth = begin + 1; depth <= end && !failed; ++depth) {
                const int current = sample(side, lane, depth);
                const int delta = current - previous;
                if (delta > strongest) { strongest = delta; position = depth; }
                previous = current;
            }
            if (position > 0 && strongest >= 4) positions[found++] = position;
        }
        if (!found) { edge.support = 0; return edge; }
        int members = 0, center = 0;
        for (int i = 0; i < found; ++i) {
            int count = 0;
            for (int j = 0; j < found; ++j)
                if (std::abs(positions[i] - positions[j]) <= step) ++count;
            if (count > members) { members = count; center = positions[i]; }
        }
        int count = 0, lo = (side == 1 || side == 3) ? height : width, hi = 0;
        for (int i = 0; i < found; ++i) {
            if (std::abs(positions[i] - center) > step) continue;
            ++count;
            lo = (std::min)(lo, positions[i]);
            hi = (std::max)(hi, positions[i]);
        }
        edge.depth = lo;
        edge.uncertainty = hi - lo;
        edge.support = (std::min)(edge.support, count / 9.0);
        edge.score = .35 * edge.support + .35 * Unit(edge.contrast / (12 + 2 * edge.noise)) +
            .30 * MatteEvidence(edge.darkness,edge.contrast,edge.noise,edge.matte,nominalBlack) - .10 * Unit(edge.uncertainty / double(step));
        return edge;
    };
    for (int axis = 0; axis < 2 && !failed; ++axis) {
        struct Pair { int first = 0, last = 0; double score = 0; };
        std::array<Pair,3> ranked{};
        int count = 0;
        for (int i = 0; i < sizes[axis]; ++i) {
            for (int j = 0; j < sizes[axis + 2]; ++j) {
                const double score = scorePair(candidates[axis][i].edge,
                    candidates[axis + 2][j].edge, axis);
                if (score <= 0) continue;
                // Strict comparison preserves the original nested-loop tie order.
                int position = 0;
                while (position < count && score <= ranked[position].score) ++position;
                if (position == int(ranked.size())) continue;
                count = (std::min)(count + 1, int(ranked.size()));
                for (int move = count - 1; move > position; --move)
                    ranked[move] = ranked[move - 1];
                ranked[position] = {i, j, score};
            }
        }
        // Retry only failed native refinement, bounded by three distinct retained
        // pairs and the unchanged pixel/deadline budget. Shared edges are read once.
        for (int attempt = 0; attempt < count && !failed; ++attempt) {
            if (!allow()) { failed = true; out.reason = "budget"; break; }
            ++out.pairAttempts[axis];
            const auto& pair = ranked[attempt];
            out.edges[axis] = refine(axis, pair.first);
            if (failed) break;
            out.edges[axis + 2] = refine(axis + 2, pair.last);
            if (failed) break;
            pairScores[axis] = scorePair(out.edges[axis], out.edges[axis + 2], axis);
            accepted[axis] = pairScores[axis] >= .82;
            if (accepted[axis]) {
                out.acceptedPairAttempt[axis] = unsigned(attempt + 1);
                break;
            }
        }
    }
    // Challenge matte-like panels with picture still visible at the raster sides.
    // Central captions may contaminate strips; distributed exterior picture at
    // the far sides contradicts crop regardless of history or ratio support.
    bool exteriorConflict=false;
    for(int axis=0;axis<2&&!failed;++axis)if(accepted[axis]){
        const int across=axis==1?width:height,extent=axis==1?height:width;
        const int positions[]={across/200,across/40,across-1-across/40,across-1-across/200};
        bool conflict=false;
        for(int k=0;k<2&&!failed;++k){
            const auto& edge=out.edges[axis+2*k];
            const double limit=edge.matte+(std::max)(24.0,3*edge.noise+8);
            for(int a:positions){
                int visible=0;
                for(int depth=1;depth<=4&&!failed;++depth){
                    const int d=edge.depth*depth/5,coordinate=k?extent-1-d:d;
                    if(pixel(axis==1?a:coordinate,axis==1?coordinate:a)>limit)++visible;
                }
                if(visible>=3)conflict=true;
            }
            // Compare depths on the same lane so brightness changes along
            // a raised matte do not count as picture. Require variation on
            // several lanes to tolerate isolated overlays in genuine bars.
            int texturedLanes=0;
            for(int lane=1;lane<=9&&!failed&&!conflict;++lane){
                const int a=lane*across/10;
                std::array<int,4> levels{};
                for(int depth=1;depth<=4;++depth){
                    const int d=edge.depth*depth/5,coordinate=k?extent-1-d:d;
                    levels[depth-1]=pixel(axis==1?a:coordinate,axis==1?coordinate:a);
                }
                std::sort(levels.begin(),levels.end());
                if(levels[2]-levels[0]>=48||levels[3]-levels[1]>=48)
                    conflict=++texturedLanes>=3;
            }
        }
        if(conflict){accepted[axis]=false;out.acceptedPairAttempt[axis]=0;exteriorConflict=true;}
    }
    out.fullScore=*std::min_element(fullSupport.begin(),fullSupport.end());
    if (!failed && !accepted[0] && !accepted[1]) {
        int supportedEdges = 0;
        bool competingBoundary = false;
        // A sparse positive level alone also describes raised matte. Require
        // broad variation that persists inward: independent noise, uniform
        // lifted bars and isolated edge overlays do not supply this evidence.
        for (int side = 0; side < 4 && !failed; ++side) {
            const bool alongRow = side == 1 || side == 3;
            const int across = alongRow ? width : height;
            const int extent = alongRow ? height : width;
            std::array<std::array<int,48>,3> strips{};
            const int depths[] = {0, 4, 16};
            for (int depth = 0; depth < 3 && !failed; ++depth) {
                const int coordinate = side >= 2 ? extent - 1 - depths[depth] : depths[depth];
                for (int lane = 0; lane < 48 && !failed; ++lane) {
                    const int position = ((lane * 2 + 1) * across) / 96;
                    strips[depth][lane] = pixel(alongRow ? position : coordinate,
                        alongRow ? coordinate : position);
                }
            }
            if (failed) break;
            auto sorted = strips[0];
            std::sort(sorted.begin(), sorted.end());
            const int spread = sorted[43] - sorted[4];
            bool coherent = spread >= 32;
            for (int depth = 1; depth < 3 && coherent; ++depth) {
                double sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
                for (int lane = 0; lane < 48; ++lane) {
                    const double a = strips[0][lane], b = strips[depth][lane];
                    sumA += a; sumB += b; sumAA += a*a; sumBB += b*b; sumAB += a*b;
                }
                const double covariance = 48*sumAB - sumA*sumB;
                const double varianceA = 48*sumAA - sumA*sumA;
                const double varianceB = 48*sumBB - sumB*sumB;
                coherent = covariance > 0 && varianceA > 0 && varianceB > 0 &&
                    covariance*covariance >= .49*varianceA*varianceB;
            }
            out.pictureEdgeSupported[side] = coherent;
            if (coherent) ++supportedEdges;
            if (competingMatteBoundary[side] ||
                (!coherent && out.unpairedBoundarySupport[side] > 0)) competingBoundary = true;
        }
        out.fullExtentScore = supportedEdges / 4.0;
        out.fullExtentQualified = !failed && supportedEdges >= 3 && !competingBoundary;
    }
    if(failed||!allow()){
        if(!failed)out.reason="budget";
        out.acceptedPairAttempt[0]=out.acceptedPairAttempt[1]=0;
        out.fullExtentQualified=false;
        return out;
    }
    out.complete=true;out.reason=exteriorConflict?"exterior-picture-conflict":"insufficient-spatial-evidence";
    if(accepted[0]){out.left=out.edges[0].depth;out.right=width-out.edges[2].depth;}
    if(accepted[1]){out.top=out.edges[1].depth;out.bottom=height-out.edges[3].depth;}
    if(accepted[0]||accepted[1]){
        out.kind=Kind::Bars;out.reason="joint-measured-boundaries";out.score=(std::max)(pairScores[0],pairScores[1]);
        out.ratioSupport=RatioSupport(double(out.right-out.left)/(out.bottom-out.top));
        const int a=pairScores[1]>=pairScores[0]?1:0,extent=a?height:width;
        out.symmetry=Unit(1-std::abs(out.edges[a].depth-out.edges[a+2].depth)/double((std::max)(8,extent/40)));
    }else if(out.fullExtentQualified||out.fullScore>=.88){
        out.kind=Kind::FullRaster;
        out.score=out.fullExtentQualified?out.fullExtentScore:out.fullScore;
        out.reason=out.fullExtentQualified?"structured-current-picture-full-extent":"distributed-positive-outer-picture";
    }
    return out;
}
} // namespace ExperimentalAspect
