#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/DisplayCadenceHealth.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(DisplayCadenceHealthTests)
    {
    public:
        static AlphaDxgiPresentationSample Sample(uint32_t syncRefresh,
            int64_t syncQpc, double expectedHz = 60.0, uint64_t generation = 1)
        {
            AlphaDxgiPresentationSample sample;
            sample.generation = generation;
            sample.timingStatus = AlphaPresentationTimingStatus::Available;
            sample.available = true;
            sample.syncRefreshCount = syncRefresh;
            sample.syncQpc = syncQpc;
            sample.qpcFrequency = 60000;
            sample.expectedDisplayHz = expectedHz;
            return sample;
        }

        static void AddIntervals(DisplayCadenceHealth& health,
            AlphaDxgiPresentationSample& sample, uint32_t refreshDelta,
            int64_t qpcDelta, unsigned count, uint64_t outputEpoch = 1)
        {
            for (unsigned index = 0; index < count; ++index)
            {
                sample.syncRefreshCount += refreshDelta;
                sample.syncQpc += qpcDelta;
                health.Observe(sample, sample.syncQpc, outputEpoch);
            }
        }

        static void AddIntervalsAtOffset(DisplayCadenceHealth& health,
            AlphaDxgiPresentationSample& sample, uint32_t refreshDelta,
            int64_t qpcDelta, unsigned count, int64_t nowOffsetFromSampleQpc,
            uint64_t outputEpoch = 1)
        {
            for (unsigned index = 0; index < count; ++index)
            {
                sample.syncRefreshCount += refreshDelta;
                sample.syncQpc += qpcDelta;
                health.Observe(sample,
                    sample.syncQpc + nowOffsetFromSampleQpc, outputEpoch);
            }
        }

        TEST_METHOD(PersistentSixtyHzAgainstTwentyFourTargetIsDetectedWithinWindow)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(100, 60000, 23.976);
            health.Observe(sample, sample.syncQpc, 1);
            // At a 60 Hz observation cadence, twelve intervals span 200 ms.
            AddIntervals(health, sample, 1, 1000, 12);

            const auto snapshot = health.Get();
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Mismatch),
                static_cast<int>(snapshot.state));
            Assert::AreEqual(23.976, snapshot.expectedHz, 0.001);
            Assert::AreEqual(60.0, snapshot.observedHz, 0.001);
            Assert::AreEqual(static_cast<uint32_t>(12), snapshot.uniqueIntervals);
            Assert::IsTrue(snapshot.windowMs >= 200.0);
        }

        TEST_METHOD(RecordedQuantizedSixtyHzCadenceMismatchesTwentyFourHzTarget)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(49877001, 14005801500192LL, 23.976);
            sample.qpcFrequency = 10000000;
            health.Observe(sample, sample.syncQpc, 1);

            // Reproduce quantized refresh/QPC pairs near the logged 59.95 Hz
            // cadence: 2 refreshes per 333611 QPC ticks, then 1 per 166805.
            for (unsigned index = 0; index < 6; ++index)
            {
                sample.syncRefreshCount += 2;
                sample.syncQpc += 333611;
                health.Observe(sample, sample.syncQpc, 1);
            }
            for (unsigned index = 0; index < 6; ++index)
            {
                ++sample.syncRefreshCount;
                sample.syncQpc += 166805;
                health.Observe(sample, sample.syncQpc, 1);
            }

            const auto snapshot = health.Get();
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Mismatch),
                static_cast<int>(snapshot.state));
            Assert::AreEqual(23.976, snapshot.expectedHz, 0.001);
            Assert::IsTrue(snapshot.observedHz > 59.9 && snapshot.observedHz < 60.0);
            Assert::IsTrue(snapshot.uniqueIntervals >= 9);
            Assert::IsTrue(snapshot.windowMs >= 200.0);
        }
        TEST_METHOD(FilmSourceRateIsNotComparedToSixtyHertzDisplay)
        {
            // AlphaDxgiPresentationSample carries the display target and sync
            // counters, not source FPS. A 23.976 source is healthy when the
            // selected display and DXGI sync both run at 60 Hz.
            DisplayCadenceHealth health;
            auto sample = Sample(0, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            AddIntervals(health, sample, 1, 1000, 12);

            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(60.0, health.Get().observedHz, 0.001);
            Assert::IsTrue(health.Get().windowMs >= 200.0);
        }

        TEST_METHOD(SampleAgeHorizonIncludesTheBoundaryAndRejectsTheNextQpcTick)
        {
            struct HorizonCase
            {
                double targetHz;
                int64_t horizonQpc;
            };
            // 60 Hz uses the 250 ms floor; 10 Hz uses three target periods.
            const HorizonCase cases[] = {
                { 60.0, 15000 },
                { 10.0, 18000 }
            };
            for (const HorizonCase& horizon : cases)
            {
                auto sample = Sample(10, 600000, horizon.targetHz);
                DisplayCadenceHealth atBoundary;
                atBoundary.Observe(sample,
                    sample.syncQpc + horizon.horizonQpc, 1);
                Assert::AreEqual(static_cast<int>(
                        DisplayCadenceHealth::State::Warming),
                    static_cast<int>(atBoundary.Get().state));

                DisplayCadenceHealth beyondBoundary;
                beyondBoundary.Observe(sample,
                    sample.syncQpc + horizon.horizonQpc + 1, 1);
                Assert::AreEqual(static_cast<int>(
                        DisplayCadenceHealth::State::Unavailable),
                    static_cast<int>(beyondBoundary.Get().state));
            }

            auto sample = Sample(10, 600000, 60.0);
            DisplayCadenceHealth equalTimestamp;
            equalTimestamp.Observe(sample, sample.syncQpc, 1);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Warming),
                static_cast<int>(equalTimestamp.Get().state));

            DisplayCadenceHealth futureTimestamp;
            futureTimestamp.Observe(sample, sample.syncQpc - 1, 1);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(futureTimestamp.Get().state));
        }

        TEST_METHOD(AdvancingFiveSecondOldStatisticsStayUnavailableUntilFreshWindow)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(100, 60000, 60.0);
            constexpr int64_t staleAgeQpc = 5 * 60000;
            health.Observe(sample, sample.syncQpc + staleAgeQpc, 1);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));

            AddIntervalsAtOffset(health, sample, 1, 1000, 12,
                staleAgeQpc);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(static_cast<uint32_t>(0),
                health.Get().uniqueIntervals);

            // A distinct, current sample can restart the classifier, but the
            // full minimum evidence window must be collected again.
            sample.syncRefreshCount += 301;
            sample.syncQpc += staleAgeQpc + 1000;
            health.Observe(sample, sample.syncQpc, 1);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Warming),
                static_cast<int>(health.Get().state));
            AddIntervals(health, sample, 1, 1000, 12);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));
        }

        TEST_METHOD(FiveSecondFutureStatisticsCannotClassifyOrReplayAsFresh)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(100, 600000, 23.976);
            constexpr int64_t futureOffsetQpc = -5 * 60000;
            health.Observe(sample, sample.syncQpc + futureOffsetQpc, 1);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));

            AddIntervalsAtOffset(health, sample, 1, 1000, 12,
                futureOffsetQpc);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));

            // Once the same future timestamp falls into the past, it still
            // cannot seed a new baseline; the next distinct sample can.
            health.Observe(sample, sample.syncQpc, 1);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));
            ++sample.syncRefreshCount;
            sample.syncQpc += 1000;
            health.Observe(sample, sample.syncQpc, 1);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Warming),
                static_cast<int>(health.Get().state));
            AddIntervals(health, sample, 1, 1000, 12);
            Assert::AreEqual(static_cast<int>(
                    DisplayCadenceHealth::State::Mismatch),
                static_cast<int>(health.Get().state));
        }

        TEST_METHOD(HealthyCadenceAtOneHundredTwentyTwoFortyAndThreeSixtyHz)
        {
            struct RateCase
            {
                double hz;
                int64_t intervalQpc;
                unsigned intervals;
            };
            const RateCase cases[] = {
                { 120.0, 3000, 24 },
                { 240.0, 1500, 48 },
                { 360.0, 1000, 72 }
            };
            for (const RateCase& rate : cases)
            {
                DisplayCadenceHealth health;
                auto sample = Sample(10, 360000, rate.hz);
                sample.qpcFrequency = 360000;
                health.Observe(sample, sample.syncQpc, 1);
                AddIntervals(health, sample, 1, rate.intervalQpc,
                    rate.intervals);

                const auto snapshot = health.Get();
                Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                    static_cast<int>(snapshot.state));
                Assert::AreEqual(rate.hz, snapshot.observedHz, 0.001);
                Assert::IsTrue(snapshot.windowMs >= 200.0);
            }
        }

        TEST_METHOD(DuplicateStatisticsDoNotAddEvidenceAndEventuallyBecomeUnavailable)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(0, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            AddIntervals(health, sample, 1, 1000, 12);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));
            const uint32_t intervals = health.Get().uniqueIntervals;

            health.Observe(sample, sample.syncQpc + 15000, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(intervals, health.Get().uniqueIntervals);

            health.Observe(sample, sample.syncQpc + 15060, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(static_cast<uint32_t>(0), health.Get().uniqueIntervals);
            Assert::AreEqual(0.0, health.Get().observedHz);
        }

        TEST_METHOD(StaleDuplicateCannotBecomeANewWarmingBaseline)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(0, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            AddIntervals(health, sample, 1, 1000, 12);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));

            const int64_t staleNowQpc = sample.syncQpc + 15060;
            health.Observe(sample, staleNowQpc, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));
            health.Observe(sample, staleNowQpc + 30000, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(static_cast<uint32_t>(0), health.Get().uniqueIntervals);

            sample.syncRefreshCount += 46;
            sample.syncQpc = staleNowQpc + 31000;
            health.Observe(sample, sample.syncQpc, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Warming),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(static_cast<uint32_t>(0), health.Get().uniqueIntervals);
        }

        TEST_METHOD(AmbiguousCadenceAfterHealthyWindowDowngradesToWarming)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(0, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            AddIntervals(health, sample, 1, 1000, 12);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));

            AddIntervals(health, sample, 1, 1000, 6);
            AddIntervals(health, sample, 2, 1000, 6); // Half healthy, half fast.
            Assert::IsTrue(health.Get().windowMs >= 200.0);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Warming),
                static_cast<int>(health.Get().state));
        }
        TEST_METHOD(SkippedCallbacksAndMultipleRefreshesUseElapsedQpc)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(10, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            // The observer missed callbacks. Six vblanks over 100 ms are still
            // a 60 Hz cadence, not a 10 Hz event stream.
            AddIntervals(health, sample, 6, 6000, 5);

            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(60.0, health.Get().observedHz, 0.001);
            Assert::IsTrue(health.Get().windowMs >= 200.0);
        }

        TEST_METHOD(OneOutlierDoesNotOverrideEightyPercentConsistentIntervals)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(0, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            AddIntervals(health, sample, 1, 1000, 5);
            AddIntervals(health, sample, 2, 1000, 1); // one 120 Hz outlier
            AddIntervals(health, sample, 1, 1000, 7);

            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(60.0, health.Get().observedHz, 0.001);
            Assert::IsTrue(health.Get().windowMs >= 200.0);
        }

        TEST_METHOD(RefreshCounterWrapIsAcceptedAsForwardProgress)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(UINT32_MAX - 5, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            AddIntervals(health, sample, 1, 1000, 12);

            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(60.0, health.Get().observedHz, 0.001);
        }

        TEST_METHOD(OutputEpochTargetRateAndQpcFrequencyChangesRewarm)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(0, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            AddIntervals(health, sample, 1, 1000, 12);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));

            ++sample.syncRefreshCount;
            ++sample.syncQpc;
            health.Observe(sample, sample.syncQpc, 2);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Warming),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(static_cast<uint32_t>(0), health.Get().uniqueIntervals);

            sample.expectedDisplayHz = 23.976;
            ++sample.syncRefreshCount;
            ++sample.syncQpc;
            health.Observe(sample, sample.syncQpc, 2);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Warming),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(23.976, health.Get().expectedHz, 0.001);
            Assert::AreEqual(static_cast<uint32_t>(0), health.Get().uniqueIntervals);

            sample.qpcFrequency = 120000;
            sample.syncQpc *= 2;
            health.Observe(sample, sample.syncQpc, 2);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Warming),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(static_cast<uint32_t>(0), health.Get().uniqueIntervals);
        }

        TEST_METHOD(UnavailableDisjointZeroTimestampAndBackwardQpcResetEvidence)
        {
            DisplayCadenceHealth health;
            auto sample = Sample(0, 60000, 60.0);
            health.Observe(sample, sample.syncQpc, 1);
            AddIntervals(health, sample, 1, 1000, 12);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Healthy),
                static_cast<int>(health.Get().state));

            auto unavailable = sample;
            unavailable.available = false;
            unavailable.timingStatus =
                AlphaPresentationTimingStatus::FrameStatisticsUnavailable;
            health.Observe(unavailable, unavailable.syncQpc, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));

            health.Observe(sample, sample.syncQpc, 1);
            auto disjoint = sample;
            disjoint.disjoint = true;
            disjoint.timingStatus = AlphaPresentationTimingStatus::Disjoint;
            health.Observe(disjoint, disjoint.syncQpc, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));

            auto zeroTimestamp = Sample(50, 0, 60.0);
            health.Observe(zeroTimestamp, 60001, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Unavailable),
                static_cast<int>(health.Get().state));

            health.Observe(sample, sample.syncQpc, 1);
            auto backward = Sample(sample.syncRefreshCount + 3,
                sample.syncQpc - 1000, 60.0);
            health.Observe(backward, backward.syncQpc, 1);
            Assert::AreEqual(static_cast<int>(DisplayCadenceHealth::State::Warming),
                static_cast<int>(health.Get().state));
            Assert::AreEqual(static_cast<uint32_t>(0), health.Get().uniqueIntervals);
        }
    };
}
