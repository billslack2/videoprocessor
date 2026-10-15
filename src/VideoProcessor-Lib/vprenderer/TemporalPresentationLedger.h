#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

// Source evidence is advanced once for each captured source image. Output
// mapping changes consume this frozen evidence without renewing its lease.
struct TemporalSourceEvidenceKey
{
    uint64_t sourceGeneration = 0;
    uint64_t sourceSequence = 0;

    bool operator==(const TemporalSourceEvidenceKey& other) const
    {
        return sourceGeneration == other.sourceGeneration &&
            sourceSequence == other.sourceSequence;
    }
};

struct TemporalPresentationKey
{
    uint64_t sourceGeneration = 0;
    uint64_t sourceSequence = 0;
    uint64_t viewportEpoch = 0;
    uint64_t outputContractSerial = 0;

    bool operator==(const TemporalPresentationKey& other) const
    {
        return sourceGeneration == other.sourceGeneration &&
            sourceSequence == other.sourceSequence &&
            viewportEpoch == other.viewportEpoch &&
            outputContractSerial == other.outputContractSerial;
    }
};

// Bounded render-thread journal. Payloads own their data and must not retain
// GPU resources or pointers into the current render invocation. Failed builds
// are tombstoned so the same source cannot partly advance policy twice.
template<class Payload, size_t Capacity = 16, class Key = TemporalPresentationKey>
class TemporalPresentationLedger
{
    static_assert(Capacity > 0, "Temporal ledger capacity must be positive");
    struct Entry
    {
        Key key{};
        Payload payload{};
        bool seen = false;
        bool succeeded = false;
    };
    std::array<Entry, Capacity> entries{};
    size_t nextInsert = 0;
    uint64_t highWaterGeneration = 0;
    uint64_t highWaterSequence = 0;

    const Entry* Lookup(const Key& key) const
    {
        if (!key.sourceGeneration || !key.sourceSequence)
            return nullptr;
        for (const auto& entry : entries)
            if (entry.seen && entry.key == key) return &entry;
        return nullptr;
    }

public:
    void Clear()
    {
        for (auto& entry : entries)
            entry = {};
        nextInsert = 0;
        highWaterGeneration = 0;
        highWaterSequence = 0;
    }

    bool Seen(const Key& key) const { return Lookup(key) != nullptr; }
    bool Failed(const Key& key) const
    {
        const auto* entry = Lookup(key);
        return entry && !entry->succeeded;
    }

    const Payload* Find(const Key& key) const
    {
        const auto* entry = Lookup(key);
        return entry && entry->succeeded ? &entry->payload : nullptr;
    }

    template<class Builder>
    bool Ensure(const Key& key, Builder&& builder, bool allowRetainedOlder = false)
    {
        if (!key.sourceGeneration || !key.sourceSequence)
            return false;
        if (const auto* prior = Lookup(key))
            return prior->succeeded;

        // An evicted old source must not be evaluated again. Output mappings
        // may opt in only when their separate immutable source fact is retained.
        if (!allowRetainedOlder &&
            (key.sourceGeneration < highWaterGeneration ||
             (key.sourceGeneration == highWaterGeneration &&
              key.sourceSequence < highWaterSequence)))
            return false;

        auto& entry = entries[nextInsert];
        nextInsert = (nextInsert + 1) % Capacity;
        entry.key = key;
        entry.payload = {};
        entry.seen = true;
        entry.succeeded = false;

        if (key.sourceGeneration != highWaterGeneration ||
            key.sourceSequence > highWaterSequence)
        {
            highWaterGeneration = key.sourceGeneration;
            highWaterSequence = key.sourceSequence;
        }

        Payload candidate{};
        if (!std::forward<Builder>(builder)(candidate))
            return false;
        entry.payload = std::move(candidate);
        entry.succeeded = true;
        return true;
    }
};

template<class Payload, size_t Capacity = 16>
using TemporalSourceEvidenceLedger =
    TemporalPresentationLedger<Payload, Capacity, TemporalSourceEvidenceKey>;