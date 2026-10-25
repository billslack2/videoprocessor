#pragma once

#include <cstdint>

// Owned only by the GUI thread. Decisions schedule no work and call no Win32 API.
namespace FullscreenActivation
{
    constexpr uint64_t SettleDelayMs = 5000;
    enum class Decision { Idle, Cancel, WaitDelay, WaitReady, Activate };

    class Session
    {
    public:
        bool Arm(uint64_t nowMs)
        {
            if (m_pending || m_attempted)
                return false;
            m_pending = true;
            m_deadline = nowMs + SettleDelayMs;
            ClearReady();
            return true;
        }

        void Cancel()
        {
            m_pending = false;
            m_attempted = false;
            m_deadline = 0;
            ClearReady();
        }

        // Call only after the existing generation/target-matched reveal succeeds.
        void OnRevealed(uintptr_t target, uint32_t generation)
        {
            if (!m_pending || m_attempted || !target || !generation)
                return;
            m_revealedTarget = target;
            m_revealedGeneration = generation;
        }

        void ClearReady()
        {
            m_revealedTarget = 0;
            m_revealedGeneration = 0;
        }

        uint64_t DelayMs(uint64_t nowMs) const
        {
            return nowMs < m_deadline ? m_deadline - nowMs : 1;
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
            // An old WM_TIMER can survive KillTimer or a previous session.
            if (nowMs < m_deadline)
                return Decision::WaitDelay;
            if (!ready || !target || target != m_revealedTarget ||
                !currentGeneration || currentGeneration != m_revealedGeneration)
                return Decision::WaitReady;

            // Consume before the native call. Windows denial is still one attempt.
            m_pending = false;
            m_attempted = true;
            return Decision::Activate;
        }

        bool Pending() const { return m_pending; }
        bool Attempted() const { return m_attempted; }
        uint64_t Deadline() const { return m_deadline; }

    private:
        bool m_pending = false;
        bool m_attempted = false;
        uint64_t m_deadline = 0;
        uintptr_t m_revealedTarget = 0;
        uint32_t m_revealedGeneration = 0;
    };
}
