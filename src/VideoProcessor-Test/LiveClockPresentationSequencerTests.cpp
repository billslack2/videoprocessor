#include "pch.h"
#include "CppUnitTest.h"

#include <LiveClockPresentationSequencer.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
	constexpr VideoReferenceTime k5994Duration = 166833;
	constexpr VideoReferenceTime k23976Duration = 417083;

	LiveClockPresentationInput Input(
		uint64_t epoch = 7,
		VideoReferenceTime streamTime = 1000000,
		VideoReferenceTime duration = k5994Duration)
	{
		LiveClockPresentationInput input;
		input.epoch = epoch;
		input.streamTime = streamTime;
		input.nominalFrameDuration = duration;
		input.observedFrameDuration = duration;
		input.observedDurationValid = true;
		return input;
	}
}

namespace Tests
{
	TEST_CLASS(LiveClockPresentationSequencerTests)
	{
	public:
		TEST_METHOD(FirstDeliveryAnchorsOneFrameAheadOfGraphStreamTime)
		{
			LiveClockPresentationSequencer sequencer;
			const auto decision = sequencer.Preview(Input());
			Assert::IsTrue(decision.valid);
			Assert::IsTrue(decision.reanchored);
			Assert::IsTrue(decision.discontinuity);
			Assert::AreEqual<VideoReferenceTime>(
				1000000 + k5994Duration, decision.start);
			Assert::AreEqual<VideoReferenceTime>(
				decision.start + k5994Duration, decision.stop);
		}

		TEST_METHOD(ExplicitLargerPresentationLeadRemainsHonored)
		{
			LiveClockPresentationSequencer sequencer;
			auto input = Input();
			input.presentationLead = 900000;
			const auto decision = sequencer.Preview(input);
			Assert::AreEqual<VideoReferenceTime>(1900000, decision.start);
		}

		TEST_METHOD(OnlyCommittedDeliveriesAdvancePresentationTime)
		{
			LiveClockPresentationSequencer sequencer;
			const auto attempted = sequencer.Preview(Input());
			const auto retry = sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(attempted.outputSequence,
				retry.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(attempted.start, retry.start);
			Assert::IsTrue(sequencer.Commit(retry));
			const auto next = sequencer.Preview(Input());
			Assert::AreEqual<VideoReferenceTime>(retry.stop, next.start);
		}

		TEST_METHOD(DiscardedCapturePicturesCannotCreateTimestampGap)
		{
			LiveClockPresentationSequencer sequencer;
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));

			// Capture identity is intentionally absent. Whether one or twenty
			// stale pictures were removed, the next delivered output is contiguous.
			const auto next = sequencer.Preview(Input(7, 1000000));
			Assert::AreEqual<VideoReferenceTime>(first.stop, next.start);
			Assert::IsFalse(next.reanchored);
		}

		TEST_METHOD(SourceDiscontinuityIsMarkedButRemainsContinuous)
		{
			LiveClockPresentationSequencer sequencer;
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			auto gap = Input();
			gap.sourceDiscontinuity = true;
			const auto next = sequencer.Preview(gap);
			Assert::IsTrue(next.discontinuity);
			Assert::AreEqual<VideoReferenceTime>(first.stop, next.start);
		}

		TEST_METHOD(NewEpochReanchorsToCurrentGraphStreamTime)
		{
			LiveClockPresentationSequencer sequencer;
			const auto first = sequencer.Preview(
				Input(7, 1000000, k23976Duration));
			Assert::IsTrue(sequencer.Commit(first));
			const auto nextEpoch = sequencer.Preview(
				Input(8, 250000, k23976Duration));
			Assert::IsTrue(nextEpoch.reanchored);
			Assert::IsTrue(nextEpoch.discontinuity);
			Assert::AreEqual<VideoReferenceTime>(
				250000 + k23976Duration, nextEpoch.start);
		}

		TEST_METHOD(SameEpochClockLeapCatchesUpWithoutRemainingLate)
		{
			LiveClockPresentationSequencer sequencer;
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			const auto caughtUp = sequencer.Preview(Input(7, 3000000));
			Assert::IsTrue(caughtUp.reanchored);
			Assert::IsTrue(caughtUp.discontinuity);
			Assert::AreEqual<VideoReferenceTime>(
				3000000 + k5994Duration, caughtUp.start);
		}

		TEST_METHOD(MissingFrameDurationFallsBackToNominal)
		{
			LiveClockPresentationSequencer sequencer;
			auto input = Input();
			input.observedFrameDuration = k5994Duration * 3;
			const auto decision = sequencer.Preview(input);
			Assert::AreEqual<VideoReferenceTime>(
				k5994Duration, decision.duration);
		}
	};
}
