#include "pch.h"
#include "CppUnitTest.h"

#include <functional>
#include <vector>

#include <EpochBoundedQueue.h>
#include <LiveEpochConvergenceController.h>
#include <LiveSteadyQueuePolicy.h>

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

		TEST_METHOD(ExplicitZeroTargetRemainsEnabledAndTrimsToZero)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			input.desiredVpDepth = 0;
			input.targetConfigured = true;

			(void)Observe(controller, input, 60000, 4, 1);
			(void)Observe(controller, input, 16683, 4);
			(void)Observe(controller, input, 16683, 4);
			const LiveEpochConvergenceDecision decision =
				Observe(controller, input, 16683, 4);

			Assert::IsTrue(decision.requestConvergence);
			Assert::AreEqual<size_t>(4, decision.staleVpFrames);
			Assert::AreEqual(
				static_cast<int>(LiveEpochConvergenceState::TrimApplied),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(ObservationTimeoutRemainsEligibleForLateHandshakeEvidence)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			(void)Observe(controller, input, 100, 13, 1);
			input.deliveryCompleted = false;
			input.observationTickMs +=
				LiveEpochConvergenceController::kBlockObservationTimeoutMs;
			const LiveEpochConvergenceDecision decision = controller.Observe(input);
			Assert::IsFalse(decision.requestConvergence);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::ObservingIngress),
				static_cast<int>(decision.state));
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceReason::BlockObservationTimedOut),
				static_cast<int>(decision.reason));

			// A slow HDMI handshake may return its first conclusive blocked
			// delivery after the diagnostic observation window has elapsed.
			input.deliveryCompleted = true;
			LiveEpochConvergenceDecision lateDecision =
				Observe(controller, input, 60000, 13, 1);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::IngressBlocked),
				static_cast<int>(lateDecision.state));
			(void)Observe(controller, input, 16683, 13);
			(void)Observe(controller, input, 16683, 13);
			lateDecision = Observe(controller, input, 16683, 13);
			Assert::IsTrue(lateDecision.requestConvergence);
			Assert::AreEqual(
				static_cast<int>(LiveEpochConvergenceActivation::HardBlockRecovery),
				static_cast<int>(lateDecision.activation));
		}

		TEST_METHOD(HandshakeDelayMatrixPreservesConfiguredSteadyQueueTarget)
		{
			const uint64_t handshakeDelaysMs[] =
				{ 0, 100, 500, 2000, 5000, 10000, 15000 };
			const size_t desiredDepths[] = { 2, 3 };
			const size_t launchDepths[] = { 16, 32 };

			for (const size_t desiredDepth : desiredDepths)
			{
				for (const size_t launchDepth : launchDepths)
				{
					for (const uint64_t handshakeDelayMs : handshakeDelaysMs)
					{
						LiveEpochConvergenceController controller;
						LiveEpochConvergenceInput input = Input(
							41, desiredDepth == 2 ? 41708 : 16683);
						input.desiredVpDepth = desiredDepth;
						input.targetConfigured = true;
						input.vpRawDepth = 3;
						input.observationTickMs = 1000 + handshakeDelayMs;

						// Inject the HDMI/madVR readiness delay without manufacturing
						// deliveries during it. The first downstream-scale block can
						// arrive immediately or fifteen seconds into the same epoch.
						input.deliveryCompleted = false;
						Assert::IsFalse(controller.Observe(input).requestConvergence);
						input.deliveryCompleted = true;
						LiveEpochConvergenceDecision decision = Observe(
							controller, input,
							input.nominalFrameDurationUs * 3,
							launchDepth, 1);
						Assert::AreEqual(
							static_cast<int>(LiveEpochConvergenceState::IngressBlocked),
							static_cast<int>(decision.state));

						for (int recovery = 0; recovery < 3; ++recovery)
						{
							decision = Observe(controller, input,
								input.nominalFrameDurationUs,
								launchDepth);
						}

						Assert::IsTrue(decision.requestConvergence);
						Assert::AreEqual<size_t>(desiredDepth,
							launchDepth - decision.staleConvertedFrames);
						Assert::AreEqual<size_t>(launchDepth - desiredDepth,
							decision.staleConvertedFrames);
						Assert::AreEqual(
							static_cast<int>(
								LiveEpochConvergenceActivation::HardBlockRecovery),
							static_cast<int>(decision.activation));
					}
				}
			}
		}

		TEST_METHOD(Paced23976IngressPrimesWithoutThreeFrameStall)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input(41, 41708);
			input.desiredVpDepth = 1;
			input.targetConfigured = true;
			input.vpRawDepth = 12;

			// madVR accepts its initial burst immediately.
			for (int index = 0; index < 6; ++index)
				Assert::IsFalse(Observe(controller, input, 100, 32, 1).requestConvergence);

			// Once the downstream path is primed, 23.976-to-59.94 delivery is
			// paced in the observed alternating ~33/~50ms cadence. Its six-call
			// mean is one input-frame period without any >=3F hard stall.
			const uint64_t pacedDurations[] =
				{ 33000, 50000, 33000, 50000, 33000, 50000 };
			LiveEpochConvergenceDecision decision;
			for (uint64_t duration : pacedDurations)
				decision = Observe(controller, input, duration, 32, 42);

			Assert::IsTrue(decision.requestConvergence);
			Assert::AreEqual<size_t>(31, decision.staleConvertedFrames);
			Assert::AreEqual<uint32_t>(6,
				decision.consecutivePacedDeliveryCount);
			Assert::AreEqual<size_t>(8, decision.pacedPrimingDepth);
			Assert::AreEqual(
				static_cast<int>(LiveEpochConvergenceActivation::PacedPrime),
				static_cast<int>(decision.activation));
		}

		TEST_METHOD(PacedIngressRequiresLocalPrimingBacklog)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input(41, 41708);
			input.desiredVpDepth = 1;
			input.targetConfigured = true;
			input.vpRawDepth = 0;

			for (int index = 0; index < 6; ++index)
				(void)Observe(controller, input, 100, 2, 1);
			LiveEpochConvergenceDecision decision;
			for (int index = 0; index < 6; ++index)
				decision = Observe(controller, input, 41708, 2, 42);
			Assert::IsFalse(decision.requestConvergence);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::ObservingIngress),
				static_cast<int>(decision.state));

			// The same proven paced path becomes actionable once VP has enough
			// local stale work to make a latest-wins transition meaningful.
			decision = Observe(controller, input, 41708, 8, 42);
			Assert::IsTrue(decision.requestConvergence);
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

		TEST_METHOD(ObservedAsymmetricBacklogActivatesConvertedLatestWinsCatchUp)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			input.desiredVpDepth = 1;
			input.vpRawDepth = 29;
			(void)Observe(controller, input, 109800, 32, 1);
			(void)Observe(controller, input, 16683, 32);
			(void)Observe(controller, input, 16683, 32);
			LiveEpochConvergenceDecision decision = Observe(
				controller, input, 16683, 32);
			Assert::IsTrue(decision.requestConvergence);
			Assert::IsTrue(decision.rawDepthKnown);
			Assert::IsTrue(decision.rawBacklogObserved);
			Assert::AreEqual<size_t>(0, decision.staleRawFrames);
			Assert::AreEqual<size_t>(31, decision.staleConvertedFrames);
			Assert::AreEqual<size_t>(31, decision.staleVpFrames);
			Assert::AreEqual(static_cast<int>(LiveEpochConvergenceState::TrimApplied),
				static_cast<int>(decision.state));

			decision = Observe(controller, input, 16683, 32);
			Assert::IsFalse(decision.requestConvergence);
		}

		TEST_METHOD(SteadyQueueAppliesBackpressureBeforeConverting)
		{
			LiveSteadyQueueDecision decision = LiveSteadyQueuePolicy::Evaluate({
				5, 5, true, false, 1, 22 });
			Assert::IsTrue(decision.active);
			Assert::AreEqual<size_t>(1, decision.highWater);
			Assert::IsTrue(decision.holdConversion);

			decision = LiveSteadyQueuePolicy::Evaluate({
				5, 5, true, false, 0, 0 });
			Assert::AreEqual<size_t>(1, decision.highWater);
			Assert::IsFalse(decision.holdConversion);
			decision = LiveSteadyQueuePolicy::Evaluate({
				5, 5, true, false, 0, 1 });
			Assert::IsTrue(decision.holdConversion);

			decision = LiveSteadyQueuePolicy::Evaluate({
				5, 6, true, false, 1, 22 });
			Assert::IsFalse(decision.active);
			decision = LiveSteadyQueuePolicy::Evaluate({
				5, 5, true, true, 1, 22 });
			Assert::IsTrue(decision.active);
			Assert::IsTrue(decision.holdConversion);
		}

		TEST_METHOD(SteadyCaptureUsesBoundedQueueCapacity)
		{
			const PipelineEpoch epoch{ 7 };
			std::vector<int> released;
			EpochBoundedQueue<int, std::function<void(int&)>> raw(
				32, [&released](int& frame) { released.push_back(frame); });
			const LiveSteadyQueueDecision decision =
				LiveSteadyQueuePolicy::Evaluate({ 7, 7, true, false, 3, 3 });

			Assert::IsTrue(decision.active);
			Assert::IsTrue(decision.holdConversion);
			Assert::AreEqual(static_cast<int>(EpochBoundedQueuePushResult::Accepted),
				static_cast<int>(raw.Push(100, epoch, epoch)));
			Assert::AreEqual(static_cast<int>(EpochBoundedQueuePushResult::Accepted),
				static_cast<int>(raw.Push(101, epoch, epoch)));
			Assert::AreEqual<size_t>(2, raw.Size());
			Assert::AreEqual<size_t>(0, released.size());
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

		TEST_METHOD(ExplicitTargetConvergesDuringSceneCadence)
		{
			LiveEpochConvergenceController controller;
			LiveEpochConvergenceInput input = Input();
			input.sceneCadenceActive = true;
			(void)Observe(controller, input, 60000, 13, 1);
			(void)Observe(controller, input, 16683, 13);
			(void)Observe(controller, input, 16683, 13);
			const LiveEpochConvergenceDecision decision =
				Observe(controller, input, 16683, 13);
			Assert::IsTrue(decision.requestConvergence);
			Assert::AreEqual(
				static_cast<int>(LiveEpochConvergenceState::TrimApplied),
				static_cast<int>(decision.state));
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
