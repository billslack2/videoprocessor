#pragma once
#include <SourcePictureMeasurement.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// A cheap current-source search proposal. These bounds never carry native or
// crop authority: the caller must refine and run the existing native verifier.
struct SourcePictureCoarseColorProposal {
    bool available=false;
    // Native samples contradict a continuous physical rectangle; distinct from
    // an unsupported format, unreadable source, or simply absent nomination.
    bool physicalContinuityContradicted=false;
    ActivePictureBounds bounds; // First native color departure; nomination only.
    ActivePictureBounds physicalEstimate,verificationBounds;
    std::array<int,4> bracketMinimum{},bracketMaximum{};
    size_t samples=0;
    const char* reason="coarse-color-unresolved";
};
namespace SourcePictureCoarseColorDetail {
struct Color {float c[3];};
inline float Quantile(std::vector<float> values,unsigned numerator,unsigned denominator) {
    if(values.empty())return 0;
    const size_t index=(values.size()-1)*numerator/denominator;
    std::nth_element(values.begin(),values.begin()+index,values.end());return values[index];
}
inline float Distance(const Color& a,const Color& b) {
    return std::max({std::abs(a.c[0]-b.c[0]),std::abs(a.c[1]-b.c[1]),std::abs(a.c[2]-b.c[2])});
}
inline Color Median(const std::vector<Color>& pixels) {
    Color out{};for(unsigned c=0;c<3;++c) {std::vector<float> values;values.reserve(pixels.size());
        for(const auto& p:pixels)values.push_back(p.c[c]);out.c[c]=Quantile(std::move(values),1,2);}
    return out;
}
}
inline SourcePictureCoarseColorProposal ProposeSourcePictureFromCoarseColor(
    const AnalysisLumaSource& source,const AnalysisEvaluationBudget* budget=nullptr) {
    using namespace SourcePictureCoarseColorDetail;
    SourcePictureCoarseColorProposal out;
    if(!source.IsValid() || source.range==AnalysisLumaRange::Unknown || source.width<32 || source.height<32)return out;
    ValidatedAnalysisLumaSampler sampler(source);
    // Other color interpretations retain their established native detector;
    // do not silently treat unknown or differently encoded source primaries as 709.
    if(source.colorspace!=ColorSpace::REC_709 && source.colorspace!=ColorSpace::BT_2020)return out;
    const bool wide=source.colorspace==ColorSpace::BT_2020;
    const float redV=wide?1.4746f:1.5748f,blueU=wide?1.8814f:1.8556f;
    const float greenU=wide?.164553f:.187324f,greenV=wide?.571353f:.468124f;
    const float yScale=source.range==AnalysisLumaRange::Limited?1.f/876:1.f/1023;
    const float cScale=source.range==AnalysisLumaRange::Limited?1.f/896:1.f/1023;
    const int nominal=source.range==AnalysisLumaRange::Limited?64:0;
    // Half of one 8-bit-equivalent code in each encoded channel. Color is
    // unclipped: conversion must not turn chroma excursions into black.
    const float rounding=2*yScale+2*std::max(redV,blueU)*cScale;
    auto read=[&](int x,int y,Color& color,AnalysisLumaSample* raw=nullptr) {
        if((out.samples++&255u)==0)CheckAnalysisEvaluationBudget(budget);
        AnalysisLumaSample p;if(!sampler.Sample(x,y,p))return false;if(raw)*raw=p;
        const float l=(int(p.luma)-nominal)*yScale,u=(int(p.chromaU)-512)*cScale,v=(int(p.chromaV)-512)*cScale;
        color={{l+redV*v,l-greenU*u-greenV*v,l+blueU*u}};return true;
    };
    const int gw=std::min(240,source.width),gh=std::max(1,int(int64_t(gw)*source.height/source.width));
    if(gh>240)return out; // Portrait/very unusual rasters retain the established path.
    std::vector<Color> grid(size_t(gw)*gh);
    for(int y=0;y<gh;++y)for(int x=0;x<gw;++x)
        if(!read(int(int64_t(2*x+1)*source.width/(2*gw)),int(int64_t(2*y+1)*source.height/(2*gh)),grid[size_t(y)*gw+x]))return out;
    int edge[4]={0,0,source.width,source.height};
    int physical[4]={0,0,source.width,source.height},safe[4]={0,0,source.width,source.height};
    for(unsigned side=0;side<4;++side) {
        const bool horizontal=side==1 || side==3,leading=side<2;
        const int depth=horizontal?source.height:source.width,along=horizontal?source.width:source.height;
        const int cells=horizontal?gh:gw,columns=horizontal?gw:gh;
        auto nativeLine=[&](int inward,std::vector<Color>& pixels,std::vector<float>* luma=nullptr) {
            pixels.clear();pixels.reserve(128);
            for(int i=0;i<128;++i) {
                const int a=int(int64_t(2*i+1)*along/256),d=leading?inward:depth-1-inward;
                Color c;AnalysisLumaSample raw;if(!read(horizontal?a:d,horizontal?d:a,c,&raw))return false;
                pixels.push_back(c);if(luma)luma->push_back(float(raw.luma));
            }return true;
        };
        std::vector<Color> outer;std::vector<float> luma;
        if(!nativeLine(0,outer,&luma))return out;
        const Color reference=Median(outer);std::vector<float> deviations;
        for(const auto& p:outer)deviations.push_back(Distance(p,reference));
        const float outerSpread=Quantile(deviations,9,10);
        const float outerLuma=Quantile(luma,1,2);
        // Preserve the established source-measurement ceiling for raised
        // encoded black (108 canonical codes), in both supported ranges.
        // This is eligibility to search a matte, never the picture cutoff.
        const bool nearBlack=outerLuma<=108 && outerLuma>=nominal-8;
        // Coarse samples cannot establish continuity through unsampled native
        // rows/columns. Inspect every intervening native line using stratified,
        // deterministically phase-varying pairs. This is bounded sampled evidence,
        // not an exhaustive topology certificate. A hidden matte gap declines
        // physical promotion for this source, including a conflicting fallback.
        auto nativeContinuity=[&](int from) {
            for(int inward=from;inward<depth/2;++inward) {
                bool observed=false;
                for(unsigned stratum=0;stratum<32 && !observed;++stratum) {
                    const int begin=int(int64_t(stratum)*along/32),end=int(int64_t(stratum+1)*along/32);
                    uint32_t phase=uint32_t(inward)*0x9e3779b9u+(stratum+1)*0x85ebca6bu;phase^=phase>>16;
                    const int a=begin+int(phase%unsigned(std::max(1,end-begin-1)));
                    const int d=leading?inward:depth-1-inward;Color first,second;
                    if(!read(horizontal?a:d,horizontal?d:a,first) ||
                        !read(horizontal?std::min(along-1,a+1):d,horizontal?d:std::min(along-1,a+1),second))return false;
                    observed=Distance(first,reference)>rounding && Distance(second,reference)>rounding;
                }
                if(!observed) {out.physicalContinuityContradicted=true;return false;}
            }return true;
        };
        // A nonuniform current raster edge or a uniform nonblack edge is
        // positive raster contact, not a place to search for an interior object.
        if(outerSpread>rounding || !nearBlack) {
            if(nearBlack && !nativeContinuity(1)) {out.reason="coarse-color-native-continuity-unresolved";return out;}
            out.bracketMinimum[side]=out.bracketMaximum[side]=edge[side];continue;
        }
        auto lineDistance=[&](const std::vector<Color>& pixels) {
            std::vector<float> values;values.reserve(pixels.size());
            for(const auto& p:pixels)values.push_back(Distance(p,reference));
            return Quantile(std::move(values),3,5);
        };
        int first=-1;bool separated=false;
        for(int d=0;d<cells/2;++d) {
            std::vector<Color> line;line.reserve(columns);
            const int dd=leading?d:cells-1-d;
            for(int a=0;a<columns;++a)line.push_back(horizontal?grid[size_t(dd)*gw+a]:grid[size_t(a)*gw+dd]);
            const bool picture=lineDistance(line)>rounding;
            if(picture && first<0)first=d;
            if(!picture && first>=0)separated=true;
        }
        // Detached graphics, subtitles and unresolved dark gaps are not a
        // rectangular-matte proof. Let the established topology path inspect them.
        if(first<0 || separated) {out.reason="coarse-color-disconnected-or-unobserved-picture";return out;}
        // Bracket the first departure between the actual sampled centers.
        // Reversing an even-sized raster changes center rounding by one pixel;
        // cell boundaries are not valid substitutes for those native coordinates.
        const auto sampledInward=[&](int index) {
            const int cell=leading?index:cells-1-index;
            const int coordinate=int(int64_t(2*cell+1)*depth/(2*cells));
            return leading?coordinate:depth-1-coordinate;
        };
        const int lo=first?sampledInward(first-1)+1:0;
        const int hi=sampledInward(first);
        int refined=-1;std::vector<Color> line;std::vector<float> profile;
        for(int d=lo;d<=hi;++d) {
            if(!nativeLine(d,line))return out;
            const float value=lineDistance(line);profile.push_back(value);
            if(refined<0 && value>rounding)refined=d;
        }
        if(refined<0) {out.reason="coarse-color-native-refinement-unresolved";return out;}
        if(!nativeContinuity(refined+1)) {out.reason="coarse-color-native-continuity-unresolved";return out;}
        // The first visible filtered collar and the physical step center are
        // different facts. Preserve the complete measured transition interval;
        // only the outward guard may be offered to the native crop verifier.
        const float inside=profile.back();int center=refined,end=hi;
        for(int d=refined;d<=hi;++d)if(profile[size_t(d-lo)]>=inside*.5f) {center=d;break;}
        for(int d=center;d<=hi;++d)if(profile[size_t(d-lo)]>=inside-rounding) {end=d;break;}
        edge[side]=leading?refined:depth-refined;
        physical[side]=leading?center:depth-center;
        // Include the preceding native sample and the inter-pixel uncertainty.
        // The existing verifier can round this guard outward to chroma pairs.
        const int outside=std::max(0,refined-1);
        safe[side]=leading?outside:depth-outside;
        out.bracketMinimum[side]=leading?outside:depth-end;
        out.bracketMaximum[side]=leading?end:depth-outside;
    }
    if(edge[0]>=edge[2] || edge[1]>=edge[3])return out;
    out.bounds={edge[0],edge[1],edge[2],edge[3],source.width,source.height,
        double(edge[2]-edge[0])/(edge[3]-edge[1]),ActivePictureBounds::BarAxes::NONE};
    out.physicalEstimate={physical[0],physical[1],physical[2],physical[3],source.width,source.height,
        double(physical[2]-physical[0])/(physical[3]-physical[1]),ActivePictureBounds::BarAxes::NONE};
    out.verificationBounds={safe[0],safe[1],safe[2],safe[3],source.width,source.height,
        double(safe[2]-safe[0])/(safe[3]-safe[1]),ActivePictureBounds::BarAxes::NONE};
    out.available=true;out.reason="current-coarse-color-native-refined-proposal";return out;
}
