#pragma once

#include <cstdint>
#include <limits>

// Diagnostic display totals over cumulative producer counters. The caller owns
// each counter and supplies a stable, monotonic domain identity for its producer
// instance/run (not a recyclable pointer). No producer or playback state changes.
class PlaybackCounterWindow
{
public:
    uint64_t Observe(uint64_t raw, uint64_t domainId) noexcept
    {
        // First observation and a new/restarted producer contribute its current
        // raw total. An unchanged producer contributes only its new events.
        const uint64_t delta = !m_initialized || domainId != m_domainId || raw < m_raw
            ? raw : raw - m_raw;
        const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
        m_total = delta > maximum - m_total ? maximum : m_total + delta;
        m_raw = raw;
        m_domainId = domainId;
        m_initialized = true;
        return m_total;
    }

    // Manual/physical-input reset seeds the current producer counters even when
    // the OSD is hidden, so the next observation does not recount old events.
    void Reset(uint64_t raw, uint64_t domainId) noexcept
    {
        m_total = 0;
        m_raw = raw;
        m_domainId = domainId;
        m_initialized = true;
    }

    uint64_t Total() const noexcept { return m_total; }

private:
    uint64_t m_total = 0;
    uint64_t m_raw = 0;
    uint64_t m_domainId = 0;
    bool m_initialized = false;
};