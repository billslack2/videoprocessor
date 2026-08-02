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

		TEST_METHOD(TemporaryMaximumKeepsNewestFramesWithoutResizingTheQueue)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(8, { &released });
			const PipelineEpoch epoch{ 12 };
			(void)queue.PushWithMaximum(70, epoch, epoch, 3);
			(void)queue.PushWithMaximum(71, epoch, epoch, 3);
			(void)queue.PushWithMaximum(72, epoch, epoch, 3);
			Assert::AreEqual(
				static_cast<int>(EpochBoundedQueuePushResult::AcceptedAfterOverflowDiscard),
				static_cast<int>(queue.PushWithMaximum(73, epoch, epoch, 3)));
			Assert::AreEqual<size_t>(3, queue.Size());
			Assert::AreEqual<size_t>(8, queue.Metrics().capacity);
			Assert::AreEqual<size_t>(1, released.size());
			Assert::AreEqual(70, released[0]);
		}

		TEST_METHOD(TemporaryMaximumAnnotatesTheRetainedNewestFrameAtomically)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(8, { &released });
			const PipelineEpoch epoch{ 18 };
			(void)queue.PushWithMaximum(1, epoch, epoch, 1);
			size_t discarded = 0;
			Assert::AreEqual(
				static_cast<int>(EpochBoundedQueuePushResult::AcceptedAfterOverflowDiscard),
				static_cast<int>(queue.PushWithMaximumAndMutateNewest(
					2, epoch, epoch, 1, &discarded,
					[](int& retained, size_t replaced)
					{
						retained += static_cast<int>(replaced * 100);
					})));
			Assert::AreEqual<size_t>(1, discarded);
			int popped = 0;
			Assert::IsTrue(queue.TryPopCurrent(epoch, popped));
			Assert::AreEqual(102, popped);
			Assert::AreEqual<size_t>(1, released.size());
			Assert::AreEqual(1, released[0]);
		}

		TEST_METHOD(TemporaryMaximumImmediatelyTrimsPreExistingBacklog)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(8, { &released });
			const PipelineEpoch epoch{ 17 };
			for (int value = 1; value <= 8; ++value)
				(void)queue.Push(value, epoch, epoch);
			Assert::AreEqual(
				static_cast<int>(EpochBoundedQueuePushResult::AcceptedAfterOverflowDiscard),
				static_cast<int>(queue.PushWithMaximum(9, epoch, epoch, 3)));
			Assert::AreEqual<size_t>(3, queue.Size());
			Assert::AreEqual<size_t>(6, released.size());
			int popped = 0;
			Assert::IsTrue(queue.TryPopCurrent(epoch, popped));
			Assert::AreEqual(7, popped);
			Assert::IsTrue(queue.TryPopCurrent(epoch, popped));
			Assert::AreEqual(8, popped);
			Assert::IsTrue(queue.TryPopCurrent(epoch, popped));
			Assert::AreEqual(9, popped);
		}

		TEST_METHOD(TemporaryMaximumStillRejectsAnObsoleteEpoch)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(8, { &released });
			Assert::AreEqual(
				static_cast<int>(EpochBoundedQueuePushResult::RejectedStale),
				static_cast<int>(queue.PushWithMaximum(
					80, { 13 }, { 14 }, 3)));
			Assert::AreEqual<size_t>(0, queue.Size());
			Assert::AreEqual(80, released[0]);
		}

		TEST_METHOD(CurrentDepthNeverCountsStaleEpochWork)
		{
			std::vector<int> released;
			EpochBoundedQueue<int, RecordingRelease> queue(8, { &released });
			(void)queue.Push(90, { 15 }, { 15 });
			(void)queue.Push(91, { 16 }, { 16 });
			Assert::AreEqual<size_t>(1, queue.CurrentDepth({ 16 }));
		}
	};
}
