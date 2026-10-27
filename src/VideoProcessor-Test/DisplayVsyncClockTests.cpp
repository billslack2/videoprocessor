#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/AlphaQueuePolicy.h>
#include <vprenderer/DisplayVsyncClock.h>
#include <deque>
#include <limits>
#include <utility>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
    TEST_CLASS(DisplayVsyncClockTests)
    {
    public:
        TEST_METHOD(ScheduleIdentityChangesOnlyWhenTimingIsReanchored)
        {
            DisplayVsyncClock clock;
            const auto empty = clock.ScheduleSerial();
            clock.BeginSource(1.0, 1000000000, 1, 24.0, false);
            const auto first = clock.ScheduleSerial();
            Assert::IsTrue(first > empty);
            clock.Submitted();
            clock.BeginSource(1.0 + 1.0 / 24.0, 1041666667, 1, 24.0, false);
            clock.SkipMissed(1200000000);
            Assert::AreEqual(first, clock.ScheduleSerial());
            clock.Reset();
            const auto reset = clock.ScheduleSerial();
            Assert::IsTrue(reset > first);
            clock.BeginSource(2.0, 2000000000, 1, 24.0, false);
            const auto restarted = clock.ScheduleSerial();
            Assert::IsTrue(restarted > reset);
            clock.BeginSource(2.1, 2100000000, 2, 24.0, false);
            const auto newGeneration = clock.ScheduleSerial();
            Assert::IsTrue(newGeneration > restarted);
            clock.BeginSource(2.2, 2200000000, 2, 60.0, false);
            const auto newRate = clock.ScheduleSerial();
            Assert::IsTrue(newRate > newGeneration);
            clock.BeginSource(2.3, 2300000000, 2, 60.0, true);
            Assert::IsTrue(clock.ScheduleSerial() > newRate);
        }

        TEST_METHOD(FilmOn120HzRepeatsFourTimesBetweenSources)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(10.0, 1000000000, 1, 120.0, false);
            clock.Submitted();
            for (int repeat = 0; repeat < 4; ++repeat) {
                Assert::IsFalse(clock.SourceDue(10.0 + 1.0 / 24.0));
                clock.Submitted();
            }
            Assert::IsTrue(clock.SourceDue(10.0 + 1.0 / 24.0));
        }
        TEST_METHOD(FilmOn60HzUsesThreeTwoCadenceWithoutBlending)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, 1000000000, 1, 60.0, false);
            int counts[4]{};
            for (int frame = 0; frame < 4; ++frame)
                while (!clock.SourceDue((frame + 1) / 24.0)) {
                    ++counts[frame]; clock.Submitted();
                }
            Assert::AreEqual(3, counts[0]); Assert::AreEqual(2, counts[1]);
            Assert::AreEqual(3, counts[2]); Assert::AreEqual(2, counts[3]);
        }
        TEST_METHOD(SubmissionWaitPreservesFilmCadenceAt60Hz)
        {
            DisplayVsyncClock clock;
            const int64_t baseNs = 1000000000;
            clock.BeginSource(0.0, baseNs, 1, 60.0, false);
            int64_t nowNs = baseNs;
            int counts[4]{};
            for (int frame = 0; frame < 4; ++frame)
                while (!clock.SourceDue((frame + 1) / 24.0)) {
                    Assert::AreEqual(int64_t(0), clock.SubmissionWaitNs(nowNs, 2));
                    ++counts[frame];
                    clock.Submitted();
                    nowNs = clock.DeadlineNs();
                }
            Assert::AreEqual(3, counts[0]);
            Assert::AreEqual(2, counts[1]);
            Assert::AreEqual(3, counts[2]);
            Assert::AreEqual(2, counts[3]);
        }
        TEST_METHOD(MatchedQuantizedCaptureDoesNotAlternateRepeatsAndRetirement)
        {
            for (const double hz : {50.0,59.94097,60.0}) for (const double quantum : {1e-6,1e-5}) {
                DisplayVsyncClock clock;
                const double origin=1366400.12345;
                const int64_t started=1000000000;
                clock.BeginSource(origin,started,1,hz,false,quantum);
                clock.Submitted();
                uint64_t next=1;
                unsigned repeats=0,drops=0;
                auto pts=[&](uint64_t n) {
                    // Capture PTS has 1us or 10us precision; rounding must
                    // not become a repeat/drop pair.
                    return origin+std::round((n/hz)/quantum)*quantum;
                };
                for (uint64_t slot=1;slot<=2400;++slot) {
                    // Available queue lead absorbs variable, sub-refresh work
                    // such as enabling NLS. There is no actual missed slot.
                    const int64_t now=started+static_cast<int64_t>(slot*1e9/hz)-10000000+
                        static_cast<int64_t>((slot%8)*1000000);
                    clock.SkipMissed(now);
                    while (clock.SourceDue(pts(next+1))) { ++next; ++drops; }
                    if (clock.SourceDue(pts(next))) {
                        clock.BeginSource(pts(next),now,1,hz,false,quantum);
                        Assert::AreEqual(slot,next);
                        ++next;
                    } else ++repeats;
                    clock.Submitted();
                }
                Assert::AreEqual(0u,repeats);Assert::AreEqual(0u,drops);
            }
        }
        TEST_METHOD(TimestampToleranceDoesNotConsumeAFutureOrHideRealCadenceDrift)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0,1000000000,1,60,false);
            Assert::IsTrue(clock.SourceDue(0.000009));
            Assert::IsFalse(clock.SourceDue(0.004));
            unsigned repeats=0,drops=0;uint64_t next=1;
            clock.Submitted();
            for (uint64_t slot=1;slot<=60000;++slot) {
                while (clock.SourceDue((next+1)/59.94)) { ++next; ++drops; }
                if (clock.SourceDue(next/59.94)) ++next;
                else ++repeats;
                clock.Submitted();
            }
            // The source is 60 frames slower over 1000 seconds. The window
            // removes jitter only; it neither speeds up nor locks the source.
            Assert::AreEqual(60u,repeats);Assert::AreEqual(0u,drops);
        }
        TEST_METHOD(TimestampQuantumIsBoundedAndReplacedAcrossGenerationChanges)
        {
            DisplayVsyncClock clock;
            for (double quantum : {0.0,-1.0,static_cast<double>(NAN)}) {
                clock.BeginSource(0,1000000000,1,60,true,quantum);
                Assert::AreEqual(1e-5,clock.SourceToleranceSeconds(),1e-12);
            }
            clock.BeginSource(0,1000000000,1,60,true,1.0);
            Assert::AreEqual(1e-4,clock.SourceToleranceSeconds(),1e-12);
            Assert::IsFalse(clock.SourceDue(0.004));
            clock.BeginSource(2,2000000000,2,60,false,1e-6);
            Assert::AreEqual(1e-5,clock.SourceToleranceSeconds(),1e-12);
            Assert::IsFalse(clock.SourceDue(2.004));
        }
        TEST_METHOD(PhaseJitterCannotCauseMatchedRateRepeatRetirePairs)
        {
            for (const double hz : {24.0,59.94097,60.0}) {
                DisplayVsyncClock clock;
                auto pts=[&](uint64_t n) { return 100.0+n/hz+(n%2?-.00114:.00114); };
                clock.BeginSource(pts(0),1000000000,1,hz,false);
                clock.Submitted();
                uint64_t next=1;unsigned repeats=0,drops=0;
                for (uint64_t slot=1;slot<=10000;++slot) {
                    while (clock.CanRetireBefore(pts(next+1))) { ++next; ++drops; }
                    if (clock.SourceDue(pts(next))) ++next; else ++repeats;
                    clock.Submitted();
                }
                Assert::AreEqual(0u,repeats);Assert::AreEqual(0u,drops);
            }
        }
        TEST_METHOD(EligibilityDoesNotMeanThePredecessorCanBeRetired)
        {
            DisplayVsyncClock clock;clock.BeginSource(0,1000000000,1,24,false);
            Assert::IsTrue(clock.SourceDue(.00114));
            Assert::IsFalse(clock.CanRetireBefore(.00114));
            Assert::IsFalse(clock.CanRetireBefore(-.00114));
            Assert::IsTrue(clock.CanRetireBefore(-.004));
            Assert::IsFalse(clock.SourceDue(.004));
            clock.BeginSource(0,1000000000,2,360,false);
            Assert::IsTrue(clock.DecisionBandSeconds()<=.25/360);
        }
        TEST_METHOD(JitteredFilmCadenceKeepsThreeTwoPhaseAndRealDrift)
        {
            DisplayVsyncClock clock;clock.BeginSource(0,1000000000,1,60,false);
            int holds[4]{};
            for (int frame=0;frame<4;++frame)
                while (!clock.SourceDue((frame+1)/24.0+(frame%2?.00114:-.00114))) {
                    ++holds[frame];clock.Submitted();
                }
            Assert::AreEqual(3,holds[0]);Assert::AreEqual(2,holds[1]);
            Assert::AreEqual(3,holds[2]);Assert::AreEqual(2,holds[3]);
            // A genuinely faster independent input must still retire sources.
            clock.Reset();clock.BeginSource(0,1000000000,1,60,false);clock.Submitted();
            uint64_t next=1;unsigned drops=0;
            for (unsigned slot=1;slot<=60000;++slot) {
                while (clock.CanRetireBefore((next+1)/60.06)) { ++next; ++drops; }
                if (clock.SourceDue(next/60.06)) ++next;
                clock.Submitted();
            }
            Assert::IsTrue(drops>=59&&drops<=60);
        }
        TEST_METHOD(OutputChangeRecoveryWaitsForOneSuccessfulPresentAndCoalesces)
        {
            DisplayCadenceTransition transition;
            Assert::IsFalse(transition.Presented(true,true));
            transition.OutputChanged();transition.OutputChanged();
            Assert::IsFalse(transition.Presented(false,true));
            Assert::IsTrue(transition.Presented(true,true));
            Assert::IsFalse(transition.Presented(true,true));
            transition.OutputChanged();
            Assert::IsFalse(transition.Presented(true,false));
            Assert::IsFalse(transition.Presented(true,true));
        }
        TEST_METHOD(DiscontinuityGenerationAndRateChangesReanchor)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(10.0, 1000000000, 1, 120.0, false);
            clock.Submitted();
            clock.BeginSource(2.0, 2000000000, 1, 120.0, true);
            Assert::AreEqual(2.0, clock.SourcePts(), 1e-9);
            clock.BeginSource(3.0, 3000000000, 2, 60.0, false);
            Assert::IsFalse(clock.Matches(1, 60.0)); Assert::IsTrue(clock.Matches(2, 60.0));
            clock.BeginSource(3.01, 3010000000, 2, 120.0, false);
            Assert::IsTrue(clock.Matches(2, 120.0));
            Assert::AreEqual(3.01, clock.SourcePts(), 1e-9);
        }
        TEST_METHOD(StallRetiresMissedRefreshesWithoutCatchupBurst)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, 1000000000, 1, 60.0, false);
            clock.Submitted(); clock.SkipMissed(1500000000);
            Assert::AreEqual(0.5, clock.SourcePts(), 1e-9);
            clock.Submitted(); Assert::IsTrue(clock.DeadlineNs() > 1500000000);
        }
        TEST_METHOD(SubmissionWaitBoundsFastPresentsWithoutFeedback)
        {
            const double targetHz = 24000.0 / 1001.0;
            const unsigned depth = 2;
            const int64_t baseNs = 1000000000;
            const int64_t fastPresentIntervalNs = 16666667;
            const double periodSeconds = 1.0 / targetHz;
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, baseNs, 1, targetHz, false);
            int64_t nowNs = baseNs;
            bool waited = false;
            for (unsigned attempt = 0; attempt < 600; ++attempt) {
                nowNs += fastPresentIntervalNs;
                const int64_t waitNs = clock.SubmissionWaitNs(nowNs, depth);
                if (waitNs > 0) { waited = true; nowNs += waitNs; }
                clock.Submitted();
                const double leadSeconds = clock.SourcePts() -
                    static_cast<double>(nowNs - baseNs) / 1e9;
                Assert::IsTrue(leadSeconds <= (depth + 1.0) * periodSeconds + 1e-8);
            }
            Assert::IsTrue(waited);
        }
        TEST_METHOD(SubmissionWaitAllowsMatched24And60HzWorkAhead)
        {
            const int64_t baseNs = 1000000000;
            for (const auto& rates : {std::pair<double, double>{24000.0 / 1001.0, 24.0},
                                      std::pair<double, double>{60.0, 60.0}}) {
                DisplayVsyncClock clock;
                clock.BeginSource(0.0, baseNs, 1, rates.first, false);
                const int64_t intervalNs = static_cast<int64_t>(
                    std::llround(1e9 / rates.second));
                int64_t nowNs = baseNs;
                for (unsigned slot = 0; slot < 240; ++slot) {
                    Assert::AreEqual(int64_t(0), clock.SubmissionWaitNs(nowNs, 2));
                    clock.Submitted();
                    nowNs += intervalNs;
                }
            }
        }
        TEST_METHOD(MatchedNear60HzLowWaterDoesNotCreateRepeatsOrRetirements)
        {
            struct SimulationResult
            {
                unsigned sourcePresents = 0;
                unsigned cachedRepeats = 0;
                unsigned retiredSources = 0;
            };

            auto simulate = [](double sourceHz, double displayHz,
                unsigned sourcePresentsToRun)
            {
                constexpr int64_t baseNs = 1'000'000'000LL;
                constexpr size_t desiredDepth = 3;
                constexpr unsigned presentDepth = 2;
                constexpr double timestampQuantum = 1e-5;
                constexpr int64_t workNs = 300'000;
                constexpr int64_t callbackJitterNs[] = {
                    100'000, 900'000, 200'000, 1'200'000,
                    400'000, 1'500'000, 300'000, 1'000'000 };

                DisplayVsyncClock clock;
                clock.BeginSource(0.0, baseNs, 7, displayHz, false,
                    timestampQuantum);
                auto sourcePts = [&](uint64_t sequence)
                {
                    const double raw = static_cast<double>(sequence) / sourceHz;
                    return std::round(raw / timestampQuantum) * timestampQuantum;
                };
                auto arrivalNs = [&](uint64_t sequence)
                {
                    const int64_t sourcePeriodNs = static_cast<int64_t>(
                        std::llround(1e9 / sourceHz));
                    const int64_t periodsAfterPrefill =
                        static_cast<int64_t>(sequence) - 2;
                    const int64_t jitter = callbackJitterNs[
                        sequence % (sizeof(callbackJitterNs) / sizeof(callbackJitterNs[0]))];
                    return baseNs + periodsAfterPrefill * sourcePeriodNs + jitter;
                };

                std::deque<double> queue;
                for (uint64_t sequence = 0; sequence < desiredDepth; ++sequence)
                    queue.push_back(sourcePts(sequence));
                uint64_t nextCapture = desiredDepth;
                int64_t nowNs = baseNs;
                SimulationResult result;

                auto publishArrivalsThrough = [&](int64_t throughNs)
                {
                    while (nextCapture < sourcePresentsToRun + 32 &&
                        arrivalNs(nextCapture) <= throughNs)
                    {
                        queue.push_back(sourcePts(nextCapture));
                        ++nextCapture;
                    }
                };

                for (unsigned iteration = 0;
                    result.sourcePresents < sourcePresentsToRun &&
                        iteration < sourcePresentsToRun * 8 + 64;
                    ++iteration)
                {
                    // Model bounded Present(1) backpressure. Capture callbacks
                    // can fill the queue while the render thread waits.
                    clock.SkipMissed(nowNs);
                    const int64_t waitNs = clock.SubmissionWaitNs(
                        nowNs, presentDepth);
                    if (waitNs > 0)
                    {
                        nowNs += waitNs;
                        publishArrivalsThrough(nowNs);
                    }
                    publishArrivalsThrough(nowNs);

                    auto canDequeue = [&]()
                    {
                        return AlphaQueuePolicy::CanDequeuePrepared(
                            queue.size(), desiredDepth, false,
                            false, false, false);
                    };

                    // Match the renderer's stale-head retirement rule.
                    while (queue.size() > desiredDepth && queue.size() >= 2 &&
                        canDequeue() && clock.CanRetireBefore(queue[1]))
                    {
                        queue.pop_front();
                        ++result.retiredSources;
                    }

                    if (canDequeue() && !queue.empty() &&
                        clock.SourceDue(queue.front()))
                    {
                        queue.pop_front();
                        ++result.sourcePresents;
                        clock.Submitted();
                        nowNs += workNs;
                        continue;
                    }

                    const bool repeat = ShouldPresentDisplayRepeat(clock,
                        !queue.empty(), false,
                        queue.empty() ? NAN : queue.front());
                    if (repeat)
                    {
                        ++result.cachedRepeats;
                        clock.Submitted();
                        nowNs += workNs;
                        continue;
                    }

                    // A due head below healthy low-water waits for capture to
                    // restore dequeue eligibility instead of presenting a copy.
                    if (nextCapture >= sourcePresentsToRun + 32)
                        break;
                    nowNs = arrivalNs(nextCapture);
                    publishArrivalsThrough(nowNs);
                }
                return result;
            };

            const double displayHz = 60000.0 / 1001.0;
            const auto matched = simulate(59.94097, displayHz, 600);
            Assert::AreEqual(600u, matched.sourcePresents);
            Assert::AreEqual(0u, matched.retiredSources);
            Assert::AreEqual(0u, matched.cachedRepeats);

            DisplayVsyncClock policy;
            const int64_t policyBaseNs = 1'000'000'000LL;
            policy.BeginSource(0.0, policyBaseNs, 9, displayHz, false);
            policy.Submitted();
            const double dueHeadPts = 1.0 / displayHz;
            Assert::IsTrue(policy.SourceDue(dueHeadPts));
            Assert::IsFalse(ShouldPresentDisplayRepeat(policy,
                true, false, dueHeadPts));
            Assert::IsTrue(ShouldPresentDisplayRepeat(policy,
                true, false, 2.0 / displayHz));
            Assert::IsFalse(ShouldPresentDisplayRepeat(policy,
                true, true, dueHeadPts));
            Assert::IsTrue(ShouldPresentDisplayRepeat(policy,
                false, false, NAN));
            Assert::IsFalse(ShouldPresentDisplayRepeat(policy,
                true, false, NAN));

            // Delayed DXGI feedback cannot rephase the current slot estimate.
            const int64_t priorDeadlineNs = policy.DeadlineNs();
            Assert::IsFalse(policy.ObserveQueueTail(10, 9, 90, 90,
                policyBaseNs - 300'000'000LL, policyBaseNs,
                policyBaseNs, 2));
            Assert::AreEqual(priorDeadlineNs, policy.DeadlineNs());

            // A new output/source epoch starts a fresh clock domain.
            policy.BeginSource(0.0, policyBaseNs + 100'000'000LL,
                10, 60.0, false);
            Assert::IsFalse(policy.Matches(9, 60.0));
            Assert::IsTrue(policy.Matches(10, 60.0));
            Assert::AreEqual(0.0, policy.SourcePts(), 1e-9);
            // Preserve repeats when the source is genuinely ahead of its slot,
            // and when no source is queued.
            DisplayVsyncClock film;
            film.BeginSource(0.0, 1'000'000'000LL, 8, 60.0, false);
            Assert::IsTrue(ShouldPresentDisplayRepeat(film,
                true, false, 1.0 / 24.0));
            Assert::IsTrue(ShouldPresentDisplayRepeat(film,
                false, false, NAN));
        }
        TEST_METHOD(SubmissionWaitClampsDepthAndBoundsPostStallWorkAhead)
        {
            DisplayVsyncClock clock;
            const int64_t baseNs = 1000000000;
            clock.BeginSource(0.0, baseNs, 1, 60.0, false);
            for (int slot = 0; slot < 10; ++slot) clock.Submitted();
            Assert::AreEqual(clock.SubmissionWaitNs(baseNs, 1),
                clock.SubmissionWaitNs(baseNs, 0));
            Assert::AreEqual(clock.SubmissionWaitNs(baseNs, 8),
                clock.SubmissionWaitNs(baseNs, 100));

            clock.Reset();
            clock.BeginSource(0.0, baseNs, 1, 60.0, false);
            clock.Submitted();
            const int64_t afterStallNs = baseNs + 500000000;
            clock.SkipMissed(afterStallNs);
            unsigned immediateSlots = 0;
            int64_t waitNs = 0;
            do {
                waitNs = clock.SubmissionWaitNs(afterStallNs, 2);
                if (waitNs > 0) break;
                clock.Submitted();
                ++immediateSlots;
            } while (immediateSlots <= 3);
            Assert::AreEqual(3u, immediateSlots);
            Assert::IsTrue(waitNs > 0);
        }
        TEST_METHOD(IndependentSourceClockDriftDoesNotReanchorPlayback)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, 1000000000, 1, 60.0, false);
            for (int frame = 1; frame <= 10000; ++frame) {
                clock.Submitted();
                clock.BeginSource(frame / 59.94,
                    1000000000LL + static_cast<int64_t>(frame * 1e9 / 60.0), 1, 60.0, false);
            }
            Assert::AreEqual(10000.0 / 60.0, clock.SourcePts(), 1e-9);
        }
        TEST_METHOD(BrokenSourceTimestampsUseOneAnchoredDomainForPeekAndAccept)
        {
            DisplaySourceClock source;
            Assert::AreEqual(10.0,source.Accept(10.0,100,24,1,false),1e-9);
            const double peek=source.Preview(10.0,101,24,1);
            Assert::AreEqual(10.0+1.0/24,peek,1e-9);
            Assert::AreEqual(peek,source.Accept(10.0,101,24,1,false),1e-9);
            Assert::IsTrue(source.Synthetic());
            Assert::AreEqual(10.0+2.0/24,source.Accept(1.0,102,24,1,false),1e-9);
            Assert::AreEqual(10.0+3.0/24,source.Accept(200.0,103,24,1,false),1e-9);
            Assert::AreEqual(2.0,source.Accept(2.0,1,24,2,false),1e-9);
            Assert::IsFalse(source.Synthetic());
        }
        TEST_METHOD(MissingTimestampsAndCadenceRepeatsShareTheSameFallback)
        {
            DisplaySourceClock source;
            Assert::AreEqual(4.0,source.Accept(NAN,96,24,1,false),1e-9);
            Assert::AreEqual(4.0,source.Preview(NAN,96,24,1),1e-9);
            const double next=source.Preview(NAN,97,24,1);
            Assert::AreEqual(4.0+1.0/24,next,1e-9);
            Assert::AreEqual(next,source.Accept(NAN,97,24,1,false),1e-9);
            Assert::AreEqual(0.0,source.Accept(0.0,0,24,1,true),1e-9);
        }
        TEST_METHOD(HealthySourceClockKeepsActualDriftAndRejectsBackwardJitter)
        {
            DisplaySourceClock source;
            source.Accept(1.0,1,60,1,false);
            Assert::AreEqual(1.0+1.0/59.94,source.Accept(1.0+1.0/59.94,2,60,1,false),1e-9);
            Assert::IsFalse(source.Synthetic());
            const double previous=1.0+1.0/59.94;
            Assert::AreEqual(previous+1.0/60,source.Accept(previous-1e-6,3,60,1,false),1e-9);
            Assert::IsTrue(source.Synthetic());
        }
        TEST_METHOD(HighRefreshRepeatsNominalFieldPicturesWithoutExtraTemporalWork)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0.0,1000000000,1,360,false);
            int slots=0;
            while(!clock.SourceDue(1.0/40)) { ++slots; clock.Submitted(); }
            Assert::AreEqual(9,slots);
            Assert::AreEqual(1.0/40,clock.SourcePts(),1e-9);
        }
        TEST_METHOD(FractionalFilmRatesKeepCadenceWithoutRetimingToDisplay)
        {
            for(double hz:{119.88,120.0}) {
                DisplayVsyncClock clock;
                clock.BeginSource(0,1000000000,1,hz,false);
                int slots=0;
                for(int source=1;source<=23976;++source)
                    while(!clock.SourceDue(source/23.976)) { clock.Submitted(); ++slots; }
                Assert::AreEqual(hz==120.0?120000:119880,slots);
            }
        }
        TEST_METHOD(FeedbackRequiresStableSamplesAndAccountsForQueuedLead)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0,1000000000,1,100,false);
            clock.Submitted();
            Assert::IsFalse(clock.ObserveQueueTail(10,9,90,90,1000000000,1000000000,1000000000,2));
            clock.Submitted();
            Assert::IsFalse(clock.ObserveQueueTail(11,10,91,91,1010000000,1010000000,1010000000,2));
            clock.Submitted();
            Assert::IsTrue(clock.ObserveQueueTail(12,11,92,92,1020000000,1020000000,1020000000,2));
            Assert::IsTrue(clock.HasFeedback());
            Assert::AreEqual(int64_t(1040000000),clock.DeadlineNs());
            Assert::AreEqual(int64_t(0),clock.SubmissionWaitNs(1020000000,2));
            Assert::AreEqual(int64_t(10000000),clock.SubmissionWaitNs(1020000000,1));
            clock.SkipMissed(1030000000);
            Assert::AreEqual(0.03,clock.SourcePts(),1e-9);
            Assert::IsFalse(clock.ObserveQueueTail(30,11,92,92,1020000000,1020000000,1020000000,2));
            clock.InvalidateFeedback(); Assert::IsFalse(clock.HasFeedback());
        }
        TEST_METHOD(FutureFeedbackCannotRephaseTheSlotClock)
        {
            const int64_t baseNs = 1000000000;
            const int64_t intervalNs = 16666667;
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, baseNs, 1, 60.0, false);
            for (int i = 0; i < 3; ++i) clock.Submitted();
            const int64_t originalDeadlineNs = clock.DeadlineNs();

            for (int i = 0; i < 3; ++i) {
                const int64_t nowNs = baseNs + i * intervalNs;
                const int64_t futureSyncNs = nowNs + 50000000;
                Assert::IsFalse(clock.ObserveQueueTail(10u + i, 9u + i,
                    90u + i, 91u + i, futureSyncNs, nowNs, nowNs, 2));
                Assert::IsFalse(clock.HasFeedback());
                Assert::AreEqual(originalDeadlineNs, clock.DeadlineNs());
            }
        }
        TEST_METHOD(StaleFeedbackClearsPriorQualificationAndFreshSamplesCanRequalify)
        {
            const int64_t baseNs = 1000000000;
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, baseNs, 1, 100.0, false);
            for (int i = 0; i < 3; ++i) clock.Submitted();

            Assert::IsFalse(clock.ObserveQueueTail(10, 9, 90, 91,
                baseNs, baseNs, baseNs, 2));
            Assert::IsFalse(clock.ObserveQueueTail(11, 10, 91, 92,
                baseNs + 10000000, baseNs + 10000000,
                baseNs + 10000000, 2));
            Assert::IsFalse(clock.HasFeedback());

            const int64_t staleNowNs = baseNs + 300000000;
            Assert::IsFalse(clock.ObserveQueueTail(12, 11, 92, 93,
                baseNs + 10000000, staleNowNs, staleNowNs, 2));
            Assert::IsFalse(clock.HasFeedback());

            // This sample would complete the old two-interval qualification if
            // the stale rejection left its prior sample count and timestamp.
            Assert::IsFalse(clock.ObserveQueueTail(13, 12, 121, 122,
                staleNowNs + 10000000, staleNowNs + 10000000,
                staleNowNs + 10000000, 2));
            Assert::IsFalse(clock.HasFeedback());
            Assert::IsFalse(clock.ObserveQueueTail(14, 13, 122, 123,
                staleNowNs + 20000000, staleNowNs + 20000000,
                staleNowNs + 20000000, 2));
            Assert::IsFalse(clock.HasFeedback());
            Assert::IsTrue(clock.ObserveQueueTail(15, 14, 123, 124,
                staleNowNs + 30000000, staleNowNs + 30000000,
                staleNowNs + 30000000, 2));
            Assert::IsTrue(clock.HasFeedback());
        }
        TEST_METHOD(NonfiniteTimestampCannotDrainAnActiveSlotClock)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(1.0,1000000000,1,60,false);
            Assert::IsFalse(clock.SourceDue(NAN));
        }
        TEST_METHOD(UnknownOrInvalidRefreshDisablesTheClock)
        {
            DisplayVsyncClock clock;
            for (double hz : {0.0, -1.0, 1000.0, std::numeric_limits<double>::quiet_NaN()}) {
                clock.BeginSource(1.0, 1000000000, 1, hz, false);
                Assert::IsFalse(clock.Active());
            }
            clock.BeginSource(1.0, 1000000000, 1, 60.0, false);
            clock.Reset(); Assert::IsFalse(clock.Active());
        }
    };
}
