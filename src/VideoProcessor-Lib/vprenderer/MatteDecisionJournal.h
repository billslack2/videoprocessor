#pragma once
#include <cstdint>

// Metadata-only selection for the opt-in existing asynchronous log. This does
// not classify pixels or grant crop authority.
namespace MatteDecisionJournal
{
    enum class Guard : uint32_t { Unknown, None, Current, Held, Blocked, Other };

    struct Identity
    {
        uint64_t generation = 0;
        uint64_t sequence = 0;
        uint64_t domain = 0;
        uint64_t sourceFormat = 0;
        uint64_t sourceFrame = 0;
        uint64_t timestamp = 0;
        uint64_t viewport = 0;
        uint64_t policy = 0;
        uint64_t continuity = 0;
    };

    inline bool SameIdentity(const Identity& a, const Identity& b) noexcept
    {
        return a.generation == b.generation && a.sequence == b.sequence &&
            a.domain == b.domain && a.sourceFormat == b.sourceFormat &&
            a.sourceFrame == b.sourceFrame && a.timestamp == b.timestamp && a.viewport == b.viewport &&
            a.policy == b.policy && a.continuity == b.continuity;
    }

    inline bool IsSelectedSnapshot(bool available, bool mixing, const Identity& snapshot,
        const Identity& selected) noexcept
    {
        return available && !mixing && snapshot.generation != 0 && snapshot.sequence != 0 &&
            snapshot.domain != 0 && SameIdentity(snapshot, selected);
    }

    inline bool ShouldEmit(bool poseChanged, Guard guard, Guard previousGuard,
        const Identity& selected, const Identity& previousSelected) noexcept
    {
        return poseChanged || guard != previousGuard ||
            (guard == Guard::Current && !SameIdentity(selected, previousSelected));
    }
}
