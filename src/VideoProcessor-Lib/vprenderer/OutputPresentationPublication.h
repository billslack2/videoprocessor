#pragma once
#include <RendererLiveness.h>
#include "AlphaOutputPresentationTelemetry.h"
#include <mutex>

// The renderer owns the observer. Only this small copy crosses to the OSD.
// Publication never waits for the UI and reading never takes renderMutex.
class OutputPresentationPublication
{
public:
    void Publish(const AlphaOutputPresentationSnapshot& observed, uint64_t lastWitnessTick)
    {
        RendererOutputPresentationStats next;
        next.supported = true;
        switch (observed.availability) {
        case AlphaOutputPresentationAvailability::Warming: next.state = RendererOutputPresentationState::Warming; break;
        case AlphaOutputPresentationAvailability::Observing: next.state = RendererOutputPresentationState::Observing; break;
        case AlphaOutputPresentationAvailability::Limited: next.state = RendererOutputPresentationState::Limited; break;
        default: next.state = RendererOutputPresentationState::Unavailable; break;
        }
        switch (observed.timingStatus) {
        case AlphaPresentationTimingStatus::NoSwapchain: next.reason = RendererOutputPresentationReason::NoSwapchain; break;
        case AlphaPresentationTimingStatus::FrameStatisticsUnavailable: next.reason = RendererOutputPresentationReason::Unsupported; break;
        case AlphaPresentationTimingStatus::FrameStatisticsFailed: next.reason = RendererOutputPresentationReason::StatisticsFailed; break;
        case AlphaPresentationTimingStatus::Disjoint: next.reason = RendererOutputPresentationReason::Disjoint; break;
        default:
            if (next.state == RendererOutputPresentationState::Limited)
                next.reason = RendererOutputPresentationReason::ObservationGap;
            break;
        }
        next.observedLongHolds = observed.observedLongHolds;
        next.excessRefreshes = observed.excessRefreshes;
        next.verifiedIntervals = observed.verifiedIntervals;
        next.observationGaps = observed.observationGaps;
        std::unique_lock<std::mutex> guard(mutex, std::try_to_lock);
        if (!guard.owns_lock()) return;
        published = next;
        witnessTick = lastWitnessTick;
    }

    RendererOutputPresentationStats Snapshot(uint64_t now) const
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto result = published;
        if (result.supported && result.reason != RendererOutputPresentationReason::Unsupported &&
            result.reason != RendererOutputPresentationReason::StatisticsFailed &&
            result.reason != RendererOutputPresentationReason::NoSwapchain &&
            result.reason != RendererOutputPresentationReason::Disjoint &&
            witnessTick && (now < witnessTick || now - witnessTick > 1000)) {
            result.state = RendererOutputPresentationState::Limited;
            result.reason = RendererOutputPresentationReason::Stale;
        }
        return result;
    }
private:
    mutable std::mutex mutex;
    RendererOutputPresentationStats published;
    uint64_t witnessTick = 0;
};
