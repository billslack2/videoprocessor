#include "pch.h"
#include "CppUnitTest.h"

#include <VideoTimingController.h>
#include <microsoft_directshow/live_source_filter/ALiveSourceVideoOutputPin.h>

#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
	// Exercise the production source-pin helper without creating a graph.
	class SourcePinClockConversion : public ALiveSourceVideoOutputPin
	{
	public:
		using ALiveSourceVideoOutputPin::ConvertTimingClockToReferenceTime;
	};

	void AssertBothConversions(uint64_t timestamp, uint64_t ticksPerSecond,
		VideoReferenceTime expected)
	{
		Assert::AreEqual<VideoReferenceTime>(expected,
			VideoTimingController::ClockToReferenceTime(timestamp, ticksPerSecond));
		Assert::AreEqual<REFERENCE_TIME>(expected,
			SourcePinClockConversion::ConvertTimingClockToReferenceTime(
				static_cast<timingclocktime_t>(timestamp),
				static_cast<timingclocktime_t>(ticksPerSecond)));
	}
}

namespace Tests
{
	TEST_CLASS(CaptureClockConversionTests)
	{
	public:
		TEST_METHOD(PreservesEveryMicrosecondAcrossTheOldMultiplyOverflowBoundary)
		{
			const uint64_t oldBoundary =
				static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) / 10000000ULL;
			for (const uint64_t timestamp :
				{ oldBoundary - 1, oldBoundary, oldBoundary + 1, oldBoundary + 16683 })
				AssertBothConversions(timestamp, 1000000,
					static_cast<VideoReferenceTime>(timestamp * 10));
		}

		TEST_METHOD(PreservesSubsecondPrecisionAfterThirtyDaysAtEachClockFrequency)
		{
			constexpr uint64_t seconds = 30ULL * 24 * 60 * 60;
			for (const uint64_t frequency :
				{ 1000ULL, 1000000ULL, 10000000ULL, 1000000000ULL })
			{
				const uint64_t remainder = frequency / 3 + 1;
				const uint64_t timestamp = seconds * frequency + remainder;
				// This independent quotient/remainder oracle never multiplies the
				// multi-day absolute timestamp by the reference-time frequency.
				const VideoReferenceTime expected = static_cast<VideoReferenceTime>(
					seconds * 10000000ULL +
					(remainder * 10000000ULL + frequency / 2) / frequency);
				AssertBothConversions(timestamp, frequency, expected);
			}
		}

		TEST_METHOD(RetainsEstablishedNearestTickRounding)
		{
			AssertBothConversions(1, 30000000, 0);
			AssertBothConversions(2, 30000000, 1);
			AssertBothConversions(1, 20000000, 1);
			AssertBothConversions(3, 20000000, 2);
			AssertBothConversions(0, 1000000, 0);
			AssertBothConversions(12345, 0, 0);
		}

		TEST_METHOD(ConvertsTheLargestRepresentableReferenceTimeExactly)
		{
			const VideoReferenceTime maximum =
				(std::numeric_limits<VideoReferenceTime>::max)();
			AssertBothConversions(static_cast<uint64_t>(maximum), 10000000, maximum);
		}

		TEST_METHOD(ClockSmartKeepsFrameStartsDistinctAfterThirtyDays)
		{
			VideoTimingControllerConfig config;
			config.mode = VideoTimingMode::ClockSmart;
			config.timeScale = 60000;
			config.frameDurationTicks = 1001;
			config.theoreticalFrameDuration = 166833;
			VideoTimingController controller(config);
			constexpr uint64_t baseline = 30ULL * 24 * 60 * 60 * 1000000;
			FrameTimingInput input;
			input.hasHardwareTimestamp = true;
			input.timingClockTicksPerSecond = 1000000;

			VideoReferenceTime previous = -1;
			for (uint64_t frame = 0; frame < 120; ++frame)
			{
				input.sourceFrameNumber = frame;
				input.hardwareTimestamp = baseline + frame * 16683;
				const TimingDecision decision = controller.Decide(input);
				Assert::IsTrue(decision.valid);
				Assert::AreEqual<VideoReferenceTime>(
					static_cast<VideoReferenceTime>(frame * 166830), decision.start);
				Assert::IsTrue(decision.start > previous);
				Assert::AreEqual<VideoReferenceTime>(166833, decision.stop - decision.start);
				previous = decision.start;
			}
		}
	};
}