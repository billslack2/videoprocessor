#include "pch.h"
#include "CppUnitTest.h"

#include <OutputReadinessController.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
OutputReadinessInput ReadyInput()
{
	OutputReadinessInput input;
	input.transitionGeneration = 7;
	input.graphOperational = true;
	input.displayDecision = DisplayRefreshRateDecision::Accepted;
	input.displayReason = DisplayRefreshRateReason::Accepted;
	input.expectedOutputRefreshHz = 60000.0 / 1001.0;
	input.observedOutputRefreshHz = 59.950499;
	input.reserveFrames = 8;
	return input;
}
}

namespace Tests
{
	TEST_CLASS(OutputReadinessControllerTests)
	{
	public:
		TEST_METHOD(PreservesImmediateDeliveryUntilValidatedResetCompletes)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.graphOperational = false;
			OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.discardLiveCapture);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessReason::AwaitingGraph),
				static_cast<int>(decision.reason));

			input.graphOperational = true;
			input.displayDecision = DisplayRefreshRateDecision::Warming;
			decision = controller.Observe(input);
			Assert::IsFalse(decision.discardLiveCapture);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessReason::AwaitingDisplayMeasurement),
				static_cast<int>(decision.reason));

			input.displayDecision = DisplayRefreshRateDecision::Quarantined;
			decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessReason::DisplayMeasurementRejected),
				static_cast<int>(decision.reason));
		}

		TEST_METHOD(RejectsARefreshRateOutsideTheRequestedOutputFamily)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.observedOutputRefreshHz = 23.976;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.discardLiveCapture);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessReason::OutputRefreshFamilyMismatch),
				static_cast<int>(decision.reason));
		}

		TEST_METHOD(RequestsOneResetThenGatesOnlyThePostResetEpochForPrefill)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::IsFalse(decision.discardLiveCapture);
			Assert::IsTrue(decision.allowDownstreamDelivery);

			decision = controller.Observe(input);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
			Assert::IsFalse(decision.discardLiveCapture);
			Assert::IsTrue(decision.allowDownstreamDelivery);

			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = 7;
			decision = controller.Observe(input);
			Assert::IsTrue(decision.admitCurrentEpochCapture);
			Assert::IsFalse(decision.allowDownstreamDelivery);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Prefilling),
				static_cast<int>(decision.state));

			input.currentEpochProcessedDepth = 8;
			decision = controller.Observe(input);
			Assert::IsTrue(decision.prefillSatisfied);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(ExplicitTwoFramePrefillDoesNotRequireTheAutomaticReserve)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			(void)controller.Observe(input);

			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = 1;
			OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Prefilling),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.prefillSatisfied);

			input.currentEpochProcessedDepth = 2;
			decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::IsTrue(decision.prefillSatisfied);
		}

		TEST_METHOD(NewTransitionInvalidatesSteadyStateAndRequestsOneNewReset)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = 8;
			Assert::IsTrue(controller.Observe(input).allowDownstreamDelivery);

			input.transitionGeneration = 8;
			input.postReadyResetCompleted = false;
			input.postReadyEpoch = 0;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.discardLiveCapture);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::AreEqual<uint64_t>(8, decision.transitionGeneration);
		}
	};
}
