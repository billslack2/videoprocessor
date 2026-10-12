#pragma once
#include <AnalysisLumaSource.h>
#include <SubtitleBoxDetector.h>
#include <array>
#include <algorithm>
#include <cstdlib>
namespace SubtitleTemporalFill {
using Signature=std::array<AnalysisLumaSample,32*18>;
inline bool Capture(const AnalysisLumaSource& source,Signature& out) {
 if(!source.IsValid())return false;
 for(int y=0;y<18;++y)for(int x=0;x<32;++x)
  if(!source.Sample((2*x+1)*source.width/64,(2*y+1)*source.height/36,out[y*32+x]))return false;
 return true;
}
inline bool Matches(const AnalysisLumaSource& source,const Signature& donor,
 const SubtitleBoxRect& cleanup,int pictureTop,int pictureBottom) {
 if(!source.IsValid() || !cleanup.Valid())return false;
 const int left=(std::max)(0,cleanup.left-192);
 const int right=(std::min)(source.width,cleanup.right+192);
 const int top=(std::max)(pictureTop,cleanup.top-120);
 const int bottom=(std::min)(pictureBottom,cleanup.bottom+120);
 unsigned count=0;unsigned long long difference=0;
 for(int y=0;y<18;++y)for(int x=0;x<32;++x) {
  const int px=(2*x+1)*source.width/64;
  const int py=(2*y+1)*source.height/36;
  if(px<left || px>=right || py<top || py>=bottom ||
   (px>=cleanup.left-4 && px<cleanup.right+4 &&
    py>=cleanup.top-4 && py<cleanup.bottom+4))continue;
  AnalysisLumaSample now;
  if(!source.Sample(px,py,now))return false;
  const auto& then=donor[y*32+x];
  const unsigned delta=std::abs(int(now.luma)-int(then.luma))+
   (std::abs(int(now.chromaU)-int(then.chromaU))+
    std::abs(int(now.chromaV)-int(then.chromaV)))/2;
  if(delta>120)return false;
  difference+=delta;++count;
 }
 return count>=8 && difference<=static_cast<unsigned long long>(count)*32;
}
}
