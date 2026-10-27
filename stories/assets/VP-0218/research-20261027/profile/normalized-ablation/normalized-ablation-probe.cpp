#include <ActivePictureEvidence.h>
#include <AnalysisLumaSource.h>
#include <ColorSpace.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
static const int W=3840,H=2160;
static const char* cls(ActivePictureClassification c){switch(c){case ActivePictureClassification::UNAVAILABLE:return "UNAVAILABLE";case ActivePictureClassification::FULL_RASTER_TRUSTED:return "FULL_RASTER_TRUSTED";case ActivePictureClassification::BAR_CROP_TRUSTED:return "BAR_CROP_TRUSTED";case ActivePictureClassification::PROVISIONAL:return "PROVISIONAL";}return "?";}
int wmain(int argc,wchar_t** argv){
 if(argc!=3){std::wcerr<<L"usage: normalized-ablation-probe <input-dir> <csv>\n";return 2;}
 const std::wstring dir=argv[1];std::ofstream out(argv[2]);out<<std::setprecision(12);
 out<<"kind,sampler_all_pixels_exact,checked_pixels,available,class,proposed_top,proposed_bottom,trusted_top,trusted_bottom,trusted_axes,reason,global_nearblack,global_cutoff,ar_distributed_dark,retention_analysis_valid,retention_presentation_valid,retention_pixel_safe,retention_can_retain,retention_outward_visible,retention_outward_top,retention_outward_bottom,retention_reason\n";
 const wchar_t* names[]={L"native_frame-000001",L"native_frame-000002",L"native_frame-000003",L"native_frame-000004"};const char* cnames[]={"frame-000001","frame-000002","frame-000003","frame-000004"};int idx=0;const size_t plane=(size_t)W*H;
 for(const auto* name:names){
  const std::wstring path=dir+L"\\"+name+L".p210",expectedPath=dir+L"\\"+name+L".expected";
  std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in){std::wcerr<<L"open failed "<<path<<L"\n";return 3;}auto n=in.tellg();std::vector<uint8_t> bytes((size_t)n);in.seekg(0);in.read((char*)bytes.data(),n);if(!in)return 4;
  std::ifstream ex(expectedPath,std::ios::binary|std::ios::ate);if(!ex)return 5;auto en=ex.tellg();std::vector<int16_t> expected((size_t)en/sizeof(int16_t));ex.seekg(0);ex.read((char*)expected.data(),en);if(!ex||expected.size()!=3ull*plane)return 6;
  AnalysisLumaSource s;s.data=bytes.data();s.dataBytes=bytes.size();s.width=W;s.height=H;s.rowBytes=W*2;s.chromaRowBytes=W*2;s.format=AnalysisLumaFormat::P210;s.encoding=VideoFrameEncoding::UNKNOWN;s.colorspace=ColorSpace::REC_709;s.generation=1;s.range=AnalysisLumaRange::Limited;
  bool exact=true;size_t checked=0;
  for(int y=0;y<H&&exact;++y)for(int x=0;x<W;++x){AnalysisLumaSample v;if(!s.Sample(x,y,v)){exact=false;break;}size_t k=(size_t)y*W+x;if(v.luma!=expected[k]||v.chromaU!=expected[plane+k]||v.chromaV!=expected[2*plane+k]){exact=false;break;}++checked;}
  if(!exact||checked!=plane){std::wcerr<<L"all-pixel P210 sampler roundtrip failed for "<<name<<L" checked="<<checked<<L"\n";return 7;}
  auto raw=ExtractActivePictureEvidence(s);auto global=EvaluateActivePictureGlobalNearBlack(s);auto dark=EvaluateActivePictureArDarkness(s,raw,global);
  ActivePictureBounds candidate{0,293,W,1865,W,H,double(W)/(1865-293),ActivePictureBounds::BarAxes::TOP_BOTTOM};auto retention=EvaluateActivePicturePresentationRetention(s,candidate,raw,global,dark);
  out<<"\""<<cnames[idx++]<<"\",1,"<<checked<<","<<raw.available<<","<<cls(raw.classification)<<","<<raw.proposedBounds.top<<","<<raw.proposedBounds.bottom<<","<<raw.trustedBounds.top<<","<<raw.trustedBounds.bottom<<","<<int(raw.trustedBounds.trustedBarAxes)<<",\""<<raw.reason<<"\","<<global.nearBlack<<","<<global.lumaCutoff<<","<<dark.distributedDarkPicture<<","<<retention.analysisValid<<","<<retention.presentationValid<<","<<retention.currentlyPixelSafe<<","<<retention.CanRetainPresentation()<<","<<retention.outwardVisibleBoundsAvailable<<","<<retention.outwardVisibleBounds.top<<","<<retention.outwardVisibleBounds.bottom<<",\""<<retention.reason<<"\"\n";
 }
}
