#include "pch.h"
#include "CppUnitTest.h"

#include <cmath>
#include <vprenderer/DisplayCadenceHealth.h>
#include <vprenderer/DisplayCadenceRecovery.h>
#include <vprenderer/DisplayVsyncClock.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(DisplayCadenceRecoveryTests)
    {
    public:
        static DisplayCadenceRecovery MakeRecovery()
        {
            DisplayCadenceRecovery recovery;
            recovery.SetTarget(1, 23.976);
            return recovery;
        }

        static int64_t PeriodNs(double hz = 23.976)
        {
            return static_cast<int64_t>(std::ceil(1'000'000'000.0 / hz));
        }

        // Explicitly inject three distinct, sufficiently long slot waits over
        // at least one target period. MakeRecovery intentionally has no proof.
        static int64_t AddWorkaheadProof(DisplayCadenceRecovery& recovery,
            int64_t firstNs, uint64_t firstSerial = 1, double hz = 23.976)
        {
            const int64_t periodNs = PeriodNs(hz);
            const int64_t waitNs = periodNs / 2;
            Assert::IsTrue(recovery.RecordWorkahead(
                firstNs, firstSerial, waitNs));
            Assert::IsTrue(recovery.RecordWorkahead(
                firstNs + periodNs / 2, firstSerial + 1, waitNs));
            Assert::IsTrue(recovery.RecordWorkahead(
                firstNs + periodNs, firstSerial + 2, waitNs));
            return firstNs + periodNs;
        }

        static AlphaDxgiPresentationSample MakeSample(double expectedHz,
            uint32_t syncRefreshCount, int64_t syncQpc)
        {
            AlphaDxgiPresentationSample sample;
            sample.generation = 7;
            sample.qpcFrequency = 1'000'000;
            sample.expectedDisplayHz = expectedHz;
            sample.available = true;
            sample.timingStatus = AlphaPresentationTimingStatus::Available;
            sample.syncRefreshCount = syncRefreshCount;
            sample.syncQpc = syncQpc;
            return sample;
        }

        static void FeedCadence(DisplayCadenceHealth& health,
            DisplayCadenceRecovery& recovery, double expectedHz,
            uint32_t firstSyncRefreshCount, int64_t firstSyncQpc,
            int64_t intervalQpc, uint32_t intervalCount, uint64_t outputEpoch,
            bool recordWorkahead = false, uint64_t firstSerial = 1)
        {
            for (uint32_t index = 0; index <= intervalCount; ++index)
            {
                const int64_t syncQpc = firstSyncQpc +
                    static_cast<int64_t>(index) * intervalQpc;
                const auto sample = MakeSample(expectedHz,
                    firstSyncRefreshCount + index, syncQpc);
                health.Observe(sample, syncQpc, outputEpoch);

                const int64_t nowNs = syncQpc * 1'000;
                // For the 60-vs-24 example, the shallow test queue starts
                // predicting a material slot wait after the initial burst.
                // Feed only those later required-wait signals during Warming.
                if (recordWorkahead && index >= 6 && index % 3 == 0)
                {
                    Assert::IsTrue(recovery.RecordWorkahead(nowNs,
                        firstSerial++, PeriodNs(expectedHz) / 2));
                }

                const auto state = health.Get().state;
                recovery.Observe(
                    state == DisplayCadenceHealth::State::Mismatch,
                    state == DisplayCadenceHealth::State::Healthy, nowNs);
            }
        }

        static bool SimulateUntilMismatch(double acceptedHz, unsigned depth)
        {
            DisplayCadenceHealth health;
            auto recovery = MakeRecovery();
            DisplayVsyncClock clock;
            constexpr int64_t baseNs = 1'000'000'000LL;
            int64_t nowNs = baseNs;
            uint64_t submissionSerial = 0;
            clock.BeginSource(0.0, nowNs, 7, 23.976, false);
            for (unsigned index = 0; index < 100; ++index)
            {
                const int64_t waitNs = clock.SubmissionWaitNs(nowNs, depth);
                if (waitNs > 0)
                {
                    recovery.RecordWorkahead(nowNs, submissionSerial, waitNs);
                    nowNs += waitNs;
                }
                nowNs += static_cast<int64_t>(std::llround(1e9 / acceptedHz));
                clock.Submitted();
                ++submissionSerial;
                // Independently synthesize reported 60 Hz sync statistics.
                // They are fresh even when real submissions follow 23.976 Hz.
                const uint32_t refresh = static_cast<uint32_t>(
                    std::floor((nowNs - baseNs) * 60.0 / 1e9));
                auto sample = MakeSample(23.976, 100 + refresh,
                    baseNs + static_cast<int64_t>(std::floor(refresh * 1e9 / 60.0)));
                sample.qpcFrequency = 1'000'000'000LL;
                health.Observe(sample, nowNs, 1);
                const auto state = health.Get().state;
                recovery.Observe(state == DisplayCadenceHealth::State::Mismatch,
                    state == DisplayCadenceHealth::State::Healthy, nowNs);
                if (recovery.Fallback())
                {
                    Assert::IsTrue(nowNs - baseNs < 500'000'000LL);
                    clock.Reset(); // Renderer preserves this qualified candidate.
                    return recovery.TakeRequest(nowNs);
                }
            }
            Assert::Fail(L"Fresh 60 Hz statistics never reached mismatch");
            return false;
        }

        TEST_METHOD(FastPresentsNeedRealClockClampEvidenceBeforeRebuild)
        {
            Assert::IsTrue(SimulateUntilMismatch(60.0, 2));
            // At depth8 fallback arrives before enough clamps; no speculative
            // near-cap inference authorizes a rebuild.
            Assert::IsFalse(SimulateUntilMismatch(60.0, 8));
        }

        TEST_METHOD(MisleadingStatsWithMatchedSubmissionsCannotRebuild)
        {
            Assert::IsFalse(SimulateUntilMismatch(23.976, 2));
        }

        TEST_METHOD(FreshProofCannotBypassRetryCooldown)
        {
            auto recovery = MakeRecovery();
            AddWorkaheadProof(recovery, 900'000'000LL);
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));
            AddWorkaheadProof(recovery, 2'900'000'000LL, 10);
            recovery.Observe(true, false, 2'950'000'000LL);
            Assert::IsFalse(recovery.TakeRequest(2'999'999'999LL));
            Assert::IsTrue(recovery.Pending());
            Assert::IsTrue(recovery.TakeRequest(3'000'000'000LL));
            Assert::AreEqual(uint32_t(2), recovery.Attempts());
        }

        TEST_METHOD(StatsOnlyMismatchFallsBackButCannotRequestRebuild)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);

            Assert::IsTrue(recovery.Fallback());
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(1'000'000'000LL));
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
        }

        TEST_METHOD(FreshClampProofAllowsOneRequestAndIsConsumed)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            AddWorkaheadProof(recovery, 900'000'000LL);
            recovery.Observe(true, false, 1'000'000'000LL);

            Assert::IsTrue(recovery.Fallback());
            Assert::IsTrue(recovery.Pending());
            Assert::IsTrue(recovery.HasFreshWorkahead(1'000'000'000LL));
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.HasFreshWorkahead(1'000'000'000LL));

            recovery.Observe(true, false, 1'050'000'000LL);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(1'050'000'000LL));
        }

        TEST_METHOD(DuplicateWaitWakeupsDoNotCountAsDistinctSubmissions)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            const int64_t firstNs = 1'000'000'000LL;
            const int64_t periodNs = PeriodNs();
            const int64_t waitNs = periodNs / 2;

            Assert::IsTrue(recovery.RecordWorkahead(firstNs, 10, waitNs));
            Assert::IsFalse(recovery.RecordWorkahead(
                firstNs + periodNs / 2, 10, waitNs));
            Assert::IsTrue(recovery.RecordWorkahead(
                firstNs + periodNs / 2, 11, waitNs));
            Assert::IsFalse(recovery.RecordWorkahead(
                firstNs + periodNs, 11, waitNs));
            Assert::IsFalse(recovery.HasFreshWorkahead(firstNs + periodNs));

            Assert::IsTrue(recovery.RecordWorkahead(
                firstNs + periodNs, 12, waitNs));
            Assert::IsTrue(recovery.HasFreshWorkahead(firstNs + periodNs));
            recovery.Observe(true, false, firstNs + periodNs);
            Assert::IsTrue(recovery.Pending());
        }

        TEST_METHOD(ShortWaitsAndUnqualifiedSerialsCannotProveWorkahead)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            const int64_t firstNs = 1'000'000'000LL;
            const int64_t periodNs = PeriodNs();
            const int64_t shortWaitNs = periodNs / 4;
            const int64_t longWaitNs = periodNs / 2;

            Assert::IsFalse(recovery.RecordWorkahead(firstNs, 0, longWaitNs));
            Assert::IsFalse(recovery.RecordWorkahead(firstNs, 1, shortWaitNs));
            Assert::IsTrue(recovery.RecordWorkahead(firstNs, 2, longWaitNs));
            Assert::IsTrue(recovery.RecordWorkahead(
                firstNs + periodNs / 2, 3, longWaitNs));
            Assert::IsTrue(recovery.RecordWorkahead(
                firstNs + periodNs, 4, longWaitNs));
            Assert::IsTrue(recovery.HasFreshWorkahead(firstNs + periodNs));
            recovery.Observe(false, false, firstNs + periodNs);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(firstNs + periodNs));
        }

        TEST_METHOD(ExpiredProofCancelsPendingWithoutChargingAttempt)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            const int64_t latestProofNs = AddWorkaheadProof(
                recovery, 1'000'000'000LL);
            const int64_t mismatchNs = latestProofNs + 10'000'000LL;
            recovery.Observe(true, false, mismatchNs);
            Assert::IsTrue(recovery.Pending());

            Assert::IsFalse(recovery.TakeRequest(
                latestProofNs + 500'000'001LL));
            Assert::IsFalse(recovery.Pending());
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
        }

        TEST_METHOD(OutputChangeClearsProofAndQueuedRequest)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            const int64_t firstProofNs = 900'000'000LL;
            const int64_t latestProofNs = AddWorkaheadProof(recovery,
                firstProofNs);
            Assert::IsTrue(recovery.HasFreshWorkahead(latestProofNs));

            recovery.OutputChanged();
            Assert::IsFalse(recovery.HasFreshWorkahead(latestProofNs));
            const int64_t firstMismatchNs = latestProofNs + 1;
            recovery.Observe(true, false, firstMismatchNs);
            Assert::IsTrue(recovery.Fallback());
            Assert::IsFalse(recovery.Pending());

            const int64_t secondProofNs = 1'500'000'000LL;
            const int64_t secondLatestNs = AddWorkaheadProof(recovery,
                secondProofNs, 10);
            recovery.Observe(true, false, secondLatestNs + 1);
            Assert::IsTrue(recovery.Pending());
            recovery.OutputChanged();
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(secondLatestNs + 1));
            Assert::AreEqual(uint32_t(0), recovery.Attempts());

            const int64_t thirdProofNs = 2'000'000'000LL;
            const int64_t thirdLatestNs = AddWorkaheadProof(recovery,
                thirdProofNs, 20);
            recovery.Observe(true, false, thirdLatestNs + 1);
            Assert::IsTrue(recovery.TakeRequest(thirdLatestNs + 1));
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
        }

        TEST_METHOD(RetryRequiresNewProofAndStillHonorsTwoSecondCooldown)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            AddWorkaheadProof(recovery, 900'000'000LL);
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            AddWorkaheadProof(recovery, 1'900'000'000LL, 10);
            recovery.Observe(true, false, 2'000'000'000LL);
            Assert::IsTrue(recovery.Pending());
            // This old proof has expired by the time the cooldown is nearly up.
            Assert::IsFalse(recovery.TakeRequest(2'999'999'999LL));
            Assert::IsFalse(recovery.Pending());
            Assert::AreEqual(uint32_t(1), recovery.Attempts());

            AddWorkaheadProof(recovery, 3'000'000'000LL, 20);
            const int64_t retryNs = 3'050'000'000LL;
            recovery.Observe(true, false, retryNs);
            Assert::IsTrue(recovery.Pending());
            Assert::IsTrue(recovery.TakeRequest(retryNs));
            Assert::AreEqual(uint32_t(2), recovery.Attempts());

            recovery.Observe(true, false, retryNs + 1);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(retryNs + 1));
        }

        TEST_METHOD(HealthyAndUnavailableObservationsCancelPendingProof)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            AddWorkaheadProof(recovery, 900'000'000LL);
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            AddWorkaheadProof(recovery, 1'900'000'000LL, 10);
            recovery.Observe(true, false, 2'000'000'000LL);
            Assert::IsTrue(recovery.Pending());
            recovery.Observe(false, true, 2'100'000'000LL);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.HasFreshWorkahead(2'100'000'000LL));

            AddWorkaheadProof(recovery, 2'200'000'000LL, 20);
            recovery.Observe(true, false, 2'300'000'000LL);
            Assert::IsTrue(recovery.Pending());
            // Warming/unavailable feedback cancels queued work, but retains
            // independently scoped evidence while the health window is built.
            recovery.Observe(false, false, 2'310'000'000LL);
            Assert::IsFalse(recovery.Pending());
            Assert::IsTrue(recovery.HasFreshWorkahead(2'310'000'000LL));
            recovery.SetTarget(1, 0.0);
            Assert::IsFalse(recovery.HasFreshWorkahead(2'310'000'000LL));
        }

        TEST_METHOD(OutputAndTargetChangesKeepBudgetPolicyButDropOldProof)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            AddWorkaheadProof(recovery, 900'000'000LL);
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            // This small rate adjustment is inside the target-change tolerance.
            recovery.SetTarget(1, 24.02);
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            recovery.SetTarget(1, 24.04);
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
            Assert::IsFalse(recovery.HasFreshWorkahead(1'000'000'000LL));

            const int64_t latestProofNs = AddWorkaheadProof(recovery,
                1'500'000'000LL, 10, 24.04);
            recovery.Observe(true, false, latestProofNs + 1);
            Assert::IsTrue(recovery.TakeRequest(latestProofNs + 1));
            recovery.SetTarget(2, 24.04);
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
            Assert::IsFalse(recovery.HasFreshWorkahead(latestProofNs + 1));
        }

        TEST_METHOD(HealthyValidationRestoresRepeatsAndRefillsBudgetAfterFiveSeconds)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            AddWorkaheadProof(recovery, 900'000'000LL);
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            recovery.Observe(false, true, 1'100'000'000LL);
            Assert::IsTrue(recovery.Fallback());
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            recovery.Observe(false, true, 1'599'999'999LL);
            Assert::IsTrue(recovery.Fallback());
            recovery.Observe(false, true, 1'600'000'000LL);
            Assert::IsFalse(recovery.Fallback());
            Assert::AreEqual(uint32_t(1), recovery.Attempts());

            recovery.Observe(false, true, 6'099'999'999LL);
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            recovery.Observe(false, true, 6'100'000'000LL);
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
        }

        TEST_METHOD(InvalidAndBackwardTimesCannotProveOrRequestRecovery)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            Assert::IsFalse(recovery.RecordWorkahead(-1, 1, 30'000'000));

            AddWorkaheadProof(recovery, 900'000'000LL);
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.Pending());
            Assert::IsFalse(recovery.RecordWorkahead(
                999'999'999LL, 10, 30'000'000));
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.HasFreshWorkahead(1'000'000'000LL));

            AddWorkaheadProof(recovery, 1'100'000'000LL, 20);
            recovery.Observe(true, false, 1'200'000'000LL);
            Assert::IsTrue(recovery.Pending());
            recovery.Observe(true, false, 1'199'999'999LL);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.HasFreshWorkahead(1'200'000'000LL));

            AddWorkaheadProof(recovery, 1'300'000'000LL, 30);
            recovery.Observe(true, false, 1'400'000'000LL);
            Assert::IsTrue(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(1'399'999'999LL));
            Assert::IsFalse(recovery.Pending());
            Assert::AreEqual(uint32_t(0), recovery.Attempts());

            AddWorkaheadProof(recovery, 3'000'000'000LL, 40);
            const int64_t retryNs = 3'050'000'000LL;
            recovery.Observe(true, false, retryNs);
            Assert::IsTrue(recovery.TakeRequest(retryNs));
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            recovery.Observe(false, true, retryNs - 1);
            Assert::IsTrue(recovery.Fallback());
            recovery.Observe(false, true, retryNs);
            Assert::IsTrue(recovery.Fallback());
            recovery.Observe(false, true, retryNs + 500'000'000LL);
            Assert::IsFalse(recovery.Fallback());
        }

        TEST_METHOD(HealthMismatchNeedsClampProofAndHealthyScanoutRestoresRepeats)
        {
            DisplayCadenceHealth health;
            DisplayCadenceRecovery recovery = MakeRecovery();
            const int64_t wrongStartQpc = 1'000'000;
            FeedCadence(health, recovery, 23.976, 100, wrongStartQpc,
                16'667, 12, 1, true, 1000);

            Assert::IsTrue(health.Get().state == DisplayCadenceHealth::State::Mismatch);
            Assert::IsTrue(health.Get().windowMs >= 200.0);
            const int64_t mismatchNowNs =
                (wrongStartQpc + 12 * 16'667) * 1'000;
            Assert::IsTrue(recovery.Pending());
            Assert::IsTrue(recovery.TakeRequest(mismatchNowNs));
            Assert::AreEqual(uint32_t(1), recovery.Attempts());

            // Rebuild starts a fresh health epoch while policy budget survives.
            health.Reset();
            recovery.OutputChanged();
            const int64_t healthyStartQpc = wrongStartQpc + 12 * 16'667 + 1;
            for (uint32_t index = 0; index <= 17; ++index)
            {
                const int64_t syncQpc = healthyStartQpc +
                    static_cast<int64_t>(index) * 41'708;
                const auto sample = MakeSample(23.976, 200 + index, syncQpc);
                health.Observe(sample, syncQpc, 2);
                const auto state = health.Get().state;
                recovery.Observe(
                    state == DisplayCadenceHealth::State::Mismatch,
                    state == DisplayCadenceHealth::State::Healthy,
                    syncQpc * 1'000);
                if (index == 16)
                    Assert::IsTrue(recovery.Fallback());
            }

            Assert::IsTrue(health.Get().state == DisplayCadenceHealth::State::Healthy);
            Assert::IsFalse(recovery.Fallback());
            Assert::IsFalse(recovery.Pending());
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
        }

        TEST_METHOD(PersistentWrongCadenceCapsAtTwoProofBackedAttempts)
        {
            DisplayCadenceHealth health;
            DisplayCadenceRecovery recovery = MakeRecovery();
            int64_t startQpc = 1'000'000;
            for (uint64_t epoch = 1; epoch <= 3; ++epoch)
            {
                health.Reset();
                FeedCadence(health, recovery, 23.976,
                    static_cast<uint32_t>(epoch * 100), startQpc,
                    16'667, 12, epoch, true, epoch * 1000);
                Assert::IsTrue(health.Get().state == DisplayCadenceHealth::State::Mismatch);

                const int64_t mismatchNowNs =
                    (startQpc + 12 * 16'667) * 1'000;
                if (epoch <= 2)
                {
                    Assert::IsTrue(recovery.Pending());
                    Assert::IsTrue(recovery.TakeRequest(mismatchNowNs));
                    Assert::AreEqual(static_cast<uint32_t>(epoch), recovery.Attempts());
                }
                else
                {
                    Assert::IsFalse(recovery.Pending());
                    Assert::IsFalse(recovery.TakeRequest(mismatchNowNs));
                    Assert::AreEqual(uint32_t(2), recovery.Attempts());
                }

                // Each rebuilt output is a new evidence scope; attempts and
                // cooldown remain bounded across the transition.
                recovery.OutputChanged();
                startQpc += 2'300'000;
            }
        }

        TEST_METHOD(FilmOn60HzScanoutIsHealthyAndDoesNotRequestRecovery)
        {
            DisplayCadenceHealth health;
            DisplayCadenceRecovery recovery;
            recovery.SetTarget(5, 60.0);

            // This classifier consumes scanout feedback only; source cadence
            // does not make a healthy 60 Hz sync sequence a mismatch.
            FeedCadence(health, recovery, 60.0, 500, 2'000'000,
                16'667, 12, 1);

            Assert::IsTrue(health.Get().state == DisplayCadenceHealth::State::Healthy);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.Fallback());
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
        }
    };
}
