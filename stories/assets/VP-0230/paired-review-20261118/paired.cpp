#include <ActivePictureEvidence.h>
#include <ExperimentalAspectDetector.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <chrono>
using Clock=std::chrono::steady_clock;
int main(int argc,char**argv){
 std::cout<<"file,identity,legacy_kind,legacy_bounds,legacy_reason,legacy_samples,legacy_ms,new_kind,new_bounds,new_reason,new_samples,new_ms,new2ms_kind,new2ms_bounds,new2ms_reason,new2ms_samples,new2ms_ms\n";
 for(int a=1;a<argc;++a){std::filesystem::path path=argv[a],txt=path;txt.replace_extension(".txt");std::ifstream meta(txt);std::string header,identity;std::getline(meta,header);std::getline(meta,identity);std::istringstream h(header);AnalysisLumaSource s;int format,encoding,color,top,bottom,script,range=0;h>>s.width>>s.height>>s.rowBytes>>s.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script>>range;if(!h)return 2;s.format=static_cast<AnalysisLumaFormat>(format);s.encoding=static_cast<VideoFrameEncoding>(encoding);s.colorspace=static_cast<ColorSpace>(color);s.range=static_cast<AnalysisLumaRange>(range);
 std::istringstream ids(identity);std::string version;uint64_t transport,sequence,source,ts,fmt,vp,renderer,domain,policy,continuity,duration;int nearbar,opt,disc,repeat,alignment;ids>>version>>transport>>sequence>>source>>ts>>fmt>>vp>>renderer>>domain>>policy>>continuity>>nearbar>>opt>>duration>>disc>>repeat>>alignment;if(!ids||version!="identity_v1")return 3;s.generation=transport;s.rgbBitAlignment=static_cast<uint8_t>(alignment);
 std::ifstream bin(path,std::ios::binary|std::ios::ate);auto n=bin.tellg();if(n<=0||n>128ll*1024*1024)return 4;std::vector<uint8_t> bytes(static_cast<size_t>(n));bin.seekg(0);bin.read((char*)bytes.data(),n);s.data=bytes.data();s.dataBytes=bytes.size();if(!bin||!s.IsValid())return 5;
 if(a==7||a==10){std::ofstream pgm("preview-"+std::to_string(a)+".pgm",std::ios::binary);pgm<<"P5\n960 540\n255\n";for(int y=0;y<2160;y+=4)for(int x=0;x<3840;x+=4){AnalysisLumaSample pixel;s.Sample(x,y,pixel);unsigned char v=(unsigned char)std::min(255,std::max(0,(pixel.luma-64)*255/876));pgm.write((char*)&v,1);}} auto start=Clock::now();auto old=ExtractActivePictureEvidence(s);double oldms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();auto read=[&](int x,int y,int&v){AnalysisLumaSample p;if(!s.Sample(x,y,p))return false;v=p.luma;return true;};
 start=Clock::now();auto fresh=ExperimentalAspect::Measure(s.width,s.height,range==int(AnalysisLumaRange::Full)?0:64,read,[]{return true;});double newms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 start=Clock::now();auto bounded=ExperimentalAspect::Measure(s.width,s.height,range==int(AnalysisLumaRange::Full)?0:64,read,[&]{return Clock::now()-start<std::chrono::milliseconds(2);});double boundms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 auto b=old.trustedBounds;std::cout<<'"'<<path.string()<<"\",\""<<identity<<"\","<<int(old.classification)<<",\""<<b.left<<' '<<b.top<<' '<<b.right<<' '<<b.bottom<<"\",\""<<old.reason<<"\","<<old.lumaSamples<<','<<oldms;
 int idx=0;for(const auto&r:{fresh,bounded})std::cout<<','<<int(r.kind)<<",\""<<r.left<<' '<<r.top<<' '<<r.right<<' '<<r.bottom<<"\",\""<<r.reason<<"\","<<r.samples<<','<<(idx++==0?newms:boundms);std::cout<<'\n';
 }
}

