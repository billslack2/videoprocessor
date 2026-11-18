#pragma once
// VP-0230: independent, current-frame experiment. Scores are not probabilities.
// No renderer, screen, subtitle, remembered geometry, or standard-ratio snapping.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ExperimentalAspect {
enum class Kind { Unknown, Bars, FullRaster };
struct Edge {
    int depth=0, uncertainty=0;
    double support=0, contrast=0, noise=0, darkness=0, score=0;
};
struct Result {
    Kind kind=Kind::Unknown;
    int left=0,top=0,right=0,bottom=0;
    Edge edges[4]; // left, top, right, bottom
    double score=0, fullScore=0, symmetry=0, ratioSupport=0;
    size_t samples=0;
    bool complete=false;
    const char* reason="invalid-source";
};
inline double Unit(double x) { return (std::max)(0.0,(std::min)(1.0,x)); }
template<size_t N> inline int Median(std::array<int,N> a, size_t n) {
    std::sort(a.begin(),a.begin()+n);return a[n/2];
}
// read returns normalized 10-bit luma; allow() supplies the external deadline.
// A failed read/budget produces UNKNOWN, never full-raster evidence.
template<class Read,class Allow>
Result Measure(int width,int height,int nominalBlack,Read read,Allow allow,
    size_t maxSamples=24000) {
    Result out;out.right=width;out.bottom=height;
    if(width<64 || height<64 || width>8192 || height>8192 || nominalBlack<0)return out;
    bool failed=false;
    auto pixel=[&](int x,int y) {
        int v=0;
        if(failed)return v;
        if(out.samples>=maxSamples || ((out.samples&63)==0 && !allow())) {
            failed=true;out.reason="budget";return v;
        }
        ++out.samples;
        if(!read(x,y,v)){failed=true;out.reason="read-failed";}
        return v;
    };
    std::array<double,4> fullSupport{};
    for(int side=0;side<4 && !failed;++side) {
        const bool horizontal=side==1 || side==3, reverse=side>=2;
        const int extent=horizontal?height:width, across=horizontal?width:height;
        const int step=(std::max)(2,extent/192), count=(std::min)(96,extent/3/step);
        const int parallel=(std::max)(1,across/1024);
        std::array<int,9> positions{}, strengths{}, noises{}, darks{};
        int found=0,full=0;
        auto sample=[&](int lane,int depth) {
            const int center=(lane+1)*across/10;
            std::array<int,3> v{};
            const int d=reverse?extent-1-depth:depth;
            for(int k=0;k<3;++k) {
                const int a=(std::max)(0,(std::min)(across-1,center+(k-1)*parallel));
                v[k]=pixel(horizontal?a:d,horizontal?d:a);
            }
            return Median(v,3); // parallel-only filtering preserves edge coordinates
        };
        for(int lane=0;lane<9 && !failed;++lane) {
            std::array<int,96> line{};
            for(int i=0;i<count && !failed;++i)line[i]=sample(lane,i*step);
            if(failed)break;
            // Positive full-extent evidence on BOTH outer and inward samples.
            // A failed bar hypothesis contributes nothing to this score.
            if(line[0]>nominalBlack+48 && line[2]>nominalBlack+48)++full;
            int best=-1; double bestMerit=0; int bestNoise=0,bestDark=0;
            for(int i=2;i<count-2;++i) {
                std::array<int,96> prefix=line;
                std::sort(prefix.begin(),prefix.begin()+i);
                const int median=prefix[i/2];
                const int spread=prefix[(i*9)/10]-prefix[i/10];
                const int delta=(line[i]+line[i+1]-line[i-1]-line[i-2])/2;
                const double dark=Unit((nominalBlack+96.0-median)/96.0);
                // Local transition relative to bar noise, with a soft darkness prior.
                const double merit=(delta/(8.0+spread))*dark;
                if(delta>0 && merit>bestMerit){best=i;bestMerit=merit;bestNoise=spread;bestDark=median;}
            }
            if(best<0 || bestMerit<1.0)continue;
            const int begin=(best-2)*step,end=(best+1)*step;
            int previous=sample(lane,begin), strongest=0,position=0;
            for(int d=begin+1;d<=end && !failed;++d) {
                const int current=sample(lane,d),delta=current-previous;
                if(delta>strongest){strongest=delta;position=d;}
                previous=current;
            }
            if(failed)break;
            if(position>0 && strongest>0){positions[found]=position;strengths[found]=strongest;
                noises[found]=bestNoise;darks[found]=bestDark;++found;}
        }
        fullSupport[side]=full/9.0;
        if(!found || failed)continue;
        // Find the densest cluster rather than average unrelated movie edges.
        int center=0,members=0;
        for(int i=0;i<found;++i) {
            int n=0;for(int j=0;j<found;++j)if(std::abs(positions[i]-positions[j])<=step)++n;
            if(n>members){members=n;center=positions[i];}
        }
        std::array<int,9> selected{},selectedStrength{},selectedNoise{},selectedDark{};
        int n=0,lo=extent,hi=0;
        for(int i=0;i<found;++i)if(std::abs(positions[i]-center)<=step) {
            selected[n]=positions[i];selectedStrength[n]=strengths[i];selectedNoise[n]=noises[i];selectedDark[n]=darks[i];
            lo=(std::min)(lo,positions[i]);hi=(std::max)(hi,positions[i]);++n;
        }
        auto& e=out.edges[side];e.depth=Median(selected,n);e.uncertainty=hi-lo;
        e.support=n/9.0;e.contrast=Median(selectedStrength,n);e.noise=Median(selectedNoise,n);
        e.darkness=Unit((nominalBlack+96.0-Median(selectedDark,n))/96.0);
        e.score=.35*e.support+.35*Unit(e.contrast/(12.0+2*e.noise))+.30*e.darkness;
    }
    if(failed || !allow()){if(!failed)out.reason="budget";return out;}
    out.complete=true;out.reason="insufficient-spatial-evidence";
    out.fullScore=*std::min_element(fullSupport.begin(),fullSupport.end());
    auto pairScore=[&](int a,int b,int extent) {
        const auto& x=out.edges[a];const auto& y=out.edges[b];
        if(x.support<.55 || y.support<.55 || !x.depth || !y.depth)return 0.0;
        const double symmetry=Unit(1.0-std::abs(x.depth-y.depth)/double((std::max)(8,extent/60)));
        return .8*(x.score+y.score)/2+.2*symmetry;
    };
    const double horizontal=pairScore(0,2,width),vertical=pairScore(1,3,height);
    if(horizontal>=.80){out.left=out.edges[0].depth;out.right=width-out.edges[2].depth;}
    if(vertical>=.80){out.top=out.edges[1].depth;out.bottom=height-out.edges[3].depth;}
    if(horizontal>=.80 || vertical>=.80) {
        const double ar=double(out.right-out.left)/(out.bottom-out.top);
        constexpr double ratios[]={1.33,1.43,1.66,1.78,1.85,1.90,2.00,2.20,2.35,2.39,2.40,2.55,2.76};
        for(double ratio:ratios)out.ratioSupport=(std::max)(out.ratioSupport,Unit(1-std::abs(ar/ratio-1)/.015));
        out.score=.95*(std::max)(horizontal,vertical)+.05*out.ratioSupport;
        out.symmetry=vertical>=horizontal?Unit(1-std::abs(out.edges[1].depth-out.edges[3].depth)/double((std::max)(8,height/60))):
            Unit(1-std::abs(out.edges[0].depth-out.edges[2].depth)/double((std::max)(8,width/60)));
        out.kind=Kind::Bars;out.reason="paired-measured-boundaries";
    } else if(out.fullScore>=.88) {
        out.kind=Kind::FullRaster;out.score=out.fullScore;out.reason="distributed-positive-outer-picture";
    }
    return out;
}
} // namespace ExperimentalAspect
