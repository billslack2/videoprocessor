#include "pch.h"
#include "CppUnitTest.h"

#include <EotfTransitionStabilizer.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace Tests
{
	TEST_CLASS(EotfTransitionStabilizerTests)
	{
	public:
		TEST_METHOD(StableTransitionCommitsExactlyOnceAfterSettlingWindow)
		{
			EotfTransitionStabilizer policy(3000, 2);
			policy.Reset(EOTF::SDR);

			Assert::IsTrue(policy.Observe(EOTF::PQ, true, 100).
				action == EotfTransitionAction::CandidateStarted);
			Assert::IsTrue(policy.Observe(EOTF::PQ, true, 900).
				action == EotfTransitionAction::CandidateConfirmed);
			Assert::IsTrue(policy.Evaluate(3099, true).
				action == EotfTransitionAction::None);
			Assert::IsTrue(policy.Evaluate(3100, true).
				action == EotfTransitionAction::CommitRestart);
			Assert::IsTrue(policy.Evaluate(5000, true).
				action == EotfTransitionAction::None);
		}

		TEST_METHOD(FlipBackCancelsPendingTransitionWithoutRestart)
		{
			EotfTransitionStabilizer policy(3000, 2);
			policy.Reset(EOTF::PQ);
			policy.Observe(EOTF::SDR, true, 100);
			policy.Observe(EOTF::SDR, true, 200);

			const auto cancelled = policy.Observe(EOTF::PQ, true, 500);
			Assert::IsTrue(cancelled.action ==
				EotfTransitionAction::CandidateCancelled);
			Assert::IsTrue(cancelled.candidate == EOTF::SDR);
			Assert::IsFalse(policy.HasPendingCandidate());
			Assert::IsTrue(policy.Evaluate(5000, true).
				action == EotfTransitionAction::None);
		}

		TEST_METHOD(EventAndPeriodicObservationsShareAndCoalesceCandidate)
		{
			EotfTransitionStabilizer policy(3000, 2);
			policy.Reset(EOTF::SDR);

			policy.Observe(EOTF::PQ, true, 0); // capture event
			const auto periodic = policy.Observe(EOTF::PQ, true, 1000);
			Assert::IsTrue(periodic.action ==
				EotfTransitionAction::CandidateConfirmed);
			Assert::AreEqual<uint32_t>(2, periodic.matchingObservations);
			Assert::IsTrue(policy.Observe(EOTF::PQ, true, 2000).
				action == EotfTransitionAction::ObservationCoalesced);
			Assert::IsTrue(policy.Evaluate(3000, true).
				action == EotfTransitionAction::CommitRestart);
		}

		TEST_METHOD(DisplayTransitionDefersButDoesNotDuplicateCommit)
		{
			EotfTransitionStabilizer policy(3000, 2);
			policy.Reset(EOTF::SDR);
			policy.Observe(EOTF::PQ, true, 0);
			policy.Observe(EOTF::PQ, true, 1000);

			Assert::IsTrue(policy.Evaluate(3000, false).
				action == EotfTransitionAction::CommitDeferred);
			Assert::IsTrue(policy.Evaluate(4000, false).
				action == EotfTransitionAction::ObservationCoalesced);
			Assert::IsTrue(policy.Evaluate(5000, true).
				action == EotfTransitionAction::CommitRestart);
			Assert::IsTrue(policy.Evaluate(6000, true).
				action == EotfTransitionAction::None);
		}

		TEST_METHOD(InvalidAndUnknownObservationsCannotConfirmCandidate)
		{
			EotfTransitionStabilizer policy(3000, 2);
			policy.Reset(EOTF::SDR);
			policy.Observe(EOTF::PQ, true, 0);

			policy.Observe(EOTF::PQ, false, 1000);
			policy.Observe(EOTF::UNKNOWN, true, 2000);
			Assert::IsFalse(policy.IsConfirmed());
			Assert::IsTrue(policy.Evaluate(5000, true).
				action == EotfTransitionAction::None);
		}
	};
}
