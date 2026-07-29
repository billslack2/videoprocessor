#include "pch.h"
#include "CppUnitTest.h"

#include <RendererResetRequestLatch.h>

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

		TEST_METHOD(LegacyPollDoesNotClearResetLatch)
		{
			RendererResetRequestLatch latch;
			Assert::IsTrue(latch.Request(GraphRecovery(41)));
			Assert::IsTrue(latch.ConsumeLegacyNotification());
			Assert::IsFalse(latch.ConsumeLegacyNotification());
			Assert::IsTrue(latch.Pending());
		}
	};
}
