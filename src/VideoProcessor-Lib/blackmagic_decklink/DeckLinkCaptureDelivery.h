#pragma once
#include <CaptureDeliveryStats.h>

// Capture-thread accounting; the device protects this object for OSD readers.
// Wall time measures delivery, hardware timestamps identify skipped input frames.
class DeckLinkCaptureDelivery
{
public:
    void Reset(double expectedFps = 0.0)
    {
        m_stats = {};
        m_stats.expectedFps = expectedFps;
        m_started = false;
        m_frames = m_missed = 0;
    }

    void SetExpectedFps(double expectedFps) { m_stats.expectedFps = expectedFps; }

    struct Observation { uint64_t missedFrames = 0; bool windowComplete = false; };
    Observation Observe(int64_t timestamp, uint32_t period, double nowSeconds)
    {
        if (timestamp < 0 || !period)
        {
            Reset(m_stats.expectedFps);
            return {};
        }
        // Duplicate/backward timestamps start a fresh window, never a huge loss.
        if (m_started && (timestamp <= m_previous || nowSeconds < m_lastArrival))
            Reset(m_stats.expectedFps);
        if (!m_started)
        {
            m_started = true;
            m_previous = timestamp;
            m_start = m_lastArrival = nowSeconds;
            return {};
        }
        Observation result;
        // This accumulator has an explicit first-sample flag, so hardware zero
        // is a valid anchor (unlike the legacy helper's zero sentinel).
        const uint64_t delta = uint64_t(timestamp) - uint64_t(m_previous);
        const uint64_t intervals = delta / period + ((delta % period) * 2 >= period);
        result.missedFrames = intervals > 1 ? intervals - 1 : 0;
        m_previous = timestamp;
        m_lastArrival = nowSeconds;
        ++m_frames;
        m_missed += result.missedFrames;
        const double seconds = nowSeconds - m_start;
        if (seconds >= 5.0)
        {
            m_stats.deliveredFps = m_frames / seconds;
            m_stats.windowSeconds = seconds;
            m_stats.recentMissedFrames = m_missed;
            m_frames = m_missed = 0;
            m_start = nowSeconds;
            result.windowComplete = true;
        }
        return result;
    }

    CaptureDeliveryStats Snapshot(double nowSeconds) const
    {
        auto stats = m_stats;
        // Do not leave a healthy completed window visible after callbacks stop.
        if (m_started && nowSeconds - m_lastArrival > 5.0)
        {
            stats.deliveredFps = stats.windowSeconds = 0.0;
            stats.recentMissedFrames = 0;
        }
        return stats;
    }

private:
    CaptureDeliveryStats m_stats;
    bool m_started = false;
    int64_t m_previous = 0;
    double m_start = 0.0, m_lastArrival = 0.0;
    uint64_t m_frames = 0, m_missed = 0;
};
