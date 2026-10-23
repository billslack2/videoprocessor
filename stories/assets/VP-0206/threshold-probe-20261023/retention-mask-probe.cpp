#include <ActivePictureEvidence.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <memory>
#include <vector>
void edge(const char* name,const ActivePictureEdgeEvidence& e) {
 std::cout << "\""<<name<<"\":{\"pixels\":"<<e.barPixels<<",\"floor\":"<<e.lumaFloor<<",\"p90\":"<<e.lumaP90<<",\"black_fraction\":"<<e.blackFraction<<",\"continuity\":"<<e.continuity<<",\"texture\":"<<e.texture<<",\"mask\":[";
 for(int q=0;q<4;++q){if(q)std::cout<<',';std::cout<<unsigned(e.distributedSupportDepthRowsByQuartile[q]);} std::cout<<"]}";
}
int main(int argc,char** argv) {
 if(argc!=2)return 2;std::string path=argv[1];std::ifstream meta(path.substr(0,path.size()-4)+".txt");std::string line;std::getline(meta,line);std::istringstream header(line);
 AnalysisLumaSource s;int format=0,encoding=0,color=0,top=0,bottom=0,script=0,range=0;header>>s.width>>s.height>>s.rowBytes>>s.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script>>range;if(!header)return 3;
 std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)return 4;auto n=input.tellg();auto pixels=std::make_shared<std::vector<uint8_t>>(size_t(n));input.seekg(0);input.read(reinterpret_cast<char*>(pixels->data()),n);if(!input)return 5;
 s.data=pixels->data();s.dataBytes=pixels->size();s.backingOwner=pixels;s.format=static_cast<AnalysisLumaFormat>(format);s.encoding=static_cast<VideoFrameEncoding>(encoding);s.colorspace=static_cast<ColorSpace>(color);s.range=static_cast<AnalysisLumaRange>(range);s.generation=2;if(!s.IsValid())return 6;
 ActivePictureBounds base{0,274,3840,1886,3840,2160,3840.0/1612,ActivePictureBounds::BarAxes::TOP_BOTTOM};
 const auto raw=ExtractActivePictureEvidence(s);const auto global=EvaluateActivePictureGlobalNearBlack(s);const auto dark=EvaluateActivePictureArDarkness(s,raw,global);auto r=EvaluateActivePicturePresentationRetention(s,base,raw,global,dark);
 std::cout<<"{\"source_valid\":true,\"raw_class\":"<<int(raw.classification)<<",\"raw_bounds\":["<<raw.proposedBounds.left<<','<<raw.proposedBounds.top<<','<<raw.proposedBounds.right<<','<<raw.proposedBounds.bottom<<"],\"h_state\":"<<int(raw.axisEvidence.horizontal.state)<<",\"h_reason\":"<<int(raw.axisEvidence.horizontal.reason)<<",\"v_state\":"<<int(raw.axisEvidence.vertical.state)<<",\"v_reason\":"<<int(raw.axisEvidence.vertical.reason)<<",\"global_p90\":"<<global.lumaP90<<",\"near_black\":"<<global.nearBlack<<",\"black_floor\":"<<r.presentationBlackFloor<<",\"black_threshold\":"<<r.presentationBlackThreshold<<",\"bands_safe\":"<<r.excludedBandsPixelSafe<<",\"visible\":["<<r.outwardVisibleBounds.left<<','<<r.outwardVisibleBounds.top<<','<<r.outwardVisibleBounds.right<<','<<r.outwardVisibleBounds.bottom<<"],";
 edge("top",r.excludedTop);std::cout<<',';edge("bottom",r.excludedBottom);std::cout<<",\"depth_samples\":[";
 for(int d=0;d<6;++d){if(d)std::cout<<',';int depth=std::min(273,((d*2+1)*274)/12);int ys[2]={depth,s.height-1-depth};std::cout<<"{\"top_y\":"<<ys[0]<<",\"bottom_y\":"<<ys[1]<<",\"counts\":[";for(int edgeIndex=0;edgeIndex<2;++edgeIndex){if(edgeIndex)std::cout<<',';std::cout<<'[';for(int q=0;q<4;++q){if(q)std::cout<<',';int count=0;for(int i=q*12;i<(q+1)*12;++i){int x=((i*2+1)*s.width)/96;AnalysisLumaSample v;if(!s.Sample(x,ys[edgeIndex],v))return 7;count+=v.luma>r.presentationBlackThreshold;}std::cout<<count;}std::cout<<']';}std::cout<<"]}";}std::cout<<"]";
 ActivePictureBounds vertical{0,0,3840,2160,3840,2160,3840.0/2160,ActivePictureBounds::BarAxes::NONE};const bool sampled=ResampleActivePictureExpansionStrips(s,base,vertical,r);auto proof=AlphaSourceCrop::ConfirmOutwardPictureTransition({},base,vertical,r,2,10694);std::cout<<",\"vertical_resampled\":"<<sampled<<",\"vertical_broad\":"<<proof.broadOpposingPicture<<",\"vertical_native\":"<<proof.nativeDistributedPicture<<",\"vertical_reason\":\""<<proof.diagnosticReason<<"\"}\n";return 0;
}