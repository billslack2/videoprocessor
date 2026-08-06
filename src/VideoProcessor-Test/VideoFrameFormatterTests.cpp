#include "pch.h"
#include "CppUnitTest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <numeric>

#include <video_frame_formatter/CNoopVideoFrameFormatter.h>
#include <video_frame_formatter/CARGBtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CDeckLinkRGBToP010VideoFrameFormatter.h>
#include <video_frame_formatter/CR210toRGB48VideoFrameFormatter.h>
#include <video_frame_formatter/CR12BtoRGB48VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP210VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP210VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP010VideoFrameFormatter.h>
#include <vprenderer/AlphaNativeRgbIngress.h>
#include <microsoft_directshow/video_renderers/MadVRIngressPolicy.h>
#include <microsoft_directshow/video_renderers/DirectShowIngressPolicy.h>
#include <microsoft_directshow/DirectShowTranslations.h>
#include <IntegerMath.h>
#include <AspectRatio.h>
#include <DisplayRuleExpression.h>
#include <RendererProfileConfig.h>
#include <UnifiedProfileRuntime.h>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	namespace
	{
		struct ConverterBenchmarkStatistics
		{
			double averageUs = 0.0;
			double medianUs = 0.0;
			double p95Us = 0.0;
			double p99Us = 0.0;
			double maximumUs = 0.0;
			double framesPerSecond = 0.0;
		};

		struct ConverterBenchmarkComparison
		{
			ConverterBenchmarkStatistics hotBuffer;
			ConverterBenchmarkStatistics rotatingBuffers;
		};

		double BenchmarkPercentile(const std::vector<double>& sortedSamples,
			double percentile)
		{
			const size_t index = static_cast<size_t>(std::ceil(
				percentile * static_cast<double>(sortedSamples.size()))) - 1;
			return sortedSamples[std::min(index, sortedSamples.size() - 1)];
		}

		ConverterBenchmarkStatistics MeasureFormatterPerformance(
			IVideoFrameFormatter& formatter,
			const std::vector<VideoFrame>& frames,
			std::vector<std::vector<BYTE>>& outputs,
			size_t warmupFrames,
			size_t measuredFrames)
		{
			for (size_t i = 0; i < warmupFrames; ++i)
			{
				const size_t bufferIndex = i % frames.size();
				Assert::IsTrue(formatter.FormatVideoFrame(
					frames[bufferIndex], outputs[bufferIndex].data()));
			}

			std::vector<double> samples;
			samples.reserve(measuredFrames);
			for (size_t i = 0; i < measuredFrames; ++i)
			{
				const size_t bufferIndex = i % frames.size();
				const auto start = std::chrono::steady_clock::now();
				const bool succeeded = formatter.FormatVideoFrame(
					frames[bufferIndex], outputs[bufferIndex].data());
				const auto finish = std::chrono::steady_clock::now();
				Assert::IsTrue(succeeded);
				samples.push_back(std::chrono::duration<double, std::micro>(
					finish - start).count());
			}

			ConverterBenchmarkStatistics result;
			result.averageUs = std::accumulate(samples.begin(), samples.end(), 0.0) /
				static_cast<double>(samples.size());
			std::sort(samples.begin(), samples.end());
			result.medianUs = BenchmarkPercentile(samples, 0.50);
			result.p95Us = BenchmarkPercentile(samples, 0.95);
			result.p99Us = BenchmarkPercentile(samples, 0.99);
			result.maximumUs = samples.back();
			result.framesPerSecond = 1000000.0 / result.averageUs;
			return result;
		}

		void FillBenchmarkPattern(std::vector<BYTE>& buffer, uint32_t seed)
		{
			uint32_t value = seed;
			for (BYTE& sample : buffer)
			{
				value = value * 1664525U + 1013904223U;
				sample = static_cast<BYTE>(value >> 24);
			}
		}

		void LogBenchmarkResult(const wchar_t* converter,
			const wchar_t* workload,
			const ConverterBenchmarkStatistics& result)
		{
			wchar_t message[320];
			swprintf_s(message,
				L"PERF|%s|%s|avg=%.0f|median=%.0f|p95=%.0f|p99=%.0f|max=%.0f|fps=%.1f",
				converter, workload, result.averageUs, result.medianUs,
				result.p95Us, result.p99Us, result.maximumUs,
				result.framesPerSecond);
			Logger::WriteMessage(message);
		}

		double MedianBenchmarkValue(std::vector<double> values)
		{
			std::sort(values.begin(), values.end());
			return values[values.size() / 2];
		}

		ConverterBenchmarkStatistics MedianBenchmarkStatistics(
			const std::vector<ConverterBenchmarkStatistics>& runs)
		{
			std::vector<double> averages;
			std::vector<double> medians;
			std::vector<double> p95s;
			std::vector<double> p99s;
			std::vector<double> maxima;
			for (const auto& run : runs)
			{
				averages.push_back(run.averageUs);
				medians.push_back(run.medianUs);
				p95s.push_back(run.p95Us);
				p99s.push_back(run.p99Us);
				maxima.push_back(run.maximumUs);
			}

			ConverterBenchmarkStatistics result;
			result.averageUs = MedianBenchmarkValue(averages);
			result.medianUs = MedianBenchmarkValue(medians);
			result.p95Us = MedianBenchmarkValue(p95s);
			result.p99Us = MedianBenchmarkValue(p99s);
			result.maximumUs = MedianBenchmarkValue(maxima);
			result.framesPerSecond = 1000000.0 / result.averageUs;
			return result;
		}

		ConverterBenchmarkComparison CompareFormatterPerformanceOnce(
			IVideoFrameFormatter& formatter,
			size_t inputBytes,
			size_t outputBytes)
		{
			std::vector<std::vector<BYTE>> hotInputs(1,
				std::vector<BYTE>(inputBytes, 0));
			std::vector<std::vector<BYTE>> hotOutputs(1,
				std::vector<BYTE>(outputBytes, 0));
			std::vector<VideoFrame> hotFrames;
			hotFrames.emplace_back(hotInputs[0].data(), 1, 0, nullptr);

			ConverterBenchmarkComparison comparison;
			comparison.hotBuffer = MeasureFormatterPerformance(formatter,
				hotFrames, hotOutputs, 5, 30);

			constexpr size_t rotatingBufferCount = 4;
			std::vector<std::vector<BYTE>> rotatingInputs;
			std::vector<std::vector<BYTE>> rotatingOutputs;
			std::vector<VideoFrame> rotatingFrames;
			rotatingInputs.reserve(rotatingBufferCount);
			rotatingOutputs.reserve(rotatingBufferCount);
			rotatingFrames.reserve(rotatingBufferCount);
			for (size_t i = 0; i < rotatingBufferCount; ++i)
			{
				rotatingInputs.emplace_back(inputBytes);
				FillBenchmarkPattern(rotatingInputs.back(),
					0x9e3779b9U ^ static_cast<uint32_t>(i * 0x45d9f3bU));
				rotatingOutputs.emplace_back(outputBytes,
					static_cast<BYTE>(0x31U + i));
				rotatingFrames.emplace_back(rotatingInputs.back().data(),
					static_cast<uint64_t>(i + 1), 0, nullptr);
			}

			comparison.rotatingBuffers = MeasureFormatterPerformance(formatter,
				rotatingFrames, rotatingOutputs, 10, 120);
			return comparison;
		}

		ConverterBenchmarkComparison CompareFormatterPerformance(
			const wchar_t* converter,
			IVideoFrameFormatter& formatter,
			size_t inputBytes,
			size_t outputBytes)
		{
			constexpr size_t runCount = 3;
			std::vector<ConverterBenchmarkStatistics> hotRuns;
			std::vector<ConverterBenchmarkStatistics> rotatingRuns;
			for (size_t run = 0; run < runCount; ++run)
			{
				const auto result = CompareFormatterPerformanceOnce(formatter,
					inputBytes, outputBytes);
				hotRuns.push_back(result.hotBuffer);
				rotatingRuns.push_back(result.rotatingBuffers);
			}

			ConverterBenchmarkComparison result;
			result.hotBuffer = MedianBenchmarkStatistics(hotRuns);
			result.rotatingBuffers = MedianBenchmarkStatistics(rotatingRuns);
			LogBenchmarkResult(converter, L"hot-zero-30-median3", result.hotBuffer);
			LogBenchmarkResult(converter, L"rotating-pattern-120-median3",
				result.rotatingBuffers);
			return result;
		}

		void WriteR10Pixel(BYTE* destination, VideoFrameEncoding encoding,
			uint16_t red, uint16_t green, uint16_t blue)
		{
			const uint32_t word = (static_cast<uint32_t>(red) << 22) |
				(static_cast<uint32_t>(green) << 12) |
				(static_cast<uint32_t>(blue) << 2);
			if (encoding == VideoFrameEncoding::R10l)
			{
				destination[0] = static_cast<BYTE>(word);
				destination[1] = static_cast<BYTE>(word >> 8);
				destination[2] = static_cast<BYTE>(word >> 16);
				destination[3] = static_cast<BYTE>(word >> 24);
			}
			else
			{
				destination[0] = static_cast<BYTE>(word >> 24);
				destination[1] = static_cast<BYTE>(word >> 16);
				destination[2] = static_cast<BYTE>(word >> 8);
				destination[3] = static_cast<BYTE>(word);
			}
		}

		void WriteR210Pixel(BYTE* destination,
			uint16_t red, uint16_t green, uint16_t blue)
		{
			const uint32_t word = (static_cast<uint32_t>(red) << 20) |
				(static_cast<uint32_t>(green) << 10) |
				static_cast<uint32_t>(blue);
			destination[0] = static_cast<BYTE>(word >> 24);
			destination[1] = static_cast<BYTE>(word >> 16);
			destination[2] = static_cast<BYTE>(word >> 8);
			destination[3] = static_cast<BYTE>(word);
		}

		void WriteR12LPixelPair(BYTE* destination,
			uint16_t r0, uint16_t g0, uint16_t b0,
			uint16_t r1, uint16_t g1, uint16_t b1)
		{
			destination[0] = static_cast<BYTE>(r0);
			destination[1] = static_cast<BYTE>((r0 >> 8) | (g0 << 4));
			destination[2] = static_cast<BYTE>(g0 >> 4);
			destination[3] = static_cast<BYTE>(b0);
			destination[4] = static_cast<BYTE>((b0 >> 8) | (r1 << 4));
			destination[5] = static_cast<BYTE>(r1 >> 4);
			destination[6] = static_cast<BYTE>(g1);
			destination[7] = static_cast<BYTE>((g1 >> 8) | (b1 << 4));
			destination[8] = static_cast<BYTE>(b1 >> 4);
		}

		void WriteR12BBlock(BYTE* destination,
			uint16_t red, uint16_t green, uint16_t blue)
		{
			BYTE logicalBytes[36] = {};
			for (uint32_t pair = 0; pair < 4; ++pair)
			{
				WriteR12LPixelPair(logicalBytes + pair * 9U,
					red, green, blue, red, green, blue);
			}

			// R12B is the same SMPTE 268M C4 byte stream as R12L with each
			// 32-bit word stored big-endian.
			for (uint32_t byteIndex = 0; byteIndex < 36; ++byteIndex)
				destination[(byteIndex / 4U) * 4U + 3U - (byteIndex % 4U)] =
					logicalBytes[byteIndex];
		}

		void WriteV210Pack(BYTE* destination,
			uint16_t u0, uint16_t y0, uint16_t v0, uint16_t y1,
			uint16_t u2, uint16_t y2, uint16_t v2, uint16_t y3,
			uint16_t u4, uint16_t y4, uint16_t v4, uint16_t y5)
		{
			const auto writeWord = [](BYTE* target,
				uint16_t a, uint16_t b, uint16_t c)
			{
				const uint32_t word = static_cast<uint32_t>(a) |
					(static_cast<uint32_t>(b) << 10) |
					(static_cast<uint32_t>(c) << 20);
				std::memcpy(target, &word, sizeof(word));
			};

			writeWord(destination + 0, u0, y0, v0);
			writeWord(destination + 4, y1, u2, y2);
			writeWord(destination + 8, v2, y3, u4);
			writeWord(destination + 12, y4, v4, y5);
		}
	}

	TEST_CLASS(VideoFrameFormatterTests)
	{
	public:
		TEST_METHOD(MadVRPackedTwelveBitIngressUsesP010Policy)
		{
			const auto automatic =
				VideoConversionOverride::VIDEOCONVERSION_NONE;
			const auto forcedP010 =
				VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010;

			Assert::IsTrue(MadVRUsesP010Ingress(
				VideoFrameEncoding::R12B, automatic));
			Assert::IsTrue(MadVRUsesP010Ingress(
				VideoFrameEncoding::R12L, automatic));
			Assert::IsFalse(MadVRUsesP010Ingress(
				VideoFrameEncoding::R210, automatic));
			Assert::IsTrue(MadVRUsesP010Ingress(
				VideoFrameEncoding::R210, forcedP010));
			Assert::IsTrue(IsDeckLinkPackedRgbP010Encoding(
				VideoFrameEncoding::R12B));
			Assert::IsTrue(IsDeckLinkPackedRgbP010Encoding(
				VideoFrameEncoding::R12L));
			Assert::IsFalse(IsDeckLinkPackedRgbP010Encoding(
				VideoFrameEncoding::BGRA_8BIT));
		}

		TEST_METHOD(RendererIngressHelpersCoverEveryDeckLinkCaptureEncoding)
		{
			struct Case
			{
				VideoFrameEncoding encoding;
				bool alphaNativeIngress;
				bool alphaAutomaticP210;
				bool madvrAutomaticP010;
				bool packedRgbP010;
				bool mpcAutomaticP010;
				bool genericAutomaticP010;
			};

			const Case cases[] = {
				{ VideoFrameEncoding::ARGB_8BIT, true,  false, true,  false, true,  true  },
				{ VideoFrameEncoding::BGRA_8BIT, true,  false, true,  false, true,  true  },
				{ VideoFrameEncoding::UYVY,      false, true,  false, false, false, false },
				{ VideoFrameEncoding::HDYC,      false, true,  false, false, false, false },
				{ VideoFrameEncoding::V210,      false, true,  false, false, false, false },
				{ VideoFrameEncoding::R210,      false, false, false, true,  false, false },
				{ VideoFrameEncoding::R10b,      true,  false, true,  true,  true,  true  },
				{ VideoFrameEncoding::R10l,      true,  false, true,  true,  true,  true  },
				{ VideoFrameEncoding::R12B,      false, false, true,  true,  false, false },
				{ VideoFrameEncoding::R12L,      false, false, true,  true,  true,  true  },
			};

			for (const Case& test : cases)
			{
				Assert::AreEqual(test.alphaNativeIngress,
					AlphaCanUseNativeRgbUpload(test.encoding));
				Assert::AreEqual(test.alphaAutomaticP210,
					AlphaUsesP210Ingress(test.encoding,
						VideoConversionOverride::VIDEOCONVERSION_NONE));
				Assert::IsFalse(AlphaUsesP210Ingress(test.encoding,
					VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010));
				Assert::AreEqual(test.madvrAutomaticP010,
					MadVRUsesP010Ingress(test.encoding,
						VideoConversionOverride::VIDEOCONVERSION_NONE));
				Assert::IsTrue(MadVRUsesP010Ingress(test.encoding,
					VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010));
				Assert::AreEqual(test.packedRgbP010,
					IsDeckLinkPackedRgbP010Encoding(test.encoding));
				Assert::IsTrue(DirectShowCanConvertToP010(test.encoding));
				Assert::AreEqual(test.mpcAutomaticP010,
					DirectShowUsesP010Ingress(DirectShowIngressFamily::MPC,
						test.encoding, VideoConversionOverride::VIDEOCONVERSION_NONE));
				Assert::AreEqual(test.genericAutomaticP010,
					DirectShowUsesP010Ingress(DirectShowIngressFamily::GENERIC,
						test.encoding, VideoConversionOverride::VIDEOCONVERSION_NONE));
				Assert::IsTrue(DirectShowUsesP010Ingress(
					DirectShowIngressFamily::MPC, test.encoding,
					VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010));
				Assert::IsTrue(DirectShowUsesP010Ingress(
					DirectShowIngressFamily::GENERIC, test.encoding,
					VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010));
			}
		}

		TEST_METHOD(DeckLinkPackedRgbFormatterContractsMatchDocumentedRanges)
		{
			const auto expect = [](VideoFrameEncoding encoding,
				VideoFrameSampleRange expectedRange)
			{
				CDeckLinkRGBToP010VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					128, 100, false, 24000, 1000);
				state->videoFrameEncoding = encoding;
				state->colorspace = ColorSpace::REC_709;
				formatter.OnVideoState(state);

				const auto contract = formatter.GetOutputContract();
				Assert::IsTrue(contract.IsValid());
				Assert::AreEqual(static_cast<int>(expectedRange),
					static_cast<int>(contract.sampleRange));
				Assert::AreEqual(10, static_cast<int>(contract.colorDepth));
				Assert::AreEqual(6, static_cast<int>(contract.bitShift));
			};

			// DeckLinkAPIModes.idl defines these three as SMPTE-range RGB.
			expect(VideoFrameEncoding::R210, VideoFrameSampleRange::LIMITED);
			expect(VideoFrameEncoding::R10b, VideoFrameSampleRange::LIMITED);
			expect(VideoFrameEncoding::R10l, VideoFrameSampleRange::LIMITED);
			// The same SDK declaration explicitly defines both 12-bit formats as
			// full-range 0-4095.
			expect(VideoFrameEncoding::R12B, VideoFrameSampleRange::FULL);
			expect(VideoFrameEncoding::R12L, VideoFrameSampleRange::FULL);
		}

		TEST_METHOD(DeckLinkR12BAndRgb48ContractsRemainFullRange)
		{
			CR12BtoRGB48VideoFrameFormatter formatter;
			const auto contract = formatter.GetOutputContract();
			Assert::IsTrue(contract.IsValid());
			Assert::AreEqual(static_cast<int>(VideoFrameSampleRange::FULL),
				static_cast<int>(contract.sampleRange));
			Assert::AreEqual(16, static_cast<int>(contract.colorDepth));
			Assert::AreEqual(0, static_cast<int>(contract.bitShift));
		}

		TEST_METHOD(DeckLinkR210Rgb48ContractIsLimitedRange)
		{
			CR210toRGB48VideoFrameFormatter formatter;
			const auto contract = formatter.GetOutputContract();
			Assert::IsTrue(contract.IsValid());
			Assert::AreEqual(static_cast<int>(VideoFrameSampleRange::LIMITED),
				static_cast<int>(contract.sampleRange));
			Assert::AreEqual(10, static_cast<int>(contract.colorDepth));
			Assert::AreEqual(6, static_cast<int>(contract.bitShift));
		}

		TEST_METHOD(DeckLinkR12BDirectMediaTypeUsesThirtySixBitsPerPixel)
		{
			Assert::AreEqual(36U,
				VideoFrameEncodingBitsPerPixel(VideoFrameEncoding::R12B));
		}

		TEST_METHOD(DirectShowNominalRangeFollowsFormatterContractUnlessForced)
		{
			const VideoFrameFormatterOutputContract full = {
				VideoFrameSampleRange::FULL, 10, 6
			};
			const VideoFrameFormatterOutputContract limited = {
				VideoFrameSampleRange::LIMITED, 10, 6
			};
			const VideoFrameFormatterOutputContract unknown;
			const auto automatic = DXVA_NominalRange::DXVA_NominalRange_Unknown;

			Assert::AreEqual(
				static_cast<int>(DXVA_NominalRange::DXVA_NominalRange_0_255),
				static_cast<int>(ResolveDirectShowNominalRange(automatic, full)));
			Assert::AreEqual(
				static_cast<int>(DXVA_NominalRange::DXVA_NominalRange_16_235),
				static_cast<int>(ResolveDirectShowNominalRange(automatic, limited)));
			Assert::AreEqual(static_cast<int>(automatic),
				static_cast<int>(ResolveDirectShowNominalRange(automatic, unknown)));

			const auto forcedFull = DXVA_NominalRange::DXVA_NominalRange_0_255;
			Assert::AreEqual(static_cast<int>(forcedFull),
				static_cast<int>(ResolveDirectShowNominalRange(forcedFull, limited)));
		}

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

		TEST_METHOD(AlphaFormatterOutputContractsAreExplicit)
		{
			const auto expect = [](const VideoFrameFormatterOutputContract& contract,
				VideoFrameSampleRange range, uint8_t depth, uint8_t shift)
			{
				Assert::IsTrue(contract.IsValid());
				Assert::AreEqual(static_cast<int>(range),
					static_cast<int>(contract.sampleRange));
				Assert::AreEqual(static_cast<int>(depth),
					static_cast<int>(contract.colorDepth));
				Assert::AreEqual(static_cast<int>(shift),
					static_cast<int>(contract.bitShift));
			};

			CARGBtoP010VideoFrameFormatter argb;
			CUYVYtoP010VideoFrameFormatter uyvyP010;
			CV210toP010VideoFrameFormatter v210P010;
			CUYVYtoP210VideoFrameFormatter uyvyP210;
			CV210toP210VideoFrameFormatter v210P210;
			expect(argb.GetOutputContract(), VideoFrameSampleRange::FULL, 10, 6);
			expect(uyvyP010.GetOutputContract(), VideoFrameSampleRange::LIMITED, 8, 8);
			expect(v210P010.GetOutputContract(), VideoFrameSampleRange::LIMITED, 10, 6);
			expect(uyvyP210.GetOutputContract(), VideoFrameSampleRange::LIMITED, 8, 8);
			expect(v210P210.GetOutputContract(), VideoFrameSampleRange::LIMITED, 10, 6);
		}

		TEST_METHOD(CARGBtoP010VideoFrameFormatterUsesVpAssumedFullRangeEndpoints)
		{
			// DeckLink documents the ARGB memory layout but not its nominal range.
			// This test records VP's existing full-range product assumption.
			CARGBtoP010VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(100, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::ARGB_8BIT;
			vs->colorspace = ColorSpace::REC_709;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0xFF);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			const auto* words = reinterpret_cast<const uint16_t*>(output.data());
			const size_t ySamples = 100 * 100;
			Assert::AreEqual(1023U << 6, static_cast<unsigned int>(words[0]));
			Assert::AreEqual(1023U << 6, static_cast<unsigned int>(words[ySamples - 1]));
			Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[ySamples]));
			Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[ySamples + 1]));

			for (size_t pixel = 0; pixel < input.size(); pixel += 4)
			{
				input[pixel] = 0xFF; // alpha
				input[pixel + 1] = 0xFF; // red
				input[pixel + 2] = 0;
				input[pixel + 3] = 0;
			}
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			Assert::AreEqual(217U << 6, static_cast<unsigned int>(words[0]));
			Assert::AreEqual(395U << 6, static_cast<unsigned int>(words[ySamples]));
			Assert::AreEqual(1023U << 6, static_cast<unsigned int>(words[ySamples + 1]));
		}

		TEST_METHOD(CARGBtoP010AVX2MatchesScalarBitExactly)
		{
			const VideoFrameEncoding encodings[] = {
				VideoFrameEncoding::ARGB_8BIT,
				VideoFrameEncoding::BGRA_8BIT,
			};
			const ColorSpace colorSpaces[] = {
				ColorSpace::REC_709,
				ColorSpace::BT_2020,
			};
			for (const auto encoding : encodings)
			{
				for (const auto colorSpace : colorSpaces)
				{
					VideoStateComPtr state = new VideoState();
					state->valid = true;
					state->displayMode = std::make_shared<DisplayMode>(
						128, 100, false, 60000, 1001);
					state->videoFrameEncoding = encoding;
					state->colorspace = colorSpace;
					std::vector<BYTE> input(state->BytesPerFrame());
					FillBenchmarkPattern(input,
						0xabcdef01U ^ static_cast<uint32_t>(encoding) ^
						(static_cast<uint32_t>(colorSpace) << 16));

					// Cover every possible 8-bit component value in vectorized blocks,
					// including all three exact 8-to-10 rounding transitions.
					for (uint32_t value = 0; value < 256; ++value)
					{
						BYTE* pixel = input.data() +
							static_cast<size_t>(value / 128) * state->BytesPerRow() +
							static_cast<size_t>(value % 128) * 4U;
						const BYTE red = static_cast<BYTE>(value);
						const BYTE green = static_cast<BYTE>(255U - value);
						const BYTE blue = static_cast<BYTE>((value * 73U) & 0xffU);
						if (encoding == VideoFrameEncoding::BGRA_8BIT)
						{
							pixel[0] = blue; pixel[1] = green;
							pixel[2] = red; pixel[3] = 0xff;
						}
						else
						{
							pixel[0] = 0xff; pixel[1] = red;
							pixel[2] = green; pixel[3] = blue;
						}
					}
					VideoFrame frame(input.data(), 1, 0, nullptr);

					CARGBtoP010VideoFrameFormatter scalar;
					scalar.SetConversionMethod(
						CARGBtoP010VideoFrameFormatter::ConversionMethod::SCALAR);
					scalar.OnVideoState(state);
					std::vector<BYTE> scalarOutput(scalar.GetOutFrameSize(), 0x55);
					Assert::IsTrue(scalar.FormatVideoFrame(frame, scalarOutput.data()));

					CARGBtoP010VideoFrameFormatter avx2;
					avx2.SetConversionMethod(
						CARGBtoP010VideoFrameFormatter::ConversionMethod::AVX2);
					avx2.OnVideoState(state);
					std::vector<BYTE> avx2Output(avx2.GetOutFrameSize(), 0xaa);
					Assert::IsTrue(avx2.FormatVideoFrame(frame, avx2Output.data()));
					Assert::IsTrue(scalarOutput == avx2Output,
						L"ARGB/BGRA AVX2 output must match scalar output bit-for-bit");
				}
			}

			VideoStateComPtr threadedState = new VideoState();
			threadedState->valid = true;
			threadedState->displayMode = std::make_shared<DisplayMode>(
				1920, 1080, false, 60000, 1001);
			threadedState->videoFrameEncoding = VideoFrameEncoding::BGRA_8BIT;
			threadedState->colorspace = ColorSpace::BT_2020;
			std::vector<BYTE> threadedInput(threadedState->BytesPerFrame());
			FillBenchmarkPattern(threadedInput, 0x36bc17e2U);
			VideoFrame threadedFrame(threadedInput.data(), 1, 0, nullptr);

			CARGBtoP010VideoFrameFormatter threadedScalar;
			threadedScalar.SetConversionMethod(
				CARGBtoP010VideoFrameFormatter::ConversionMethod::SCALAR);
			threadedScalar.OnVideoState(threadedState);
			std::vector<BYTE> threadedScalarOutput(
				threadedScalar.GetOutFrameSize());
			Assert::IsTrue(threadedScalar.FormatVideoFrame(
				threadedFrame, threadedScalarOutput.data()));

			CARGBtoP010VideoFrameFormatter threadedAVX2;
			threadedAVX2.SetConversionMethod(
				CARGBtoP010VideoFrameFormatter::ConversionMethod::AVX2);
			threadedAVX2.OnVideoState(threadedState);
			std::vector<BYTE> threadedAVX2Output(threadedAVX2.GetOutFrameSize());
			Assert::IsTrue(threadedAVX2.FormatVideoFrame(
				threadedFrame, threadedAVX2Output.data()));
			Assert::IsTrue(threadedScalarOutput == threadedAVX2Output,
				L"Threaded BGRA AVX2 conversion must match scalar output bit-for-bit");
		}

		TEST_METHOD(CUYVYtoP010VideoFrameFormatterPreservesLimitedRangeCodes)
		{
			CUYVYtoP010VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(100, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::HDYC;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			for (uint32_t line = 0; line < 100; ++line)
			{
				BYTE* row = input.data() + static_cast<size_t>(line) * vs->BytesPerRow();
				row[0] = 128; row[1] = 16; row[2] = 128; row[3] = 235;
			}
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			const auto* words = reinterpret_cast<const uint16_t*>(output.data());
			const size_t ySamples = 100 * 100;
			Assert::AreEqual(64U << 6, static_cast<unsigned int>(words[0]));
			Assert::AreEqual(940U << 6, static_cast<unsigned int>(words[1]));
			Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[ySamples]));
			Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[ySamples + 1]));
		}

		TEST_METHOD(CV210toP010VideoFrameFormatter720pPreservesAllActivePixels)
		{
			CV210toP010VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(1280, 720, false, 60000, 1001);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			auto writeWord = [](BYTE* destination, uint16_t a, uint16_t b, uint16_t c)
			{
				const uint32_t word = static_cast<uint32_t>(a) |
					(static_cast<uint32_t>(b) << 10) |
					(static_cast<uint32_t>(c) << 20);
				std::memcpy(destination, &word, sizeof(word));
			};
			for (uint32_t line = 0; line < 720; ++line)
			{
				BYTE* row = input.data() + static_cast<size_t>(line) * vs->BytesPerRow();
				writeWord(row, 128, 64, 128);
				writeWord(row + 4, 64, 128, 64);
				writeWord(row + 8, 128, 64, 128);
			}

			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			const auto* words = reinterpret_cast<const uint16_t*>(output.data());
			const size_t ySamples = 1280ULL * 720;
			// 1280 is not divisible by v210's six-pixel pack size, but all 1280
			// active pixels remain image data. Packing padding exists only after
			// the active tail and must not conceal either edge.
			Assert::AreEqual(64U << 6, static_cast<unsigned int>(words[0]));
			Assert::AreEqual(64U << 6, static_cast<unsigned int>(words[1]));
			Assert::AreEqual(64U << 6, static_cast<unsigned int>(words[2]));
			Assert::AreEqual(0U, static_cast<unsigned int>(words[1278]));
			Assert::AreEqual(0U, static_cast<unsigned int>(words[1279]));
			Assert::AreEqual(128U << 6, static_cast<unsigned int>(words[ySamples]));
			Assert::AreEqual(128U << 6, static_cast<unsigned int>(words[ySamples + 1]));
			Assert::AreEqual(128U << 6, static_cast<unsigned int>(words[ySamples + 2]));
			Assert::AreEqual(128U << 6, static_cast<unsigned int>(words[ySamples + 3]));
			Assert::AreEqual(0U, static_cast<unsigned int>(words[ySamples + 1278]));
			Assert::AreEqual(0U, static_cast<unsigned int>(words[ySamples + 1279]));
		}

		TEST_METHOD(V210P010AndP210SupportStandardResolutionMatrix)
		{
			const uint32_t dimensions[][2] = {
				{ 640, 360 },
				{ 720, 480 },
				{ 1280, 720 },
				{ 1920, 1080 },
				{ 3840, 2160 },
			};
			auto writeWord = [](BYTE* destination,
				uint16_t a, uint16_t b, uint16_t c)
			{
				const uint32_t word = static_cast<uint32_t>(a) |
					(static_cast<uint32_t>(b) << 10) |
					(static_cast<uint32_t>(c) << 20);
				std::memcpy(destination, &word, sizeof(word));
			};
			auto writePack = [&writeWord](BYTE* destination)
			{
				constexpr uint16_t u = 129;
				constexpr uint16_t y = 321;
				constexpr uint16_t v = 777;
				writeWord(destination + 0, u, y, v);
				writeWord(destination + 4, y, u, y);
				writeWord(destination + 8, v, y, u);
				writeWord(destination + 12, y, v, y);
			};

			for (const auto& dimension : dimensions)
			{
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					dimension[0], dimension[1], false, 60000, 1001);
				state->videoFrameEncoding = VideoFrameEncoding::V210;
				std::vector<BYTE> input(state->BytesPerFrame(), 0xff);
				const uint32_t packsPerRow = (dimension[0] + 5U) / 6U;
				for (uint32_t line = 0; line < dimension[1]; ++line)
				{
					BYTE* row = input.data() +
						static_cast<size_t>(line) * state->BytesPerRow();
					for (uint32_t pack = 0; pack < packsPerRow; ++pack)
						writePack(row + static_cast<size_t>(pack) * 16U);
				}
				VideoFrame frame(input.data(), 1, 0, nullptr);

				CV210toP010VideoFrameFormatter scalar;
				scalar.SetConversionMethod(
					CV210toP010VideoFrameFormatter::ConversionMethod::STANDARD);
				scalar.OnVideoState(state);
				std::vector<BYTE> scalarOutput(scalar.GetOutFrameSize());
				Assert::IsTrue(scalar.FormatVideoFrame(frame, scalarOutput.data()));
				const auto* p010 = reinterpret_cast<const uint16_t*>(
					scalarOutput.data());
				const size_t ySamples =
					static_cast<size_t>(dimension[0]) * dimension[1];
				Assert::AreEqual(321U << 6, static_cast<unsigned int>(p010[0]));
				Assert::AreEqual(321U << 6,
					static_cast<unsigned int>(p010[ySamples - 1]));
				Assert::AreEqual(129U << 6,
					static_cast<unsigned int>(p010[ySamples]));
				Assert::AreEqual(777U << 6,
					static_cast<unsigned int>(p010[ySamples + dimension[0] - 1]));

				const CV210toP010VideoFrameFormatter::ConversionMethod methods[] = {
					CV210toP010VideoFrameFormatter::ConversionMethod::AUTO,
					CV210toP010VideoFrameFormatter::ConversionMethod::OPTIMIZED,
					CV210toP010VideoFrameFormatter::ConversionMethod::SIMD,
				};
				for (const auto method : methods)
				{
					CV210toP010VideoFrameFormatter candidate;
					candidate.SetConversionMethod(method);
					candidate.OnVideoState(state);
					std::vector<BYTE> candidateOutput(candidate.GetOutFrameSize());
					Assert::IsTrue(candidate.FormatVideoFrame(
						frame, candidateOutput.data()));
					Assert::IsTrue(candidateOutput == scalarOutput,
						L"Every v210 P010 method must match at every standard resolution");
				}

				CV210toP210VideoFrameFormatter p210Formatter;
				p210Formatter.OnVideoState(state);
				std::vector<BYTE> p210Output(p210Formatter.GetOutFrameSize());
				Assert::IsTrue(p210Formatter.FormatVideoFrame(
					frame, p210Output.data()));
				const auto* p210 = reinterpret_cast<const uint16_t*>(
					p210Output.data());
				Assert::AreEqual(321U << 6, static_cast<unsigned int>(p210[0]));
				Assert::AreEqual(321U << 6,
					static_cast<unsigned int>(p210[ySamples - 1]));
				Assert::AreEqual(129U << 6,
					static_cast<unsigned int>(p210[ySamples]));
				Assert::AreEqual(777U << 6,
					static_cast<unsigned int>(p210[ySamples + dimension[0] - 1]));
			}
		}

		TEST_METHOD(DeckLinkRgbAndUyvyConvertersSupportStandardResolutionMatrix)
		{
			const uint32_t dimensions[][2] = {
				{ 640, 360 },
				{ 720, 480 },
				{ 1280, 720 },
				{ 1920, 1080 },
				{ 3840, 2160 },
			};
			for (const auto& dimension : dimensions)
			{
				auto makeState = [&dimension](VideoFrameEncoding encoding)
				{
					VideoStateComPtr state = new VideoState();
					state->valid = true;
					state->displayMode = std::make_shared<DisplayMode>(
						dimension[0], dimension[1], false, 60000, 1001);
					state->videoFrameEncoding = encoding;
					state->colorspace = ColorSpace::BT_2020;
					return state;
				};

				for (const auto encoding : {
					VideoFrameEncoding::UYVY, VideoFrameEncoding::HDYC })
				{
					VideoStateComPtr state = makeState(encoding);
					std::vector<BYTE> input(state->BytesPerFrame());
					for (uint32_t line = 0; line < dimension[1]; ++line)
					{
						BYTE* row = input.data() +
							static_cast<size_t>(line) * state->BytesPerRow();
						for (uint32_t x = 0; x < dimension[0]; x += 2)
						{
							row[0] = 129; row[1] = 64;
							row[2] = 201; row[3] = 65;
							row += 4;
						}
					}
					VideoFrame frame(input.data(), 1, 0, nullptr);

					CUYVYtoP010VideoFrameFormatter p010Formatter;
					p010Formatter.OnVideoState(state);
					std::vector<BYTE> p010Output(p010Formatter.GetOutFrameSize());
					Assert::IsTrue(p010Formatter.FormatVideoFrame(
						frame, p010Output.data()));
					const auto* p010 = reinterpret_cast<const uint16_t*>(
						p010Output.data());
					const size_t ySamples =
						static_cast<size_t>(dimension[0]) * dimension[1];
					Assert::AreEqual(64U << 8,
						static_cast<unsigned int>(p010[0]));
					Assert::AreEqual(65U << 8,
						static_cast<unsigned int>(p010[ySamples - 1]));

					CUYVYtoP210VideoFrameFormatter p210Formatter;
					p210Formatter.OnVideoState(state);
					std::vector<BYTE> p210Output(p210Formatter.GetOutFrameSize());
					Assert::IsTrue(p210Formatter.FormatVideoFrame(
						frame, p210Output.data()));
					const auto* p210 = reinterpret_cast<const uint16_t*>(
						p210Output.data());
					Assert::AreEqual(64U << 8,
						static_cast<unsigned int>(p210[0]));
					Assert::AreEqual(65U << 8,
						static_cast<unsigned int>(p210[ySamples - 1]));
				}

				for (const auto encoding : {
					VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
				{
					VideoStateComPtr state = makeState(encoding);
					std::vector<BYTE> input(state->BytesPerFrame(), 64);
					VideoFrame frame(input.data(), 1, 0, nullptr);
					CARGBtoP010VideoFrameFormatter formatter;
					formatter.OnVideoState(state);
					std::vector<BYTE> output(formatter.GetOutFrameSize());
					Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
					const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
					const size_t ySamples =
						static_cast<size_t>(dimension[0]) * dimension[1];
					Assert::IsTrue(samples[0] != 0);
					Assert::AreEqual(static_cast<unsigned int>(samples[0]),
						static_cast<unsigned int>(samples[ySamples - 1]));
				}

				for (const auto encoding : {
					VideoFrameEncoding::R210, VideoFrameEncoding::R10b,
					VideoFrameEncoding::R10l, VideoFrameEncoding::R12B,
					VideoFrameEncoding::R12L })
				{
					VideoStateComPtr state = makeState(encoding);
					std::vector<BYTE> input(state->BytesPerFrame());
					for (uint32_t line = 0; line < dimension[1]; ++line)
					{
						BYTE* row = input.data() +
							static_cast<size_t>(line) * state->BytesPerRow();
						if (encoding == VideoFrameEncoding::R12B)
						{
							for (uint32_t x = 0; x < dimension[0]; x += 8)
								WriteR12BBlock(row + static_cast<size_t>(x / 8) * 36U,
									2048, 2048, 2048);
						}
						else if (encoding == VideoFrameEncoding::R12L)
						{
							for (uint32_t x = 0; x < dimension[0]; x += 2)
								WriteR12LPixelPair(row + static_cast<size_t>(x / 2) * 9U,
									2048, 2048, 2048, 2048, 2048, 2048);
						}
						else
						{
							for (uint32_t x = 0; x < dimension[0]; ++x)
							{
								if (encoding == VideoFrameEncoding::R210)
									WriteR210Pixel(row + static_cast<size_t>(x) * 4U,
										512, 512, 512);
								else
									WriteR10Pixel(row + static_cast<size_t>(x) * 4U,
										encoding, 512, 512, 512);
							}
						}
					}
					VideoFrame frame(input.data(), 1, 0, nullptr);
					CDeckLinkRGBToP010VideoFrameFormatter formatter;
					formatter.OnVideoState(state);
					std::vector<BYTE> output(formatter.GetOutFrameSize());
					Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
					const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
					const size_t ySamples =
						static_cast<size_t>(dimension[0]) * dimension[1];
					Assert::IsTrue(samples[0] != 0);
					Assert::AreEqual(static_cast<unsigned int>(samples[0]),
						static_cast<unsigned int>(samples[ySamples - 1]));
				}

				{
					VideoStateComPtr state = makeState(VideoFrameEncoding::R210);
					std::vector<BYTE> input(state->BytesPerFrame());
					for (uint32_t line = 0; line < dimension[1]; ++line)
					{
						BYTE* row = input.data() +
							static_cast<size_t>(line) * state->BytesPerRow();
						for (uint32_t x = 0; x < dimension[0]; ++x)
							WriteR210Pixel(row + static_cast<size_t>(x) * 4U,
								512, 512, 512);
					}
					VideoFrame frame(input.data(), 1, 0, nullptr);
					CR210toRGB48VideoFrameFormatter formatter;
					formatter.OnVideoState(state);
					std::vector<BYTE> output(formatter.GetOutFrameSize());
					Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
					const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
					const size_t componentCount =
						static_cast<size_t>(dimension[0]) * dimension[1] * 3U;
					Assert::AreEqual(512U << 6,
						static_cast<unsigned int>(samples[0]));
					Assert::AreEqual(512U << 6,
						static_cast<unsigned int>(samples[componentCount - 1]));
				}

				{
					VideoStateComPtr state = makeState(VideoFrameEncoding::R12B);
					std::vector<BYTE> input(state->BytesPerFrame());
					for (uint32_t line = 0; line < dimension[1]; ++line)
					{
						BYTE* row = input.data() +
							static_cast<size_t>(line) * state->BytesPerRow();
						for (uint32_t x = 0; x < dimension[0]; x += 8)
							WriteR12BBlock(row + static_cast<size_t>(x / 8) * 36U,
								2048, 2048, 2048);
					}
					VideoFrame frame(input.data(), 1, 0, nullptr);
					CR12BtoRGB48VideoFrameFormatter formatter;
					formatter.OnVideoState(state);
					std::vector<BYTE> output(formatter.GetOutFrameSize());
					Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
					const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
					const size_t componentCount =
						static_cast<size_t>(dimension[0]) * dimension[1] * 3U;
					Assert::AreEqual(32776U,
						static_cast<unsigned int>(samples[0]));
					Assert::AreEqual(32776U,
						static_cast<unsigned int>(samples[componentCount - 1]));
				}
			}
		}

		TEST_METHOD(CV210toP210VideoFrameFormatterPreservesEvery422Sample)
		{
			CV210toP210VideoFrameFormatter vff;

			// v210 rows are 128-byte aligned. A 144-pixel line is three alignment
			// units and satisfies DisplayMode's supported minimum dimensions.
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(144, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			auto writeWord = [](BYTE* destination, uint16_t a, uint16_t b, uint16_t c)
			{
				const uint32_t word = static_cast<uint32_t>(a) |
					(static_cast<uint32_t>(b) << 10) |
					(static_cast<uint32_t>(c) << 20);
				std::memcpy(destination, &word, sizeof(word));
			};
			auto writePack = [&writeWord](BYTE* row, uint16_t u0, uint16_t y0,
				uint16_t v0, uint16_t y1, uint16_t u2, uint16_t y2,
				uint16_t v2, uint16_t y3, uint16_t u4, uint16_t y4,
				uint16_t v4, uint16_t y5)
			{
				writeWord(row + 0, u0, y0, v0);
				writeWord(row + 4, y1, u2, y2);
				writeWord(row + 8, v2, y3, u4);
				writeWord(row + 12, y4, v4, y5);
			};
			writePack(input.data(), 101, 201, 301, 202, 102, 203,
				302, 204, 103, 205, 303, 206);
			writePack(input.data() + vs->BytesPerRow(), 401, 501, 601, 502,
				402, 503, 602, 504, 403, 505, 603, 506);

			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));

			const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
			const size_t ySamples = 144 * 100;
			auto expect = [&samples](size_t offset, uint16_t value)
			{
				Assert::AreEqual(static_cast<int>(value << 6),
					static_cast<int>(samples[offset]));
			};
			// All six luma samples and three 4:2:2 chroma pairs on the first row.
			for (size_t x = 0; x < 6; ++x)
				expect(x, static_cast<uint16_t>(201 + x));
			expect(ySamples + 0, 101);
			expect(ySamples + 1, 301);
			expect(ySamples + 2, 102);
			expect(ySamples + 3, 302);
			expect(ySamples + 4, 103);
			expect(ySamples + 5, 303);
			// The second row is independently preserved; there is no vertical
			// chroma average, which is the loss in the old P010 path.
			expect(144, 501);
			expect(ySamples + 144, 401);
			expect(ySamples + 145, 601);
		}

		TEST_METHOD(CV210toP010VideoFrameFormatterPreservesPaddedTailSamples)
		{
			CV210toP010VideoFrameFormatter vff;
			vff.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::STANDARD);

			// 100 pixels ends four pixels into the final v210 pack. The rest of
			// that pack and the DeckLink row alignment are sentinels.
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(100, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0xFF);
			auto writeWord = [](BYTE* destination, uint16_t a, uint16_t b, uint16_t c)
			{
				const uint32_t word = static_cast<uint32_t>(a) |
					(static_cast<uint32_t>(b) << 10) |
					(static_cast<uint32_t>(c) << 20);
				std::memcpy(destination, &word, sizeof(word));
			};
			for (uint32_t line = 0; line < 100; ++line)
			{
				BYTE* finalPack = input.data() + static_cast<size_t>(line) * vs->BytesPerRow() + 16 * 16;
				writeWord(finalPack + 0, 101, 201, 301);
				writeWord(finalPack + 4, 202, 102, 203);
				writeWord(finalPack + 8, 302, 204, 1023); // U4 is padding.
				writeWord(finalPack + 12, 1023, 1023, 1023); // Entire word is padding.
			}

			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));

			const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
			const size_t ySamples = 100 * 100;
			Assert::AreEqual(201U << 6, static_cast<unsigned int>(samples[96]));
			Assert::AreEqual(202U << 6, static_cast<unsigned int>(samples[97]));
			Assert::AreEqual(203U << 6, static_cast<unsigned int>(samples[98]));
			Assert::AreEqual(204U << 6, static_cast<unsigned int>(samples[99]));
			Assert::AreEqual(101U << 6, static_cast<unsigned int>(samples[ySamples + 96]));
			Assert::AreEqual(301U << 6, static_cast<unsigned int>(samples[ySamples + 97]));
			Assert::AreEqual(102U << 6, static_cast<unsigned int>(samples[ySamples + 98]));
			Assert::AreEqual(302U << 6, static_cast<unsigned int>(samples[ySamples + 99]));

			const CV210toP010VideoFrameFormatter::ConversionMethod alternatives[] = {
				CV210toP010VideoFrameFormatter::ConversionMethod::OPTIMIZED,
				CV210toP010VideoFrameFormatter::ConversionMethod::SIMD
			};
			for (const auto method : alternatives)
			{
				CV210toP010VideoFrameFormatter alternative;
				alternative.SetConversionMethod(method);
				alternative.OnVideoState(vs);
				std::vector<BYTE> alternativeOutput(alternative.GetOutFrameSize(), 0);
				Assert::IsTrue(alternative.FormatVideoFrame(frame, alternativeOutput.data()));
				Assert::IsTrue(output == alternativeOutput);
			}
		}

		TEST_METHOD(CV210toP010VideoFrameFormatterDciWidthSmokeTest)
		{
			const uint32_t dimensions[][2] = { { 2048, 1080 }, { 4096, 2160 } };
			for (const auto& dimension : dimensions)
			{
				CV210toP010VideoFrameFormatter vff;
				VideoStateComPtr vs = new VideoState();
				vs->valid = true;
				vs->displayMode = std::make_shared<DisplayMode>(dimension[0], dimension[1], false, 60000, 1001);
				vs->videoFrameEncoding = VideoFrameEncoding::V210;
				vff.OnVideoState(vs);

				std::vector<BYTE> input(vs->BytesPerFrame(), 0);
				std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
				VideoFrame frame(input.data(), 1, 0, nullptr);
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
				Assert::IsTrue(std::all_of(output.begin(), output.end(),
					[](BYTE value) { return value == 0; }));
			}
		}

		TEST_METHOD(CV210toP010VideoFrameFormatterAlignedPathsRemainEquivalent)
		{
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(192, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;

			std::vector<BYTE> input(vs->BytesPerFrame());
			for (size_t index = 0; index < input.size(); ++index)
				input[index] = static_cast<BYTE>((index * 37U + 11U) & 0xFFU);
			VideoFrame frame(input.data(), 1, 0, nullptr);

			CV210toP010VideoFrameFormatter standard;
			standard.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::STANDARD);
			standard.OnVideoState(vs);
			std::vector<BYTE> standardOutput(standard.GetOutFrameSize());
			Assert::IsTrue(standard.FormatVideoFrame(frame, standardOutput.data()));

			CV210toP010VideoFrameFormatter optimized;
			optimized.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::OPTIMIZED);
			optimized.OnVideoState(vs);
			std::vector<BYTE> optimizedOutput(optimized.GetOutFrameSize());
			Assert::IsTrue(optimized.FormatVideoFrame(frame, optimizedOutput.data()));

			CV210toP010VideoFrameFormatter simd;
			simd.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::SIMD);
			simd.OnVideoState(vs);
			std::vector<BYTE> simdOutput(simd.GetOutFrameSize());
			Assert::IsTrue(simd.FormatVideoFrame(frame, simdOutput.data()));

			Assert::IsTrue(standardOutput == optimizedOutput);
			Assert::IsTrue(standardOutput == simdOutput);
		}

		TEST_METHOD(CV210toP210VideoFrameFormatterPreservesPaddedEdgeSamples)
		{
			CV210toP210VideoFrameFormatter vff;

			// 100 pixels is not a multiple of v210's six-pixel pack. The input
			// stride nevertheless has DeckLink's 128-byte alignment.
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(100, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::V210;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			auto writeWord = [](BYTE* destination, uint16_t a, uint16_t b, uint16_t c)
			{
				const uint32_t word = static_cast<uint32_t>(a) |
					(static_cast<uint32_t>(b) << 10) |
					(static_cast<uint32_t>(c) << 20);
				std::memcpy(destination, &word, sizeof(word));
			};
			// The active final pack contains only two U/V pairs and four luma
			// samples; its remaining components are alignment padding.
			BYTE* finalPack = input.data() + 16 * 16;
			writeWord(finalPack + 0, 101, 201, 301);
			writeWord(finalPack + 4, 202, 102, 203);

			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));

			const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
			const size_t ySamples = 100 * 100;
			auto expect = [&samples](size_t offset, uint16_t value)
			{
				Assert::AreEqual(static_cast<int>(value << 6),
					static_cast<int>(samples[offset]));
			};
			expect(96, 201);
			expect(97, 202);
			expect(98, 203);
			expect(99, 0);
			expect(ySamples + 96, 101);
			expect(ySamples + 97, 301);
			expect(ySamples + 98, 102);
			expect(ySamples + 99, 0);
		}

		TEST_METHOD(CV210toP210AVX2MatchesScalarBitExactly)
		{
			VideoStateComPtr state = new VideoState();
			state->valid = true;
			state->displayMode = std::make_shared<DisplayMode>(
				100, 100, false, 60000, 1001);
			state->videoFrameEncoding = VideoFrameEncoding::V210;
			std::vector<BYTE> input(state->BytesPerFrame());
			FillBenchmarkPattern(input, 0x27182818U);
			VideoFrame frame(input.data(), 1, 0, nullptr);

			CV210toP210VideoFrameFormatter scalar;
			scalar.SetConversionMethod(
				CV210toP210VideoFrameFormatter::ConversionMethod::SCALAR);
			scalar.OnVideoState(state);
			std::vector<BYTE> scalarOutput(scalar.GetOutFrameSize(), 0x55);
			Assert::IsTrue(scalar.FormatVideoFrame(frame, scalarOutput.data()));

			CV210toP210VideoFrameFormatter avx2;
			avx2.SetConversionMethod(
				CV210toP210VideoFrameFormatter::ConversionMethod::AVX2);
			avx2.OnVideoState(state);
			std::vector<BYTE> avx2Output(avx2.GetOutFrameSize(), 0xaa);
			Assert::IsTrue(avx2.FormatVideoFrame(frame, avx2Output.data()));
			Assert::IsTrue(scalarOutput == avx2Output,
				L"v210 P210 AVX2 output must match scalar output bit-for-bit");
		}

		TEST_METHOD(CUYVYtoP210VideoFrameFormatterPreservesEvery422Sample)
		{
			CUYVYtoP210VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(100, 100, false, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::UYVY;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			// UYVY: U0, Y0, V0, Y1. The two rows deliberately have distinct
			// chroma, proving P210 does not apply P010's vertical average.
			input[0] = 11; input[1] = 21; input[2] = 31; input[3] = 22;
			const size_t secondRow = vs->BytesPerRow();
			input[secondRow + 0] = 41;
			input[secondRow + 1] = 51;
			input[secondRow + 2] = 61;
			input[secondRow + 3] = 52;

			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));

			const auto* samples = reinterpret_cast<const uint16_t*>(output.data());
			const size_t ySamples = 100 * 100;
			auto expect = [&samples](size_t offset, uint8_t value)
			{
				Assert::AreEqual(static_cast<int>(value << 8),
					static_cast<int>(samples[offset]));
			};
			expect(0, 21);
			expect(1, 22);
			expect(ySamples + 0, 11);
			expect(ySamples + 1, 31);
			expect(100, 51);
			expect(101, 52);
			expect(ySamples + 100, 41);
			expect(ySamples + 101, 61);
		}

		TEST_METHOD(CUYVYtoP210AVX2MatchesScalarBitExactly)
		{
			const VideoFrameEncoding encodings[] = {
				VideoFrameEncoding::UYVY,
				VideoFrameEncoding::HDYC,
			};
			for (const auto encoding : encodings)
			{
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					130, 100, false, 60000, 1001);
				state->videoFrameEncoding = encoding;
				std::vector<BYTE> input(state->BytesPerFrame());
				FillBenchmarkPattern(input,
					0x31415926U ^ static_cast<uint32_t>(encoding));
				VideoFrame frame(input.data(), 1, 0, nullptr);

				CUYVYtoP210VideoFrameFormatter scalar;
				scalar.SetConversionMethod(
					CUYVYtoP210VideoFrameFormatter::ConversionMethod::SCALAR);
				scalar.OnVideoState(state);
				std::vector<BYTE> scalarOutput(scalar.GetOutFrameSize(), 0x55);
				Assert::IsTrue(scalar.FormatVideoFrame(frame, scalarOutput.data()));

				CUYVYtoP210VideoFrameFormatter avx2;
				avx2.SetConversionMethod(
					CUYVYtoP210VideoFrameFormatter::ConversionMethod::AVX2);
				avx2.OnVideoState(state);
				std::vector<BYTE> avx2Output(avx2.GetOutFrameSize(), 0xaa);
				Assert::IsTrue(avx2.FormatVideoFrame(frame, avx2Output.data()));
				Assert::IsTrue(scalarOutput == avx2Output,
					L"UYVY/HDYC P210 AVX2 output must match scalar bit-for-bit");
			}
		}

		TEST_METHOD(CUYVYtoP010AVX2MatchesScalarBitExactlyIncludingRowTails)
		{
			const uint32_t widths[] = { 100, 110, 112, 114, 126, 128, 130, 142 };
			for (const auto encoding : {
				VideoFrameEncoding::UYVY, VideoFrameEncoding::HDYC })
			{
				for (const uint32_t width : widths)
				{
					VideoStateComPtr state = new VideoState();
					state->valid = true;
					state->displayMode = std::make_shared<DisplayMode>(
						width, 100, false, 60000, 1001);
					state->videoFrameEncoding = encoding;
					std::vector<BYTE> input(state->BytesPerFrame());
					FillBenchmarkPattern(input,
						0x6d2b79f5U ^ width ^ static_cast<uint32_t>(encoding));
					VideoFrame frame(input.data(), 1, 0, nullptr);

					CUYVYtoP010VideoFrameFormatter scalar;
					scalar.SetConversionMethod(
						CUYVYtoP010VideoFrameFormatter::ConversionMethod::SCALAR);
					scalar.OnVideoState(state);
					std::vector<BYTE> scalarOutput(scalar.GetOutFrameSize(), 0x55);
					Assert::IsTrue(scalar.FormatVideoFrame(
						frame, scalarOutput.data()));

					CUYVYtoP010VideoFrameFormatter avx2;
					avx2.SetConversionMethod(
						CUYVYtoP010VideoFrameFormatter::ConversionMethod::AVX2);
					avx2.OnVideoState(state);
					std::vector<BYTE> avx2Output(avx2.GetOutFrameSize(), 0xaa);
					Assert::IsTrue(avx2.FormatVideoFrame(frame, avx2Output.data()));
					Assert::IsTrue(scalarOutput == avx2Output,
						L"UYVY/HDYC P010 AVX2 output must match scalar at SIMD tails and row boundaries");
				}
			}
		}

		TEST_METHOD(UYVYAndHDYCProduceByteIdenticalP010AndP210Frames)
		{
			auto makeState = [](VideoFrameEncoding encoding)
			{
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					130, 100, false, 60000, 1001);
				state->videoFrameEncoding = encoding;
				state->colorspace = encoding == VideoFrameEncoding::HDYC ?
					ColorSpace::REC_709 : ColorSpace::REC_601_525;
				return state;
			};

			VideoStateComPtr uyvyState = makeState(VideoFrameEncoding::UYVY);
			VideoStateComPtr hdycState = makeState(VideoFrameEncoding::HDYC);
			Assert::AreEqual(uyvyState->BytesPerRow(), hdycState->BytesPerRow());
			Assert::AreEqual(260U, uyvyState->BytesPerRow());
			std::vector<BYTE> input(uyvyState->BytesPerFrame());
			FillBenchmarkPattern(input, 0xa511e9b3U);
			VideoFrame frame(input.data(), 1, 0, nullptr);

			CUYVYtoP010VideoFrameFormatter uyvyP010;
			uyvyP010.OnVideoState(uyvyState);
			CUYVYtoP010VideoFrameFormatter hdycP010;
			hdycP010.OnVideoState(hdycState);
			std::vector<BYTE> uyvyP010Output(uyvyP010.GetOutFrameSize());
			std::vector<BYTE> hdycP010Output(hdycP010.GetOutFrameSize());
			Assert::IsTrue(uyvyP010.FormatVideoFrame(frame, uyvyP010Output.data()));
			Assert::IsTrue(hdycP010.FormatVideoFrame(frame, hdycP010Output.data()));
			Assert::IsTrue(uyvyP010Output == hdycP010Output);

			CUYVYtoP210VideoFrameFormatter uyvyP210;
			uyvyP210.OnVideoState(uyvyState);
			CUYVYtoP210VideoFrameFormatter hdycP210;
			hdycP210.OnVideoState(hdycState);
			std::vector<BYTE> uyvyP210Output(uyvyP210.GetOutFrameSize());
			std::vector<BYTE> hdycP210Output(hdycP210.GetOutFrameSize());
			Assert::IsTrue(uyvyP210.FormatVideoFrame(frame, uyvyP210Output.data()));
			Assert::IsTrue(hdycP210.FormatVideoFrame(frame, hdycP210Output.data()));
			Assert::IsTrue(uyvyP210Output == hdycP210Output);
		}

		TEST_METHOD(OptionalCapturedDeckLink8BitYuvFrameReplay)
		{
			const auto environment = [](const char* name)
			{
				const DWORD required = GetEnvironmentVariableA(name, nullptr, 0);
				if (required == 0) return std::string();
				std::string value(required, '\0');
				GetEnvironmentVariableA(name, &value[0], required);
				value.resize(required - 1);
				return value;
			};
			const std::string path = environment("VP_DECKLINK_HDYC_FIXTURE");
			if (path.empty())
			{
				Logger::WriteMessage(
					L"Optional DeckLink replay skipped: VP_DECKLINK_HDYC_FIXTURE is not set");
				return;
			}

			const std::string widthText = environment("VP_DECKLINK_FIXTURE_WIDTH");
			const std::string heightText = environment("VP_DECKLINK_FIXTURE_HEIGHT");
			Assert::IsFalse(widthText.empty());
			Assert::IsFalse(heightText.empty());
			const uint32_t width = static_cast<uint32_t>(std::stoul(widthText));
			const uint32_t height = static_cast<uint32_t>(std::stoul(heightText));
			Assert::IsTrue(width > 0 && (width & 1U) == 0);
			Assert::IsTrue(height > 0 && (height & 1U) == 0);

			std::ifstream stream(path, std::ios::binary | std::ios::ate);
			Assert::IsTrue(stream.good(), L"Could not open DeckLink raw frame fixture");
			const size_t fileBytes = static_cast<size_t>(stream.tellg());
			const size_t expectedBytes = static_cast<size_t>(width) * height * 2U;
			Assert::AreEqual(expectedBytes, fileBytes,
				L"Raw bmdFormat8BitYUV fixture must contain exactly one tight UYVY frame");
			std::vector<BYTE> input(fileBytes);
			stream.seekg(0);
			stream.read(reinterpret_cast<char*>(input.data()), input.size());
			Assert::IsTrue(stream.good());

			VideoStateComPtr state = new VideoState();
			state->valid = true;
			state->displayMode = std::make_shared<DisplayMode>(
				width, height, false, 60000, 1001);
			state->videoFrameEncoding = VideoFrameEncoding::HDYC;
			state->colorspace = ColorSpace::REC_709;
			VideoFrame frame(input.data(), 1, 0, nullptr);

			CUYVYtoP010VideoFrameFormatter p010Scalar;
			p010Scalar.SetConversionMethod(
				CUYVYtoP010VideoFrameFormatter::ConversionMethod::SCALAR);
			p010Scalar.OnVideoState(state);
			CUYVYtoP010VideoFrameFormatter p010Avx2;
			p010Avx2.SetConversionMethod(
				CUYVYtoP010VideoFrameFormatter::ConversionMethod::AVX2);
			p010Avx2.OnVideoState(state);
			std::vector<BYTE> scalarOutput(p010Scalar.GetOutFrameSize());
			std::vector<BYTE> avx2Output(p010Avx2.GetOutFrameSize());
			Assert::IsTrue(p010Scalar.FormatVideoFrame(frame, scalarOutput.data()));
			Assert::IsTrue(p010Avx2.FormatVideoFrame(frame, avx2Output.data()));
			Assert::IsTrue(scalarOutput == avx2Output);

			CUYVYtoP210VideoFrameFormatter p210Scalar;
			p210Scalar.SetConversionMethod(
				CUYVYtoP210VideoFrameFormatter::ConversionMethod::SCALAR);
			p210Scalar.OnVideoState(state);
			CUYVYtoP210VideoFrameFormatter p210Avx2;
			p210Avx2.SetConversionMethod(
				CUYVYtoP210VideoFrameFormatter::ConversionMethod::AVX2);
			p210Avx2.OnVideoState(state);
			scalarOutput.assign(p210Scalar.GetOutFrameSize(), 0);
			avx2Output.assign(p210Avx2.GetOutFrameSize(), 0);
			Assert::IsTrue(p210Scalar.FormatVideoFrame(frame, scalarOutput.data()));
			Assert::IsTrue(p210Avx2.FormatVideoFrame(frame, avx2Output.data()));
			Assert::IsTrue(scalarOutput == avx2Output);
		}

		TEST_METHOD(AlphaNativeRgbLayoutPreservesComponentBitfields)
		{
			AlphaNativeRgbLayout layout;
			Assert::IsTrue(GetAlphaNativeRgbLayout(VideoFrameEncoding::R210, layout));
			Assert::IsTrue(layout.swapped);
			Assert::IsTrue(layout.limitedRange);
			Assert::IsFalse(AlphaCanUseNativeRgbUpload(VideoFrameEncoding::R210));
			Assert::AreEqual(10, layout.bitDepth);
			const uint32_t r210 = (1U << 20) | (2U << 10) | 3U;
			Assert::AreEqual(1U, static_cast<uint32_t>((r210 & layout.masks[0]) >> 20));
			Assert::AreEqual(2U, static_cast<uint32_t>((r210 & layout.masks[1]) >> 10));
			Assert::AreEqual(3U, static_cast<uint32_t>(r210 & layout.masks[2]));

			Assert::IsTrue(GetAlphaNativeRgbLayout(VideoFrameEncoding::R10l, layout));
			Assert::IsFalse(layout.swapped);
			Assert::IsTrue(layout.limitedRange);
			Assert::IsTrue(AlphaCanUseNativeRgbUpload(VideoFrameEncoding::R10l));
			const uint32_t r10 = (1U << 22) | (2U << 12) | (3U << 2);
			Assert::AreEqual(1U, static_cast<uint32_t>((r10 & layout.masks[0]) >> 22));
			Assert::AreEqual(2U, static_cast<uint32_t>((r10 & layout.masks[1]) >> 12));
			Assert::AreEqual(3U, static_cast<uint32_t>((r10 & layout.masks[2]) >> 2));

			Assert::IsTrue(GetAlphaNativeRgbLayout(VideoFrameEncoding::R10b, layout));
			Assert::IsTrue(layout.swapped);
			Assert::IsTrue(layout.limitedRange);
			Assert::IsTrue(AlphaCanUseNativeRgbUpload(VideoFrameEncoding::R10b));
			Assert::AreEqual(10, layout.bitDepth);
			Assert::IsFalse(GetAlphaNativeRgbLayout(VideoFrameEncoding::R12L, layout));
		}

		TEST_METHOD(CR210toRGB48VideoFrameFormatterGoldenTest)
		{
			CR210toRGB48VideoFrameFormatter vff;

			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(128, 100, false /* interlaced */, 24000, 1000);
			vs->videoFrameEncoding = VideoFrameEncoding::R210;

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

			// DeckLink defines r210 as SMPTE-range RGB: 64-960. Widening
			// preserves those codes by padding the low six bits. It must not
			// replicate high bits as though r210 were full-range 0-1023.
			WriteR210Pixel(input.data(), 64, 960, 512);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			const auto* components =
				reinterpret_cast<const uint16_t*>(output.data());
			Assert::AreEqual(static_cast<unsigned int>(64U << 6),
				static_cast<unsigned int>(components[0]));
			Assert::AreEqual(static_cast<unsigned int>(960U << 6),
				static_cast<unsigned int>(components[1]));
			Assert::AreEqual(static_cast<unsigned int>(512U << 6),
				static_cast<unsigned int>(components[2]));

			// Preserve out-of-nominal-range codes without clipping or full-range
			// replication so downstream range handling can make that decision.
			WriteR210Pixel(input.data(), 0, 1023, 63);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			Assert::AreEqual(0U, static_cast<unsigned int>(components[0]));
			Assert::AreEqual(static_cast<unsigned int>(1023U << 6),
				static_cast<unsigned int>(components[1]));
			Assert::AreEqual(static_cast<unsigned int>(63U << 6),
				static_cast<unsigned int>(components[2]));
		}

		TEST_METHOD(CR210toRGB48AVX2MatchesScalarBitExactly)
		{
			// A non-multiple-of-eight width exercises both the SIMD body and the
			// scalar tail while retaining DeckLink's aligned r210 row stride.
			VideoStateComPtr tailState = new VideoState();
			tailState->valid = true;
			tailState->displayMode = std::make_shared<DisplayMode>(
				102, 100, false, 60000, 1001);
			tailState->videoFrameEncoding = VideoFrameEncoding::R210;
			std::vector<BYTE> tailInput(tailState->BytesPerFrame());
			FillBenchmarkPattern(tailInput, 0x21048a52U);
			VideoFrame tailFrame(tailInput.data(), 1, 0, nullptr);

			CR210toRGB48VideoFrameFormatter tailScalar;
			tailScalar.SetConversionMethod(
				CR210toRGB48VideoFrameFormatter::ConversionMethod::SCALAR);
			tailScalar.OnVideoState(tailState);
			std::vector<BYTE> tailScalarOutput(tailScalar.GetOutFrameSize());
			Assert::IsTrue(tailScalar.FormatVideoFrame(
				tailFrame, tailScalarOutput.data()));

			CR210toRGB48VideoFrameFormatter tailAVX2;
			tailAVX2.SetConversionMethod(
				CR210toRGB48VideoFrameFormatter::ConversionMethod::AVX2);
			tailAVX2.OnVideoState(tailState);
			std::vector<BYTE> tailAVX2Output(tailAVX2.GetOutFrameSize());
			Assert::IsTrue(tailAVX2.FormatVideoFrame(
				tailFrame, tailAVX2Output.data()));
			Assert::IsTrue(tailScalarOutput == tailAVX2Output,
				L"r210 RGB48 AVX2 must match scalar through its pixel tail");

			// Exhaust all source codes in every channel and SIMD lane. The output
			// is limited-range code preservation: the low six bits remain zero.
			VideoStateComPtr exhaustiveState = new VideoState();
			exhaustiveState->valid = true;
			exhaustiveState->displayMode = std::make_shared<DisplayMode>(
				104, 100, false, 60000, 1001);
			exhaustiveState->videoFrameEncoding = VideoFrameEncoding::R210;
			std::vector<BYTE> exhaustiveInput(exhaustiveState->BytesPerFrame());
			CR210toRGB48VideoFrameFormatter exhaustiveAVX2;
			exhaustiveAVX2.SetConversionMethod(
				CR210toRGB48VideoFrameFormatter::ConversionMethod::AVX2);
			exhaustiveAVX2.OnVideoState(exhaustiveState);
			std::vector<BYTE> exhaustiveOutput(exhaustiveAVX2.GetOutFrameSize());
			VideoFrame exhaustiveFrame(exhaustiveInput.data(), 2, 0, nullptr);
			for (uint16_t code = 0; code <= 1023U; ++code)
			{
				for (uint32_t line = 0; line < 100; ++line)
				{
					BYTE* row = exhaustiveInput.data() +
						static_cast<size_t>(line) * exhaustiveState->BytesPerRow();
					for (uint32_t pixel = 0; pixel < 8; ++pixel)
						WriteR210Pixel(row + pixel * 4U, code, code, code);
				}
				Assert::IsTrue(exhaustiveAVX2.FormatVideoFrame(
					exhaustiveFrame, exhaustiveOutput.data()));
				const auto* samples = reinterpret_cast<const uint16_t*>(
					exhaustiveOutput.data());
				const uint16_t expected = static_cast<uint16_t>(code << 6);
				for (uint32_t component = 0; component < 24; ++component)
					Assert::AreEqual(static_cast<unsigned int>(expected),
						static_cast<unsigned int>(samples[component]));
			}

			VideoStateComPtr threadedState = new VideoState();
			threadedState->valid = true;
			threadedState->displayMode = std::make_shared<DisplayMode>(
				1920, 1080, false, 60000, 1001);
			threadedState->videoFrameEncoding = VideoFrameEncoding::R210;
			std::vector<BYTE> threadedInput(threadedState->BytesPerFrame());
			FillBenchmarkPattern(threadedInput, 0x8457c210U);
			VideoFrame threadedFrame(threadedInput.data(), 3, 0, nullptr);

			CR210toRGB48VideoFrameFormatter threadedScalar;
			threadedScalar.SetConversionMethod(
				CR210toRGB48VideoFrameFormatter::ConversionMethod::SCALAR);
			threadedScalar.OnVideoState(threadedState);
			std::vector<BYTE> threadedScalarOutput(
				threadedScalar.GetOutFrameSize());
			Assert::IsTrue(threadedScalar.FormatVideoFrame(
				threadedFrame, threadedScalarOutput.data()));

			CR210toRGB48VideoFrameFormatter threadedAVX2;
			threadedAVX2.SetConversionMethod(
				CR210toRGB48VideoFrameFormatter::ConversionMethod::AVX2);
			threadedAVX2.OnVideoState(threadedState);
			std::vector<BYTE> threadedAVX2Output(threadedAVX2.GetOutFrameSize());
			Assert::IsTrue(threadedAVX2.FormatVideoFrame(
				threadedFrame, threadedAVX2Output.data()));
			Assert::IsTrue(threadedScalarOutput == threadedAVX2Output,
				L"Threaded r210 RGB48 AVX2 must match scalar output bit-for-bit");
		}

		TEST_METHOD(CV210toP010VideoFrameFormatter4K60PerformanceSmokeTest)
		{
			CV210toP010VideoFrameFormatter formatter;
			VideoStateComPtr state = new VideoState();
			state->valid = true;
			state->displayMode = std::make_shared<DisplayMode>(
				3840, 2160, false, 60000, 1001);
			state->videoFrameEncoding = VideoFrameEncoding::V210;
			formatter.OnVideoState(state);

			std::vector<BYTE> input(state->BytesPerFrame(), 0);
			std::vector<BYTE> output(formatter.GetOutFrameSize(), 0xFF);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			for (int iteration = 0; iteration < 3; ++iteration)
				Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
			for (int iteration = 0; iteration < 30; ++iteration)
				Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));

			double currentUs = 0.0;
			double averageUs = 0.0;
			double maximumUs = 0.0;
			formatter.GetConversionPerformance(currentUs, averageUs, maximumUs);
			wchar_t message[160];
			swprintf_s(message,
				L"v210 4K60 to P010 current/avg/max: %.0f / %.0f / %.0f us",
				currentUs, averageUs, maximumUs);
			Logger::WriteMessage(message);
			Assert::IsTrue(averageUs < 16667.0,
				L"Average v210 conversion time exceeds one 60 fps frame period");
		}

		TEST_METHOD(DeckLinkConvertersSustained4K60PerformanceComparison)
		{
			bool allAveragesMeet60Fps = true;
			bool allP95sMeet60Fps = true;
			const auto recordResult = [&](const ConverterBenchmarkComparison& result)
			{
				allAveragesMeet60Fps = allAveragesMeet60Fps &&
					result.rotatingBuffers.averageUs < 16667.0;
				allP95sMeet60Fps = allP95sMeet60Fps &&
					result.rotatingBuffers.p95Us < 16667.0;
			};

			{
				CV210toP010VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					3840, 2160, false, 60000, 1001);
				state->videoFrameEncoding = VideoFrameEncoding::V210;
				formatter.OnVideoState(state);
				recordResult(CompareFormatterPerformance(L"v210-to-P010", formatter,
					state->BytesPerFrame(), formatter.GetOutFrameSize()));
			}

			{
				CV210toP210VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					3840, 2160, false, 60000, 1001);
				state->videoFrameEncoding = VideoFrameEncoding::V210;
				formatter.OnVideoState(state);
				recordResult(CompareFormatterPerformance(L"v210-to-P210", formatter,
					state->BytesPerFrame(), formatter.GetOutFrameSize()));
			}

			const VideoFrameEncoding uyvyEncodings[] = {
				VideoFrameEncoding::UYVY,
				VideoFrameEncoding::HDYC,
			};
			for (const auto encoding : uyvyEncodings)
			{
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					3840, 2160, false, 60000, 1001);
				state->videoFrameEncoding = encoding;

				CUYVYtoP010VideoFrameFormatter p010;
				p010.OnVideoState(state);
				std::wstring p010Name = ToString(encoding);
				p010Name += L"-to-P010";
				recordResult(CompareFormatterPerformance(p010Name.c_str(), p010,
					state->BytesPerFrame(), p010.GetOutFrameSize()));

				CUYVYtoP210VideoFrameFormatter p210;
				p210.OnVideoState(state);
				std::wstring p210Name = ToString(encoding);
				p210Name += L"-to-P210";
				recordResult(CompareFormatterPerformance(p210Name.c_str(), p210,
					state->BytesPerFrame(), p210.GetOutFrameSize()));
			}

			const VideoFrameEncoding argbEncodings[] = {
				VideoFrameEncoding::ARGB_8BIT,
				VideoFrameEncoding::BGRA_8BIT,
			};
			for (const auto encoding : argbEncodings)
			{
				CARGBtoP010VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					3840, 2160, false, 60000, 1001);
				state->videoFrameEncoding = encoding;
				state->colorspace = ColorSpace::BT_2020;
				formatter.OnVideoState(state);
				std::wstring converterName = ToString(encoding);
				converterName += L"-to-P010";
				recordResult(CompareFormatterPerformance(converterName.c_str(), formatter,
					state->BytesPerFrame(), formatter.GetOutFrameSize()));
			}

			{
				CNoopVideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					3840, 2160, false, 60000, 1001);
				state->videoFrameEncoding = VideoFrameEncoding::V210;
				formatter.OnVideoState(state);
				recordResult(CompareFormatterPerformance(L"v210-noop-copy", formatter,
					state->BytesPerFrame(), formatter.GetOutFrameSize()));
			}

			{
				CR210toRGB48VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					3840, 2160, false, 60000, 1001);
				state->videoFrameEncoding = VideoFrameEncoding::R210;
				formatter.OnVideoState(state);
				recordResult(CompareFormatterPerformance(L"r210-to-RGB48", formatter,
					state->BytesPerFrame(), formatter.GetOutFrameSize()));
			}

			{
				CR12BtoRGB48VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					3840, 2160, false, 60000, 1001);
				state->videoFrameEncoding = VideoFrameEncoding::R12B;
				formatter.OnVideoState(state);
				recordResult(CompareFormatterPerformance(L"R12B-to-RGB48", formatter,
					state->BytesPerFrame(), formatter.GetOutFrameSize()));
			}

			const VideoFrameEncoding packedRgbEncodings[] = {
				VideoFrameEncoding::R210,
				VideoFrameEncoding::R10b,
				VideoFrameEncoding::R10l,
				VideoFrameEncoding::R12B,
				VideoFrameEncoding::R12L
			};
			for (const auto encoding : packedRgbEncodings)
			{
				CDeckLinkRGBToP010VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					3840, 2160, false, 60000, 1001);
				state->videoFrameEncoding = encoding;
				state->colorspace = ColorSpace::BT_2020;
				formatter.OnVideoState(state);
				std::wstring converterName = ToString(encoding);
				converterName += L"-to-P010";
				recordResult(CompareFormatterPerformance(converterName.c_str(), formatter,
					state->BytesPerFrame(), formatter.GetOutFrameSize()));
			}

			Assert::IsTrue(allAveragesMeet60Fps,
				L"A sustained converter average exceeds one 60 fps frame period");
			Assert::IsTrue(allP95sMeet60Fps,
				L"A sustained converter p95 exceeds one 60 fps frame period");
		}

		TEST_METHOD(P010ConvertersUseOneVerticalChromaDownsamplingPolicy)
		{
			// Both source formats are 4:2:2. This oracle records the existing UYVY
			// rounded vertical average as the candidate common 4:2:0 policy. It is
			// intentionally red for v210 until chroma siting is explicitly decided.
			VideoStateComPtr uyvyState = new VideoState();
			uyvyState->valid = true;
			uyvyState->displayMode = std::make_shared<DisplayMode>(
				192, 100, false, 24000, 1000);
			uyvyState->videoFrameEncoding = VideoFrameEncoding::UYVY;
			std::vector<BYTE> uyvyInput(uyvyState->BytesPerFrame(), 0);
			for (uint32_t line = 0; line < 100; ++line)
			{
				BYTE* row = uyvyInput.data() +
					static_cast<size_t>(line) * uyvyState->BytesPerRow();
				const BYTE u = (line & 1U) == 0 ? 25 : 225;
				const BYTE v = (line & 1U) == 0 ? 50 : 250;
				for (uint32_t x = 0; x < 192; x += 2)
				{
					row[x * 2U + 0] = u;
					row[x * 2U + 1] = 16;
					row[x * 2U + 2] = v;
					row[x * 2U + 3] = 16;
				}
			}
			CUYVYtoP010VideoFrameFormatter uyvy;
			uyvy.OnVideoState(uyvyState);
			std::vector<BYTE> uyvyOutput(uyvy.GetOutFrameSize(), 0);
			VideoFrame uyvyFrame(uyvyInput.data(), 1, 0, nullptr);
			Assert::IsTrue(uyvy.FormatVideoFrame(uyvyFrame, uyvyOutput.data()));

			VideoStateComPtr v210State = new VideoState();
			v210State->valid = true;
			v210State->displayMode = uyvyState->displayMode;
			v210State->videoFrameEncoding = VideoFrameEncoding::V210;
			std::vector<BYTE> v210Input(v210State->BytesPerFrame(), 0);
			for (uint32_t line = 0; line < 100; ++line)
			{
				BYTE* row = v210Input.data() +
					static_cast<size_t>(line) * v210State->BytesPerRow();
				const uint16_t u = (line & 1U) == 0 ? 100 : 900;
				const uint16_t v = (line & 1U) == 0 ? 200 : 1000;
				for (uint32_t x = 0; x < 192; x += 6)
					WriteV210Pack(row + (x / 6U) * 16U,
						u, 64, v, 64, u, 64, v, 64, u, 64, v, 64);
			}

			const CV210toP010VideoFrameFormatter::ConversionMethod methods[] = {
				CV210toP010VideoFrameFormatter::ConversionMethod::STANDARD,
				CV210toP010VideoFrameFormatter::ConversionMethod::OPTIMIZED,
				CV210toP010VideoFrameFormatter::ConversionMethod::SIMD,
			};
			for (const auto method : methods)
			{
				CV210toP010VideoFrameFormatter v210;
				v210.SetConversionMethod(method);
				v210.OnVideoState(v210State);
				std::vector<BYTE> v210Output(v210.GetOutFrameSize(), 0);
				VideoFrame v210Frame(v210Input.data(), 1, 0, nullptr);
				Assert::IsTrue(v210.FormatVideoFrame(v210Frame, v210Output.data()));

				const auto* samples = reinterpret_cast<const uint16_t*>(
					v210Output.data());
				const size_t chromaStart = 192ULL * 100ULL;
				Assert::AreEqual(500U << 6,
					static_cast<unsigned int>(samples[chromaStart]));
				Assert::AreEqual(600U << 6,
					static_cast<unsigned int>(samples[chromaStart + 1]));
			}
		}

		TEST_METHOD(DisplayRuleExpressionTest)
		{
			const DisplayRuleExpression::ValueLookup values =
				[](const std::string& name, std::string& value)
				{
					if (name == "eotf") { value = "PQ"; return true; }
					if (name == "key") { value = "Ctrl+F4"; return true; }
					if (name == "source_rate") { value = "23"; return true; }
					if (name == "hdr_metadata") { value = "true"; return true; }
					return false;
				};

			std::string error;
			int specificity = 0;
			Assert::IsTrue(DisplayRuleExpression::Matches(
				"(${eotf} == PQ || ${eotf} == HLG) && ${source_rate} >= 23 && ${source_rate} < 31",
				values, specificity, error));
			Assert::AreEqual(3, specificity);
			Assert::IsTrue(DisplayRuleExpression::Matches(
				"${source_rate}==23-24 && !${hdr_metadata}==false", values, specificity, error));
			Assert::IsTrue(DisplayRuleExpression::Matches(
				"${key} == \"Ctrl+F4\" || ${key} == \"Ctrl+F5\"", values, specificity, error));
			Assert::IsFalse(DisplayRuleExpression::Matches(
				"${key} == \"Ctrl+F6\"", values, specificity, error));
			Assert::IsFalse(DisplayRuleExpression::Validate("${eotf} > PQ", error));
			Assert::IsTrue(error.find("supports only = and !=") != std::string::npos);
			Assert::IsFalse(DisplayRuleExpression::Validate("${unknown} == value", error));
			Assert::IsTrue(error.find("unknown variable") != std::string::npos);

			DisplayRuleExpression::Expression compiled;
			Assert::IsTrue(compiled.Compile(
				"${cadence}==24000/1001 || ${key}==\"Ctrl+F5\"", error, true));
			Assert::AreEqual(static_cast<size_t>(1), compiled.KeyChords().size());
			Assert::AreEqual("ctrl+f5",
				ConfigFile::NormalizeName(compiled.KeyChords().front()).c_str());
			Assert::IsTrue(compiled.Matches(
				[](const std::string& name, std::string& value)
				{
					if (name == "cadence") { value = "23.976"; return true; }
					if (name == "key") { value = "none"; return true; }
					return false;
				}, specificity, error));
			Assert::IsFalse(compiled.Compile("$key==F5", error, true));
			Assert::IsFalse(compiled.Compile("$transfer==PQ|HLG", error, true));
		}

		TEST_METHOD(AspectRatioParserAcceptsAndNormalizesDocumentedForms)
		{
			struct Case
			{
				const char* text;
				uint64_t numerator;
				uint64_t denominator;
			};
			for (const Case& test : std::vector<Case>{
				{ "4:3", 4, 3 },
				{ "16:9", 16, 9 },
				{ "16x9", 16, 9 },
				{ "16X9", 16, 9 },
				{ " 16 : 9 ", 16, 9 },
				{ "2:1", 2, 1 },
				{ "2.1:1", 21, 10 },
				{ "2100x1000", 21, 10 },
				{ "2.2:1", 11, 5 },
				{ "2.35:1", 47, 20 },
				{ "1.7777778", 8888889, 5000000 },
				{ "2.35", 47, 20 } })
			{
				AspectRatio aspect;
				std::string error;
				Assert::IsTrue(AspectRatioParser::Parse(
					test.text, 1.0, 4.0, aspect, error),
					std::wstring(error.begin(), error.end()).c_str());
				Assert::AreEqual<uint64_t>(
					test.numerator, aspect.numerator);
				Assert::AreEqual<uint64_t>(
					test.denominator, aspect.denominator);
			}
		}

		TEST_METHOD(AspectRatioParserRejectsMalformedAndOutOfRangeForms)
		{
			for (const char* text : {
				"", "0", "0:1", "1:0", "-16:9", "16:", ":9",
				"16::9", "16x9:1", "16:9junk", "nan", "inf",
				"0.99", "4.01" })
			{
				AspectRatio aspect;
				std::string error;
				Assert::IsFalse(AspectRatioParser::Parse(
					text, 1.0, 4.0, aspect, error));
				Assert::IsFalse(error.empty());
			}
		}

		TEST_METHOD(RendererProfileConfigRejectsRemovedViewportAliases)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0038-alias.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\npersist_profile_selection: false\n";
				for (const char* group : {
					"input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group <<
						"]\nprofiles: base\ndefault: base\n";
					file << "[profiles." << group << ".base]\n";
					if (std::string(group) == "viewport")
						file << "scope_screen_aspect: 2.35:1\n"
							"scope_automatic_crop: true\n"
							"scope_subtitle_fit: true\n"
							"scope_subtitle_release_drift_seconds: 1.5\n";
				}
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(
				config, model, error));
			Assert::IsTrue(error.find("scope_") != std::string::npos,
				L"Any removed scope-era viewport key must be rejected");
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsRemovedAliasAlongsideCanonicalKey)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0038-duplicate.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n";
				for (const char* group : {
					"input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group <<
						"]\nprofiles: base\ndefault: base\n";
					file << "[profiles." << group << ".base]\n";
					if (std::string(group) == "viewport")
						file << "screen_aspect: 16:9\n"
							"scope_screen_aspect: 2.35:1\n";
				}
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(
				config, model, error));
			Assert::IsTrue(error.find("scope_screen_aspect") !=
				std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigResolvesGenericViewportDefaults)
		{
			RendererProfileConfig::Model model;
			RendererProfileConfig::Profile normal;
			normal.group = "viewport";
			normal.name = "normal";
			model.profiles.emplace("viewport.normal", normal);
			RendererProfileConfig::Profile cinema;
			cinema.group = "viewport";
			cinema.name = "cinema";
			cinema.settings["screen_aspect"] = "2.35:1";
			cinema.settings["automatic_crop"] = "true";
			cinema.settings["subtitle_fit"] = "true";
			cinema.settings["subtitle_hold_seconds"] = "2";
			cinema.settings["subtitle_release_drift_seconds"] = "3";
			cinema.settings["subtitle_padding_pixels"] = "30";
			model.profiles.emplace("viewport.cinema", cinema);

			RendererProfileConfig::ResolvedViewport viewport;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(
				model, "normal", 1, viewport, error));
			Assert::AreEqual<uint64_t>(16, viewport.screenAspect.numerator);
			Assert::AreEqual<uint64_t>(9, viewport.screenAspect.denominator);
			Assert::IsFalse(viewport.hasScreenAspect);
			Assert::IsFalse(viewport.automaticCrop);
			Assert::IsFalse(viewport.subtitleFit);
			Assert::IsTrue(RendererProfileConfig::ResolveViewport(
				model, "cinema", 2, viewport, error));
			Assert::AreEqual<uint64_t>(47, viewport.screenAspect.numerator);
			Assert::AreEqual<uint64_t>(20, viewport.screenAspect.denominator);
			Assert::IsTrue(viewport.hasScreenAspect);
			Assert::IsTrue(viewport.automaticCrop);
			Assert::IsTrue(viewport.subtitleFit);
			Assert::AreEqual<uint64_t>(
				2000, viewport.subtitleHoldMilliseconds);
			Assert::AreEqual<uint64_t>(
				3000, viewport.subtitleReleaseDriftMilliseconds);
			Assert::AreEqual(30, viewport.subtitlePaddingPixels);
			Assert::AreEqual<uint64_t>(2, viewport.generation);
		}

		TEST_METHOD(UnifiedProfileRuntimeRestoresPublishesAndPersistsViewport)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string base = std::string(temporaryDirectory) +
				"VideoProcessor-vp0038-runtime-" +
				std::to_string(GetCurrentProcessId());
			const std::string configPath = base + ".cfg";
			const std::string statePath = base + ".state";
			{
				std::ofstream file(configPath,
					std::ios::out | std::ios::trunc);
				file << "[general]\npersist_profile_selection: true\n";
				for (const char* group :
					{ "input", "scaling", "display" })
				{
					file << "[profile_groups." << group <<
						"]\nprofiles: base\ndefault: base\n";
					file << "[profiles." << group << ".base]\n";
				}
				file << "[profile_groups.viewport]\n"
					"profiles: normal,scope\n"
					"default: normal\n"
					"[profiles.viewport.normal]\n"
					"when: $key==\"F3\"\n"
					"[profiles.viewport.scope]\n"
					"when: $key==\"F2\"\n"
					"screen_aspect: 2.35:1\n"
					"automatic_crop: true\n"
					"subtitle_fit: true\n"
					"subtitle_hold_seconds: 2\n"
					"subtitle_padding_pixels: 30\n";
			}
			{
				std::ofstream state(statePath,
					std::ios::out | std::ios::trunc);
				state << "profile.viewport: scope\n"
					"display_recovery.v1: DEADBEEF\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(configPath));
			UnifiedProfileRuntime::Runtime runtime;
			std::string error;
			const DisplayRuleExpression::ValueLookup source =
				[](const std::string& name, std::string& value)
				{
					if (name == "eotf" || name == "transfer")
					{
						value = "pq";
						return true;
					}
					if (name == "width")
					{
						value = "3840";
						return true;
					}
					if (name == "hdr_metadata")
					{
						value = "true";
						return true;
					}
					return false;
				};
			Assert::IsTrue(runtime.Initialize(config, source, error),
				std::wstring(error.begin(), error.end()).c_str());
			auto snapshot = runtime.GetSnapshot();
			Assert::IsNotNull(snapshot.get());
			Assert::AreEqual("scope",
				snapshot->viewport.profile.c_str());
			Assert::AreEqual<uint64_t>(
				47, snapshot->viewport.screenAspect.numerator);
			Assert::AreEqual<uint64_t>(
				20, snapshot->viewport.screenAspect.denominator);
			Assert::IsTrue(snapshot->viewport.automaticCrop);
			Assert::IsTrue(snapshot->viewport.subtitleFit);
			const StateVariables::Value* automaticCrop =
				snapshot->variables.Find("$automatic_crop");
			Assert::IsNotNull(automaticCrop);
			Assert::IsTrue(automaticCrop->type ==
				StateVariables::ValueType::Boolean);
			Assert::IsTrue(automaticCrop->boolean);
			const StateVariables::Value* screenAspect =
				snapshot->variables.Find("$screen_aspect");
			Assert::IsNotNull(screenAspect);
			Assert::IsTrue(screenAspect->type ==
				StateVariables::ValueType::Aspect);
			const StateVariables::Value* viewportProfile =
				snapshot->variables.Find("viewport_profile");
			Assert::IsNotNull(viewportProfile);
			Assert::AreEqual("scope", viewportProfile->text.c_str());
			const StateVariables::Value* groupProfile =
				snapshot->variables.Find("profile.viewport");
			Assert::IsNotNull(groupProfile);
			Assert::AreEqual("scope", groupProfile->text.c_str());
			const StateVariables::Value* eotf =
				snapshot->variables.Find("eotf");
			Assert::IsNotNull(eotf);
			Assert::AreEqual("pq", eotf->text.c_str());
			const StateVariables::Value* width =
				snapshot->variables.Find("width");
			Assert::IsNotNull(width);
			Assert::AreEqual(3840.0, width->number);
			const StateVariables::Value* hdrMetadata =
				snapshot->variables.Find("hdr_metadata");
			Assert::IsNotNull(hdrMetadata);
			Assert::IsTrue(hdrMetadata->boolean);

			UnifiedProfileRuntime::SelectionResult selection;
			Assert::IsTrue(runtime.SelectKey(
				"F3", source, selection, error));
			Assert::IsTrue(selection.changed);
			Assert::AreEqual("normal",
				selection.snapshot->viewport.profile.c_str());
			Assert::AreEqual<uint64_t>(
				16, selection.snapshot->viewport.screenAspect.numerator);
			Assert::AreEqual<uint64_t>(
				9, selection.snapshot->viewport.screenAspect.denominator);
			const uint64_t normalGeneration =
				selection.snapshot->generation;

			Assert::IsTrue(runtime.SelectKey(
				"F3", source, selection, error));
			Assert::IsFalse(selection.changed);
			Assert::AreEqual<uint64_t>(
				normalGeneration, selection.snapshot->generation);

			std::ifstream persisted(statePath);
			const std::string contents(
				(std::istreambuf_iterator<char>(persisted)),
				std::istreambuf_iterator<char>());
			Assert::IsTrue(contents.find(
				"profile.viewport: normal") != std::string::npos);
			Assert::IsTrue(contents.find(
				"display_recovery.v1: DEADBEEF") != std::string::npos,
				L"Profile persistence must preserve display recovery state");

			DeleteFileA(configPath.c_str());
			DeleteFileA(statePath.c_str());
			DeleteFileA((statePath + ".tmp").c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsIncompleteUnifiedConfiguration)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) + "VideoProcessor-vp0028-incomplete.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n";
				file << "[profile_groups.input]\nprofiles: sdr\ndefault: auto\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("profiles.input.sdr") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigAllowsOmittedGroupsWithoutPlaceholders)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0045-optional-groups.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"persist_profile_selection: true\n"
					"[profile_groups.viewport]\n"
					"profiles: normal\n"
					"default: normal\n"
					"[profiles.viewport.normal]\n"
					"[vpvr.display]\n"
					"quality: high\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsTrue(
				RendererProfileConfig::Read(config, model, error));
			Assert::AreEqual(static_cast<size_t>(1), model.groups.size());
			Assert::AreEqual("viewport", model.groups.front().name.c_str());
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsProfileForOmittedGroup)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0045-orphan-optional-group.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n"
					"persist_profile_selection: true\n"
					"[profiles.input.base]\n"
					"tone_mapping: AUTO\n"
					"[vpvr.display]\n"
					"quality: high\n";
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(
				RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find(
				"[profiles.input.base] is orphaned") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsConfigurationVersionKeys)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) +
				"VideoProcessor-vp0028-version.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\nconfig_version: 2\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("unknown key 'config_version'") !=
				std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigReadsOrderedIndependentGroups)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) + "VideoProcessor-vp0028-model.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\npersist_profile_selection: false\n";
				for (const char* group : { "input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group << "]\nprofiles: base\ndefault: auto\n";
					file << "[profiles." << group << ".base]\nwhen: $key==\"F5\"\npriority: 10\n";
				}
			}

			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::Read(config, model, error));
			Assert::AreEqual(static_cast<size_t>(4), model.groups.size());
			Assert::AreEqual("input", model.groups[0].name.c_str());
			Assert::AreEqual("viewport", model.groups[3].name.c_str());
			Assert::IsFalse(model.persistSelection);
			const auto profile = model.profiles.find("display.base");
			Assert::IsTrue(profile != model.profiles.end());
			Assert::AreEqual(10, profile->second.priority);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigOneKeySelectsIndependentGroups)
		{
			RendererProfileConfig::Model model;
			for (const char* groupName : { "input", "display" })
			{
				RendererProfileConfig::Group group;
				group.name = groupName;
				group.profiles = { "selected" };
				model.groups.push_back(group);
				RendererProfileConfig::Profile profile;
				profile.group = groupName;
				profile.name = "selected";
				profile.when = "$key==\"F5\"";
				std::string compileError;
				Assert::IsTrue(profile.whenExpression.Compile(profile.when, compileError, true));
				model.profiles.emplace(std::string(groupName) + ".selected", profile);
			}
			std::vector<RendererProfileConfig::KeySelection> selections;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F5",
				[](const std::string&, std::string&) { return false; }, selections, error));
			Assert::AreEqual(static_cast<size_t>(2), selections.size());
			Assert::AreEqual("input", selections[0].group.c_str());
			Assert::AreEqual("display", selections[1].group.c_str());
		}

		TEST_METHOD(RendererProfileConfigKeySelectionIgnoresOtherAutomaticBranches)
		{
			RendererProfileConfig::Model model;
			RendererProfileConfig::Group group;
			group.name = "display";
			group.profiles = { "f5", "f6" };
			model.groups.push_back(group);
			for (const auto& definition : std::vector<std::pair<std::string, std::string>>
				{ { "f5", "$transfer==PQ || $key==\"F5\"" },
				  { "f6", "$transfer==PQ || $key==\"F6\"" } })
			{
				RendererProfileConfig::Profile profile;
				profile.group = "display";
				profile.name = definition.first;
				profile.when = definition.second;
				std::string compileError;
				Assert::IsTrue(profile.whenExpression.Compile(profile.when, compileError, true));
				model.profiles.emplace("display." + profile.name, profile);
			}
			std::vector<RendererProfileConfig::KeySelection> selections;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F5",
				[](const std::string& name, std::string& value)
					{ if (name == "transfer") { value = "PQ"; return true; } return false; },
				selections, error));
			Assert::AreEqual(static_cast<size_t>(1), selections.size());
			Assert::AreEqual("f5", selections[0].profile.c_str());
		}

		TEST_METHOD(RendererProfileConfigResetChordDoesNotSuppressOtherProfileKeys)
		{
			RendererProfileConfig::Model model;
			RendererProfileConfig::Group group;
			group.name = "display";
			group.profiles = { "rec709", "bt2020" };
			group.resetWhen = "$key==\"F4\"";
			std::string resetCompileError;
			Assert::IsTrue(group.resetExpression.Compile(group.resetWhen, resetCompileError, true));
			model.groups.push_back(group);
			for (const auto& definition : std::vector<std::pair<std::string, std::string>>
				{ { "rec709", "$key==\"F5\"" }, { "bt2020", "$key==\"F6\"" } })
			{
				RendererProfileConfig::Profile profile;
				profile.group = group.name;
				profile.name = definition.first;
				profile.when = definition.second;
				std::string compileError;
				Assert::IsTrue(profile.whenExpression.Compile(profile.when, compileError, true));
				model.profiles.emplace(group.name + "." + profile.name, profile);
			}

			std::vector<RendererProfileConfig::KeySelection> selections;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::SelectForKey(model, "F5",
				[](const std::string&, std::string&) { return false; }, selections, error));
			Assert::AreEqual(static_cast<size_t>(1), selections.size());
			Assert::AreEqual("rec709", selections[0].profile.c_str());
		}

		TEST_METHOD(RendererProfileConfigCheckedInExamplesPassStartupValidation)
		{
			for (const char* path : {
				"VideoProcessor.cfg",
				"docs\\examples\\VideoProcessorRenderer.unified.proposed.cfg",
				"docs\\examples\\VideoProcessorRenderer.unified.minimal.proposed.cfg",
				"docs\\examples\\VideoProcessorRenderer.from-legacy.proposed.cfg" })
			{
				std::string absolutePath = __FILE__;
				const size_t sourceDirectory = absolutePath.rfind("\\src\\");
				Assert::IsTrue(sourceDirectory != std::string::npos);
				absolutePath.resize(sourceDirectory + 1);
				absolutePath += path;
				ConfigFile config;
				Assert::IsTrue(config.Load(absolutePath));
				RendererProfileConfig::Model model;
				std::string error;
				if (!RendererProfileConfig::Read(config, model, error))
				{
					const std::string detail = std::string(path) + ": " + error;
					Assert::Fail(std::wstring(detail.begin(), detail.end()).c_str());
				}
				if (std::string(path) == "VideoProcessor.cfg" &&
					!MainConfigSchema::Validate(config, error))
				{
					const std::string detail = std::string(path) + ": " + error;
					Assert::Fail(std::wstring(detail.begin(), detail.end()).c_str());
				}
			}
		}

		TEST_METHOD(RendererProfileConfigRejectsWrongOwnerAndUnknownSetting)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) + "VideoProcessor-vp0028-owner.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n";
				for (const char* group : { "input", "scaling", "display", "viewport" })
				{
					file << "[profile_groups." << group << "]\nprofiles: base\ndefault: base\n";
					file << "[profiles." << group << ".base]\n";
					if (std::string(group) == "input") file << "mode: scope\n";
				}
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("input-owned") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigRejectsMixedLegacyAndUnifiedConfiguration)
		{
			char temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathA(ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::string path = std::string(temporaryDirectory) + "VideoProcessor-vp0028-mixed.cfg";
			{
				std::ofstream file(path, std::ios::out | std::ios::trunc);
				file << "[general]\n[display_rules]\nrules: old\n";
			}
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			RendererProfileConfig::Model model;
			std::string error;
			Assert::IsFalse(RendererProfileConfig::Read(config, model, error));
			Assert::IsTrue(error.find("legacy [display_rules]") != std::string::npos);
			DeleteFileA(path.c_str());
		}

		TEST_METHOD(RendererProfileConfigUsesFirstMatchingProfileInFileOrder)
		{
			RendererProfileConfig::Model model;
			RendererProfileConfig::Group input;
			input.name = "input";
			input.profiles = { "sdr", "pq", "pq_specific" };
			input.defaultSelection = "sdr";
			model.groups.push_back(input);
			for (const auto& definition : std::vector<std::pair<std::string, std::string>>
				{ { "sdr", "$transfer==SDR" }, { "pq", "$transfer==PQ" },
				  { "pq_specific", "$transfer==PQ && $width>=3840" } })
			{
				RendererProfileConfig::Profile profile;
				profile.group = "input";
				profile.name = definition.first;
				profile.when = definition.second;
				std::string compileError;
				Assert::IsTrue(profile.whenExpression.Compile(profile.when, compileError, true));
				profile.priority = 100;
				model.profiles.emplace("input." + profile.name, profile);
			}

			std::vector<RendererProfileConfig::AutomaticSelection> selections;
			std::string error;
			Assert::IsTrue(RendererProfileConfig::SelectAutomatic(model,
				[](const std::string& name, std::string& value)
				{
					if (name == "transfer") { value = "PQ"; return true; }
					if (name == "width") { value = "3840"; return true; }
					return false;
				}, selections, error));
			Assert::AreEqual(static_cast<size_t>(1), selections.size());
			Assert::AreEqual("pq", selections[0].profile.c_str());
			Assert::IsFalse(selections[0].configuredDefault);
		}

		TEST_METHOD(CR210toRGB48VideoFrameFormatter4KSmokeTest)
		{
			CR210toRGB48VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(3840, 2160, false, 60000, 1001);
			vs->videoFrameEncoding = VideoFrameEncoding::R210;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			for (int i = 0; i < 5; ++i)
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			for (int i = 0; i < 30; ++i)
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));

			Assert::IsTrue(std::all_of(output.begin(), output.end(),
				[](BYTE value) { return value == 0; }));
			double currentUs = 0.0;
			double averageUs = 0.0;
			double maximumUs = 0.0;
			vff.GetConversionPerformance(currentUs, averageUs, maximumUs);
			wchar_t message[128];
			swprintf_s(message, L"Native R210 4K conversion current/avg/max: %.0f / %.0f / %.0f us",
				currentUs, averageUs, maximumUs);
			Logger::WriteMessage(message);
			Assert::IsTrue(averageUs < 16667.0,
				L"Average r210 RGB48 conversion exceeds one 60 fps frame period");
		}

		TEST_METHOD(CDeckLinkRGBToP010VideoFrameFormatterGoldenTest)
		{
			const VideoFrameEncoding encodings[] = {
				VideoFrameEncoding::R210,
				VideoFrameEncoding::R10b,
				VideoFrameEncoding::R10l,
				VideoFrameEncoding::R12L
			};

			for (const auto encoding : encodings)
			{
				CDeckLinkRGBToP010VideoFrameFormatter vff;
				VideoStateComPtr vs = new VideoState();
				vs->valid = true;
				vs->displayMode = std::make_shared<DisplayMode>(104, 100, false, 60000, 1001);
				vs->videoFrameEncoding = encoding;
				vs->colorspace = ColorSpace::REC_709;
				vff.OnVideoState(vs);
				Assert::AreEqual(31200L, vff.GetOutFrameSize());

				std::vector<BYTE> input(vs->BytesPerFrame(), 0);
				for (uint32_t line = 0; line < 100; ++line)
				{
					BYTE* row = input.data() + static_cast<size_t>(line) * vs->BytesPerRow();
					for (uint32_t x = 0; x < 104; x += 2)
					{
						if (encoding == VideoFrameEncoding::R12L)
							WriteR12LPixelPair(row, 4095, 0, 0, 4095, 0, 0);
						else if (encoding == VideoFrameEncoding::R210)
						{
							WriteR210Pixel(row, 960, 64, 64);
							WriteR210Pixel(row + 4, 960, 64, 64);
						}
						else
						{
							WriteR10Pixel(row, encoding, 940, 64, 64);
							WriteR10Pixel(row + 4, encoding, 940, 64, 64);
						}
						row += encoding == VideoFrameEncoding::R12L ? 9 : 8;
					}
				}

				std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
				VideoFrame frame(input.data(), 1, 0, nullptr);
				Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
				const uint16_t* words = reinterpret_cast<const uint16_t*>(output.data());
				const bool fullRange = encoding == VideoFrameEncoding::R12L;
				const unsigned int expectedY = (fullRange ? 217U : 250U) << 6;
				const unsigned int expectedCb = (fullRange ? 395U : 409U) << 6;
				const unsigned int expectedCr = (fullRange ? 1023U : 960U) << 6;
				for (size_t i = 0; i < 104ULL * 100; ++i)
					Assert::AreEqual(expectedY, static_cast<unsigned int>(words[i]));
				for (size_t i = 104ULL * 100; i < 104ULL * 150; i += 2)
				{
					Assert::AreEqual(expectedCb, static_cast<unsigned int>(words[i]));
					Assert::AreEqual(expectedCr, static_cast<unsigned int>(words[i + 1]));
				}
			}
		}

		TEST_METHOD(CDeckLinkLimitedRgbToP010MatchesBT709ReferenceCodes)
		{
			struct ExpectedColor
			{
				const char* name;
				uint16_t red;
				uint16_t green;
				uint16_t blue;
				uint16_t y;
				uint16_t cb;
				uint16_t cr;
			};

			const auto verify = [](VideoFrameEncoding encoding,
				const ExpectedColor* colors,
				size_t colorCount)
			{
				CDeckLinkRGBToP010VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					128, 100, false, 24000, 1000);
				state->videoFrameEncoding = encoding;
				state->colorspace = ColorSpace::REC_709;
				formatter.OnVideoState(state);

				std::vector<BYTE> input(state->BytesPerFrame(), 0);
				std::vector<BYTE> output(formatter.GetOutFrameSize(), 0);
				VideoFrame frame(input.data(), 1, 0, nullptr);
				for (size_t colorIndex = 0; colorIndex < colorCount; ++colorIndex)
				{
					const ExpectedColor& color = colors[colorIndex];
					for (uint32_t line = 0; line < 100; ++line)
					{
						BYTE* row = input.data() +
							static_cast<size_t>(line) * state->BytesPerRow();
						for (uint32_t x = 0; x < 128; ++x)
						{
							if (encoding == VideoFrameEncoding::R210)
								WriteR210Pixel(row + x * 4U,
									color.red, color.green, color.blue);
							else
								WriteR10Pixel(row + x * 4U, encoding,
									color.red, color.green, color.blue);
						}
					}

					Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
					const auto* samples =
						reinterpret_cast<const uint16_t*>(output.data());
					const size_t ySamples = 128ULL * 100ULL;
					const std::wstring message(color.name,
						color.name + strlen(color.name));
					Assert::AreEqual(static_cast<unsigned int>(color.y << 6),
						static_cast<unsigned int>(samples[0]), message.c_str());
					Assert::AreEqual(static_cast<unsigned int>(color.cb << 6),
						static_cast<unsigned int>(samples[ySamples]), message.c_str());
					Assert::AreEqual(static_cast<unsigned int>(color.cr << 6),
						static_cast<unsigned int>(samples[ySamples + 1]), message.c_str());
				}
			};

			// R10b/R10l use SMPTE RGB 64-940. Expected values are an
			// independent round-to-nearest BT.709 limited-range oracle.
			const ExpectedColor r10Colors[] = {
				{ "black", 64, 64, 64, 64, 512, 512 },
				{ "white", 940, 940, 940, 940, 512, 512 },
				{ "red",   940, 64, 64, 250, 409, 960 },
				{ "green", 64, 940, 64, 691, 167, 105 },
				{ "blue",  64, 64, 940, 127, 960, 471 },
			};
			verify(VideoFrameEncoding::R10b, r10Colors,
				_countof(r10Colors));
			verify(VideoFrameEncoding::R10l, r10Colors,
				_countof(r10Colors));

			// r210 has a distinct 64-960 RGB input span. It must normalize
			// that span into limited P010 rather than treating 960 as full-range
			// luma code 960.
			const ExpectedColor r210Colors[] = {
				{ "black", 64, 64, 64, 64, 512, 512 },
				{ "white", 960, 960, 960, 940, 512, 512 },
				{ "red",   960, 64, 64, 250, 409, 960 },
				{ "green", 64, 960, 64, 691, 167, 105 },
				{ "blue",  64, 64, 960, 127, 960, 471 },
			};
			verify(VideoFrameEncoding::R210, r210Colors,
				_countof(r210Colors));
		}

		TEST_METHOD(CDeckLinkLimitedRgbToP010MatchesBT2020ReferenceCodes)
		{
			struct ExpectedColor
			{
				uint16_t red;
				uint16_t green;
				uint16_t blue;
				uint16_t y;
				uint16_t cb;
				uint16_t cr;
			};

			const auto verify = [](VideoFrameEncoding encoding,
				const ExpectedColor* colors, size_t colorCount)
			{
				CDeckLinkRGBToP010VideoFrameFormatter formatter;
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					128, 100, false, 24000, 1000);
				state->videoFrameEncoding = encoding;
				state->colorspace = ColorSpace::BT_2020;
				formatter.OnVideoState(state);

				std::vector<BYTE> input(state->BytesPerFrame(), 0);
				std::vector<BYTE> output(formatter.GetOutFrameSize(), 0);
				VideoFrame frame(input.data(), 1, 0, nullptr);
				for (size_t colorIndex = 0; colorIndex < colorCount; ++colorIndex)
				{
					const ExpectedColor& color = colors[colorIndex];
					for (uint32_t line = 0; line < 100; ++line)
					{
						BYTE* row = input.data() +
							static_cast<size_t>(line) * state->BytesPerRow();
						for (uint32_t x = 0; x < 128; ++x)
						{
							if (encoding == VideoFrameEncoding::R210)
								WriteR210Pixel(row + x * 4U,
									color.red, color.green, color.blue);
							else
								WriteR10Pixel(row + x * 4U, encoding,
									color.red, color.green, color.blue);
						}
					}

					Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
					const auto* samples =
						reinterpret_cast<const uint16_t*>(output.data());
					const size_t chromaStart = 128ULL * 100ULL;
					Assert::AreEqual(static_cast<unsigned int>(color.y << 6),
						static_cast<unsigned int>(samples[0]));
					Assert::AreEqual(static_cast<unsigned int>(color.cb << 6),
						static_cast<unsigned int>(samples[chromaStart]));
					Assert::AreEqual(static_cast<unsigned int>(color.cr << 6),
						static_cast<unsigned int>(samples[chromaStart + 1]));
				}
			};

			const ExpectedColor r10Colors[] = {
				{ 940, 64, 64, 294, 387, 960 },
				{ 64, 940, 64, 658, 189, 100 },
				{ 64, 64, 940, 116, 960, 476 },
			};
			const ExpectedColor r210Colors[] = {
				{ 960, 64, 64, 294, 387, 960 },
				{ 64, 960, 64, 658, 189, 100 },
				{ 64, 64, 960, 116, 960, 476 },
			};
			verify(VideoFrameEncoding::R10b, r10Colors, _countof(r10Colors));
			verify(VideoFrameEncoding::R10l, r10Colors, _countof(r10Colors));
			verify(VideoFrameEncoding::R210, r210Colors, _countof(r210Colors));
		}

		TEST_METHOD(CDeckLinkLimitedRgbAVX2MatchesScalarBitExactly)
		{
			const VideoFrameEncoding encodings[] = {
				VideoFrameEncoding::R210,
				VideoFrameEncoding::R10b,
				VideoFrameEncoding::R10l,
			};
			const ColorSpace colorSpaces[] = {
				ColorSpace::REC_709,
				ColorSpace::BT_2020,
			};

			for (const auto colorSpace : colorSpaces)
			{
				for (const auto encoding : encodings)
				{
					// 130 exercises sixteen AVX2 blocks plus the two-pixel scalar tail.
					VideoStateComPtr state = new VideoState();
					state->valid = true;
					state->displayMode = std::make_shared<DisplayMode>(
						130, 100, false, 60000, 1001);
					state->videoFrameEncoding = encoding;
					state->colorspace = colorSpace;

					std::vector<BYTE> input(state->BytesPerFrame());
					FillBenchmarkPattern(input,
						0x13579bdfU ^ static_cast<uint32_t>(encoding) ^
						(static_cast<uint32_t>(colorSpace) << 16));
					for (uint32_t line = 0; line < 2; ++line)
					{
						BYTE* row = input.data() +
							static_cast<size_t>(line) * state->BytesPerRow();
						const uint16_t excursionCodes[][3] = {
							{ 0, 0, 0 }, { 1023, 1023, 1023 },
							{ 0, 1023, 63 }, { 1023, 0, 1023 },
							{ 64, 940, 960 }, { 960, 64, 940 },
							{ 1, 1022, 65 }, { 1022, 1, 959 },
						};
						for (uint32_t x = 0; x < _countof(excursionCodes); ++x)
						{
							if (encoding == VideoFrameEncoding::R210)
								WriteR210Pixel(row + x * 4U,
									excursionCodes[x][0], excursionCodes[x][1],
									excursionCodes[x][2]);
							else
								WriteR10Pixel(row + x * 4U, encoding,
									excursionCodes[x][0], excursionCodes[x][1],
									excursionCodes[x][2]);
						}
					}
					VideoFrame frame(input.data(), 1, 0, nullptr);

					CDeckLinkRGBToP010VideoFrameFormatter scalar;
					scalar.SetConversionMethod(
						CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::SCALAR);
					scalar.OnVideoState(state);
					std::vector<BYTE> scalarOutput(scalar.GetOutFrameSize(), 0x55);
					Assert::IsTrue(scalar.FormatVideoFrame(frame, scalarOutput.data()));

					CDeckLinkRGBToP010VideoFrameFormatter avx2;
					avx2.SetConversionMethod(
						CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::AVX2);
					avx2.OnVideoState(state);
					std::vector<BYTE> avx2Output(avx2.GetOutFrameSize(), 0xaa);
					Assert::IsTrue(avx2.FormatVideoFrame(frame, avx2Output.data()));
					Assert::IsTrue(scalarOutput == avx2Output,
						L"AVX2 packed RGB conversion must match scalar output bit-for-bit");
				}
			}

			// Cross the production thread-pool threshold and its row-pair segment
			// boundaries with one full-size randomized frame.
			VideoStateComPtr threadedState = new VideoState();
			threadedState->valid = true;
			threadedState->displayMode = std::make_shared<DisplayMode>(
				1920, 1080, false, 60000, 1001);
			threadedState->videoFrameEncoding = VideoFrameEncoding::R10b;
			threadedState->colorspace = ColorSpace::BT_2020;
			std::vector<BYTE> threadedInput(threadedState->BytesPerFrame());
			FillBenchmarkPattern(threadedInput, 0x2468ace0U);
			VideoFrame threadedFrame(threadedInput.data(), 1, 0, nullptr);

			CDeckLinkRGBToP010VideoFrameFormatter threadedScalar;
			threadedScalar.SetConversionMethod(
				CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::SCALAR);
			threadedScalar.OnVideoState(threadedState);
			std::vector<BYTE> threadedScalarOutput(
				threadedScalar.GetOutFrameSize());
			Assert::IsTrue(threadedScalar.FormatVideoFrame(
				threadedFrame, threadedScalarOutput.data()));

			CDeckLinkRGBToP010VideoFrameFormatter threadedAVX2;
			threadedAVX2.SetConversionMethod(
				CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::AVX2);
			threadedAVX2.OnVideoState(threadedState);
			std::vector<BYTE> threadedAVX2Output(threadedAVX2.GetOutFrameSize());
			Assert::IsTrue(threadedAVX2.FormatVideoFrame(
				threadedFrame, threadedAVX2Output.data()));
			Assert::IsTrue(threadedScalarOutput == threadedAVX2Output,
				L"Threaded AVX2 conversion must match threaded scalar output bit-for-bit");
		}

		TEST_METHOD(CDeckLinkR12AVX2MatchesScalarBitExactly)
		{
			const VideoFrameEncoding encodings[] = {
				VideoFrameEncoding::R12B,
				VideoFrameEncoding::R12L,
			};
			const ColorSpace colorSpaces[] = {
				ColorSpace::REC_709,
				ColorSpace::BT_2020,
			};
			for (const auto encoding : encodings)
			{
				for (const auto colorSpace : colorSpaces)
				{
					VideoStateComPtr state = new VideoState();
					state->valid = true;
					state->displayMode = std::make_shared<DisplayMode>(
						104, 100, false, 60000, 1001);
					state->videoFrameEncoding = encoding;
					state->colorspace = colorSpace;
					std::vector<BYTE> input(state->BytesPerFrame());
					FillBenchmarkPattern(input,
						0x5a5a1234U ^ static_cast<uint32_t>(encoding) ^
						(static_cast<uint32_t>(colorSpace) << 16));
					VideoFrame frame(input.data(), 1, 0, nullptr);

					CDeckLinkRGBToP010VideoFrameFormatter scalar;
					scalar.SetConversionMethod(
						CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::SCALAR);
					scalar.OnVideoState(state);
					std::vector<BYTE> scalarOutput(scalar.GetOutFrameSize(), 0x55);
					Assert::IsTrue(scalar.FormatVideoFrame(frame, scalarOutput.data()));

					CDeckLinkRGBToP010VideoFrameFormatter avx2;
					avx2.SetConversionMethod(
						CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::AVX2);
					avx2.OnVideoState(state);
					std::vector<BYTE> avx2Output(avx2.GetOutFrameSize(), 0xaa);
					Assert::IsTrue(avx2.FormatVideoFrame(frame, avx2Output.data()));
					Assert::IsTrue(scalarOutput == avx2Output,
						L"R12 AVX2 output must match scalar output bit-for-bit");
				}
			}

			// Exercise the production thread-pool split at its 1080p threshold,
			// including segment boundaries that the small exhaustive cases do
			// not reach.
			VideoStateComPtr threadedState = new VideoState();
			threadedState->valid = true;
			threadedState->displayMode = std::make_shared<DisplayMode>(
				1920, 1080, false, 60000, 1001);
			threadedState->videoFrameEncoding = VideoFrameEncoding::R12B;
			threadedState->colorspace = ColorSpace::BT_2020;
			std::vector<BYTE> threadedInput(threadedState->BytesPerFrame());
			FillBenchmarkPattern(threadedInput, 0x7ca45129U);
			VideoFrame threadedFrame(threadedInput.data(), 1, 0, nullptr);

			CDeckLinkRGBToP010VideoFrameFormatter threadedScalar;
			threadedScalar.SetConversionMethod(
				CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::SCALAR);
			threadedScalar.OnVideoState(threadedState);
			std::vector<BYTE> threadedScalarOutput(
				threadedScalar.GetOutFrameSize());
			Assert::IsTrue(threadedScalar.FormatVideoFrame(
				threadedFrame, threadedScalarOutput.data()));

			CDeckLinkRGBToP010VideoFrameFormatter threadedAVX2;
			threadedAVX2.SetConversionMethod(
				CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::AVX2);
			threadedAVX2.OnVideoState(threadedState);
			std::vector<BYTE> threadedAVX2Output(threadedAVX2.GetOutFrameSize());
			Assert::IsTrue(threadedAVX2.FormatVideoFrame(
				threadedFrame, threadedAVX2Output.data()));
			Assert::IsTrue(threadedScalarOutput == threadedAVX2Output,
				L"Threaded R12 AVX2 conversion must match scalar output bit-for-bit");
		}

		TEST_METHOD(CDeckLinkRGBToP010R12BBlackWhiteContractTest)
		{
			CDeckLinkRGBToP010VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(
				104, 100, false, 60000, 1001);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;
			vs->colorspace = ColorSpace::REC_709;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0xff);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			const uint16_t* words =
				reinterpret_cast<const uint16_t*>(output.data());
			const size_t lumaWords = 104ULL * 100;
			const size_t totalWords = lumaWords + 104ULL * 50;
			for (size_t i = 0; i < lumaWords; ++i)
				Assert::AreEqual(0U, static_cast<unsigned int>(words[i]));
			for (size_t i = lumaWords; i < totalWords; ++i)
				Assert::AreEqual(512U << 6,
					static_cast<unsigned int>(words[i]));

			std::fill(input.begin(), input.end(), 0xff);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			for (size_t i = 0; i < lumaWords; ++i)
				Assert::AreEqual(1023U << 6,
					static_cast<unsigned int>(words[i]));
			for (size_t i = lumaWords; i < totalWords; ++i)
				Assert::AreEqual(512U << 6,
					static_cast<unsigned int>(words[i]));
		}

		TEST_METHOD(CDeckLinkRGBToP010VideoFrameFormatter4KSmokeTest)
		{
			const VideoFrameEncoding encodings[] = {
				VideoFrameEncoding::R210,
				VideoFrameEncoding::R10b,
				VideoFrameEncoding::R10l,
				VideoFrameEncoding::R12B,
				VideoFrameEncoding::R12L
			};
			for (const auto encoding : encodings)
			{
				CDeckLinkRGBToP010VideoFrameFormatter vff;
				VideoStateComPtr vs = new VideoState();
				vs->valid = true;
				vs->displayMode = std::make_shared<DisplayMode>(3840, 2160, false, 60000, 1001);
				vs->videoFrameEncoding = encoding;
				vs->colorspace = ColorSpace::BT_2020;
				vff.OnVideoState(vs);

				std::vector<BYTE> input(vs->BytesPerFrame(), 0);
				const bool limitedInput = encoding == VideoFrameEncoding::R210 ||
					encoding == VideoFrameEncoding::R10b ||
					encoding == VideoFrameEncoding::R10l;
				if (limitedInput)
				{
					for (uint32_t line = 0; line < 2160; ++line)
					{
						BYTE* row = input.data() +
							static_cast<size_t>(line) * vs->BytesPerRow();
						for (uint32_t x = 0; x < 3840; ++x)
						{
							if (encoding == VideoFrameEncoding::R210)
								WriteR210Pixel(row + x * 4U, 64, 64, 64);
							else
								WriteR10Pixel(row + x * 4U, encoding, 64, 64, 64);
						}
					}
				}
				std::vector<BYTE> output(vff.GetOutFrameSize(), 0xFF);
				VideoFrame frame(input.data(), 1, 0, nullptr);
				for (int i = 0; i < 3; ++i)
					Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
				for (int i = 0; i < 15; ++i)
					Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));

				const uint16_t* words = reinterpret_cast<const uint16_t*>(output.data());
				const unsigned int expectedBlack = (limitedInput ? 64U : 0U) << 6;
				Assert::AreEqual(expectedBlack, static_cast<unsigned int>(words[0]));
				Assert::AreEqual(expectedBlack,
					static_cast<unsigned int>(words[3840ULL * 2160 - 1]));
				Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[3840ULL * 2160]));
				Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[3840ULL * 2160 + 1]));

				double currentUs = 0.0;
				double averageUs = 0.0;
				double maximumUs = 0.0;
				vff.GetConversionPerformance(currentUs, averageUs, maximumUs);
				wchar_t message[160];
				swprintf_s(message, L"Packed RGB %s 4K to P010 current/avg/max: %.0f / %.0f / %.0f us",
					ToString(encoding), currentUs, averageUs, maximumUs);
				Logger::WriteMessage(message);
				Assert::IsTrue(averageUs < 16667.0,
					L"Average packed RGB conversion exceeds one 60 fps frame period");
			}
		}

		TEST_METHOD(CDeckLinkRGBToP010VideoFrameFormatterR12BGoldenTest)
		{
			CDeckLinkRGBToP010VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(104, 100, false, 60000, 1001);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;
			vs->colorspace = ColorSpace::REC_709;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			for (uint32_t line = 0; line < 100; ++line)
			{
				BYTE* row = input.data() + static_cast<size_t>(line) * vs->BytesPerRow();
				for (uint32_t x = 0; x < 104; x += 8)
				{
					WriteR12BBlock(row, 4095, 0, 0);
					row += 36;
				}
			}

			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(frame, output.data()));
			const uint16_t* words = reinterpret_cast<const uint16_t*>(output.data());
			for (size_t i = 0; i < 104ULL * 100; ++i)
				Assert::AreEqual(217U << 6, static_cast<unsigned int>(words[i]));
			for (size_t i = 104ULL * 100; i < 104ULL * 150; i += 2)
			{
				Assert::AreEqual(395U << 6, static_cast<unsigned int>(words[i]));
				Assert::AreEqual(1023U << 6, static_cast<unsigned int>(words[i + 1]));
			}
		}

		TEST_METHOD(CDeckLinkRGBToP010VideoFrameFormatterR12BEndpointsAndWidthValidation)
		{
			CDeckLinkRGBToP010VideoFrameFormatter vff;
			VideoStateComPtr vs = new VideoState();
			vs->valid = true;
			vs->displayMode = std::make_shared<DisplayMode>(104, 100, false, 60000, 1001);
			vs->videoFrameEncoding = VideoFrameEncoding::R12B;
			vs->colorspace = ColorSpace::REC_709;
			vff.OnVideoState(vs);

			std::vector<BYTE> input(vs->BytesPerFrame(), 0);
			std::vector<BYTE> output(vff.GetOutFrameSize(), 0);
			VideoFrame black(input.data(), 1, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(black, output.data()));
			const uint16_t* words = reinterpret_cast<const uint16_t*>(output.data());
			const size_t ySamples = 104ULL * 100;
			Assert::AreEqual(0U, static_cast<unsigned int>(words[0]));
			Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[ySamples]));
			Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[ySamples + 1]));

			for (uint32_t line = 0; line < 100; ++line)
			{
				BYTE* row = input.data() + static_cast<size_t>(line) * vs->BytesPerRow();
				for (uint32_t x = 0; x < 104; x += 8)
				{
					WriteR12BBlock(row, 4095, 4095, 4095);
					row += 36;
				}
			}
			VideoFrame white(input.data(), 2, 0, nullptr);
			Assert::IsTrue(vff.FormatVideoFrame(white, output.data()));
			Assert::AreEqual(1023U << 6, static_cast<unsigned int>(words[0]));
			Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[ySamples]));
			Assert::AreEqual(512U << 6, static_cast<unsigned int>(words[ySamples + 1]));

			VideoStateComPtr invalid = new VideoState();
			invalid->valid = true;
			invalid->displayMode = std::make_shared<DisplayMode>(100, 100, false, 60000, 1001);
			invalid->videoFrameEncoding = VideoFrameEncoding::R12B;
			Assert::ExpectException<std::runtime_error>([&]() { vff.OnVideoState(invalid); });
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

		TEST_METHOD(CR12BtoRGB48UsesDocumentedFullRangeScalingAtVideoCodePoints)
		{
			CR12BtoRGB48VideoFrameFormatter formatter;
			VideoStateComPtr state = new VideoState();
			state->valid = true;
			state->displayMode = std::make_shared<DisplayMode>(
				104, 100, false, 24000, 1000);
			state->videoFrameEncoding = VideoFrameEncoding::R12B;
			formatter.OnVideoState(state);

			std::vector<BYTE> input(state->BytesPerFrame(), 0);
			std::vector<BYTE> output(formatter.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			const uint16_t referenceCodes[] = {
				0x000, 0x001, 0x089, 0x100, 0x800, 0xEB0, 0xFFE, 0xFFF
			};

			for (const uint16_t code : referenceCodes)
			{
				WriteR12BBlock(input.data(), code, code, code);
				Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
				const auto* components =
					reinterpret_cast<const uint16_t*>(output.data());
				const uint16_t expected = static_cast<uint16_t>(
					(code << 4) | (code >> 8));
				Assert::AreEqual(static_cast<unsigned int>(expected),
					static_cast<unsigned int>(components[0]));
				Assert::AreEqual(static_cast<unsigned int>(expected),
					static_cast<unsigned int>(components[1]));
				Assert::AreEqual(static_cast<unsigned int>(expected),
					static_cast<unsigned int>(components[2]));
			}

			// These values specifically distinguish documented full-range R12B
			// scaling from limited-range zero padding.
			Assert::AreEqual(0x1001U,
				static_cast<unsigned int>((0x100U << 4) | (0x100U >> 8)));
			Assert::AreEqual(0xEB0EU,
				static_cast<unsigned int>((0xEB0U << 4) | (0xEB0U >> 8)));
		}

		TEST_METHOD(CR12BtoRGB48AVX2MatchesScalarBitExactly)
		{
			VideoStateComPtr tailState = new VideoState();
			tailState->valid = true;
			tailState->displayMode = std::make_shared<DisplayMode>(
				104, 100, false, 60000, 1001);
			tailState->videoFrameEncoding = VideoFrameEncoding::R12B;
			std::vector<BYTE> tailInput(tailState->BytesPerFrame());
			FillBenchmarkPattern(tailInput, 0x12b48a52U);
			VideoFrame tailFrame(tailInput.data(), 1, 0, nullptr);

			CR12BtoRGB48VideoFrameFormatter tailScalar;
			tailScalar.SetConversionMethod(
				CR12BtoRGB48VideoFrameFormatter::ConversionMethod::SCALAR);
			tailScalar.OnVideoState(tailState);
			std::vector<BYTE> tailScalarOutput(tailScalar.GetOutFrameSize());
			Assert::IsTrue(tailScalar.FormatVideoFrame(
				tailFrame, tailScalarOutput.data()));

			CR12BtoRGB48VideoFrameFormatter tailAVX2;
			tailAVX2.SetConversionMethod(
				CR12BtoRGB48VideoFrameFormatter::ConversionMethod::AVX2);
			tailAVX2.OnVideoState(tailState);
			std::vector<BYTE> tailAVX2Output(tailAVX2.GetOutFrameSize());
			Assert::IsTrue(tailAVX2.FormatVideoFrame(
				tailFrame, tailAVX2Output.data()));
			Assert::IsTrue(tailScalarOutput == tailAVX2Output,
				L"R12B RGB48 AVX2 must match scalar through its eight-pixel tail");

			// Exhaust every source code in both documented eight-pixel blocks of
			// one SIMD unit and prove exact endpoint-preserving bit replication.
			VideoStateComPtr exhaustiveState = new VideoState();
			exhaustiveState->valid = true;
			exhaustiveState->displayMode = std::make_shared<DisplayMode>(
				112, 100, false, 60000, 1001);
			exhaustiveState->videoFrameEncoding = VideoFrameEncoding::R12B;
			std::vector<BYTE> exhaustiveInput(exhaustiveState->BytesPerFrame());
			CR12BtoRGB48VideoFrameFormatter exhaustiveAVX2;
			exhaustiveAVX2.SetConversionMethod(
				CR12BtoRGB48VideoFrameFormatter::ConversionMethod::AVX2);
			exhaustiveAVX2.OnVideoState(exhaustiveState);
			std::vector<BYTE> exhaustiveOutput(exhaustiveAVX2.GetOutFrameSize());
			VideoFrame exhaustiveFrame(exhaustiveInput.data(), 2, 0, nullptr);
			for (uint32_t code = 0; code <= 0xfffU; ++code)
			{
				for (uint32_t line = 0; line < 100; ++line)
				{
					BYTE* row = exhaustiveInput.data() +
						static_cast<size_t>(line) * exhaustiveState->BytesPerRow();
					WriteR12BBlock(row, static_cast<uint16_t>(code),
						static_cast<uint16_t>(code), static_cast<uint16_t>(code));
					WriteR12BBlock(row + 36, static_cast<uint16_t>(code),
						static_cast<uint16_t>(code), static_cast<uint16_t>(code));
				}
				Assert::IsTrue(exhaustiveAVX2.FormatVideoFrame(
					exhaustiveFrame, exhaustiveOutput.data()));
				const auto* samples = reinterpret_cast<const uint16_t*>(
					exhaustiveOutput.data());
				const uint16_t expected = static_cast<uint16_t>(
					(code << 4) | (code >> 8));
				Assert::AreEqual(static_cast<unsigned int>(expected),
					static_cast<unsigned int>(samples[0]));
				Assert::AreEqual(static_cast<unsigned int>(expected),
					static_cast<unsigned int>(samples[47]));
			}

			VideoStateComPtr threadedState = new VideoState();
			threadedState->valid = true;
			threadedState->displayMode = std::make_shared<DisplayMode>(
				1920, 1080, false, 60000, 1001);
			threadedState->videoFrameEncoding = VideoFrameEncoding::R12B;
			std::vector<BYTE> threadedInput(threadedState->BytesPerFrame());
			FillBenchmarkPattern(threadedInput, 0x8457c129U);
			VideoFrame threadedFrame(threadedInput.data(), 3, 0, nullptr);

			CR12BtoRGB48VideoFrameFormatter threadedScalar;
			threadedScalar.SetConversionMethod(
				CR12BtoRGB48VideoFrameFormatter::ConversionMethod::SCALAR);
			threadedScalar.OnVideoState(threadedState);
			std::vector<BYTE> threadedScalarOutput(
				threadedScalar.GetOutFrameSize());
			Assert::IsTrue(threadedScalar.FormatVideoFrame(
				threadedFrame, threadedScalarOutput.data()));

			CR12BtoRGB48VideoFrameFormatter threadedAVX2;
			threadedAVX2.SetConversionMethod(
				CR12BtoRGB48VideoFrameFormatter::ConversionMethod::AVX2);
			threadedAVX2.OnVideoState(threadedState);
			std::vector<BYTE> threadedAVX2Output(threadedAVX2.GetOutFrameSize());
			Assert::IsTrue(threadedAVX2.FormatVideoFrame(
				threadedFrame, threadedAVX2Output.data()));
			Assert::IsTrue(threadedScalarOutput == threadedAVX2Output,
				L"Threaded R12B RGB48 AVX2 must match scalar output bit-for-bit");
		}

		TEST_METHOD(CDeckLinkR12BToP010UsesNormalizedFullRangeRounding)
		{
			CDeckLinkRGBToP010VideoFrameFormatter formatter;
			VideoStateComPtr state = new VideoState();
			state->valid = true;
			state->displayMode = std::make_shared<DisplayMode>(
				104, 100, false, 24000, 1000);
			state->videoFrameEncoding = VideoFrameEncoding::R12B;
			state->colorspace = ColorSpace::REC_709;
			formatter.OnVideoState(state);

			std::vector<BYTE> input(state->BytesPerFrame(), 0);
			std::vector<BYTE> output(formatter.GetOutFrameSize(), 0);
			VideoFrame frame(input.data(), 1, 0, nullptr);
			const uint16_t referenceCodes[] = {
				0x000, 0x001, 0x002, 0x089, 0x100, 0x800, 0xEB0, 0xFFE, 0xFFF
			};

			for (const uint16_t code : referenceCodes)
			{
				WriteR12BBlock(input.data(), code, code, code);
				Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));
				const auto* samples =
					reinterpret_cast<const uint16_t*>(output.data());
				const uint16_t expected10 = static_cast<uint16_t>(
					(static_cast<uint32_t>(code) * 1023U + 2047U) / 4095U);
				Assert::AreEqual(static_cast<unsigned int>(expected10 << 6),
					static_cast<unsigned int>(samples[0]));
			}
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
			Assert::IsTrue(averageUs < 16667.0,
				L"Average R12B RGB48 conversion exceeds one 60 fps frame period");
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
