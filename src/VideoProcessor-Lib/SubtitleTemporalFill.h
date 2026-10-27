#pragma once
#include <AnalysisLumaSource.h>
#include <SubtitleBoxDetector.h>
#include <SubtitleCutPaste.h>
#include <array>
#include <algorithm>
#include <cstdlib>
namespace SubtitleTemporalFill {
using Signature=std::array<AnalysisLumaSample,32*18>;
// Count consecutive certified source frames, not display refreshes. Unknown
// new input breaks the run; a repeated output neither advances nor breaks it.
inline unsigned AdvanceCleanRun(unsigned count,bool enabled,bool repeated,bool confirmedClean) {
 if(!enabled)return 0;
 if(repeated)return count;
 return confirmedClean?(std::min)(count,7u)+1u:0u;
}
// Enumerate exactly the current cleanup mask for donor restoration. The
// conservative exclusion envelope never becomes an erase rectangle. Complete
// validation precedes every callback so a malformed late row cannot partially
// modify a render target or fall back to a larger rectangle.
template<class Callback>
inline bool ForEachCleanupRect(const SubtitleCutPasteGeometry& geometry,Callback&& callback) {
 const auto& common=geometry.generatedCleanup;
 const auto& exclusion=geometry.generatedCleanupExclusion.Valid()?
  geometry.generatedCleanupExclusion:common;
 const auto contains=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
  return a.Valid() && b.Valid() && b.left>=a.left && b.top>=a.top &&
   b.right<=a.right && b.bottom<=a.bottom;
 };
 const auto inPicture=[&](const SubtitleBoxRect& r) {
  return r.Valid() && r.left>=0 && r.right<=65536 &&
   r.top>=geometry.pictureTop && r.bottom<=geometry.pictureBottom;
 };
 if(!geometry.valid || geometry.pictureTop<0 || geometry.pictureBottom>65536 ||
  geometry.pictureTop>=geometry.pictureBottom || !inPicture(common) ||
  !inPicture(exclusion) || !contains(exclusion,common) ||
  geometry.generatedCleanupRowCount>geometry.generatedCleanupRows.size())return false;
 const unsigned count=geometry.generatedCleanupRowCount;
 if(!count) {callback(common);return true;}
 int top=65536,bottom=0;
 for(unsigned i=0;i<count;++i) {
  const auto& row=geometry.generatedCleanupRows[i];
  if(!inPicture(row) || row.bottom!=row.top+1 || !contains(exclusion,row) ||
   row.left>common.left || row.right<common.right)return false;
  top=(std::min)(top,row.top);bottom=(std::max)(bottom,row.bottom);
 }
 if(bottom-top!=int(count) || common.top!=top || common.bottom!=bottom)return false;
 std::array<SubtitleBoxRect,128> rows{};
 for(unsigned i=0;i<count;++i) {
  const auto& row=geometry.generatedCleanupRows[i];
  auto& slot=rows[unsigned(row.top-top)];
  if(slot.Valid())return false;
  slot=row;
 }
 // Unique rows spanning exactly count native y positions are contiguous.
 // Coalescing changes only command count; it never fills a rounded corner.
 auto merged=rows[0];
 for(unsigned i=1;i<count;++i) {
  const auto& row=rows[i];
  if(row.left==merged.left && row.right==merged.right)merged.bottom=row.bottom;
  else {callback(merged);merged=row;}
 }
 callback(merged);return true;
}
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
// Matching excludes the full measured source backing, including its rounded
// shoulders and uncertain fringe, while restoration still uses only row spans.
inline bool Matches(const AnalysisLumaSource& source,const Signature& donor,
 const SubtitleCutPasteGeometry& geometry) {
 if(!source.IsValid() || !ForEachCleanupRect(geometry,[](const SubtitleBoxRect&){}))return false;
 const auto& exclusion=geometry.generatedCleanupExclusion.Valid()?
  geometry.generatedCleanupExclusion:geometry.generatedCleanup;
 if(exclusion.right>source.width || exclusion.bottom>source.height ||
  geometry.pictureBottom>source.height)return false;
 return Matches(source,donor,exclusion,geometry.pictureTop,geometry.pictureBottom);
}
}
