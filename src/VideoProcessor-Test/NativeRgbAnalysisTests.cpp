#include "pch.h"

#include <AnalysisLumaSource.h>
#include <ActivePictureEvidence.h>
#include <SceneDetector.h>
#include <SubtitleCadenceGuard.h>
#include "CppUnitTest.h"

#include <array>
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

			void FillRectangle(int left, int top, int right, int bottom,
				uint8_t value)
			{
				for (int y = top; y < bottom; ++y)
					for (int x = left; x < right; ++x)
					{
						const size_t offset =
							(static_cast<size_t>(y) * width + x) * 4;
						pixels[offset + 0] = value;
						pixels[offset + 1] = value;
						pixels[offset + 2] = value;
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

			void BlackOutside(int left, int top, int right, int bottom)
			{
				for (int y = 0; y < height; ++y)
					for (int x = 0; x < width; ++x)
						if (x < left || x >= right || y < top || y >= bottom)
							SetPixel(x, y, 64, 64, 64);
			}

			void DarkBlueOutside(int left, int top, int right, int bottom)
			{
				for (int y = 0; y < height; ++y)
					for (int x = 0; x < width; ++x)
						if (x < left || x >= right || y < top || y >= bottom)
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
        TEST_METHOD(ValidatedSamplerMatchesCanonicalCodesAcrossFormatsRangesAndCoordinates)
        {
            struct Layout {AnalysisLumaFormat format;VideoFrameEncoding encoding;uint8_t alignment;};
            const Layout layouts[]={
                {AnalysisLumaFormat::P010,VideoFrameEncoding::V210,0},
                {AnalysisLumaFormat::P210,VideoFrameEncoding::V210,0},
                {AnalysisLumaFormat::DecodedDolbyVision422,VideoFrameEncoding::V210,0},
                {AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::UYVY,0},
                {AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::HDYC,0},
                {AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,0},
                {AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::BGRA_8BIT,0},
                {AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::ARGB_8BIT,0},
                {AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::R210,0},
                {AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::R10b,0},
                {AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::R10l,0},
                {AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::R12B,0},
                {AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::R12L,0},
                {AnalysisLumaFormat::SdrTunnelRgb,VideoFrameEncoding::R210,1},
                {AnalysisLumaFormat::SdrTunnelRgb,VideoFrameEncoding::R210,2},
                {AnalysisLumaFormat::SdrTunnelRgb,VideoFrameEncoding::R10b,2},
                {AnalysisLumaFormat::SdrTunnelRgb,VideoFrameEncoding::R10l,2},
                {AnalysisLumaFormat::SdrTunnelRgb,VideoFrameEncoding::BGRA_8BIT,2},
                {AnalysisLumaFormat::SdrTunnelRgb,VideoFrameEncoding::ARGB_8BIT,2}};
            std::vector<uint8_t> bytes(4096);
            for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t((i*73+(i>>3)*19+41)&255);
            for(const auto& layout:layouts)for(auto range:{AnalysisLumaRange::Unknown,AnalysisLumaRange::Limited,AnalysisLumaRange::Full})
                for(auto colorspace:{ColorSpace::REC_709,ColorSpace::BT_2020}) {
                    AnalysisLumaSource source{bytes.data(),bytes.size(),24,8,128,128,layout.format,layout.encoding,colorspace,17};
                    source.range=range;source.rgbBitAlignment=layout.alignment;
                    ValidatedAnalysisLumaSampler sampler(source);
                    Assert::AreEqual(source.IsValid(),sampler.IsValid());
                    for(int y=-1;y<=8;++y)for(int x=-1;x<=24;++x) {
                        AnalysisLumaSample expected,actual;
                        const bool canonical=source.Sample(x,y,expected),fast=sampler.Sample(x,y,actual);
                        Assert::AreEqual(canonical,fast);
                        if(canonical) {
                            Assert::AreEqual(int(expected.luma),int(actual.luma));
                            Assert::AreEqual(int(expected.chromaU),int(actual.chromaU));
                            Assert::AreEqual(int(expected.chromaV),int(actual.chromaV));
                        }
                    }
                }
        }
        TEST_METHOD(ValidatedSamplerRejectsInvalidViewsAndRetainsImmutableOwnedMetadata)
        {
            auto owner=std::make_shared<std::vector<uint8_t>>(4096,127);
            std::weak_ptr<const void> lifetime=owner;
            AnalysisLumaSource source{owner->data(),owner->size(),24,8,128,128,
                AnalysisLumaFormat::P210,VideoFrameEncoding::V210,ColorSpace::REC_709,17};
            source.backingOwner=owner;
            AnalysisLumaSample expected;Assert::IsTrue(source.Sample(23,7,expected));
            ValidatedAnalysisLumaSampler sampler(source);
            source={};owner.reset();Assert::IsFalse(lifetime.expired());
            AnalysisLumaSample actual;Assert::IsTrue(sampler.Sample(23,7,actual));
            Assert::AreEqual(int(expected.luma),int(actual.luma));
            Assert::IsFalse(sampler.Sample(24,7,actual));Assert::IsFalse(sampler.Sample(23,8,actual));
            for(int fault=0;fault<5;++fault) {
                std::vector<uint8_t> bytes(4096);
                AnalysisLumaSource invalid{bytes.data(),bytes.size(),24,8,128,128,
                    AnalysisLumaFormat::P010,VideoFrameEncoding::V210,ColorSpace::REC_709,17};
                switch(fault) {
                case 0: invalid.data=nullptr;break;
                case 1: invalid.width=23;break;
                case 2: invalid.height=7;break;
                case 3: invalid.dataBytes=8;break;
                case 4: invalid.chromaRowBytes=2;break;
                }
                ValidatedAnalysisLumaSampler rejected(invalid);
                Assert::IsFalse(rejected.IsValid());Assert::IsFalse(rejected.Sample(0,0,actual));
            }
        }
		TEST_METHOD(BgraBarsProduceTrustedSourceCoordinateEvidence)
		{
			BgraFrame frame(320, 180);
			frame.BlackOutside(22, 158);
			const ActivePictureEvidence evidence =
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
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::TOP_BOTTOM),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(BgraStartupSubtitleUsesTheSameSymmetricHypothesis)
		{
			BgraFrame frame(320, 180);
			frame.BlackOutside(22, 158);
			frame.FillRectangle(110, 162, 210, 172, 235);
			const AnalysisLumaSource source = frame.Source();
			const auto observed = ExtractActivePictureEvidence(source);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::PROVISIONAL),
				static_cast<int>(observed.classification));
			const auto hypothesis = EvaluateSymmetricVerticalBarHypothesis(
				source, observed);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(hypothesis.classification));
			Assert::AreEqual(hypothesis.trustedBounds.top,
				180 - hypothesis.trustedBounds.bottom);
		}

		TEST_METHOD(R210BarsProduceTrustedSourceCoordinateEvidence)
		{
			R210Frame frame(320, 180);
			frame.BlackOutside(0, 22, 320, 158);
			const ActivePictureEvidence evidence =
				ExtractActivePictureEvidence(frame.Source());
			Assert::IsTrue(evidence.available);
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.top.trusted);
			Assert::IsTrue(evidence.bottom.trusted);
			Assert::IsTrue(evidence.trustedBounds.top >= 20);
			Assert::IsTrue(evidence.trustedBounds.bottom <= 160);
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::TOP_BOTTOM),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(R210ColoredDarkEdgesCannotAuthorizeCrop)
		{
			R210Frame frame(320, 180);
			frame.DarkBlueOutside(0, 22, 320, 158);
			const ActivePictureEvidence evidence =
				ExtractActivePictureEvidence(frame.Source());
			Assert::AreNotEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
		}

		TEST_METHOD(R210PillarboxUsesTheSameTrustedCoordinateContract)
		{
			R210Frame frame(320, 180);
			frame.BlackOutside(40, 0, 280, 180);
			const ActivePictureEvidence evidence =
				ExtractActivePictureEvidence(frame.Source());
			Assert::AreEqual(static_cast<int>(
				ActivePictureClassification::BAR_CROP_TRUSTED),
				static_cast<int>(evidence.classification));
			Assert::IsTrue(evidence.left.trusted);
			Assert::IsTrue(evidence.right.trusted);
			Assert::IsTrue(evidence.trustedBounds.left >= 38);
			Assert::IsTrue(evidence.trustedBounds.right <= 282);
			Assert::AreEqual(
				static_cast<int>(ActivePictureBounds::BarAxes::LEFT_RIGHT),
				static_cast<int>(evidence.trustedBounds.trustedBarAxes));
		}

		TEST_METHOD(R210DarkArtworkDoesNotBecomeTrustedBars)
		{
			R210Frame frame(320, 180);
			frame.Fill(80);
			const ActivePictureEvidence evidence =
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

		TEST_METHOD(PackedTenBitLimitedAnalysisMatchesRendererReferenceCodes)
		{
			auto bytes = [](uint32_t word, bool littleEndian)
			{
				std::array<uint8_t, 4> result{};
				for (size_t index = 0; index < result.size(); ++index)
				{
					const size_t shiftIndex = littleEndian ? index : 3 - index;
					result[index] = static_cast<uint8_t>(word >> (shiftIndex * 8));
				}
				return result;
			};

			const uint32_t r10Word = (940U << 22) | (64U << 12) | (64U << 2);
			const uint32_t r210Word = (960U << 20) | (64U << 10) | 64U;
			const auto r10b = bytes(r10Word, false);
			const auto r10l = bytes(r10Word, true);
			const auto r210 = bytes(r210Word, false);
			const struct Case
			{
				VideoFrameEncoding encoding;
				const std::array<uint8_t, 4>* data;
			} cases[] = {
				{ VideoFrameEncoding::R10b, &r10b },
				{ VideoFrameEncoding::R10l, &r10l },
				{ VideoFrameEncoding::R210, &r210 },
			};

			for (const Case& test : cases)
			{
				const AnalysisLumaSource source = {
					test.data->data(), test.data->size(), 1, 1, 4, 0,
					AnalysisLumaFormat::NativeRgb, test.encoding,
					ColorSpace::REC_709, 1
				};
				AnalysisLumaSample sample;
				Assert::IsTrue(source.Sample(0, 0, sample));
				Assert::AreEqual(250, static_cast<int>(sample.luma));
				Assert::AreEqual(409, static_cast<int>(sample.chromaU));
				Assert::AreEqual(960, static_cast<int>(sample.chromaV));
			}
		}

		TEST_METHOD(PackedTwelveBitLayoutsDecodeSmpteComponentPacking)
		{
			const uint8_t r12b[] = {
				0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
				0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
				0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
				0x20, 0x21, 0x22, 0x23
			};
			const uint8_t r12l[] = {
				0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
				0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
				0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
				0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
			};
			const AnalysisLumaSource big = { r12b, sizeof(r12b), 8, 1, 36, 0,
				AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::R12B,
				ColorSpace::REC_709, 1 };
			const AnalysisLumaSource little = { r12l, sizeof(r12l), 8, 1, 36, 0,
				AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::R12L,
				ColorSpace::REC_709, 1 };
			AnalysisLumaSample sample;
			Assert::IsTrue(big.Sample(0, 0, sample));
			Assert::AreEqual(63, static_cast<int>(sample.luma));
			Assert::IsTrue(little.Sample(7, 0, sample));
			Assert::AreEqual(1023, static_cast<int>(sample.luma));
			Assert::AreEqual(512, static_cast<int>(sample.chromaU));
			Assert::AreEqual(512, static_cast<int>(sample.chromaV));
		}

		TEST_METHOD(PackedTwelveBitAnalysisUsesNormalizedFullRangeRounding)
		{
			std::array<uint8_t, 36> row{};
			for (size_t pair = 0; pair < 4; ++pair)
			{
				uint8_t* destination = row.data() + pair * 9;
				destination[0] = 2;
				destination[1] = 2 << 4;
				destination[3] = 2;
				destination[4] = 2 << 4;
				destination[6] = 2;
				destination[7] = 2 << 4;
			}
			const AnalysisLumaSource source = {
				row.data(), row.size(), 8, 1, row.size(), 0,
				AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::R12L,
				ColorSpace::REC_709, 1
			};
			AnalysisLumaSample sample;
			Assert::IsTrue(source.Sample(0, 0, sample));
			Assert::AreEqual(0, static_cast<int>(sample.luma));
			Assert::AreEqual(512, static_cast<int>(sample.chromaU));
			Assert::AreEqual(512, static_cast<int>(sample.chromaV));
		}

		TEST_METHOD(P210SamplingPreservesEachChromaRow)
		{
			// Two luma rows, followed by two chroma rows. P010 would correctly
			// read the first chroma row for both lines; P210 must not.
			const uint16_t plane[] = {
				0, 0, 0, 0,
				100 << 6, 200 << 6,
				300 << 6, 400 << 6
			};
			const AnalysisLumaSource source = {
				reinterpret_cast<const uint8_t*>(plane), sizeof(plane), 2, 2,
				4, 4, AnalysisLumaFormat::P210, VideoFrameEncoding::V210,
				ColorSpace::REC_709, 1 };
			AnalysisLumaSample sample;
			Assert::IsTrue(source.Sample(1, 1, sample));
			Assert::AreEqual(300, static_cast<int>(sample.chromaU));
			Assert::AreEqual(400, static_cast<int>(sample.chromaV));
		}

		TEST_METHOD(UyvySparseSamplingPreservesStudioCodesAndSharedChroma)
		{
			const uint8_t row[] = { 64, 16, 192, 235 };
			const AnalysisLumaSource source = {
				row, sizeof(row), 2, 1, 4, 0,
				AnalysisLumaFormat::NativeYuv422,
				VideoFrameEncoding::UYVY, ColorSpace::REC_709, 1 };
			AnalysisLumaSample first;
			AnalysisLumaSample second;
			Assert::IsTrue(source.Sample(0, 0, first));
			Assert::IsTrue(source.Sample(1, 0, second));
			Assert::AreEqual(64, static_cast<int>(first.luma));
			Assert::AreEqual(940, static_cast<int>(second.luma));
			Assert::AreEqual(256, static_cast<int>(first.chromaU));
			Assert::AreEqual(768, static_cast<int>(first.chromaV));
			Assert::AreEqual(static_cast<int>(first.chromaU),
				static_cast<int>(second.chromaU));
			Assert::AreEqual(static_cast<int>(first.chromaV),
				static_cast<int>(second.chromaV));
		}

		TEST_METHOD(V210SparseSamplingDecodesAllSixPixels)
		{
			auto word = [](uint16_t first, uint16_t second, uint16_t third)
			{
				return static_cast<uint32_t>(first) |
					(static_cast<uint32_t>(second) << 10) |
					(static_cast<uint32_t>(third) << 20);
			};
			const uint32_t row[] = {
				word(100, 10, 200), word(20, 300, 30),
				word(400, 40, 500), word(50, 600, 60)
			};
			const AnalysisLumaSource source = {
				reinterpret_cast<const uint8_t*>(row), sizeof(row), 6, 1,
				sizeof(row), 0, AnalysisLumaFormat::NativeYuv422,
				VideoFrameEncoding::V210, ColorSpace::REC_709, 1 };
			const int expectedLuma[] = { 10, 20, 30, 40, 50, 60 };
			const int expectedU[] = { 100, 100, 300, 300, 500, 500 };
			const int expectedV[] = { 200, 200, 400, 400, 600, 600 };
			for (int x = 0; x < 6; ++x)
			{
				AnalysisLumaSample sample;
				Assert::IsTrue(source.Sample(x, 0, sample));
				Assert::AreEqual(expectedLuma[x], static_cast<int>(sample.luma));
				Assert::AreEqual(expectedU[x], static_cast<int>(sample.chromaU));
				Assert::AreEqual(expectedV[x], static_cast<int>(sample.chromaV));
			}
		}

        TEST_METHOD(CurrentV210GroupReaderMatchesCanonicalAcrossGroupsRowsAndPartialTails)
        {
            for (int width : {2, 4, 6, 8, 10, 14, 32})
            for (auto range : {AnalysisLumaRange::Limited, AnalysisLumaRange::Full}) {
                const int height=5;
                const size_t pitch=((size_t(width)+5)/6)*16+7;
                std::vector<uint8_t> bytes(1+pitch*height,0xA5);
                uint32_t state=0x93f1a27u;
                for(int y=0;y<height;++y)for(size_t group=0;group<(size_t(width)+5)/6;++group)
                    for(int w=0;w<4;++w) {
                        state=state*1664525u+1013904223u;
                        const uint32_t value=state|0xc0000000u; // Unused high bits must be ignored.
                        std::memcpy(bytes.data()+1+size_t(y)*pitch+group*16+w*4,&value,4);
                    }
                AnalysisLumaSource source{bytes.data()+1,bytes.size()-1,width,height,pitch,0,
                    AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
                source.range=range;
                subtitle_detail::CurrentV210GroupReader reader(source);
                Assert::IsTrue(reader.IsValid());
                const auto check=[&](int x,int y) {
                    AnalysisLumaSample expected,actual;
                    const bool valid=source.Sample(x,y,expected);
                    Assert::AreEqual(valid,reader.Sample(x,y,actual));
                    if(valid) {
                        Assert::AreEqual(int(expected.luma),int(actual.luma));
                        Assert::AreEqual(int(expected.chromaU),int(actual.chromaU));
                        Assert::AreEqual(int(expected.chromaV),int(actual.chromaV));
                    }
                };
                for(int y=-1;y<=height;++y)for(int x=-1;x<=width;++x)check(x,y);
                for(int i=0;i<1024;++i) {
                    state=state*1664525u+1013904223u;
                    const int x=int(state%unsigned(width));
                    state=state*1664525u+1013904223u;
                    const int y=int(state%unsigned(height));
                    check(x,y);check(x,y); // Both miss and repeated-group hit.
                }
            }
            const uint32_t words[]={0xc0000000u|(64u<<10)|(1023u<<20),
                940u|(512u<<10)|(1u<<20),1023u|(2u<<10)|(3u<<20),4u|(5u<<10)|(6u<<20)};
            AnalysisLumaSource golden{reinterpret_cast<const uint8_t*>(words),sizeof(words),6,1,16,0,
                AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::BT_2020,1};
            subtitle_detail::CurrentV210GroupReader reader(golden);
            const int ys[]={64,940,1,2,4,6},us[]={0,0,512,512,3,3},vs[]={1023,1023,1023,1023,5,5};
            for(int x=0;x<6;++x) {
                AnalysisLumaSample sample;Assert::IsTrue(reader.Sample(x,0,sample));
                Assert::AreEqual(ys[x],int(sample.luma));Assert::AreEqual(us[x],int(sample.chromaU));
                Assert::AreEqual(vs[x],int(sample.chromaV));
            }
        }

        TEST_METHOD(CurrentV210GroupReaderOwnsOneImmutableOperationAndRejectsInvalidViews)
        {
            auto owner=std::make_shared<std::vector<uint8_t>>(32,0);
            (*owner)[2]=1; // First Y=64.
            AnalysisLumaSource source{owner->data(),owner->size(),6,2,16,0,
                AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,17};
            source.backingOwner=owner;
            std::weak_ptr<std::vector<uint8_t>> lifetime=owner;
            {
                subtitle_detail::CurrentV210GroupReader reader(source);
                source={};owner.reset();Assert::IsFalse(lifetime.expired());
                AnalysisLumaSample sample;Assert::IsTrue(reader.Sample(0,0,sample));
                Assert::AreEqual(64,int(sample.luma));
                Assert::IsTrue(reader.Sample(0,1,sample));Assert::AreEqual(0,int(sample.luma));
                Assert::IsTrue(reader.Sample(0,0,sample));Assert::AreEqual(64,int(sample.luma));
                Assert::IsFalse(reader.Sample(6,0,sample));Assert::IsFalse(reader.Sample(0,2,sample));
            }
            Assert::IsTrue(lifetime.expired(),L"No source owner survives the verification operation.");
            std::vector<uint8_t> next(128,0);next[2]=2;
            AnalysisLumaSource valid{next.data(),next.size(),6,2,16,0,
                AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,18};
            subtitle_detail::CurrentV210GroupReader fresh(valid);
            AnalysisLumaSample sample;Assert::IsTrue(fresh.Sample(0,0,sample));
            Assert::AreEqual(128,int(sample.luma),L"A new source operation must decode its own bytes.");
            for(int fault=0;fault<10;++fault) {
                auto bad=valid;
                if(fault==0)bad.data=nullptr;
                if(fault==1)bad.dataBytes=31;
                if(fault==2)bad.rowBytes=15;
                if(fault==3)bad.width=5;
                if(fault==4)bad.encoding=VideoFrameEncoding::UYVY;
                if(fault==5)bad.format=AnalysisLumaFormat::P010;
                if(fault==6)bad.format=AnalysisLumaFormat::P210;
                if(fault==7)bad.format=AnalysisLumaFormat::DecodedDolbyVision422;
                if(fault==8)bad.format=AnalysisLumaFormat::NativeRgb;
                if(fault==9)bad.height=0;
                subtitle_detail::CurrentV210GroupReader invalid(bad);
                Assert::IsFalse(invalid.IsValid());Assert::IsFalse(invalid.Sample(0,0,sample));
            }
        }

	};
}
