#include <SubtitleBoxLookahead.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
int main(){
 _setmode(_fileno(stdin),_O_BINARY);
 const int w=3840,h=2160; std::vector<uint8_t> bytes(size_t(w)*h*4);
 std::vector<SubtitleBoxObservation> observations;
 unsigned seq=0;std::vector<std::vector<uint8_t>> pixels;
 SubtitleBoxDetector detector; detector.SetNearBarDistance(20);
 std::cout<<"seq,detected,lines,top,capturetop,captureright,own,raw,upper_own,upper_raw,present,plines,ptop,cue,reason,ink_reason,coverage,difference,refresh_ms,refresh_samples,refresh_reason\n";
 SubtitleBoxPresentation presentation;
 auto emit=[&](){size_t i=0;
  const auto&o=observations[i];unsigned owned=0,raw=0,uowned=0,uraw=0;
  if(o.ink)for(int y=0;y<o.ink->height;++y)for(int x=0;x<o.ink->width;++x){bool a=o.ink->Get(x,y,true),b=o.ink->Get(x,y,false);owned+=a;raw+=b;if(o.ink->sourceRows[y]>=1480&&o.ink->sourceRows[y]<1650){uowned+=a;uraw+=b;}}
  auto p=SubtitleBoxLookahead::Resolve(observations.data()+i,(std::min)(size_t(4),observations.size()-i),1,1);
  AnalysisLumaSource current;current.data=pixels.front().data();current.dataBytes=pixels.front().size();current.width=w;current.height=h;current.rowBytes=w*4;current.format=AnalysisLumaFormat::NativeRgb;current.encoding=VideoFrameEncoding::BGRA_8BIT;current.colorspace=ColorSpace::REC_709;current.generation=1; const auto refreshStart=std::chrono::steady_clock::now();presentation.RefreshReferencePixels(p,current);const double refreshMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-refreshStart).count();
  const auto r=presentation.Consume(p,250,1000.0/23.976);const auto diag=presentation.InkDiagnostics();
  std::cout<<o.identity.acceptedSequence<<","<<o.text.detected<<","<<o.text.lineCount<<","<<o.text.bounds.top<<","<<o.text.capturePanel.top<<","<<o.text.capturePanel.right<<","<<owned<<","<<raw<<","<<uowned<<","<<uraw<<","<<r.detected<<","<<r.lineCount<<","<<r.bounds.top<<","<<r.cue<<","<<presentation.DecisionReason()<<","<<diag.reason<<","<<diag.minimumCoverage<<","<<diag.maximumDifference<<","<<refreshMs<<","<<presentation.ReferenceSampleCount()<<","<<presentation.ReferenceRefreshReason()<<"\n";
 };
 while(std::fread(bytes.data(),1,bytes.size(),stdin)==bytes.size()){
  SubtitleBoxObservation o; o.analyzed=o.barAuthority=true;o.width=w;o.height=h;o.pictureTop=276;o.pictureBottom=1884;o.policyGeneration=o.continuityGeneration=1;
  o.identity.transportGeneration=o.identity.sourceFormatGeneration=o.identity.viewportGeneration=o.identity.rendererGeneration=1;
  o.identity.acceptedSequence=++seq;o.identity.sourceFrameNumber=seq;
  o.barEvidence.available=true;o.barEvidence.top=276;o.barEvidence.bottom=1884;
  AnalysisLumaSource s;s.data=bytes.data();s.dataBytes=bytes.size();s.width=w;s.height=h;s.rowBytes=w*4;s.format=AnalysisLumaFormat::NativeRgb;s.encoding=VideoFrameEncoding::BGRA_8BIT;s.colorspace=ColorSpace::REC_709;s.generation=1;
  detector.Reset();o.text=detector.Analyze(s,276,1884,seq,1);o.ink=detector.InkSnapshot();observations.push_back(o);pixels.push_back(bytes);if(observations.size()==4){emit();observations.erase(observations.begin());pixels.erase(pixels.begin());}
 }
 while(!observations.empty()){emit();observations.erase(observations.begin());pixels.erase(pixels.begin());}
}
