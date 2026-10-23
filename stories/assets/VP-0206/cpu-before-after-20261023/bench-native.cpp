#include "bench-wire.h"
#include <ActivePictureEvidence.h>
#include <windows.h>
#include <algorithm>
#include <memory>
#include <vector>
struct Context {
 std::shared_ptr<std::vector<uint8_t>> pixels;AnalysisLumaSource source;ActivePictureBounds base;
 ActivePictureEvidence raw;ActivePictureGlobalNearBlackEvidence global;ActivePictureArDarknessEvidence dark;
 LARGE_INTEGER frequency{};
};
extern "C" __declspec(dllexport) void* BenchCreate(const uint8_t* data,uint64_t bytes,const BenchConfig* c) {
 try { if(!data||!bytes||!c||bytes>128ull*1024*1024)return nullptr;auto p=std::make_unique<Context>();p->pixels=std::make_shared<std::vector<uint8_t>>(data,data+size_t(bytes));p->source={p->pixels->data(),p->pixels->size(),c->width,c->height,size_t(c->rowBytes),size_t(c->chromaRowBytes),static_cast<AnalysisLumaFormat>(c->format),static_cast<VideoFrameEncoding>(c->encoding),static_cast<ColorSpace>(c->colorspace),2};p->source.backingOwner=p->pixels;p->source.range=static_cast<AnalysisLumaRange>(c->range);if(!p->source.IsValid())return nullptr;p->base={0,c->top,c->width,c->bottom,c->width,c->height,double(c->width)/(c->bottom-c->top),ActivePictureBounds::BarAxes::TOP_BOTTOM};p->raw=ExtractActivePictureEvidence(p->source);p->global=EvaluateActivePictureGlobalNearBlack(p->source);p->dark=EvaluateActivePictureArDarkness(p->source,p->raw,p->global);QueryPerformanceFrequency(&p->frequency);return p.release();}catch(...){return nullptr;}
}
extern "C" __declspec(dllexport) void BenchDestroy(void* context) {delete static_cast<Context*>(context);}
void RawOutcome(BenchOutcome& out,const ActivePictureEvidence& r) {
 out.classification=int(r.classification);out.horizontalState=int(r.axisEvidence.horizontal.state);out.verticalState=int(r.axisEvidence.vertical.state);out.bounds[0]=r.proposedBounds.left;out.bounds[1]=r.proposedBounds.top;out.bounds[2]=r.proposedBounds.right;out.bounds[3]=r.proposedBounds.bottom;out.rawLuma=r.lumaSamples;out.rawChroma=r.chromaSamples;
#if VP_BENCH_AFTER
 out.calibrationMode=int(r.blackReference.mode);out.effectiveFloor=r.blackReference.floor;out.effectiveCutoff=r.blackReference.threshold;
#else
 out.calibrationMode=-1;out.effectiveFloor=-1;out.effectiveCutoff=-1;
#endif
}
extern "C" __declspec(dllexport) int BenchMeasure(void* context,int mode,BenchOutcome* result) {
 if(!context||!result||mode<0||mode>2)return 0;try {auto& p=*static_cast<Context*>(context);BenchOutcome out{};LARGE_INTEGER start{},end{};ULONG64 cyclesStart=0,cyclesEnd=0;ActivePictureEvidence raw;ActivePicturePresentationRetentionEvidence retention;uint64_t globalDarkLuma=mode==1?p.global.lumaSamples+p.dark.lumaSamples:0;QueryThreadCycleTime(GetCurrentThread(),&cyclesStart);QueryPerformanceCounter(&start);
 if(mode==0)raw=ExtractActivePictureEvidence(p.source);
 else if(mode==1)retention=EvaluateActivePicturePresentationRetention(p.source,p.base,p.raw,p.global,p.dark);
 else {raw=ExtractActivePictureEvidence(p.source);const auto global=EvaluateActivePictureGlobalNearBlack(p.source);const auto dark=EvaluateActivePictureArDarkness(p.source,raw,global);globalDarkLuma=global.lumaSamples+dark.lumaSamples;retention=EvaluateActivePicturePresentationRetention(p.source,p.base,raw,global,dark);}
 QueryPerformanceCounter(&end);QueryThreadCycleTime(GetCurrentThread(),&cyclesEnd);out.wallMs=double(end.QuadPart-start.QuadPart)*1000.0/p.frequency.QuadPart;out.cycles=cyclesEnd-cyclesStart;RawOutcome(out,mode==1?p.raw:raw);if(mode){out.globalDarkLuma=globalDarkLuma;out.retentionLuma=retention.lumaSamples-out.rawLuma-globalDarkLuma;out.retentionChroma=retention.chromaSamples-out.rawChroma;out.retentionValid=retention.analysisValid&&retention.presentationValid;out.retentionSafe=retention.CanRetainPresentation();out.visible[0]=retention.outwardVisibleBounds.left;out.visible[1]=retention.outwardVisibleBounds.top;out.visible[2]=retention.outwardVisibleBounds.right;out.visible[3]=retention.outwardVisibleBounds.bottom;}*result=out;return 1;}catch(...){return 0;}
}