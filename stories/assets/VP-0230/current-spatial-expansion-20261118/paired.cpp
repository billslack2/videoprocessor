#include <ActivePictureEvidence.h>
#include <vprenderer/ExperimentalAspectActive.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <chrono>
using Clock=std::chrono::steady_clock;
int main(int argc,char**argv){ ActivePictureTransitionModel model; ActivePictureTransitionModel admissionModels[2]; ActivePictureBounds bases[2]; bool haveBase[2]{}; AlphaSourceCrop::OutwardPictureConfirmationState previousProof[2]; int failures=0; std::string previousDirectory; uint64_t previousSequence=0;
 std::cout<<"file,identity,legacy_kind,legacy_bounds,legacy_reason,legacy_samples,legacy_ms,new_kind,new_bounds,new_reason,new_samples,new_ms,new2ms_kind,new2ms_bounds,new2ms_reason,new2ms_samples,new2ms_ms,full_qualified,full_extent_score,adapter_kind,adapter_available,h_axis,v_axis,publish,stable,published_bounds,stable_bounds\n";
 for(int a=1;a<argc;++a){std::filesystem::path path=argv[a],txt=path;txt.replace_extension(".txt");std::ifstream meta(txt);std::string header,identity;std::getline(meta,header);std::getline(meta,identity);std::istringstream h(header);AnalysisLumaSource s;int format,encoding,color,top,bottom,script,range=0;h>>s.width>>s.height>>s.rowBytes>>s.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script>>range;if(!h)return 2;s.format=static_cast<AnalysisLumaFormat>(format);s.encoding=static_cast<VideoFrameEncoding>(encoding);s.colorspace=static_cast<ColorSpace>(color);s.range=static_cast<AnalysisLumaRange>(range);
 std::istringstream ids(identity);std::string version;uint64_t transport,sequence,source,ts,fmt,vp,renderer,domain,policy,continuity,duration;int nearbar,opt,disc,repeat,alignment;ids>>version>>transport>>sequence>>source>>ts>>fmt>>vp>>renderer>>domain>>policy>>continuity>>nearbar>>opt>>duration>>disc>>repeat>>alignment;if(!ids||version!="identity_v1")return 3;s.generation=transport;s.rgbBitAlignment=static_cast<uint8_t>(alignment);
 std::ifstream bin(path,std::ios::binary|std::ios::ate);auto n=bin.tellg();if(n<=0||n>128ll*1024*1024)return 4;std::vector<uint8_t> bytes(static_cast<size_t>(n));bin.seekg(0);bin.read((char*)bytes.data(),n);s.data=bytes.data();s.dataBytes=bytes.size();if(!bin||!s.IsValid())return 5;
 if(a==7||a==10){std::ofstream pgm("preview-"+std::to_string(a)+".pgm",std::ios::binary);pgm<<"P5\n960 540\n255\n";for(int y=0;y<2160;y+=4)for(int x=0;x<3840;x+=4){AnalysisLumaSample pixel;s.Sample(x,y,pixel);unsigned char v=(unsigned char)std::min(255,std::max(0,(pixel.luma-64)*255/876));pgm.write((char*)&v,1);}} auto start=Clock::now();auto old=ExtractActivePictureEvidence(s);double oldms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();auto read=[&](int x,int y,int&v){AnalysisLumaSample p;if(!s.Sample(x,y,p))return false;v=p.luma;return true;};
 start=Clock::now();auto fresh=ExperimentalAspect::Measure(s.width,s.height,range==int(AnalysisLumaRange::Full)?0:64,read,[]{return true;});double newms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 start=Clock::now();auto bounded=ExperimentalAspect::Measure(s.width,s.height,range==int(AnalysisLumaRange::Full)?0:64,read,[&]{return Clock::now()-start<std::chrono::milliseconds(2);});double boundms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 auto b=old.trustedBounds;std::cout<<'"'<<path.string()<<"\",\""<<identity<<"\","<<int(old.classification)<<",\""<<b.left<<' '<<b.top<<' '<<b.right<<' '<<b.bottom<<"\",\""<<old.reason<<"\","<<old.lumaSamples<<','<<oldms;
 int idx=0;for(const auto&r:{fresh,bounded})std::cout<<','<<int(r.kind)<<",\""<<r.left<<' '<<r.top<<' '<<r.right<<' '<<r.bottom<<"\",\""<<r.reason<<"\","<<r.samples<<','<<(idx++==0?newms:boundms);auto adapted=ExperimentalAspect::EvidenceFromMeasurement(s,fresh); if(previousDirectory!=path.parent_path().string()||sequence!=previousSequence+1){model.Reset();for(int k=0;k<2;++k){admissionModels[k].Reset();haveBase[k]=false;previousProof[k]={};}} previousDirectory=path.parent_path().string();previousSequence=sequence;for(int mode=0;mode<2;++mode){
 AlphaSourceCrop::TransitionAdmissionDecision admitted;
 ActivePictureTransitionDecision accepted;
 if(!haveBase[mode])accepted=admissionModels[mode].Observe(MakeActivePictureObservation(adapted,sequence,10000000.0/duration));
 else {
  auto global=EvaluateActivePictureGlobalNearBlack(s);auto dark=EvaluateActivePictureArDarkness(s,adapted,global);
  auto retention=EvaluateActivePicturePresentationRetention(s,bases[mode],adapted,global,dark);
  ResampleActivePictureExpansionStrips(s,bases[mode],adapted.trustedBounds,retention);
  AlphaSourceCrop::TransitionAdmissionInput input;
  input.evidence=adapted;input.outwardCandidate=adapted.trustedBounds;input.presentationBeforeObservation=bases[mode];input.trustedGeometry=bases[mode];input.trustedGeometryAvailable=input.compatiblePresentation=true;
  input.trustedGeneration=input.sourceGeneration=transport;input.sourceSequence=sequence;input.framesPerSecond=10000000.0/duration;
  input.retention=retention;input.previousOutward=previousProof[mode];input.presentation.action=AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE;input.presentationEvidenceGeneration=transport;input.translationDriftActive=true;input.allowCurrentSpatialExpansion=mode==1;
  admitted=AlphaSourceCrop::EvaluateTransitionAdmission(input);previousProof[mode]=admitted.outward.state;
  accepted=admissionModels[mode].Observe(admitted.observation);
 }
 if(accepted.publish){bases[mode]=accepted.bounds;haveBase[mode]=true;}
 std::cerr<<"mode="<<mode<<" seq="<<sequence<<" strong="<<adapted.strongCurrentBarMeasurement<<" direct="<<admitted.currentSpatialExpansion<<" deferred="<<admitted.observation.transitionDeferred<<" publish="<<accepted.publish<<" bounds="<<accepted.bounds.left<<","<<accepted.bounds.top<<","<<accepted.bounds.right<<","<<accepted.bounds.bottom<<" reason="<<accepted.reason<<"\n";
 if(sequence==18400){bool pass=mode?admitted.currentSpatialExpansion&&accepted.publish&&accepted.bounds.top==68&&accepted.bounds.bottom==2092:!admitted.currentSpatialExpansion&&!accepted.publish;std::cerr<<(pass?"PASS":"FAIL")<<" first_expansion_mode="<<mode<<"\n";failures+=!pass;}
}auto decision=model.Observe(MakeActivePictureObservation(adapted,sequence,10000000.0/duration));std::cout<<','<<fresh.fullExtentQualified<<','<<fresh.fullExtentScore<<','<<int(adapted.classification)<<','<<adapted.available<<','<<int(adapted.axisEvidence.horizontal.state)<<','<<int(adapted.axisEvidence.vertical.state)<<','<<decision.publish<<','<<decision.stable<<",\""<<decision.bounds.left<<' '<<decision.bounds.top<<' '<<decision.bounds.right<<' '<<decision.bounds.bottom<<"\",\""<<decision.stableBounds.left<<' '<<decision.stableBounds.top<<' '<<decision.stableBounds.right<<' '<<decision.stableBounds.bottom<<"\"\n";
 }
return failures?1:0;
}
