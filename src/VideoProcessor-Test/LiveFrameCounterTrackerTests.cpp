#include "pch.h"
#include "CppUnitTest.h"

#include <LiveFrameCounterTracker.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace VideoProcessorTest
{
	TEST_CLASS(LiveFrameCounterTrackerTests)
	{
	public:
		TEST_METHOD(ConsecutiveFramesRemainContinuous)
		{
			LiveFrameCounterTracker tracker;
			const LiveFrameCounterDecision first = tracker.Observe(1404);
			const LiveFrameCounterDecision next = tracker.Observe(1405);

			Assert::AreEqual(
				static_cast<int>(LiveFrameCounterTransition::First),
				static_cast<int>(first.transition));
			Assert::IsFalse(first.IsDiscontinuity());
			Assert::AreEqual(
				static_cast<int>(LiveFrameCounterTransition::Consecutive),
				static_cast<int>(next.transition));
			Assert::IsFalse(next.IsDiscontinuity());
		}

		TEST_METHOD(ResumedForwardGapBecomesLocalSourceDiscontinuity)
		{
			LiveFrameCounterTracker tracker;
			(void)tracker.Observe(1404);

			const LiveFrameCounterDecision decision = tracker.Observe(1794);

			Assert::AreEqual(
				static_cast<int>(LiveFrameCounterTransition::ForwardGap),
				static_cast<int>(decision.transition));
			Assert::IsTrue(decision.IsDiscontinuity());
			Assert::AreEqual<uint64_t>(1404, decision.previous);
			Assert::AreEqual<uint64_t>(1794, decision.current);
			Assert::AreEqual<uint64_t>(389, decision.missingFrames);
		}

		TEST_METHOD(CounterResetBecomesLocalSourceDiscontinuity)
		{
			LiveFrameCounterTracker tracker;
			(void)tracker.Observe(3107);

			const LiveFrameCounterDecision decision = tracker.Observe(1);

			Assert::AreEqual(
				static_cast<int>(LiveFrameCounterTransition::CounterReset),
				static_cast<int>(decision.transition));
			Assert::IsTrue(decision.IsDiscontinuity());
			Assert::AreEqual<uint64_t>(0, decision.missingFrames);
		}

		TEST_METHOD(GraphResetRebaselinesWithoutSyntheticDiscontinuity)
		{
			LiveFrameCounterTracker tracker;
			(void)tracker.Observe(3107);
			tracker.Reset();

			const LiveFrameCounterDecision decision = tracker.Observe(3119);

			Assert::AreEqual(
				static_cast<int>(LiveFrameCounterTransition::First),
				static_cast<int>(decision.transition));
			Assert::IsFalse(decision.IsDiscontinuity());
		}
	};
}
