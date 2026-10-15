#pragma once

#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureTransitionModel.h>
#include <AnalysisLumaSource.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// A source-pixel certificate for undoing only Classic's final vertical
// translation. It never establishes caption absence, cleanup, or picture crop
// authority. Each bound contains only complete native rows inspected at every
// column; a foreground pixel stops that side before its row.
struct SubtitleBarStripClearance
{
    bool available = false;
    ActivePictureFrameIdentity identity;
    uint64_t policyGeneration = 0;
    uint64_t continuityGeneration = 0;
    int rasterWidth = 0, rasterHeight = 0;
    int pictureTop = 0, pictureBottom = 0;
    int clearTopFrom = 0;   // [clearTopFrom, pictureTop) was inspected.
    int clearBottomTo = 0;  // [pictureBottom, clearBottomTo) was inspected.
    int blackFloor = 0, lumaLimit = 0;
    size_t referenceSamples = 0, topSamples = 0, bottomSamples = 0;
    size_t samples = 0;
    double elapsedMs = 0.0;
};

// Unknown code range cannot use a threshold calibrated for full or limited
// native samples. A fixed per-side budget limits optional worker cost. A budget
// ending mid-row never certifies that row.
inline SubtitleBarStripClearance MeasureSubtitleBarStripClearance(
    const AnalysisLumaSource& source, const ActivePictureFrameIdentity& identity,
    uint64_t policyGeneration, uint64_t continuityGeneration,
    int pictureTop, int pictureBottom,
    size_t maxSamplesPerSide = 1048576)
{
    SubtitleBarStripClearance result;
    const auto started = std::chrono::steady_clock::now();
    if (!source.IsValid() || source.range == AnalysisLumaRange::Unknown ||
        source.width < 16 || source.height < 16 ||
        pictureTop < 0 || pictureBottom > source.height ||
        pictureTop >= pictureBottom || !identity.transportGeneration ||
        !identity.acceptedSequence || !identity.sourceFormatGeneration ||
        !identity.analysisDomainKey || !policyGeneration ||
        !continuityGeneration || !maxSamplesPerSide)
        return result;
    std::array<int,256> perimeter{};
    size_t count=0;
    auto reference=[&](int x,int y) {
        AnalysisLumaSample sample;
        if(!source.Sample(x,y,sample))return false;
        perimeter[count++]=sample.luma;
        return true;
    };
    for(int i=0;i<64;++i) {
        const int x=((i*2+1)*source.width)/128;
        const int y=((i*2+1)*source.height)/128;
        if(!reference(x,0) || !reference(x,source.height-1) ||
            !reference(0,y) || !reference(source.width-1,y))
            return SubtitleBarStripClearance{};
    }
    std::sort(perimeter.begin(),perimeter.end());
    const int observedLow=perimeter[25];
    // Never round the observed floor upward: doing so could hide a faint
    // stroke on a raised black bar (for example code 40 against code 60).
    const int floor=(std::min)(observedLow,80);
    // This full-pixel certificate is deliberately stricter than the sampled
    // active-picture retention profile. Dim or colored single-pixel strokes
    // must prevent removal of their source row.
    const int lumaLimit=floor+8;
    constexpr int chromaLimit=16;
    result.available=true;
    result.identity=identity;
    result.policyGeneration=policyGeneration;
    result.continuityGeneration=continuityGeneration;
    result.rasterWidth=source.width;
    result.rasterHeight=source.height;
    result.pictureTop=pictureTop;
    result.pictureBottom=pictureBottom;
    result.clearTopFrom=pictureTop;
    result.clearBottomTo=pictureBottom;
    result.blackFloor=floor;
    result.lumaLimit=lumaLimit;
    result.referenceSamples=count;

    auto clearRow=[&](int y,size_t& samples,size_t budget,bool& readError) {
        if(static_cast<size_t>(source.width)>budget-samples)return false;
        bool within=false;
        size_t examined=0;
        if(!source.RowWithinLimits(y,static_cast<uint16_t>(lumaLimit),
            chromaLimit,within,examined)) {readError=true;return false;}
        samples+=examined;
        return within;
    };
    bool readError=false;
    for(int y=pictureBottom;y<source.height;++y) {
        if(!clearRow(y,result.bottomSamples,maxSamplesPerSide,readError))break;
        result.clearBottomTo=y+1;
    }
    if(!readError)for(int y=pictureTop-1;y>=0;--y) {
        if(!clearRow(y,result.topSamples,maxSamplesPerSide,readError))break;
        result.clearTopFrom=y;
    }
    if(readError)return SubtitleBarStripClearance{};
    result.samples=result.referenceSamples+result.topSamples+result.bottomSamples;
    result.elapsedMs=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-started).count();
    return result;
}

inline bool SubtitleBarStripClearanceProvesCropUndo(
    const SubtitleBarStripClearance& clearance,
    const ActivePictureFrameIdentity& selectedIdentity,
    uint64_t policyGeneration, uint64_t continuityGeneration,
    const ActivePictureBounds& classicCrop,
    const ActivePictureBounds& physicalCrop)
{
    const auto valid=[&](const ActivePictureBounds& crop) {
        return crop.rasterWidth==clearance.rasterWidth &&
            crop.rasterHeight==clearance.rasterHeight &&
            crop.left>=0 && crop.top>=0 &&
            crop.right<=crop.rasterWidth && crop.bottom<=crop.rasterHeight &&
            crop.left<crop.right && crop.top<crop.bottom;
    };
    if(!clearance.available ||
        !SameAnalysisFrameIdentity(clearance.identity,selectedIdentity) ||
        clearance.policyGeneration!=policyGeneration ||
        clearance.continuityGeneration!=continuityGeneration ||
        !valid(classicCrop) || !valid(physicalCrop) ||
        physicalCrop.top>clearance.pictureTop ||
        physicalCrop.bottom<clearance.pictureBottom ||
        classicCrop.left!=physicalCrop.left ||
        classicCrop.right!=physicalCrop.right ||
        classicCrop.bottom-classicCrop.top!=
            physicalCrop.bottom-physicalCrop.top)
        return false;
    const int shift=classicCrop.top-physicalCrop.top;
    if(!shift || classicCrop.bottom-physicalCrop.bottom!=shift)return false;
    if(shift>0)
        return physicalCrop.bottom>=clearance.pictureBottom &&
            classicCrop.bottom<=clearance.clearBottomTo;
    return physicalCrop.top<=clearance.pictureTop &&
        classicCrop.top>=clearance.clearTopFrom;
}

// Kept separate from global no-caption or clean-pixel classification. This
// helper certifies only that undoing Classic's final translation discards
// already inspected black source rows.
inline bool ProvesSubtitlePresentationUndo(
    const SubtitleBarStripClearance& clearance,
    const ActivePictureFrameIdentity& selectedIdentity,
    uint64_t policyGeneration, uint64_t continuityGeneration,
    const ActivePictureBounds& classicCrop,
    const ActivePictureBounds& physicalCrop)
{
    return SubtitleBarStripClearanceProvesCropUndo(clearance,selectedIdentity,
        policyGeneration,continuityGeneration,classicCrop,physicalCrop);
}
