#pragma once

#include "AlphaSourceCropPolicy.h"
#include <libplacebo/renderer.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Pure output-time pose for one selected source packet. It never updates the
// source detector, subtitle lease, or the saved drift state.
struct TemporalPresentationPoseInput {
    AlphaSourceCrop::VerticalTranslationDrift drift{};
    AlphaSourceCrop::VerticalBarPresentationResolutionInput verticalInput{};
    pl_rect2df sourceCrop{};
    float requestedTranslation=0.0f;
    uint64_t sourceTick=0,driftDurationMs=0;
    int sourceRoutedTranslation=0;
    double sourcePts=0.0,outputPts=0.0,sourceDurationSeconds=0.0;
    bool translationRequested=false;
};

inline pl_rect2df ResolveTemporalPresentationCrop(const TemporalPresentationPoseInput& snapshot) {
    pl_rect2df crop=snapshot.sourceCrop;
    if (!snapshot.sourceTick || !snapshot.driftDurationMs) return crop;
    const double elapsedSeconds=(std::max)(0.0,(std::min)(
        snapshot.outputPts-snapshot.sourcePts,snapshot.sourceDurationSeconds));
    const uint64_t outputTick=snapshot.sourceTick+
        static_cast<uint64_t>(std::llround(elapsedSeconds*1000.0));
    auto pose=snapshot.drift;
    const float resolved=pose.Resolve(snapshot.requestedTranslation,
        outputTick,snapshot.driftDurationMs);
    auto input=snapshot.verticalInput;
    input.detailedAction=snapshot.translationRequested ||
        std::abs(resolved)>0.5f
        ? AlphaSourceCrop::VerticalBarPresentationAction::TRANSLATE
        : AlphaSourceCrop::VerticalBarPresentationAction::NONE;
    input.translationPixels=resolved;
    input.zeroTranslationRetainsTrustedBase=
        snapshot.translationRequested && pose.IsActive();
    const auto route=AlphaSourceCrop::ResolveVerticalBarRendererRouting(
        AlphaSourceCrop::ResolveVerticalBarPresentation(input));
    if (route.failOpen || route.fitActive) return crop;
    const int routed=route.translationActive ? route.translationPixels : 0;
    const float delta=static_cast<float>(routed-snapshot.sourceRoutedTranslation);
    const float top=crop.y0+delta;
    const float bottom=crop.y1+delta;
    if (top>=0.0f && bottom<=static_cast<float>(input.rasterHeight)) {
        crop.y0=top;
        crop.y1=bottom;
    }
    return crop;
}