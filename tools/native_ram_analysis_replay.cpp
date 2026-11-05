// Offline, serial source replay through the existing production native worker.
// This is analysis evidence only: no renderer, output crop, subtitle or deadline claim.
#include <SourcePictureDetector.h>
#include <vprenderer/SourcePictureSupportEpisode.h>
#include <vprenderer/OptionalAnalysisWorker.h>
#include <bcrypt.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
#pragma comment(lib,"bcrypt.lib")
namespace {
using Clock=std::chrono::steady_clock;
using Identity=std::array<uint64_t,16>;
void Require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
double Ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
std::string Quote(const std::string& text){std::ostringstream out;out<<'"';for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<c;else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;else out<<c;}out<<'"';return out.str();}
std::string Hash(const uint8_t* data,size_t size){BCRYPT_ALG_HANDLE h=nullptr;Require(BCryptOpenAlgorithmProvider(&h,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider unavailable");std::array<uint8_t,32> digest{};auto result=BCryptHash(h,nullptr,0,const_cast<PUCHAR>(data),static_cast<ULONG>(size),digest.data(),ULONG(digest.size()));BCryptCloseAlgorithmProvider(h,0);Require(result>=0,"SHA256 failed");std::ostringstream s;for(auto b:digest)s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return s.str();}
std::vector<uint8_t> Read(const std::filesystem::path& path,size_t maximum){std::ifstream in(path,std::ios::binary|std::ios::ate);Require(bool(in),"Native input unavailable");const auto n=in.tellg();Require(n>0&&uint64_t(n)<=maximum,"Native input exceeds bound");std::vector<uint8_t> bytes(static_cast<size_t>(n));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),n);Require(bool(in),"Native input truncated");return bytes;}
Identity ReadIdentity(std::istream& in){Identity id{};for(auto& v:id){std::string word;in>>word;Require(bool(in)&&!word.empty()&&word[0]!='-',"Invalid identity field");size_t used=0;v=std::stoull(word,&used);Require(used==word.size(),"Invalid identity integer");}return id;}
struct Input {unsigned index=0;Identity original{};std::array<int64_t,11> layout{};AnalysisLumaSource source;ActivePictureFrameIdentity identity;double readMs=0;};
struct Result {SourcePictureDetectionResult detected;bool ordered=false;Clock::time_point started,finished;};
using Worker=OptionalAnalysisWorker<Result>;
void Rect(std::ostream& o,const ActivePictureBounds& b){o<<'['<<b.left<<','<<b.top<<','<<b.right<<','<<b.bottom<<']';}
void IdentityJson(std::ostream& o,const Identity& id){o<<'[';for(size_t n=0;n<id.size();++n){if(n)o<<',';o<<id[n];}o<<']';}
}
int wmain(int argc,wchar_t** argv){
 try {
  Require(argc==5,"Usage: NativeRamAnalysisReplay CAPTURE_ROOT CAPTURE_MANIFEST INPUT_PINS OUTPUT_JSONL");
  const std::filesystem::path root=argv[1],manifestPath=argv[2],pinsPath=argv[3],outputPath=argv[4];
  Require(!std::filesystem::exists(outputPath),"Output already exists");
  std::ifstream manifest(manifestPath);size_t count=0;manifest>>count;Require(bool(manifest)&&count>0&&count<=65536,"Invalid replay count");
  std::vector<std::pair<unsigned,Identity>> requests;unsigned last=0;
  for(size_t n=0;n<count;++n){unsigned index=0;manifest>>index;Require(bool(manifest)&&index>last&&index<=65536,"Manifest index is not original ordered ledger index");requests.push_back({index,ReadIdentity(manifest)});last=index;}
  std::string extra;Require(!(manifest>>extra),"Unexpected manifest data");
  std::map<unsigned,std::pair<std::string,std::string>> pins;std::ifstream pinFile(pinsPath);unsigned index=0;std::string binHash,sideHash;
  while(pinFile>>index>>binHash>>sideHash){Require(binHash.size()==64&&sideHash.size()==64&&pins.emplace(index,std::make_pair(binHash,sideHash)).second,"Invalid input pins");}
  Require(pinFile.eof()&&pins.size()==count,"Input pins do not cover manifest");
  std::ofstream out(outputPath,std::ios::binary);Require(bool(out),"Cannot create output");out<<std::setprecision(9)<<std::boolalpha;
  out<<"{\"kind\":\"scope\",\"schema\":1,\"mode\":\"serial-offline-production-worker\",\"inputCount\":"<<count
   <<",\"pixelsRewritten\":false,\"timestampsRewritten\":false,\"sidecarPicturePlanesConsumed\":false,\"gpu\":false,\"videoDeadlineClaim\":false,\"acceptedSceneStateModeled\":false}\n";
  std::unique_ptr<Worker> worker;std::shared_ptr<SourcePictureSupportEpisode> episode;std::unique_ptr<Input> previous;
  unsigned completed=0,rejected=0,failed=0,resets=0,gaps=0;uint64_t totalWorkUs=0,peakWorkUs=0;
  for(const auto& request:requests){
   Input input;input.index=request.first;input.original=request.second;auto readStart=Clock::now();
   std::ostringstream stem;stem<<"frame-"<<std::setw(6)<<std::setfill('0')<<input.index;
   const auto side=Read(root/(stem.str()+".txt"),4096);Require(Hash(side.data(),side.size())==pins.at(input.index).second,"Sidecar hash differs from exported pin");
   std::istringstream metadata(std::string(side.begin(),side.end()));for(auto& v:input.layout)metadata>>v;
   std::string version;metadata>>version;Require(bool(metadata)&&version=="identity_v1","Missing native layout/identity");Require(ReadIdentity(metadata)==input.original,"Sidecar differs from original manifest identity");Require(!(metadata>>extra),"Unexpected sidecar data");
   auto pixels=std::make_shared<std::vector<uint8_t>>(Read(root/(stem.str()+".bin"),128ull*1024*1024));
   Require(Hash(pixels->data(),pixels->size())==pins.at(input.index).first,"Native pixels differ from exported pin");
   const auto& l=input.layout;Require(l[0]>0&&l[0]<=8192&&l[1]>0&&l[1]<=4320&&l[2]>0&&l[3]>=0&&l[4]>=0&&l[4]<=5&&l[5]>=0&&l[5]<=int(VideoFrameEncoding::P010)&&l[6]>=0&&l[6]<=int(ColorSpace::BT_2020)&&l[10]>=0&&l[10]<=2,"Unsupported native layout");
   const auto& ids=input.original;Require(ids[0]&&ids[1]&&ids[4]&&ids[7]&&ids[12]&&ids[12]<=100000000&&ids[15]<=2,"Incomplete source identity/timing");
   input.identity={ids[0],ids[1],ids[2],ids[3],ids[4],ids[5],ids[6],ids[7]};
   auto& source=input.source;source.data=pixels->data();source.dataBytes=pixels->size();source.backingOwner=pixels;
   source.width=int(l[0]);source.height=int(l[1]);source.rowBytes=size_t(l[2]);source.chromaRowBytes=size_t(l[3]);source.format=AnalysisLumaFormat(l[4]);source.encoding=VideoFrameEncoding(l[5]);source.colorspace=ColorSpace(l[6]);source.range=AnalysisLumaRange(l[10]);source.rgbBitAlignment=uint8_t(ids[15]);source.generation=ids[4];Require(source.IsValid(),"Native source view invalid");
   input.readMs=Ms(readStart,Clock::now());
   bool contextReset=!previous,sequenceGap=false;std::string acceptedDelta="0",sourceDelta="0";
   if(previous){const auto& old=previous->original;for(unsigned n:{0u,4u,5u,6u,7u,8u,9u,12u,15u})contextReset|=old[n]!=ids[n];
    for(unsigned n:{0u,1u,2u,3u,4u,5u,6u,10u})contextReset|=previous->layout[n]!=l[n];contextReset|=ids[13]!=0;
    auto delta=[](uint64_t a,uint64_t b){return a>=b?std::to_string(a-b):"-"+std::to_string(b-a);}; acceptedDelta=delta(ids[1],old[1]);sourceDelta=delta(ids[2],old[2]);
    sequenceGap=acceptedDelta!="1" || ((ids[2]||old[2])&&sourceDelta!="1") || (ids[3]&&old[3]&&ids[3]<=old[3]);
   }
   if(contextReset){if(worker){worker->Stop();auto m=worker->Snapshot();totalWorkUs+=m.wallUs;peakWorkUs=std::max(peakWorkUs,m.wallPeakUs);}worker=std::make_unique<Worker>();episode=std::make_shared<SourcePictureSupportEpisode>();++resets;}
   if(sequenceGap)++gaps;
   auto slot=std::make_shared<Worker::Slot>();slot->completion=std::make_shared<AnalysisCompletionSignal>();const auto notice=slot->completion->Snapshot();
   const auto offered=Clock::now();unsigned attempts=0;bool admitted=false;
   // Explicit offline admission retry, not a runtime policy or deadline claim.
   for(;attempts<4&&!admitted;++attempts){admitted=worker->Submit(slot,ids[0],ids[1],ids[1],[source,id=input.identity,episode]{Result r;r.started=Clock::now();SourcePictureSupportRequest request;r.ordered=episode->Begin(source,id,request);r.detected=DetectPreparedSourcePictureBounded(source,id,r.ordered?&request:nullptr);if(r.ordered)episode->Complete(r.detected);r.finished=Clock::now();return r;});if(!admitted)Sleep(1);}
   if(admitted)slot->completion->WaitUntil(notice,Clock::now()+std::chrono::seconds(5));
   const auto ready=Worker::Ready(slot);const auto observed=Clock::now();
   out<<"{\"kind\":\"frame\",\"index\":"<<input.index<<",\"identity_v1\":";IdentityJson(out,ids);
   out<<",\"contextReset\":"<<contextReset<<",\"sequenceGap\":"<<sequenceGap<<",\"acceptedSequenceDelta\":"<<acceptedDelta<<",\"sourceFrameNumberDelta\":"<<sourceDelta
      <<",\"readAndHashMs\":"<<input.readMs<<",\"admissionAttempts\":"<<attempts<<",\"admitted\":"<<admitted<<",\"completed\":"<<bool(ready)<<",\"elapsedToObservationMs\":"<<Ms(offered,observed);
   if(ready){const auto& d=ready->detected;const auto& e=d.evidence;const auto& physical=e.physicalEdges;
    bool exact=e.currentSourceIdentityAvailable&&SameAnalysisFrameIdentity(e.currentSourceIdentity,input.identity);if(!exact)++failed;
    out<<",\"exactCurrentIdentity\":"<<exact<<",\"episodeAdmitted\":"<<ready->ordered<<",\"queueMs\":"<<Ms(offered,ready->started)<<",\"workMs\":"<<Ms(ready->started,ready->finished)
       <<",\"nominalFrameMs\":"<<double(ids[12])/10000<<",\"workExceedsNominalPeriod\":"<<(Ms(ready->started,ready->finished)>double(ids[12])/10000)
       <<",\"geometryReused\":"<<d.geometryReused<<",\"geometryLookupMs\":"<<d.geometryLookupMs<<",\"nativeAvailable\":"<<e.available<<",\"nativeClassification\":"<<int(e.classification)<<",\"nativeReason\":"<<Quote(e.reason)
       <<",\"physicalAvailable\":"<<physical.estimateAvailable<<",\"physicalConfidence\":"<<int(physical.confidence)<<",\"physicalReason\":"<<Quote(physical.reason?physical.reason:"")<<",\"physicalBounds\":";Rect(out,physical.estimate);
    out<<",\"physicalAspect\":"<<(physical.estimateAvailable?physical.estimate.aspectRatio:0)<<",\"nativeEvidenceBounds\":";Rect(out,e.trustedBounds);
    const auto support=d.support;const bool supportExact=support&&support->currentSupport.evaluated&&SameAnalysisFrameIdentity(support->currentSupport.identity,input.identity);if(support&&support->currentSupport.verified&&!supportExact)++failed;out<<" ,\"supportExactCurrentIdentity\":"<<supportExact<<",\"supportVerified\":"<<(support&&support->currentSupport.verified)<<",\"supportReason\":"<<Quote(support?support->currentSupport.reason:"support-episode-not-admitted")<<",\"supportBounds\":";Rect(out,support?support->currentSupport.samplingBounds:ActivePictureBounds{});
    out<<",\"retainedPhysicalCompatible\":"<<(support&&support->physicalHypothesisCompatible)<<",\"retainedPhysicalBounds\":";Rect(out,support?support->retainedPhysicalHypothesis:ActivePictureBounds{});out<<",\"nativeLumaSamples\":"<<e.lumaSamples;++completed;
   }else if(!admitted)++rejected;else{++failed;worker->Stop();worker.reset();episode.reset();}
   out<<"}\n";out.flush();Require(bool(out),"Ledger write failed");
   input.source={};previous=std::make_unique<Input>(std::move(input));
   if(!worker)previous.reset(); // Timed-out optional work cannot supply subsequent history.
  }
  if(worker){worker->Stop();auto m=worker->Snapshot();totalWorkUs+=m.wallUs;peakWorkUs=std::max(peakWorkUs,m.wallPeakUs);}
  out<<"{\"kind\":\"summary\",\"inputs\":"<<count<<",\"completed\":"<<completed<<",\"admissionRejected\":"<<rejected<<",\"failed\":"<<failed<<",\"contextEpisodes\":"<<resets<<",\"sequenceGaps\":"<<gaps<<",\"workerTotalMs\":"<<double(totalWorkUs)/1000<<",\"workerPeakMs\":"<<double(peakWorkUs)/1000<<",\"gpu\":false,\"outputCropApplied\":false,\"videoDeadlineClaim\":false}\n";out.close();Require(!out.fail(),"Ledger close failed");return failed||rejected?1:0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
