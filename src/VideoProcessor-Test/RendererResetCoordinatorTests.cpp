#include "pch.h"
#include "CppUnitTest.h"

#include <RendererResetCoordinator.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace Tests
{
	class FakeResetClock
	{
	public:
		uint64_t Now() const
		{
			return tick.load(std::memory_order_acquire);
		}

		void Set(uint64_t value)
		{
			tick.store(value, std::memory_order_release);
		}

	private:
		std::atomic<uint64_t> tick{100};
	};


	TEST_CLASS(RendererResetCoordinatorTests)
	{
	public:
		TEST_METHOD(BackendRequestIsReadyWithoutAnotherFrame)
		{
			FakeResetClock clock;
			std::atomic<uint64_t> wakeCount{0};
			RendererResetCoordinator coordinator(
				[&wakeCount]() { wakeCount.fetch_add(1); },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(7);

			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			request.scope = RendererResetScope::Graph;
			request.backendEpoch = 44;
			binding.sink->Submit(request);

			Assert::AreEqual(
				static_cast<unsigned long long>(1),
				static_cast<unsigned long long>(wakeCount.load()));
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			Assert::AreEqual(
				static_cast<unsigned int>(7),
				static_cast<unsigned int>(selected.rendererGeneration));
			Assert::AreEqual(
				static_cast<unsigned long long>(44),
				static_cast<unsigned long long>(
					selected.request.backendEpoch));
		}

		TEST_METHOD(StaleBindingTokenIsRejected)
		{
			FakeResetClock clock;
			std::atomic<uint64_t> wakeCount{0};
			RendererResetCoordinator coordinator(
				[&wakeCount]() { wakeCount.fetch_add(1); },
				[&clock]() { return clock.Now(); });
			const auto stale = coordinator.Bind(1);
			const auto current = coordinator.Bind(2);

			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			stale.sink->Submit(request);

			Assert::AreEqual(
				static_cast<unsigned long long>(0),
				static_cast<unsigned long long>(wakeCount.load()));
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsFalse(
				coordinator.DrainReady(clock.Now(), selected));
			const auto diagnostics = coordinator.GetDiagnostics();
			Assert::AreEqual(
				static_cast<unsigned long long>(1),
				static_cast<unsigned long long>(
					diagnostics.staleRequestCount));
			Assert::AreEqual(
				static_cast<unsigned long long>(current.token),
				static_cast<unsigned long long>(
					diagnostics.bindingToken));
		}

		TEST_METHOD(BurstCoalescesToOneOutstandingWake)
		{
			FakeResetClock clock;
			std::atomic<uint64_t> wakeCount{0};
			RendererResetCoordinator coordinator(
				[&wakeCount]() { wakeCount.fetch_add(1); },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(3);

			std::vector<std::thread> submitters;
			for (int index = 0; index < 8; ++index)
			{
				submitters.emplace_back([sink = binding.sink]()
				{
					RendererResetRequest request;
					request.reason = RendererResetReason::QueuePressure;
					request.scope = RendererResetScope::LiveQueue;
					sink->Submit(request);
				});
			}
			for (auto& submitter : submitters)
				submitter.join();

			Assert::AreEqual(
				static_cast<unsigned long long>(1),
				static_cast<unsigned long long>(wakeCount.load()));
			Assert::IsTrue(
				coordinator.GetDiagnostics().wakePosted);
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(
				coordinator.DrainReady(clock.Now(), selected));
			Assert::IsFalse(
				coordinator.GetDiagnostics().wakePosted);

			RendererResetRequest next;
			next.reason = RendererResetReason::QueuePressure;
			next.scope = RendererResetScope::LiveQueue;
			binding.sink->Submit(next);
			Assert::AreEqual(
				static_cast<unsigned long long>(2),
				static_cast<unsigned long long>(wakeCount.load()));
		}

		TEST_METHOD(HigherPriorityPreemptsAndScopeUpgrades)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() {},
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(9);

			RendererResetRequest delayedDisplay;
			delayedDisplay.reason = RendererResetReason::DisplayTransition;
			delayedDisplay.scope = RendererResetScope::LiveQueue;
			delayedDisplay.requestedTick = 100;
			delayedDisplay.deadlineTick = 500;
			binding.sink->Submit(delayedDisplay);

			RendererResetRequest recovery;
			recovery.reason = RendererResetReason::LivenessRecovery;
			recovery.scope = RendererResetScope::Graph;
			recovery.requestedTick = 100;
			recovery.deadlineTick = 100;
			binding.sink->Submit(recovery);

			const auto diagnostics = coordinator.GetDiagnostics();
			Assert::IsTrue(diagnostics.hasPending);
			Assert::IsTrue(
				diagnostics.pendingReason ==
				RendererResetReason::LivenessRecovery);
			Assert::IsTrue(
				diagnostics.pendingScope == RendererResetScope::Graph);
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(100, selected));
			Assert::IsTrue(
				selected.request.scope == RendererResetScope::Graph);
		}

		TEST_METHOD(LowerPriorityGraphRequestUpgradesSelectedScope)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() {},
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(5);

			RendererResetRequest manual;
			manual.reason = RendererResetReason::Manual;
			manual.scope = RendererResetScope::LiveQueue;
			binding.sink->Submit(manual);

			RendererResetRequest pressure;
			pressure.reason = RendererResetReason::QueuePressure;
			pressure.scope = RendererResetScope::Graph;
			binding.sink->Submit(pressure);

			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			Assert::IsTrue(
				selected.request.reason == RendererResetReason::Manual);
			Assert::IsTrue(
				selected.request.scope == RendererResetScope::Graph);
		}

		TEST_METHOD(DelayedUiRequestBlocksRevealUntilReadyAndDrained)
		{
			FakeResetClock clock;
			clock.Set(1000);
			RendererResetCoordinator coordinator(
				[]() {},
				[&clock]() { return clock.Now(); });
			coordinator.Bind(12);

			Assert::IsTrue(coordinator.RequestUi(
				RendererResetReason::PostRendererStart,
				RendererResetScope::Graph,
				250));
			Assert::IsTrue(coordinator.BlocksReveal(12));
			Assert::IsFalse(coordinator.BlocksReveal(11));
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsFalse(
				coordinator.DrainReady(1249, selected));
			Assert::IsTrue(coordinator.BlocksReveal(12));
			Assert::IsTrue(
				coordinator.DrainReady(1250, selected));
			Assert::IsFalse(coordinator.BlocksReveal(12));
		}

		TEST_METHOD(RevokeDropsPendingAndRetainedSinkIsHarmless)
		{
			FakeResetClock clock;
			std::atomic<uint64_t> wakeCount{0};
			RendererResetCoordinator coordinator(
				[&wakeCount]() { wakeCount.fetch_add(1); },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(20);
			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			binding.sink->Submit(request);
			coordinator.Revoke(binding.token);

			Assert::IsFalse(coordinator.BlocksReveal(20));
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsFalse(
				coordinator.DrainReady(clock.Now(), selected));
			binding.sink->Submit(request);
			Assert::AreEqual(
				static_cast<unsigned long long>(1),
				static_cast<unsigned long long>(wakeCount.load()));
		}
	};
}
