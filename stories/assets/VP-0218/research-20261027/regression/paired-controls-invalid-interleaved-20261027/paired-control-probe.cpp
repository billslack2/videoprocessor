#include <ActivePictureEvidence.h>
#include <AnalysisLumaSource.h>
#include <ColorSpace.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
int wmain(int argc,wchar_t** argv){
 if(argc!=3){std::wcerr<<L"usage: paired-control-probe <controls-dir> <csv>\n";return 2;}
 const std::wstring dir=argv[1]; std::ofstream out(argv[2]); out<<std::setprecision(12);
 out<<"kind,available,class,proposed_left,proposed_top,proposed_right,proposed_bottom,proposed_ar,trusted_axes,reason,global_nearblack,global_cutoff,ar_distributed_dark,retention_analysis_valid,retention_presentation_valid,retention_pixel_safe,retention_can_retain,retention_outward_visible,retention_outward_top,retention_outward_bottom,retention_reason\n";
 const wchar_t* names[]={L"gradient_matte",L"authored_ceiling_identical_pixels",L"fine_caption",L"colored_object",L"broad_low_contrast",L"gradient_noise"};
 int idx=0; const char* cnames[]={"gradient_matte","authored_ceiling_identical_pixels","fine_caption","colored_object","broad_low_contrast","gradient_noise"};
 for(const auto* name:names){
  const std::wstring path=dir+L"\\"+name+L".p210"; std::ifstream in(path,std::ios::binary|std::ios::ate); if(!in){std::wcerr<<L"open failed "<<path<<L"\n";return 3;} auto n=in.tellg();std::vector<uint8_t> bytes((size_t)n);in.seekg(0);in.read((char*)bytes.data(),n);if(!in)return 4;
  AnalysisLumaSource s; s.data=bytes.data();s.dataBytes=bytes.size();s.width=1280;s.height=720;s.rowBytes=2560;s.chromaRowBytes=2560;s.format=AnalysisLumaFormat::P210;s.encoding=VideoFrameEncoding::UNKNOWN;s.colorspace=ColorSpace::REC_709;s.generation=1;s.range=AnalysisLumaRange::Limited;
  auto raw=ExtractActivePictureEvidence(s);auto global=EvaluateActivePictureGlobalNearBlack(s);auto dark=EvaluateActivePictureArDarkness(s,raw,global);
  ActivePictureBounds b{0,130,1280,590,1280,720,double(1280)/460,ActivePictureBounds::BarAxes::TOP_BOTTOM};
  auto retention=EvaluateActivePicturePresentationRetention(s,b,raw,global,dark);
  out<<"\""<<cnames[idx++]<<"\","<<raw.available<<","<<int(raw.classification)<<","<<raw.proposedBounds.left<<","<<raw.proposedBounds.top<<","<<raw.proposedBounds.right<<","<<raw.proposedBounds.bottom<<","<<raw.proposedBounds.aspectRatio<<","<<int(raw.proposedBounds.trustedBarAxes)<<",\""<<raw.reason<<"\","<<global.nearBlack<<","<<global.lumaCutoff<<","<<dark.distributedDarkPicture<<","<<retention.analysisValid<<","<<retention.presentationValid<<","<<retention.currentlyPixelSafe<<","<<retention.CanRetainPresentation()<<","<<retention.outwardVisibleBoundsAvailable<<","<<retention.outwardVisibleBounds.top<<","<<retention.outwardVisibleBounds.bottom<<",\""<<retention.reason<<"\"\n";
 }
}


