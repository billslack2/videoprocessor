#include "pch.h"
#include "CppUnitTest.h"

#include <RendererResetRequestLatch.h>
#include <microsoft_directshow/video_renderers/DirectShowGraphExecutor.h>

#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	namespace
	{
		class RecordingResetSink final : public IRendererResetRequestSink
		{
		public:
			void Submit(RendererResetRequest request) noexcept override
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_requests.push_back(request);
			}

			size_t Count() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_requests.size();
			}

			RendererResetRequest First() const
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				return m_requests.front();
			}

		private:
			mutable std::mutex m_mutex;
			std::vector<RendererResetRequest> m_requests;
		};

		class BlockingResetSink final : public IRendererResetRequestSink
		{
		public:
			void Submit(RendererResetRequest) noexcept override
			{
				m_entered.set_value();
				m_release.get_future().wait();
				++m_count;
			}

			std::future<void> Entered()
			{
				return m_entered.get_future();
			}

			void Release()
			{
				m_release.set_value();
			}

			int Count() const
			{
				return m_count.load(std::memory_order_acquire);
			}

		private:
			std::promise<void> m_entered;
			std::promise<void> m_release;
			std::atomic_int m_count{0};
		};

		RendererResetRequest GraphRecovery(uint64_t epoch)
		{
			RendererResetRequest request;
			request.backendEpoch = epoch;
			request.reason = RendererResetReason::LivenessRecovery;
			request.scope = RendererResetScope::Graph;
			return request;
		}
	}


	TEST_CLASS(RendererResetRequestLatchTests)
	{
	public:
		TEST_METHOD(PublishesImmediatelyAndOnlyOnceWhilePending)
		{
			RendererResetRequestLatch latch;
			auto sink = std::make_shared<RecordingResetSink>();
			latch.SetSink(sink);

			Assert::IsTrue(latch.Request(GraphRecovery(17)));
			Assert::IsFalse(latch.Request(GraphRecovery(18)));

			Assert::AreEqual<size_t>(1, sink->Count());
			Assert::AreEqual<uint64_t>(17, sink->First().backendEpoch);
			Assert::IsTrue(latch.Pending());
		}

		TEST_METHOD(LateSinkReceivesAlreadyLatchedRequest)
		{
			RendererResetRequestLatch latch;
			Assert::IsTrue(latch.Request(GraphRecovery(23)));

			auto sink = std::make_shared<RecordingResetSink>();
			latch.SetSink(sink);
			latch.SetSink(sink);

			Assert::AreEqual<size_t>(1, sink->Count());
			Assert::AreEqual<uint64_t>(23, sink->First().backendEpoch);
		}

		TEST_METHOD(SuccessfulCompletionAllowsNextRequest)
		{
			RendererResetRequestLatch latch;
			auto sink = std::make_shared<RecordingResetSink>();
			latch.SetSink(sink);

			Assert::IsTrue(latch.Request(GraphRecovery(31)));
			latch.Complete();
			Assert::IsFalse(latch.Pending());
			Assert::IsTrue(latch.Request(GraphRecovery(32)));

			Assert::AreEqual<size_t>(2, sink->Count());
		}

		TEST_METHOD(RebindingPendingRequestPublishesToNewBinding)
		{
			RendererResetRequestLatch latch;
			auto oldSink = std::make_shared<RecordingResetSink>();
			auto newSink = std::make_shared<RecordingResetSink>();
			latch.SetSink(oldSink);
			Assert::IsTrue(latch.Request(GraphRecovery(35)));

			latch.SetSink({});
			latch.SetSink(newSink);

			Assert::AreEqual<size_t>(1, oldSink->Count());
			Assert::AreEqual<size_t>(1, newSink->Count());
			Assert::AreEqual<uint64_t>(35, newSink->First().backendEpoch);
		}

		TEST_METHOD(CompletionCannotOvertakePublication)
		{
			RendererResetRequestLatch latch;
			auto sink = std::make_shared<BlockingResetSink>();
			std::future<void> entered = sink->Entered();
			latch.SetSink(sink);

			std::future<bool> requested = std::async(
				std::launch::async,
				[&latch]()
				{
					return latch.Request(GraphRecovery(37));
				});
			entered.wait();

			std::future<void> completed = std::async(
				std::launch::async,
				[&latch]()
				{
					latch.Complete();
				});
			const bool completionWasBlocked =
				completed.wait_for(std::chrono::milliseconds(50)) ==
				std::future_status::timeout;

			sink->Release();
			Assert::IsTrue(requested.get());
			completed.get();
			Assert::IsTrue(completionWasBlocked);
			Assert::AreEqual(1, sink->Count());
			Assert::IsFalse(latch.Pending());
		}

		TEST_METHOD(ConcurrentRequestsPublishExactlyOnce)
		{
			RendererResetRequestLatch latch;
			auto sink = std::make_shared<RecordingResetSink>();
			latch.SetSink(sink);

			std::vector<std::thread> threads;
			for (uint64_t epoch = 1; epoch <= 16; ++epoch)
			{
				threads.emplace_back([&latch, epoch]()
					{
						latch.Request(GraphRecovery(epoch));
					});
			}
			for (auto& thread : threads)
				thread.join();

			Assert::AreEqual<size_t>(1, sink->Count());
			Assert::IsTrue(latch.Pending());
		}

	};


	TEST_CLASS(DirectShowGraphExecutorTests)
	{
	public:
		TEST_METHOD(UsesOnePermanentMtaOwner)
		{
			DirectShowGraphExecutor executor;
			const DWORD caller = GetCurrentThreadId();
			const DWORD first = executor.Invoke([]()
				{
					APTTYPE apartmentType = APTTYPE_CURRENT;
					APTTYPEQUALIFIER qualifier =
						APTTYPEQUALIFIER_NONE;
					Assert::IsTrue(SUCCEEDED(CoGetApartmentType(
						&apartmentType, &qualifier)));
					Assert::IsTrue(apartmentType == APTTYPE_MTA);
					return GetCurrentThreadId();
				});
			const DWORD second = executor.Invoke([]()
				{
					return GetCurrentThreadId();
				});

			Assert::AreNotEqual(caller, first);
			Assert::AreEqual(first, second);
			Assert::AreEqual(first, executor.OwnerThreadId());
		}

		TEST_METHOD(ConcurrentCommandsNeverOverlap)
		{
			DirectShowGraphExecutor executor;
			std::promise<void> firstEntered;
			std::promise<void> releaseFirst;
			std::shared_future<void> release =
				releaseFirst.get_future().share();
			std::future<void> first = std::async(
				std::launch::async, [&]()
				{
					executor.Invoke([&]()
						{
							firstEntered.set_value();
							release.wait();
						});
				});
			firstEntered.get_future().wait();

			std::atomic_bool secondEntered{false};
			std::future<void> second = std::async(
				std::launch::async, [&]()
				{
					executor.Invoke([&]()
						{
							secondEntered.store(
								true, std::memory_order_release);
						});
				});
			const bool stayedSerialized =
				second.wait_for(std::chrono::milliseconds(50)) ==
				std::future_status::timeout &&
				!secondEntered.load(std::memory_order_acquire);

			releaseFirst.set_value();
			first.get();
			second.get();
			Assert::IsTrue(stayedSerialized);
			Assert::IsTrue(
				secondEntered.load(std::memory_order_acquire));
		}

		TEST_METHOD(NestedOwnerInvocationRunsInline)
		{
			DirectShowGraphExecutor executor;
			const DWORD nested = executor.Invoke([&executor]()
				{
					const DWORD outer = GetCurrentThreadId();
					const DWORD inner = executor.Invoke([]()
						{
							return GetCurrentThreadId();
						});
					Assert::AreEqual(outer, inner);
					return inner;
				});
			Assert::AreEqual(executor.OwnerThreadId(), nested);
		}

		TEST_METHOD(AsyncCommandsPreserveFifoOrder)
		{
			DirectShowGraphExecutor executor;
			std::mutex mutex;
			std::vector<int> observed;
			for (int value = 1; value <= 3; ++value)
			{
				Assert::IsTrue(executor.Post([&mutex, &observed, value]()
					{
						std::lock_guard<std::mutex> lock(mutex);
						observed.push_back(value);
					}));
			}
			executor.Shutdown();

			Assert::AreEqual<size_t>(3, observed.size());
			Assert::AreEqual(1, observed[0]);
			Assert::AreEqual(2, observed[1]);
			Assert::AreEqual(3, observed[2]);
		}

		TEST_METHOD(CoalescesOnlyQueuedNotStartedCommand)
		{
			DirectShowGraphExecutor executor;
			std::promise<void> activeEntered;
			std::promise<void> releaseActive;
			const std::shared_future<void> release =
				releaseActive.get_future().share();
			std::mutex mutex;
			std::vector<int> observed;

			Assert::IsTrue(executor.Post([&]()
				{
					activeEntered.set_value();
					release.wait();
				}));
			activeEntered.get_future().wait();
			Assert::IsTrue(executor.PostCoalesced(99, [&]()
				{
					std::lock_guard<std::mutex> lock(mutex);
					observed.push_back(1);
				}));
			Assert::IsTrue(executor.PostCoalesced(99, [&]()
				{
					std::lock_guard<std::mutex> lock(mutex);
					observed.push_back(2);
				}));
			releaseActive.set_value();
			executor.Shutdown();

			Assert::AreEqual<size_t>(1, observed.size());
			Assert::AreEqual(2, observed.front());
		}

		TEST_METHOD(ForcedShutdownCancelsQueuedWorkAndRunsFinalCommand)
		{
			DirectShowGraphExecutor executor;
			std::promise<void> activeEntered;
			std::promise<void> releaseActive;
			const std::shared_future<void> release =
				releaseActive.get_future().share();
			std::atomic_bool cancelledCommandRan{false};
			std::atomic_bool finalCommandRan{false};

			Assert::IsTrue(executor.Post([&]()
				{
					activeEntered.set_value();
					release.wait();
				}));
			activeEntered.get_future().wait();
			Assert::IsTrue(executor.Post([&]()
				{
					cancelledCommandRan.store(
						true, std::memory_order_release);
				}));

			std::future<void> shutdown = std::async(
				std::launch::async, [&]()
				{
					executor.CancelPendingAndShutdown([&]()
						{
							finalCommandRan.store(
								true, std::memory_order_release);
						});
				});
			while (executor.PostCoalesced(777, []() {}))
				std::this_thread::yield();
			releaseActive.set_value();
			shutdown.get();

			Assert::IsFalse(cancelledCommandRan.load(
				std::memory_order_acquire));
			Assert::IsTrue(finalCommandRan.load(
				std::memory_order_acquire));
		}

		TEST_METHOD(ShutdownRejectsNewCommands)
		{
			DirectShowGraphExecutor executor;
			executor.Invoke([]() {});
			executor.Shutdown();

			bool rejected = false;
			try
			{
				executor.Invoke([]() {});
			}
			catch (const std::runtime_error&)
			{
				rejected = true;
			}
			Assert::IsTrue(rejected);
			Assert::IsFalse(executor.Post([]() {}));
			Assert::IsFalse(executor.PostCoalesced(1, []() {}));
		}
	};
}
