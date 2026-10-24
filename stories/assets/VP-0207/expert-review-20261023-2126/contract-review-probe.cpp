#define NOMINMAX
#include <SubtitleCutPaste.h>
#include <SubtitleCueContinuation.h>
#include <fstream>
#include <cstdio>
#include <iterator>
static ActivePictureFrameIdentity Id(unsigned long long n){return {2,n,n,n*41708,2,3,4};}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
 std::ifstream meta(std::string(argv[1])+".txt");AnalysisLumaSource source;int format,encoding,color,top,bottom,hint,range;
 meta>>source.width>>source.height>>source.rowBytes>>source.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>hint>>range;
 source.data=bytes.data();source.dataBytes=bytes.size();source.generation=2;
 source.format=(AnalysisLumaFormat)format;source.encoding=(VideoFrameEncoding)encoding;source.colorspace=(ColorSpace)color;source.range=(AnalysisLumaRange)range;
 if(!source.IsValid())return 3;
 ActivePictureBounds base{0,376,3840,1784,3840,2160};
 SubtitlePictureAuthority shared;shared.required=true;shared.identity=Id(1);shared.bounds=base;
 SubtitleBoxDetector detector;auto original=SubtitleBoxLookahead::Measure(detector,source,ExtractActivePictureEvidence(source),Id(1),false,{},7,8,shared);
 printf("RAW capture: detected=%d lines=%d physical=%d:%d independent=%d nearby=%d/%d\n",original.text.detected,original.text.lineCount,original.pictureTop,original.pictureBottom,original.independentBarAnalysis,original.barEvidence.topRevalidatedNearby,original.barEvidence.bottomRevalidatedNearby);
 for(int mode=0;mode<3;++mode){
  // Replay the logged metadata conditions over FIXED current captured pixels.
  // This is a contract probe, not a fabricated prior captured frame.
  auto reference=original;reference.pictureTop=reference.barEvidence.top=384;reference.pictureBottom=reference.barEvidence.bottom=1776;
  reference.independentBarAnalysis=mode==0;reference.barTrackingAuthority=false;reference.currentAnchorUsesTrackedEdge=false;
  reference.barEvidence.topRevalidatedNearby=mode==1;reference.barEvidence.topRevalidatedAtReference=false;
  reference.barEvidence.bottomRevalidatedAtReference=reference.barEvidence.bottomRevalidatedNearby=false;
  reference.barEvidence.captionBackedTop=reference.barEvidence.captionBackedBottom=false;
  if(mode==2)reference.sharedPicture.bounds={};
  auto next=reference;next.identity=Id(2);next.sharedPicture.identity=Id(2);
  std::array<SubtitleBoxObservation,2> pair{reference,next};SubtitleBoxPresentation display;
  display.Consume(SubtitleBoxLookahead::Resolve(pair.data(),2,7,8));
  auto preview=SubtitleBoxLookahead::Resolve(&next,1,7,8);auto accepted=display.Consume(preview);
  SubtitleCueContinuationGuard guard;bool seeded=guard.Remember(preview,accepted,base,source);
  printf("CASE %d acquired=%d seed=%d reason=%s\n",mode,accepted.detected,seeded,guard.LastSeedReason());
  for(unsigned n=3;n<=10;++n){
   auto gap=preview;gap.current.identity=Id(n);gap.current.sharedPicture.identity=Id(n);
   gap.current.pictureTop=gap.current.barEvidence.top=0;gap.current.independentBarAnalysis=false;
   auto future=next;future.identity=Id(n+1);future.sharedPicture.identity=Id(n+1);
   std::array<SubtitleBoxObservation,2> mixedPlanes{gap.current,future};
   gap=SubtitleBoxLookahead::Resolve(mixedPlanes.data(),mixedPlanes.size(),7,8);
   auto proof=guard.Prepare(gap,source,base,250,41.708);
   auto visible=display.Consume(gap,250,41.708,nullptr,false,false,proof.valid?&proof:nullptr);
   printf(" frame=%u detected=%d verified=%d samples=%u elapsed=%.3f reason=%s visible=%d cue=%llu\n",n,gap.current.text.detected,proof.valid,proof.samples,proof.elapsedMs,proof.reason,visible.detected,visible.cue);
  }
 }
}
