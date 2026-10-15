#pragma once
#include <array>
#include <cmath>
#include <cstdint>

// Diagnostic state only: record every visible submitted geometry change,
// including a one-output excursion and its return. Source sequence/PTS are
// correlation metadata, never the change key. Failed outputs cannot overwrite
// the last geometry actually shown to the user.
struct SubmittedOutputGeometryTrace {
    using Rect=std::array<float,4>;
    bool Observe(bool submitted,uint64_t sourceGeneration,uint64_t viewportEpoch,
        const Rect& source,const Rect& target) {
        if(!submitted)return false;
        bool changed=!valid || generation!=sourceGeneration || epoch!=viewportEpoch;
        for(size_t i=0;i<4;++i)
            changed=changed || std::abs(source[i]-sourceCrop[i])>=0.5f ||
                std::abs(target[i]-targetCrop[i])>=0.5f;
        if(!changed)return false;
        valid=true; generation=sourceGeneration; epoch=viewportEpoch;
        sourceCrop=source; targetCrop=target;
        return true;
    }
private:
    bool valid=false;
    uint64_t generation=0,epoch=0;
    Rect sourceCrop{},targetCrop{};
};