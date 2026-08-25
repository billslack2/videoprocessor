#include "pch.h"
#include "CppUnitTest.h"

#include <RendererResetCoordinator.h>
#include <RendererRetirementService.h>
#include <IRenderer.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
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

	class FakeResetRenderer : public IVideoRenderer
	{
	public:
		size_t GetConvertedQueueSize() override { return 0; }
		bool OnVideoState(VideoStateComPtr&) override { return true; }
		void OnVideoFrame(VideoFrame&) override {}
		HRESULT OnWindowsEvent(LONG_PTR, LONG_PTR) override { return S_OK; }
		void Build() override {}
		void Start() override {}
		void Stop() override {}
		void Reset() override {}
		void OnSize() override {}
		void OnPaint() override {}
		void SetFrameQueueMaxSize(size_t) override {}
		void SetSceneAwareTimingCorrection(bool) override {}
		size_t GetFrameQueueSize() override { return 0; }
		double EntryLatencyMs() const override { return 0; }
		double ExitLatencyMs() const override { return 0; }
		uint64_t DroppedFrameCount() const override { return 0; }

		void ResetWithIngressDrain(
			const std::function<void()>& drain) override
		{
			{
				std::lock_guard<std::mutex> lock(mutex);
				graphStopped = true;
				enteredDrain = true;
			}
			changed.notify_all();
			drain();
			{
				std::lock_guard<std::mutex> lock(mutex);
				drainReturned = true;
			}
			changed.notify_all();
			if (fail)
				throw std::runtime_error("fake graph failure");
		}

		void ResetLiveQueue() override
		{
			std::lock_guard<std::mutex> lock(mutex);
			liveResetCalled = true;
			if (fail)
				throw std::runtime_error("fake live failure");
		}

		bool RetargetWindowWithIngressDrain(
			uintptr_t targetWindow,
			const std::function<void()>& drain) override
		{
			{
				std::lock_guard<std::mutex> lock(mutex);
				graphStopped = true;
				enteredDrain = true;
				retargetWindow = targetWindow;
			}
			changed.notify_all();
			drain();
			{
				std::lock_guard<std::mutex> lock(mutex);
				drainReturned = true;
			}
			changed.notify_all();
			if (fail)
				throw std::runtime_error("fake retarget failure");
			return true;
		}

		bool WaitForDrainEntry()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return changed.wait_for(lock, std::chrono::seconds(2),
				[this]() { return enteredDrain; });
		}

		bool WaitForDrainReturn()
		{
			std::unique_lock<std::mutex> lock(mutex);
			return changed.wait_for(lock, std::chrono::seconds(2),
				[this]() { return drainReturned; });
		}

		std::mutex mutex;
		std::condition_variable changed;
		bool graphStopped = false;
		bool enteredDrain = false;
		bool drainReturned = false;
		bool liveResetCalled = false;
		uintptr_t retargetWindow = 0;
		bool fail = false;
	};

	class BlockingRetirementRenderer final : public FakeResetRenderer
	{
	public:
		void Retire() noexcept override
		{
			retireThread.store(
				GetCurrentThreadId(), std::memory_order_release);
			retireEntered.set_value();
			releaseRetire.get_future().wait();
			retired.store(true, std::memory_order_release);
		}

		~BlockingRetirementRenderer() override
		{
			destructorThread.store(
				GetCurrentThreadId(), std::memory_order_release);
		}

		std::promise<void> retireEntered;
		std::promise<void> releaseRetire;
		std::atomic<DWORD> retireThread{0};
		std::atomic<DWORD> destructorThread{0};
		std::atomic_bool retired{false};
	};

	class RetriedRetirementRenderer final : public FakeResetRenderer
	{
	public:
		void Retire() noexcept override
		{
			attempts.fetch_add(1, std::memory_order_acq_rel);
		}

		bool RetirementSucceeded() const override
		{
			return attempts.load(std::memory_order_acquire) >= 2;
		}

		std::atomic<unsigned int> attempts{0};
	};

	bool WaitForCompletion(RendererResetCoordinator& coordinator)
	{
		for (int attempt = 0; attempt < 200; ++attempt)
		{
			if (coordinator.GetDiagnostics().completionPending)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}


	TEST_CLASS(RendererResetCoordinatorTests)
	{
	public:
		TEST_METHOD(GraphRetargetCarriesTargetAndPreservesIngressBarrier)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			coordinator.Bind(18);
			coordinator.GetIngressState()->OpenAdmission();
			auto lease = coordinator.GetIngressState()->TryAcquire();
			Assert::IsTrue(static_cast<bool>(lease));

			const uintptr_t targetWindow = 0x12345678;
			Assert::IsTrue(coordinator.RequestUi(
				RendererResetReason::DisplayTransition,
				RendererResetScope::GraphRetarget,
				0, 0, targetWindow));
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(
				clock.Now(), selected));
			Assert::IsTrue(
				selected.request.scope ==
					RendererResetScope::GraphRetarget);
			Assert::AreEqual<uint64_t>(
				static_cast<uint64_t>(targetWindow),
				static_cast<uint64_t>(
					selected.request.targetWindow));

			auto renderer = std::make_shared<FakeResetRenderer>();
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(
					selected, renderer) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(renderer->WaitForDrainEntry());
			Assert::AreEqual<uint64_t>(
				static_cast<uint64_t>(targetWindow),
				static_cast<uint64_t>(
					renderer->retargetWindow));
			lease.Release();
			Assert::IsTrue(renderer->WaitForDrainReturn());
			Assert::IsTrue(WaitForCompletion(coordinator));
			RendererResetCoordinator::OperationResult result;
			Assert::IsTrue(
				coordinator.ConsumeCompletion(18, true, result));
			Assert::IsTrue(result.succeeded);
			Assert::IsTrue(result.ingressReopened);

			// A fullscreen retarget is followed by a separately delayed,
			// LiveQueue-only re-prime.  Prove that the coordinator accepts the
			// second phase after the retarget completes and honors its deadline.
			Assert::IsTrue(coordinator.RequestUi(
				RendererResetReason::DisplayTransition,
				RendererResetScope::LiveQueue, 5000));
			Assert::IsFalse(coordinator.DrainReady(5099, selected));
			Assert::IsTrue(coordinator.DrainReady(5100, selected));
			Assert::IsTrue(
				selected.request.scope == RendererResetScope::LiveQueue);
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(selected, renderer) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(WaitForCompletion(coordinator));
			Assert::IsTrue(
				coordinator.ConsumeCompletion(18, true, result));
			Assert::IsTrue(result.succeeded);
			Assert::IsTrue(renderer->liveResetCalled);
		}

		TEST_METHOD(GraphRetargetSubsumesGraphRequestsInEitherArrivalOrder)
		{
			auto verifyOrder = [](bool retargetFirst)
				{
					FakeResetClock clock;
					RendererResetCoordinator coordinator(
						[]() { return true; },
						[&clock]() { return clock.Now(); });
					coordinator.Bind(retargetFirst ? 19 : 20);
					const uintptr_t targetWindow =
						retargetFirst ? 0x11112222 : 0x33334444;
					if (retargetFirst)
					{
						Assert::IsTrue(coordinator.RequestUi(
							RendererResetReason::DisplayTransition,
							RendererResetScope::GraphRetarget,
							0, 0, targetWindow));
						Assert::IsTrue(coordinator.RequestUi(
							RendererResetReason::LivenessRecovery,
							RendererResetScope::Graph));
					}
					else
					{
						Assert::IsTrue(coordinator.RequestUi(
							RendererResetReason::LivenessRecovery,
							RendererResetScope::Graph));
						Assert::IsTrue(coordinator.RequestUi(
							RendererResetReason::DisplayTransition,
							RendererResetScope::GraphRetarget,
							0, 0, targetWindow));
					}
					RendererResetCoordinator::SelectedReset selected;
					Assert::IsTrue(
						coordinator.DrainReady(clock.Now(), selected));
					Assert::IsTrue(
						selected.request.scope ==
							RendererResetScope::GraphRetarget);
					Assert::AreEqual<uint64_t>(
						static_cast<uint64_t>(targetWindow),
						static_cast<uint64_t>(
							selected.request.targetWindow));
				};

			verifyOrder(true);
			verifyOrder(false);
		}

		TEST_METHOD(LatestRetargetTargetWinsBeforeSelection)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			coordinator.Bind(21);
			Assert::IsTrue(coordinator.RequestUi(
				RendererResetReason::DisplayTransition,
				RendererResetScope::GraphRetarget,
				0, 0, 0x1111));
			Assert::IsTrue(coordinator.RequestUi(
				RendererResetReason::DisplayTransition,
				RendererResetScope::GraphRetarget,
				0, 0, 0x2222));
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			Assert::AreEqual<uint64_t>(
				0x2222,
				static_cast<uint64_t>(
					selected.request.targetWindow));
		}

		TEST_METHOD(RetargetFailureRequiresCoveredRestartAndKeepsIngressClosed)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			coordinator.Bind(22);
			coordinator.GetIngressState()->OpenAdmission();
			Assert::IsTrue(coordinator.RequestUi(
				RendererResetReason::DisplayTransition,
				RendererResetScope::GraphRetarget,
				0, 0, 0x1234));
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			auto renderer = std::make_shared<FakeResetRenderer>();
			renderer->fail = true;
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(
					selected, renderer) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(WaitForCompletion(coordinator));
			RendererResetCoordinator::OperationResult result;
			Assert::IsTrue(
				coordinator.ConsumeCompletion(22, true, result));
			Assert::IsFalse(result.succeeded);
			Assert::IsTrue(result.restartRequired);
			Assert::IsFalse(result.ingressReopened);
			Assert::IsFalse(
				coordinator.GetIngressState()->IsAdmitting());
		}

		TEST_METHOD(FirstFrameCanAcceleratePendingPostStartReset)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			coordinator.Bind(17);

			Assert::IsTrue(coordinator.RequestUi(
				RendererResetReason::PostRendererStart,
				RendererResetScope::Graph, 3000));
			RendererResetCoordinator::Diagnostics diagnostics =
				coordinator.GetDiagnostics();
			Assert::IsTrue(diagnostics.hasPending);
			Assert::AreEqual<uint64_t>(
				3100, diagnostics.pendingDeadlineTick);

			Assert::IsTrue(coordinator.RequestUi(
				RendererResetReason::PostRendererStart,
				RendererResetScope::Graph, 0));
			diagnostics = coordinator.GetDiagnostics();
			Assert::AreEqual<uint64_t>(
				100, diagnostics.pendingDeadlineTick);
			Assert::IsTrue(
				diagnostics.pendingScope ==
					RendererResetScope::Graph);

			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(
				clock.Now(), selected));
			Assert::IsTrue(
				selected.request.reason ==
					RendererResetReason::PostRendererStart);
			Assert::IsTrue(
				selected.request.scope ==
					RendererResetScope::Graph);
		}

		TEST_METHOD(RendererRetirementNeverBlocksUiAndExplicitlyRetiresPinnedObject)
		{
			RendererRetirementService service;
			auto renderer =
				std::make_shared<BlockingRetirementRenderer>();
			auto transientUiPin = renderer;
			std::future<void> entered =
				renderer->retireEntered.get_future();
			const DWORD uiThread = GetCurrentThreadId();

			Assert::IsTrue(service.Retire(
				renderer, 41, nullptr, WM_APP + 90));
			renderer.reset();
			Assert::IsTrue(entered.wait_for(
				std::chrono::seconds(2)) == std::future_status::ready);
			Assert::IsFalse(service.IsIdle());
			Assert::AreNotEqual(uiThread,
				transientUiPin->retireThread.load(
					std::memory_order_acquire));

			transientUiPin->releaseRetire.set_value();
			for (int attempt = 0;
				attempt < 200 && !service.IsIdle(); ++attempt)
			{
				std::this_thread::sleep_for(
					std::chrono::milliseconds(5));
			}
			Assert::IsTrue(service.IsIdle());
			Assert::IsTrue(transientUiPin->retired.load(
				std::memory_order_acquire));

			// The explicit worker retirement is already terminal. A transient
			// callback pin can release later on the UI without owning shutdown.
			transientUiPin.reset();
			service.RequestClose();
			service.Join();
		}

		TEST_METHOD(RendererRetirementCompletionSurvivesLostWindowWake)
		{
			RendererRetirementService service;
			auto renderer =
				std::make_shared<BlockingRetirementRenderer>();
			std::future<void> entered =
				renderer->retireEntered.get_future();

			// An invalid completion window guarantees that the best-effort wake
			// cannot be delivered. The completion itself must remain available
			// for the UI timer/state reconciliation paths.
			const HWND invalidWindow = reinterpret_cast<HWND>(
				static_cast<ULONG_PTR>(0x1234));
			Assert::IsTrue(service.Retire(
				renderer, 73, invalidWindow, WM_APP + 91));
			Assert::IsTrue(entered.wait_for(
				std::chrono::seconds(2)) == std::future_status::ready);
			renderer->releaseRetire.set_value();

			RendererRetirementService::Completion completion;
			for (int attempt = 0; attempt < 200; ++attempt)
			{
				if (service.TryTakeCompletion(73, completion))
					break;
				std::this_thread::sleep_for(
					std::chrono::milliseconds(5));
			}
			Assert::AreEqual(
				static_cast<unsigned long long>(73),
				static_cast<unsigned long long>(completion.token));
			Assert::IsTrue(completion.succeeded);
			Assert::IsFalse(completion.wakePosted);
			Assert::AreNotEqual(
				static_cast<unsigned long>(ERROR_SUCCESS),
				static_cast<unsigned long>(completion.wakePostError));

			service.RequestClose();
			service.Join();
		}

		TEST_METHOD(RendererRetirementCompletionSurvivesAggressivePolling)
		{
			RendererRetirementService service;
			auto renderer = std::make_shared<BlockingRetirementRenderer>();
			BlockingRetirementRenderer* rendererLifetime = renderer.get();
			std::future<void> entered =
				renderer->retireEntered.get_future();
			Assert::IsTrue(service.Retire(
				renderer, 74, nullptr, WM_APP + 92));
			renderer.reset();
			Assert::IsTrue(entered.wait_for(
				std::chrono::seconds(2)) == std::future_status::ready);

			std::promise<RendererRetirementService::Completion> published;
			std::future<RendererRetirementService::Completion> completion =
				published.get_future();
			std::thread poller([&service, &published]()
				{
					RendererRetirementService::Completion value;
					while (!service.TryTakeCompletion(74, value))
						std::this_thread::yield();
					published.set_value(value);
				});
			rendererLifetime->releaseRetire.set_value();
			Assert::IsTrue(completion.wait_for(
				std::chrono::seconds(2)) == std::future_status::ready);
			Assert::IsTrue(completion.get().succeeded);
			poller.join();
			service.RequestClose();
			service.Join();
		}

		TEST_METHOD(RendererRetirementRetainsObjectForAsynchronousRestoreRetry)
		{
			RendererRetirementService service;
			auto renderer = std::make_shared<RetriedRetirementRenderer>();
			Assert::IsTrue(service.Retire(
				renderer, 75, nullptr, WM_APP + 93));

			RendererRetirementService::Completion first;
			for (int attempt = 0; attempt < 200; ++attempt)
			{
				if (service.TryTakeCompletion(75, first))
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			Assert::AreEqual(
				static_cast<unsigned long long>(75),
				static_cast<unsigned long long>(first.token));
			Assert::IsFalse(first.succeeded);
			Assert::IsNotNull(first.renderer.get());
			Assert::AreEqual(
				static_cast<unsigned int>(1), renderer->attempts.load());

			Assert::IsTrue(service.Retire(
				std::move(first.renderer), 76, nullptr, WM_APP + 94));
			RendererRetirementService::Completion second;
			for (int attempt = 0; attempt < 200; ++attempt)
			{
				if (service.TryTakeCompletion(76, second))
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			Assert::AreEqual(
				static_cast<unsigned long long>(76),
				static_cast<unsigned long long>(second.token));
			Assert::IsTrue(second.succeeded);
			Assert::IsNull(second.renderer.get());
			Assert::AreEqual(
				static_cast<unsigned int>(2), renderer->attempts.load());

			service.RequestClose();
			service.Join();
		}

		TEST_METHOD(BackendRequestIsReadyWithoutAnotherFrame)
		{
			FakeResetClock clock;
			std::atomic<uint64_t> wakeCount{0};
			RendererResetCoordinator coordinator(
				[&wakeCount]() { wakeCount.fetch_add(1); return true; },
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
				[&wakeCount]() { wakeCount.fetch_add(1); return true; },
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
				[&wakeCount]() { wakeCount.fetch_add(1); return true; },
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
				[]() { return true; },
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
				[]() { return true; },
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

		TEST_METHOD(ProfileThenReadinessCoalescesWithoutChangingPriority)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			coordinator.Bind(50);

			const auto profile = coordinator.RequestUiWithReceipt(
				RendererResetReason::Manual,
				RendererResetScope::Graph,
				8000, 0, 0,
				RendererResetOrigin::AutomaticProfile, 14);
			Assert::IsTrue(profile.accepted);
			Assert::IsTrue(profile.disposition ==
				RendererResetCoordinator::SubmissionDisposition::Selected);
			Assert::AreEqual<uint64_t>(
				profile.requestSequence, profile.selectedSequence);

			clock.Set(6100);
			const auto readiness = coordinator.RequestUiWithReceipt(
				RendererResetReason::OutputReadiness,
				RendererResetScope::Graph,
				0, 0, 0,
				RendererResetOrigin::AutomaticReadiness, 0x1234);
			Assert::IsTrue(readiness.accepted);
			Assert::IsTrue(readiness.disposition ==
				RendererResetCoordinator::SubmissionDisposition::Coalesced);
			Assert::AreEqual<uint64_t>(
				profile.requestSequence, readiness.selectedSequence);
			Assert::IsTrue(readiness.selectedReason ==
				RendererResetReason::Manual);
			Assert::IsTrue(readiness.selectedOrigin ==
				RendererResetOrigin::AutomaticProfile);
			Assert::AreEqual<uint64_t>(14,
				readiness.selectedOriginGeneration);
			Assert::AreEqual<uint64_t>(8100,
				readiness.selectedDeadlineTick);
			Assert::IsTrue(RendererResetHasOrigin(
				readiness.selectedOriginContributors,
				RendererResetOrigin::AutomaticProfile));
			Assert::IsTrue(RendererResetHasOrigin(
				readiness.selectedOriginContributors,
				RendererResetOrigin::AutomaticReadiness));
			const RendererResetCoordinator::Diagnostics pendingDiagnostics =
				coordinator.GetDiagnostics();
			Assert::IsTrue(pendingDiagnostics.hasPending);
			Assert::IsFalse(pendingDiagnostics.selectionPrepared);
			Assert::IsFalse(pendingDiagnostics.operationActive);

			RendererResetCoordinator::SelectedReset selected;
			Assert::IsFalse(coordinator.DrainReady(8099, selected));
			Assert::IsTrue(coordinator.DrainReady(8100, selected));
			Assert::IsTrue(coordinator.GetDiagnostics().selectionPrepared);
			Assert::IsFalse(coordinator.GetDiagnostics().operationActive);
			Assert::IsTrue(selected.request.origin ==
				RendererResetOrigin::AutomaticProfile);
			Assert::IsTrue(RendererResetHasOrigin(
				selected.request.originContributors,
				RendererResetOrigin::AutomaticReadiness));
			Assert::IsTrue(coordinator.AcknowledgeBlackAndStart(
				selected, std::make_shared<FakeResetRenderer>()) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(coordinator.GetDiagnostics().operationActive);
			Assert::IsTrue(WaitForCompletion(coordinator));
			RendererResetCoordinator::OperationResult result;
			Assert::IsTrue(coordinator.ConsumeCompletion(50, true, result));
			Assert::IsTrue(result.succeeded);
			Assert::IsTrue(RendererResetHasOrigin(
				result.request.originContributors,
				RendererResetOrigin::AutomaticProfile));
			Assert::IsTrue(RendererResetHasOrigin(
				result.request.originContributors,
				RendererResetOrigin::AutomaticReadiness));
		}

		TEST_METHOD(ProfileReplacesReadinessAndRetainsBothContributors)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			coordinator.Bind(51);

			const auto readiness = coordinator.RequestUiWithReceipt(
				RendererResetReason::OutputReadiness,
				RendererResetScope::Graph,
				0, 0, 0,
				RendererResetOrigin::AutomaticReadiness, 31);
			const auto profile = coordinator.RequestUiWithReceipt(
				RendererResetReason::Manual,
				RendererResetScope::Graph,
				8000, 0, 0,
				RendererResetOrigin::AutomaticProfile, 15);
			Assert::IsTrue(readiness.accepted && profile.accepted);
			Assert::IsTrue(profile.disposition ==
				RendererResetCoordinator::SubmissionDisposition::Replaced);
			Assert::AreEqual<uint64_t>(
				profile.requestSequence, profile.selectedSequence);
			Assert::IsTrue(profile.selectedOrigin ==
				RendererResetOrigin::AutomaticProfile);
			Assert::IsTrue(RendererResetHasOrigin(
				profile.selectedOriginContributors,
				RendererResetOrigin::AutomaticReadiness));
			Assert::IsTrue(RendererResetHasOrigin(
				profile.selectedOriginContributors,
				RendererResetOrigin::AutomaticProfile));

			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(8100, selected));
			Assert::IsTrue(selected.request.reason ==
				RendererResetReason::Manual);
			Assert::AreEqual<uint64_t>(15,
				selected.request.originGeneration);
		}

		TEST_METHOD(ImmediateUserManualReplacesDelayedAutomaticProfile)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			coordinator.Bind(52);

			const auto profile = coordinator.RequestUiWithReceipt(
				RendererResetReason::Manual,
				RendererResetScope::Graph,
				8000, 0, 0,
				RendererResetOrigin::AutomaticProfile, 16);
			clock.Set(200);
			const auto user = coordinator.RequestUiWithReceipt(
				RendererResetReason::Manual,
				RendererResetScope::Graph,
				0, 0, 0,
				RendererResetOrigin::UserManual, 0);
			Assert::IsTrue(profile.accepted && user.accepted);
			Assert::IsTrue(user.disposition ==
				RendererResetCoordinator::SubmissionDisposition::Replaced);
			Assert::AreEqual<uint64_t>(
				user.requestSequence, user.selectedSequence);
			Assert::IsTrue(user.selectedReason ==
				RendererResetReason::Manual);
			Assert::IsTrue(user.selectedOrigin ==
				RendererResetOrigin::UserManual);
			Assert::AreEqual<uint64_t>(200, user.selectedDeadlineTick);
			Assert::IsTrue(RendererResetHasOrigin(
				user.selectedOriginContributors,
				RendererResetOrigin::AutomaticProfile));
			Assert::IsTrue(RendererResetHasOrigin(
				user.selectedOriginContributors,
				RendererResetOrigin::UserManual));
		}

		TEST_METHOD(EqualPriorityAndDeadlineKeepsFirstOrigin)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			coordinator.Bind(53);

			const auto profile = coordinator.RequestUiWithReceipt(
				RendererResetReason::Manual,
				RendererResetScope::Graph,
				0, 0, 0,
				RendererResetOrigin::AutomaticProfile, 17);
			const auto user = coordinator.RequestUiWithReceipt(
				RendererResetReason::Manual,
				RendererResetScope::Graph,
				0, 0, 0,
				RendererResetOrigin::UserManual, 0);
			Assert::IsTrue(profile.accepted && user.accepted);
			Assert::IsTrue(user.disposition ==
				RendererResetCoordinator::SubmissionDisposition::Coalesced);
			Assert::AreEqual<uint64_t>(
				profile.requestSequence, user.selectedSequence);
			Assert::IsTrue(user.selectedOrigin ==
				RendererResetOrigin::AutomaticProfile);
			Assert::IsTrue(RendererResetHasOrigin(
				user.selectedOriginContributors,
				RendererResetOrigin::UserManual));
		}

		TEST_METHOD(DelayedUiRequestBlocksRevealUntilReadyAndDrained)
		{
			FakeResetClock clock;
			clock.Set(1000);
			RendererResetCoordinator coordinator(
				[]() { return true; },
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
			Assert::IsTrue(coordinator.BlocksReveal(12));
			Assert::IsTrue(coordinator.RejectBlackAndRequireRestart(
				selected, "test cover unavailable"));
		}

		TEST_METHOD(RevokeDropsPendingAndRetainedSinkIsHarmless)
		{
			FakeResetClock clock;
			std::atomic<uint64_t> wakeCount{0};
			RendererResetCoordinator coordinator(
				[&wakeCount]() { wakeCount.fetch_add(1); return true; },
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

		TEST_METHOD(GraphResetStopsBeforeWaitingForIngressDrain)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(30);
			coordinator.GetIngressState()->OpenAdmission();
			auto lease = coordinator.GetIngressState()->TryAcquire();
			Assert::IsTrue(static_cast<bool>(lease));

			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			request.scope = RendererResetScope::Graph;
			binding.sink->Submit(request);
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			auto renderer = std::make_shared<FakeResetRenderer>();
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(selected, renderer) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(renderer->WaitForDrainEntry());
			{
				std::lock_guard<std::mutex> lock(renderer->mutex);
				Assert::IsTrue(renderer->graphStopped);
				Assert::IsFalse(renderer->drainReturned);
			}
			lease.Release();
			Assert::IsTrue(renderer->WaitForDrainReturn());
			Assert::IsTrue(WaitForCompletion(coordinator));
			RendererResetCoordinator::OperationResult result;
			Assert::IsTrue(coordinator.ConsumeCompletion(30, true, result));
			Assert::IsTrue(result.succeeded);
			Assert::IsTrue(result.ingressReopened);
		}

		TEST_METHOD(OnlyOneOperationRunsAndRequestsDuringItStayPending)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(31);
			coordinator.GetIngressState()->OpenAdmission();
			auto lease = coordinator.GetIngressState()->TryAcquire();
			auto firstRenderer = std::make_shared<FakeResetRenderer>();

			RendererResetRequest first;
			first.reason = RendererResetReason::LivenessRecovery;
			first.scope = RendererResetScope::Graph;
			binding.sink->Submit(first);
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(
					selected, firstRenderer) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(firstRenderer->WaitForDrainEntry());

			RendererResetRequest second;
			second.reason = RendererResetReason::QueuePressure;
			second.scope = RendererResetScope::LiveQueue;
			binding.sink->Submit(second);
			RendererResetCoordinator::SelectedReset blocked;
			Assert::IsFalse(coordinator.DrainReady(clock.Now(), blocked));
			Assert::IsTrue(coordinator.GetDiagnostics().hasPending);

			lease.Release();
			Assert::IsTrue(WaitForCompletion(coordinator));
			RendererResetCoordinator::OperationResult result;
			Assert::IsTrue(coordinator.ConsumeCompletion(31, true, result));
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), blocked));
		}

		TEST_METHOD(FailureStaysCoveredAndRequiresRestart)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(32);
			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			request.scope = RendererResetScope::Graph;
			request.origin = RendererResetOrigin::AutomaticProfile;
			request.originGeneration = 55;
			binding.sink->Submit(request);
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			auto renderer = std::make_shared<FakeResetRenderer>();
			renderer->fail = true;
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(selected, renderer) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(WaitForCompletion(coordinator));
			RendererResetCoordinator::OperationResult result;
			Assert::IsTrue(coordinator.ConsumeCompletion(32, true, result));
			Assert::IsFalse(result.succeeded);
			Assert::IsTrue(result.restartRequired);
			Assert::IsFalse(result.ingressReopened);
			Assert::IsTrue(result.request.origin ==
				RendererResetOrigin::AutomaticProfile);
			Assert::AreEqual<uint64_t>(55,
				result.request.originGeneration);
			Assert::IsTrue(RendererResetHasOrigin(
				result.request.originContributors,
				RendererResetOrigin::AutomaticProfile));
			Assert::IsTrue(coordinator.BlocksReveal(32));
			coordinator.Revoke(binding.token);
			Assert::IsFalse(coordinator.BlocksReveal(32));
		}

		TEST_METHOD(StaleGenerationCompletionDoesNotReopenIngress)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(33);
			RendererResetRequest request;
			request.scope = RendererResetScope::LiveQueue;
			request.reason = RendererResetReason::QueuePressure;
			request.origin = RendererResetOrigin::AutomaticReadiness;
			request.originGeneration = 66;
			binding.sink->Submit(request);
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(
					selected, std::make_shared<FakeResetRenderer>()) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(WaitForCompletion(coordinator));
			RendererResetCoordinator::OperationResult result;
			Assert::IsTrue(coordinator.ConsumeCompletion(34, true, result));
			Assert::IsTrue(result.staleGeneration);
			Assert::IsTrue(result.restartRequired);
			Assert::IsTrue(result.request.origin ==
				RendererResetOrigin::AutomaticReadiness);
			Assert::AreEqual<uint64_t>(66,
				result.request.originGeneration);
			Assert::IsTrue(RendererResetHasOrigin(
				result.request.originContributors,
				RendererResetOrigin::AutomaticReadiness));
			Assert::IsFalse(
				coordinator.GetIngressState()->IsAdmitting());
		}

		TEST_METHOD(CloseJoinsWorkerAndRetainedSinkCannotRestartIt)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(35);
			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			binding.sink->Submit(request);
			coordinator.Close();
			coordinator.Join();
			binding.sink->Submit(request);
			Assert::IsTrue(coordinator.GetDiagnostics().closed);
			Assert::IsFalse(coordinator.GetDiagnostics().hasPending);
		}

		TEST_METHOD(FailedWakeIsRetriedWithoutAnotherRequest)
		{
			FakeResetClock clock;
			std::atomic<int> attempts{0};
			RendererResetCoordinator coordinator(
				[&attempts]()
				{
					return attempts.fetch_add(1) > 0;
				},
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(36);
			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			binding.sink->Submit(request);

			for (int wait = 0;
				wait < 200 && attempts.load() < 2;
				++wait)
			{
				std::this_thread::sleep_for(
					std::chrono::milliseconds(5));
			}
			Assert::IsTrue(attempts.load() >= 2);
			Assert::IsTrue(coordinator.GetDiagnostics().wakePosted);
		}

		TEST_METHOD(IngressRemainsClosedUntilHostDeclaresRenderingReady)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			Assert::IsFalse(
				coordinator.GetIngressState()->IsAdmitting());
			Assert::IsFalse(static_cast<bool>(
				coordinator.GetIngressState()->TryAcquire()));

			coordinator.Bind(40);
			Assert::IsFalse(
				coordinator.GetIngressState()->IsAdmitting());
			Assert::IsFalse(static_cast<bool>(
				coordinator.GetIngressState()->TryAcquire()));

			coordinator.GetIngressState()->OpenAdmission();
			Assert::IsTrue(static_cast<bool>(
				coordinator.GetIngressState()->TryAcquire()));
		}

		TEST_METHOD(CompletionDefersLifecycleAndPreservesTransitionIdentity)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(41);
			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			request.scope = RendererResetScope::Graph;
			binding.sink->Submit(request);

			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			selected.transitionToken = 0x1122334455667788ULL;
			selected.targetRevision = 0x8877665544332211ULL;
			Assert::IsTrue(coordinator.RequiresLifecycleDeferral());
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(
					selected, std::make_shared<FakeResetRenderer>()) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(WaitForCompletion(coordinator));

			// Worker completion is not permission to tear down/rebind. The
			// host must first consume the identity-bearing completion.
			Assert::IsTrue(coordinator.RequiresLifecycleDeferral());
			RendererResetCoordinator::OperationResult result;
			Assert::IsTrue(coordinator.ConsumeCompletion(
				41, true, result,
				selected.transitionToken,
				selected.targetRevision,
				true));
			Assert::AreEqual(
				static_cast<unsigned long long>(
					selected.transitionToken),
				static_cast<unsigned long long>(
					result.transitionToken));
			Assert::AreEqual(
				static_cast<unsigned long long>(
					selected.targetRevision),
				static_cast<unsigned long long>(
					result.targetRevision));
			Assert::IsFalse(coordinator.RequiresLifecycleDeferral());
		}

		TEST_METHOD(CompletionIdentityAndTransitionReadinessGateIngress)
		{
			const auto runCase = [](
				uint64_t expectedToken,
				uint64_t expectedRevision,
				bool transitionReady,
				bool expectReopen)
				{
					FakeResetClock clock;
					RendererResetCoordinator coordinator(
						[]() { return true; },
						[&clock]() { return clock.Now(); });
					const auto binding = coordinator.Bind(42);
					RendererResetRequest request;
					request.reason =
						RendererResetReason::LivenessRecovery;
					request.scope = RendererResetScope::Graph;
					binding.sink->Submit(request);
					RendererResetCoordinator::SelectedReset selected;
					Assert::IsTrue(
						coordinator.DrainReady(clock.Now(), selected));
					selected.transitionToken = 101;
					selected.targetRevision = 202;
					Assert::IsTrue(
						coordinator.AcknowledgeBlackAndStart(
							selected,
							std::make_shared<FakeResetRenderer>()) ==
						RendererResetCoordinator::StartResult::Started);
					Assert::IsTrue(WaitForCompletion(coordinator));
					RendererResetCoordinator::OperationResult result;
					Assert::IsTrue(coordinator.ConsumeCompletion(
						42, true, result,
						expectedToken, expectedRevision,
						transitionReady));
					Assert::AreEqual(expectReopen,
						result.ingressReopened);
					Assert::AreEqual(!expectReopen,
						result.restartRequired);
					Assert::AreEqual(expectReopen,
						coordinator.GetIngressState()->IsAdmitting());
				};

			runCase(999, 202, true, false);  // mismatched token
			runCase(101, 999, true, false);  // mismatched revision
			runCase(101, 202, false, false); // model not ready
			runCase(101, 202, true, true);   // exact current identity
		}

		TEST_METHOD(CloseJoinsOperationAfterBlockedIngressLeaseReleases)
		{
			FakeResetClock clock;
			RendererResetCoordinator coordinator(
				[]() { return true; },
				[&clock]() { return clock.Now(); });
			const auto binding = coordinator.Bind(43);
			coordinator.GetIngressState()->OpenAdmission();
			auto lease = coordinator.GetIngressState()->TryAcquire();
			Assert::IsTrue(static_cast<bool>(lease));

			RendererResetRequest request;
			request.reason = RendererResetReason::LivenessRecovery;
			request.scope = RendererResetScope::Graph;
			binding.sink->Submit(request);
			RendererResetCoordinator::SelectedReset selected;
			Assert::IsTrue(coordinator.DrainReady(clock.Now(), selected));
			auto renderer = std::make_shared<FakeResetRenderer>();
			Assert::IsTrue(
				coordinator.AcknowledgeBlackAndStart(
					selected, renderer) ==
				RendererResetCoordinator::StartResult::Started);
			Assert::IsTrue(renderer->WaitForDrainEntry());

			coordinator.Close();
			std::atomic_bool joined{false};
			std::thread joiner([&coordinator, &joined]()
				{
					coordinator.Join();
					joined.store(true, std::memory_order_release);
				});
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
			Assert::IsFalse(joined.load(std::memory_order_acquire));
			lease.Release();
			joiner.join();
			Assert::IsTrue(joined.load(std::memory_order_acquire));
			Assert::IsTrue(renderer->WaitForDrainReturn());
			binding.sink->Submit(request);
			Assert::IsFalse(coordinator.GetDiagnostics().hasPending);
		}

		TEST_METHOD(ReadinessPreservesMeasurementForGraphButNotRetarget)
		{
			const RendererResetOriginContributors readiness =
				RendererResetOriginBit(RendererResetOrigin::AutomaticReadiness);
			Assert::IsTrue(
				RendererResetPreservesReadinessDisplayMeasurement(
					RendererResetScope::Graph, readiness));
			Assert::IsFalse(
				RendererResetPreservesReadinessDisplayMeasurement(
					RendererResetScope::GraphRetarget, readiness));
			Assert::IsFalse(
				RendererResetPreservesReadinessDisplayMeasurement(
					RendererResetScope::LiveQueue, readiness));
			Assert::IsTrue(RendererResetKeepsFreshRetargetSettleLineage(
				RendererResetScope::GraphRetarget, true));
			Assert::IsFalse(RendererResetKeepsFreshRetargetSettleLineage(
				RendererResetScope::Graph, true));
			Assert::IsFalse(RendererResetKeepsFreshRetargetSettleLineage(
				RendererResetScope::GraphRetarget, false));
		}
	};
}
