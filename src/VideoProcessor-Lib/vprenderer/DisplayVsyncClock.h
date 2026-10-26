#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Render-thread-only next-unsubmitted-slot estimate, not scanout confirmation.
// Present(1) supplies bounded FIFO backpressure; this class never sleeps.
// Elapsed time only retires overdue slots after a stall. Source speed is unchanged.
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
        bool discontinuity, double timestampQuantumSeconds = 0.0)
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
        // API timebase and device precision can differ. The captured DeckLink
        // PTS in the incident used a 10us grid despite its 1MHz API timebase.
        // Bound the uncertainty to 100us, far below a refresh interval.
        sourceTolerance = std::isfinite(timestampQuantumSeconds) ?
            (std::max)(1e-5, (std::min)(timestampQuantumSeconds, 1e-4)) : 1e-5;
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
    // Leave bounded room for the configured presentation queue. This is a
    // work-ahead allowance, not permission to advance the clock indefinitely
    // when Present returns faster than the selected display cadence.
    int64_t SubmissionWaitNs(int64_t nowNs, unsigned presentationDepth) const
    {
        if (!Active()) return 0;
        const unsigned depth = (std::max)(1u, (std::min)(presentationDepth, 8u));
        const long double waitNs = static_cast<long double>(DeadlineNs()) -
            static_cast<long double>(nowNs) -
            static_cast<long double>(depth) * static_cast<long double>(periodNs);
        if (waitNs <= 0.0L) return 0;
        const long double maxNs = static_cast<long double>(
            (std::numeric_limits<int64_t>::max)());
        if (waitNs >= maxNs) return (std::numeric_limits<int64_t>::max)();
        return static_cast<int64_t>(std::ceil(waitNs));
    }
    double SourcePts() const { return sourceOrigin + tick / refreshHz; }
    double DecisionBandSeconds() const
    {
        // Raw capture phase can move by milliseconds, even with precise ticks.
        // Keep a bounded uncertainty band instead of toggling an entire refresh
        // because a timestamp moved across a binary boundary. At high refresh
        // rates the band remains at most one quarter of a display interval.
        return Active() ? (std::max)(sourceTolerance,(std::min)(0.003,0.25/refreshHz)) : sourceTolerance;
    }
    bool SourceDue(double pts) const
    {
        return !Active() || (std::isfinite(pts) && pts <= SourcePts() + DecisionBandSeconds());
    }
    bool CanRetireBefore(double nextPts) const
    {
        // A merely eligible successor is not evidence that its predecessor is
        // stale. Require it to be unambiguously before the slot's other edge.
        return Active() && std::isfinite(nextPts) && nextPts < SourcePts() - DecisionBandSeconds();
    }
    void Submitted() { if (Active()) ++tick; }
    double SourceToleranceSeconds() const { return sourceTolerance; }
    bool HasFeedback() const { return feedback; }
    void InvalidateFeedback() { feedback=false; feedbackSamples=0; lastSyncNs=0; }
    // mpv's guarded estimate of the last queued flip's display time. DXGI
    // counts are wrapping UINTs. Feedback disciplines only missed-slot timing;
    // it never changes the source origin or asserts that a flip was displayed.
    bool ObserveQueueTail(uint32_t lastPresent, uint32_t displayedPresent,
        uint32_t displayedRefresh, uint32_t syncRefresh, int64_t syncNs,
        int64_t nowNs, int64_t submittedNs, unsigned cap)
    {
        feedback=false;
        if (!Active() || !tick || !lastPresent || !displayedPresent ||
            !displayedRefresh || !syncRefresh || syncNs<=0 || cap<1 || cap>8) return false;
        const uint32_t queued=lastPresent-displayedPresent;
        const uint32_t elapsed=syncRefresh-displayedRefresh;
        if (queued>cap+1 || elapsed>cap+1 || queued<elapsed) return false;
        const int64_t tail=syncNs+static_cast<int64_t>(std::llround((queued-elapsed)*periodNs));
        if (tail<submittedNs || tail<nowNs-2*periodNs || tail>nowNs+(cap+2)*periodNs) return false;
        if (lastSyncNs && syncNs<lastSyncNs) { InvalidateFeedback(); return false; }
        if (syncNs!=lastSyncNs) {
            if (lastSyncNs) {
                const uint32_t refreshes=syncRefresh-lastSyncRefresh;
                const double measured=refreshes*1e9/static_cast<double>(syncNs-lastSyncNs);
                if (!refreshes || refreshes>32 ||
                    std::abs(measured-refreshHz)>(std::max)(0.2,refreshHz*0.02)) {
                    InvalidateFeedback(); return false;
                }
                ++feedbackSamples;
            }
            lastSyncNs=syncNs; lastSyncRefresh=syncRefresh;
        }
        if (feedbackSamples<2) return false;
        wallOriginNs=tail-static_cast<int64_t>(std::llround((tick-1)*periodNs));
        feedback=true;
        return true;
    }
private:
    uint64_t generation = 0, tick = 0;
    int64_t wallOriginNs = 0;
    double sourceOrigin = 0.0, lastSourcePts = 0.0;
    double refreshHz = 0.0, periodNs = 0.0, sourceTolerance = 1e-5;
    bool feedback=false;
    unsigned feedbackSamples=0;
    int64_t lastSyncNs=0;
    uint32_t lastSyncRefresh=0;
};

// Shared by queue peeking and source acceptance. Once a timestamp domain is
// broken, keep a sequence/rate fallback anchored to the previous accepted PTS
// until the transport generation changes. Do not compare raw and fallback PTS.
class DisplaySourceClock
{
public:
    void Reset() { *this = {}; }
    bool Synthetic() const { return synthetic; }
    double Select(bool useRepeatCadence,double raw,uint64_t seq,double hz,
        uint64_t epoch,bool discontinuity)
    {
        if (useRepeatCadence) return Accept(raw,seq,hz,epoch,discontinuity);
        Reset();
        return std::isfinite(raw)?raw:static_cast<double>(seq)/hz;
    }
    double Preview(double raw, uint64_t seq, double hz, uint64_t epoch) const
    {
        if (!valid || epoch != generation)
            return std::isfinite(raw) ? raw : Fallback(seq, hz);
        if (seq == sequence) return normalized;
        const double step = seq > sequence ? Fallback(seq - sequence, hz) : NAN;
        const double delta = raw - rawPts;
        const bool plausible = !synthetic && std::isfinite(raw) &&
            std::isfinite(rawPts) && delta > 0 &&
            (!std::isfinite(step) || std::abs(delta-step) <= (std::max)(0.5,step*0.5));
        return plausible ? normalized + delta : normalized + step;
    }
    double Accept(double raw, uint64_t seq, double hz, uint64_t epoch, bool reset)
    {
        if (reset || epoch != generation) Reset();
        const double next = Preview(raw, seq, hz, epoch);
        if (valid && seq != sequence &&
            (!std::isfinite(raw) || !std::isfinite(rawPts) || raw <= rawPts ||
             (std::isfinite(next) && std::abs((raw-rawPts)-(next-normalized)) > 1e-7)))
            synthetic = true;
        if (!valid) synthetic = !std::isfinite(raw);
        if (std::isfinite(next)) {
            valid=true; generation=epoch; sequence=seq; normalized=next; rawPts=raw;
        }
        return next;
    }
private:
    static double Fallback(uint64_t seq, double hz)
    {
        return std::isfinite(hz) && hz > 0 ? static_cast<double>(seq)/hz : NAN;
    }
    bool valid=false, synthetic=false;
    uint64_t generation=0, sequence=0;
    double rawPts=0, normalized=0;
};

// A profile transition can compile shaders or block in the driver. Retire its
// timing estimate after one successful changed output, then anchor fresh on
// the next source. Failed presents must not consume this recovery request.
class DisplayCadenceTransition
{
public:
    void OutputChanged() { pending=true; }
    bool Presented(bool successful,bool timingActive) {
        if (!successful) return false;
        const bool restart=pending&&timingActive;
        pending=false;
        return restart;
    }
private:
    bool pending=false;
};
