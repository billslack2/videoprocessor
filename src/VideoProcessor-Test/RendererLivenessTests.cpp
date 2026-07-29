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
			snapshot.deliverySuccessCount = 1;
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
			snapshot.deliverySuccessCount = 1;
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
	};
}
