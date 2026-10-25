#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// Passive statistics only. The producer copies immutable POD intent into a
// bounded SPSC ring; all event correlation and classification belongs on the
// collector thread. This header deliberately has no renderer/DXGI dependency.
namespace PassiveDisplayStatistics
{
enum class Disposition : uint8_t
{
    Displayed,
    Discarded,
    Unknown,
    Lost
};

enum class CoverageState : uint8_t
{
    Unavailable,
    Warming,
    Observing,
    Limited
};

// The render-producer atomics below must not fall back to library locks on the
// supported x64 MSVC target. C++14 does not provide is_always_lock_free, so
// use the implementation's compile-time lock-free contract and also exercise
// the exact atomic<size_t> specialization in the native test suite.
#if defined(_MSC_VER) && defined(_M_X64)
static_assert(ATOMIC_LLONG_LOCK_FREE == 2,
    "x64 render-producer 64-bit atomics must always be lock-free");
static_assert(sizeof(size_t) == sizeof(unsigned long long),
    "the x64 ring index must use a lock-free 64-bit atomic width");
#endif

// Written once by the render producer. QPC bounds identify the host submission
// call; output PTS and rates describe the intended cadence without claiming an
// absolute vblank phase.
struct OutputIntent
{
    uint32_t processId;
    uint32_t threadId;
    uintptr_t swapchainIdentity;
    uint64_t swapchainLifetime;
    uint64_t rendererEpoch;
    uint64_t sourceEpoch;
    uint64_t viewportEpoch;
    uint64_t resetEpoch;
    uint64_t outputSerial;
    int64_t submitStartQpc;
    int64_t submitEndQpc;
    int64_t qpcFrequency;
    double plannedOutputPts; // Seconds on the renderer timeline.
    double scheduledOutputHz;
    double displayHz;
    bool intentionalCorrection;
    bool renderFailed;
};

// Normalized from the passive display-event collector. Windows events do not
// carry VP's lifetime/reset epochs; the unique bounded match supplies them.
struct DisplayEvent
{
    uint32_t processId;
    uint32_t threadId;
    uintptr_t swapchainIdentity;
    int64_t presentStartQpc;
    int64_t screenQpc;
    int64_t qpcFrequency;
    Disposition disposition;
    uint32_t presentMode;
    double displayHz;
    bool displayRateValid;
};

// Counters are observed lower bounds. coverageGaps is cumulative since reset;
// state reports current observation freshness, not complete coverage.
struct HoldWitness
{
    uint64_t previousOutput = 0, output = 0;
    int64_t previousScreenQpc = 0, screenQpc = 0, frequency = 0;
    double ptsDelta = 0, displayHz = 0;
    uint64_t expectedRefreshes = 0, actualRefreshes = 0;
    uint32_t presentMode = 0;
};

struct Snapshot
{
    CoverageState state;
    uint64_t matchedOutcomes;
    uint64_t matchedDisplayed;
    uint64_t confirmedDrops;
    uint64_t unexpectedDrops;
    uint64_t holdIncidents;
    uint64_t excessRefreshes;
    uint64_t verifiedIntervals;
    uint64_t coverageGaps;
    uint64_t appliedResetSerial;
    size_t pendingIntents;
    size_t pendingEvents;
    bool resetPending;
    HoldWitness lastHold;
};

// Single-producer/single-consumer queue. A full queue rejects immediately and
// latches overflow; it never waits, allocates, or retries on the producer.
template <size_t Capacity>
class SpscIntentRing
{
    static_assert(Capacity > 0, "SPSC capacity must be nonzero");
    static_assert(std::is_trivially_copyable<OutputIntent>::value,
        "OutputIntent must remain a bounded POD copy");
public:
    bool TryPush(const OutputIntent& intent) noexcept
    {
        const size_t write = writeIndex_.load(std::memory_order_relaxed);
        const size_t read = readIndex_.load(std::memory_order_acquire);
        if (write - read >= Capacity)
        {
            // Producer serials are monotonic. Tagging the loss lets reset
            // application discard only pre-cutoff overflow while preserving
            // any overflow submitted after that reset.
            overflowOutputSerial_.store(intent.outputSerial, std::memory_order_release);
            return false;
        }
        slots_[write % Capacity] = intent;
        writeIndex_.store(write + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(OutputIntent& intent) noexcept
    {
        const size_t read = readIndex_.load(std::memory_order_relaxed);
        const size_t write = writeIndex_.load(std::memory_order_acquire);
        if (read == write)
            return false;
        intent = slots_[read % Capacity];
        readIndex_.store(read + 1, std::memory_order_release);
        return true;
    }

    uint64_t ConsumeOverflowOutputSerial() noexcept
    {
        return overflowOutputSerial_.exchange(0, std::memory_order_acq_rel);
    }

    bool ProducerAtomicsLockFree() const noexcept
    {
        return writeIndex_.is_lock_free() && readIndex_.is_lock_free() &&
            overflowOutputSerial_.is_lock_free();
    }

    size_t ApproximateSize() const noexcept
    {
        const size_t write = writeIndex_.load(std::memory_order_acquire);
        const size_t read = readIndex_.load(std::memory_order_acquire);
        return write - read;
    }

private:
    std::array<OutputIntent, Capacity> slots_{};
    alignas(64) std::atomic<size_t> writeIndex_{0};
    alignas(64) std::atomic<size_t> readIndex_{0};
    std::atomic<uint64_t> overflowOutputSerial_{0};
};

class Meter
{
public:
    static constexpr size_t Capacity = 256;
    static constexpr int64_t PendingExpirySeconds = 2;

    Meter() = default;
    Meter(const Meter&) = delete;
    Meter& operator=(const Meter&) = delete;

    // Render-thread producer API. One bounded copy into the ring; no locks,
    // allocation, waits, or collector callbacks.
    bool SubmitIntent(const OutputIntent& intent) noexcept
    {
        const uint64_t requested = requestedResetSerial_.load(std::memory_order_acquire);
        if (intent.resetEpoch > requested)
            RequestReset(intent.resetEpoch);

        // Publish the QPC floor before the serial so a reset reader that sees
        // the serial also sees its corresponding (or a newer) timestamp.
        const int64_t previousEnd = latestProducerEndQpc_.load(std::memory_order_relaxed);
        if (intent.submitEndQpc > previousEnd)
            latestProducerEndQpc_.store(intent.submitEndQpc, std::memory_order_relaxed);
        const uint64_t previousSerial = latestProducerSerial_.load(std::memory_order_relaxed);
        if (intent.outputSerial > previousSerial)
            latestProducerSerial_.store(intent.outputSerial, std::memory_order_release);
        return intents_.TryPush(intent);
    }

    // Render-producer-thread API, serialized with SubmitIntent(). Cutoffs are
    // published before the reset serial, so Drain() cannot observe a new reset
    // with an older watermark. No retry loop is used.
    void RequestReset(uint64_t serial) noexcept
    {
        RequestReset(serial, latestProducerEndQpc_.load(std::memory_order_relaxed));
    }

    void RequestReset(uint64_t serial, int64_t cutoffQpc) noexcept
    {
        if (!serial || serial <= requestedResetSerial_.load(std::memory_order_relaxed))
            return;
        // Publish a coherent reset watermark without making either thread
        // wait. Drain() samples this small seqlock once and defers if a reset
        // overlaps the read; the next scheduled Drain() retries.
        uint64_t sequence = resetPublicationSequence_.load(
            std::memory_order_seq_cst);
        if (sequence & 1)
            return; // RequestReset is serialized on the render producer.
        if (sequence > (std::numeric_limits<uint64_t>::max)() - 2)
            sequence = 0;
        resetPublicationSequence_.store(sequence + 1, std::memory_order_seq_cst);
        resetCutoffSerial_.store(
            latestProducerSerial_.load(std::memory_order_relaxed),
            std::memory_order_seq_cst);
        resetQpcFloor_.store(cutoffQpc, std::memory_order_seq_cst);
        publishedResetSerial_.store(serial, std::memory_order_seq_cst);
        resetPublicationSequence_.store(sequence + 2, std::memory_order_seq_cst);
        requestedResetSerial_.store(serial, std::memory_order_release);
    }

    bool ProducerAtomicsLockFree() const noexcept
    {
        return intents_.ProducerAtomicsLockFree() &&
            latestProducerSerial_.is_lock_free() &&
            latestProducerEndQpc_.is_lock_free() &&
            requestedResetSerial_.is_lock_free() &&
            resetCutoffSerial_.is_lock_free() &&
            resetQpcFloor_.is_lock_free() &&
            publishedResetSerial_.is_lock_free() &&
            resetPublicationSequence_.is_lock_free();
    }

    // Collector-thread API. Report a collector gap or unavailable source while
    // retaining already accumulated lower-bound counters.
    void ReportGap() noexcept
    {
        MarkGap();
        ClearPending();
    }

    void MarkUnavailable() noexcept
    {
        unavailable_ = true;
        ReportGap();
    }

    void MarkAvailable() noexcept
    {
        if (unavailable_)
        {
            unavailable_ = false;
            limited_ = true;
            hasAnchor_ = false;
        }
    }

    // Event arrival can precede the corresponding host
    // intent; it is buffered and reconciled when Drain() sees the intent.
    void ObserveEvent(const DisplayEvent& event) noexcept
    {
        if (event.presentStartQpc <= resetQpcFloorApplied_)
            return; // Expected late event from before the acknowledged reset.

        bool conflictingDuplicate = false;
        if (WasSeen(event, conflictingDuplicate))
        {
            if (conflictingDuplicate)
            {
                MarkGap();
                ClearPending();
            }
            return;
        }
        Remember(event);

        if (!ValidEventEnvelope(event))
        {
            MarkGap();
            return;
        }
        size_t slot = FindFreeEventSlot();
        if (slot == Capacity)
        {
            MarkGap();
            ClearPending();
            slot = FindFreeEventSlot();
        }
        if (slot == Capacity)
            return;
        events_[slot].used = true;
        events_[slot].event = event;
    }

    // Collector-thread API. nowQpc/frequency are optional; when supplied, old
    // unmatched evidence expires after a bounded window and creates a gap.
    void Drain(int64_t nowQpc = 0, int64_t qpcFrequency = 0) noexcept
    {
        ApplyPendingReset();

        // ApplyPendingReset can defer when its versioned watermark read races
        // a producer reset. Do not consume intents until that reset is applied.
        if (HasUnappliedReset())
            return;

        if (hasDeferredIntent_)
        {
            if (HasUnappliedReset())
                return;
            const OutputIntent deferred = deferredIntent_;
            hasDeferredIntent_ = false;
            AddIntent(deferred);
        }

        OutputIntent intent{};
        for (size_t popped = 0; popped < Capacity; )
        {
            if (HasUnappliedReset())
                return;
            if (!intents_.TryPop(intent))
                break;
            ++popped;

            // A reset can be published after the pre-pop check. Since a
            // post-reset intent can only enter the ring after its reset
            // publication, inspect both its epoch and the latest serial before
            // classifying it. Retain this one popped item for the next Drain.
            if (intent.resetEpoch > appliedResetSerial_ || HasUnappliedReset())
            {
                deferredIntent_ = intent;
                hasDeferredIntent_ = true;
                return;
            }
            AddIntent(intent);
        }

        ResolveEvents();
        ExpirePending(nowQpc, qpcFrequency);

        // Consume the overflow latch only after normal processing, and retain
        // its serial if a reset raced this Drain. The next Drain compares it to
        // the newly applied cutoff rather than losing a post-reset gap.
        const uint64_t overflowSerial = intents_.ConsumeOverflowOutputSerial();
        if (overflowSerial > deferredOverflowOutputSerial_)
            deferredOverflowOutputSerial_ = overflowSerial;
        if (HasUnappliedReset())
            return;
        if (IsPostResetOverflow(deferredOverflowOutputSerial_))
            MarkGap();
        deferredOverflowOutputSerial_ = 0;
    }

    // Read on the collector thread, then publish the small copy through the
    // application's existing nonblocking snapshot publication path.
    Snapshot ReadSnapshot() const noexcept
    {
        Snapshot result{};
        result.matchedOutcomes = matchedOutcomes_;
        result.matchedDisplayed = matchedDisplayed_;
        result.confirmedDrops = confirmedDrops_;
        result.unexpectedDrops = unexpectedDrops_;
        result.holdIncidents = holdIncidents_;
        result.excessRefreshes = excessRefreshes_;
        result.verifiedIntervals = verifiedIntervals_;
        result.coverageGaps = coverageGaps_;
        result.lastHold = lastHold_;
        result.appliedResetSerial = appliedResetSerial_;
        result.pendingIntents = CountUsedIntents() + (hasDeferredIntent_ ? 1 : 0);
        result.pendingEvents = CountUsedEvents();
        result.resetPending = requestedResetSerial_.load(std::memory_order_acquire) >
            appliedResetSerial_;
        if (unavailable_ || (!hasIntent_ && !result.pendingIntents))
            result.state = CoverageState::Unavailable;
        else if (result.resetPending || limited_)
            result.state = CoverageState::Limited;
        else if (hasAnchor_ && verifiedIntervals_)
            result.state = CoverageState::Observing;
        else
            result.state = CoverageState::Warming;
        return result;
    }

private:
    struct IntentEntry
    {
        OutputIntent intent{};
        bool used = false;
    };

    struct EventEntry
    {
        DisplayEvent event{};
        bool used = false;
    };

    struct SeenEntry
    {
        DisplayEvent event{};
        bool used = false;
    };

    struct Anchor
    {
        OutputIntent intent{};
        DisplayEvent event{};
    };

    static void Increment(uint64_t& value, uint64_t amount = 1) noexcept
    {
        const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
        value = amount > maximum - value ? maximum : value + amount;
    }

    static bool ValidRate(double hz) noexcept
    {
        return std::isfinite(hz) && hz >= 10.0 && hz <= 500.0;
    }

    static bool CloseRate(double a, double b) noexcept
    {
        return ValidRate(a) && ValidRate(b) &&
            std::abs(a - b) / (std::max)(a, b) <= 0.01;
    }

    static bool ValidIntentEnvelope(const OutputIntent& intent) noexcept
    {
        return intent.processId && intent.threadId && intent.swapchainIdentity &&
            intent.swapchainLifetime && intent.outputSerial &&
            intent.submitStartQpc > 0 &&
            intent.submitEndQpc >= intent.submitStartQpc &&
            intent.qpcFrequency > 0;
    }

    static bool ValidEventEnvelope(const DisplayEvent& event) noexcept
    {
        if (!event.processId || !event.threadId || !event.swapchainIdentity ||
            event.presentStartQpc <= 0 || event.qpcFrequency <= 0)
            return false;
        return event.disposition != Disposition::Displayed || event.screenQpc > 0;
    }

    static bool SameEventKey(const DisplayEvent& a, const DisplayEvent& b) noexcept
    {
        return a.processId == b.processId && a.threadId == b.threadId &&
            a.swapchainIdentity == b.swapchainIdentity &&
            a.presentStartQpc == b.presentStartQpc;
    }

    static bool SameEventValue(const DisplayEvent& a, const DisplayEvent& b) noexcept
    {
        return SameEventKey(a, b) && a.screenQpc == b.screenQpc &&
            a.qpcFrequency == b.qpcFrequency && a.disposition == b.disposition &&
            a.presentMode == b.presentMode && a.displayRateValid == b.displayRateValid &&
            a.displayHz == b.displayHz;
    }

    static bool SameContext(const OutputIntent& a, const OutputIntent& b) noexcept
    {
        return a.processId == b.processId && a.threadId == b.threadId &&
            a.swapchainIdentity == b.swapchainIdentity &&
            a.swapchainLifetime == b.swapchainLifetime &&
            a.rendererEpoch == b.rendererEpoch && a.sourceEpoch == b.sourceEpoch &&
            a.viewportEpoch == b.viewportEpoch && a.resetEpoch == b.resetEpoch &&
            a.qpcFrequency == b.qpcFrequency &&
            CloseRate(a.displayHz, b.displayHz) &&
            CloseRate(a.scheduledOutputHz, b.scheduledOutputHz);
    }

    static bool Matches(const OutputIntent& intent, const DisplayEvent& event) noexcept
    {
        return intent.processId == event.processId &&
            intent.threadId == event.threadId &&
            intent.swapchainIdentity == event.swapchainIdentity &&
            intent.qpcFrequency == event.qpcFrequency &&
            event.presentStartQpc >= intent.submitStartQpc &&
            event.presentStartQpc <= intent.submitEndQpc;
    }

    bool HasUnappliedReset() const noexcept
    {
        return requestedResetSerial_.load(std::memory_order_acquire) >
            appliedResetSerial_;
    }

    void ApplyPendingReset() noexcept
    {
        const uint64_t requested =
            requestedResetSerial_.load(std::memory_order_acquire);
        if (requested <= appliedResetSerial_)
            return;

        const uint64_t sequenceBefore =
            resetPublicationSequence_.load(std::memory_order_seq_cst);
        if (sequenceBefore & 1)
            return;
        const uint64_t publishedSerial =
            publishedResetSerial_.load(std::memory_order_seq_cst);
        const uint64_t cutoffSerial =
            resetCutoffSerial_.load(std::memory_order_seq_cst);
        const int64_t qpcFloor =
            resetQpcFloor_.load(std::memory_order_seq_cst);
        const uint64_t sequenceAfter =
            resetPublicationSequence_.load(std::memory_order_seq_cst);
        const uint64_t latestRequested =
            requestedResetSerial_.load(std::memory_order_acquire);
        if (sequenceBefore != sequenceAfter || (sequenceAfter & 1) ||
            publishedSerial != requested || latestRequested != requested)
            return;

        resetCutoffSerialApplied_ = cutoffSerial;
        resetQpcFloorApplied_ = qpcFloor;
        matchedOutcomes_ = matchedDisplayed_ = 0;
        confirmedDrops_ = unexpectedDrops_ = holdIncidents_ = excessRefreshes_ = 0;
        verifiedIntervals_ = coverageGaps_ = 0;
        lastHold_ = {};
        ClearPendingForReset(resetCutoffSerialApplied_, resetQpcFloorApplied_);
        if (deferredOverflowOutputSerial_ <= resetCutoffSerialApplied_)
            deferredOverflowOutputSerial_ = 0;
        ClearSeenBefore(resetQpcFloorApplied_);
        hasAnchor_ = false;
        appliedResetSerial_ = requested;
        limited_ = false;
    }

    bool IsPostResetOverflow(uint64_t outputSerial) const noexcept
    {
        // Zero means no overflow was latched. The render producer publishes
        // latestProducerSerial_ before pushing, so a reset's cutoff includes
        // every rejected pre-reset intent.
        return outputSerial != 0 && outputSerial > resetCutoffSerialApplied_;
    }

    void ClearPendingForReset(uint64_t cutoffSerial, int64_t qpcFloor) noexcept
    {
        uint64_t latestRetainedSerial = cutoffSerial;
        bool retainedIntent = false;
        for (auto& entry : intentEntries_)
        {
            if (entry.used && entry.intent.outputSerial <= cutoffSerial)
            {
                entry.used = false;
                continue;
            }
            if (entry.used)
            {
                retainedIntent = true;
                latestRetainedSerial = (std::max)(latestRetainedSerial,
                    entry.intent.outputSerial);
            }
        }
        for (auto& entry : events_)
            if (entry.used && entry.event.presentStartQpc <= qpcFloor)
                entry.used = false;

        const bool retainDeferred = hasDeferredIntent_ &&
            deferredIntent_.outputSerial > cutoffSerial;
        if (!retainDeferred)
            hasDeferredIntent_ = false;
        else
        {
            retainedIntent = true;
            latestRetainedSerial = (std::max)(latestRetainedSerial,
                deferredIntent_.outputSerial);
        }
        hasIntent_ = retainedIntent;
        lastOutputSerial_ = latestRetainedSerial;
    }

    void ClearPending() noexcept
    {
        for (auto& entry : intentEntries_)
            entry.used = false;
        for (auto& entry : events_)
            entry.used = false;
        hasDeferredIntent_ = false;
    }

    void ClearSeenBefore(int64_t qpcFloor) noexcept
    {
        for (auto& entry : seen_)
            if (entry.used && entry.event.presentStartQpc <= qpcFloor)
                entry.used = false;
        nextSeen_ = 0;
        while (nextSeen_ < Capacity && seen_[nextSeen_].used)
            ++nextSeen_;
        if (nextSeen_ == Capacity)
            nextSeen_ = 0;
    }

    void ClearSeen() noexcept
    {
        for (auto& entry : seen_)
            entry.used = false;
        nextSeen_ = 0;
    }

    void MarkGap() noexcept
    {
        Increment(coverageGaps_);
        limited_ = true;
        hasAnchor_ = false;
    }

    size_t FindFreeIntentSlot() const noexcept
    {
        for (size_t i = 0; i < Capacity; ++i)
            if (!intentEntries_[i].used)
                return i;
        return Capacity;
    }

    size_t FindFreeEventSlot() const noexcept
    {
        for (size_t i = 0; i < Capacity; ++i)
            if (!events_[i].used)
                return i;
        return Capacity;
    }

    size_t CountUsedIntents() const noexcept
    {
        size_t count = 0;
        for (const auto& entry : intentEntries_)
            count += entry.used ? 1 : 0;
        return count;
    }

    size_t CountUsedEvents() const noexcept
    {
        size_t count = 0;
        for (const auto& entry : events_)
            count += entry.used ? 1 : 0;
        return count;
    }

    bool WasSeen(const DisplayEvent& event, bool& conflicting) const noexcept
    {
        conflicting = false;
        for (const auto& entry : seen_)
        {
            if (!entry.used || !SameEventKey(entry.event, event))
                continue;
            conflicting = !SameEventValue(entry.event, event);
            return true;
        }
        return false;
    }

    void Remember(const DisplayEvent& event) noexcept
    {
        seen_[nextSeen_].event = event;
        seen_[nextSeen_].used = true;
        nextSeen_ = (nextSeen_ + 1) % Capacity;
    }

    void AddIntent(const OutputIntent& intent) noexcept
    {
        if (intent.outputSerial <= resetCutoffSerialApplied_ ||
            intent.resetEpoch < appliedResetSerial_)
            return;

        if (!ValidIntentEnvelope(intent))
        {
            MarkGap();
            return;
        }
        if (hasIntent_)
        {
            if (intent.outputSerial <= lastOutputSerial_)
            {
                MarkGap();
                return;
            }
            if (lastOutputSerial_ &&
                (lastOutputSerial_ == (std::numeric_limits<uint64_t>::max)() ||
                    intent.outputSerial != lastOutputSerial_ + 1))
                MarkGap();
        }

        lastOutputSerial_ = intent.outputSerial;
        hasIntent_ = true;

        size_t slot = FindFreeIntentSlot();
        if (slot == Capacity)
        {
            MarkGap();
            ClearPending();
            slot = FindFreeIntentSlot();
        }
        if (slot == Capacity)
            return;
        intentEntries_[slot].intent = intent;
        intentEntries_[slot].used = true;
    }

    void ResolveEvents() noexcept
    {
        // Each successful match consumes one event and one intent. Restarting
        // after a gap is cheap (fixed capacity) and avoids stale references.
        for (size_t pass = 0; pass < Capacity * 2; ++pass)
        {
            bool changed = false;
            for (size_t eventIndex = 0; eventIndex < Capacity; ++eventIndex)
            {
                if (!events_[eventIndex].used)
                    continue;
                const DisplayEvent event = events_[eventIndex].event;
                size_t matchIndex = Capacity;
                size_t matchCount = 0;
                for (size_t intentIndex = 0; intentIndex < Capacity; ++intentIndex)
                {
                    if (!intentEntries_[intentIndex].used ||
                        !Matches(intentEntries_[intentIndex].intent, event))
                        continue;
                    matchIndex = intentIndex;
                    ++matchCount;
                }
                if (matchCount > 1)
                {
                    MarkGap();
                    ClearPending();
                    changed = true;
                    break;
                }
                if (matchCount == 1)
                {
                    const OutputIntent intent = intentEntries_[matchIndex].intent;
                    // Preserve output order when the ETW consumer delivers later events first.
                    if (HasEarlierIntent(intent.outputSerial))
                        continue;
                    intentEntries_[matchIndex].used = false;
                    events_[eventIndex].used = false;
                    ProcessOutcome(intent, event);
                    changed = true;
                    break;
                }
            }
            if (!changed)
                break;
        }
    }

    bool HasEarlierIntent(uint64_t outputSerial) const noexcept
    {
        for (const auto& entry : intentEntries_)
            if (entry.used && entry.intent.outputSerial < outputSerial)
                return true;
        return false;
    }

    void ProcessOutcome(const OutputIntent& intent, const DisplayEvent& event) noexcept
    {
        Increment(matchedOutcomes_);
        if (event.disposition == Disposition::Displayed)
            Increment(matchedDisplayed_);
        if (event.disposition == Disposition::Discarded)
        {
            Increment(confirmedDrops_);
            if (!intent.intentionalCorrection || intent.renderFailed)
                Increment(unexpectedDrops_);
            MarkGap();
            return;
        }
        if (event.disposition == Disposition::Unknown ||
            event.disposition == Disposition::Lost)
        {
            MarkGap();
            return;
        }
        if (event.disposition != Disposition::Displayed)
        {
            MarkGap();
            return;
        }
        if (intent.intentionalCorrection || intent.renderFailed)
        {
            MarkGap();
            return;
        }

        if (!event.displayRateValid || !CloseRate(event.displayHz, intent.displayHz) ||
            !ValidRate(intent.scheduledOutputHz) ||
            !std::isfinite(intent.plannedOutputPts))
        {
            MarkGap();
            return;
        }

        if (hasAnchor_)
        {
            const OutputIntent& priorIntent = anchor_.intent;
            const DisplayEvent& priorEvent = anchor_.event;
            if (intent.outputSerial <= priorIntent.outputSerial ||
                event.screenQpc <= priorEvent.screenQpc)
            {
                MarkGap();
                return;
            }
            if (!SameContext(priorIntent, intent) ||
                priorEvent.presentMode != event.presentMode ||
                priorIntent.outputSerial == (std::numeric_limits<uint64_t>::max)() ||
                intent.outputSerial != priorIntent.outputSerial + 1 ||
                intent.plannedOutputPts <= priorIntent.plannedOutputPts ||
                event.qpcFrequency != intent.qpcFrequency ||
                priorEvent.qpcFrequency != intent.qpcFrequency)
            {
                MarkGap();
            }
            else
            {
                const double ptsDelta = intent.plannedOutputPts -
                    priorIntent.plannedOutputPts;
                const double screenSeconds = static_cast<double>(
                    event.screenQpc - priorEvent.screenQpc) /
                    static_cast<double>(intent.qpcFrequency);
                const double expectedValue = ptsDelta * intent.displayHz;
                const double actualValue = screenSeconds * intent.displayHz;
                if (!(std::isfinite(expectedValue) && std::isfinite(actualValue)) ||
                    expectedValue <= 0.0 || actualValue < 0.0 ||
                    expectedValue > 1000000.0 || actualValue > 1000000.0)
                {
                    MarkGap();
                }
                else
                {
                    // The ceil allowance is conservative because absolute vblank phase is unknown.
                    // Subtracting large timeline PTS values can put an exact integer cadence
                    // just above or below that integer by several representable-double steps.
                    // Snap only within a conservative arithmetic error bound; fractional cadence
                    // such as 59.94 -> 60 remains outside this tiny bound. If PTS precision is
                    // too poor to establish the bound, do not classify the interval.
                    const double ptsScale = (std::max)(1.0,
                        (std::max)(std::fabs(priorIntent.plannedOutputPts),
                            std::fabs(intent.plannedOutputPts)));
                    const double expectedError =
                        4.0 * std::numeric_limits<double>::epsilon() *
                            (ptsScale * intent.displayHz +
                                (std::max)(1.0, std::fabs(expectedValue)));
                    if (!std::isfinite(expectedError) || expectedError >= 0.01)
                    {
                        MarkGap();
                    }
                    else
                    {
                        double normalizedExpectedValue = expectedValue;
                        const double nearestInteger = std::floor(expectedValue + 0.5);
                        if (nearestInteger >= 1.0 && std::fabs(expectedValue - nearestInteger) <= expectedError)
                            normalizedExpectedValue = nearestInteger;

                        const uint64_t expectedRefreshes =
                            static_cast<uint64_t>(std::ceil(normalizedExpectedValue));
                        const uint64_t actualRefreshes = static_cast<uint64_t>(
                            std::floor(actualValue + 0.5));
                        Increment(verifiedIntervals_);
                        limited_ = false;
                        const uint64_t excess = actualRefreshes > expectedRefreshes
                            ? actualRefreshes - expectedRefreshes : 0;
                        if (excess)
                        {
                            Increment(holdIncidents_);
                            Increment(excessRefreshes_, excess);
                            lastHold_ = {priorIntent.outputSerial, intent.outputSerial,
                                priorEvent.screenQpc, event.screenQpc, intent.qpcFrequency,
                                ptsDelta, intent.displayHz, expectedRefreshes, actualRefreshes,
                                event.presentMode};
                        }
                    }
                }
            }
        }

        anchor_.intent = intent;
        anchor_.event = event;
        hasAnchor_ = true;
    }

    void ExpirePending(int64_t nowQpc, int64_t qpcFrequency) noexcept
    {
        if (nowQpc <= 0 || qpcFrequency <= 0)
            return;
        const long double timeout =
            static_cast<long double>(qpcFrequency) * PendingExpirySeconds;
        bool expired = false;
        for (auto& entry : intentEntries_)
        {
            if (entry.used && entry.intent.submitEndQpc > 0 &&
                static_cast<long double>(nowQpc) - entry.intent.submitEndQpc > timeout)
            {
                entry.used = false;
                expired = true;
            }
        }
        for (auto& entry : events_)
        {
            if (entry.used && entry.event.presentStartQpc > 0 &&
                static_cast<long double>(nowQpc) - entry.event.presentStartQpc > timeout)
            {
                entry.used = false;
                expired = true;
            }
        }
        if (expired)
            MarkGap();
    }

    SpscIntentRing<Capacity> intents_;
    std::array<IntentEntry, Capacity> intentEntries_{};
    std::array<EventEntry, Capacity> events_{};
    std::array<SeenEntry, Capacity> seen_{};
    size_t nextSeen_ = 0;

    std::atomic<uint64_t> latestProducerSerial_{0};
    std::atomic<int64_t> latestProducerEndQpc_{0};
    std::atomic<uint64_t> requestedResetSerial_{0};
    std::atomic<uint64_t> resetPublicationSequence_{0};
    std::atomic<uint64_t> publishedResetSerial_{0};
    std::atomic<uint64_t> resetCutoffSerial_{0};
    std::atomic<int64_t> resetQpcFloor_{0};

    uint64_t appliedResetSerial_ = 0;
    uint64_t resetCutoffSerialApplied_ = 0;
    int64_t resetQpcFloorApplied_ = 0;
    uint64_t lastOutputSerial_ = 0;
    uint64_t matchedOutcomes_ = 0;
    uint64_t matchedDisplayed_ = 0;
    uint64_t confirmedDrops_ = 0;
    uint64_t unexpectedDrops_ = 0;
    uint64_t holdIncidents_ = 0;
    uint64_t excessRefreshes_ = 0;
    uint64_t verifiedIntervals_ = 0;
    uint64_t coverageGaps_ = 0;
    bool hasIntent_ = false;
    bool hasAnchor_ = false;
    bool limited_ = false;
    bool unavailable_ = false;
    Anchor anchor_{};
    HoldWitness lastHold_{};
    OutputIntent deferredIntent_{};
    bool hasDeferredIntent_ = false;
    uint64_t deferredOverflowOutputSerial_ = 0;
};
}
