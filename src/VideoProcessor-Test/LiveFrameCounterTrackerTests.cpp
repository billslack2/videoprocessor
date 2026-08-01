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

		TEST_METHOD(MaterialGapUsesOneHundredMillisecondsAt5994)
		{
			LiveFrameCounterTracker tracker;
			LiveSourceGapRecoveryPolicy policy;
			(void)tracker.Observe(100);

			const auto sixMissing = policy.Observe(
				tracker.Observe(107), 60000, 1001);

			Assert::AreEqual<uint64_t>(6, sixMissing.materialGapFrames);
			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::RequestGraphReprime),
				static_cast<int>(sixMissing.action));
		}

		TEST_METHOD(Isolated5994MissRemainsLocal)
		{
			LiveFrameCounterTracker tracker;
			LiveSourceGapRecoveryPolicy policy;
			(void)tracker.Observe(100);

			const auto oneMissing = policy.Observe(
				tracker.Observe(102), 60000, 1001);

			Assert::AreEqual<uint64_t>(6, oneMissing.materialGapFrames);
			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::LocalDiscontinuity),
				static_cast<int>(oneMissing.action));
		}

		TEST_METHOD(MaterialGapUsesThreeFramesAt23976)
		{
			LiveFrameCounterTracker tracker;
			LiveSourceGapRecoveryPolicy policy;
			(void)tracker.Observe(100);

			const auto twoMissing = policy.Observe(
				tracker.Observe(103), 24000, 1001);
			const auto threeMissing = policy.Observe(
				tracker.Observe(107), 24000, 1001);

			Assert::AreEqual<uint64_t>(3, twoMissing.materialGapFrames);
			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::LocalDiscontinuity),
				static_cast<int>(twoMissing.action));
			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::RequestGraphReprime),
				static_cast<int>(threeMissing.action));
		}

		TEST_METHOD(GraphResetRequiresOneHealthySecondBeforeRearming)
		{
			LiveFrameCounterTracker tracker;
			LiveSourceGapRecoveryPolicy policy;
			policy.OnGraphReset();

			LiveFrameCounterDecision counter = tracker.Observe(100);
			auto recovery = policy.Observe(counter, 60000, 1001);
			for (uint64_t frame = 101; frame < 159; ++frame)
			{
				counter = tracker.Observe(frame);
				recovery = policy.Observe(counter, 60000, 1001);
			}

			const auto suppressed = policy.Observe(
				tracker.Observe(166), 60000, 1001);
			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::SuppressedUntilHealthy),
				static_cast<int>(suppressed.action));

			tracker.Reset();
			policy.OnGraphReset();
			counter = tracker.Observe(200);
			(void)policy.Observe(counter, 60000, 1001);
			for (uint64_t frame = 201; frame <= 260; ++frame)
			{
				counter = tracker.Observe(frame);
				(void)policy.Observe(counter, 60000, 1001);
			}
			const auto rearmed = policy.Observe(
				tracker.Observe(267), 60000, 1001);
			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::RequestGraphReprime),
				static_cast<int>(rearmed.action));
		}

		TEST_METHOD(MaterialGapRequestIsLatchedUntilGraphReset)
		{
			LiveFrameCounterTracker tracker;
			LiveSourceGapRecoveryPolicy policy;
			(void)tracker.Observe(100);

			const auto first = policy.Observe(
				tracker.Observe(107), 60000, 1001);
			const auto repeated = policy.Observe(
				tracker.Observe(114), 60000, 1001);

			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::RequestGraphReprime),
				static_cast<int>(first.action));
			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::SuppressedUntilHealthy),
				static_cast<int>(repeated.action));
		}

		TEST_METHOD(CounterResetRemainsLocalForTransitionOwner)
		{
			LiveFrameCounterTracker tracker;
			LiveSourceGapRecoveryPolicy policy;
			(void)tracker.Observe(3107);

			const auto recovery = policy.Observe(
				tracker.Observe(1), 60000, 1001);

			Assert::AreEqual(
				static_cast<int>(
					LiveSourceGapRecoveryAction::LocalDiscontinuity),
				static_cast<int>(recovery.action));
		}
	};
}
