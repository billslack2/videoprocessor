#include "pch.h"
#include "CppUnitTest.h"

#include <RendererLiveness.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(RendererLivenessTests)
	{
	public:
		TEST_METHOD(EnqueuedFrameIsNotDownstreamDelivery)
		{
			RendererLivenessSnapshot snapshot;
			snapshot.supported = true;
			snapshot.active = true;
			snapshot.queueEpoch = 7;
			snapshot.inputCount = 1;

			Assert::IsFalse(
				HasCurrentEpochDownstreamDelivery(snapshot));
		}

		TEST_METHOD(CurrentEpochDeliveryQualifiesForReveal)
		{
			RendererLivenessSnapshot snapshot;
			snapshot.supported = true;
			snapshot.active = true;
			snapshot.queueEpoch = 7;
			snapshot.deliverySuccessCount = 5;
			snapshot.currentEpochDeliverySuccessCount = 5;
			snapshot.lastDeliverySuccessQueueEpoch = 7;
			snapshot.lastDeliverySuccessTick = 100;

			Assert::IsTrue(
				HasCurrentEpochDownstreamDelivery(snapshot));
		}

		TEST_METHOD(PreviousEpochDeliveryCannotRevealAfterReset)
		{
			RendererLivenessSnapshot snapshot;
			snapshot.supported = true;
			snapshot.active = true;
			snapshot.queueEpoch = 8;
			snapshot.deliverySuccessCount = 5;
			snapshot.currentEpochDeliverySuccessCount = 5;
			snapshot.lastDeliverySuccessQueueEpoch = 7;
			snapshot.lastDeliverySuccessTick = 100;

			Assert::IsFalse(
				HasCurrentEpochDownstreamDelivery(snapshot));
		}

		TEST_METHOD(BufferingOrResetBlocksReveal)
		{
			RendererLivenessSnapshot snapshot;
			snapshot.supported = true;
			snapshot.active = true;
			snapshot.queueEpoch = 8;
			snapshot.deliverySuccessCount = 5;
			snapshot.currentEpochDeliverySuccessCount = 5;
			snapshot.lastDeliverySuccessQueueEpoch = 8;
			snapshot.lastDeliverySuccessTick = 100;

			snapshot.buffering = true;
			Assert::IsFalse(
				HasCurrentEpochDownstreamDelivery(snapshot));

			snapshot.buffering = false;
			snapshot.resetInProgress = true;
			Assert::IsFalse(
				HasCurrentEpochDownstreamDelivery(snapshot));
		}

		TEST_METHOD(DownstreamPrerollRequiresFiveAcceptedSamples)
		{
			Assert::IsFalse(HasSufficientDownstreamPreroll(0));
			Assert::IsFalse(HasSufficientDownstreamPreroll(4));
			Assert::IsTrue(HasSufficientDownstreamPreroll(5));
		}

		TEST_METHOD(RecentDeliveryProvesHealthEvenWhenVpIsBackpressured)
		{
			RendererLivenessSnapshot snapshot;
			snapshot.supported = true;
			snapshot.active = true;
			snapshot.queueEpoch = 8;
			snapshot.currentEpochDeliverySuccessCount = 12;
			snapshot.lastDeliverySuccessQueueEpoch = 8;
			snapshot.lastDeliverySuccessTick = 9950;

			Assert::IsTrue(HasRecentCurrentEpochDelivery(
				snapshot, 10000, 500));
			Assert::IsFalse(HasRecentCurrentEpochDelivery(
				snapshot, 11000, 500));
		}

		TEST_METHOD(DirectShowStallRequiresSustainedBlockedDelivery)
		{
			RendererLivenessSnapshot snapshot;
			snapshot.supported = true;
			snapshot.active = true;
			snapshot.queueEpoch = 8;
			snapshot.lastInputTick = 9900;
			snapshot.deliveryInProgress = true;
			snapshot.lastDeliveryStartTick = 6000;
			snapshot.lastDeliverySuccessTick = 6000;

			Assert::IsFalse(IsSustainedDirectShowDeliveryStall(
				snapshot, 10000, true, 5000));
			snapshot.lastDeliveryStartTick = 5000;
			snapshot.lastDeliverySuccessTick = 5000;
			Assert::IsTrue(IsSustainedDirectShowDeliveryStall(
				snapshot, 10000, true, 5000));
			Assert::IsFalse(IsSustainedDirectShowDeliveryStall(
				snapshot, 10000, false, 5000));
		}

		TEST_METHOD(StreamTimeNormalizationIsRelativeToEachQueueEpoch)
		{
			RendererStreamTimeNormalizer normalizer;
			int64_t normalized = -1;
			Assert::IsTrue(normalizer.Normalize(
				10, 3923336128300LL, normalized));
			Assert::AreEqual<int64_t>(0, normalized);
			Assert::IsTrue(normalizer.Normalize(
				10, 3923336228300LL, normalized));
			Assert::AreEqual<int64_t>(100000, normalized);
			Assert::IsTrue(normalizer.Normalize(
				11, 3923337000000LL, normalized));
			Assert::AreEqual<int64_t>(0, normalized);
		}

		TEST_METHOD(StreamTimeNormalizationRebasesSameEpochGraphClockRollback)
		{
			RendererStreamTimeNormalizer normalizer;
			int64_t normalized = -1;
			Assert::IsTrue(normalizer.Normalize(10, 1000000, normalized));
			Assert::AreEqual<int64_t>(0, normalized);
			Assert::IsTrue(normalizer.Normalize(10, 1100000, normalized));
			Assert::AreEqual<int64_t>(100000, normalized);
			Assert::IsTrue(normalizer.Normalize(10, 900000, normalized));
			Assert::AreEqual<int64_t>(900000, normalized);
			Assert::IsTrue(normalizer.LastObservationRebased());
			Assert::AreEqual<int64_t>(900000,
				normalizer.LastValidObservedTime100ns());
			Assert::IsTrue(normalizer.Normalize(10, 1200000, normalized));
			Assert::AreEqual<int64_t>(1200000, normalized);
			Assert::IsFalse(normalizer.LastObservationRebased());

			// The next real queue epoch establishes a fresh comparison.
			Assert::IsTrue(normalizer.Normalize(11, 500000, normalized));
			Assert::AreEqual<int64_t>(0, normalized);
			Assert::IsFalse(normalizer.LastObservationRebased());
		}

		TEST_METHOD(ScheduledLatencyUsesVpResidenceAndRemainingDsLead)
		{
			RendererLatencySnapshot snapshot;
			const bool calculated = CalculateScheduledLatency(
				1000, 1014,
				2000000, 300000,
				snapshot);

			Assert::IsTrue(calculated);
			Assert::IsTrue(snapshot.supported);
			Assert::IsTrue(snapshot.scheduledPresentationKnown);
			Assert::AreEqual(14.0, snapshot.vpInternalMs, 0.001);
			Assert::AreEqual(170.0, snapshot.dsScheduleLeadMs, 0.001);
			Assert::AreEqual(184.0, snapshot.scheduledLatencyMs, 0.001);
		}

		TEST_METHOD(ScheduledLatencyRejectsMissingVpArrivalBoundary)
		{
			RendererLatencySnapshot snapshot;
			Assert::IsFalse(CalculateScheduledLatency(
				0, 1014, 2000000, 300000, snapshot));
			Assert::IsFalse(snapshot.supported);
		}

		TEST_METHOD(VpInternalLatencyDoesNotRequireDirectShowTimestamps)
		{
			RendererLatencySnapshot snapshot;
			const bool calculated = CalculateVpInternalLatency(
				2000, 2023, snapshot);

			Assert::IsTrue(calculated);
			Assert::IsTrue(snapshot.supported);
			Assert::IsFalse(snapshot.scheduledPresentationKnown);
			Assert::AreEqual(23.0, snapshot.vpInternalMs, 0.001);
		}

		TEST_METHOD(LatencyDisplayIgnoresStartupThenAveragesCleanEvidence)
		{
			RendererLatencyStabilizer stabilizer;
			RendererLatencySnapshot observed;
			observed.supported = true;
			observed.scheduledPresentationKnown = true;
			RendererLatencySnapshot stable;

			observed.vpInternalMs = 900.0;
			observed.dsScheduleLeadMs = 800.0;
			observed.scheduledLatencyMs = 1700.0;
			Assert::IsFalse(stabilizer.Observe(4, 100, observed, stable));
			Assert::IsFalse(stabilizer.Observe(4, 1000, observed, stable));

			for (uint64_t tick = 1100; tick <= 1700; tick += 200)
			{
				observed.vpInternalMs = 20.0;
				observed.dsScheduleLeadMs = 40.0;
				observed.scheduledLatencyMs = 60.0;
				Assert::IsFalse(stabilizer.Observe(4, tick, observed, stable));
			}
			Assert::IsTrue(stabilizer.Observe(4, 2100, observed, stable));
			Assert::AreEqual(20.0, stable.vpInternalMs, 0.001);
			Assert::AreEqual(40.0, stable.dsScheduleLeadMs, 0.001);
			Assert::AreEqual(60.0, stable.scheduledLatencyMs, 0.001);
		}

		TEST_METHOD(LatencyDisplayRewarmsForEveryEpoch)
		{
			RendererLatencyStabilizer stabilizer;
			RendererLatencySnapshot observed;
			observed.supported = true;
			RendererLatencySnapshot stable;

			Assert::IsFalse(stabilizer.Observe(1, 100, observed, stable));
			for (uint64_t tick = 1100; tick <= 2100; tick += 200)
				stabilizer.Observe(1, tick, observed, stable);
			Assert::IsFalse(stabilizer.Observe(2, 2200, observed, stable));
		}

		TEST_METHOD(LatencyDisplayDiscardsPtsEvidenceBeforeClockLoss)
		{
			RendererLatencyStabilizer stabilizer;
			RendererLatencySnapshot observed;
			observed.supported = true;
			observed.scheduledPresentationKnown = true;
			observed.vpInternalMs = 42.0;
			observed.dsScheduleLeadMs = 180.0;
			observed.scheduledLatencyMs = 222.0;
			RendererLatencySnapshot stable;

			Assert::IsFalse(stabilizer.Observe(4, 100, observed, stable));
			for (uint64_t tick = 1100; tick <= 1500; tick += 100)
				Assert::IsFalse(stabilizer.Observe(4, tick, observed, stable));

			// A clock-domain loss before publication invalidates every PTS
			// sample collected earlier in this same evidence window.
			observed.scheduledPresentationKnown = false;
			Assert::IsFalse(stabilizer.Observe(4, 1600, observed, stable));
			Assert::IsTrue(stabilizer.Observe(4, 2100, observed, stable));
			Assert::AreEqual(42.0, stable.vpInternalMs, 0.001);
			Assert::IsFalse(stable.scheduledPresentationKnown);
		}

		TEST_METHOD(LatencyDisplayCanRewarmAfterSameEpochLiveCatchUp)
		{
			RendererLatencyStabilizer stabilizer;
			RendererLatencySnapshot observed;
			observed.supported = true;
			RendererLatencySnapshot stable;

			Assert::IsFalse(stabilizer.Observe(3, 100, observed, stable));
			for (uint64_t tick = 1100; tick <= 2100; tick += 200)
				stabilizer.Observe(3, tick, observed, stable);
			stabilizer.Reset();
			Assert::IsFalse(stabilizer.Observe(3, 2200, observed, stable));
		}
	};
}
