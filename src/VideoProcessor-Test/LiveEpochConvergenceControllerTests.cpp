#include "pch.h"
#include "CppUnitTest.h"

#include <LiveEpochConvergenceController.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
LiveEpochConvergenceInput EpochInput()
{
	LiveEpochConvergenceInput input;
	input.epoch = 41;
	input.epochActive = true;
	input.vpConvertedDepth = 12;
	input.desiredVpDepth = 2;
	input.deliveryCompleted = true;
	input.deliverySucceeded = true;
	input.nominalFrameDurationUs = 16683;
	input.deliveryDurationUs = 16683;
	return input;
}
}

namespace Tests
{
	TEST_CLASS(LiveEpochConvergenceControllerTests)
	{
	public:
		TEST_METHOD(WaitsForDownstreamPrimeEvenWhenEarlyDeliveriesAreFast)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = EpochInput();
			for (uint32_t i = 0; i < 4; ++i)
			{
				const LiveEpochConvergenceDecision decision = controller.Observe(input);
				Assert::IsFalse(decision.requestConvergence);
				Assert::AreEqual(
					static_cast<int>(LiveEpochConvergenceState::AwaitingDownstreamPrime),
					static_cast<int>(decision.state));
			}
		}

		TEST_METHOD(ConvergesOnceAfterStartupStallsThenStableDelivery)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = EpochInput();
			for (uint32_t i = 0; i < 4; ++i)
				(void)controller.Observe(input);

			input.deliveryDurationUs = 109719;
			LiveEpochConvergenceDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);

			input.deliveryDurationUs = 123475;
			decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);

			input.deliveryDurationUs = 16683;
			decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);
			decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);
			decision = controller.Observe(input);
			Assert::IsTrue(decision.requestConvergence);
			Assert::AreEqual<size_t>(10, decision.staleVpFrames);
			Assert::AreEqual(
				static_cast<int>(LiveEpochConvergenceState::Converged),
				static_cast<int>(decision.state));

			decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);
		}

		TEST_METHOD(ConvergesWithoutDiscardWhenTheDesiredDepthIsAlreadyMet)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = EpochInput();
			input.vpConvertedDepth = 2;
			for (uint32_t i = 0; i < 7; ++i)
				(void)controller.Observe(input);
			const LiveEpochConvergenceDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);
			Assert::AreEqual<size_t>(0, decision.staleVpFrames);
			Assert::AreEqual(
				static_cast<int>(LiveEpochConvergenceState::Converged),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(NewEpochRearmsAOneShotConvergence)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = EpochInput();
			for (uint32_t i = 0; i < 8; ++i)
				(void)controller.Observe(input);
			Assert::IsFalse(controller.Observe(input).requestConvergence);

			input.epoch = 42;
			for (uint32_t i = 0; i < 6; ++i)
				Assert::IsFalse(controller.Observe(input).requestConvergence);
			const LiveEpochConvergenceDecision decision = controller.Observe(input);
			Assert::IsTrue(decision.requestConvergence);
			Assert::AreEqual<uint64_t>(42, decision.epoch);
		}

		TEST_METHOD(AutomaticQueuePolicyDoesNotRequestConvergence)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = EpochInput();
			input.desiredVpDepth = 0;
			const LiveEpochConvergenceDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);
			Assert::AreEqual(
				static_cast<int>(LiveEpochConvergenceState::Disabled),
				static_cast<int>(decision.state));
		}
	};
}
