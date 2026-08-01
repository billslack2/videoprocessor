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
	};
}
