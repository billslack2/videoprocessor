#pragma once

#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <atomic>
#include <algorithm>
#include <utility>

// Reset intent, not presentation timing. One render-thread consumer observes
// all pending requests atomically. A full source reset cannot be downgraded by
// a later queue-only request before that consumer handles the boundary.
enum class QueueNativeResetKind : uint8_t { NONE=0, QUEUE_ONLY=1, FULL=2 };
class QueueNativeResetEpoch
{
public:
    void Request(QueueNativeResetKind kind=QueueNativeResetKind::FULL)
    {
        if(kind==QueueNativeResetKind::NONE)return;
        auto observed=m_requested.load(std::memory_order_relaxed);
        for(;;)
        {
            uint64_t epoch=(observed>>2)+1;
            if(epoch>=(uint64_t(1)<<62))epoch=1;
            const auto pending=observed&3;
            const uint64_t desired=(epoch<<2)|(std::max)(pending,uint64_t(kind));
            if(m_requested.compare_exchange_weak(observed,desired,
                std::memory_order_release,std::memory_order_relaxed))return;
        }
    }
    bool Consume(uint64_t& consumedEpoch,QueueNativeResetKind* kind=nullptr)
    {
        auto observed=m_requested.load(std::memory_order_acquire);
        for(;;)
        {
            const uint64_t epoch=observed>>2,pending=observed&3;
            if(epoch==consumedEpoch || pending==0)return false;
            if(m_requested.compare_exchange_weak(observed,epoch<<2,
                std::memory_order_acq_rel,std::memory_order_acquire))
            {
                consumedEpoch=epoch;
                if(kind)*kind=static_cast<QueueNativeResetKind>(pending);
                return true;
            }
        }
    }
private:
    std::atomic<uint64_t> m_requested{0};
};

// A request can race the already-dequeued last packet of the old queue.
// Preserve that packet's existing contract until the new generation arrives;
// source resets/discontinuities always invalidate immediately.
inline bool DeferQueueNativeResetUntilNewPacket(QueueNativeResetKind kind,
    uint64_t analyzedGeneration, uint64_t packetGeneration, bool discontinuity)
{
    return kind == QueueNativeResetKind::QUEUE_ONLY && !discontinuity &&
        analyzedGeneration != 0 && packetGeneration <= analyzedGeneration;
}

struct QueueNativeContinuityInput
{
    QueueNativeResetKind resetKind=QueueNativeResetKind::FULL;
    const ActivePictureTransitionModel* previousModel=nullptr;
    ActivePictureBounds previousNativeBounds;
    ActivePictureFrameIdentity previousIdentity,currentIdentity;
    ActivePictureFrameIdentity nativeEvidenceIdentity,retentionIdentity;
    ActivePictureEvidence nativeEvidence;
    ActivePicturePresentationRetentionEvidence retention;
    bool currentSourceValid=false,sourceDiscontinuity=false,darknessEvaluated=false;
    int width=0,height=0;
};
struct QueueNativeContinuityDecision
{
    bool retained=false;
    ActivePictureBounds nativeBounds;
    uint64_t sourceGeneration=0;
    ActivePictureTransitionModel model;
    const char* reason="not-evaluated";
};

inline QueueNativeContinuityDecision EvaluateQueueNativeContinuity(
    const QueueNativeContinuityInput& input)
{
    QueueNativeContinuityDecision result;
    auto reject=[&](const char* reason){result.reason=reason;return result;};
    const auto& before=input.previousIdentity;
    const auto& current=input.currentIdentity;
    const auto& native=input.previousNativeBounds;
    const auto sameAperture=[](const ActivePictureBounds& a,const ActivePictureBounds& b){
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;
    };
    const auto sameBounds=[&](const ActivePictureBounds& a,const ActivePictureBounds& b){
        return sameAperture(a,b) && a.trustedBarAxes==b.trustedBarAxes;
    };
    if(input.resetKind!=QueueNativeResetKind::QUEUE_ONLY || !input.previousModel)
        return reject("not-native-queue-only-reset");
    if(!input.currentSourceValid || input.sourceDiscontinuity ||
        !before.transportGeneration || !current.transportGeneration ||
        current.transportGeneration<=before.transportGeneration ||
        !before.acceptedSequence || !current.acceptedSequence ||
        !before.sourceFormatGeneration || before.sourceFormatGeneration!=current.sourceFormatGeneration ||
        !before.analysisDomainKey || before.analysisDomainKey!=current.analysisDomainKey ||
        before.viewportGeneration!=current.viewportGeneration ||
        before.rendererGeneration!=before.transportGeneration ||
        current.rendererGeneration!=current.transportGeneration ||
        !before.sourceFrameNumber || current.sourceFrameNumber<=before.sourceFrameNumber ||
        !before.captureTimestamp || current.captureTimestamp<=before.captureTimestamp ||
        input.width<=0 || input.height<=0 ||
        native.rasterWidth!=input.width || native.rasterHeight!=input.height)
        return reject("queue-reset-source-context-changed");
    if(!SameAnalysisFrameIdentity(current,input.nativeEvidenceIdentity) ||
        !SameAnalysisFrameIdentity(current,input.retentionIdentity))
        return reject("queue-reset-evidence-not-current");
    const auto& raw=input.nativeEvidence;
    if(!raw.available || raw.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
        raw.authorityOrigin!=ActivePictureAuthorityOrigin::NATIVE ||
        raw.sparseTransitionProof.available || raw.rememberedEdgeReturnProof.available ||
        raw.axisEvidence.verticalCropGlyphExclusions ||
        !raw.axisEvidence.horizontal.scanComplete || !raw.axisEvidence.vertical.scanComplete ||
        !sameBounds(raw.trustedBounds,native) || !sameAperture(raw.proposedBounds,native) ||
        raw.axisEvidence.HasBlockingFailedBar(native))
        return reject("queue-reset-native-bars-not-exact");
    const auto& pixels=input.retention;
    if(!input.darknessEvaluated || pixels.EffectiveArNearBlack() ||
        !pixels.analysisValid || !pixels.presentationValid || !pixels.currentlyPixelSafe ||
        !pixels.excludedBandsPixelSafe || !pixels.excludedHorizontalBandsPixelSafe ||
        !pixels.excludedVerticalBandsPixelSafe || pixels.outwardVisibleBoundsAvailable ||
        !pixels.proposedBoundsAvailable || !pixels.proposedBoundsContained ||
        !pixels.activePicture.available ||
        pixels.activePicture.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
        pixels.activePicture.authorityOrigin!=ActivePictureAuthorityOrigin::NATIVE ||
        pixels.activePicture.sparseTransitionProof.available ||
        pixels.activePicture.rememberedEdgeReturnProof.available ||
        pixels.activePicture.axisEvidence.verticalCropGlyphExclusions ||
        !sameBounds(pixels.activePicture.trustedBounds,native))
        return reject("queue-reset-current-pixels-not-strictly-safe");
    auto rebound=*input.previousModel;
    if(!rebound.ResetForNativeQueueContinuity(native))
        return reject("queue-reset-no-native-stable-model");
    result.retained=true;result.nativeBounds=native;
    result.sourceGeneration=current.transportGeneration;
    result.model=std::move(rebound);
    result.reason="exact-current-native-bars-revalidate-queue-continuity";
    return result;
}
