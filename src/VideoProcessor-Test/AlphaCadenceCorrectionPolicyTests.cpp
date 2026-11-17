#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaCadenceCorrectionPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(AlphaCadenceCorrectionPolicyTests)
	{
	public:
		static AlphaCadenceCorrectionInput Input(double captureRate)
		{
			AlphaCadenceCorrectionInput input;
			input.enabled = true;
			input.generation = 1;
			input.presentationEvidence = AlphaPresentationEvidence::Stable;
			input.captureRateHz = captureRate;
			input.displayRateHz = 60.0;
			input.desiredQueueDepth = 4;
			input.queueDepth = 5;
			input.presentationDebt = 1;
			return input;
		}

		static AlphaCadenceCorrectionDecision Advance(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput input,
			uint32_t frames)
		{
			AlphaCadenceCorrectionDecision decision;
			for (uint32_t frame = 0; frame < frames; ++frame)
				decision = policy.Evaluate(input);
			return decision;
		}

		static AlphaCadenceCorrectionDecision AdvanceUntilDue(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput input,
			uint32_t maximumFrames = 20000)
		{
			for (uint32_t frame = 0; frame < maximumFrames; ++frame)
			{
				const auto decision = policy.Evaluate(input);
				if (decision.due)
					return decision;
			}
			Assert::Fail(L"Cadence correction did not become due");
			return {};
		}

		static int Reason(AlphaCadenceBlockReason reason)
		{
			return static_cast<int>(reason);
		}

		TEST_METHOD(InvalidPresentationEvidenceAlwaysFailsClosed)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			input.presentationEvidence = AlphaPresentationEvidence::Disjoint;
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;

			const auto decision = Advance(policy, input, 10000);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(decision.action));
			Assert::IsFalse(decision.planned);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::PresentationEvidenceUnavailable),
				Reason(decision.blockReason));
		}

		TEST_METHOD(IncompatibleRatesCannotPlanOrAct)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(61.0);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;

			const auto decision = Advance(policy, input, 10000);
			Assert::IsFalse(decision.ratesCompatible);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(decision.action));
		}

		TEST_METHOD(Capture23976To60HzMonitorNeverBecomesPpmOrQueueCorrection)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(24000.0 / 1001.0);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			input.queueDepth = 8;
			input.desiredQueueDepth = 8;

			const auto decision = Advance(policy, input, 10000);
			Assert::IsFalse(decision.ratesCompatible);
			Assert::IsFalse(decision.predictionValid);
			Assert::IsFalse(decision.planned);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(decision.action));
			Assert::AreEqual(Reason(AlphaCadenceBlockReason::RateMismatchTooLarge),
				Reason(decision.blockReason));
		}

		TEST_METHOD(PublishesLongRangePredictionBeforePlanningWindow)
		{
			AlphaCadenceCorrectionPolicy policy;
			const auto decision = Advance(policy, Input(60.012), 601);

			Assert::IsTrue(decision.predictionValid);
			Assert::IsFalse(decision.planned);
			Assert::AreEqual(
				static_cast<int>(AlphaCadenceTimingStatus::Forecasting),
				static_cast<int>(decision.timingStatus));
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.predictedAction));
			Assert::IsTrue(decision.secondsUntilCorrection > 70.0);
			Assert::IsTrue(decision.secondsUntilCorrection < 80.0);
			Assert::IsTrue(decision.secondsUntilPlan > 50.0);
			Assert::IsTrue(decision.secondsUntilPlan < 60.0);
			Assert::IsTrue(decision.secondsUntilPlan <
				decision.secondsUntilCorrection);
		}

		TEST_METHOD(ReportsMatchedWhenStableRatesNeedNoCorrection)
		{
			AlphaCadenceCorrectionPolicy policy;
			const auto decision = Advance(policy, Input(60.0), 601);

			Assert::IsTrue(decision.ratesCompatible);
			Assert::IsFalse(decision.predictionValid);
			Assert::AreEqual(
				static_cast<int>(AlphaCadenceTimingStatus::Matched),
				static_cast<int>(decision.timingStatus));
		}

		TEST_METHOD(NearZeroNoiseCannotReversePredictionDirection)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.0 * (1.0 + 5e-6));
			auto decision = Advance(policy, input, 601);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.predictedAction));

			for (uint32_t frame = 0; frame < 1200; ++frame)
			{
				const double ppm = frame % 2 == 0 ? -20.0 : 20.0;
				input.captureRateHz = 60.0 * (1.0 + ppm / 1000000.0);
				decision = policy.Evaluate(input);
				Assert::IsFalse(
					decision.predictedAction == AlphaCadenceAction::Repeat);
			}
		}

		TEST_METHOD(DropRequiresPhaseQueueDebtAndSceneAgreement)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			Advance(policy, input, 5120);
			input.safeSceneBoundary = true;
			input.sceneEventId = 7;
			const auto decision = policy.Evaluate(input);

			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.action));
			Assert::AreEqual(static_cast<uint64_t>(7), decision.sceneEventId);
			Assert::IsTrue(decision.verificationPending);
		}

		TEST_METHOD(RepeatRequiresLowQueueAndNoPresentationDebt)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(59.988);
			input.queueDepth = 3;
			input.presentationDebt = 0;
			Advance(policy, input, 5120);
			input.safeSceneBoundary = true;
			input.sceneEventId = 9;
			const auto decision = policy.Evaluate(input);

			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Repeat),
				static_cast<int>(decision.action));
		}

		TEST_METHOD(DueDropGoesAtFreshSceneBelowDesiredQueue)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			input.queueDepth = 3;
			Advance(policy, input, 5200);
			input.safeSceneBoundary = true;
			input.sceneEventId = 3;

			const auto decision = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.action));
			Assert::AreEqual(static_cast<uint64_t>(3), decision.sceneEventId);
		}

		TEST_METHOD(DueForecastPublishesSignedOverdueTime)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(59.988);
			input.queueDepth = input.desiredQueueDepth;
			input.presentationDebt = 0;

			const auto due = AdvanceUntilDue(policy, input);
			Assert::IsTrue(due.predictionValid);
			Assert::IsTrue(due.due);
			Assert::IsTrue(due.secondsUntilCorrection <= 0.0);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::RepeatQueueNotBelowDesired),
				Reason(due.blockReason));

			const auto overdue = Advance(policy, input, 60);
			Assert::IsTrue(overdue.due);
			Assert::IsTrue(overdue.secondsUntilCorrection < 0.0);
		}

		TEST_METHOD(DueRepeatDistinguishesQueueAndDebtBlockers)
		{
			AlphaCadenceCorrectionPolicy queuePolicy;
			AlphaCadenceCorrectionInput queueInput = Input(59.988);
			queueInput.queueDepth = queueInput.desiredQueueDepth;
			queueInput.presentationDebt = 0;
			const auto queueBlocked =
				AdvanceUntilDue(queuePolicy, queueInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::RepeatQueueNotBelowDesired),
				Reason(queueBlocked.blockReason));

			AlphaCadenceCorrectionPolicy debtPolicy;
			AlphaCadenceCorrectionInput debtInput = Input(59.988);
			debtInput.queueDepth = debtInput.desiredQueueDepth - 1;
			debtInput.presentationDebt = 1;
			const auto debtBlocked = AdvanceUntilDue(debtPolicy, debtInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::RepeatPresentationDebtPresent),
				Reason(debtBlocked.blockReason));
			Assert::AreEqual(
				debtInput.queueDepth,
				debtBlocked.diagnostic.queueDepth);
			Assert::AreEqual(
				debtInput.desiredQueueDepth,
				debtBlocked.diagnostic.desiredQueueDepth);
			Assert::AreEqual(
				debtInput.presentationDebt,
				debtBlocked.diagnostic.presentationDebt);
			Assert::IsTrue(
				debtBlocked.diagnostic.fallbackMature);
			Assert::IsTrue(
				debtBlocked.diagnostic.fallbackEligible);
		}

		TEST_METHOD(DueDropDistinguishesDebtAndFallbackAgeBlockers)
		{
			AlphaCadenceCorrectionPolicy debtPolicy;
			AlphaCadenceCorrectionInput debtInput = Input(60.012);
			debtInput.presentationDebt = 0;
			const auto debtBlocked = AdvanceUntilDue(debtPolicy, debtInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::DropPresentationDebtMissing),
				Reason(debtBlocked.blockReason));

			AlphaCadenceCorrectionPolicy fallbackPolicy;
			AlphaCadenceCorrectionInput fallbackInput = Input(60.012);
			fallbackInput.oldestQueuedAgeMs = 0.0;
			const auto fallbackBlocked =
				AdvanceUntilDue(fallbackPolicy, fallbackInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::DropFallbackQueueTooYoung),
				Reason(fallbackBlocked.blockReason));
		}

		TEST_METHOD(DueRepeatUsesBoundedNoSceneFallback)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(59.988);
			input.queueDepth = input.desiredQueueDepth - 1;
			input.presentationDebt = 0;

			const auto decision = AdvanceUntilDue(policy, input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Repeat),
				static_cast<int>(decision.action));
			Assert::IsTrue(decision.deadlineFallback);
			Assert::AreEqual(Reason(AlphaCadenceBlockReason::None),
				Reason(decision.blockReason));
		}

		TEST_METHOD(GenerationReplacementClearsPendingAction)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			Advance(policy, input, 5120);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(policy.Evaluate(input).action));

			input.generation = 2;
			const auto decision = policy.Evaluate(input);
			Assert::IsFalse(decision.verificationPending);
			Assert::AreEqual(0.0, decision.phaseFrames);
		}

		TEST_METHOD(VerificationRequiresDebtToChangeByExactlyOne)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			Advance(policy, input, 5120);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			input.lastPresentId = 10;
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(policy.Evaluate(input).action));

			input.safeSceneBoundary = false;
			input.lastPresentId = 11;
			input.presentationDebt = 1;
			const auto ambiguous = policy.Evaluate(input);
			Assert::IsTrue(ambiguous.verificationCompleted);
			Assert::AreEqual(
				static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(ambiguous.verificationAction));
			Assert::IsFalse(ambiguous.lastVerificationSucceeded);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(ambiguous.action));
		}

		TEST_METHOD(OneSurplusSourceDropsAtFreshScene)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			input.queueDepth = input.desiredQueueDepth;
			input.presentationDebt = 1;
			const auto due = AdvanceUntilDue(policy, input);
			Assert::AreNotEqual(
				Reason(AlphaCadenceBlockReason::DropQueueNotAboveDesired),
				Reason(due.blockReason));

			input.safeSceneBoundary = true;
			input.sceneEventId = 5;
			const auto decision = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.action));
			Assert::AreEqual(static_cast<uint64_t>(5), decision.sceneEventId);
		}

		TEST_METHOD(HeldSurplusBringsSlowForecastDue)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.0 * (1.0 + 14.6e-6));
			input.queueDepth = input.desiredQueueDepth;

			const auto due = AdvanceUntilDue(policy, input, 720);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(due.predictedAction));
			Assert::IsTrue(due.phaseFrames >= 1.0);
		}

		TEST_METHOD(RestingQueueAtZeroPpmNeverBecomesDue)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.0);
			input.queueDepth = input.desiredQueueDepth - 1;
			input.safeSceneBoundary = true;

			for (uint32_t frame = 0; frame < 10000; ++frame)
			{
				input.sceneEventId = frame + 1;
				const auto decision = policy.Evaluate(input);
				Assert::IsFalse(decision.due);
				Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
					static_cast<int>(decision.action));
			}
		}

		TEST_METHOD(SurplusLeftAfterResetBecomesDueAgain)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.0 * (1.0 + 14.6e-6));
			input.queueDepth = input.desiredQueueDepth;
			AdvanceUntilDue(policy, input, 720);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(policy.Evaluate(input).action));

			input.generation = 2;
			input.safeSceneBoundary = false;
			input.sceneEventId = 0;
			const auto due = AdvanceUntilDue(policy, input, 120 + 600 + 120);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::DropFallbackQueueTooYoung),
				Reason(due.blockReason));

			input.safeSceneBoundary = true;
			input.sceneEventId = 2;
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(policy.Evaluate(input).action));
		}

		TEST_METHOD(IncompatibleRatesDiscardSurplusConfirmation)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input =
				Input(60.0 * (1.0 + 14.6e-6));
			input.queueDepth = input.desiredQueueDepth;
			Advance(policy, input, 718); // 600 rate samples, then 118 surplus samples.

			// A valid but grossly incompatible mode invalidates the partial
			// surplus confirmation immediately, without using the invalid-rate
			// input gate that also resets the rate filter.
			input.captureRateHz = 240.0;
			const auto incompatible = policy.Evaluate(input);
			Assert::IsFalse(incompatible.ratesCompatible);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::RateMismatchTooLarge),
				Reason(incompatible.blockReason));

			input.captureRateHz = 60.0 * (1.0 + 14.6e-6);
			input.safeSceneBoundary = false;
			input.sceneEventId = 0;
			bool recovered = false;
			for (uint32_t frame = 0; frame < 5000; ++frame)
			{
				const auto decision = policy.Evaluate(input);
				if (decision.ratesCompatible)
				{
					recovered = true;
					break;
				}
			}
			Assert::IsTrue(recovered,
				L"Compatible cadence rates did not recover after the mode change");

			// The first compatible evaluation counted as stable sample one.
			// Complete the 120-sample stabilization, then present a fresh cut
			// while the queue still has exactly one surplus source.
			Advance(policy, input, 119);
			input.safeSceneBoundary = true;
			input.sceneEventId = 7;
			const auto recoveredCut = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(recoveredCut.action));
			Assert::IsFalse(recoveredCut.due);
			Assert::IsTrue(recoveredCut.phaseFrames < 1.0);
		}

		TEST_METHOD(UnavailableEvidenceDiscardsSurplusConfirmation)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input =
				Input(60.0 * (1.0 + 14.6e-6));
			input.queueDepth = input.desiredQueueDepth;
			Advance(policy, input, 718); // 600 rate samples, then 118 surplus samples.

			input.presentationEvidence = AlphaPresentationEvidence::Unavailable;
			const auto unavailable = policy.Evaluate(input);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::PresentationEvidenceUnavailable),
				Reason(unavailable.blockReason));

			input.presentationEvidence = AlphaPresentationEvidence::Stable;
			input.safeSceneBoundary = true;
			input.sceneEventId = 8;
			for (uint32_t frame = 0; frame < 600; ++frame)
			{
				const auto decision = policy.Evaluate(input);
				if (frame == 599)
				{
					Assert::IsTrue(decision.ratesCompatible);
					Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
						static_cast<int>(decision.action));
					Assert::IsFalse(decision.due);
					Assert::IsTrue(decision.phaseFrames < 1.0);
				}
			}
		}

		TEST_METHOD(QueueSurplusConfirmationRestartsAfterQueueDip)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input =
				Input(60.0 * (1.0 + 14.6e-6));
			input.queueDepth = input.desiredQueueDepth;
			Advance(policy, input, 718); // 600 rate samples, then 118 surplus samples.

			input.queueDepth = input.desiredQueueDepth - 1;
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			const auto atRest = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(atRest.action));
			Assert::IsFalse(atRest.due);

			input.queueDepth = input.desiredQueueDepth;
			input.sceneEventId = 2;
			const auto firstNewSurplus = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(firstNewSurplus.action));
			Assert::IsFalse(firstNewSurplus.due);
			Assert::IsTrue(firstNewSurplus.phaseFrames < 1.0);
		}

		TEST_METHOD(CooldownAndFreshSceneGateTheNextCorrection)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.009);
			input.queueDepth = input.desiredQueueDepth - 1;
			input.presentationDebt = 1;

			const auto due = AdvanceUntilDue(policy, input);
			Assert::IsTrue(due.due);
			input.queueDepth = input.desiredQueueDepth;
			input.safeSceneBoundary = true;
			input.sceneEventId = 7;
			const auto firstDrop = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(firstDrop.action));

			// Reflect one new present with the expected one-unit debt reduction.
			input.queueDepth = input.desiredQueueDepth - 1;
			input.safeSceneBoundary = false;
			input.lastPresentId = 1;
			input.presentationDebt = 0;
			const auto verified = policy.Evaluate(input);
			Assert::IsTrue(verified.verificationCompleted);
			Assert::IsTrue(verified.lastVerificationSucceeded);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::Cooldown),
				Reason(verified.blockReason));
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(verified.action));
			for (uint32_t frame = 0; frame < 119; ++frame)
			{
				const auto cooldown = policy.Evaluate(input);
				Assert::AreEqual(
					Reason(AlphaCadenceBlockReason::Cooldown),
					Reason(cooldown.blockReason));
				Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
					static_cast<int>(cooldown.action));
			}

			input.presentationDebt = 1;
			// Let real phase drift reach the next due point at a resting queue.
			const auto nextDue = AdvanceUntilDue(policy, input);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::DropFallbackQueueTooYoung),
				Reason(nextDue.blockReason));
			input.queueDepth = input.desiredQueueDepth;
			input.safeSceneBoundary = true;
			input.sceneEventId = 7;
			const auto repeatedScene = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(repeatedScene.action));
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::DropFallbackQueueTooYoung),
				Reason(repeatedScene.blockReason));

			input.sceneEventId = 8;
			const auto freshScene = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(freshScene.action));
		}
	};
}
