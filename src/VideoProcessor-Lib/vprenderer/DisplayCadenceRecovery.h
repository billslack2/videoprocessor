#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Bounded renderer-side response to a persistent mismatch between the
// selected monitor refresh and the observed presentation cadence. This class
// owns policy only; callers perform swapchain work at a render-thread safe
// point and report later health observations here.
class DisplayCadenceRecovery
{
public:
    void SetTarget(uint64_t monitorIdentity, double refreshHz)
    {
        if (monitorIdentity != 0)
        {
            const bool monitorChanged = m_hasMonitorIdentity &&
                monitorIdentity != m_monitorIdentity;
            if (monitorChanged)
            {
                ResetEpisode();
                m_hasTargetRate = false;
            }
            m_monitorIdentity = monitorIdentity;
            m_hasMonitorIdentity = true;
        }

        if (monitorIdentity == 0 || !ValidRate(refreshHz))
        {
            m_targetAvailable = false;
            m_healthySinceNs = kNoTime;
            ClearWorkahead();
            return;
        }

        if (!m_hasTargetRate)
        {
            m_targetHz = refreshHz;
            m_hasTargetRate = true;
        }
        else if (std::abs(refreshHz - m_targetHz) >
            (std::max)(0.05, m_targetHz * 0.001))
        {
            ResetEpisode();
            m_targetHz = refreshHz;
        }

        m_targetAvailable = true;
    }

    // Workahead is independent corroboration for a rebuild, not a substitute
    // for the DXGI cadence classifier. waitNs is the guard's required wait,
    // derived from the local queue-depth schedule before any CV wait occurs.
    // Repeated observations of one serial (for example, CV wakeups) count once.
    // This indicates local scheduler pressure only; it proves neither actual
    // blocking nor physical scanout timing, and does not validate DXGI statistics.
    bool RecordWorkahead(int64_t nowNs, uint64_t submissionSerial,
        int64_t waitNs)
    {
        if (!AcceptTime(nowNs))
        {
            ClearWorkahead();
            return false;
        }
        if (!m_targetAvailable || !m_hasTargetRate || submissionSerial == 0 ||
            waitNs <= 0 || submissionSerial <= m_lastWorkaheadSerial)
            return false;

        const long double periodNs = 1'000'000'000.0L /
            static_cast<long double>(m_targetHz);
        if (static_cast<long double>(waitNs) <= periodNs / 4.0L)
            return false;

        m_workaheadEvents[m_nextWorkaheadIndex] = { nowNs, submissionSerial };
        m_nextWorkaheadIndex = (m_nextWorkaheadIndex + 1) %
            kRequiredWorkaheadEvents;
        if (m_workaheadCount < kRequiredWorkaheadEvents)
            ++m_workaheadCount;
        m_lastWorkaheadSerial = submissionSerial;
        return true;
    }

    bool HasFreshWorkahead(int64_t nowNs) const
    {
        return nowNs >= 0 && (!m_hasLastNow || nowNs >= m_lastNowNs) &&
            HasFreshWorkaheadAt(nowNs);
    }

    // Clearing evidence also cancels any queued recovery candidate.
    void ClearWorkahead()
    {
        m_workaheadEvents = {};
        m_workaheadCount = 0;
        m_nextWorkaheadIndex = 0;
        m_lastWorkaheadSerial = 0;
        m_pending = false;
    }

    // A queued request and partial validation belong to the output that produced them.
    // Keep the episode budget, cooldown and source-paced fallback across output
    // changes; only fresh target selection or sustained health may reset those.
    void OutputChanged()
    {
        ClearWorkahead();
        m_healthySinceNs = kNoTime;
    }

    void Observe(bool mismatch, bool healthy, int64_t nowNs)
    {
        const bool validTime = AcceptTime(nowNs);

        if (mismatch)
        {
            // Fallback is the immediate bounded response. A swapchain rebuild
            // additionally requires recent required-wait evidence from the
            // local slot schedule; DXGI mismatch alone is not enough.
            m_fallback = true;
            m_healthySinceNs = kNoTime;
            if (!validTime)
            {
                ClearWorkahead();
                return;
            }
            m_pending = m_targetAvailable && m_attempts < kMaximumAttempts &&
                HasFreshWorkaheadAt(nowNs);
            if (m_attempts >= kMaximumAttempts)
                ClearWorkahead();
            return;
        }

        m_pending = false;
        if (!validTime)
        {
            ClearWorkahead();
            m_healthySinceNs = kNoTime;
            return;
        }

        if (!healthy)
        {
            // Warming/temporarily unavailable feedback cancels queued work,
            // but does not erase independently scoped clamp evidence collected
            // while the cadence classifier builds its observation window.
            m_healthySinceNs = kNoTime;
            return;
        }

        // A healthy observation invalidates prior clamp evidence. The cached-
        // repeat fallback remains until health has persisted for half a second.
        ClearWorkahead();
        if (!m_targetAvailable)
        {
            m_healthySinceNs = kNoTime;
            return;
        }

        if (m_healthySinceNs == kNoTime)
            m_healthySinceNs = nowNs;

        const int64_t healthyNs = nowNs - m_healthySinceNs;
        if (healthyNs >= kFallbackValidationNs)
            m_fallback = false;

        if (healthyNs >= kBudgetResetNs)
        {
            m_attempts = 0;
            m_hasLastAttempt = false;
            m_pending = false;
        }
    }

    bool Pending() const { return m_pending; }

    bool TakeRequest(int64_t nowNs)
    {
        if (!AcceptTime(nowNs))
        {
            ClearWorkahead();
            return false;
        }
        if (!m_pending)
            return false;
        if (!m_targetAvailable || m_attempts >= kMaximumAttempts ||
            !HasFreshWorkaheadAt(nowNs))
        {
            // Stale evidence cannot remain latent and later authorize a retry.
            ClearWorkahead();
            return false;
        }

        if (m_hasLastAttempt &&
            nowNs - m_lastAttemptNs < kAttemptCooldownNs)
            return false;

        ++m_attempts;
        m_lastAttemptNs = nowNs;
        m_hasLastAttempt = true;
        ClearWorkahead();
        return true;
    }

    uint32_t Attempts() const { return m_attempts; }
    bool Fallback() const { return m_fallback; }

private:
    struct WorkaheadEvent
    {
        int64_t timeNs = 0;
        uint64_t serial = 0;
    };

    static constexpr int64_t kNoTime = -1;
    static constexpr int64_t kAttemptCooldownNs = 2'000'000'000LL;
    static constexpr int64_t kFallbackValidationNs = 500'000'000LL;
    static constexpr int64_t kBudgetResetNs = 5'000'000'000LL;
    static constexpr int64_t kWorkaheadFreshnessNs = 500'000'000LL;
    static constexpr uint32_t kMaximumAttempts = 2;
    static constexpr uint32_t kRequiredWorkaheadEvents = 3;

    static bool ValidRate(double hz)
    {
        return std::isfinite(hz) && hz >= 10.0 && hz <= 500.0;
    }

    bool AcceptTime(int64_t nowNs)
    {
        if (nowNs < 0 || (m_hasLastNow && nowNs < m_lastNowNs))
            return false;
        m_lastNowNs = nowNs;
        m_hasLastNow = true;
        return true;
    }

    bool HasFreshWorkaheadAt(int64_t nowNs) const
    {
        if (!m_targetAvailable || !m_hasTargetRate ||
            m_workaheadCount < kRequiredWorkaheadEvents || nowNs < 0)
            return false;

        const uint32_t oldestIndex = m_nextWorkaheadIndex;
        const uint32_t latestIndex = (m_nextWorkaheadIndex +
            kRequiredWorkaheadEvents - 1) % kRequiredWorkaheadEvents;
        const int64_t firstNs = m_workaheadEvents[oldestIndex].timeNs;
        const int64_t latestNs = m_workaheadEvents[latestIndex].timeNs;
        if (firstNs < 0 || latestNs < firstNs || nowNs < latestNs ||
            nowNs - firstNs > kWorkaheadFreshnessNs ||
            nowNs - latestNs > kWorkaheadFreshnessNs)
            return false;

        const long double periodNs = 1'000'000'000.0L /
            static_cast<long double>(m_targetHz);
        return static_cast<long double>(latestNs - firstNs) >= periodNs;
    }

    void ResetEpisode()
    {
        m_attempts = 0;
        m_fallback = false;
        m_healthySinceNs = kNoTime;
        m_hasLastAttempt = false;
        ClearWorkahead();
    }

    uint64_t m_monitorIdentity = 0;
    double m_targetHz = 0.0;
    int64_t m_healthySinceNs = kNoTime;
    int64_t m_lastAttemptNs = 0;
    int64_t m_lastNowNs = 0;
    uint64_t m_lastWorkaheadSerial = 0;
    std::array<WorkaheadEvent, kRequiredWorkaheadEvents> m_workaheadEvents{};
    uint32_t m_workaheadCount = 0;
    uint32_t m_nextWorkaheadIndex = 0;
    uint32_t m_attempts = 0;
    bool m_hasMonitorIdentity = false;
    bool m_hasTargetRate = false;
    bool m_targetAvailable = false;

    bool m_hasLastAttempt = false;
    bool m_hasLastNow = false;
    bool m_pending = false;
    bool m_fallback = false;
};
