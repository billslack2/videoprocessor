#include <ActivePictureEvidence.h>
#include <iostream>
#include <vector>
#include <cstdint>
int main() {
 const int w=640,h=360; const size_t pitch=1280;
 const int floors[]={0,24,25,31,32,40,64,80,81,90,104,105,128};
 std::cout<<"range,bar_y,class,available,proposed_top,proposed_bottom,trusted_top,trusted_bottom,observed_floor,cutoff,reference,qualified_axes,nominal_axes,top_trusted,bottom_trusted,reason\n";
 for(auto range:{AnalysisLumaRange::Full,AnalysisLumaRange::Limited,AnalysisLumaRange::Unknown}) for(int level:floors) {
  std::vector<uint8_t> bytes(pitch*h*2,0);
  auto code=[&](size_t off,int v){const uint16_t c=uint16_t(v<<6);bytes[off]=uint8_t(c);bytes[off+1]=uint8_t(c>>8);};
  for(int y=0;y<h;y++)for(int x=0;x<w;x++)code(size_t(y)*pitch+x*2,(y<48||y>=h-48)?level:300);
  for(size_t off=pitch*h;off<bytes.size();off+=2)code(off,512);
  AnalysisLumaSource source{bytes.data(),bytes.size(),w,h,pitch,pitch,AnalysisLumaFormat::P210,VideoFrameEncoding::V210,ColorSpace::REC_709,1};source.range=range;
  const auto r=ExtractActivePictureEvidence(source);
  const char* name=r.classification==ActivePictureClassification::BAR_CROP_TRUSTED?"BAR_CROP_TRUSTED":r.classification==ActivePictureClassification::FULL_RASTER_TRUSTED?"FULL_RASTER_TRUSTED":r.classification==ActivePictureClassification::PROVISIONAL?"PROVISIONAL":"UNAVAILABLE";
  const char* rn=range==AnalysisLumaRange::Full?"Full":range==AnalysisLumaRange::Limited?"Limited":"Unknown";
  std::cout<<rn<<','<<level<<','<<name<<','<<r.available<<','<<r.proposedBounds.top<<','<<r.proposedBounds.bottom<<','<<r.trustedBounds.top<<','<<r.trustedBounds.bottom<<','<<r.blackReference.observedFloor<<','<<r.blackReference.observedThreshold<<','<<ActivePictureBlackReferenceModeName(r.blackReference.mode)<<','<<int(r.blackReference.qualifiedAxes)<<','<<int(r.blackReference.nominalFullExtentAxes)<<','<<r.top.trusted<<','<<r.bottom.trusted<<",\""<<r.reason<<"\"\n";
 }
}
