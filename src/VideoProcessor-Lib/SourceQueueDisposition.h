#pragma once
#include <atomic>
#include <cstdint>

// Diagnostic counts of owned input queue entries, NOT display/scanout drops.
// Lifetime is the loaded renderer module/process, across renderer instances and
// queue generations. Normal dequeue is never recorded; repeat copies are not
// additional accepted sources. No source-number subtraction or reset heuristic.
// This excludes failures after normal dequeue (conversion/render/Present),
// upstream capture losses and scanout. Legacy renderer counts report their
// own broader, instance-scoped failures; these totals are not additive to them.
struct SourceQueueDispositionSnapshotV1
{
    uint32_t size=sizeof(SourceQueueDispositionSnapshotV1), version=1;
    uint32_t reportingModules=0, unavailableModules=0;
    uint64_t capacity=0, recovery=0, lifecycle=0, dueSource=0, repeatCopies=0;
    bool Supported() const { return size==sizeof(*this) && version==1; }
    uint64_t RetiredSources() const { return capacity+recovery+lifecycle+dueSource; }
};

enum class SourceQueueDisposition { Capacity, Recovery, Lifecycle, DueSource };
class SourceQueueDispositionMeter
{
    std::atomic<uint64_t> m_counts[4]{};
    std::atomic<uint64_t> m_repeatCopies{0};
public:
    static_assert(ATOMIC_LLONG_LOCK_FREE == 2 && sizeof(uint64_t) == sizeof(unsigned long long),
        "Queue diagnostics must not add a lock to source retirement");
    void Retire(SourceQueueDisposition reason,bool repeatCopy=false) noexcept
    {
        if(repeatCopy)m_repeatCopies.fetch_add(1,std::memory_order_relaxed);
        else m_counts[static_cast<unsigned>(reason)].fetch_add(1,std::memory_order_relaxed);
    }
    SourceQueueDispositionSnapshotV1 Read() const noexcept
    {
        SourceQueueDispositionSnapshotV1 out;out.reportingModules=1;
        out.capacity=m_counts[0].load(std::memory_order_relaxed);
        out.recovery=m_counts[1].load(std::memory_order_relaxed);
        out.lifecycle=m_counts[2].load(std::memory_order_relaxed);
        out.dueSource=m_counts[3].load(std::memory_order_relaxed);
        out.repeatCopies=m_repeatCopies.load(std::memory_order_relaxed);
        return out;
    }
};
using SourceQueueDispositionQueryV1=bool (__cdecl *)(SourceQueueDispositionSnapshotV1*);
inline bool CopySourceQueueDispositionSnapshot(const SourceQueueDispositionSnapshotV1& current,
    SourceQueueDispositionSnapshotV1* out)
{
    if(!out || !out->Supported() || !current.Supported())return false;
    *out=current;return true;
}
// Called once for each already-loaded flavor into a NEW total at every poll.
// Missing optional exports remain explicitly unavailable, never an invented 0.
inline void AddSourceQueueDispositionModule(SourceQueueDispositionSnapshotV1& total,
    SourceQueueDispositionQueryV1 query)
{
    SourceQueueDispositionSnapshotV1 current;
    if(!query || !query(&current) || !current.Supported() || current.reportingModules!=1) {
        ++total.unavailableModules;return;
    }
    ++total.reportingModules;
    total.capacity+=current.capacity;total.recovery+=current.recovery;
    total.lifecycle+=current.lifecycle;total.dueSource+=current.dueSource;
    total.repeatCopies+=current.repeatCopies;
}
