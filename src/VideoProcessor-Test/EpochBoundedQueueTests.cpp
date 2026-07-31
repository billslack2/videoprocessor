#include "pch.h"
#include "CppUnitTest.h"

#include <EpochBoundedQueue.h>

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(EpochBoundedQueueTests)
	{
	public:
		struct RecordingRelease
		{
			std::vector<int>* released = nullptr;

			void operator()(int& value) const
			{
				released->push_back(value);
			}
		};

		TEST_METHOD(DropsOldestAtCapacityWithoutAddingAnotherQueue)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(2, { &released });
			const PipelineEpoch epoch{ 7 };
			Assert::AreEqual(
				static_cast<int>(EpochBoundedQueuePushResult::Accepted),
				static_cast<int>(queue.Push(10, epoch, epoch)));
			Assert::AreEqual(
				static_cast<int>(EpochBoundedQueuePushResult::Accepted),
				static_cast<int>(queue.Push(11, epoch, epoch)));
			Assert::AreEqual(
				static_cast<int>(EpochBoundedQueuePushResult::AcceptedAfterOverflowDiscard),
				static_cast<int>(queue.Push(12, epoch, epoch)));

			Assert::AreEqual<size_t>(1, released.size());
			Assert::AreEqual(10, released[0]);
			int popped = 0;
			Assert::IsTrue(queue.TryPopCurrent(epoch, popped));
			Assert::AreEqual(11, popped);
			Assert::IsTrue(queue.TryPopCurrent(epoch, popped));
			Assert::AreEqual(12, popped);
			Assert::IsFalse(queue.TryPopCurrent(epoch, popped));
			Assert::AreEqual<uint64_t>(1, queue.Metrics().overflowDiscarded);
		}

		TEST_METHOD(RejectsAndReleasesWorkFromAnObsoleteEpoch)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(2, { &released });
			const PipelineEpoch oldEpoch{ 3 };
			const PipelineEpoch currentEpoch{ 4 };

			Assert::AreEqual(
				static_cast<int>(EpochBoundedQueuePushResult::RejectedStale),
				static_cast<int>(queue.Push(20, oldEpoch, currentEpoch)));
			Assert::AreEqual<size_t>(1, released.size());
			Assert::AreEqual(20, released[0]);
			Assert::AreEqual<size_t>(0, queue.Size());
			Assert::AreEqual<uint64_t>(1, queue.Metrics().staleDiscarded);
		}

		TEST_METHOD(PopDiscardsAStaleHeadAndNeverReturnsItToTheNextStage)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(3, { &released });
			const PipelineEpoch oldEpoch{ 5 };
			const PipelineEpoch currentEpoch{ 6 };
			(void)queue.Push(30, oldEpoch, oldEpoch);
			(void)queue.Push(31, currentEpoch, currentEpoch);

			int popped = 0;
			Assert::IsTrue(queue.TryPopCurrent(currentEpoch, popped));
			Assert::AreEqual(31, popped);
			Assert::AreEqual<size_t>(1, released.size());
			Assert::AreEqual(30, released[0]);
			Assert::AreEqual<uint64_t>(1, queue.Metrics().staleDiscarded);
		}

		TEST_METHOD(ResizeAndFlushReleaseOldestThenAllRemainingOwnership)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(4, { &released });
			const PipelineEpoch epoch{ 8 };
			(void)queue.Push(40, epoch, epoch);
			(void)queue.Push(41, epoch, epoch);
			(void)queue.Push(42, epoch, epoch);

			Assert::AreEqual<size_t>(2, queue.Resize(1));
			Assert::AreEqual<size_t>(2, released.size());
			Assert::AreEqual(40, released[0]);
			Assert::AreEqual(41, released[1]);
			Assert::AreEqual<size_t>(1, queue.Flush());
			Assert::AreEqual<size_t>(3, released.size());
			Assert::AreEqual(42, released[2]);
			Assert::AreEqual<uint64_t>(1, queue.Metrics().flushed);
		}

		TEST_METHOD(MinimumRemainingDepthPreservesTheExistingSteadyHandoffCushion)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(4, { &released });
			const PipelineEpoch epoch{ 9 };
			(void)queue.Push(50, epoch, epoch);
			int popped = 0;
			Assert::IsFalse(queue.TryPopCurrentIfDepthAbove(epoch, 1, popped));
			(void)queue.Push(51, epoch, epoch);
			Assert::IsTrue(queue.TryPopCurrentIfDepthAbove(epoch, 1, popped));
			Assert::AreEqual(50, popped);
		}

		TEST_METHOD(MutatesOnlyTheRequestedCurrentEpochFrame)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(4, { &released });
			const PipelineEpoch oldEpoch{ 10 };
			const PipelineEpoch currentEpoch{ 11 };
			(void)queue.Push(60, oldEpoch, oldEpoch);
			(void)queue.Push(61, currentEpoch, currentEpoch);
			Assert::IsTrue(queue.TryMutateCurrentFromBack(
				currentEpoch, 1, [](int& value) { value = 62; }));
			Assert::IsFalse(queue.TryMutateCurrentFromBack(
				currentEpoch, 2, [](int&) {}));
			int popped = 0;
			Assert::IsTrue(queue.TryPopCurrent(currentEpoch, popped));
			Assert::AreEqual(62, popped);
		}
	};
}
