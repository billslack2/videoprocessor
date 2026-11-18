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
    const char* reason="invalid-source";
};
inline double Unit(double x){return (std::max)(0.0,(std::min)(1.0,x));}
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
            for(int lane=0;lane<9;++lane){
                const double dark=Unit((nominalBlack+96.0-matte[lane][i])/96.0);
                if(contrast[lane][i]>=4&&contrast[lane][i]/(8.0+noise[lane][i])*dark>=1){
                    laneMask|=1u<<lane;strengths[support]=contrast[lane][i];spreads[support]=noise[lane][i];floors[support]=matte[lane][i];highs[support]=high[lane][i];++support;
                }
            }
            if(support<3)continue;
            auto& c=options[n++];c.index=i;c.laneMask=laneMask;auto& e=c.edge;e.depth=i*step;
            e.support=support/9.0;e.contrast=Median(strengths,support);e.noise=Median(spreads,support);
            e.matte=Median(floors,support);e.matteP90=Median(highs,support);
            e.darkness=Unit((nominalBlack+96.0-e.matte)/96.0);
            e.score=.35*e.support+.35*Unit(e.contrast/(12+2*e.noise))+.30*e.darkness-.10*Unit(e.uncertainty/double(step));
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
    for(int axis=0;axis<2&&!failed;++axis){
        int first=-1,last=-1;double best=0;
        for(int i=0;i<sizes[axis];++i)for(int j=0;j<sizes[axis+2];++j){
            const double score=scorePair(candidates[axis][i].edge,candidates[axis+2][j].edge,axis);
            if(score>best){best=score;first=i;last=j;}
        }
        if(first<0)continue;
        const int choices[]={first,last};
        for(int k=0;k<2&&!failed;++k){
            const int side=axis+2*k,step=steps[side];const auto& c=candidates[side][choices[k]];
            auto& e=out.edges[side];e=c.edge;
            std::array<int,9> positions{};int found=0;
            for(int lane=0;lane<9&&!failed;++lane){
                if(!(c.laneMask&(1u<<lane)))continue;
                const int begin=(c.index-2)*step,end=(c.index+1)*step;
                int previous=sample(side,lane,begin),strongest=0,position=0;
                for(int d=begin+1;d<=end&&!failed;++d){const int current=sample(side,lane,d),delta=current-previous;if(delta>strongest){strongest=delta;position=d;}previous=current;}
                if(position>0&&strongest>=4)positions[found++]=position;
            }
            if(!found){e.support=0;continue;}
            int members=0,center=0;
            for(int i=0;i<found;++i){int n=0;for(int j=0;j<found;++j)if(std::abs(positions[i]-positions[j])<=step)++n;if(n>members){members=n;center=positions[i];}}
            std::array<int,9> cluster{};int n=0,lo=((side==1||side==3)?height:width),hi=0;
            for(int i=0;i<found;++i)if(std::abs(positions[i]-center)<=step){cluster[n++]=positions[i];lo=(std::min)(lo,positions[i]);hi=(std::max)(hi,positions[i]);}
            e.depth=lo;e.uncertainty=hi-lo;e.support=(std::min)(e.support,n/9.0);
            e.score=.35*e.support+.35*Unit(e.contrast/(12+2*e.noise))+.30*e.darkness-.10*Unit(e.uncertainty/double(step));
        }
        pairScores[axis]=scorePair(out.edges[axis],out.edges[axis+2],axis);
        accepted[axis]=pairScores[axis]>=.82;
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
        }
        if(conflict){accepted[axis]=false;exteriorConflict=true;}
    }
    if(failed||!allow()){if(!failed)out.reason="budget";return out;}
    out.complete=true;out.reason=exteriorConflict?"exterior-picture-conflict":"insufficient-spatial-evidence";
    out.fullScore=*std::min_element(fullSupport.begin(),fullSupport.end());
    if(accepted[0]){out.left=out.edges[0].depth;out.right=width-out.edges[2].depth;}
    if(accepted[1]){out.top=out.edges[1].depth;out.bottom=height-out.edges[3].depth;}
    if(accepted[0]||accepted[1]){
        out.kind=Kind::Bars;out.reason="joint-measured-boundaries";out.score=(std::max)(pairScores[0],pairScores[1]);
        out.ratioSupport=RatioSupport(double(out.right-out.left)/(out.bottom-out.top));
        const int a=pairScores[1]>=pairScores[0]?1:0,extent=a?height:width;
        out.symmetry=Unit(1-std::abs(out.edges[a].depth-out.edges[a+2].depth)/double((std::max)(8,extent/40)));
    }else if(out.fullScore>=.88){out.kind=Kind::FullRaster;out.score=out.fullScore;out.reason="distributed-positive-outer-picture";}
    return out;
}
} // namespace ExperimentalAspect
