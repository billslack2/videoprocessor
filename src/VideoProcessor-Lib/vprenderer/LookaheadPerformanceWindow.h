#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// One rendering owner updates these counters. This type does not sample clocks,
// allocate storage, format records, synchronize threads, or decide proof safety.
// Copy it at the existing low-rate reporting boundary, then Reset the window.
struct LookaheadPerformanceWindow
{
    enum class Stage : uint8_t
    {
        Measurements, Retention, InwardCaption, ExpansionProof,
        RelativeContrast, RememberedProof, Count
    };
    enum class StopReason : uint8_t
    {
        None, ZeroAdmission, Deadline, InputNotReady, QueueLock, RenderLock,
        PolicyChanged, IdentityMismatch, OwnerIneligible, InvalidEvidence,
        NoChange, Other, Count
    };
    enum class WorkOutcome : uint8_t { Complete, Cancelled, Deferred, Count };
    enum class ProofKind : uint8_t
    {
        ScheduledDecision, BufferedExpansion, InwardConfirmation, GuardedReturn, Count
    };
    enum class Rejection : uint8_t
    {
        IdentityMismatch, ReferenceMismatch, PolicyChanged, OwnerIneligible,
        InvalidEvidence, Expired, Other, Count
    };
    enum class AsyncOutcome : uint8_t { Ready, Late, Invalid, NoChange, Stale, Count };

    static constexpr size_t StageCount = static_cast<size_t>(Stage::Count);
    static constexpr size_t StopReasonCount = static_cast<size_t>(StopReason::Count);
    static constexpr size_t ProofKindCount = static_cast<size_t>(ProofKind::Count);
    static constexpr size_t RejectionCount = static_cast<size_t>(Rejection::Count);
    static constexpr size_t AsyncOutcomeCount = static_cast<size_t>(AsyncOutcome::Count);

    struct Metric
    {
        uint64_t count = 0;
        double total = 0, minimum = 0, maximum = 0;
        void Add(double value)
        {
            if (!count) minimum = maximum = value;
            else
            {
                if (value < minimum) minimum = value;
                if (value > maximum) maximum = value;
            }
            ++count;
            total += value;
        }
        double Average() const { return count ? total / static_cast<double>(count) : 0; }
    };

    struct Depth
    {
        uint64_t samples = 0, invalid = 0;
        uint64_t selectedTotal = 0, readyTotal = 0, requiredTotal = 0;
        uint64_t selectedPeak = 0, readyPeak = 0, requiredPeak = 0;
        uint64_t readyShortfall = 0, requiredShortfall = 0, noReady = 0;
    };
    struct StageStatistics
    {
        uint64_t lookups = 0, cacheHits = 0, cacheMisses = 0, zeroAdmissionHits = 0;
        uint64_t workCalls = 0, completed = 0, cancelled = 0, deferred = 0;
        uint64_t invalidElapsedSamples = 0;
        Metric elapsedMs;
        std::array<uint64_t, StopReasonCount> stops{};
    };
    struct ProofStatistics
    {
        uint64_t published = 0, adopted = 0, rejected = 0;
        std::array<uint64_t, RejectionCount> rejections{};
        std::array<uint64_t, AsyncOutcomeCount> async{};
        Metric effectiveLeadFrames, effectiveLeadMs;
    };

    Depth depth;
    std::array<StageStatistics, StageCount> stages{};
    std::array<ProofStatistics, ProofKindCount> proofs{};
    uint64_t independentRetained = 0, independentRetainedAfterCancellation = 0;
    uint64_t invalidEvents = 0;

    // All three values use the same units (e.g. source frames including current).
    // A reduced ready prefix is distinct from insufficient proof depth. Invalid
    // ready > selected samples are observable and excluded from depth totals.
    void ObserveDepth(uint64_t selected, uint64_t ready, uint64_t required)
    {
        ++depth.samples;
        if (ready > selected)
        {
            ++depth.invalid;
            ++invalidEvents;
            return;
        }
        depth.selectedTotal += selected;
        depth.readyTotal += ready;
        depth.requiredTotal += required;
        if (selected > depth.selectedPeak) depth.selectedPeak = selected;
        if (ready > depth.readyPeak) depth.readyPeak = ready;
        if (required > depth.requiredPeak) depth.requiredPeak = required;
        depth.readyShortfall += ready < selected ? 1 : 0;
        depth.requiredShortfall += ready < required ? 1 : 0;
        depth.noReady += ready == 0 ? 1 : 0;
    }

    // Lookup occurs before optional admission. A complete cached proof may be
    // published with zero new-work admission; a cache hit is not an evaluation.
    void RecordCache(Stage stage, bool hit, bool admitted)
    {
        if (!Valid(stage)) return;
        auto& s = stages[static_cast<size_t>(stage)];
        ++s.lookups;
        if (hit)
        {
            ++s.cacheHits;
            if (!admitted) ++s.zeroAdmissionHits;
        }
        else ++s.cacheMisses;
    }

    // Record one stage evaluation, including its cancelled/partial work cost.
    // Complete means the stage finished, not that a valid proof was published.
    void RecordWork(Stage stage, WorkOutcome outcome, double elapsedMs,
        StopReason reason = StopReason::None)
    {
        if (!Valid(stage)) return;
        if (static_cast<size_t>(outcome) >= static_cast<size_t>(WorkOutcome::Count) ||
            static_cast<size_t>(reason) >= StopReasonCount)
        {
            ++invalidEvents;
            return;
        }
        auto& s = stages[static_cast<size_t>(stage)];
        ++s.workCalls;
        switch (outcome)
        {
        case WorkOutcome::Complete: ++s.completed; break;
        case WorkOutcome::Cancelled: ++s.cancelled; break;
        case WorkOutcome::Deferred: ++s.deferred; break;
        default: break;
        }
        if (std::isfinite(elapsedMs) && elapsedMs >= 0) s.elapsedMs.Add(elapsedMs);
        else { ++s.invalidElapsedSamples; ++invalidEvents; }
        if (reason != StopReason::None) ++s.stops[static_cast<size_t>(reason)];
    }

    // Stops before any stage evaluation need no fabricated elapsed-work sample.
    // Do not also RecordStop for a reason already passed to RecordWork.
    void RecordStop(Stage stage, StopReason reason)
    {
        if (!Valid(stage)) return;
        if (static_cast<size_t>(reason) >= StopReasonCount) { ++invalidEvents; return; }
        if (reason != StopReason::None)
            ++stages[static_cast<size_t>(stage)].stops[static_cast<size_t>(reason)];
    }

    // Count complete independent immutable entries kept by an attempt once.
    // afterCancellation is a subset: preservation is never proof publication.
    // These are retention events, not a claim about unique frames in playback.
    void RecordRetainedMeasurements(uint64_t count, bool afterCancellation)
    {
        independentRetained += count;
        if (afterCancellation) independentRetainedAfterCancellation += count;
    }

    void RecordPublished(ProofKind kind)
    {
        if (Valid(kind)) ++proofs[static_cast<size_t>(kind)].published;
    }
    void RecordAdopted(ProofKind kind)
    {
        if (Valid(kind)) ++proofs[static_cast<size_t>(kind)].adopted;
    }
    // Supply actual signed lead at adoption. Omit this overload when lead was
    // not measured; defaulting an unknown lead to zero would hide missing data.
    void RecordAdopted(ProofKind kind, double leadFrames, double leadMs)
    {
        if (!Valid(kind)) return;
        auto& p = proofs[static_cast<size_t>(kind)];
        ++p.adopted;
        if (std::isfinite(leadFrames)) p.effectiveLeadFrames.Add(leadFrames);
        else ++invalidEvents;
        if (std::isfinite(leadMs)) p.effectiveLeadMs.Add(leadMs);
        else ++invalidEvents;
    }
    void RecordRejected(ProofKind kind, Rejection reason)
    {
        if (!Valid(kind)) return;
        if (static_cast<size_t>(reason) >= RejectionCount) { ++invalidEvents; return; }
        auto& p = proofs[static_cast<size_t>(kind)];
        ++p.rejected;
        ++p.rejections[static_cast<size_t>(reason)];
    }
    // One result observation, not a lifetime job count. An invalid or no-change
    // completed result is distinct from a result still late at consumption.
    void RecordAsync(ProofKind kind, AsyncOutcome outcome)
    {
        if (!Valid(kind)) return;
        if (static_cast<size_t>(outcome) >= AsyncOutcomeCount) { ++invalidEvents; return; }
        ++proofs[static_cast<size_t>(kind)].async[static_cast<size_t>(outcome)];
    }

    void Reset() { *this = LookaheadPerformanceWindow{}; }

private:
    bool Valid(Stage stage)
    {
        if (static_cast<size_t>(stage) < StageCount) return true;
        ++invalidEvents;
        return false;
    }
    bool Valid(ProofKind kind)
    {
        if (static_cast<size_t>(kind) < ProofKindCount) return true;
        ++invalidEvents;
        return false;
    }
};
