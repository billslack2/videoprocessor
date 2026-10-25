#pragma once
#include <RendererLiveness.h>
#include "PassiveDisplayStatistics.h"
#include <memory>

// All process creation, pipe I/O, event correlation and publication is performed
// by a background worker. The render thread only copies a bounded intent.
class PassiveDisplayObserver
{
public:
    PassiveDisplayObserver() noexcept;
    ~PassiveDisplayObserver();
    PassiveDisplayObserver(const PassiveDisplayObserver&) = delete;
    PassiveDisplayObserver& operator=(const PassiveDisplayObserver&) = delete;
    void Start(); // Build/lifecycle thread, never the render loop.
    void Submit(const PassiveDisplayStatistics::OutputIntent& intent) noexcept;
    void Reset(uint64_t serial, int64_t cutoffQpc) noexcept; // render producer only
    RendererPassiveDisplayStats Snapshot(uint64_t now) const;
private:
    struct State;
    std::unique_ptr<State> state;
};
