#include <SubtitleBoxDetector.h>
#include <SubtitleBarEvidence.h>
#include "recovery-under-test.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>
int main(int argc,char** argv) {
    if(argc<2)return 2;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file),{});
    const int width=std::atoi(argv[2]),height=std::atoi(argv[3]);
    if(bytes.size()!=size_t(width)*height*4)return 3;
    for(int flipped=0;flipped<1;++flipped) {
        if(flipped)for(int y=0;y<height/2;++y)
            for(int x=0;x<width*4;++x)std::swap(bytes[size_t(y)*width*4+x],bytes[size_t(height-1-y)*width*4+x]);
        for(int edge=0;edge<2;++edge) {
            AnalysisLumaSource source;source.data=bytes.data();source.dataBytes=bytes.size();
            source.width=width;source.height=height;source.rowBytes=width*4;
            source.format=AnalysisLumaFormat::NativeRgb;source.encoding=VideoFrameEncoding::BGRA_8BIT;
            source.colorspace=ColorSpace::REC_709;source.generation=1;
            const int trustPad=argc>6?std::atoi(argv[6]):2; ActivePictureBounds trusted{0,std::atoi(argv[4])-trustPad,width,std::atoi(argv[5])+trustPad,width,height,0.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
            auto beginRecovery=std::chrono::steady_clock::now();
            auto recovered=RecoverSubtitleInspectionBars(source,trusted,1,ActivePictureClassification::PROVISIONAL,true,false);
            std::printf("recovered=%d,%d valid=%d recovery_ms=%.4f\n",recovered.top,recovered.bottom,recovered.rasterWidth!=0,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-beginRecovery).count());
            std::vector<double> recoveryTimes; for(int warm=0;warm<30;++warm) { auto begin=std::chrono::steady_clock::now(); auto measured=RecoverSubtitleInspectionBars(source,trusted,1,ActivePictureClassification::PROVISIONAL,true,false); if(warm>=5) recoveryTimes.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()); if(measured.rasterWidth!=recovered.rasterWidth) return 9; } std::sort(recoveryTimes.begin(),recoveryTimes.end()); std::printf("recovery_warm_median_ms=%.6f p95_ms=%.6f\n",recoveryTimes[recoveryTimes.size()/2],recoveryTimes[23]); if(recovered.rasterWidth) { SubtitleBoxDetector gated; gated.SetNearBarDistance(20); SubtitleBoxResult g; for(int iteration=0;iteration<5;++iteration) g=gated.Analyze(source,recovered.top,recovered.bottom,iteration+1,1); std::printf("gated=analyzed detected=%d lines=%d reason=%s bars=%d,%d\n",g.detected,g.lineCount,g.diagnosticReason,recovered.top,recovered.bottom); } else std::printf("gated=not-analyzed no-authority\n"); const auto bars=ExtractSubtitleBarEvidence(source);
            auto ref=RevalidateSubtitleBarEvidence(source,bars,std::atoi(argv[4]),std::atoi(argv[5]));
            std::printf("samples_extract=%u samples_reference=%u\n", unsigned(bars.samples),unsigned(ref.samples)); std::printf("bars=%d,%d available=%d ref=%d,%d fractions=%.4f,%.4f runs=%d,%d\n",bars.top,bars.bottom,bars.available,ref.topRevalidatedAtReference,ref.bottomRevalidatedAtReference,ref.topReferenceBlackFraction,ref.bottomReferenceBlackFraction,ref.topReferenceMaxIntrusion,ref.bottomReferenceMaxIntrusion);
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            SubtitleBoxResult result;std::vector<double> costs;
            for(int repeat=0;repeat<5;++repeat) {
                auto start=std::chrono::steady_clock::now();
                result=detector.Analyze(source,std::atoi(argv[4])+edge,std::atoi(argv[5])-edge,repeat+1,1);
                auto stop=std::chrono::steady_clock::now();
                if(repeat>=2)costs.push_back(std::chrono::duration<double,std::milli>(stop-start).count());
            }
            std::sort(costs.begin(),costs.end());double sum=0;for(auto value:costs)sum+=value;
            std::printf("flipped=%d edge=%d detected=%d lines=%d limit=%d bounds=%d,%d-%d,%d capture=%d,%d-%d,%d cleanup=%d,%d-%d,%d mean_ms=%.4f median_ms=%.4f max_ms=%.4f\n",
                flipped,edge,result.detected,result.lineCount,result.workLimit,
                result.bounds.left,result.bounds.top,result.bounds.right,result.bounds.bottom,
                result.capturePanel.left,result.capturePanel.top,result.capturePanel.right,result.capturePanel.bottom,
                result.sourcePanel.left,result.sourcePanel.top,result.sourcePanel.right,result.sourcePanel.bottom,
                sum/costs.size(),costs[costs.size()/2],costs.back());
            std::printf("reason=%s components=%u nearGap=%d inferred=%d\n", result.diagnosticReason, result.diagnosticComponentCount, result.diagnosticNearBarGap, result.diagnosticNearBarGapInferred); for(int row=0;row<result.lineCount;++row) {const auto& b=result.lineBounds[row];
                std::printf("line%d=%d,%d-%d,%d\n",row,b.left,b.top,b.right,b.bottom);}
        }
    }
}
