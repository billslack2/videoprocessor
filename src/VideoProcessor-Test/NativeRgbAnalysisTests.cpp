#include "pch.h"

#include <AnalysisLumaSource.h>
#include <P010ActivePictureEvidence.h>
#include <SceneDetector.h>
#include "CppUnitTest.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	namespace
	{
		struct BgraFrame
		{
			int width;
			int height;
			std::vector<uint8_t> pixels;

			BgraFrame(int frameWidth, int frameHeight) :
				width(frameWidth), height(frameHeight),
				pixels(static_cast<size_t>(frameWidth) * frameHeight * 4, 0)
			{
				Fill(75);
			}

			void Fill(uint8_t value)
			{
				for (size_t offset = 0; offset < pixels.size(); offset += 4)
				{
					pixels[offset + 0] = value;
					pixels[offset + 1] = value;
					pixels[offset + 2] = value;
					pixels[offset + 3] = 255;
				}
			}

			void BlackOutside(int top, int bottom)
			{
				for (int y = 0; y < height; ++y)
					if (y < top || y >= bottom)
						for (int x = 0; x < width; ++x)
						{
							const size_t offset =
								(static_cast<size_t>(y) * width + x) * 4;
							pixels[offset + 0] = 16;
							pixels[offset + 1] = 16;
							pixels[offset + 2] = 16;
						}
			}

			AnalysisLumaSource Source(uint64_t generation = 17) const
			{
				return { pixels.data(), pixels.size(), width, height,
					static_cast<size_t>(width) * 4, 0,
					AnalysisLumaFormat::NativeRgb,
					VideoFrameEncoding::BGRA_8BIT, ColorSpace::REC_709,
					generation };
			}
		};

		struct R210Frame
		{
			int width;
			int height;
			std::vector<uint8_t> pixels;

			R210Frame(int frameWidth, int frameHeight) :
				width(frameWidth), height(frameHeight),
				pixels(static_cast<size_t>(frameWidth) * frameHeight * 4, 0)
			{
				Fill(301);
			}

			void SetPixel(int x, int y, uint16_t red, uint16_t green,
				uint16_t blue)
			{
				const uint32_t word = (static_cast<uint32_t>(red) << 20) |
					(static_cast<uint32_t>(green) << 10) | blue;
				const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
				pixels[offset + 0] = static_cast<uint8_t>(word >> 24);
				pixels[offset + 1] = static_cast<uint8_t>(word >> 16);
				pixels[offset + 2] = static_cast<uint8_t>(word >> 8);
				pixels[offset + 3] = static_cast<uint8_t>(word);
			}

			void Fill(uint16_t code)
			{
				for (int y = 0; y < height; ++y)
					for (int x = 0; x < width; ++x)
						SetPixel(x, y, code, code, code);
			}

			void BlackOutside(int top, int bottom)
			{
				for (int y = 0; y < height; ++y)
					if (y < top || y >= bottom)
						for (int x = 0; x < width; ++x)
							SetPixel(x, y, 64, 64, 64);
			}

			void DarkBlueOutside(int top, int bottom)
			{
				for (int y = 0; y < height; ++y)
					if (y < top || y >= bottom)
						for (int x = 0; x < width; ++x)
							SetPixel(x, y, 0, 0, 128);
			}

			AnalysisLumaSource Source(uint64_t generation = 17) const
			{
				return { pixels.data(), pixels.size(), width, height,
					static_cast<size_t>(width) * 4, 0,
					AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::R210,
					ColorSpace::REC_709, generation };
			}
		};
	}

	TEST_CLASS(NativeRgbAnalysisTests)
	{
	public:
		TEST_METHOD(BgraBarsProduceTrustedSourceCoordinateEvidence)
		{
			BgraFrame frame(320, 180);
			frame.BlackOutside(22, 158);
			const P010ActivePictureEvidence evidence =
				ExtractActivePictureEvidence(frame.Source());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::IsTrue(evidence.trustedBounds.top >= 20);
			Assert::IsTrue(evidence.trustedBounds.bottom <= 160);
			Assert::IsTrue(evidence.lumaSamples < 30000);
		}

		TEST_METHOD(R210BarsProduceTrustedSourceCoordinateEvidence)
		{
			R210Frame frame(320, 180);
			frame.BlackOutside(22, 158);
			const P010ActivePictureEvidence evidence =
				ExtractActivePictureEvidence(frame.Source());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::IsTrue(evidence.trustedBounds.top >= 20);
			Assert::IsTrue(evidence.trustedBounds.bottom <= 160);
		}

		TEST_METHOD(R210ColoredDarkEdgesCannotAuthorizeCrop)
		{
			R210Frame frame(320, 180);
			frame.DarkBlueOutside(22, 158);
			const P010ActivePictureEvidence evidence =
				ExtractActivePictureEvidence(frame.Source());
			Assert::AreNotEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(R210SceneDetectorUsesGenerationSafeSparseSamples)
		{
			R210Frame bright(64, 36);
			bright.Fill(192);
			R210Frame black(64, 36);
			black.Fill(0);
			SceneDetector detector;
			const AnalysisLumaSource brightSource = bright.Source(7);
			const AnalysisLumaSource blackSource = black.Source(7);
			const AnalysisLumaSource resetSource = bright.Source(8);
			const SceneDetectorResult first = detector.Analyze({
				nullptr, 64, 36, 0, 1, 417083, 7, 417083, true,
				&brightSource });
			const SceneDetectorResult boundary = detector.Analyze({
				nullptr, 64, 36, 0, 2, 834166, 7, 417083, true,
				&blackSource });
			const SceneDetectorResult reset = detector.Analyze({
				nullptr, 64, 36, 0, 3, 1251249, 8, 417083, true,
				&resetSource });
			Assert::AreEqual(static_cast<int>(SceneDetectorStatus::Warming),
				static_cast<int>(first.status));
			Assert::IsTrue(boundary.safeBoundary);
			Assert::AreEqual(static_cast<int>(SceneDetectorStatus::Warming),
				static_cast<int>(reset.status));
		}

		TEST_METHOD(PackedTenBitEndianLayoutsPreserveComponentEndpoints)
		{
			const uint8_t bigEndian[] = { 0xff, 0xc0, 0x00, 0x00 };
			const uint8_t littleEndian[] = { 0x00, 0x00, 0xc0, 0xff };
			AnalysisLumaSample bigSample;
			AnalysisLumaSample littleSample;
			AnalysisLumaSource big = { bigEndian, sizeof(bigEndian), 1, 1, 4, 0,
				AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::R10b,
				ColorSpace::REC_709, 1 };
			AnalysisLumaSource little = { littleEndian, sizeof(littleEndian), 1, 1, 4, 0,
				AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::R10l,
				ColorSpace::REC_709, 1 };
			Assert::IsTrue(big.Sample(0, 0, bigSample));
			Assert::IsTrue(little.Sample(0, 0, littleSample));
			Assert::AreEqual(static_cast<int>(bigSample.luma),
				static_cast<int>(littleSample.luma));
			Assert::IsTrue(bigSample.luma > 200);
		}
	};
}
