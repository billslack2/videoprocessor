#pragma once
#include <cstdint>
struct BenchConfig { int32_t width,height,format,encoding,colorspace,range,top,bottom;uint64_t rowBytes,chromaRowBytes; };
struct BenchOutcome { double wallMs;uint64_t cycles;uint64_t rawLuma,rawChroma,globalDarkLuma,retentionLuma,retentionChroma;int32_t classification,horizontalState,verticalState,bounds[4];int32_t calibrationMode,effectiveFloor,effectiveCutoff,retentionValid,retentionSafe,visible[4]; };
extern "C" {
__declspec(dllexport) void* BenchCreate(const uint8_t*,uint64_t,const BenchConfig*);
__declspec(dllexport) void BenchDestroy(void*);
__declspec(dllexport) int BenchMeasure(void*,int,BenchOutcome*);
}