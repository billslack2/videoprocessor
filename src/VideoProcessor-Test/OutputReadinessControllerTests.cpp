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

		TEST_METHOD(ExplicitZeroFrameReserveIsImmediatelySteady)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 0;
			(void)controller.Observe(input);

			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = 0;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::IsTrue(decision.prefillSatisfied);
			Assert::IsTrue(decision.allowDownstreamDelivery);
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

		TEST_METHOD(AdoptsRecoveredSameEpochWithoutReset)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 2;
			input.currentGraphRawDepth = 1;
			input.currentGraphConvertedDepth = 2;
			input.currentGraphPostProofDeliverySuccesses = 3;
			const OutputReadinessDecision decision = controller.Observe(input);

			Assert::IsTrue(decision.adoptedCurrentGraph);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::AreEqual<uint64_t>(41, decision.postReadyEpoch);
		}

		TEST_METHOD(AdoptsProofCompletedBeforeDisplayEvidenceArrives)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 2;
			input.currentGraphRawDepth = 0;
			input.currentGraphConvertedDepth = 2;
			input.currentGraphPostProofDeliverySuccesses = 3;

			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsTrue(decision.adoptedCurrentGraph);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(RecoveryNeedsThreeSuccessfulPostProofDeliveries)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 2;
			input.currentGraphRawDepth = 0;
			input.currentGraphConvertedDepth = 2;
			input.currentGraphPostProofDeliverySuccesses = 2;

			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(IncompleteOrStaleProofCannotSuppressFallbackReset)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 1; // stale policy proof
			input.currentGraphRawDepth = 0;
			input.currentGraphConvertedDepth = 1;
			input.currentGraphPostProofDeliverySuccesses = 3;
			const OutputReadinessDecision decision = controller.Observe(input);

			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);
		}

		TEST_METHOD(DeepRawBacklogCannotBeAdopted)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 2;
			input.currentGraphRawDepth = 30;
			input.currentGraphConvertedDepth = 2;
			input.currentGraphPostProofDeliverySuccesses = 3;

			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(ConvertedDepthBelowTargetCannotBeAdopted)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 2;
			input.currentGraphRawDepth = 0;
			input.currentGraphConvertedDepth = 1;
			input.currentGraphPostProofDeliverySuccesses = 3;

			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(HardBlockWithoutFullConvertedPrimeCannotBeAdopted)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = false;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 2;
			input.currentGraphRawDepth = 0;
			input.currentGraphConvertedDepth = 2;
			input.currentGraphPostProofDeliverySuccesses = 3;

			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(ExactZeroTargetCanBeAdopted)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 0;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 0;
			input.currentGraphRawDepth = 0;
			input.currentGraphConvertedDepth = 0;
			input.currentGraphPostProofDeliverySuccesses = 3;

			Assert::IsTrue(controller.Observe(input).adoptedCurrentGraph);
		}

		TEST_METHOD(RejectedFallbackRequestCanBeRetried)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			Assert::IsTrue(
				controller.Observe(input).requestSerializedPostReadyReset);
			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);

			controller.RearmResetRequest();
			Assert::IsTrue(
				controller.Observe(input).requestSerializedPostReadyReset);
		}

		TEST_METHOD(NewTransitionCannotAdoptProofFromPriorGeneration)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.transitionGeneration = 8;
			input.reserveFrames = 2;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 2;
			input.currentGraphRawDepth = 0;
			input.currentGraphConvertedDepth = 2;
			input.currentGraphPostProofDeliverySuccesses = 3;

			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}
	};
}
