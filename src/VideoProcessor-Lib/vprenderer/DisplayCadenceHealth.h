#pragma once

#include <vprenderer/AlphaPresentationTelemetry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>

// Classifies whether DXGI's latest sync-vblank timestamps agree with the
// monitor-qualified output refresh rate. Source-frame cadence is deliberately
// not an input: film on a 60 Hz display is a healthy 24-to-60 cadence.
class DisplayCadenceHealth
{
public:
    enum class State
    {
        Unavailable,
        Warming,
        Healthy,
        Mismatch
    };

    struct Snapshot
    {
        State state = State::Unavailable;
        double expectedHz = 0.0;
        double observedHz = 0.0;
        uint32_t uniqueIntervals = 0;
        double windowMs = 0.0;
    };

    void Reset()
    {
        m_snapshot = {};
        m_hasIdentity = false;
        m_generation = 0;
        m_outputEpoch = 0;
        m_qpcFrequency = 0;
        m_lastSyncRefresh = 0;
        m_lastSyncQpc = 0;
        m_lastObservationQpc = 0;
        m_hasLastSample = false;
        m_intervals.clear();
        m_hasStaleReplay = false;
    }

    void Observe(const AlphaDxgiPresentationSample& sample,
        int64_t nowQpc, uint64_t outputEpoch)
    {
        const bool expectedValid = ValidRate(sample.expectedDisplayHz);
        const double previousExpectedHz = m_snapshot.expectedHz;
        const bool targetRateChanged = expectedValid && m_hasIdentity &&
            std::abs(sample.expectedDisplayHz - previousExpectedHz) >
                TargetRateChangeThreshold(previousExpectedHz);
        if (expectedValid)
            m_snapshot.expectedHz = sample.expectedDisplayHz;

        const bool identityChanged = !m_hasIdentity ||
            sample.generation != m_generation ||
            outputEpoch != m_outputEpoch ||
            sample.qpcFrequency != m_qpcFrequency || targetRateChanged;
        if (identityChanged)
        {
            m_hasStaleReplay = false;
            ClearWindow(State::Unavailable);
            m_hasIdentity = true;
            m_generation = sample.generation;
            m_outputEpoch = outputEpoch;
            m_qpcFrequency = sample.qpcFrequency;
            if (expectedValid)
                m_snapshot.expectedHz = sample.expectedDisplayHz;
        }

        if (sample.disjoint)
            m_hasStaleReplay = false;
        if (nowQpc <= 0 || !expectedValid || sample.qpcFrequency <= 0 ||
            !sample.available || sample.disjoint ||
            sample.timingStatus != AlphaPresentationTimingStatus::Available ||
            sample.syncQpc <= 0)
        {
            ClearWindow(State::Unavailable);
            return;
        }

        // DXGI can keep returning advancing statistics whose timestamps are
        // no longer useful for cadence classification. Require the sample
        // itself to be recent, as well as the observation stream.
        if (!IsTimestampFresh(nowQpc, sample.syncQpc, sample.qpcFrequency,
                sample.expectedDisplayHz))
        {
            ClearWindow(State::Unavailable);
            m_hasStaleReplay = true;
            m_staleSyncRefresh = sample.syncRefreshCount;
            m_staleSyncQpc = sample.syncQpc;
            return;
        }

        if (m_hasLastSample)
        {
            const bool duplicateSync =
                sample.syncRefreshCount == m_lastSyncRefresh &&
                sample.syncQpc == m_lastSyncQpc;
            const bool stale = nowQpc < m_lastObservationQpc ||
                IsStale(nowQpc, m_lastObservationQpc, sample.qpcFrequency,
                    sample.expectedDisplayHz);
            if (stale)
            {
                ClearWindow(State::Unavailable);
                // Keep the stale sample signature so repeated polling cannot
                // turn the same old statistics into a new warming baseline.
                if (duplicateSync)
                {
                    m_hasStaleReplay = true;
                    m_staleSyncRefresh = sample.syncRefreshCount;
                    m_staleSyncQpc = sample.syncQpc;
                    return;
                }
            }
            else if (duplicateSync)
            {
                // Re-reading the latest frame statistics is normal when the
                // render loop runs faster than scanout. Duplicates add no
                // cadence evidence and do not refresh the stale-age clock.
                return;
            }
        }

        if (m_hasStaleReplay)
        {
            if (sample.syncRefreshCount == m_staleSyncRefresh &&
                sample.syncQpc == m_staleSyncQpc)
                return;
            m_hasStaleReplay = false;
        }

        if (!m_hasLastSample)
        {
            SetBaseline(sample, nowQpc);
            return;
        }

        if (sample.syncQpc <= m_lastSyncQpc)
        {
            Rebaseline(sample, nowQpc);
            return;
        }

        // Unsigned subtraction deliberately accepts a forward UINT wrap. A
        // backward/reset counter produces an implausible rate and rebaselines.
        const uint32_t refreshDelta = sample.syncRefreshCount - m_lastSyncRefresh;
        const int64_t qpcDelta = sample.syncQpc - m_lastSyncQpc;
        if (refreshDelta == 0 || qpcDelta <= 0)
        {
            Rebaseline(sample, nowQpc);
            return;
        }

        const double intervalMs = static_cast<double>(qpcDelta) * 1000.0 /
            static_cast<double>(sample.qpcFrequency);
        const double intervalHz = static_cast<double>(refreshDelta) *
            static_cast<double>(sample.qpcFrequency) /
            static_cast<double>(qpcDelta);
        if (!std::isfinite(intervalHz) || intervalHz < kMinimumPlausibleHz ||
            intervalHz > kMaximumPlausibleHz || !std::isfinite(intervalMs) ||
            intervalMs <= 0.0)
        {
            Rebaseline(sample, nowQpc);
            return;
        }

        m_intervals.push_back({ intervalHz, qpcDelta });
        while (m_intervals.size() > kMaximumWindowIntervals)
            m_intervals.pop_front();
        if (m_snapshot.uniqueIntervals != UINT32_MAX)
            ++m_snapshot.uniqueIntervals;
        m_lastSyncRefresh = sample.syncRefreshCount;
        m_lastSyncQpc = sample.syncQpc;
        m_lastObservationQpc = nowQpc;
        m_hasLastSample = true;
        UpdateSnapshot(sample.expectedDisplayHz, sample.qpcFrequency);
    }

    Snapshot Get() const
    {
        return m_snapshot;
    }

private:
    struct Interval
    {
        double hz = 0.0;
        int64_t qpc = 0;
    };

    static constexpr size_t kMaximumWindowIntervals = 128;
    static constexpr size_t kMinimumIntervals = 5;
    static constexpr double kMinimumPlausibleHz = 10.0;
    static constexpr double kMaximumPlausibleHz = 500.0;
    static constexpr double kMinimumWindowMs = 200.0;
    static constexpr double kMismatchTolerance = 0.03;
    static constexpr double kStaleFloorMs = 250.0;
    static constexpr size_t kVoteNumerator = 4;
    static constexpr size_t kVoteDenominator = 5;

    static bool ValidRate(double hz)
    {
        return std::isfinite(hz) && hz >= kMinimumPlausibleHz &&
            hz <= kMaximumPlausibleHz;
    }

    static double TargetRateChangeThreshold(double expectedHz)
    {
        return (std::max)(0.05, expectedHz * 0.001);
    }

    static double StaleHorizonMs(double expectedHz)
    {
        const double targetStaleMs = 3000.0 / expectedHz;
        return targetStaleMs > kStaleFloorMs ? targetStaleMs :
            kStaleFloorMs;
    }

    static bool IsTimestampFresh(int64_t nowQpc, int64_t sampleQpc,
        int64_t qpcFrequency, double expectedHz)
    {
        if (nowQpc <= 0 || sampleQpc <= 0 || sampleQpc > nowQpc ||
            qpcFrequency <= 0 || !ValidRate(expectedHz))
            return false;
        const double ageMs = static_cast<double>(nowQpc - sampleQpc) *
            1000.0 / static_cast<double>(qpcFrequency);
        return std::isfinite(ageMs) && ageMs <= StaleHorizonMs(expectedHz);
    }

    static bool IsStale(int64_t nowQpc, int64_t lastObservationQpc,
        int64_t qpcFrequency, double expectedHz)
    {
        if (nowQpc < lastObservationQpc || qpcFrequency <= 0 ||
            !ValidRate(expectedHz))
            return true;
        const double ageMs = static_cast<double>(nowQpc - lastObservationQpc) *
            1000.0 / static_cast<double>(qpcFrequency);
        return ageMs > StaleHorizonMs(expectedHz);
    }

    void ClearWindow(State state)
    {
        m_snapshot.state = state;
        m_snapshot.observedHz = 0.0;
        m_snapshot.uniqueIntervals = 0;
        m_snapshot.windowMs = 0.0;
        m_lastSyncRefresh = 0;
        m_lastSyncQpc = 0;
        m_lastObservationQpc = 0;
        m_hasLastSample = false;
        m_intervals.clear();
    }

    void SetBaseline(const AlphaDxgiPresentationSample& sample, int64_t nowQpc)
    {
        m_lastSyncRefresh = sample.syncRefreshCount;
        m_lastSyncQpc = sample.syncQpc;
        m_lastObservationQpc = nowQpc;
        m_hasLastSample = true;
        m_snapshot.state = State::Warming;
        m_snapshot.observedHz = 0.0;
        m_snapshot.uniqueIntervals = 0;
        m_snapshot.windowMs = 0.0;
        m_intervals.clear();
    }

    void Rebaseline(const AlphaDxgiPresentationSample& sample, int64_t nowQpc)
    {
        ClearWindow(State::Unavailable);
        SetBaseline(sample, nowQpc);
    }

    void UpdateSnapshot(double expectedHz, int64_t qpcFrequency)
    {
        m_snapshot.expectedHz = expectedHz;
        std::array<double, kMaximumWindowIntervals> rates{};
        size_t windowCount = 0;
        int64_t windowQpc = 0;
        for (auto interval = m_intervals.rbegin();
            interval != m_intervals.rend(); ++interval)
        {
            rates[windowCount++] = interval->hz;
            windowQpc += interval->qpc;
            const double spanMs = static_cast<double>(windowQpc) * 1000.0 /
                static_cast<double>(qpcFrequency);
            if (windowCount >= kMinimumIntervals &&
                spanMs >= kMinimumWindowMs)
                break;
        }

        m_snapshot.windowMs = static_cast<double>(windowQpc) * 1000.0 /
            static_cast<double>(qpcFrequency);
        if (windowCount < kMinimumIntervals ||
            m_snapshot.windowMs < kMinimumWindowMs)
        {
            if (m_snapshot.state == State::Unavailable)
                m_snapshot.state = State::Warming;
            return;
        }

        std::sort(rates.begin(), rates.begin() + windowCount);
        if (windowCount % 2 == 0)
            m_snapshot.observedHz = (rates[windowCount / 2 - 1] +
                rates[windowCount / 2]) / 2.0;
        else
            m_snapshot.observedHz = rates[windowCount / 2];

        size_t matching = 0;
        size_t faster = 0;
        size_t slower = 0;
        for (size_t index = 0; index < windowCount; ++index)
        {
            const double relativeError =
                (rates[index] - expectedHz) / expectedHz;
            if (std::abs(relativeError) <= kMismatchTolerance)
                ++matching;
            else if (relativeError > 0.0)
                ++faster;
            else
                ++slower;
        }
        const size_t requiredVotes =
            (windowCount * kVoteNumerator + kVoteDenominator - 1) /
            kVoteDenominator;
        if (matching >= requiredVotes)
            m_snapshot.state = State::Healthy;
        else if (faster >= requiredVotes || slower >= requiredVotes)
            m_snapshot.state = State::Mismatch;
        else
            m_snapshot.state = State::Warming;
    }

    Snapshot m_snapshot;
    bool m_hasIdentity = false;
    uint64_t m_generation = 0;
    uint64_t m_outputEpoch = 0;
    int64_t m_qpcFrequency = 0;
    uint32_t m_lastSyncRefresh = 0;
    int64_t m_lastSyncQpc = 0;
    int64_t m_lastObservationQpc = 0;
    bool m_hasLastSample = false;
    bool m_hasStaleReplay = false;
    uint32_t m_staleSyncRefresh = 0;
    int64_t m_staleSyncQpc = 0;
    std::deque<Interval> m_intervals;
};

