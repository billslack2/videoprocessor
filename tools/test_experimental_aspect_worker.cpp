#include "../src/VideoProcessor-Lib/vprenderer/ExperimentalAspectShadow.h"
#include <cstdio>
#include <vector>
#include <string>
using Clock=std::chrono::steady_clock;
std::atomic<bool> entered{false},releaseLog{false};
std::atomic<unsigned> ownerReleasedOn{0};
std::mutex recordsMutex;
std::vector<std::string> records;
void __cdecl Sink(const char* text){
 {std::lock_guard<std::mutex> lock(recordsMutex);records.emplace_back(text);}
 entered=true;
 while(!releaseLog.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
int main(){
 int failures=0;auto check=[&](bool b,const char* s){std::printf("%s %s\n",b?"PASS":"FAIL",s);if(!b)++failures;};
 auto pixels=std::shared_ptr<std::vector<uint16_t>>(new std::vector<uint16_t>(1280*720*3/2,512<<6),
     [](std::vector<uint16_t>* p){ownerReleasedOn=GetCurrentThreadId();delete p;});
 for(int y=0;y<720;++y)for(int x=0;x<1280;++x)(*pixels)[y*1280+x]=(y<90||y>=630?64:300)<<6;
 PreparedAnalysisFrame frame;frame.source.data=reinterpret_cast<const uint8_t*>(pixels->data());
 frame.source.dataBytes=pixels->size()*2;frame.source.width=1280;frame.source.height=720;
 frame.source.rowBytes=2560;frame.source.chromaRowBytes=2560;frame.source.range=AnalysisLumaRange::Limited;frame.sourceOwner=pixels;
 frame.domain=AnalysisFrameDomain::ConvertedP010;frame.domainKey=42;
 ActivePictureFrameIdentity id;id.transportGeneration=7;id.acceptedSequence=123;id.captureTimestamp=456;id.analysisDomainKey=42;
 DebugLog::SetExternalSink(Sink);
 SetEnvironmentVariableA("VP_AR_EXPERIMENT",nullptr);
 {ExperimentalAspectShadow disabled;disabled.Offer(frame,id,{},ActivePictureClassification::UNAVAILABLE,false);}
 check(!entered,"disabled by default");
 SetEnvironmentVariableA("VP_AR_EXPERIMENT","shadow");
 {
  ExperimentalAspectShadow worker;
  worker.Offer(frame,id,{},ActivePictureClassification::UNAVAILABLE,false);
  auto deadline=Clock::now()+std::chrono::seconds(3);
  while(!entered&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  check(entered,"worker produces diagnostic with real P010 sampler");
  std::this_thread::sleep_for(std::chrono::milliseconds(260));
  auto start=Clock::now();worker.Offer(frame,id,{},ActivePictureClassification::UNAVAILABLE,false);
  auto us=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
  check(us<10000,"offer returns while optional worker is blocked");
  frame={};pixels.reset();
  check(ownerReleasedOn==0,"immutable source remains owned by active worker");
  releaseLog=true;
 }
 check(ownerReleasedOn!=0&&ownerReleasedOn!=GetCurrentThreadId(),"last source owner released on worker");
 check(records.size()==1,"busy offer dropped without backlog");
 if(!records.empty()){
  check(records[0].find("generation=7 source=123 capture=456")!=std::string::npos&&records[0].find("domain=42")!=std::string::npos,"exact source identity retained");
  check(records[0].find("kind=1 complete=1 bounds=0,90,1280,630")!=std::string::npos,"native P010 measured rectangle");
  std::puts(records[0].c_str());
 }
 DebugLog::SetExternalSink(nullptr);SetEnvironmentVariableA("VP_AR_EXPERIMENT",nullptr);
 return failures?1:0;
}
