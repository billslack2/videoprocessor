// Replay a captured native-resolution frame at source cadence through the
// production worker. Only input loading and deliberate source pacing may wait.
#include <SubtitleMeasurementWorker.h>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
int main(int argc,char** argv) {
    if(argc!=6)return 2;
    const int w=std::atoi(argv[2]),h=std::atoi(argv[3]),fps=std::atoi(argv[4]),count=std::atoi(argv[5]);
    if(w<64 || w>8192 || h<48 || h>4320 || fps<1 || fps>120 || count<6 || count>1000)return 2;
    auto pixels=std::make_shared<std::vector<uint8_t>>(size_t(w)*h*4);
    std::ifstream input(argv[1],std::ios::binary);input.read(reinterpret_cast<char*>(pixels->data()),pixels->size());
    if(!input)return 3;
    AnalysisLumaSource source{pixels->data(),pixels->size(),w,h,size_t(w)*4,0,
        AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::BGRA_8BIT,ColorSpace::REC_709,1};
    SubtitleMeasurementWorker worker;int misses=0,ready=0,detected=0;double maxPoll=0,totalScan=0;int maxPollFrame=-1;
    auto keyFor=[&](int i){SubtitleBoxObservation key;key.identity={1,uint64_t(i+1),uint64_t(i),uint64_t(i+1),1,1,1};
        key.width=w;key.height=h;key.nearBarDistance=20;key.policyGeneration=key.continuityGeneration=1;return key;};
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<count+5;++i) {
        std::this_thread::sleep_until(start+std::chrono::microseconds(int64_t(i)*1000000/fps));
        const auto poll=std::chrono::steady_clock::now();
        if(i<count) {
            const auto key=keyFor(i);
            worker.Submit(key,[pixels,source,key,fps](SubtitleMeasurementSampler& scanner,const SubtitleBarTrackingReference& previous){
                return scanner.Measure(source,key,previous,fps);
            });
        }
        if(i>=5) {
            SubtitleBoxObservation result;
            if(worker.TryTake(keyFor(i-5),result)) {++ready;detected+=result.text.detected;totalScan+=result.analysisMs;}
            else ++misses;
        }
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-poll).count();
        if(elapsed>maxPoll){maxPoll=elapsed;maxPollFrame=i;}
    }
    std::fprintf(stderr,"Peak submit/poll at source iteration %d (0 is lazy thread startup)\n",maxPollFrame);
    worker.Stop();
    std::printf("{\"frames\":%d,\"fps\":%d,\"ready\":%d,\"misses\":%d,\"detected\":%d,\"max_poll_ms\":%.4f,\"mean_worker_ms\":%.4f}\n",
        count,fps,ready,misses,detected,maxPoll,ready?totalScan/ready:0);
    return misses?1:0;
}
