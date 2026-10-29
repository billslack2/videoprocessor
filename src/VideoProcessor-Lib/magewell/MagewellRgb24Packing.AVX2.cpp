#include "MagewellRgb24Packing.h"
#include <immintrin.h>

void MagewellRgb24Packing::ExpandAvx2(const uint8_t* source,
    uint32_t sourceStride,uint8_t* destination,uint32_t destinationStride,
    uint32_t width,uint32_t height) noexcept
{
    const auto words=_mm256_setr_epi32(0,1,2,3,3,4,5,6);
    const auto bytes=_mm256_setr_epi8(0,1,2,-1,3,4,5,-1,6,7,8,-1,9,10,11,-1,
        0,1,2,-1,3,4,5,-1,6,7,8,-1,9,10,11,-1);
    const auto alpha=_mm256_set1_epi32(0xff000000);
    bool streamed=false;
    for(uint32_t y=0;y<height;++y)
    {
        const auto* s=source+size_t(y)*sourceStride;
        auto* d=destination+size_t(y)*destinationStride;
        uint32_t x=0;
        // Heap buffers are at least 16-byte aligned. Other alignments are
        // safe through the scalar path. Streaming stores avoid reading the
        // overwritten alpha/destination cache lines across a full UHD frame.
        for(;x<width && (reinterpret_cast<uintptr_t>(d)&31);++x,s+=3,d+=4)
        { d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=255; }
        for(;uint64_t(x)+16<=width;x+=8,s+=24,d+=32)
        {
            // Load 32 bytes only when at least 16 pixels remain; never read
            // padding or the next row. The final pixels use the scalar tail.
            const auto raw=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
            const auto packed=_mm256_or_si256(_mm256_shuffle_epi8(
                _mm256_permutevar8x32_epi32(raw,words),bytes),alpha);
            _mm256_stream_si256(reinterpret_cast<__m256i*>(d),packed);
            streamed=true;
        }
        for(;x<width;++x,s+=3,d+=4)
        { d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=255; }
    }
    if(streamed) _mm_sfence(); // publish all non-temporal writes before callback/ownership transfer
    _mm256_zeroupper();
}
