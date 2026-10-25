#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Render-thread-only presentation clock. Source PTS selects the image; the
// display clock selects submission times. No playback-speed or audio adjustment.
class DisplayVsyncClock
{
public:
    void Reset() { *this = {}; }
    bool Active() const { return periodNs > 0.0; }
    bool Matches(uint64_t epoch, double hz) const
    {
        return Active() && generation == epoch && ValidRate(hz) &&
            std::abs(hz - refreshHz) <= (std::max)(0.05, refreshHz * 0.001);
    }
    static bool ValidRate(double hz)
    {
        return std::isfinite(hz) && hz >= 10.0 && hz <= 360.0;
    }
    void BeginSource(double pts, int64_t nowNs, uint64_t epoch, double hz,
        bool discontinuity)
    {
        if (!std::isfinite(pts) || !ValidRate(hz)) { Reset(); return; }
        if (!Matches(epoch, hz) || discontinuity ||
            pts < lastSourcePts || pts - lastSourcePts > 0.5)
        {
            sourceOrigin = pts;
            wallOriginNs = nowNs;
            generation = epoch;
            refreshHz = hz;
            periodNs = 1e9 / hz;
            tick = 0;
        }
        lastSourcePts = pts;
        SkipMissed(nowNs);
    }
    // Missed deadlines are retired, never repaid with a burst of presents.
    void SkipMissed(int64_t nowNs)
    {
        if (!Active() || nowNs <= DeadlineNs()) return;
        const double elapsedTicks = (nowNs - wallOriginNs) / periodNs;
        if (elapsedTicks >= static_cast<double>(tick) + 1.0)
            tick = static_cast<uint64_t>(std::floor(elapsedTicks + 1e-9));
    }
    int64_t DeadlineNs() const
    {
        return wallOriginNs + static_cast<int64_t>(std::llround(tick * periodNs));
    }
    double SourcePts() const { return sourceOrigin + tick / refreshHz; }
    bool SourceDue(double pts) const
    {
        return !Active() || !std::isfinite(pts) || pts <= SourcePts() + 1e-7;
    }
    void Submitted() { if (Active()) ++tick; }
private:
    uint64_t generation = 0, tick = 0;
    int64_t wallOriginNs = 0;
    double sourceOrigin = 0.0, lastSourcePts = 0.0;
    double refreshHz = 0.0, periodNs = 0.0;
};
