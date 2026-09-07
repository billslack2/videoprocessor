#pragma once

#include "DisplayRefreshRatePolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>

// OSD-only estimate: average signed rate error before taking its reciprocal.
// Averaging individual intervals would over-weight nearly matched samples.
class CadenceIntervalEstimate
{
public:
    struct Contract
    {
        std::wstring monitor;
        double captureNominalHz = 0.0;
        double displayNominalHz = 0.0;
        double overrideHz = 0.0;
        int rateSource = 0;

        bool operator==(const Contract& other) const
        {
            return monitor == other.monitor &&
                captureNominalHz == other.captureNominalHz &&
                displayNominalHz == other.displayNominalHz &&
                overrideHz == other.overrideHz && rateSource == other.rateSource;
        }
    };

    // Diagnostics can accumulate validated physical measurements while the
    // longer scene-correction phase-confidence gate is still warming.
    static double SelectPhysicalDisplayRate(const DisplayRefreshRateResult& phase,
        const DisplayRefreshRateResult& readiness,
        const DisplayRefreshRateResult& startup)
    {
        if (phase.decision == DisplayRefreshRateDecision::Accepted)
            return phase.selectedRateHz;
        if (readiness.readinessValidated)
            return readiness.readinessRateHz;
        if (startup.startupValidated)
            return startup.startupRateHz;
        return 0.0;
    }

    void Reset()
    {
        m_samples.clear();
        m_started = false;
        m_waitingForRates = false;
    }

    void Update(uint64_t nowMs, double captureHz, double displayHz,
        const Contract& contract, bool waitingForRates = false)
    {
        if (!std::isfinite(captureHz) || !std::isfinite(displayHz) ||
            captureHz <= 0.0 || displayHz <= 0.0)
        {
            Reset();
            m_waitingForRates = waitingForRates;
            return;
        }
        if (m_started && (!(contract == m_contract) || nowMs < m_lastMs ||
            nowMs - m_lastMs > 5000))
            Reset();
        if (!m_started)
        {
            m_contract = contract;
            m_lastMs = nowMs;
            m_started = true;
            return;
        }
        // UI-triggered refreshes cannot add extra weight or unbounded samples.
        if (nowMs - m_lastMs < 1000)
            return;
        m_samples.push_back({m_lastMs, nowMs, captureHz - displayHz});
        m_lastMs = nowMs;
        const uint64_t cutoff = nowMs > 180000 ? nowMs - 180000 : 0;
        while (!m_samples.empty() && m_samples.front().endMs <= cutoff)
            m_samples.pop_front();
        if (!m_samples.empty())
            m_samples.front().startMs = (std::max)(m_samples.front().startMs, cutoff);
    }

    double EvidenceSeconds() const
    {
        return m_samples.empty() ? 0.0 :
            (m_samples.back().endMs - m_samples.front().startMs) / 1000.0;
    }

    double MeanDifferenceHz() const
    {
        double weighted = 0.0;
        double duration = 0.0;
        for (const auto& sample : m_samples)
        {
            const double ms = static_cast<double>(sample.endMs - sample.startMs);
            weighted += sample.differenceHz * ms;
            duration += ms;
        }
        return duration > 0.0 ? weighted / duration : 0.0;
    }

    std::wstring Text() const
    {
        if (!m_started)
            return m_waitingForRates ? L"Warming" : L"Unavailable";
        if (EvidenceSeconds() < 30.0)
            return L"Warming";
        const double difference = MeanDifferenceHz();
        if (std::fabs(difference) <= 1e-12)
            return L"None";
        const double seconds = 1.0 / std::fabs(difference);
        // Compare at the displayed one-second precision, avoiding cancellation
        // error at exactly 48 hours when subtracting the two measured rates.
        if (std::round(seconds) > 172800.0)
            return L"None";
        return std::wstring(difference > 0.0 ? L"Drop every " : L"Repeat every ") +
            FormatInterval(seconds);
    }

    static std::wstring FormatInterval(double seconds)
    {
        if (seconds < 1.0)
            return L"<1s";
        const uint64_t total = static_cast<uint64_t>(std::round(seconds));
        const uint64_t hours = total / 3600;
        const uint64_t minutes = total % 3600 / 60;
        const uint64_t secs = total % 60;
        return (hours ? std::to_wstring(hours) + L"h" : L"") +
            (hours || minutes ? std::to_wstring(minutes) + L"m" : L"") +
            std::to_wstring(secs) + L"s";
    }

private:
    struct Sample { uint64_t startMs; uint64_t endMs; double differenceHz; };
    std::deque<Sample> m_samples;
    Contract m_contract;
    uint64_t m_lastMs = 0;
    bool m_started = false;
    bool m_waitingForRates = false;
};
