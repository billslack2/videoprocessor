#include <ActivePictureEvidence.h>
#include <AnalysisLumaSource.h>
#include <ColorSpace.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
static const char* cls(ActivePictureClassification c){switch(c){case ActivePictureClassification::UNAVAILABLE:return "UNAVAILABLE";case ActivePictureClassification::FULL_RASTER_TRUSTED:return "FULL_RASTER_TRUSTED";case ActivePictureClassification::BAR_CROP_TRUSTED:return "BAR_CROP_TRUSTED";case ActivePictureClassification::PROVISIONAL:return "PROVISIONAL";}return "?";}
int wmain(int argc,wchar_t** argv){
 if(argc!=3){std::wcerr<<L"usage: paired-control-probe <controls-dir> <csv>\n";return 2;}
 const std::wstring dir=argv[1];std::ofstream out(argv[2]);out<<std::setprecision(12);
 out<<"kind,sampler_all_pixels_exact,checked_pixels,available,class,proposed_top,proposed_bottom,trusted_top,trusted_bottom,trusted_axes,reason,black_reference_mode,black_floor,black_threshold,global_nearblack,global_cutoff,ar_distributed_dark,retention_analysis_valid,retention_presentation_valid,retention_pixel_safe,retention_can_retain,retention_outward_visible,retention_outward_top,retention_outward_bottom,retention_reason\n";
 const wchar_t* names[]={L"neutral_gradient",L"neutral_gradient_grain_sigma1",L"subpixel_glyph_caption",L"faint_glyph_caption",L"sparse_stars",L"compact_logo",L"broad_smooth_object_plus8",L"structured_ceiling_panel",L"internal_letterbox_lookalike",L"boundary_subtitle"};
 const char* cnames[]={"neutral_gradient","neutral_gradient_grain_sigma1","subpixel_glyph_caption","faint_glyph_caption","sparse_stars","compact_logo","broad_smooth_object_plus8","structured_ceiling_panel","internal_letterbox_lookalike","boundary_subtitle"};int idx=0;
 for(const auto* name:names){
  const std::wstring path=dir+L"\\"+name+L".p210",expectedPath=dir+L"\\"+name+L".expected";
  std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in){std::wcerr<<L"open failed "<<path<<L"\n";return 3;}auto n=in.tellg();std::vector<uint8_t> bytes((size_t)n);in.seekg(0);in.read((char*)bytes.data(),n);if(!in)return 4;
  std::ifstream ex(expectedPath,std::ios::binary|std::ios::ate);if(!ex)return 5;auto en=ex.tellg();std::vector<int16_t> expected((size_t)en/sizeof(int16_t));ex.seekg(0);ex.read((char*)expected.data(),en);if(!ex||expected.size()!=3ull*3840*2160)return 6;
  AnalysisLumaSource s;s.data=bytes.data();s.dataBytes=bytes.size();s.width=3840;s.height=2160;s.rowBytes=7680;s.chromaRowBytes=7680;s.format=AnalysisLumaFormat::P210;s.encoding=VideoFrameEncoding::UNKNOWN;s.colorspace=ColorSpace::REC_709;s.generation=1;s.range=AnalysisLumaRange::Limited;
  bool exact=true;size_t checked=0;const size_t plane=3840ull*2160;
  for(int y=0;y<2160&&exact;++y)for(int x=0;x<3840;++x){AnalysisLumaSample v;if(!s.Sample(x,y,v)){exact=false;break;}size_t k=(size_t)y*3840+x;if(v.luma!=expected[k]||v.chromaU!=expected[plane+k]||v.chromaV!=expected[2*plane+k]){exact=false;break;}++checked;}
  if(!exact||checked!=plane){std::wcerr<<L"all-pixel P210 sampler roundtrip failed for "<<name<<L" checked="<<checked<<L"\n";return 7;}
  auto raw=ExtractActivePictureEvidence(s);auto global=EvaluateActivePictureGlobalNearBlack(s);auto dark=EvaluateActivePictureArDarkness(s,raw,global);
  ActivePictureBounds b{0,293,3840,1865,3840,2160,double(3840)/(1865-293),ActivePictureBounds::BarAxes::TOP_BOTTOM};auto retention=EvaluateActivePicturePresentationRetention(s,b,raw,global,dark);
  out<<"\""<<cnames[idx++]<<"\",1,"<<checked<<","<<raw.available<<","<<cls(raw.classification)<<","<<raw.proposedBounds.top<<","<<raw.proposedBounds.bottom<<","<<raw.trustedBounds.top<<","<<raw.trustedBounds.bottom<<","<<int(raw.trustedBounds.trustedBarAxes)<<",\""<<raw.reason<<"\",\""<<ActivePictureBlackReferenceModeName(raw.blackReference.mode)<<"\","<<raw.blackReference.floor<<','<<raw.blackReference.threshold<<','<<global.nearBlack<<","<<global.lumaCutoff<<","<<dark.distributedDarkPicture<<","<<retention.analysisValid<<","<<retention.presentationValid<<","<<retention.currentlyPixelSafe<<","<<retention.CanRetainPresentation()<<","<<retention.outwardVisibleBoundsAvailable<<","<<retention.outwardVisibleBounds.top<<","<<retention.outwardVisibleBounds.bottom<<",\""<<retention.reason<<"\"\n";
 }
}

