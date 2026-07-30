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
	};
}
