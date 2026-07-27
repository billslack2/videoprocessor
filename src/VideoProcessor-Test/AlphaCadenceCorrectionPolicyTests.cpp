#include "pch.h"
#include "CppUnitTest.h"

#include <libplacebo/AlphaCadenceCorrectionPolicy.h>

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
			Assert::IsFalse(ambiguous.lastVerificationSucceeded);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(ambiguous.action));
		}
	};
}
