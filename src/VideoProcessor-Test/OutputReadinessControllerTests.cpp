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
	input.correctiveRecoveryGeneration = 7;
	input.correctiveRecoveryContractRevision = 11;
	input.graphOperational = true;
	input.displayDecision = DisplayRefreshRateDecision::Accepted;
	input.displayReason = DisplayRefreshRateReason::Accepted;
	input.expectedOutputRefreshHz = 60000.0 / 1001.0;
	input.observedOutputRefreshHz = 59.950499;
	input.reserveFrames = 8;
	// Zero deliberately exercises the controller's fail-safe immediate reset.
	// Set a real monotonic tick in tests that exercise the settle interval.
	input.observationTickMs = 0;
	input.currentGraphMaximumSuccessfulDeliveryDurationUs = 100000;
	return input;
}

void SetValidPostResetEvidence(OutputReadinessInput& input, uint64_t epoch)
{
	input.currentGraphBoundarySafe = true;
	input.currentGraphPostResetBoundarySafe = true;
	input.currentGraphPrimeProven = true;
	input.currentGraphPrimeObservedFullConvertedQueue = true;
	input.currentGraphDeliveryRecent = true;
	input.currentGraphQueueEpoch = epoch;
	input.currentGraphPrimeTransitionGeneration = input.transitionGeneration;
	input.currentGraphPrimeEpoch = epoch;
	input.currentGraphPrimeTargetFrames = input.reserveFrames;
	input.currentGraphRawDepth = 0;
	input.currentGraphConvertedDepth = input.reserveFrames;
	input.currentGraphRetainedSourceBufferCount = 0;
	input.currentGraphPostProofDeliverySuccesses =
		OutputReadinessController::kRequiredPostResetValidationDeliveries;
	input.currentGraphMaximumSuccessfulDeliveryDurationUs = 100000;
	input.currentGraphUnexpectedLiveDeliveryGapEvents = 0;
	input.currentGraphUnexpectedLiveDeliveryGapSlots = 0;
}

OutputReadinessDecision FinishPostResetValidation(
	OutputReadinessController& controller,
	OutputReadinessInput& input,
	uint64_t epoch)
{
	input.postReadyResetCompleted = true;
	input.postReadyEpoch = epoch;
	input.currentEpochProcessedDepth = input.reserveFrames;
	input.observationTickMs = std::max<uint64_t>(1, input.observationTickMs);
	SetValidPostResetEvidence(input, epoch);
	OutputReadinessDecision decision = controller.Observe(input);
	Assert::AreEqual(
		static_cast<int>(OutputReadinessState::PostResetValidating),
		static_cast<int>(decision.state));
	input.observationTickMs += 1;
	decision = controller.Observe(input);
	Assert::AreEqual(
		static_cast<int>(OutputReadinessState::PostResetValidating),
		static_cast<int>(decision.state));
	input.observationTickMs +=
		OutputReadinessController::kPostResetValidationStableMs;
	return controller.Observe(input);
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
			input.observationTickMs = 1;
			decision = controller.Observe(input);
			Assert::IsTrue(decision.prefillSatisfied);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(decision.state));

			SetValidPostResetEvidence(input, 42);
			input.observationTickMs = 2;
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(controller.Observe(input).state));
			input.observationTickMs +=
				OutputReadinessController::kPostResetValidationStableMs;
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(controller.Observe(input).state));
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
			decision = FinishPostResetValidation(controller, input, 42);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::IsTrue(decision.prefillSatisfied);
		}

		TEST_METHOD(WaitsTwoSecondsAfterValidatedReadinessWithoutGatingVideo)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.observationTickMs = 1000;
			OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessReason::AwaitingPostReadySettle),
				static_cast<int>(decision.reason));

			input.observationTickMs = 2999;
			decision = controller.Observe(input);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
			Assert::AreEqual<uint32_t>(1999, decision.postReadySettleElapsedMs);

			input.observationTickMs = 3000;
			decision = controller.Observe(input);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::AreEqual<uint32_t>(2000, decision.postReadySettleElapsedMs);
			Assert::IsTrue(decision.allowDownstreamDelivery);

			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);
		}

		TEST_METHOD(HandshakeDelayMatrixNeverChangesExactPrefillTarget)
		{
			const uint64_t handshakeDelaysMs[] =
				{ 0, 100, 500, 2000, 5000, 10000, 15000 };
			const size_t reserveTargets[] = { 2, 3 };
			for (const size_t reserveTarget : reserveTargets)
			{
				for (const uint64_t handshakeDelayMs : handshakeDelaysMs)
				{
					OutputReadinessController controller;
					OutputReadinessInput input = ReadyInput();
					input.reserveFrames = reserveTarget;
					input.observationTickMs = 1000;
					input.graphOperational = false;
					input.displayDecision = DisplayRefreshRateDecision::Warming;
					OutputReadinessDecision decision = controller.Observe(input);
					Assert::IsTrue(decision.allowDownstreamDelivery);

					input.observationTickMs += handshakeDelayMs;
					input.graphOperational = true;
					input.displayDecision = DisplayRefreshRateDecision::Accepted;
					decision = controller.Observe(input);
					Assert::IsTrue(decision.allowDownstreamDelivery);
					Assert::IsFalse(decision.requestSerializedPostReadyReset);

					input.observationTickMs +=
						OutputReadinessController::kPostReadySettleMs;
					decision = controller.Observe(input);
					Assert::IsTrue(decision.requestSerializedPostReadyReset);

					input.postReadyResetCompleted = true;
					input.postReadyEpoch = 42;
					input.currentEpochProcessedDepth = reserveTarget - 1;
					decision = controller.Observe(input);
					Assert::AreEqual(
						static_cast<int>(OutputReadinessState::Prefilling),
						static_cast<int>(decision.state));
					Assert::IsFalse(decision.allowDownstreamDelivery);

					input.currentEpochProcessedDepth = reserveTarget;
					decision = FinishPostResetValidation(
						controller, input, 42);
					Assert::AreEqual(
						static_cast<int>(OutputReadinessState::Steady),
						static_cast<int>(decision.state));
					Assert::IsTrue(decision.allowDownstreamDelivery);
				}
			}
		}

		TEST_METHOD(ProofAppearingDuringSettleCannotCancelCommittedReset)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 2;
			input.observationTickMs = 1000;
			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);

			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 2;
			input.currentGraphConvertedDepth = 2;
			input.currentGraphPostProofDeliverySuccesses = 3;
			input.observationTickMs = 3000;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(EvidenceLossRestartsTheSettleWindow)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.observationTickMs = 1000;
			(void)controller.Observe(input);

			input.observationTickMs = 2500;
			input.displayDecision = DisplayRefreshRateDecision::Warming;
			(void)controller.Observe(input);

			input.displayDecision = DisplayRefreshRateDecision::Accepted;
			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);
			input.observationTickMs = 4499;
			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);
			input.observationTickMs = 4500;
			Assert::IsTrue(
				controller.Observe(input).requestSerializedPostReadyReset);
		}

		TEST_METHOD(ExistingResetDuringSettleCancelsFallbackReset)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.observationTickMs = 1000;
			(void)controller.Observe(input);

			input.observationTickMs = 1500;
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = 8;
			const OutputReadinessDecision decision =
				FinishPostResetValidation(controller, input, 42);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(ExplicitZeroFrameReserveStillUsesPostResetValidation)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 0;
			(void)controller.Observe(input);

			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = 0;
			const OutputReadinessDecision decision =
				FinishPostResetValidation(controller, input, 42);
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
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(FinishPostResetValidation(
					controller, input, 42).state));

			input.transitionGeneration = 8;
			input.correctiveRecoveryGeneration = 8;
			input.postReadyResetCompleted = false;
			input.postReadyEpoch = 0;
			OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.discardLiveCapture);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
			Assert::AreEqual<uint64_t>(8, decision.transitionGeneration);
			input.observationTickMs +=
				OutputReadinessController::kPostReadySettleMs;
			decision = controller.Observe(input);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
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

		TEST_METHOD(HandshakeScaleBlockCannotSuppressFallbackReset)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 1;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 1;
			input.currentGraphConvertedDepth = 1;
			input.currentGraphPostProofDeliverySuccesses = 3;
			input.currentGraphMaximumSuccessfulDeliveryDurationUs = 638545;

			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(EligibleEntryProofWaitsAndIsRevalidatedAtDeadline)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 1;
			input.observationTickMs = 1000;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 1;
			input.currentGraphConvertedDepth = 1;
			input.currentGraphPostProofDeliverySuccesses = 3;
			input.currentGraphMaximumSuccessfulDeliveryDurationUs = 100000;

			OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);

			input.observationTickMs = 3000;
			decision = controller.Observe(input);
			Assert::IsTrue(decision.adoptedCurrentGraph);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(HandshakeScaleBlockDuringSettleVetoesEntryCandidate)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 1;
			input.observationTickMs = 1000;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 1;
			input.currentGraphConvertedDepth = 1;
			input.currentGraphPostProofDeliverySuccesses = 3;
			input.currentGraphMaximumSuccessfulDeliveryDurationUs = 100000;
			(void)controller.Observe(input);

			input.observationTickMs = 3000;
			input.currentGraphMaximumSuccessfulDeliveryDurationUs = 638545;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(LaterQueueEpochCannotReplaceEntryAdoptionCandidate)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 1;
			input.observationTickMs = 1000;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 1;
			input.currentGraphConvertedDepth = 1;
			input.currentGraphPostProofDeliverySuccesses = 3;
			input.currentGraphMaximumSuccessfulDeliveryDurationUs = 100000;
			(void)controller.Observe(input);

			input.observationTickMs = 3000;
			input.currentGraphPrimeEpoch = 42;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(EpsonScaleBlockIsRejectedAt23976)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.expectedOutputRefreshHz = 24000.0 / 1001.0;
			input.observedOutputRefreshHz = 23.9761;
			input.reserveFrames = 1;
			input.currentGraphBoundarySafe = true;
			input.currentGraphPrimeProven = true;
			input.currentGraphPrimeObservedFullConvertedQueue = true;
			input.currentGraphDeliveryRecent = true;
			input.currentGraphPrimeTransitionGeneration = 7;
			input.currentGraphPrimeEpoch = 41;
			input.currentGraphPrimeTargetFrames = 1;
			input.currentGraphConvertedDepth = 1;
			input.currentGraphPostProofDeliverySuccesses = 3;
			input.currentGraphMaximumSuccessfulDeliveryDurationUs = 638545;

			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.adoptedCurrentGraph);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
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

		TEST_METHOD(PostResetNeedsExactEvidenceAndRequestsOneCorrectiveReprime)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			Assert::IsTrue(
				controller.Observe(input).requestSerializedPostReadyReset);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = input.reserveFrames;
			input.observationTickMs = 1000;
			OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(decision.state));

			input.observationTickMs +=
				OutputReadinessController::kPostResetValidationDeadlineMs;
			decision = controller.Observe(input);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::IsTrue(decision.correctiveReprimeAttempted);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::AreEqual(
				static_cast<int>(
					OutputReadinessReason::AwaitingCorrectivePostReadyReset),
				static_cast<int>(decision.reason));
			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);
			controller.RearmResetRequest();
			Assert::IsTrue(
				controller.Observe(input).requestSerializedPostReadyReset);
			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);
		}

		TEST_METHOD(PostResetStableEnvelopeAcceptsTargetMinusOne)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 3;
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = 3;
			input.observationTickMs = 1000;
			SetValidPostResetEvidence(input, 42);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(controller.Observe(input).state));

			input.currentEpochProcessedDepth = 2;
			input.currentGraphConvertedDepth = 2;
			input.observationTickMs = 1001;
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(controller.Observe(input).state));
			input.observationTickMs +=
				OutputReadinessController::kPostResetValidationStableMs;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(PostResetIgnoresHistoricalStartupHandshakeMaximum)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = input.reserveFrames;
			input.observationTickMs = 1000;
			SetValidPostResetEvidence(input, 42);
			input.currentGraphMaximumSuccessfulDeliveryDurationUs = 638545;
			(void)controller.Observe(input);

			input.observationTickMs = 2000;
			OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual<uint32_t>(
				OutputReadinessValidationBlockerNone,
				decision.postResetValidationBlockers);
			Assert::AreEqual<uint32_t>(
				1, decision.postResetValidationStableObservationCount);
			input.observationTickMs = 3000;
			decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(PostResetStableWindowAllowsBoundedTimerJitter)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = input.reserveFrames;
			input.observationTickMs = 1000;
			SetValidPostResetEvidence(input, 42);
			(void)controller.Observe(input);

			input.observationTickMs = 2001;
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(controller.Observe(input).state));
			input.observationTickMs = 3002;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(PostResetFirstHealthyEvidenceAfterDeadlineStillFails)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = input.reserveFrames;
			input.observationTickMs = 1000;
			SetValidPostResetEvidence(input, 42);
			(void)controller.Observe(input);

			input.observationTickMs = 3001;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostReadyResetPending),
				static_cast<int>(decision.state));
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::AreEqual<uint32_t>(
				0, decision.postResetValidationStableObservationCount);
		}

		TEST_METHOD(UnexpectedLiveGapImmediatelyRequestsCorrectiveReprime)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = input.reserveFrames;
			input.observationTickMs = 1000;
			SetValidPostResetEvidence(input, 42);
			(void)controller.Observe(input);

			input.observationTickMs = 1001;
			input.currentGraphUnexpectedLiveDeliveryGapEvents = 1;
			input.currentGraphUnexpectedLiveDeliveryGapSlots = 2;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::IsTrue(decision.correctiveReprimeAttempted);
			Assert::IsTrue(decision.allowDownstreamDelivery);
		}

		TEST_METHOD(TransientRetainedBacklogRestartsStableWindowWithoutReset)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			input.reserveFrames = 3;
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = 3;
			input.observationTickMs = 1000;
			SetValidPostResetEvidence(input, 42);
			(void)controller.Observe(input);

			input.currentGraphConvertedDepth = 2;
			input.currentGraphRetainedSourceBufferCount = 2;
			input.observationTickMs = 1100;
			Assert::IsFalse(
				controller.Observe(input).requestSerializedPostReadyReset);
			input.currentGraphRetainedSourceBufferCount = 0;
			input.observationTickMs = 1200;
			(void)controller.Observe(input);
			input.currentGraphRawDepth = 2;
			input.currentGraphRetainedSourceBufferCount = 2;
			input.observationTickMs = 1300;
			(void)controller.Observe(input);
			input.currentGraphRawDepth = 0;
			input.currentGraphRetainedSourceBufferCount = 0;
			input.observationTickMs = 1400;
			(void)controller.Observe(input);
			input.observationTickMs = 1650;
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(controller.Observe(input).state));
		}

		TEST_METHOD(SecondFailedEpochRequiresManualRecoveryWithoutLooping)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = input.reserveFrames;
			input.observationTickMs = 1000;
			SetValidPostResetEvidence(input, 42);
			(void)controller.Observe(input);
			input.currentGraphUnexpectedLiveDeliveryGapEvents = 1;
			input.currentGraphUnexpectedLiveDeliveryGapSlots = 2;
			input.observationTickMs = 1001;
			Assert::IsTrue(
				controller.Observe(input).requestSerializedPostReadyReset);

			input.postReadyEpoch = 43;
			input.observationTickMs = 2000;
			SetValidPostResetEvidence(input, 43);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(controller.Observe(input).state));
			input.currentGraphUnexpectedLiveDeliveryGapEvents = 1;
			input.currentGraphUnexpectedLiveDeliveryGapSlots = 1;
			input.observationTickMs = 2001;
			OutputReadinessDecision decision = controller.Observe(input);
			Assert::IsTrue(decision.manualRecoveryRequired);
			Assert::IsTrue(decision.allowDownstreamDelivery);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::ManualRecoveryRequired),
				static_cast<int>(decision.state));
			decision = controller.Observe(input);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
			Assert::IsTrue(decision.allowDownstreamDelivery);
		}

		TEST_METHOD(ManualResetCanRecoverTerminalStateWithoutRestoringRetryBudget)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = input.reserveFrames;
			input.observationTickMs = 1000;
			SetValidPostResetEvidence(input, 42);
			(void)controller.Observe(input);
			input.currentGraphUnexpectedLiveDeliveryGapEvents = 1;
			input.currentGraphUnexpectedLiveDeliveryGapSlots = 1;
			input.observationTickMs = 1001;
			(void)controller.Observe(input);

			input.postReadyEpoch = 43;
			input.observationTickMs = 2000;
			SetValidPostResetEvidence(input, 43);
			(void)controller.Observe(input);
			input.currentGraphUnexpectedLiveDeliveryGapEvents = 1;
			input.currentGraphUnexpectedLiveDeliveryGapSlots = 1;
			input.observationTickMs = 2001;
			Assert::IsTrue(controller.Observe(input).manualRecoveryRequired);

			input.postReadyEpoch = 44;
			// A manual graph reset can advance the display-readiness generation,
			// but it must not replenish the renderer/contract corrective budget.
			input.transitionGeneration = 8;
			input.observationTickMs = 3000;
			SetValidPostResetEvidence(input, 44);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(controller.Observe(input).state));
			input.observationTickMs = 3001;
			(void)controller.Observe(input);
			input.observationTickMs +=
				OutputReadinessController::kPostResetValidationStableMs;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::IsTrue(decision.correctiveReprimeAttempted);
			Assert::IsFalse(decision.manualRecoveryRequired);
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

		TEST_METHOD(PostResetValidationIgnoresLegacyHealthTimerPhase)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 42;
			input.currentEpochProcessedDepth = input.reserveFrames;
			input.observationTickMs = 900;
			SetValidPostResetEvidence(input, 42);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(controller.Observe(input).state));

			// Exact epoch-local evidence is healthy while the separate legacy
			// graph-health timer is still waiting.
			input.currentGraphBoundarySafe = false;
			input.currentGraphPostResetBoundarySafe = true;
			input.observationTickMs = 1900;
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostResetValidating),
				static_cast<int>(controller.Observe(input).state));
			input.observationTickMs = 2150;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.correctiveReprimeAttempted);
		}

		TEST_METHOD(CompletionLatchWaitsForStableSnapshotAndRejectsStaleGeneration)
		{
			OutputReadinessCompletionLatch latch;
			Assert::IsFalse(latch.MarkCompleted(17, 0));
			Assert::AreEqual<uint64_t>(
				17, latch.AwaitingSnapshotGeneration());
			Assert::IsFalse(latch.Matches(17));
			Assert::IsTrue(latch.BindStableSnapshot(17, 91));
			Assert::IsTrue(latch.Matches(17));
			Assert::AreEqual<uint64_t>(91, latch.CompletedEpoch());

			Assert::IsFalse(latch.MarkCompleted(18, 0));
			Assert::IsFalse(latch.BindStableSnapshot(19, 92));
			Assert::AreEqual<uint64_t>(
				0, latch.AwaitingSnapshotGeneration());
			Assert::IsFalse(latch.Matches(18));
		}

		TEST_METHOD(CoveredRetargetSettleSuccessorAdvancesBeforePrefill)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 52;
			input.currentGraphQueueEpoch = 52;
			input.currentEpochProcessedDepth = 1;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Prefilling),
				static_cast<int>(decision.state));
			Assert::AreEqual<uint64_t>(52, decision.postReadyEpoch);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(CoveredRetargetSettleSuccessorAdvancesDuringPrefill)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 51;
			input.currentGraphQueueEpoch = 51;
			input.currentEpochProcessedDepth = 1;
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Prefilling),
				static_cast<int>(controller.Observe(input).state));

			// The explicitly covered delayed LiveQueue phase advances E1 to E2.
			input.postReadyEpoch = 52;
			input.currentGraphQueueEpoch = 52;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Prefilling),
				static_cast<int>(decision.state));
			Assert::AreEqual<uint64_t>(52, decision.postReadyEpoch);
			Assert::IsFalse(decision.requestSerializedPostReadyReset);
		}

		TEST_METHOD(UncreditedEpochChangeDuringPrefillUsesBoundedCorrectivePath)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			input.postReadyResetCompleted = true;
			input.postReadyEpoch = 51;
			input.currentGraphQueueEpoch = 51;
			input.currentEpochProcessedDepth = 1;
			(void)controller.Observe(input);

			input.postReadyResetCompleted = false;
			input.postReadyEpoch = 0;
			input.currentGraphQueueEpoch = 52;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostReadyResetPending),
				static_cast<int>(decision.state));
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::IsTrue(decision.correctiveReprimeAttempted);
		}

		TEST_METHOD(UncreditedEpochChangeAfterSteadyUsesBoundedCorrectivePath)
		{
			OutputReadinessController controller;
			OutputReadinessInput input = ReadyInput();
			(void)controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::Steady),
				static_cast<int>(FinishPostResetValidation(
					controller, input, 51).state));

			// A queue-only epoch change not carrying explicit graph/retarget
			// coverage cannot inherit the validated E51 contract.
			input.postReadyResetCompleted = false;
			input.postReadyEpoch = 0;
			input.currentGraphQueueEpoch = 52;
			const OutputReadinessDecision decision = controller.Observe(input);
			Assert::AreEqual(
				static_cast<int>(OutputReadinessState::PostReadyResetPending),
				static_cast<int>(decision.state));
			Assert::IsTrue(decision.requestSerializedPostReadyReset);
			Assert::IsTrue(decision.correctiveReprimeAttempted);
		}
	};
}
