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

		TEST_METHOD(QueueDirectionDisagreementBlocksAction)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			input.queueDepth = 3;
			Advance(policy, input, 5200);
			input.safeSceneBoundary = true;
			input.sceneEventId = 3;

			const auto decision = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(decision.action));
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::DropQueueNotAboveDesired),
				Reason(decision.blockReason));
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

		TEST_METHOD(DueDropDistinguishesQueueDebtAndFallbackAgeBlockers)
		{
			AlphaCadenceCorrectionPolicy queuePolicy;
			AlphaCadenceCorrectionInput queueInput = Input(60.012);
			queueInput.queueDepth = queueInput.desiredQueueDepth;
			const auto queueBlocked =
				AdvanceUntilDue(queuePolicy, queueInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::DropQueueNotAboveDesired),
				Reason(queueBlocked.blockReason));

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
	};
}
