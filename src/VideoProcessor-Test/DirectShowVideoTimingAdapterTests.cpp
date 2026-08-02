#include "pch.h"
#include "CppUnitTest.h"

#include <microsoft_directshow/DirectShowVideoTimingAdapter.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DirectShowVideoTimingAdapterTests)
	{
	public:
		TEST_METHOD(PreservesEveryLegacyTimestampModeName)
		{
			Assert::AreEqual(static_cast<int>(VideoTimingMode::ClockSmart),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_CLOCK_SMART)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::ClockTheoretical),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_CLOCK_THEO)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::ClockClock),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_CLOCK_CLOCK)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::TheoreticalTheoretical),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_THEO_THEO)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::RationalRational),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_RATIONAL_RATIONAL)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::ClockRational),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_CLOCK_RATIONAL)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::ClockSmart2),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_CLOCK_SMART2)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::ClockOnly),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_CLOCK_NONE)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::TheoreticalOnly),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_THEO_NONE)));
			Assert::AreEqual(static_cast<int>(VideoTimingMode::None),
				static_cast<int>(DirectShowVideoTimingAdapter::ToVideoTimingMode(DS_SSTM_NONE)));
		}

		TEST_METHOD(AppliesPresentationLeadOnlyToTheLegacyModesThatUseIt)
		{
			DirectShowVideoTimingAdapter rational(
				DS_SSTM_RATIONAL_RATIONAL, 60000, 1001, 166833);
			DirectShowFrameTimingInput input;
			input.timing.sourceFrameNumber = 10;
			input.presentationLead = 400000;
			const DirectShowTimingDecision rationalDecision = rational.Decide(input);
			Assert::AreEqual<VideoReferenceTime>(400000, rationalDecision.start);
			Assert::AreEqual<VideoReferenceTime>(566833, rationalDecision.stop);

			DirectShowVideoTimingAdapter theoretical(
				DS_SSTM_THEO_THEO, 60000, 1001, 166833);
			const DirectShowTimingDecision theoreticalDecision = theoretical.Decide(input);
			Assert::AreEqual<VideoReferenceTime>(0, theoreticalDecision.start);
			Assert::AreEqual<VideoReferenceTime>(166833, theoreticalDecision.stop);
		}

		TEST_METHOD(PreservesClockClockReferenceTimeInputAndExternalEpoch)
		{
			DirectShowVideoTimingAdapter adapter(
				DS_SSTM_CLOCK_CLOCK, 60000, 1001, 166833);
			adapter.ResetToEpoch({ 9 });
			DirectShowFrameTimingInput input;
			input.timing.sourceFrameNumber = 4;
			input.timing.hasHardwareTimestamp = true;
			input.timing.hardwareTimestamp = 1000000;
			input.timing.timingClockTicksPerSecond = 1000000;
			input.timing.hasNextReferenceTime = true;
			input.timing.nextReferenceTime = 10170000;
			const DirectShowTimingDecision decision = adapter.Decide(input);

			Assert::AreEqual<uint64_t>(9, decision.base.epoch.value);
			Assert::AreEqual<VideoReferenceTime>(170000, decision.stop);
		}

		TEST_METHOD(ReplaysTheTimestampShapeOfEveryLegacyModeWithoutAGraph)
		{
			const DirectShowStartStopTimeMethod modes[] = {
				DS_SSTM_CLOCK_SMART, DS_SSTM_CLOCK_THEO, DS_SSTM_CLOCK_CLOCK,
				DS_SSTM_THEO_THEO, DS_SSTM_RATIONAL_RATIONAL, DS_SSTM_CLOCK_RATIONAL,
				DS_SSTM_CLOCK_SMART2, DS_SSTM_CLOCK_NONE, DS_SSTM_THEO_NONE,
				DS_SSTM_NONE
			};
			for (const auto mode : modes)
			{
				DirectShowVideoTimingAdapter adapter(mode, 60000, 1001, 166833);
				DirectShowFrameTimingInput input;
				input.timing.sourceFrameNumber = 500;
				input.timing.hasHardwareTimestamp = true;
				input.timing.hardwareTimestamp = 1000000;
				input.timing.timingClockTicksPerSecond = 1000000;
				const DirectShowTimingDecision decision = adapter.Decide(input);

				Assert::IsTrue(decision.base.valid);
				Assert::IsTrue(decision.base.discontinuity);
				Assert::AreEqual<int64_t>(0, decision.base.mediaStart);
				Assert::AreEqual<int64_t>(1, decision.base.mediaStop);
				Assert::AreEqual(mode != DS_SSTM_NONE, decision.hasStart);
				Assert::AreEqual(
					mode != DS_SSTM_CLOCK_NONE && mode != DS_SSTM_THEO_NONE &&
					mode != DS_SSTM_NONE,
					decision.hasStop);
			}
		}

		TEST_METHOD(LiveCatchUpClassificationExcludesFinalDeliveryOwnedRationalMode)
		{
			Assert::IsTrue(DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(
				DS_SSTM_CLOCK_SMART));
			Assert::IsTrue(DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(
				DS_SSTM_CLOCK_SMART2));
			Assert::IsTrue(DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(
				DS_SSTM_CLOCK_THEO));
			Assert::IsFalse(DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(
				DS_SSTM_RATIONAL_RATIONAL));
			Assert::IsFalse(DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(
				DS_SSTM_NONE));
		}

		TEST_METHOD(LiveCatchUpRemovesOnlyTheDiscardedStartupTimestampSpan)
		{
			DirectShowLiveTimestampCatchUp catchUp;
			const uint64_t epoch = 7;
			catchUp.ResetToEpoch(epoch);
			catchUp.CommitSuccessfulStop(epoch, 2166833);
			catchUp.Arm(epoch);

			// Thirty discarded 59.94-Hz samples left the next pre-stamped sample
			// approximately 500 ms in the future. It must immediately follow the
			// last successful delivery, with the correction persisting thereafter.
			DirectShowLiveCatchUpDecision decision = catchUp.Adjust(
				epoch, 7171833, 7338666);
			Assert::IsTrue(decision.rebased);
			Assert::AreEqual<VideoReferenceTime>(2166833, decision.start);
			Assert::AreEqual<VideoReferenceTime>(2333666, decision.stop);
			Assert::AreEqual<VideoReferenceTime>(-5005000, decision.offset);
			catchUp.CommitSuccessfulStop(epoch, decision.stop);

			decision = catchUp.Adjust(epoch, 7338666, 7505499);
			Assert::IsFalse(decision.rebased);
			Assert::AreEqual<VideoReferenceTime>(2333666, decision.start);
			Assert::AreEqual<VideoReferenceTime>(2500499, decision.stop);
		}

		TEST_METHOD(LiveCatchUpNeverCarriesAcrossGraphEpochs)
		{
			DirectShowLiveTimestampCatchUp catchUp;
			catchUp.CommitSuccessfulStop(3, 2000000);
			catchUp.Arm(3);
			(void)catchUp.Adjust(3, 7000000, 7166833);

			const DirectShowLiveCatchUpDecision nextEpoch =
				catchUp.Adjust(4, 100000, 266833);
			Assert::IsFalse(nextEpoch.adjusted);
			Assert::IsFalse(nextEpoch.rebased);
			Assert::AreEqual<VideoReferenceTime>(100000, nextEpoch.start);
		}

		TEST_METHOD(LiveCatchUpCanSpliceConvertedTrimThenLaterRawTrim)
		{
			DirectShowLiveTimestampCatchUp catchUp;
			const uint64_t epoch = 11;
			catchUp.CommitSuccessfulStop(epoch, 2000000);
			catchUp.Arm(epoch);
			DirectShowLiveCatchUpDecision decision =
				catchUp.Adjust(epoch, 7000000, 7166833);
			Assert::AreEqual<VideoReferenceTime>(2000000, decision.start);
			catchUp.CommitSuccessfulStop(epoch, decision.stop);

			decision = catchUp.Adjust(epoch, 7166833, 7333666);
			Assert::AreEqual<VideoReferenceTime>(2166833, decision.start);
			catchUp.CommitSuccessfulStop(epoch, decision.stop);

			// A later hardware-clock jump represents raw source frames removed by
			// the same convergence transaction. Re-arming joins that boundary too.
			catchUp.Arm(epoch);
			decision = catchUp.Adjust(epoch, 7667332, 7834165);
			Assert::IsTrue(decision.rebased);
			Assert::AreEqual<VideoReferenceTime>(2333666, decision.start);
			Assert::AreEqual<VideoReferenceTime>(2500499, decision.stop);
		}
	};
}
