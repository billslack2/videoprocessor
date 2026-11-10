#include "pch.h"
#include "CppUnitTest.h"

#include <AnalysisFrame.h>
#include <ActivePictureEvidence.h>
#include <SubtitleBoxDetector.h>
#include <ActivePictureDecisionTimeline.h>
#include <VideoFrame.h>
#include <VideoState.h>
#include <video_frame_formatter/CRGBDolbyVisionTunnelFormatter.h>

#include <cstdint>
#include <fstream>
#include <cstdlib>
#include <string>
#include <memory>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	namespace
	{
		using ByteBuffer = std::shared_ptr<std::vector<uint8_t>>;

		void Write16Le(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
		{
			bytes[offset] = static_cast<uint8_t>(value);
			bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
		}

		void Write32Be(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
		{
			bytes[offset] = static_cast<uint8_t>(value >> 24);
			bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
			bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
			bytes[offset + 3] = static_cast<uint8_t>(value);
		}

		ByteBuffer MakeDecoded12Bit422(int width, int height)
		{
			const size_t pixelCount = static_cast<size_t>(width) * height;
			auto bytes = std::make_shared<std::vector<uint8_t>>(pixelCount * 4, 0);
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					const uint16_t luma = y == 0 ? 3 : 0xABC;
					Write16Le(*bytes,
						(static_cast<size_t>(y) * width + x) * 2,
						static_cast<uint16_t>(luma << 4));

					// P216-style 4:2:2: one 12-bit component is stored at
					// every pixel position, alternating U and V horizontally.
					const uint16_t chroma = y == 0 ?
						((x & 1) ? 4095 : 3) :
						((x & 1) ? 0x123 : 0xABC);
					Write16Le(*bytes,
						(pixelCount + static_cast<size_t>(y) * width + x) * 2,
						static_cast<uint16_t>(chroma << 4));
				}
			}
			return bytes;
		}

		ByteBuffer MakeR210Rgb8(int width, int height, uint8_t alignment,
			uint8_t gray = 128)
		{
			auto bytes = std::make_shared<std::vector<uint8_t>>(
				static_cast<size_t>(width) * height * 4, 0);
			const uint32_t component = alignment == 1 ?
				static_cast<uint32_t>(gray) << 2 : gray;
			const uint32_t packed = (component << 20) |
				(component << 10) | component;
			for (size_t offset = 0; offset < bytes->size(); offset += 4)
				Write32Be(*bytes, offset, packed);
			return bytes;
		}

		AnalysisFrameInput MakeDvInput(const ByteBuffer& decoded,
			const ByteBuffer& tunnel, int width = 4, int height = 2,
			AnalysisSourceProfile profile = AnalysisSourceProfile::LiveRenderer)
		{
			AnalysisFrameInput input;
			input.nativeData = tunnel->data();
			input.nativeBytes = tunnel->size();
			input.nativeRowBytes = static_cast<size_t>(width) * 4;
			input.nativeOwner = tunnel;
			input.convertedData = decoded->data();
			input.convertedBytes = decoded->size();
			input.convertedOwner = decoded;
			input.convertedContract = {
				VideoFrameSampleRange::FULL, 12, 4 };
			input.dolby.valid = true;
			input.dolby.fullRange = true;
			input.dolby.rgbBitAlignment = 1;
			input.width = width;
			input.height = height;
			input.encoding = VideoFrameEncoding::R210;
			input.colorspace = ColorSpace::REC_709;
			input.sourceGeneration = 73;
			input.profile = profile;
			input.converted422 = true;
			input.dolbyVisionTunnel = true;
			return input;
		}

		AnalysisFrameInput MakeSdrFallbackInput(const ByteBuffer& rgb,
			int width, int height, uint8_t alignment,
			AnalysisSourceProfile profile = AnalysisSourceProfile::LiveRenderer)
		{
			AnalysisFrameInput input;
			input.nativeData = rgb->data();
			input.nativeBytes = rgb->size();
			input.nativeRowBytes = static_cast<size_t>(width) * 4;
			input.nativeOwner = rgb;
			input.dolby.allowSdrFallback = true;
			input.dolby.rgbBitAlignment = alignment;
			input.width = width;
			input.height = height;
			input.encoding = VideoFrameEncoding::R210;
			input.colorspace = ColorSpace::REC_709;
			input.sourceGeneration = 73;
			input.profile = profile;
			input.rendererUsesNative = true;
			input.dolbyVisionTunnel = true;
			return input;
		}

		AnalysisLumaSample SampleAt(const PreparedAnalysisFrame& frame, int x, int y)
		{
			AnalysisLumaSample sample;
			Assert::IsTrue(frame.source.Sample(x, y, sample));
			return sample;
		}
	}

	TEST_CLASS(AnalysisRecoveryDvTests)
	{
	public:


        TEST_METHOD(TunnelSdrFallbackPreservesLimitedAndFullBlackWhiteAndDomain)
        {
            for (const auto encoding : { VideoFrameEncoding::R210, VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
            for (const auto profile : { AnalysisSourceProfile::LiveRenderer, AnalysisSourceProfile::QueuedCapture })
            {
                auto pixels = MakeR210Rgb8(4, 2, 2, 16);
                if (encoding != VideoFrameEncoding::R210)
                    std::fill(pixels->begin(), pixels->end(), 16);
                auto input = MakeSdrFallbackInput(pixels, 4, 2, 2, profile);
                input.encoding = encoding; input.tunnelSdrRgbRange = TunnelSdrRgbRange::Limited;
                auto limited = PrepareAnalysisFrame(input);
                Assert::IsTrue(limited.IsValid());
                Assert::IsTrue(limited.sampleRange == VideoFrameSampleRange::LIMITED);
                Assert::AreEqual(64, int(SampleAt(limited, 0, 0).luma));
                if (encoding == VideoFrameEncoding::R210)
                    pixels = MakeR210Rgb8(4, 2, 2, 235);
                else
                    std::fill(pixels->begin(), pixels->end(), 235);
                input.nativeData = pixels->data(); input.nativeOwner = pixels;
                limited = PrepareAnalysisFrame(input);
                Assert::AreEqual(940, int(SampleAt(limited, 0, 0).luma));
                input.tunnelSdrRgbRange = TunnelSdrRgbRange::Full;
                const auto full = PrepareAnalysisFrame(input);
                Assert::IsTrue(full.IsValid());
                Assert::AreNotEqual(static_cast<unsigned long long>(limited.domainKey),
                    static_cast<unsigned long long>(full.domainKey));
                input.tunnelSdrRgbRange = TunnelSdrRgbRange::Unknown;
                Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());
            }
        }

        TEST_METHOD(Rgb8TunnelUsesDecodedEvidenceAndExplicitSdrAcrossBothConsumers)
        {
            for (const auto encoding : { VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
            for (const auto profile : { AnalysisSourceProfile::LiveRenderer, AnalysisSourceProfile::QueuedCapture })
            {
                auto decoded = MakeDecoded12Bit422(4, 2);
                auto raw = std::make_shared<std::vector<uint8_t>>(32, 0);
                for (size_t i = 0; i < raw->size(); i += 4)
                {
                    if (encoding == VideoFrameEncoding::ARGB_8BIT)
                    { (*raw)[i] = 99; (*raw)[i+1] = 255; (*raw)[i+2] = 128; (*raw)[i+3] = 64; }
                    else
                    { (*raw)[i] = 64; (*raw)[i+1] = 128; (*raw)[i+2] = 255; (*raw)[i+3] = 99; }
                }
                auto input = MakeDvInput(decoded, raw, 4, 2, profile);
                input.encoding = encoding; input.dolby.rgbBitAlignment = 2;
                const auto dv = PrepareAnalysisFrame(input);
                Assert::IsTrue(dv.IsValid() && dv.usesConverted);
                Assert::IsTrue(dv.source.data == decoded->data());
                Assert::AreEqual(687, int(SampleAt(dv, 0, 1).luma));
                input.dolby.requiresBlackFrame = true;
                Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());
                input.dolby.requiresBlackFrame = false; input.dolby.valid = false;
                Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());
                input.dolby.allowSdrFallback = true;
                const auto sdr = PrepareAnalysisFrame(input);
                Assert::IsTrue(sdr.IsValid() && !sdr.usesConverted);
                Assert::IsTrue(sdr.source.data == raw->data());
                Assert::AreNotEqual(static_cast<unsigned long long>(dv.domainKey),
                    static_cast<unsigned long long>(sdr.domainKey));
                input.dolbyVisionTunnel = false;
                const auto native = PrepareAnalysisFrame(input);
                Assert::IsTrue(native.IsValid());
                const auto a = SampleAt(sdr, 0, 0), b = SampleAt(native, 0, 0);
                Assert::AreEqual(int(a.luma), int(b.luma));
                Assert::AreEqual(int(a.chromaU), int(b.chromaU));
                Assert::AreEqual(int(a.chromaV), int(b.chromaV));
                input.dolbyVisionTunnel = true; input.dolby.rgbBitAlignment = 1;
                Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());
                input.dolby.rgbBitAlignment = 2; input.nativeRowBytes = 15;
                Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());
            }
        }

        TEST_METHOD(DecodedPictureAndCaptionEvidenceAgreeAcrossRgbTunnelLayouts)
        {
            constexpr int width = 640, height = 360, top = 45, bottom = 315;
            const size_t pixels = size_t(width) * height;
            for (bool subtitle : { false, true })
            {
                auto decoded = std::make_shared<std::vector<uint8_t>>(pixels * 4, 0);
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        const int level = y >= top && y < bottom ? 200 : 64;
                        Write16Le(*decoded, (size_t(y) * width + x) * 2, uint16_t(level << 6));
                        Write16Le(*decoded, (pixels + size_t(y) * width + x) * 2, uint16_t(512 << 6));
                    }
                if (subtitle)
                    for (int n = 0; n < 15; ++n)
                    {
                        const int left = 200 + n * 13;
                        for (int y = 326; y < 340; ++y)
                            for (int x = left; x < left + 8; ++x)
                            {
                                const bool ink = x < left + 2 || x >= left + 6 || y < 328 || y >= 338;
                                Write16Le(*decoded, (size_t(y) * width + x) * 2,
                                    uint16_t((ink ? 510 : 64) << 6));
                            }
                    }
                auto raw = std::make_shared<std::vector<uint8_t>>(pixels * 4, 0);
                ActivePictureEvidence reference;
                SubtitleBoxResult referenceSubtitle;
                bool first = true;
                for (const auto encoding : { VideoFrameEncoding::R210, VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
                for (const auto profile : { AnalysisSourceProfile::LiveRenderer, AnalysisSourceProfile::QueuedCapture })
                {
                    auto input = MakeDvInput(decoded, raw, width, height, profile);
                    input.encoding = encoding;
                    input.dolby.rgbBitAlignment = encoding == VideoFrameEncoding::R210 ? 1 : 2;
                    input.dolby.fullRange = false;
                    input.convertedContract.sampleRange = VideoFrameSampleRange::LIMITED;
                    auto frame = PrepareAnalysisFrame(input);
                    Assert::IsTrue(frame.IsValid() && frame.usesConverted);
                    const auto evidence = ExtractActivePictureEvidence(frame.source);
                    if (!subtitle) {
                        Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),
                            int(evidence.classification), L"Clean bars must be detected, not merely agree across formats.");
                        Assert::IsTrue(std::abs(evidence.trustedBounds.top - top) <= 4 &&
                            std::abs(evidence.trustedBounds.bottom - bottom) <= 4,
                            L"The detected picture boundary must match the fixture, not just another encoding.");
                    }
                    SubtitleBoxDetector detector;
                    SubtitleBoxResult cue;
                    for (int i = 0; i < 4; ++i)
                        cue = detector.Analyze(frame.source, top, bottom, i + 1, 1);
                    if (first) { reference = evidence; referenceSubtitle = cue; first = false; }
                    else {
                        Assert::AreEqual(int(reference.classification), int(evidence.classification));
                        Assert::AreEqual(reference.proposedBounds.top, evidence.proposedBounds.top);
                        Assert::AreEqual(reference.proposedBounds.bottom, evidence.proposedBounds.bottom);
                        Assert::AreEqual(reference.top.rgbBoundaryContrast, evidence.top.rgbBoundaryContrast, 0.001);
                        Assert::AreEqual(reference.bottom.rgbBoundaryContrast, evidence.bottom.rgbBoundaryContrast, 0.001);
                        Assert::AreEqual(referenceSubtitle.detected, cue.detected);
                        Assert::AreEqual(referenceSubtitle.bounds.left, cue.bounds.left);
                        Assert::AreEqual(referenceSubtitle.bounds.bottom, cue.bounds.bottom);
                    }
                    Assert::AreEqual(subtitle, cue.detected, L"The reference cue must exercise real caption acquisition.");
                }
            }
        }

        TEST_METHOD(SdrTunnelRangeAndSubtitleEvidenceAgreeAcrossRgbLayouts)
        {
            constexpr int width = 640, height = 360, top = 45, bottom = 315;
            for (const auto range : { TunnelSdrRgbRange::Full, TunnelSdrRgbRange::Limited })
            for (bool subtitle : { false, true })
            {
                ActivePictureEvidence reference;
                SubtitleBoxResult referenceCue;
                bool first = true;
                for (const auto encoding : { VideoFrameEncoding::R210, VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
                {
                    auto raw = std::make_shared<std::vector<uint8_t>>(size_t(width) * height * 4, 0);
                    const uint8_t black = range == TunnelSdrRgbRange::Limited ? 16 : 0;
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x)
                        {
                            uint8_t value = y >= top && y < bottom ? uint8_t(black + 35) : black;
                            if (subtitle && y >= 326 && y < 340 && x >= 200 && x < 395)
                            {
                                const int offset = (x - 200) % 13;
                                const bool ink = offset < 8 && (offset < 2 || offset >= 6 || y < 328 || y >= 338);
                                if (ink) value = range == TunnelSdrRgbRange::Limited ? 200 : 220;
                            }
                            const size_t off = (size_t(y) * width + x) * 4;
                            if (encoding == VideoFrameEncoding::R210)
                                Write32Be(*raw, off, (uint32_t(value) << 20) | (uint32_t(value) << 10) | value);
                            else if (encoding == VideoFrameEncoding::ARGB_8BIT)
                                { (*raw)[off] = 255; (*raw)[off+1] = (*raw)[off+2] = (*raw)[off+3] = value; }
                            else
                                { (*raw)[off] = (*raw)[off+1] = (*raw)[off+2] = value; (*raw)[off+3] = 255; }
                        }
                    auto input = MakeSdrFallbackInput(raw, width, height, 2);
                    input.encoding = encoding; input.tunnelSdrRgbRange = range;
                    const auto frame = PrepareAnalysisFrame(input);
                    Assert::IsTrue(frame.IsValid() && !frame.usesConverted);
                    Assert::AreEqual(range == TunnelSdrRgbRange::Limited ? 64 : 0,
                        int(SampleAt(frame, 0, 0).luma));
                    const auto evidence = ExtractActivePictureEvidence(frame.source);
                    if (!subtitle) {
                        Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),
                            int(evidence.classification), L"Clean bars must be detected, not merely agree across formats.");
                        Assert::IsTrue(std::abs(evidence.trustedBounds.top - top) <= 4 &&
                            std::abs(evidence.trustedBounds.bottom - bottom) <= 4,
                            L"The detected picture boundary must match the fixture, not just another encoding.");
                    }
                    SubtitleBoxDetector detector;
                    SubtitleBoxResult cue;
                    for (int i = 0; i < 4; ++i)
                        cue = detector.Analyze(frame.source, top, bottom, i + 1, 1);
                    if (first) { reference = evidence; referenceCue = cue; first = false; }
                    else {
                        Assert::AreEqual(int(reference.classification), int(evidence.classification));
                        Assert::AreEqual(reference.proposedBounds.top, evidence.proposedBounds.top);
                        Assert::AreEqual(reference.proposedBounds.bottom, evidence.proposedBounds.bottom);
                        Assert::AreEqual(reference.top.rgbBoundaryContrast, evidence.top.rgbBoundaryContrast, 0.001);
                        Assert::AreEqual(reference.bottom.rgbBoundaryContrast, evidence.bottom.rgbBoundaryContrast, 0.001);
                        Assert::AreEqual(referenceCue.detected, cue.detected);
                        Assert::AreEqual(referenceCue.bounds.left, cue.bounds.left);
                        Assert::AreEqual(referenceCue.bounds.bottom, cue.bounds.bottom);
                    }
                    Assert::AreEqual(subtitle, cue.detected, L"The fixture must exercise real subtitle acquisition.");
                }
            }
        }

        TEST_METHOD(OptionalCapturedDecodedPictureHasRgbTunnelAnalysisParity)
        {
            char* path = nullptr; size_t size = 0;
            _dupenv_s(&path, &size, "VP_RGB8_DV_REPLAY_FRAME");
            const std::string filename = path ? path : "";
            std::free(path);
            if (filename.empty()) {
                Logger::WriteMessage(L"Set VP_RGB8_DV_REPLAY_FRAME to run the captured decoded frame parity check.");
                return;
            }
            std::ifstream meta(filename + ".txt");
            if (!meta) {
                meta.clear();
                const auto dot = filename.find_last_of('.');
                if (dot != std::string::npos) meta.open(filename.substr(0, dot) + ".txt");
            }
            int width = 0, height = 0, format = 0, oldEncoding = 0, color = 0;
            int top = 0, bottom = 0, script = 0; size_t row = 0, chromaRow = 0;
            meta >> width >> height >> row >> chromaRow >> format >> oldEncoding >> color >> top >> bottom >> script;
            Assert::IsTrue(bool(meta) && format == int(AnalysisLumaFormat::DecodedDolbyVision422));
            std::ifstream inputFile(filename, std::ios::binary | std::ios::ate);
            Assert::IsTrue(bool(inputFile));
            const auto length = inputFile.tellg();
            Assert::IsTrue(length > 0);
            auto decoded = std::make_shared<std::vector<uint8_t>>(size_t(length));
            inputFile.seekg(0); inputFile.read(reinterpret_cast<char*>(decoded->data()), length);
            Assert::IsTrue(bool(inputFile));
            auto raw = std::make_shared<std::vector<uint8_t>>(size_t(width) * height * 4, 0);
            for (bool full : { false, true })
            {
                ActivePictureEvidence reference;
                SubtitleBoxResult referenceCue;
                bool first = true;
                for (const auto encoding : { VideoFrameEncoding::R210, VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
                for (const auto profile : { AnalysisSourceProfile::LiveRenderer, AnalysisSourceProfile::QueuedCapture })
                {
                    auto input = MakeDvInput(decoded, raw, width, height, profile);
                    input.encoding = encoding; input.colorspace = static_cast<ColorSpace>(color);
                    input.convertedRowBytes = row; input.convertedChromaRowBytes = chromaRow;
                    input.dolby.rgbBitAlignment = encoding == VideoFrameEncoding::R210 ? 1 : 2;
                    input.dolby.fullRange = full;
                    input.convertedContract.sampleRange = full ? VideoFrameSampleRange::FULL : VideoFrameSampleRange::LIMITED;
                    const auto frame = PrepareAnalysisFrame(input);
                    Assert::IsTrue(frame.IsValid());
                    const auto evidence = ExtractActivePictureEvidence(frame.source);
                    SubtitleBoxDetector detector;
                    detector.SetScriptHint(static_cast<SubtitleScriptHint>(script));
                    SubtitleBoxResult cue;
                    for (int i = 0; i < 4; ++i) cue = detector.Analyze(frame.source, top, bottom, i + 1, 1);
                    if (first) { reference = evidence; referenceCue = cue; first = false; }
                    else {
                        Assert::AreEqual(int(reference.classification), int(evidence.classification));
                        Assert::AreEqual(reference.proposedBounds.top, evidence.proposedBounds.top);
                        Assert::AreEqual(reference.proposedBounds.bottom, evidence.proposedBounds.bottom);
                        Assert::AreEqual(reference.top.rgbBoundaryContrast, evidence.top.rgbBoundaryContrast, 0.001);
                        Assert::AreEqual(reference.bottom.rgbBoundaryContrast, evidence.bottom.rgbBoundaryContrast, 0.001);
                        Assert::AreEqual(referenceCue.detected, cue.detected);
                        Assert::AreEqual(referenceCue.bounds.left, cue.bounds.left);
                        Assert::AreEqual(referenceCue.bounds.bottom, cue.bounds.bottom);
                    }
                }
                Logger::WriteMessage(referenceCue.detected ? L"Captured decoded frame: subtitle acquired." :
                    L"Captured decoded frame: subtitle not acquired; parity only, no detection claim.");
            }
        }

		TEST_METHOD(DecodedDolbyVisionKeepsTwelveBitStorageAndLegacyUpperTenEvidence)
		{
			auto decoded = MakeDecoded12Bit422(4, 2);
			auto tunnel = MakeR210Rgb8(4, 2, 1, 240);
			auto input = MakeDvInput(decoded, tunnel);
			const auto prepared = PrepareAnalysisFrame(input);

			Assert::IsTrue(prepared.IsValid());
			Assert::AreEqual(static_cast<int>(AnalysisFrameDomain::DecodedDolbyVision422),
				static_cast<int>(prepared.domain));
			Assert::AreEqual(static_cast<int>(AnalysisLumaFormat::DecodedDolbyVision422),
				static_cast<int>(prepared.source.format));
			Assert::IsTrue(prepared.usesConverted);
			Assert::AreEqual(12, static_cast<int>(prepared.colorDepth));
			Assert::AreEqual(4, static_cast<int>(prepared.bitShift));
			Assert::AreEqual(static_cast<int>(VideoFrameSampleRange::FULL),
				static_cast<int>(prepared.sampleRange));
			Assert::IsTrue(prepared.source.data == decoded->data());
			Assert::IsFalse(prepared.source.data == tunnel->data());
			Assert::IsTrue(prepared.source.backingOwner != nullptr);

			// Codes are stored as code << 4. The beta detector reads the
			// upper ten bits directly; it does not rescale or round 12-bit data.
			const auto firstRow = SampleAt(prepared, 0, 0);
			Assert::AreEqual(0, static_cast<int>(firstRow.luma)); // 3 >> 2
			Assert::AreEqual(0, static_cast<int>(firstRow.chromaU)); // 3 >> 2
			Assert::AreEqual(1023, static_cast<int>(firstRow.chromaV));
			const auto secondRow = SampleAt(prepared, 0, 1);
			Assert::AreEqual(687, static_cast<int>(secondRow.luma)); // 0xABC >> 2
			Assert::AreEqual(687, static_cast<int>(secondRow.chromaU));
			Assert::AreEqual(72, static_cast<int>(secondRow.chromaV)); // 0x123 >> 2

			// The formatter contract and the parsed DM full-range flag must
			// agree. When both consistently select limited range, preserve it.
			auto limitedInput = MakeDvInput(decoded, tunnel);
			limitedInput.dolby.fullRange = false;
			limitedInput.convertedContract.sampleRange =
				VideoFrameSampleRange::LIMITED;
			const auto limited = PrepareAnalysisFrame(limitedInput);
			Assert::IsTrue(limited.IsValid());
			Assert::IsFalse(limited.dolbyFullRange);
			Assert::AreEqual(static_cast<int>(VideoFrameSampleRange::LIMITED),
				static_cast<int>(limited.sampleRange));
			Assert::AreNotEqual(
				static_cast<unsigned long long>(prepared.domainKey),
				static_cast<unsigned long long>(limited.domainKey));
		}

		TEST_METHOD(DolbyAnalysisUsesDecoded422AndNeverTheRawTunnelBytes)
		{
			auto decoded = MakeDecoded12Bit422(4, 2);
			auto tunnel = MakeR210Rgb8(4, 2, 1, 240);
			for (const auto profile : { AnalysisSourceProfile::LiveRenderer,
				AnalysisSourceProfile::QueuedCapture })
			{
				auto input = MakeDvInput(decoded, tunnel, 4, 2, profile);
				const auto prepared = PrepareAnalysisFrame(input);
				Assert::IsTrue(prepared.IsValid());
				Assert::IsTrue(prepared.usesConverted);
				Assert::AreEqual(static_cast<int>(AnalysisFrameDomain::DecodedDolbyVision422),
					static_cast<int>(prepared.domain));
				const auto sample = SampleAt(prepared, 0, 0);
				Assert::AreEqual(0, static_cast<int>(sample.luma));
				Assert::AreEqual(0, static_cast<int>(sample.chromaU));
				Assert::AreEqual(1023, static_cast<int>(sample.chromaV));
			}
		}

		TEST_METHOD(ExplicitSdrFallbackHonorsHighAndLowRgb8Alignment)
		{
			const auto highAligned = MakeR210Rgb8(2, 2, 1);
			const auto lowAligned = MakeR210Rgb8(2, 2, 2);
			auto highInput = MakeSdrFallbackInput(highAligned, 2, 2, 1);
			auto lowInput = MakeSdrFallbackInput(lowAligned, 2, 2, 2);
			const auto high = PrepareAnalysisFrame(highInput);
			const auto low = PrepareAnalysisFrame(lowInput);

			for (const auto* frame : { &high, &low })
			{
				Assert::IsTrue(frame->IsValid());
				Assert::AreEqual(static_cast<int>(AnalysisFrameDomain::SdrTunnelRgb),
					static_cast<int>(frame->domain));
				Assert::IsFalse(frame->usesConverted);
				Assert::AreEqual(8, static_cast<int>(frame->colorDepth));
				Assert::AreEqual(static_cast<int>(VideoFrameSampleRange::FULL),
					static_cast<int>(frame->sampleRange));
			}
			Assert::AreEqual(1, static_cast<int>(high.rgbBitAlignment));
			Assert::AreEqual(2, static_cast<int>(low.rgbBitAlignment));
			const auto highSample = SampleAt(high, 0, 0);
			const auto lowSample = SampleAt(low, 0, 0);
			Assert::AreEqual(514, static_cast<int>(highSample.luma));
			Assert::AreEqual(static_cast<int>(highSample.luma),
				static_cast<int>(lowSample.luma));
			Assert::AreEqual(static_cast<int>(highSample.chromaU),
				static_cast<int>(lowSample.chromaU));
			Assert::AreEqual(static_cast<int>(highSample.chromaV),
				static_cast<int>(lowSample.chromaV));
		}

		TEST_METHOD(FormatterMetadataAuthorizesOnlyRecognizedSdrAlignmentsBeforeDvLock)
		{
			const auto verify = [](uint8_t alignment, uint8_t gray,
				uint8_t expectedAlignment)
			{
				auto rgb = MakeR210Rgb8(128, 100, alignment, gray);
				VideoStateComPtr state = new VideoState();
				state->valid = true;
				state->displayMode = std::make_shared<DisplayMode>(
					128, 100, false, 24000, 1000);
				state->videoFrameEncoding = VideoFrameEncoding::R210;
				state->colorspace = ColorSpace::REC_709;
				state->dolbyVisionTunnel = true;

				CRGBDolbyVisionTunnelFormatter formatter;
				formatter.OnVideoState(state);
				VideoFrame frame(rgb->data(), 1, 0, nullptr);
				std::vector<BYTE> output(
					static_cast<size_t>(formatter.GetOutFrameSize()));
				Assert::IsTrue(formatter.FormatVideoFrame(frame, output.data()));

				const auto metadata = formatter.GetDolbyVisionTunnelMetadata();
				Assert::IsFalse(metadata.valid);
				Assert::IsTrue(metadata.allowSdrFallback);
				Assert::AreEqual(expectedAlignment,
					metadata.rgbBitAlignment);
			};

			verify(1, 128, 1); // high 8 bits of each R210 channel
			verify(2, 129, 2); // low 8 bits; nonzero low bits disambiguate it
		}

		TEST_METHOD(TransitionBlankExcludesCachedDmPixelsFromAllAnalysisProfiles)
		{
			auto decoded = MakeDecoded12Bit422(4, 2);
			auto tunnel = MakeR210Rgb8(4, 2, 1, 240);
			for (const auto profile : { AnalysisSourceProfile::LiveRenderer,
				AnalysisSourceProfile::QueuedCapture })
			{
				auto input = MakeDvInput(decoded, tunnel, 4, 2, profile);
				Assert::IsTrue(PrepareAnalysisFrame(input).IsValid());
				input.dolby.requiresBlackFrame = true;
				Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());
				input.dolby.requiresBlackFrame = false;
				Assert::IsTrue(PrepareAnalysisFrame(input).IsValid());
			}
		}

		TEST_METHOD(InvalidMetadataFormatStrideAndDepthCannotCreateDvEvidence)
		{
			auto decoded = MakeDecoded12Bit422(4, 2);
			auto tunnel = MakeR210Rgb8(4, 2, 1, 240);

			auto missingMetadata = MakeDvInput(decoded, tunnel);
			missingMetadata.dolby.valid = false;
			missingMetadata.dolby.allowSdrFallback = false;
			Assert::IsFalse(PrepareAnalysisFrame(missingMetadata).IsValid());

			auto wrongEncoding = MakeDvInput(decoded, tunnel);
			wrongEncoding.encoding = VideoFrameEncoding::UYVY;
			Assert::IsFalse(PrepareAnalysisFrame(wrongEncoding).IsValid());

			auto wrongChromaLayout = MakeDvInput(decoded, tunnel);
			wrongChromaLayout.converted422 = false;
			Assert::IsFalse(PrepareAnalysisFrame(wrongChromaLayout).IsValid());

			auto oddRaster = MakeDvInput(MakeDecoded12Bit422(3, 2),
				MakeR210Rgb8(3, 2, 1), 3, 2);
			Assert::IsFalse(PrepareAnalysisFrame(oddRaster).IsValid());

			auto shortPlane = MakeDvInput(decoded, tunnel);
			--shortPlane.convertedBytes;
			Assert::IsFalse(PrepareAnalysisFrame(shortPlane).IsValid());

			auto shortLumaStride = MakeDvInput(decoded, tunnel);
			shortLumaStride.convertedRowBytes = 7;
			Assert::IsFalse(PrepareAnalysisFrame(shortLumaStride).IsValid());

			auto shortChromaStride = MakeDvInput(decoded, tunnel);
			shortChromaStride.convertedChromaRowBytes = 7;
			Assert::IsFalse(PrepareAnalysisFrame(shortChromaStride).IsValid());

			auto wrongDepth = MakeDvInput(decoded, tunnel);
			wrongDepth.convertedContract = {
				VideoFrameSampleRange::FULL, 10, 6 };
			Assert::IsFalse(PrepareAnalysisFrame(wrongDepth).IsValid());

			auto inconsistentRange = MakeDvInput(decoded, tunnel);
			inconsistentRange.dolby.fullRange = false;
			Assert::IsFalse(PrepareAnalysisFrame(inconsistentRange).IsValid());

			auto unsupportedFallbackAlignment = MakeSdrFallbackInput(
				tunnel, 4, 2, 0);
			Assert::IsFalse(PrepareAnalysisFrame(
				unsupportedFallbackAlignment).IsValid());

			auto missingOwner = MakeDvInput(decoded, tunnel);
			missingOwner.convertedOwner.reset();
			Assert::IsFalse(PrepareAnalysisFrame(missingOwner).IsValid());

			auto badFallbackStride = MakeSdrFallbackInput(tunnel, 4, 2, 1);
			badFallbackStride.nativeRowBytes = 15;
			Assert::IsFalse(PrepareAnalysisFrame(badFallbackStride).IsValid());
		}

		TEST_METHOD(InvalidDvFrameBreaksPartialTransitionVotesWithoutChangingTransportSequence)
		{
			auto decoded = MakeDecoded12Bit422(4, 2);
			auto tunnel = MakeR210Rgb8(4, 2, 1, 240);
			auto invalidDv = MakeDvInput(decoded, tunnel);
			invalidDv.dolby.valid = false;
			invalidDv.dolby.allowSdrFallback = false;
			Assert::IsFalse(PrepareAnalysisFrame(invalidDv).IsValid());

			const uint64_t transportGeneration = 91;
			const uint64_t sourceFormatGeneration = 7;
			ActivePictureDecisionTimeline timeline;
			timeline.Reset(transportGeneration);
			const auto identity = [&](uint64_t sequence, uint64_t frameNumber)
			{
				ActivePictureFrameIdentity id;
				id.transportGeneration = transportGeneration;
				id.acceptedSequence = sequence;
				id.sourceFrameNumber = frameNumber;
				id.captureTimestamp = frameNumber * 1000;
				id.sourceFormatGeneration = sourceFormatGeneration;
				id.viewportGeneration = 3;
				id.rendererGeneration = 4;
				id.analysisDomainKey = 0xD0124;
				return id;
			};
			const auto observation = [](uint64_t frameNumber,
				const ActivePictureBounds& bounds)
			{
				ActivePictureObservation result;
				result.bounds = bounds;
				result.frameNumber = frameNumber;
				result.available = true;
				result.classification =
					bounds.top == 0 && bounds.bottom == bounds.rasterHeight
					? ActivePictureClassification::FULL_RASTER_TRUSTED
					: ActivePictureClassification::BAR_CROP_TRUSTED;
				return result;
			};
			const ActivePictureBounds scope{
				0, 276, 3840, 1884, 3840, 2160, 2.388,
				ActivePictureBounds::BarAxes::TOP_BOTTOM };
			const ActivePictureBounds full{
				0, 0, 3840, 2160, 3840, 2160, 16.0 / 9.0,
				ActivePictureBounds::BarAxes::NONE };
			ActivePictureFrameDecision published;
			for (uint64_t frame = 1;
				frame <= ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;
				++frame)
			{
				Assert::AreEqual(
					frame == ActivePictureTransitionModel::INITIAL_CONFIRMATIONS,
					timeline.SubmitScheduledObservation(
						identity(frame, frame), observation(frame, scope),
						0, 0, published));
			}

			Assert::IsFalse(timeline.SubmitScheduledObservation(
				identity(5, 5), observation(5, full), 0, 0, published));
			const auto gap = identity(6, 6);
			Assert::IsTrue(timeline.TrackAcceptedFrame(gap));
			timeline.MarkDiscarded(gap, gap.sourceFrameNumber);
			Assert::AreEqual(transportGeneration, timeline.TransportGeneration());
			Assert::AreEqual(gap.acceptedSequence, timeline.LastConsumedSequence());

			Assert::IsFalse(timeline.SubmitScheduledObservation(
				identity(7, 7), observation(7, full), 0, 0, published));
			Assert::IsTrue(timeline.SubmitScheduledObservation(
				identity(8, 8), observation(8, full), 0, 0, published));
		}
		TEST_METHOD(FormatTransitionsChangeDomainAndDoNotReusePriorEvidence)
		{
			auto decoded = MakeDecoded12Bit422(4, 2);
			auto tunnel = MakeR210Rgb8(4, 2, 1, 240);
			auto dvInput = MakeDvInput(decoded, tunnel);
			const auto dv = PrepareAnalysisFrame(dvInput);
			Assert::IsTrue(dv.IsValid());

			auto lostMetadata = MakeDvInput(decoded, tunnel);
			lostMetadata.dolby.valid = false;
			lostMetadata.dolby.rgbBitAlignment = 1;
			const auto unavailable = PrepareAnalysisFrame(lostMetadata);
			Assert::IsFalse(unavailable.IsValid());
			AnalysisLumaSample unavailableSample;
			Assert::IsFalse(unavailable.source.Sample(0, 0, unavailableSample));

			auto sdrPixels = MakeR210Rgb8(4, 2, 1, 128);
			auto sdrInput = MakeSdrFallbackInput(sdrPixels, 4, 2, 1);
			const auto sdr = PrepareAnalysisFrame(sdrInput);
			Assert::IsTrue(sdr.IsValid());
			Assert::AreEqual(static_cast<int>(AnalysisFrameDomain::SdrTunnelRgb),
				static_cast<int>(sdr.domain));
			Assert::AreNotEqual(
				static_cast<unsigned long long>(dv.domainKey),
				static_cast<unsigned long long>(sdr.domainKey));
			const auto sdrSample = SampleAt(sdr, 0, 0);
			Assert::AreEqual(514, static_cast<int>(sdrSample.luma));
			Assert::AreNotEqual(0, static_cast<int>(sdrSample.luma));
		}

		TEST_METHOD(SameSourceSharesCanonicalKeysAcrossProfilesAndOutputPaths)
		{
			auto decoded = MakeDecoded12Bit422(4, 2);
			auto tunnel = MakeR210Rgb8(4, 2, 1, 240);
			const auto live = PrepareAnalysisFrame(MakeDvInput(decoded, tunnel,
				4, 2, AnalysisSourceProfile::LiveRenderer));
			const auto queued = PrepareAnalysisFrame(MakeDvInput(decoded, tunnel,
				4, 2, AnalysisSourceProfile::QueuedCapture));

			Assert::IsTrue(live.IsValid() && queued.IsValid());
			Assert::AreEqual(static_cast<int>(AnalysisSourceProfile::LiveRenderer),
				static_cast<int>(live.profile));
			Assert::AreEqual(static_cast<int>(AnalysisSourceProfile::QueuedCapture),
				static_cast<int>(queued.profile));
			Assert::AreEqual(
				static_cast<unsigned long long>(live.domainKey),
				static_cast<unsigned long long>(queued.domainKey));

			// Geometry is part of the interpretation contract even when the
			// decoder, range and compatibility sampler are otherwise identical.
			auto widerDecoded = MakeDecoded12Bit422(6, 2);
			auto widerTunnel = MakeR210Rgb8(6, 2, 1, 240);
			const auto wider = PrepareAnalysisFrame(MakeDvInput(
				widerDecoded, widerTunnel, 6, 2));
			Assert::IsTrue(wider.IsValid());
			Assert::AreNotEqual(
				static_cast<unsigned long long>(live.domainKey),
				static_cast<unsigned long long>(wider.domainKey));

			// Renderer output selection cannot change analysis input when the
			// source encoding has a native adapter.
			AnalysisFrameInput nativeInput;
			nativeInput.nativeData = tunnel->data();
			nativeInput.nativeBytes = tunnel->size();
			nativeInput.nativeRowBytes = 16;
			nativeInput.nativeOwner = tunnel;
			nativeInput.width = 4;
			nativeInput.height = 2;
			nativeInput.encoding = VideoFrameEncoding::R210;
			nativeInput.colorspace = ColorSpace::REC_709;
			nativeInput.sourceGeneration = 73;
			nativeInput.profile = AnalysisSourceProfile::QueuedCapture;

			auto p210 = std::make_shared<std::vector<uint8_t>>(32, 0);
			AnalysisFrameInput convertedInput = nativeInput;
			convertedInput.convertedData = p210->data();
			convertedInput.convertedBytes = p210->size();
			convertedInput.convertedOwner = p210;
			convertedInput.convertedContract = {
				VideoFrameSampleRange::LIMITED, 10, 6 };
			convertedInput.profile = AnalysisSourceProfile::LiveRenderer;
			convertedInput.converted422 = true;

			const auto native = PrepareAnalysisFrame(nativeInput);
			const auto converted = PrepareAnalysisFrame(convertedInput);
			Assert::IsTrue(native.IsValid() && converted.IsValid());
			Assert::AreEqual(static_cast<int>(native.domain),
				static_cast<int>(converted.domain));
			Assert::AreEqual(
				static_cast<unsigned long long>(native.domainKey),
				static_cast<unsigned long long>(converted.domainKey));
			Assert::IsFalse(native.usesConverted);
			Assert::IsFalse(converted.usesConverted);
			Assert::IsTrue(native.source.data == tunnel->data());
			Assert::IsTrue(converted.source.data == tunnel->data());
		}

		TEST_METHOD(PreparedEvidenceRetainsItsDecodedBackingOwner)
		{
			auto decoded = MakeDecoded12Bit422(4, 2);
			auto tunnel = MakeR210Rgb8(4, 2, 1, 240);
			std::weak_ptr<std::vector<uint8_t>> weakDecoded = decoded;
			PreparedAnalysisFrame prepared;
			{
				auto input = MakeDvInput(decoded, tunnel);
				prepared = PrepareAnalysisFrame(input);
				input.convertedOwner.reset();
			}
			decoded.reset();
			Assert::IsFalse(weakDecoded.expired());
			Assert::IsTrue(prepared.IsValid());
			const auto sample = SampleAt(prepared, 0, 1);
			Assert::AreEqual(687, static_cast<int>(sample.luma));
		}
	};
}



