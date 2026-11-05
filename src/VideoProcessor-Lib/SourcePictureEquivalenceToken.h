#pragma once
#include <ActivePictureDecisionTimeline.h>
#include <atomic>
#include <memory>

class OwnedSourcePictureObservationCache;
// Exact current-source byte association only. No image, crop, caption, absence,
// or presentation permission. Consumers must also bind policy/mode/geometry.
// Tokens retain no source pixels or capture owner and expire with cache entries.
class SourcePictureEquivalenceToken {
public:
    bool Valid() const {
        const auto entry=entry_.lock();
        return entry && entry->active.load(std::memory_order_acquire);
    }
    const ActivePictureFrameIdentity& Identity() const {return identity_;}
    bool MatchesSource(const ActivePictureFrameIdentity& current) const {
        return Valid() && SameAnalysisFrameIdentity(identity_,current);
    }
    bool SameSnapshot(const SourcePictureEquivalenceToken& other) const {
        const auto a=entry_.lock(),b=other.entry_.lock();
        return a && a==b && a->active.load(std::memory_order_acquire) &&
            identity_.transportGeneration==other.identity_.transportGeneration &&
            identity_.sourceFormatGeneration==other.identity_.sourceFormatGeneration &&
            identity_.analysisDomainKey==other.identity_.analysisDomainKey &&
            identity_.viewportGeneration==other.identity_.viewportGeneration &&
            identity_.rendererGeneration==other.identity_.rendererGeneration;
    }
private:
    friend class OwnedSourcePictureObservationCache;
    struct EntryLifetime {std::atomic_bool active{true};};
    std::weak_ptr<const EntryLifetime> entry_;
    ActivePictureFrameIdentity identity_;
};
