#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/DisplayCadenceHealth.h>
#include <vprenderer/DisplayCadenceRecovery.h>

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
            int64_t intervalQpc, uint32_t intervalCount, uint64_t outputEpoch)
        {
            for (uint32_t index = 0; index <= intervalCount; ++index)
            {
                const int64_t syncQpc = firstSyncQpc +
                    static_cast<int64_t>(index) * intervalQpc;
                const auto sample = MakeSample(expectedHz,
                    firstSyncRefreshCount + index, syncQpc);
                health.Observe(sample, syncQpc, outputEpoch);
                const auto state = health.Get().state;
                const int64_t nowNs = syncQpc * 1'000;
                recovery.Observe(
                    state == DisplayCadenceHealth::State::Mismatch,
                    state == DisplayCadenceHealth::State::Healthy, nowNs);
            }
        }

        TEST_METHOD(FirstMismatchImmediatelyFallsBackAndRequestIsOneShot)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);

            Assert::IsTrue(recovery.Fallback());
            Assert::IsTrue(recovery.Pending());
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(1'000'000'000LL));
        }

        TEST_METHOD(PersistentMismatchGetsAtMostTwoAttemptsWithCooldown)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            recovery.Observe(true, false, 2'000'000'000LL);
            Assert::IsTrue(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(2'999'999'999LL));
            Assert::IsTrue(recovery.Pending());
            Assert::IsTrue(recovery.TakeRequest(3'000'000'000LL));
            Assert::AreEqual(uint32_t(2), recovery.Attempts());

            recovery.Observe(true, false, 3'000'000'001LL);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(5'000'000'000LL));
            Assert::AreEqual(uint32_t(2), recovery.Attempts());
        }

        TEST_METHOD(OutputChangeCancelsStaleRequestAndKeepsEpisodeBudget)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            recovery.Observe(true, false, 2'000'000'000LL);
            Assert::IsTrue(recovery.Pending());
            recovery.OutputChanged();
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(2'000'000'000LL));
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            Assert::IsTrue(recovery.Fallback());

            // A fresh mismatch may request the remaining attempt, but the
            // original cooldown still applies across the output change.
            recovery.Observe(true, false, 2'700'000'000LL);
            Assert::IsTrue(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(2'999'999'999LL));
            Assert::IsTrue(recovery.TakeRequest(3'000'000'000LL));
            Assert::AreEqual(uint32_t(2), recovery.Attempts());
        }
        TEST_METHOD(OutputChangeRestartsHealthyValidationWithoutRefillingBudget)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            recovery.Observe(false, true, 1'100'000'000LL);
            recovery.Observe(false, true, 1'550'000'000LL);
            recovery.OutputChanged();
            Assert::IsTrue(recovery.Fallback());
            Assert::AreEqual(uint32_t(1), recovery.Attempts());

            // Start a fresh healthy interval after the output boundary.
            recovery.Observe(false, true, 1'550'000'000LL);
            recovery.Observe(false, true, 2'049'999'999LL);
            Assert::IsTrue(recovery.Fallback());
            recovery.Observe(false, true, 2'050'000'000LL);
            Assert::IsFalse(recovery.Fallback());
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
        }
        TEST_METHOD(HealthyValidationClearsFallbackBeforeRefillingBudget)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
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

        TEST_METHOD(UnavailableTargetBreaksHealthWithoutRefillingBudget)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            recovery.Observe(false, true, 1'100'000'000LL);
            recovery.SetTarget(1, 0.0);
            recovery.Observe(false, false, 6'100'000'000LL);
            Assert::AreEqual(uint32_t(1), recovery.Attempts());

            recovery.SetTarget(1, 23.976);
            recovery.Observe(false, true, 6'200'000'000LL);
            recovery.Observe(false, true, 11'099'999'999LL);
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            recovery.Observe(false, true, 11'200'000'000LL);
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
        }

        TEST_METHOD(HealthyObservationCancelsPendingRecovery)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            recovery.Observe(true, false, 2'000'000'000LL);
            Assert::IsTrue(recovery.Pending());
            recovery.Observe(false, true, 2'100'000'000LL);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(3'000'000'000LL));

            recovery.Observe(true, false, 3'100'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(3'100'000'000LL));
            Assert::AreEqual(uint32_t(2), recovery.Attempts());
        }

        TEST_METHOD(TargetChangesResetBudgetOnlyWhenMeaningful)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            recovery.SetTarget(1, 24.02);
            Assert::AreEqual(uint32_t(1), recovery.Attempts());
            recovery.SetTarget(1, 24.04);
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
            recovery.Observe(true, false, 2'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(2'000'000'000LL));

            recovery.SetTarget(2, 24.04);
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
        }

        TEST_METHOD(InvalidAndBackwardTimesCannotBypassCooldownOrValidateHealth)
        {
            DisplayCadenceRecovery recovery = MakeRecovery();
            recovery.Observe(true, false, 1'000'000'000LL);
            Assert::IsTrue(recovery.TakeRequest(1'000'000'000LL));

            recovery.Observe(true, false, 999'999'999LL);
            Assert::IsTrue(recovery.Fallback());
            Assert::IsTrue(recovery.Pending());
            Assert::IsFalse(recovery.TakeRequest(999'999'999LL));
            Assert::IsFalse(recovery.TakeRequest(-1));
            Assert::IsTrue(recovery.Pending());

            Assert::IsFalse(recovery.TakeRequest(2'999'999'999LL));
            Assert::IsTrue(recovery.TakeRequest(3'000'000'000LL));
            Assert::AreEqual(uint32_t(2), recovery.Attempts());

            recovery.Observe(false, true, 2'999'999'999LL);
            Assert::IsTrue(recovery.Fallback());
            recovery.Observe(false, true, 3'000'000'000LL);
            Assert::IsTrue(recovery.Fallback());
            recovery.Observe(false, true, 3'500'000'000LL);
            Assert::IsFalse(recovery.Fallback());
        }

        TEST_METHOD(HealthMismatchAtAbout200msRequestsOneRebuildThenHealthyValidationRestoresRepeats)
        {
            DisplayCadenceHealth health;
            DisplayCadenceRecovery recovery = MakeRecovery();
            const int64_t wrongStartQpc = 1'000'000;
            FeedCadence(health, recovery, 23.976, 100, wrongStartQpc,
                16'667, 12, 1);

            Assert::IsTrue(health.Get().state == DisplayCadenceHealth::State::Mismatch);
            Assert::IsTrue(health.Get().windowMs >= 200.0);
            const int64_t mismatchNowNs =
                (wrongStartQpc + 12 * 16'667) * 1'000;
            Assert::IsTrue(recovery.Pending());
            Assert::IsTrue(recovery.TakeRequest(mismatchNowNs));
            Assert::AreEqual(uint32_t(1), recovery.Attempts());

            // Rebuild starts a fresh health epoch while policy budget survives.
            health.Reset();
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

        TEST_METHOD(PersistentWrongCadenceAcrossRebuildsStillCapsAtTwoAttempts)
        {
            DisplayCadenceHealth health;
            DisplayCadenceRecovery recovery = MakeRecovery();
            int64_t startQpc = 1'000'000;
            for (uint64_t epoch = 1; epoch <= 3; ++epoch)
            {
                health.Reset();
                FeedCadence(health, recovery, 23.976,
                    static_cast<uint32_t>(epoch * 100), startQpc,
                    16'667, 12, epoch);
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
                startQpc += 2'300'000;
            }
        }

        TEST_METHOD(FilmOn60HzScanoutIsHealthyAndDoesNotRequestRecovery)
        {
            DisplayCadenceHealth health;
            DisplayCadenceRecovery recovery;
            recovery.SetTarget(5, 60.0);

            // The detector consumes scanout feedback only; 24fps source cadence
            // does not turn a healthy 60Hz DXGI sync sequence into a mismatch.
            FeedCadence(health, recovery, 60.0, 500, 2'000'000,
                16'667, 12, 1);

            Assert::IsTrue(health.Get().state == DisplayCadenceHealth::State::Healthy);
            Assert::IsFalse(recovery.Pending());
            Assert::IsFalse(recovery.Fallback());
            Assert::AreEqual(uint32_t(0), recovery.Attempts());
        }
    };
}