#include "pch.h"
#include "CppUnitTest.h"

#include <LiveClockGapPreserver.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(LiveClockGapPreserverTests)
	{
	public:
		TEST_METHOD(PreservesAChannelChangeGapMissingFromTheHardwareClock)
		{
			LiveClockGapPreserver preserver;
			const int64_t duration = 166833;
			(void)preserver.Observe(100, false, 1000000, duration);

			const LiveClockGapDecision gap = preserver.Observe(
				106, true, 1000000 + duration, duration);

			Assert::IsTrue(gap.applied);
			Assert::AreEqual<uint64_t>(5, gap.missingFrames);
			Assert::AreEqual<int64_t>(duration * 5, gap.repair);
			Assert::AreEqual<int64_t>(duration * 5, gap.cumulativeOffset);
			Assert::AreEqual<int64_t>(duration * 6,
				gap.adjustedHardwareTime - 1000000);
			Assert::AreEqual<int64_t>(duration * 6,
				gap.MaximumPermittedAdvance(duration * 2));
		}

		TEST_METHOD(AccumulatesTheObservedFivePlusOneFrameChannelGap)
		{
			LiveClockGapPreserver preserver;
			const int64_t duration = 166833;
			(void)preserver.Observe(100, false, 1000000, duration);
			const LiveClockGapDecision first = preserver.Observe(
				106, true, 1000000 + duration, duration);
			const LiveClockGapDecision second = preserver.Observe(
				108, true, 1000000 + duration * 2, duration);

			Assert::AreEqual<int64_t>(duration * 5, first.repair);
			Assert::AreEqual<int64_t>(duration, second.repair);
			Assert::AreEqual<int64_t>(duration * 6, second.cumulativeOffset);
		}

		TEST_METHOD(DoesNotDoubleCountAClockThatAlreadyAdvancedAcrossTheGap)
		{
			LiveClockGapPreserver preserver;
			const int64_t duration = 166833;
			(void)preserver.Observe(100, false, 1000000, duration);

			const LiveClockGapDecision gap = preserver.Observe(
				106, true, 1000000 + duration * 6, duration);

			Assert::IsFalse(gap.applied);
			Assert::AreEqual<int64_t>(0, gap.repair);
			Assert::AreEqual<int64_t>(0, gap.cumulativeOffset);
		}

		TEST_METHOD(DoesNotTreatAnIntentionalVpQueueGapAsSourceTime)
		{
			LiveClockGapPreserver preserver;
			const int64_t duration = 166833;
			(void)preserver.Observe(100, false, 1000000, duration);

			const LiveClockGapDecision gap = preserver.Observe(
				106, false, 1000000 + duration, duration);

			Assert::IsFalse(gap.applied);
			Assert::AreEqual<int64_t>(0, gap.cumulativeOffset);
		}

		TEST_METHOD(ResetStartsANewEpochWithoutAnInheritedRepair)
		{
			LiveClockGapPreserver preserver;
			const int64_t duration = 417083;
			(void)preserver.Observe(100, false, 1000000, duration);
			(void)preserver.Observe(103, true, 1000000 + duration, duration);
			preserver.Reset();

			const LiveClockGapDecision first = preserver.Observe(
				1, true, 500, duration);
			Assert::IsFalse(first.applied);
			Assert::AreEqual<int64_t>(0, first.cumulativeOffset);
			Assert::AreEqual<int64_t>(500, first.adjustedHardwareTime);
		}
	};
}
