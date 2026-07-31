#include "pch.h"
#include "CppUnitTest.h"

#include <RationalLiveOutputSequencer.h>

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
RationalLiveOutputTimestampInput Input(uint64_t epoch = 9)
{
	RationalLiveOutputTimestampInput input;
	input.epoch = epoch;
	input.ppmCorrection = -17;
	input.pipelineOffset = 0;
	input.presentationLead = 1800000;
	return input;
}

RationalLiveOutputTimestampInput DisplayInput(
	uint64_t epoch = 9, double rate = 60.0)
{
	RationalLiveOutputTimestampInput input = Input(epoch);
	input.cadence = RationalLiveOutputCadence::Display;
	input.displayRateHz = rate;
	return input;
}
}

namespace Tests
{
	TEST_CLASS(RationalLiveOutputSequencerTests)
	{
	public:
		TEST_METHOD(CommitsAContinuous5994OutputTimeline)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const RationalLiveOutputTimestampDecision first =
				sequencer.Preview(Input());
			Assert::IsTrue(first.valid);
			Assert::AreEqual<uint64_t>(0, first.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(1800000, first.start);
			Assert::IsTrue(first.discontinuity);
			Assert::IsTrue(sequencer.Commit(first));

			const RationalLiveOutputTimestampDecision second =
				sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(1, second.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(first.stop, second.start);
			Assert::IsTrue(second.stop > second.start);
			Assert::AreEqual<int64_t>(first.mediaStop, second.mediaStart);
			Assert::IsFalse(second.discontinuity);
		}

		TEST_METHOD(SkippedLivePicturesDoNotSkipPresentationTime)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			RationalLiveOutputTimestampDecision previous = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(previous));
			for (uint32_t i = 0; i < 4; ++i)
			{
				const RationalLiveOutputTimestampDecision next =
					sequencer.Preview(Input());
				Assert::IsTrue(next.start >= previous.stop);
				Assert::IsTrue(sequencer.Commit(next));
				previous = next;
			}

			// Ten captured pictures may be discarded before the next delivery. The
			// delivery-owned sequence has no capture number to turn that into a
			// timestamp hole.
			const RationalLiveOutputTimestampDecision afterDiscard =
				sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(5, afterDiscard.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(previous.stop, afterDiscard.start);
			Assert::IsFalse(afterDiscard.discontinuity);
		}

		TEST_METHOD(FailedDeliveryDoesNotAdvanceTheSequence)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const RationalLiveOutputTimestampDecision attempted =
				sequencer.Preview(Input());
			const RationalLiveOutputTimestampDecision retry =
				sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(attempted.outputSequence, retry.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(attempted.start, retry.start);
			Assert::IsTrue(sequencer.Commit(retry));
		}

		TEST_METHOD(NewEpochStartsANewDirectShowSegmentSequence)
		{
			RationalLiveOutputSequencer sequencer(24000, 1001, 417083);
			const RationalLiveOutputTimestampDecision first =
				sequencer.Preview(Input(9));
			Assert::IsTrue(sequencer.Commit(first));
			const RationalLiveOutputTimestampDecision next =
				sequencer.Preview(Input(10));
			Assert::AreEqual<uint64_t>(0, next.outputSequence);
			Assert::IsTrue(next.discontinuity);
		}

		TEST_METHOD(SameEpochResetDoesNotRestartTheTimeline)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			sequencer.ResetToEpoch(9);
			const auto next = sequencer.Preview(Input());
			Assert::AreEqual<uint64_t>(1, next.outputSequence);
			Assert::AreEqual<VideoReferenceTime>(first.stop, next.start);
			Assert::IsFalse(next.discontinuity);
		}

		TEST_METHOD(Rational23976RemainsExactAcrossLongCommittedRun)
		{
			RationalLiveOutputSequencer sequencer(24000, 1001, 417083);
			RationalLiveOutputTimestampInput input = Input();
			const uint64_t frames = 10000;
			for (uint64_t index = 0; index < frames; ++index)
			{
				const auto decision = sequencer.Preview(input);
				Assert::IsTrue(sequencer.Commit(decision));
			}
			const auto next = sequencer.Preview(input);
			Assert::AreEqual<VideoReferenceTime>(
				input.presentationLead + VideoTimingController::RationalTimestamp(
					frames, 1001, 24000, input.ppmCorrection),
				next.start);
		}

		TEST_METHOD(Display23976RemainsExactAcrossFourHours)
		{
			RationalLiveOutputSequencer sequencer(24000, 1001, 417083);
			const double displayRate = 24000.0 / 1001.0;
			const auto input = DisplayInput(9, displayRate);
			const uint64_t frames = 345254;
			for (uint64_t index = 0; index < frames; ++index)
			{
				const auto decision = sequencer.Preview(input);
				Assert::IsTrue(sequencer.Commit(decision));
			}
			const auto next = sequencer.Preview(input);
			const VideoReferenceTime expected = input.presentationLead +
				static_cast<VideoReferenceTime>(llround(
					(static_cast<long double>(frames) *
						VideoTimingController::kReferenceTimeTicksPerSecond) /
					displayRate));
			Assert::AreEqual<VideoReferenceTime>(expected, next.start);
		}

		TEST_METHOD(PpmChangeRebasesAtTheLastCommittedStop)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			auto corrected = Input();
			corrected.ppmCorrection = 25;
			const auto next = sequencer.Preview(corrected);
			Assert::AreEqual<VideoReferenceTime>(first.stop, next.start);
			Assert::AreEqual<int64_t>(first.mediaStop, next.mediaStart);
		}

		TEST_METHOD(NormalSceneNormalRebasesAtCommittedStop)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto normal = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(normal));

			const auto scene = sequencer.Preview(DisplayInput());
			Assert::AreEqual<VideoReferenceTime>(normal.stop, scene.start);
			Assert::AreEqual<int64_t>(normal.mediaStop, scene.mediaStart);
			Assert::IsTrue(sequencer.Commit(scene));

			const auto resumed = sequencer.Preview(Input());
			Assert::AreEqual<VideoReferenceTime>(scene.stop, resumed.start);
			Assert::AreEqual<int64_t>(scene.mediaStop, resumed.mediaStart);
			Assert::IsFalse(resumed.discontinuity);
		}

		TEST_METHOD(SourceDiscontinuityIsCarriedWithoutResettingEpoch)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			auto sourceGap = Input();
			sourceGap.sourceDiscontinuity = true;
			const auto marked = sequencer.Preview(sourceGap);
			Assert::IsTrue(marked.discontinuity);
			Assert::AreEqual<VideoReferenceTime>(first.stop, marked.start);
			Assert::IsTrue(sequencer.Commit(marked));
			const auto following = sequencer.Preview(Input());
			Assert::IsFalse(following.discontinuity);
		}

		TEST_METHOD(RendererGapConsumesPresentationButNotMediaSlots)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(DisplayInput());
			Assert::IsTrue(sequencer.Commit(first));
			auto gap = DisplayInput();
			gap.presentationGapSlotsBefore = 1;
			const auto afterGap = sequencer.Preview(gap);
			// 60 Hz timestamps are independently rounded: the skipped interval
			// lands at round(2 * 10,000,000 / 60), not two truncated durations.
			Assert::AreEqual<VideoReferenceTime>(2133333, afterGap.start);
			Assert::AreEqual<int64_t>(first.mediaStop, afterGap.mediaStart);
			Assert::AreEqual<uint32_t>(2, afterGap.presentationSlotsConsumed);
			Assert::IsTrue(sequencer.Commit(afterGap));
			const auto next = sequencer.Preview(DisplayInput());
			Assert::AreEqual<VideoReferenceTime>(afterGap.stop, next.start);
			Assert::AreEqual<int64_t>(afterGap.mediaStop, next.mediaStart);
		}

		TEST_METHOD(RendererGapOnCadenceRebaseDoesNotCreateASecondGap)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto normal = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(normal));

			auto sceneGap = DisplayInput();
			sceneGap.presentationGapSlotsBefore = 1;
			const auto afterGap = sequencer.Preview(sceneGap);
			Assert::IsTrue(afterGap.start > normal.stop);
			Assert::IsTrue(sequencer.Commit(afterGap));

			const auto following = sequencer.Preview(DisplayInput());
			Assert::AreEqual<VideoReferenceTime>(afterGap.stop, following.start);
			Assert::AreEqual<int64_t>(afterGap.mediaStop, following.mediaStart);
		}

		TEST_METHOD(FailedScenePreviewDoesNotAdvanceTheUnifiedTimeline)
		{
			RationalLiveOutputSequencer sequencer(60000, 1001, 166833);
			const auto first = sequencer.Preview(Input());
			Assert::IsTrue(sequencer.Commit(first));
			const auto attempted = sequencer.Preview(DisplayInput());
			const auto retry = sequencer.Preview(DisplayInput());
			Assert::AreEqual<VideoReferenceTime>(attempted.start, retry.start);
			Assert::AreEqual<int64_t>(attempted.mediaStart, retry.mediaStart);
			Assert::IsTrue(sequencer.Commit(retry));
		}
	};
}
