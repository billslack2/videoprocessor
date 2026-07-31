#include "pch.h"
#include "CppUnitTest.h"

#include <LiveEpochConvergenceController.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
LiveEpochConvergenceInput Input(uint64_t epoch = 41, uint64_t nominalUs = 16683)
{
	LiveEpochConvergenceInput input;
	input.epoch = epoch;
	input.epochActive = true;
	input.vpConvertedDepth = 2;
	input.desiredVpDepth = 2;
	input.deliveryCompleted = true;
	input.deliverySucceeded = true;
	input.deliveryDurationUs = nominalUs;
	input.nominalFrameDurationUs = nominalUs;
	input.vpRawDepth = 0;
	input.rawDepthKnown = true;
	input.observationTickMs = 1000;
	return input;
}

LiveEpochConvergenceDecision Observe(
	LiveEpochConvergenceController& controller,
	LiveEpochConvergenceInput& input, uint64_t durationUs,
	size_t convertedDepth, uint64_t tickAdvanceMs = 16)
{
	input.deliveryDurationUs = durationUs;
	input.vpConvertedDepth = convertedDepth;
	input.observationTickMs += tickAdvanceMs;
	return controller.Observe(input);
}

void ObserveRealInitialFastDeliveries(
	LiveEpochConvergenceController& controller,
	LiveEpochConvergenceInput& input)
{
	const uint64_t fastDeliveries[] = { 66, 18, 14, 14, 57, 36, 54, 41 };
	const size_t depths[] = { 4, 3, 2, 1, 1, 1, 1, 1 };
	for (size_t index = 0; index < _countof(fastDeliveries); ++index)
	{
		const LiveEpochConvergenceDecision decision = Observe(
			controller, input, fastDeliveries[index], depths[index]);
		Assert::IsFalse(decision.requestConvergence);
		Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::ObservingIngress),
			static_cast<int>(decision.state));
	}
}
}

namespace Tests
{
	TEST_CLASS(LiveEpochConvergenceControllerTests)
	{
	public:
		TEST_METHOD(Real5994TraceFastIngressThenBlockThenRecoveryTrimsOnce)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			ObserveRealInitialFastDeliveries(controller, input);

			LiveEpochConvergenceDecision decision =
				Observe(controller, input, 109800, 6, 125);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::IngressBlocked),
				static_cast<int>(decision.state));
			Assert::AreEqual<uint64_t>(50049, decision.ingressBlockThresholdUs);
			Assert::IsFalse(decision.requestConvergence);

			decision = Observe(controller, input, 124785, 13, 16);
			Assert::AreEqual<uint32_t>(2, decision.ingressBlockCount);
			Assert::IsFalse(decision.requestConvergence);

			decision = Observe(controller, input, 17897, 13);
			Assert::AreEqual<uint32_t>(1, decision.consecutiveRecoveryDeliveryCount);
			decision = Observe(controller, input, 16518, 13);
			Assert::AreEqual<uint32_t>(2, decision.consecutiveRecoveryDeliveryCount);
			decision = Observe(controller, input, 16515, 13);
			Assert::IsTrue(decision.requestConvergence);
			Assert::AreEqual<size_t>(11, decision.staleVpFrames);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::TrimApplied),
				static_cast<int>(decision.state));
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceReason::TrimRequested),
				static_cast<int>(decision.reason));

			decision = Observe(controller, input, 16500, 13);
			Assert::IsFalse(decision.requestConvergence);
		}

		TEST_METHOD(EightFastDeliveriesNeverConvergeBeforeObservedIngressBlock)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			ObserveRealInitialFastDeliveries(controller, input);
			const LiveEpochConvergenceDecision decision = Observe(
				controller, input, 16500, 13);
			Assert::IsFalse(decision.requestConvergence);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::ObservingIngress),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(NoIngressBlockTimesOutWithoutDiscardingConvertedFrames)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			(void)Observe(controller, input, 100, 13, 1);
			input.deliveryCompleted = false;
			input.observationTickMs +=
				LiveEpochConvergenceController::kBlockObservationTimeoutMs;
			const LiveEpochConvergenceDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::UnprovenNoTrim),
				static_cast<int>(decision.state));
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceReason::BlockObservationTimedOut),
				static_cast<int>(decision.reason));
		}

		TEST_METHOD(ArmedPolicyWaitsForBacklogThenSettlesWithoutTrim)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			(void)Observe(controller, input, 60000, 2, 1);
			(void)Observe(controller, input, 16683, 2);
			(void)Observe(controller, input, 16683, 2);
			LiveEpochConvergenceDecision decision = Observe(controller, input, 16683, 2);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::Armed),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.requestConvergence);

			input.deliveryCompleted = false;
			input.observationTickMs +=
				LiveEpochConvergenceController::kArmedConvergenceWindowMs;
			decision = controller.Observe(input);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::SettledNoTrim),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(NonZeroRawDepthDefersAndNeverClaimsTotalVpTarget)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			input.vpRawDepth = 1;
			(void)Observe(controller, input, 60000, 13, 1);
			(void)Observe(controller, input, 16683, 13);
			(void)Observe(controller, input, 16683, 13);
			LiveEpochConvergenceDecision decision = Observe(controller, input, 16683, 13);
			Assert::IsFalse(decision.requestConvergence);
			Assert::IsFalse(decision.rawZeroPreconditionMet);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::DeferredRawNotEmpty),
				static_cast<int>(decision.state));

			input.deliveryCompleted = false;
			input.observationTickMs +=
				LiveEpochConvergenceController::kArmedConvergenceWindowMs;
			decision = controller.Observe(input);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::UnprovenNoTrim),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(Exact23976ThresholdAndRecoveryAreRateIndependent)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input(41, 41708);
			LiveEpochConvergenceDecision decision = Observe(controller, input, 125123, 13, 1);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::ObservingIngress),
				static_cast<int>(decision.state));
			Assert::AreEqual<uint64_t>(125124, decision.ingressBlockThresholdUs);
			decision = Observe(controller, input, 125124, 13);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::IngressBlocked),
				static_cast<int>(decision.state));
			(void)Observe(controller, input, 41708, 13);
			(void)Observe(controller, input, 41708, 13);
			decision = Observe(controller, input, 41708, 13);
			Assert::IsTrue(decision.requestConvergence);
		}

		TEST_METHOD(Startup23976DoesNotMisclassifySixtyMillisecondsAsBlocked)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input(41, 41708);
			const LiveEpochConvergenceDecision decision =
				Observe(controller, input, 60000, 13, 60);
			Assert::AreEqual<uint64_t>(125124, decision.ingressBlockThresholdUs);
			Assert::AreEqual(
				static_cast<int>(LiveEpochConvergenceState::ObservingIngress),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.requestConvergence);
		}

		TEST_METHOD(SceneCadenceCannotRequestAConvertedQueueTrim)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			input.sceneCadenceActive = true;
			for (uint32_t index = 0; index < 8; ++index)
			{
				const uint64_t duration = index == 0 ? 60000 : 16683;
				const LiveEpochConvergenceDecision decision =
					Observe(controller, input, duration, 13);
				Assert::IsFalse(decision.requestConvergence);
				Assert::AreEqual(
					static_cast<int>(LiveEpochConvergenceReason::UnsafeBoundary),
					static_cast<int>(decision.reason));
			}
		}

		TEST_METHOD(NewEpochRearmsButTargetChangeWithinEpochDoesNot)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			(void)Observe(controller, input, 60000, 13, 1);
			input.desiredVpDepth = 3;
			LiveEpochConvergenceDecision decision = controller.Observe(input);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::UnprovenNoTrim),
				static_cast<int>(decision.state));
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceReason::TargetChangedWithinEpoch),
				static_cast<int>(decision.reason));

			input.epoch = 42;
			input.desiredVpDepth = 2;
			input.deliveryCompleted = true;
			input.deliverySucceeded = true;
			decision = Observe(controller, input, 60000, 13, 1);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::IngressBlocked),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(DeliveryFailureAndUnsafeBoundaryCannotRequestTrim)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			(void)Observe(controller, input, 60000, 13, 1);
			(void)Observe(controller, input, 16683, 13);
			input.resetOrFlushInProgress = true;
			LiveEpochConvergenceDecision decision = Observe(controller, input, 16683, 13);
			Assert::IsFalse(decision.requestConvergence);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceReason::UnsafeBoundary),
				static_cast<int>(decision.reason));
			input.resetOrFlushInProgress = false;
			input.deliverySucceeded = false;
			decision = Observe(controller, input, 0, 13);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::UnprovenNoTrim),
				static_cast<int>(decision.state));
		}
	};
}
