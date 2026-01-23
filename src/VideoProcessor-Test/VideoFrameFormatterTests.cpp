#include "pch.h"
#include "CppUnitTest.h"

#include <video_frame_formatter/CNoopVideoFrameFormatter.h>
#include <video_frame_formatter/CFFMpegDecoderVideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP210VideoFrameFormatter.h>
#include <IntegerMath.h>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(VideoFrameFormatterTests)
	{
	public:

		TEST_METHOD(CNoopVideoFrameFormatterTest)
		{
			CNoopVideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1920, 1080, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;  // Actual type not important

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(5529600L, vff.GetOutFrameSize());
		}

		TEST_METHOD(CV210toP010VideoFrameFormatterTest)
		{
			CV210toP010VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1920, 1080, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;  // Actual type not important

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(6220800L, vff.GetOutFrameSize());
		}

		TEST_METHOD(CV210toP210VideoFrameFormatterTest)
		{
			CV210toP210VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1920, 1080, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;  // Actual type not important

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(8294400L, vff.GetOutFrameSize());
		}

		TEST_METHOD(CFFMpegDecoderVideoFrameFormatterR210RGB48LETest)
		{
			CFFMpegDecoderVideoFrameFormatter vff(
				AV_CODEC_ID_R210,
				AV_PIX_FMT_RGB48LE);

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1920, 1080, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;  // Actual type not important

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(12441600L, vff.GetOutFrameSize());
		}

		TEST_METHOD(CFFMpegDecoderVideoFrameFormatterR12BRGB48LETest)
		{
			CFFMpegDecoderVideoFrameFormatter vff(
				AV_CODEC_ID_R12B,
				AV_PIX_FMT_RGB48LE);

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1920, 1080, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;  // Actual type not important

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(12441600L, vff.GetOutFrameSize());
		}
	};

	TEST_CLASS(U64MulDivTests)
	{
	public:

		TEST_METHOD(U64MulDivBasicRoundingTest)
		{
			// Test basic rounding behavior
			// 7 / 2 = 3.5, should round to 4
			Assert::AreEqual(4ULL, U64_MulDiv(7, 1, 2));
			
			// 6 / 2 = 3.0, should remain 3
			Assert::AreEqual(3ULL, U64_MulDiv(6, 1, 2));
			
			// 5 / 2 = 2.5, should round to 3
			Assert::AreEqual(3ULL, U64_MulDiv(5, 1, 2));
			
			// 4 / 2 = 2.0, should remain 2  
			Assert::AreEqual(2ULL, U64_MulDiv(4, 1, 2));
		}

		TEST_METHOD(U64MulDivZeroDivisorTest)
		{
			// Test zero divisor handling
			Assert::AreEqual(0ULL, U64_MulDiv(100, 50, 0));
		}

		TEST_METHOD(U64MulDivExactDivisionTest)
		{
			// Test exact divisions (no rounding needed)
			Assert::AreEqual(50ULL, U64_MulDiv(100, 1, 2));
			Assert::AreEqual(25ULL, U64_MulDiv(100, 1, 4));
			Assert::AreEqual(10ULL, U64_MulDiv(100, 1, 10));
		}

		TEST_METHOD(U64MulDivPPMTimingTest)
		{
			// Test PPM timing correction scenarios
			// Simulate 1 million ticks with 6 PPM correction: 1000006 / 1000000
			// Should be very close to 1000006 but rounded properly
			uint64_t result = U64_MulDiv(1000000, 1000006, 1000000);
			Assert::AreEqual(1000006ULL, result);

			// Test fractional PPM correction
			// 1000000 * 1000003 / 1000000 = 1000003.000, exact
			result = U64_MulDiv(1000000, 1000003, 1000000);
			Assert::AreEqual(1000003ULL, result);

			// Test case where rounding matters for PPM
			// Simulate: frameIndex * ticksPerSec / timeScale with PPM
			// 1 * 10000000 * 1001 / 24000 / 1000000 * 1000006
			// This creates fractional values where rounding is critical
			uint64_t frameIndex = 1;
			uint64_t ticksPerSec = 10000000;
			uint64_t frameDurationTicks = 1001;
			uint64_t timeScale = 24000;
			uint64_t trimNum = 1000006;
			uint64_t trimDen = 1000000;

			uint64_t t = frameIndex;
			t = U64_MulDiv(t, ticksPerSec, 1); // t = 10000000
			t = U64_MulDiv(t, frameDurationTicks, timeScale); // t ? 417083
			t = U64_MulDiv(t, trimNum, trimDen); // Apply PPM correction

			// Verify we get a reasonable result (exact value depends on rounding)
			// But should be close to 417083 * 1.000006 ? 417085
			Assert::IsTrue(t >= 417084ULL && t <= 417086ULL);
		}

		TEST_METHOD(U64MulDivLargeNumberTest)
		{
			// Test with large numbers to verify no overflow
			uint64_t large = 0x100000000ULL; // 2^32
			uint64_t result = U64_MulDiv(large, large, large);
			Assert::AreEqual(large, result);

			// Test near overflow conditions
			uint64_t veryLarge = 0x7FFFFFFFFFFFFFFFULL / 1000; // Close to max / 1000
			result = U64_MulDiv(veryLarge, 999, 1000);
			// Should be approximately veryLarge - veryLarge/1000
			Assert::IsTrue(result > 0);
		}

		TEST_METHOD(U64MulDivTimingAccuracyTest)
		{
			// Test timing accuracy for common video frame rates
			// 23.976 fps: timeScale=24000, frameDurationTicks=1001
			uint64_t ticksPerSec = 10000000; // 100ns ticks per second

			// Frame 0: should be 0
			uint64_t t0 = U64_MulDiv(0, ticksPerSec, 1);
			t0 = U64_MulDiv(t0, 1001, 24000);
			Assert::AreEqual(0ULL, t0);

			// Frame 1: should be ~417083 (exact: 417083.75)
			uint64_t t1 = U64_MulDiv(1, ticksPerSec, 1);
			t1 = U64_MulDiv(t1, 1001, 24000);
			// With rounding, 417083.75 should round to 417084
			Assert::AreEqual(417084ULL, t1);

			// Frame 2: should be ~834167 (exact: 834167.5)
			uint64_t t2 = U64_MulDiv(2, ticksPerSec, 1);
			t2 = U64_MulDiv(t2, 1001, 24000);
			// With rounding, 834167.5 should round to 834168
			Assert::AreEqual(834168ULL, t2);
		}
	};
}
