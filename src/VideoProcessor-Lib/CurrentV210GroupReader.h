#pragma once
#include <AnalysisLumaSource.h>
#include <cstring>

namespace subtitle_detail {
// One current detector/verification operation owns this reader. Only packed decoding is reused;
// source pixels, coordinates and all later glyph/cleanup predicates are unchanged.
class CurrentV210GroupReader {
public:
    explicit CurrentV210GroupReader(const AnalysisLumaSource& source)
        : source_(source),valid_(source.format==AnalysisLumaFormat::NativeYuv422 &&
            source.encoding==VideoFrameEncoding::V210 && source.IsValid()) {}
    bool IsValid() const {return valid_;}
    bool Sample(int x,int y,AnalysisLumaSample& out) {
        if(!valid_ || x<0 || y<0 || x>=source_.width || y>=source_.height)return false;
        const auto* group=source_.data+size_t(y)*source_.rowBytes+size_t(x/6)*16;
        if(group!=lastGroup_) {
            // V210's four little-endian words contain six luma and three UV pairs.
            uint32_t words[4];std::memcpy(words,group,sizeof(words));
            const auto code=[&](unsigned word,unsigned shift) {
                return uint16_t((words[word]>>shift)&0x3ffu);
            };
            pixels_[0]={code(0,10),code(0,0),code(0,20)};
            pixels_[1]={code(1,0),code(0,0),code(0,20)};
            pixels_[2]={code(1,20),code(1,10),code(2,0)};
            pixels_[3]={code(2,10),code(1,10),code(2,0)};
            pixels_[4]={code(3,0),code(2,20),code(3,10)};
            pixels_[5]={code(3,20),code(2,20),code(3,10)};
            lastGroup_=group;
        }
        out=pixels_[x%6];return true;
    }
private:
    const AnalysisLumaSource source_;
    const bool valid_;
    const uint8_t* lastGroup_=nullptr;
    AnalysisLumaSample pixels_[6];
};
}
