#include "pch.h"
#include "CppUnitTest.h"

#include <RendererTransitionModel.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace Tests
{
	namespace
	{
		void AssertSingleAction(
			const RendererTransitionModel::Actions& actions,
			RendererTransitionActionType expected,
			const RendererTransitionKey& key)
		{
			Assert::AreEqual<size_t>(1, actions.size());
			Assert::IsTrue(actions.front().type == expected);
			Assert::IsTrue(actions.front().key == key);
		}

		RendererTransitionKey AcquireAndStartReset(
			RendererTransitionModel& model,
			uint32_t generation = 7,
			uint64_t targetRevision = 3)
		{
			const auto acquire =
				model.BeginReset(generation, targetRevision);
			const RendererTransitionKey key = model.Key();
			AssertSingleAction(
				acquire,
				RendererTransitionActionType::AcquireShield,
				key);
			const auto execute = model.OnShieldAcquired(key, true);
			AssertSingleAction(
				execute,
				RendererTransitionActionType::ExecuteReset,
				key);
			model.OnResetStarted(key);
			return key;
		}
	}


	TEST_CLASS(RendererTransitionModelTests)
	{
	public:
		TEST_METHOD(ResetCannotStartBeforeMatchingShieldAck)
		{
			RendererTransitionModel model;
			const auto acquire = model.BeginReset(4, 2);
			const RendererTransitionKey key = model.Key();
			AssertSingleAction(
				acquire,
				RendererTransitionActionType::AcquireShield,
				key);
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::AcquiringBlack);

			RendererTransitionKey stale = key;
			++stale.transitionToken;
			Assert::IsTrue(
				model.OnShieldAcquired(stale, true).empty());
			Assert::IsTrue(model.OnResetStarted(key).empty());
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::AcquiringBlack);

			const auto execute = model.OnShieldAcquired(key, true);
			AssertSingleAction(
				execute,
				RendererTransitionActionType::ExecuteReset,
				key);
			Assert::IsTrue(
				model.State() == RendererTransitionState::BlackHeld);
		}

		TEST_METHOD(ShieldFailureDoesNotResetOrRelease)
		{
			RendererTransitionModel model;
			model.BeginReset(5, 1);
			const RendererTransitionKey key = model.Key();

			Assert::IsTrue(
				model.OnShieldAcquired(key, false).empty());
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::FailedCovered);
			Assert::IsFalse(model.IsCovered());
			Assert::IsTrue(model.OnResetStarted(key).empty());
			Assert::IsTrue(model.OnFrameReady(key).empty());
		}

		TEST_METHOD(ResetFailureRemainsCoveredAndRequestsRebuild)
		{
			RendererTransitionModel model;
			const RendererTransitionKey key =
				AcquireAndStartReset(model);

			const auto actions = model.OnResetCompleted(key, false);
			AssertSingleAction(
				actions,
				RendererTransitionActionType::RequestRendererRebuild,
				key);
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::FailedCovered);
			Assert::IsTrue(model.IsCovered());
			Assert::IsTrue(model.OnFrameReady(key).empty());
		}

		TEST_METHOD(StaleResetAndFrameCompletionsAreIgnored)
		{
			RendererTransitionModel model;
			const RendererTransitionKey key =
				AcquireAndStartReset(model, 8, 6);
			RendererTransitionKey stale = key;
			++stale.rendererGeneration;

			Assert::IsTrue(
				model.OnResetCompleted(stale, true).empty());
			Assert::IsTrue(
				model.State() == RendererTransitionState::Resetting);
			Assert::IsTrue(model.OnFrameReady(stale).empty());

			model.OnResetCompleted(key, true);
			Assert::IsTrue(model.OnFrameReady(stale).empty());
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::AwaitingFrame);
		}

		TEST_METHOD(ResetDuringAwaitingFrameBlocksReveal)
		{
			RendererTransitionModel model;
			const RendererTransitionKey key =
				AcquireAndStartReset(model);
			model.OnResetCompleted(key, true);

			const auto reset = model.RequestAnotherReset(key);
			AssertSingleAction(
				reset,
				RendererTransitionActionType::ExecuteReset,
				key);
			Assert::IsTrue(
				model.State() == RendererTransitionState::BlackHeld);
			Assert::IsTrue(model.OnFrameReady(key).empty());
		}

		TEST_METHOD(FrameReadinessReleasesExactlyOnce)
		{
			RendererTransitionModel model;
			const RendererTransitionKey key =
				AcquireAndStartReset(model);
			model.OnResetCompleted(key, true);

			const auto first = model.OnFrameReady(key);
			AssertSingleAction(
				first,
				RendererTransitionActionType::ReleaseShield,
				key);
			Assert::IsTrue(model.OnFrameReady(key).empty());
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::ReleasingBlack);

			model.OnShieldReleased(key, true);
			Assert::IsTrue(
				model.State() == RendererTransitionState::Visible);
			Assert::IsTrue(model.OnShieldReleased(key, true).empty());
		}

		TEST_METHOD(StaleReleaseCannotUncoverCurrentTransition)
		{
			RendererTransitionModel model;
			const RendererTransitionKey key =
				AcquireAndStartReset(model);
			model.OnResetCompleted(key, true);
			model.OnFrameReady(key);

			RendererTransitionKey stale = key;
			++stale.targetRevision;
			Assert::IsTrue(
				model.OnShieldReleased(stale, true).empty());
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::ReleasingBlack);
		}

		TEST_METHOD(TargetReplacementDuringResetQueuesWithoutOverlappingReset)
		{
			RendererTransitionModel model;
			const RendererTransitionKey oldKey =
				AcquireAndStartReset(model, 10, 4);

			const auto queued = model.ReplaceCoveredTarget(11, 5);
			Assert::IsTrue(queued.empty());
			Assert::IsTrue(
				model.State() == RendererTransitionState::Resetting);
			Assert::IsTrue(model.Key() == oldKey);
			Assert::IsTrue(model.IsCovered());

			const auto rebind =
				model.OnResetCompleted(oldKey, true);
			const RendererTransitionKey newKey = model.Key();
			Assert::AreEqual<uint64_t>(
				oldKey.transitionToken, newKey.transitionToken);
			Assert::AreEqual<uint32_t>(
				11, newKey.rendererGeneration);
			Assert::AreEqual<uint64_t>(5, newKey.targetRevision);
			AssertSingleAction(
				rebind,
				RendererTransitionActionType::RebindShieldTarget,
				newKey);
			Assert::IsTrue(
				model.State() == RendererTransitionState::BlackHeld);
			Assert::IsTrue(model.IsCovered());
			Assert::IsTrue(
				model.OnResetCompleted(oldKey, true).empty());
			Assert::IsTrue(model.OnFrameReady(oldKey).empty());
			Assert::IsTrue(model.OnResetStarted(newKey).empty());

			const auto execute =
				model.OnShieldTargetRebound(newKey, true);
			AssertSingleAction(
				execute,
				RendererTransitionActionType::ExecuteReset,
				newKey);
		}

		TEST_METHOD(LatestQueuedTargetReplacementWins)
		{
			RendererTransitionModel model;
			const RendererTransitionKey oldKey =
				AcquireAndStartReset(model, 20, 8);

			Assert::IsTrue(
				model.ReplaceCoveredTarget(21, 9).empty());
			Assert::IsTrue(
				model.ReplaceCoveredTarget(22, 10).empty());

			const auto rebind =
				model.OnResetCompleted(oldKey, true);
			const RendererTransitionKey newKey = model.Key();
			Assert::AreEqual<uint32_t>(
				22, newKey.rendererGeneration);
			Assert::AreEqual<uint64_t>(
				10, newKey.targetRevision);
			AssertSingleAction(
				rebind,
				RendererTransitionActionType::RebindShieldTarget,
				newKey);
		}

		TEST_METHOD(InvalidStatesCannotReleaseShield)
		{
			RendererTransitionModel model;
			model.BeginReset(30, 1);
			const RendererTransitionKey key = model.Key();
			Assert::IsTrue(model.OnFrameReady(key).empty());

			model.OnShieldAcquired(key, true);
			Assert::IsTrue(model.OnFrameReady(key).empty());

			model.OnResetStarted(key);
			Assert::IsTrue(model.OnFrameReady(key).empty());

			model.OnResetCompleted(key, false);
			Assert::IsTrue(model.OnFrameReady(key).empty());
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::FailedCovered);
			Assert::IsTrue(model.IsCovered());
		}

		TEST_METHOD(TargetReplacementDuringReleaseIsRejected)
		{
			RendererTransitionModel model;
			const RendererTransitionKey key =
				AcquireAndStartReset(model, 40, 2);
			model.OnResetCompleted(key, true);
			model.OnFrameReady(key);

			Assert::IsTrue(
				model.ReplaceCoveredTarget(41, 3).empty());
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::ReleasingBlack);
			Assert::IsTrue(model.Key() == key);
		}

		TEST_METHOD(ReleaseFailureStaysCovered)
		{
			RendererTransitionModel model;
			const RendererTransitionKey key =
				AcquireAndStartReset(model);
			model.OnResetCompleted(key, true);
			model.OnFrameReady(key);

			const auto actions = model.OnShieldReleased(key, false);
			AssertSingleAction(
				actions,
				RendererTransitionActionType::RequestRendererRebuild,
				key);
			Assert::IsTrue(
				model.State() ==
				RendererTransitionState::FailedCovered);
		}
	};
}
