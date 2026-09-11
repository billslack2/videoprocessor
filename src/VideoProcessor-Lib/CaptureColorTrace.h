#pragma once

#include <cstdint>
#include <tuple>

// Values already read by the capture callback. No COM calls, allocations or
// logging on the per-frame observation path. HRESULT 0 means a successful read;
// an unavailable property is never presented as a successfully reported SDR/709.
struct CaptureColorTraceSample
{
    uint64_t run = 0;
    uint32_t flags = 0;
    bool metadataAvailable = false;
    int32_t eotfResult = 0;
    int32_t colorspaceResult = 0;
    int64_t readEotf = -1;
    int64_t readColorspace = -1;
    int64_t cachedEotf = -1;
    int64_t cachedColorspace = -1;
    bool cachedHdr = false;

    bool operator==(const CaptureColorTraceSample& other) const
    {
        return std::tie(run, flags, metadataAvailable, eotfResult,
            colorspaceResult, readEotf, readColorspace, cachedEotf,
            cachedColorspace, cachedHdr) ==
            std::tie(other.run, other.flags, other.metadataAvailable,
                other.eotfResult, other.colorspaceResult, other.readEotf,
                other.readColorspace, other.cachedEotf,
                other.cachedColorspace, other.cachedHdr);
    }
};

struct CaptureColorTraceWindow
{
    bool emit = false;
    uint64_t observations = 0;
    uint64_t changes = 0;
    uint64_t missingInterface = 0;
    uint64_t eotfReadFailures = 0;
    uint64_t colorspaceReadFailures = 0;
};

// Callback-owned: first sample per run, at most one change report per second,
// and one steady-state report every ten seconds. Count even changes that revert
// before emission, so throttling cannot make an unstable source look stable.
class CaptureColorTrace
{
public:
    CaptureColorTraceWindow Observe(const CaptureColorTraceSample& sample,
        uint64_t nowMs)
    {
        const bool newRun = !m_seen || sample.run != m_previous.run;
        if (newRun)
            m_window = {};
        ++m_window.observations;
        if (!sample.metadataAvailable)
            ++m_window.missingInterface;
        else
        {
            if (sample.eotfResult != 0)
                ++m_window.eotfReadFailures;
            if (sample.colorspaceResult != 0)
                ++m_window.colorspaceReadFailures;
        }
        if (m_seen && !newRun && !(sample == m_previous))
            ++m_window.changes;
        m_previous = sample;
        m_seen = true;
        const uint64_t elapsed = nowMs - m_lastLogMs;
        if (!newRun && elapsed < 10000 &&
            (m_window.changes == 0 || elapsed < 1000))
            return {};
        m_lastLogMs = nowMs;
        CaptureColorTraceWindow result = m_window;
        result.emit = true;
        m_window = {};
        return result;
    }

private:
    bool m_seen = false;
    uint64_t m_lastLogMs = 0;
    CaptureColorTraceSample m_previous;
    CaptureColorTraceWindow m_window;
};
