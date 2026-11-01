#pragma once

#include <atomic>
#include <cstdint>

// Diagnostic admission only. This grants no detector, crop, or presentation
// authority. The caller validates source ownership before requesting admission.
class SourcePictureShadowPolicy final
{
public:
    enum { BudgetMilliseconds = 12, IntervalMilliseconds = 500 };

    explicit SourcePictureShadowPolicy(bool enabled) noexcept : m_enabled(enabled) {}

    // Uses one monotonic clock domain and its fixed positive frequency.
    // Admission spends the slot even if subsequent diagnostic work is cancelled.
    // Never waits for a slot, refunds one, or accumulates catch-up work.
    bool Admit(int64_t nowTicks, int64_t frequency) noexcept
    {
        if (!m_enabled || nowTicks < 0 || frequency <= 0)
            return false;
        // Ceiling avoids admitting sooner than 500ms for odd frequencies.
        // Division first also avoids multiplying an arbitrary clock frequency.
        const int64_t interval = frequency / 2 + frequency % 2;
        int64_t previous = m_lastAdmission.load(std::memory_order_relaxed);
        for (;;) {
            if (previous >= 0 &&
                (nowTicks < previous || nowTicks - previous < interval))
                return false;
            if (m_lastAdmission.compare_exchange_weak(previous, nowTicks,
                std::memory_order_relaxed, std::memory_order_relaxed))
                return true;
        }
    }

private:
    const bool m_enabled;
    std::atomic<int64_t> m_lastAdmission{-1};
};
