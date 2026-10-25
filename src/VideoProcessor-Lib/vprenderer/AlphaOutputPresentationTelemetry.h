#pragma once

#include "AlphaPresentationTelemetry.h"
#include <array>
#include <cstddef>
#include <cstdint>

// Observations are partial DXGI feedback, not an exact count of displayed
// drops/repeats. This observer never feeds presentation or cadence policy.
enum class AlphaOutputPresentationAvailability { Unavailable, Warming, Observing, Limited };

struct AlphaOutputSubmission {
    uint64_t generation=0, presentationEpoch=0;
    uint32_t presentId=0, intendedMaximumRefreshes=0;
    double expectedDisplayHz=0.0;
};

struct AlphaOutputPresentationSnapshot {
    AlphaOutputPresentationAvailability availability=AlphaOutputPresentationAvailability::Unavailable;
    AlphaPresentationTimingStatus timingStatus=AlphaPresentationTimingStatus::NoSwapchain;
    int32_t frameStatisticsResult=0;
    uint64_t observedLongHolds=0, excessRefreshes=0, verifiedIntervals=0, observationGaps=0;
};

class AlphaOutputPresentationTelemetry {
public:
    static constexpr size_t Capacity=256;
    // Conservative allowance only. For example 59.94 -> 60 allows two
    // refreshes; 24 -> 60 allows three. It does not predict cadence phase.
    static uint32_t IntendedMaximumRefreshes(double displayHz,double scheduledOutputHz);

    // Nonadvancing/duplicate IDs never overwrite the original submission.
    // True only for an admitted, unique submission with valid intent.
    bool RecordSubmission(const AlphaOutputSubmission& submission);
    // True only for a fresh exact matched observation accepted as an anchor.
    // A first anchor is warming; a verified adjacent interval is observing.
    // Duplicate polls return false without changing counters or freshness.
    bool Observe(const AlphaDxgiPresentationSample& sample);
    AlphaOutputPresentationSnapshot Snapshot() const { return m_snapshot; }

    // Timing reset: retain counters and any explicit counter-reset boundary.
    void ResetContinuity();
    // A real swapchain lifetime change permits its PresentID counter to restart.
    // Source generations, viewport changes and timing resets are not replacements.
    void ResetSwapchain();
    // Explicit counter reset: clear totals/receipts but reject old IDs from the
    // same swapchain, including a failed Present reporting its unchanged ID.
    void ResetSession();

private:
    static bool ValidRate(double hz);
    static bool SameRate(double a,double b);
    static bool SameIntent(const AlphaOutputSubmission& a,const AlphaOutputSubmission& b);
    static bool SamePoll(const AlphaDxgiPresentationSample& a,const AlphaDxgiPresentationSample& b);
    const AlphaOutputSubmission* Find(uint64_t generation,uint32_t presentId) const;
    void ClearRecordsAndAnchor();
    void BreakObservation(AlphaOutputPresentationAvailability availability);

    std::array<AlphaOutputSubmission,Capacity> m_records{};
    size_t m_next=0,m_count=0;
    bool m_hasSubmission=false,m_uncertainSubmission=false,m_hasPoll=false,m_hasAnchor=false;
    AlphaOutputSubmission m_lastSubmission{},m_anchorSubmission{};
    AlphaDxgiPresentationSample m_lastPoll{},m_anchorSample{};
    AlphaOutputPresentationSnapshot m_snapshot{};
    // Independent of source generation: DXGI PresentID belongs to the chain.
    uint32_t m_submissionHighWater=0,m_resetPresentCutoff=0;
};
