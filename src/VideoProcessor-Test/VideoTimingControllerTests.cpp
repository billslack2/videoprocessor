#include "pch.h"
#include "CppUnitTest.h"

#include <VideoTimingController.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(VideoTimingControllerTests)
	{
	public:
		static VideoTimingController MakeController(
			VideoTimingMode mode, uint32_t timeScale, uint32_t frameDurationTicks)
		{
			VideoTimingControllerConfig config;
			config.mode = mode;
			config.timeScale = timeScale;
			config.frameDurationTicks = frameDurationTicks;
			config.theoreticalFrameDuration = static_cast<VideoReferenceTime>(
				(VideoTimingController::kReferenceTimeTicksPerSecond * frameDurationTicks) /
				timeScale);
			return VideoTimingController(config);
		}

		TEST_METHOD(Rational24000Over1001AccumulatesWithoutDriftForFourHours)
		{
			auto controller = MakeController(
				VideoTimingMode::RationalRational, 24000, 1001);
			const uint64_t frameCount = 345255; // 4 h plus 10.625 ms at 24000/1001.
			FrameTimingInput input;
			input.sourceFrameNumber = 0;
			(void)controller.Decide(input);
			input.sourceFrameNumber = frameCount;
			const TimingDecision decision = controller.Decide(input);

			Assert::AreEqual<VideoReferenceTime>(144000106250LL, decision.start);
			Assert::AreEqual<VideoReferenceTime>(144000523333LL, decision.stop);
			Assert::IsTrue(decision.stop > decision.start);
		}

		TEST_METHOD(Rational60000Over1001UsesExactRoundedFrameTimes)
		{
			auto controller = MakeController(
				VideoTimingMode::RationalRational, 60000, 1001);
			FrameTimingInput first;
			first.sourceFrameNumber = 100;
			const TimingDecision initial = controller.Decide(first);
			Assert::AreEqual<VideoReferenceTime>(0, initial.start);
			Assert::AreEqual<VideoReferenceTime>(166833, initial.stop);

			FrameTimingInput second;
			second.sourceFrameNumber = 101;
			const TimingDecision next = controller.Decide(second);
			// The established source-pin policy enforces strict monotonic starts,
			// so a rational stop/start tie is represented as a one-tick gap.
			Assert::AreEqual<VideoReferenceTime>(166834, next.start);
			Assert::AreEqual<VideoReferenceTime>(333667, next.stop);
		}

		TEST_METHOD(RationalPpmCorrectionMovesTimelineInBothDirections)
		{
			constexpr uint64_t frame = 60000;
			const VideoReferenceTime nominal = VideoTimingController::RationalTimestamp(
				frame, 1001, 60000, 0);
			const VideoReferenceTime slower = VideoTimingController::RationalTimestamp(
				frame, 1001, 60000, 17);
			const VideoReferenceTime faster = VideoTimingController::RationalTimestamp(
				frame, 1001, 60000, -17);

			Assert::IsTrue(slower > nominal);
			Assert::IsTrue(faster < nominal);
		}

		TEST_METHOD(ClockRationalClampsBackwardHardwareTimeMonotonically)
		{
			auto controller = MakeController(
				VideoTimingMode::ClockRational, 60000, 1001);
			FrameTimingInput first;
			first.sourceFrameNumber = 1;
			first.hasHardwareTimestamp = true;
			first.hardwareTimestamp = 1000000;
			first.timingClockTicksPerSecond = 1000000;
			const TimingDecision firstDecision = controller.Decide(first);

			FrameTimingInput backwards = first;
			backwards.sourceFrameNumber = 2;
			backwards.hardwareTimestamp = 999000;
			const TimingDecision secondDecision = controller.Decide(backwards);

			Assert::IsTrue(secondDecision.start > firstDecision.start);
			Assert::IsTrue(secondDecision.stop > secondDecision.start);
			Assert::AreEqual<uint32_t>(1, secondDecision.hardwareTimestampAnomalyCount);
		}

		TEST_METHOD(ClockClockFallsBackWhenNextTimestampIsUnavailable)
		{
			auto controller = MakeController(
				VideoTimingMode::ClockClock, 60000, 1001);
			FrameTimingInput frame;
			frame.sourceFrameNumber = 1;
			frame.hasHardwareTimestamp = true;
			frame.hardwareTimestamp = 1000000;
			frame.timingClockTicksPerSecond = 1000000;
			const TimingDecision decision = controller.Decide(frame);

			Assert::AreEqual<VideoReferenceTime>(0, decision.start);
			Assert::AreEqual<VideoReferenceTime>(166833, decision.stop);
		}

		TEST_METHOD(ClockSmart2UsesSmoothedMeasuredDuration)
		{
			auto controller = MakeController(
				VideoTimingMode::ClockSmart2, 60000, 1001);
			FrameTimingInput first = { 1, 1000000, 1000000, true };
			(void)controller.Decide(first);
			FrameTimingInput second = { 2, 1017000, 1000000, true };
			const TimingDecision secondDecision = controller.Decide(second);
			FrameTimingInput third = { 3, 1035000, 1000000, true };
			const TimingDecision thirdDecision = controller.Decide(third);

			Assert::AreEqual<VideoReferenceTime>(166833, secondDecision.stop - secondDecision.start);
			Assert::AreEqual<VideoReferenceTime>(180000, thirdDecision.stop - thirdDecision.start);
		}

		TEST_METHOD(ResetReplacesEpochAndForcesDiscontinuity)
		{
			auto controller = MakeController(
				VideoTimingMode::RationalRational, 24000, 1001);
			FrameTimingInput first;
			first.sourceFrameNumber = 12;
			const TimingDecision before = controller.Decide(first);
			controller.Reset();
			const TimingDecision after = controller.Decide(first);

			Assert::IsTrue(after.epoch.value > before.epoch.value);
			Assert::IsTrue(after.discontinuity);
			Assert::AreEqual<VideoReferenceTime>(0, after.start);
		}

		TEST_METHOD(OnlyAndNoneModesExposeTheCorrectTimestampShape)
		{
			auto clockOnly = MakeController(
				VideoTimingMode::ClockOnly, 60000, 1001);
			FrameTimingInput clock = { 1, 1000000, 1000000, true };
			const TimingDecision clockDecision = clockOnly.Decide(clock);
			Assert::IsTrue(clockDecision.hasStart);
			Assert::IsFalse(clockDecision.hasStop);

			auto none = MakeController(VideoTimingMode::None, 60000, 1001);
			const TimingDecision noTimestamp = none.Decide({ 1 });
			Assert::IsFalse(noTimestamp.hasStart);
			Assert::IsFalse(noTimestamp.hasStop);
		}
	};
}
