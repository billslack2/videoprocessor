#pragma once
#include <RendererLiveness.h>
#include "AlphaOutputPresentationTelemetry.h"
#include <mutex>
#include <atomic>

// A UI request cannot wait on rendering. Coalesce concurrent/reset retries to
// the newest host serial; neither zero nor a delayed request can rewind it.
class OutputPresentationResetRequest
{
public:
    void Request(uint64_t serial)
    {
        auto previous = requested.load(std::memory_order_relaxed);
        while (serial > previous && !requested.compare_exchange_weak(previous,
            serial, std::memory_order_release, std::memory_order_relaxed)) {}
    }
    uint64_t PendingAfter(uint64_t applied) const
    {
        const auto latest = requested.load(std::memory_order_acquire);
        return latest > applied ? latest : 0;
    }
private:
    std::atomic<uint64_t> requested{0};
};

// The renderer owns the observer. Only this small copy crosses to the OSD.
// Publication never waits for the UI and reading never takes renderMutex.
class OutputPresentationPublication
{
public:
    // Called only after the render-owned observer has actually been reset.
    // The watermark makes acknowledgement visible even if try-lock publication
    // loses to a UI reader and rendering exits before its next normal publish.
    void AcknowledgeReset(uint64_t serial) { appliedReset.Request(serial); }

    void Publish(const AlphaOutputPresentationSnapshot& observed, uint64_t lastWitnessTick,
        uint64_t appliedResetSerial = 0)
    {
        if (appliedReset.PendingAfter(appliedResetSerial)) return;
        RendererOutputPresentationStats next;
        next.supported = true;
        next.appliedResetSerial = appliedResetSerial;
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
        if (!guard.owns_lock() || next.appliedResetSerial < published.appliedResetSerial ||
            appliedReset.PendingAfter(next.appliedResetSerial)) return;
        published = next;
        witnessTick = lastWitnessTick;
    }

    RendererOutputPresentationStats Snapshot(uint64_t now) const
    {
        std::lock_guard<std::mutex> guard(mutex);
        auto result = published;
        if (const auto reset = appliedReset.PendingAfter(result.appliedResetSerial)) {
            result = {};
            result.supported = true;
            result.reason = RendererOutputPresentationReason::NoSwapchain;
            result.appliedResetSerial = reset;
            return result;
        }
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
    OutputPresentationResetRequest appliedReset;
    RendererOutputPresentationStats published;
    uint64_t witnessTick = 0;
};
