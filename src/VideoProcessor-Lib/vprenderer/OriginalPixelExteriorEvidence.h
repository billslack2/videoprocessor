#pragma once
#include <ActivePictureEvidence.h>
#include <algorithm>
#include <cmath>

// Current-source presentation evidence only: no AR or cleanup authority. Its
// matte tolerance intentionally permits small luma/chroma deviations.
struct OriginalPixelExteriorEvidence
{
    bool complete = false;
    bool upperDetail = false, lowerDetail = false;
    ActivePictureBounds bounds;
    int upperX = -1, upperY = -1, lowerX = -1, lowerY = -1;
    size_t rows = 0;
};
inline OriginalPixelExteriorEvidence InspectOriginalPixelExterior(
    const AnalysisLumaSource& source, const ActivePictureEvidence& native,
    const ActivePictureBounds& base, const AnalysisEvaluationBudget* budget)
{
    OriginalPixelExteriorEvidence out;
    if (!budget || !source.IsValid() || source.range == AnalysisLumaRange::Unknown ||
        source.width > 4096 || (source.format != AnalysisLumaFormat::P010 &&
        source.format != AnalysisLumaFormat::P210 &&
        source.format != AnalysisLumaFormat::DecodedDolbyVision422)) return out;
    const auto& measured = native.trustedBounds;
    const auto valid = [&](const ActivePictureBounds& b) {
        return b.rasterWidth == source.width && b.rasterHeight == source.height &&
            b.left == 0 && b.right == source.width && b.top >= 0 &&
            b.bottom <= source.height && b.top < b.bottom;
    };
    if (!valid(measured) || !valid(base) ||
        measured.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        base.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        measured.top <= 0 || measured.bottom >= source.height ||
        std::abs(measured.top-base.top) > 2 || std::abs(measured.bottom-base.bottom) > 2) return out;
    if (!native.available || !native.strongCurrentBarMeasurement ||
        native.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        native.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
        !native.top.trusted || !native.bottom.trusted ||
        native.top.continuity < 7.0/9.0 || native.bottom.continuity < 7.0/9.0 ||
        !std::isfinite(native.top.lumaP90) || !std::isfinite(native.bottom.lumaP90) ||
        native.top.lumaP90 < 0 || native.top.lumaP90 > 1023 ||
        native.bottom.lumaP90 < 0 || native.bottom.lumaP90 > 1023) return out;
    const int innerTop=std::min(measured.top,base.top);
    const int innerBottom=std::max(measured.bottom,base.bottom);
    const size_t exteriorRows=size_t(innerTop)+size_t(source.height-innerBottom);
    if (exteriorRows > size_t(2*1024*1024)/size_t(source.width)) return out;
    out.bounds=base;
    out.bounds.top=innerTop & ~1;
    out.bounds.bottom=std::min(source.height,(innerBottom+1)&~1);
    auto scan=[&](bool top, int limit, int matte, bool& detail, int& witnessX, int& witnessY) {
        for (int depth=0; depth<limit; ++depth) {
            budget->Checkpoint();
            const int y=top?depth:source.height-1-depth;
            int x=0;
            // Existing matte policy: +/-4 luma, +/-32 chroma.
            if (!source.FindPlanarMatteRowDetail(y,-1,0,matte,4,x)) return false;
            ++out.rows;
            if (x != source.width) {
                detail=true; witnessX=x; witnessY=y;
                if (top) out.bounds.top=y & ~1;
                else out.bounds.bottom=std::min(source.height,(y+2)&~1);
                break;
            }
        }
        return true;
    };
    if (!scan(true,innerTop,int(native.top.lumaP90),out.upperDetail,out.upperX,out.upperY) ||
        !scan(false,source.height-innerBottom,int(native.bottom.lumaP90),out.lowerDetail,out.lowerX,out.lowerY)) return {};
    out.bounds.aspectRatio=double(source.width)/(out.bounds.bottom-out.bounds.top);
    out.complete=true;
    return out;
}
