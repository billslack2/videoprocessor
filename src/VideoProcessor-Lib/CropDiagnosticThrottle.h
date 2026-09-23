#pragma once
#include <cstdint>

// Diagnostic-only, wall-clock throttle. Never used as picture proof or policy.
// Coalesce rapid changes (including start/end oscillation) to two bundles/sec;
// unresolved activity gets a two-second heartbeat. No per-frame allocations.
struct CropDiagnosticThrottle
{
    bool initialized = false, logged = false, wasActive = false, pending = false;
    uint64_t generation = 0, epoch = 0, lastSequence = 0, lastTick = 0;
    uint64_t key = 0, lastLogTick = 0, suppressed = 0, emittedSuppressed = 0;

    bool Observe(uint64_t now, uint64_t sourceGeneration, uint64_t viewportEpoch,
        uint64_t sequence, bool active, uint64_t stateKey, bool event = false)
    {
        if (!initialized || generation != sourceGeneration || epoch != viewportEpoch ||
            sequence < lastSequence || now < lastTick)
        {
            *this = {};
            initialized = true;
            generation = sourceGeneration;
            epoch = viewportEpoch;
        }
        const bool changed = active != wasActive || (active && stateKey != key);
        pending = pending || changed || event;
        wasActive = active;
        key = stateKey;
        lastSequence = sequence;
        lastTick = now;
        if (!pending && !(active && (!logged || now - lastLogTick >= 2000))) return false;
        if (logged && now - lastLogTick < 500)
        {
            if (changed || event) ++suppressed;
            return false;
        }
        emittedSuppressed = suppressed;
        suppressed = 0;
        pending = false;
        logged = true;
        lastLogTick = now;
        return true;
    }
};
