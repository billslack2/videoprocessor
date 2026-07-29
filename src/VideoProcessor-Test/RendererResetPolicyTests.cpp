#include "pch.h"
#include "CppUnitTest.h"

#include <RendererResetPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(RendererResetPolicyTests)
	{
	public:
		TEST_METHOD(DisplayTransitionCannotDelayPostStartReset)
		{
			const int postStartPriority =
				RendererResetPriority(
					RendererResetReason::PostRendererStart);
			const int displayPriority =
				RendererResetPriority(
					RendererResetReason::DisplayTransition);
			Assert::IsFalse(RendererResetShouldReplace(
				displayPriority, 33000,
				postStartPriority, 5000));
		}

		TEST_METHOD(CriticalRecoveryPreemptsDisplaySettle)
		{
			Assert::IsTrue(RendererResetShouldReplace(
				RendererResetPriority(
					RendererResetReason::LivenessRecovery),
				0,
				RendererResetPriority(
					RendererResetReason::DisplayTransition),
				33000));
		}

		TEST_METHOD(EqualPriorityUsesEarliestDeadline)
		{
			const int priority =
				RendererResetPriority(
					RendererResetReason::DisplayTransition);
			Assert::IsTrue(RendererResetShouldReplace(
				priority, 5000, priority, 33000));
			Assert::IsFalse(RendererResetShouldReplace(
				priority, 33000, priority, 5000));
		}

		TEST_METHOD(ManualResetHasDeterministicTopPriority)
		{
			Assert::IsTrue(
				RendererResetPriority(RendererResetReason::Manual) >
				RendererResetPriority(
					RendererResetReason::LivenessRecovery));
		}
	};
}
