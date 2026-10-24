#pragma once
#include <AnalysisLumaSource.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>

// Stateless native-pixel proof of one nominated cue-side plane. The caller
// must bind source identity, cue ownership and policy separately. A nomination
// supplies only a coordinate; this proof cannot promote raw bar/AR authority,
// authorize glyph movement, or authorize cleanup by itself.
struct CurrentSubtitleCueSidePlaneProof {
    bool valid=false;
    int plane=0,blackFloor=0;
    bool fromTopBar=false;
    size_t samples=0;
    unsigned boundarySupport=0,boundaryZones=0,maxIntrusion=0;
    double blackFraction=0.0;
    const char* reason="plane-invalid-source";
};

inline CurrentSubtitleCueSidePlaneProof ProveCurrentSubtitleCueSidePlane(
    const AnalysisLumaSource& source,int plane,bool fromTopBar,size_t maxSamples)
{
    CurrentSubtitleCueSidePlaneProof out;out.plane=plane;out.fromTopBar=fromTopBar;
    const auto reject=[&](const char* why){out.reason=why;return out;};
    if(!source.IsValid() || source.width<64 || source.height<48 ||
        source.width>8192 || source.height>4320 || plane<=0 || plane>=source.height)
        return out;
    const int step=(std::max)(1,source.height/540);
    const int depth=fromTopBar?plane:source.height-plane;
    if(depth<2*step+1 || depth>=source.height/3)return reject("plane-invalid-depth");
    constexpr int columns=128;
    std::array<int,columns> xs{};
    for(int i=0;i<columns;++i)xs[i]=(source.width*(4*columns+96*(2*i+1)))/(200*columns);
    const auto row=[&](int d){return fromTopBar?d:source.height-1-d;};
    bool exhausted=false;
    const auto sample=[&](int x,int y,AnalysisLumaSample& value){
        if(out.samples>=maxSamples){exhausted=true;return false;}
        ++out.samples;
        if(!source.Sample(x,y,value)) {
            exhausted=source.readBudget && source.readBudget->exhausted;return false;
        }
        return true;
    };
    const auto failed=[&](){return reject(exhausted?"plane-work-budget":"plane-native-sample-unavailable");};
    const auto neutral=[](const AnalysisLumaSample& value){
        return std::abs(int(value.chromaU)-512)<=32 && std::abs(int(value.chromaV)-512)<=32;
    };
    std::array<int,columns*3> levels{};unsigned count=0,neutralCount=0;
    for(int d:{0,step,2*step})for(int x:xs){
        AnalysisLumaSample value;if(!sample(x,row(d),value))return failed();
        levels[count++]=value.luma;neutralCount+=neutral(value);
    }
    std::nth_element(levels.begin(),levels.begin()+count/2,levels.begin()+count);
    out.blackFloor=levels[count/2];
    if(out.blackFloor>96 || neutralCount*100<count*95)return reject("plane-nonneutral-bar-floor");
    // Require contrast at THIS native row, with distributed scene support.
    // The wider nine-row window corroborates sharpness but cannot move the
    // edge to a neighboring row or turn the top of a caption into a plane.
    for(int i=0;i<columns;++i){
        std::array<int,9> values{};
        for(int k=0;k<9;++k){
            AnalysisLumaSample value;if(!sample(xs[i],row(depth-4+k),value))return failed();
            values[k]=value.luma;
        }
        const int jump=values[4]-values[3];
        const int neighboringRise=(std::max)({0,values[1]-values[0],values[8]-values[7]});
        if(jump>=8 && jump>neighboringRise*3/2+2){
            ++out.boundarySupport;out.boundaryZones|=1u<<(i/32);
        }
    }
    unsigned zones=0;for(unsigned bits=out.boundaryZones;bits;bits>>=1)zones+=bits&1;
    if(out.boundarySupport<8 || zones<2)return reject("plane-no-distributed-exact-boundary");
    size_t total=0,blackCount=0;
    for(int d=0;d<depth;d=(d+step>=depth && d!=depth-1)?depth-1:d+step){
        unsigned intrusion=0;
        for(int x:xs){
            AnalysisLumaSample value;if(!sample(x,row(d),value))return failed();
            const bool black=neutral(value) && std::abs(int(value.luma)-out.blackFloor)<=12;
            ++total;blackCount+=black;intrusion=black?0:intrusion+1;
            out.maxIntrusion=(std::max)(out.maxIntrusion,intrusion);
        }
    }
    out.blackFraction=total?double(blackCount)/total:0.0;
    if(out.blackFraction<0.85 || out.maxIntrusion>=columns/5)return reject("plane-current-bar-content-conflict");
    out.valid=true;out.reason="current-exact-cue-side-plane";return out;
}

