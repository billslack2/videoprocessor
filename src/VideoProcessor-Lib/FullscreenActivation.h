#pragma once

#include <cstdint>

// Owned only by the GUI thread. Decisions schedule no work and call no Win32 API.
namespace FullscreenActivation
{
    constexpr uint64_t RetryIntervalMs = 1000;
    constexpr uint64_t RetryWindowMs = 10000;
    constexpr uint32_t MaximumAttempts = 10;
    enum class Decision { Idle, Cancel, WaitDelay, WaitReady, Activate };

    class Session
    {
    public:
        bool Arm(uint64_t nowMs)
        {
            // Rebuilds/reveals cannot extend a fullscreen session's retry window.
            if (m_pending || m_started)
                return false;
            m_pending = true;
            m_nextAttempt = nowMs + RetryIntervalMs;
            ClearReady();
            return true;
        }

        void Cancel()
        {
            m_pending = false;
            m_started = false;
            m_attempts = 0;
            m_deadline = m_nextAttempt = 0;
            ClearReady();
        }

        // Start the bounded window only when the live fullscreen host is revealed.
        void OnRevealed(uintptr_t target, uint32_t generation, uint64_t nowMs)
        {
            if (!m_pending || !target || !generation)
                return;
            m_revealedTarget = target;
            m_revealedGeneration = generation;
            if (!m_started)
            {
                m_started = true;
                m_deadline = nowMs + RetryWindowMs;
                m_nextAttempt = nowMs;
            }
        }

        void ClearReady()
        {
            m_revealedTarget = 0;
            m_revealedGeneration = 0;
        }

        uint64_t DelayMs(uint64_t nowMs) const
        {
            if (!m_started)
                return RetryIntervalMs;
            const uint64_t next = m_nextAttempt < m_deadline ? m_nextAttempt : m_deadline;
            return nowMs < next ? next - nowMs : 1;
        }

        Decision Evaluate(uint64_t nowMs, bool fullscreen, bool terminating,
            uintptr_t target, uint32_t currentGeneration, bool ready)
        {
            if (!m_pending)
                return Decision::Idle;
            if (!fullscreen || terminating)
            {
                Cancel();
                return Decision::Cancel;
            }
            if (m_started && nowMs >= m_deadline)
            {
                m_pending = false;
                return Decision::Idle;
            }
            if (!m_started)
                return Decision::WaitReady;
            // Old/duplicate WM_TIMER messages cannot compress the retry interval.
            if (nowMs < m_nextAttempt)
                return Decision::WaitDelay;
            m_nextAttempt = nowMs + RetryIntervalMs;
            if (!ready || !target || target != m_revealedTarget ||
                !currentGeneration || currentGeneration != m_revealedGeneration)
                return Decision::WaitReady;

            // Success and Windows denial both count; neither ends retries early.
            ++m_attempts;
            if (m_attempts == MaximumAttempts)
                m_pending = false;
            return Decision::Activate;
        }

        bool Pending() const { return m_pending; }
        bool Attempted() const { return m_attempts != 0; }
        uint32_t Attempts() const { return m_attempts; }
        uint64_t Deadline() const { return m_deadline; }

    private:
        bool m_pending = false;
        bool m_started = false;
        uint32_t m_attempts = 0;
        uint64_t m_deadline = 0;
        uint64_t m_nextAttempt = 0;
        uintptr_t m_revealedTarget = 0;
        uint32_t m_revealedGeneration = 0;
    };
}
