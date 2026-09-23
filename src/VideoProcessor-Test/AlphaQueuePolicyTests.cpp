#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaQueuePolicy.h>

#include <limits>
#include <deque>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(AlphaQueuePolicyTests)
	{
	public:
		TEST_METHOD(PreviewReportsPhysicalAvailabilityBeyondConfiguredBudget)
		{
			struct Frame { bool cadenceRepeat = false; };
			const std::deque<Frame> queue(8);
			const auto window = AlphaQueuePolicy::SelectActivePicturePreview(queue, 3, 8);
			Assert::AreEqual<size_t>(7, window.availableFutureFrames);
			Assert::AreEqual<size_t>(3, window.effectiveFutureFrames);
			Assert::AreEqual<size_t>(4, window.indices.size());
			for (size_t index = 0; index < window.indices.size(); ++index)
				Assert::AreEqual(index, window.indices[index]);
		}

		TEST_METHOD(PreviewShortQueuesUseOnlyExistingSourceFrames)
		{
			struct Frame { bool cadenceRepeat = false; };
			for (size_t count = 0; count <= 3; ++count)
			{
				const std::deque<Frame> queue(count);
				const auto window = AlphaQueuePolicy::SelectActivePicturePreview(queue, 5, 8);
				const size_t available = count > 0 ? count - 1 : 0;
				Assert::AreEqual(available, window.availableFutureFrames);
				Assert::AreEqual(available, window.effectiveFutureFrames);
				Assert::AreEqual(count, window.indices.size());
				Assert::IsTrue(AlphaQueuePolicy::CanDequeue(count, 1, false) == (count > 0));
			}
		}

		TEST_METHOD(PreviewRepeatsDoNotCountAsAvailableEvidence)
		{
			struct Frame { bool cadenceRepeat = false; };
			const std::deque<Frame> queue = {{false}, {true}, {false}, {true}, {false}, {true}};
			const auto window = AlphaQueuePolicy::SelectActivePicturePreview(queue, 5, 8);
			Assert::AreEqual<size_t>(2, window.availableFutureFrames);
			Assert::AreEqual<size_t>(2, window.effectiveFutureFrames);
			Assert::AreEqual<size_t>(3, window.indices.size());
			Assert::AreEqual<size_t>(0, window.indices[0]);
			Assert::AreEqual<size_t>(2, window.indices[1]);
			Assert::AreEqual<size_t>(4, window.indices[2]);
		}

		TEST_METHOD(PreviewDisabledOrRepeatedCurrentDoesNotScheduleWork)
		{
			struct Frame { bool cadenceRepeat = false; };
			for (bool repeatedCurrent : {false, true})
			{
				std::deque<Frame> queue(4);
				queue.front().cadenceRepeat = repeatedCurrent;
				const auto window = AlphaQueuePolicy::SelectActivePicturePreview(queue,
					repeatedCurrent ? 5 : 0, 8);
				Assert::AreEqual<size_t>(3, window.availableFutureFrames);
				Assert::AreEqual<size_t>(0, window.effectiveFutureFrames);
				Assert::IsTrue(window.indices.empty());
			}
		}

		TEST_METHOD(PreviewClampsWorkWithoutHidingLargerPhysicalQueue)
		{
			struct Frame { bool cadenceRepeat = false; };
			const std::deque<Frame> queue(32);
			for (size_t requested : {size_t{1}, size_t{2}, size_t{3}, size_t{5}, size_t{8}, size_t{99}})
			{
				const auto window = AlphaQueuePolicy::SelectActivePicturePreview(queue, requested, 8);
				const size_t effective = (std::min)(requested, size_t{8});
				Assert::AreEqual<size_t>(31, window.availableFutureFrames);
				Assert::AreEqual(effective, window.effectiveFutureFrames);
				Assert::AreEqual(effective + 1, window.indices.size());
			}
		}

		TEST_METHOD(PreviewSelectionDoesNotConsumeOrMutateQueue)
		{
			struct Frame { bool cadenceRepeat = false; size_t identity = 0; };
			const std::deque<Frame> queue = {{false, 100}, {true, 100}, {false, 101}, {false, 102}};
			for (int pass = 0; pass < 2; ++pass)
			{
				const auto window = AlphaQueuePolicy::SelectActivePicturePreview(queue, 2, 8);
				Assert::AreEqual<size_t>(4, queue.size());
				Assert::AreEqual<size_t>(100, queue.front().identity);
				Assert::AreEqual<size_t>(102, queue.back().identity);
				Assert::AreEqual<size_t>(3, window.indices.size());
			}
		}

		TEST_METHOD(OmittedOverrideUsesSafeAlphaDefault)
		{
			Assert::AreEqual<size_t>(4,
				AlphaQueuePolicy::ResolveDesiredDepth(0));
		}

		TEST_METHOD(OverrideWinsOverRememberedValue)
		{
			Assert::AreEqual<size_t>(6,
				AlphaQueuePolicy::ResolveDesiredDepth(6, 12));
		}

		TEST_METHOD(HardCapacityMatchesConfiguredQueueLimit)
		{
			Assert::AreEqual<size_t>(1, AlphaQueuePolicy::HardCapacity(1));
			Assert::AreEqual<size_t>(4, AlphaQueuePolicy::HardCapacity(4));
			Assert::AreEqual<size_t>(32, AlphaQueuePolicy::HardCapacity(32));
			Assert::AreEqual<size_t>(std::numeric_limits<size_t>::max(),
				AlphaQueuePolicy::HardCapacity(
					std::numeric_limits<size_t>::max()));
		}

		TEST_METHOD(SteadyTargetDoesNotChangeConfiguredCapacity)
		{
			const size_t capacity = AlphaQueuePolicy::HardCapacity(32);
			Assert::AreEqual<size_t>(32, capacity);
			Assert::AreEqual<size_t>(2,
				AlphaQueuePolicy::ClampDesiredDepthToCapacity(2, capacity));
			Assert::AreEqual<size_t>(32,
				AlphaQueuePolicy::ClampDesiredDepthToCapacity(64, capacity));
		}

		TEST_METHOD(HealthyBandStraddlesDesiredDepth)
		{
			Assert::AreEqual<size_t>(0,
				AlphaQueuePolicy::HealthyLowWater(1));
			Assert::AreEqual<size_t>(3,
				AlphaQueuePolicy::HealthyLowWater(4));
			Assert::AreEqual<size_t>(4,
				AlphaQueuePolicy::HealthyHighWater(4));
		}

		TEST_METHOD(StartupPrefillWaitsForDesiredDepth)
		{
			Assert::IsFalse(AlphaQueuePolicy::CanDequeue(3, 4, true));
			Assert::IsTrue(AlphaQueuePolicy::CanDequeue(4, 4, true));
		}

		TEST_METHOD(SteadyStateRetainsLowWaterReserve)
		{
			Assert::IsFalse(AlphaQueuePolicy::CanDequeue(3, 4, false));
			Assert::IsTrue(AlphaQueuePolicy::CanDequeue(4, 4, false));
			Assert::IsTrue(AlphaQueuePolicy::CanDequeue(1, 1, false));
		}

		TEST_METHOD(RenderStallThresholdScalesWithFramePeriod)
		{
			Assert::AreEqual(50.0,
				AlphaQueuePolicy::RenderStallThresholdMs(59.94), 0.01);
			Assert::AreEqual(83.42,
				AlphaQueuePolicy::RenderStallThresholdMs(23.976), 0.01);
		}

		TEST_METHOD(BacklogRecoveryRequiresExcessDepthAndBoundedStaleness)
		{
			Assert::IsFalse(AlphaQueuePolicy::ShouldRecoverBacklog(
				2, 2, 500.0, 500.0, 59.94));
			Assert::IsFalse(AlphaQueuePolicy::ShouldRecoverBacklog(
				3, 2, 80.0, 40.0, 59.94));
			Assert::IsTrue(AlphaQueuePolicy::ShouldRecoverBacklog(
				3, 2, 80.0, 60.0, 59.94));
			Assert::IsTrue(AlphaQueuePolicy::ShouldRecoverBacklog(
				8, 2, 140.0, 20.0, 23.976));
		}

		TEST_METHOD(ConfigValueRequiresPositiveDecimalInteger)
		{
			size_t value = 0;
			Assert::IsTrue(
				AlphaQueuePolicy::TryParsePositiveSize("4", value));
			Assert::AreEqual<size_t>(4, value);
			Assert::IsFalse(
				AlphaQueuePolicy::TryParsePositiveSize("0", value));
			Assert::IsFalse(
				AlphaQueuePolicy::TryParsePositiveSize("-4", value));
			Assert::IsFalse(
				AlphaQueuePolicy::TryParsePositiveSize("4x", value));
		}
	};
}
