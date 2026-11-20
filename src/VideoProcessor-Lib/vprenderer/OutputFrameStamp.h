#pragma once
#include <d3d11.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>

namespace OutputFrameStamp {
// Three columns by five rows, high bit at the left.
inline unsigned Glyph(char c) {
    static const unsigned digits[]={0x7b6f,0x2c97,0x73e7,0x73cf,0x5bc9,
        0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    if(c>='0' && c<='9')return digits[c-'0'];
    switch(c){case 'G':return 0x796f;case 'S':return 0x79cf;case 'O':return 0x7b6f;default:return 0;}
}
class Bitmap {
    // Three uint64 values, three prefixes and two spaces, max 3x scaling.
    std::array<uint64_t,65*4*5*9> storage{};
public:
    bool Draw(ID3D11DeviceContext* context,ID3D11Texture2D* buffer,
        uint64_t generation,uint64_t selected,uint64_t output) {
        D3D11_TEXTURE2D_DESC desc{}; buffer->GetDesc(&desc);
        const bool fp=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
        const bool ten=desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM;
        if(!fp && !ten && desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM &&
           desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)return false;
        char text[66]{};
        const int length=std::snprintf(text,sizeof(text),"G%llu S%llu O%llu",
            static_cast<unsigned long long>(generation),static_cast<unsigned long long>(selected),
            static_cast<unsigned long long>(output));
        if(length<=0 || length>65)return false;
        const unsigned scale=(std::min)(3u,(std::max)(1u,desc.Height/360));
        const unsigned width=length*4*scale,height=5*scale;
        if(desc.Width<width+8 || desc.Height<height+8)return false;
        const unsigned stride=fp?8:4;
        auto* bytes=reinterpret_cast<uint8_t*>(storage.data());
        for(unsigned y=0;y<height;++y)for(int i=0;i<length;++i){
            const unsigned row=Glyph(text[i])>>(3*(4-y/scale));
            for(unsigned x=0;x<4*scale;++x){
                const bool ink=x/scale<3 && (row & (4u>>(x/scale)));
                auto* dest=bytes+(y*width+i*4*scale+x)*stride;
                if(fp){
                    const uint64_t code=ink?0x3c00380038003800ull:0x3c00000000000000ull;
                    std::memcpy(dest,&code,8);
                }else{
                    const uint32_t code=ten?(ink?0xe0080200u:0xc0000000u):(ink?0xffccccccu:0xff000000u);
                    std::memcpy(dest,&code,4);
                }
            }
        }
        const unsigned left=8,top=desc.Height/6;
        D3D11_BOX box{left,top,0,left+width,top+height,1};
        context->UpdateSubresource(buffer,0,&box,bytes,width*stride,0);
        return true;
    }
};
}

