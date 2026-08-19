#include "pch.h"
#include "CppUnitTest.h"

#include <BoundedDeliveryGate.h>
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

		TEST_METHOD(RefreshTransitionReplacesDelayedDisplayFallback)
		{
			const int refreshPriority = RendererResetPriority(
				RendererResetReason::RefreshTransition);
			const int displayPriority = RendererResetPriority(
				RendererResetReason::DisplayTransition);
			Assert::IsTrue(RendererResetShouldReplace(
				refreshPriority, 5000,
				displayPriority, 35000));
		}

		TEST_METHOD(HostTransitionUsesTheSameQueueOnlyPriority)
		{
			Assert::AreEqual(
				RendererResetPriority(RendererResetReason::RefreshTransition),
				RendererResetPriority(RendererResetReason::HostTransition));
			Assert::IsTrue(RendererResetShouldReplace(
				RendererResetPriority(RendererResetReason::HostTransition), 5000,
				RendererResetPriority(RendererResetReason::DisplayTransition),
				35000));
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

		TEST_METHOD(QueuePolicyApplyUsesBackendOwnedSafeResetScope)
		{
			Assert::IsTrue(QueuePolicyApplyRequiresGraphReset(true));
			Assert::IsFalse(QueuePolicyApplyRequiresGraphReset(false));
		}

		TEST_METHOD(BlockedDeliveryCannotStarveGraphOwnerControl)
		{
			std::timed_mutex gate;
			std::atomic_bool gateHeld{false};
			std::thread delivery([&]()
				{
					std::lock_guard<std::timed_mutex> lock(gate);
					gateHeld.store(true, std::memory_order_release);
					std::this_thread::sleep_for(
						std::chrono::milliseconds(50));
				});
			while (!gateHeld.load(std::memory_order_acquire))
				std::this_thread::yield();

			bool executed = false;
			const auto started = std::chrono::steady_clock::now();
			const bool blockedResult = TryRunBoundedDeliveryTransaction(
				gate, std::chrono::milliseconds(5),
				[&]() { executed = true; });
			const auto elapsed = std::chrono::duration_cast<
				std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started);
			delivery.join();

			Assert::IsFalse(blockedResult);
			Assert::IsFalse(executed);
			Assert::IsTrue(elapsed < std::chrono::milliseconds(40));

			Assert::IsTrue(TryRunBoundedDeliveryTransaction(
				gate, std::chrono::milliseconds(5),
				[&]() { executed = true; }));
			Assert::IsTrue(executed);
		}

		TEST_METHOD(DynamicVideoAspectIsGeometryOnly)
		{
			Assert::AreEqual(
				static_cast<int>(DirectShowGraphEventImpact::GeometryOnly),
				static_cast<int>(ClassifyDirectShowGraphEvent(0x0e)));
			Assert::AreEqual(
				static_cast<int>(DirectShowGraphEventImpact::DisplayTransition),
				static_cast<int>(ClassifyDirectShowGraphEvent(0x16)));
			Assert::AreEqual(
				static_cast<int>(DirectShowGraphEventImpact::None),
				static_cast<int>(ClassifyDirectShowGraphEvent(0x12)));
		}

		TEST_METHOD(RapidRendererSelectionDefersAcrossLifecycleBoundaries)
		{
			Assert::AreEqual(
				static_cast<int>(
					RendererRestartDispatch::DeferUntilConstructionCompletes),
				static_cast<int>(ClassifyRendererRestartDispatch(true, false)));
			Assert::AreEqual(
				static_cast<int>(
					RendererRestartDispatch::DeferUntilConstructionCompletes),
				static_cast<int>(ClassifyRendererRestartDispatch(true, true)));
			Assert::AreEqual(
				static_cast<int>(
					RendererRestartDispatch::DeferUntilRetirementCompletes),
				static_cast<int>(ClassifyRendererRestartDispatch(false, true)));
			Assert::AreEqual(
				static_cast<int>(RendererRestartDispatch::DispatchNow),
				static_cast<int>(ClassifyRendererRestartDispatch(false, false)));
		}

		TEST_METHOD(DisplayTransitionOwnsConcurrentSourceGapRecovery)
		{
			Assert::IsTrue(
				RendererResetPriority(
					RendererResetReason::DisplayTransition) >
				RendererResetPriority(
					RendererResetReason::SourceGapRecovery));
		}

		TEST_METHOD(OutputReadinessPreemptsDisplayTransitionButNotCriticalRecovery)
		{
			const int outputReadinessPriority =
				RendererResetPriority(RendererResetReason::OutputReadiness);
			Assert::IsTrue(outputReadinessPriority >
				RendererResetPriority(RendererResetReason::DisplayTransition));
			Assert::IsTrue(outputReadinessPriority <
				RendererResetPriority(RendererResetReason::LivenessRecovery));
		}

		TEST_METHOD(ManualResetHasDeterministicTopPriority)
		{
			Assert::IsTrue(
				RendererResetPriority(RendererResetReason::Manual) >
				RendererResetPriority(
					RendererResetReason::LivenessRecovery));
		}

		TEST_METHOD(FullscreenRetargetConsumesTheFinalMatchingIntent)
		{
			// An entering retarget remains valid if repeated keyboard/API toggles
			// ultimately return to fullscreen before the first new frame arrives.
			Assert::IsFalse(FullscreenRetargetRequiresCoveredRebuild(
				false, true));
			Assert::IsTrue(FullscreenRetargetRequiresCoveredRebuild(
				false, false));
		}

		TEST_METHOD(FullscreenRetargetDefersOnlyAnOppositeFinalIntent)
		{
			// The symmetric exit case must keep its active target unless the final
			// requested state is fullscreen again.
			Assert::IsFalse(FullscreenRetargetRequiresCoveredRebuild(
				true, false));
			Assert::IsTrue(FullscreenRetargetRequiresCoveredRebuild(
				true, true));
		}

		TEST_METHOD(DirectShowToAlphaBackendHandoffIsDetected)
		{
			Assert::IsTrue(IsDirectShowToAlphaBackendHandoff(true, false));
			Assert::IsFalse(IsDirectShowToAlphaBackendHandoff(false, false));
			Assert::IsFalse(IsDirectShowToAlphaBackendHandoff(true, true));
		}

		TEST_METHOD(FreshAlphaQueueReprimesOnlyForRefreshTransition)
		{
			Assert::IsFalse(AlphaFreshStartRequiresDelayedReprime(
				AlphaFreshStartTransition::None));
			Assert::IsFalse(AlphaFreshStartRequiresDelayedReprime(
				AlphaFreshStartTransition::BackendHandoff));
			Assert::IsFalse(AlphaFreshStartRequiresDelayedReprime(
				AlphaFreshStartTransition::HostTransition));
			Assert::IsTrue(AlphaFreshStartRequiresDelayedReprime(
				AlphaFreshStartTransition::RefreshTransition));
		}
	};
}
