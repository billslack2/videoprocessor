#include <pch.h>
#include "MagewellRgb24Packing.h"
#include <CpuFeatures.h>
#include <limits>

bool MagewellRgb24Packing::Expand(const uint8_t* source, size_t sourceBytes,
    uint32_t sourceStride, uint8_t* destination, size_t destinationBytes,
    uint32_t destinationStride, uint32_t width, uint32_t height,
    bool allowSimd) noexcept
{
    if (!source || !destination || !width || !height ||
        uint64_t(width)*3 > sourceStride || uint64_t(width)*4 > destinationStride)
        return false;
    const uint64_t sourceSpan=uint64_t(height-1)*sourceStride+uint64_t(width)*3;
    const uint64_t destinationSpan=uint64_t(height-1)*destinationStride+uint64_t(width)*4;
    if (sourceSpan>sourceBytes || destinationSpan>destinationBytes) return false;
    const auto src=reinterpret_cast<uintptr_t>(source), dst=reinterpret_cast<uintptr_t>(destination);
    const auto max=(std::numeric_limits<uintptr_t>::max)();
    if (sourceSpan>max-src || destinationSpan>max-dst ||
        (src<dst+destinationSpan && dst<src+sourceSpan)) return false;
    if (allowSimd && CpuFeatures::SupportsAvx2Kernels())
    {
        ExpandAvx2(source,sourceStride,destination,destinationStride,width,height);
        return true;
    }
    for(uint32_t y=0;y<height;++y)
    {
        const auto* s=source+size_t(y)*sourceStride;
        auto* d=destination+size_t(y)*destinationStride;
        for(uint32_t x=0;x<width;++x,s+=3,d+=4)
        { d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=255; }
    }
    return true;
}
