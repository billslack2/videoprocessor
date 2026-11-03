#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

// Lifetime-owned wakeup only: never carries source facts or crop authority.
// Snapshot before observing results so completion before WaitUntil is not lost.
class AnalysisCompletionSignal {
public:
    uint64_t Snapshot() {std::lock_guard<std::mutex> lock(m_mutex);return m_revision;}
    void Notify() {
        {std::lock_guard<std::mutex> lock(m_mutex);++m_revision;}
        m_changed.notify_all();
    }
    bool WaitUntil(uint64_t observed,std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_changed.wait_until(lock,deadline,[&]{return m_revision!=observed;});
    }
private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    uint64_t m_revision=0;
};
