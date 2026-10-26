#pragma once

#include <algorithm>
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
            m_pending = false;
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

    void Observe(bool mismatch, bool healthy, int64_t nowNs)
    {
        const bool validTime = AcceptTime(nowNs);

        if (mismatch)
        {
            m_fallback = true;
            m_healthySinceNs = kNoTime;
            m_pending = m_attempts < kMaximumAttempts;
            return;
        }

        if (!validTime)
        {
            m_healthySinceNs = kNoTime;
            return;
        }

        if (!healthy)
        {
            m_healthySinceNs = kNoTime;
            return;
        }

        // One healthy observation cancels queued work. The cached-repeat
        // fallback remains until that health has persisted for half a second.
        m_pending = false;
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

    // A queued request and partial validation belong to the output that produced them.
    // Keep the episode budget, cooldown and source-paced fallback across output
    // changes; only fresh target selection or sustained health may reset those.
    void OutputChanged()
    {
        m_pending = false;
        m_healthySinceNs = kNoTime;
    }

    bool Pending() const { return m_pending; }

    bool TakeRequest(int64_t nowNs)
    {
        if (!m_pending || !m_targetAvailable ||
            m_attempts >= kMaximumAttempts || !AcceptTime(nowNs))
            return false;

        if (m_hasLastAttempt &&
            nowNs - m_lastAttemptNs < kAttemptCooldownNs)
            return false;

        ++m_attempts;
        m_lastAttemptNs = nowNs;
        m_hasLastAttempt = true;
        m_pending = false;
        return true;
    }

    uint32_t Attempts() const { return m_attempts; }
    bool Fallback() const { return m_fallback; }

private:
    static constexpr int64_t kNoTime = -1;
    static constexpr int64_t kAttemptCooldownNs = 2'000'000'000LL;
    static constexpr int64_t kFallbackValidationNs = 500'000'000LL;
    static constexpr int64_t kBudgetResetNs = 5'000'000'000LL;
    static constexpr uint32_t kMaximumAttempts = 2;

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

    void ResetEpisode()
    {
        m_attempts = 0;
        m_pending = false;
        m_fallback = false;
        m_healthySinceNs = kNoTime;
        m_hasLastAttempt = false;
    }

    uint64_t m_monitorIdentity = 0;
    double m_targetHz = 0.0;
    int64_t m_healthySinceNs = kNoTime;
    int64_t m_lastAttemptNs = 0;
    int64_t m_lastNowNs = 0;
    uint32_t m_attempts = 0;
    bool m_hasMonitorIdentity = false;
    bool m_hasTargetRate = false;
    bool m_targetAvailable = false;

    bool m_hasLastAttempt = false;
    bool m_hasLastNow = false;
    bool m_pending = false;
    bool m_fallback = false;
};