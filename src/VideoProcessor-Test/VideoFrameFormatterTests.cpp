#include "pch.h"
#include "CppUnitTest.h"

#include <algorithm>

#include <video_frame_formatter/CNoopVideoFrameFormatter.h>
#include <video_frame_formatter/CFFMpegDecoderVideoFrameFormatter.h>
#include <video_frame_formatter/CR12BtoRGB48VideoFrameFormatter.h>
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
				AV_PIX_FMT_RGB48LE,
				false /* hardware decoding */);

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(128, 100, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R210;

			vff.OnVideoState(vs);
			vff.OnVideoState(vs);

			Assert::AreEqual(76800L, vff.GetOutFrameSize());

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			// R210 is a big-endian 32-bit word: padding:2, R:10, G:10, B:10.
			// The first pixel below is R=1, G=2, B=3; the remaining padded rows are black.
			input[0] = 0x00;
			input[1] = 0x10;
			input[2] = 0x08;
			input[3] = 0x03;
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			Assert::AreEqual(static_cast<BYTE>(0x40), output[0]);
			Assert::AreEqual(static_cast<BYTE>(0x00), output[1]);
			Assert::AreEqual(static_cast<BYTE>(0x80), output[2]);
			Assert::AreEqual(static_cast<BYTE>(0x00), output[3]);
			Assert::AreEqual(static_cast<BYTE>(0xC0), output[4]);
			Assert::AreEqual(static_cast<BYTE>(0x00), output[5]);
		}

		TEST_METHOD(CR12BtoRGB48VideoFrameFormatterGoldenBlockTest)
		{
			CR12BtoRGB48VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(104, 100, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;

			vff.OnVideoState(vs);

			Assert::AreEqual(62400L, vff.GetOutFrameSize());

			const BYTE input[] = {
				0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
				0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
				0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
				0x20, 0x21, 0x22, 0x23
			};
			const BYTE expected[] = {
				0x32, 0x20, 0x00, 0x01, 0x07, 0x70, 0x00, 0x06,
				0x54, 0x40, 0x00, 0x0B, 0xA9, 0x90, 0x00, 0x08,
				0xFE, 0xE0, 0x00, 0x0D, 0xC3, 0x30, 0x11, 0x12,
				0x10, 0x01, 0x11, 0x17, 0x65, 0x51, 0x11, 0x14,
				0xBA, 0xA1, 0x11, 0x19, 0x8F, 0xF1, 0x11, 0x1E,
				0xDC, 0xC1, 0x12, 0x23, 0x21, 0x12, 0x22, 0x20
			};
			std::vector<BYTE> inputFrame(104 * 100 * 36 / 8, 0);
			memcpy(inputFrame.data(), input, sizeof(input));
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(inputFrame.data(), 1, 0, nullptr);

			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			for (size_t i = 0; i < sizeof(expected); ++i)
				Assert::AreEqual(expected[i], output[i]);
		}

		TEST_METHOD(CR12BtoRGB48VideoFrameFormatterBlackWhiteTest)
		{
			CR12BtoRGB48VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(104, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(104 * 100 * 36 / 8, 0);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame blackFrame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(blackFrame, output.data()));
			for (BYTE value : output)
				Assert::AreEqual(static_cast<BYTE>(0), value);

			memset(input.data(), 0xFF, input.size());
			VideoFrame whiteFrame(input.data(), 2, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(whiteFrame, output.data()));
			for (BYTE value : output)
				Assert::AreEqual(static_cast<BYTE>(0xFF), value);
		}

		TEST_METHOD(CR12BtoRGB48VideoFrameFormatterRejectsInvalidWidth)
		{
			CR12BtoRGB48VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(101, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;

			Assert::ExpectException<std::runtime_error>([&]() { vff.OnVideoState(vs); });
		}

		TEST_METHOD(CR12BtoRGB48VideoFrameFormatter4KSmokeTest)
		{
			CR12BtoRGB48VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(3840, 2160, false, 60000, 1001);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			// Warm the reusable worker, then sample enough frames to expose scheduling spikes.
			for (int i = 0; i < 5; ++i)
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			for (int i = 0; i < 30; ++i)
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			// Checking the whole frame also covers the split-row boundary used by the worker.
			Assert::IsTrue(std::all_of(output.begin(), output.end(),
				[](BYTE value) { return value == 0; }));

			double currentUs = 0.0;
			double averageUs = 0.0;
			double maximumUs = 0.0;
			vff.GetConversionPerformance(currentUs, averageUs, maximumUs);
			wchar_t message[128];
			swprintf_s(message, L"Native R12B 4K conversion current/avg/max: %.0f / %.0f / %.0f us",
				currentUs, averageUs, maximumUs);
			Logger::WriteMessage(message);
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

			// Frame 1: 10,000,000 * 1001 / 24000 = 417083.333...
			uint64_t t1 = U64_MulDiv(1, ticksPerSec, 1);
			t1 = U64_MulDiv(t1, 1001, 24000);
			Assert::AreEqual(417083ULL, t1);

			// Frame 2: 20,000,000 * 1001 / 24000 = 834166.666...
			uint64_t t2 = U64_MulDiv(2, ticksPerSec, 1);
			t2 = U64_MulDiv(t2, 1001, 24000);
			Assert::AreEqual(834167ULL, t2);
		}
	};
}
