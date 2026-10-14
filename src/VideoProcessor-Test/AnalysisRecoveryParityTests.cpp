#include "pch.h"
#include "CppUnitTest.h"

#include <ActivePictureEvidence.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <AnalysisFrame.h>
#include <SubtitleBoxDetector.h>
#include <SubtitleBoxLookahead.h>
#include <SubtitleMeasurementWorker.h>
#include <VideoFrame.h>
#include <VideoState.h>
#include <video_frame_formatter/CUYVYtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP210VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP210VideoFrameFormatter.h>
#include <video_frame_formatter/CDeckLinkRGBToP010VideoFrameFormatter.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(AnalysisRecoveryParityTests)
    {
        static constexpr int Width = 640;
        static constexpr int Height = 360;

        enum class Scene
        {
            OneLineCue,
            TwoLineCue,
            ThreeLineCard,
            MidPictureText,
            MovingTexture,
            ChromaEdge,
            NearBarBackingCard,
        };

        struct Fixture
        {
            VideoStateComPtr state;
            std::shared_ptr<std::vector<uint8_t>> pixels;
            VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
            size_t rowBytes = 0;
        };

        struct Conversion
        {
            std::shared_ptr<std::vector<uint8_t>> pixels;
            VideoFrameFormatterOutputContract contract;
            bool is422 = false;
        };

        static void WriteR12LPixelPair(uint8_t* destination,
            uint16_t r0, uint16_t g0, uint16_t b0,
            uint16_t r1, uint16_t g1, uint16_t b1)
        {
            destination[0] = static_cast<uint8_t>(r0);
            destination[1] = static_cast<uint8_t>((r0 >> 8) | (g0 << 4));
            destination[2] = static_cast<uint8_t>(g0 >> 4);
            destination[3] = static_cast<uint8_t>(b0);
            destination[4] = static_cast<uint8_t>((b0 >> 8) | (r1 << 4));
            destination[5] = static_cast<uint8_t>(r1 >> 4);
            destination[6] = static_cast<uint8_t>(g1);
            destination[7] = static_cast<uint8_t>((g1 >> 8) | (b1 << 4));
            destination[8] = static_cast<uint8_t>(b1 >> 4);
        }

        static std::shared_ptr<std::vector<uint8_t>> MakeR12Pixels(
            VideoFrameEncoding encoding)
        {
            constexpr int width = 104;
            constexpr int height = 100;
            constexpr size_t rowBytes = static_cast<size_t>(width) * 36 / 8;
            auto pixels = std::make_shared<std::vector<uint8_t>>(rowBytes * height, 0);
            const std::array<std::array<uint16_t, 3>, 8> rgb = {{
                {{ 0, 0, 0 }}, {{ 4095, 4095, 4095 }},
                {{ 4095, 0, 0 }}, {{ 0, 2048, 4095 }},
                {{ 1024, 2048, 3072 }}, {{ 12, 23, 34 }},
                {{ 3210, 1234, 2100 }}, {{ 4095, 64, 4095 }}
            }};
            for (int y = 0; y < height; ++y)
            {
                uint8_t* row = pixels->data() + static_cast<size_t>(y) * rowBytes;
                for (int block = 0; block < width / 8; ++block)
                {
                    uint8_t logical[36] = {};
                    for (int pair = 0; pair < 4; ++pair)
                    {
                        const int firstIndex = (block * 8 + pair * 2) % 8;
                        const auto& first = rgb[firstIndex];
                        const auto& second = rgb[firstIndex + 1];
                        WriteR12LPixelPair(logical + pair * 9,
                            first[0], first[1], first[2],
                            second[0], second[1], second[2]);
                    }
                    uint8_t* blockRow = row + static_cast<size_t>(block) * 36;
                    if (encoding == VideoFrameEncoding::R12L)
                        std::copy(logical, logical + sizeof(logical), blockRow);
                    else
                        for (size_t byte = 0; byte < sizeof(logical); ++byte)
                            blockRow[(byte / 4) * 4 + 3 - (byte % 4)] = logical[byte];
                }
            }
            return pixels;
        }

        static bool IsEightBitYuv(VideoFrameEncoding encoding)
        {
            return encoding == VideoFrameEncoding::UYVY ||
                encoding == VideoFrameEncoding::HDYC;
        }

        static int BaseLuma(Scene scene, int x, int y, int phase)
        {
            if (y < 45 || y >= 315)
                return 64;
            if (scene == Scene::MovingTexture)
            {
                const int movingLeft = phase == 0 ? 84 : 164;
                if (x >= movingLeft && x < movingLeft + 128 && y >= 130 && y < 264)
                    return ((x / 16 + y / 12) & 1) ? 280 : 200;
            }
            return 200;
        }

        static int GlyphLuma(int x, int y, int left, int top, int count)
        {
            for (int glyph = 0; glyph < count; ++glyph)
            {
                const int glyphLeft = left + glyph * 13;
                if (x < glyphLeft - 1 || x >= glyphLeft + 9 ||
                    y < top - 1 || y >= top + 15)
                    continue;
                if (x >= glyphLeft && x < glyphLeft + 8 &&
                    y >= top && y < top + 14 &&
                    !(x >= glyphLeft + 2 && x < glyphLeft + 6 &&
                        y >= top + 2 && y < top + 12))
                    return 510;
                return 64;
            }
            return -1;
        }

        static int Luma(Scene scene, int x, int y, bool topSide, int phase)
        {
            const int canonicalY = topSide ? Height - 1 - y : y;
            int value = BaseLuma(scene, x, canonicalY, phase);
            int panelTop = -1;
            int panelLeft = 190;
            int panelRight = 450;
            if (scene == Scene::OneLineCue)
            {
                panelTop = 297;
                panelLeft = 230;
                panelRight = 410;
            }
            else if (scene == Scene::TwoLineCue)
            {
                panelTop = 285;
                panelLeft = 100;
                panelRight = 520;
            }
            else if (scene == Scene::ThreeLineCard)
                panelTop = 238;
            else if (scene == Scene::NearBarBackingCard)
                panelTop = 270;

            if (panelTop >= 0 && x >= panelLeft && x < panelRight &&
                canonicalY >= panelTop && canonicalY < 315)
                value = 64;

            if (scene == Scene::OneLineCue)
            {
                const int glyph = GlyphLuma(x, canonicalY, 240, 307, 12);
                if (glyph >= 0) value = glyph;
            }
            else if (scene == Scene::TwoLineCue)
            {
                const int upper = GlyphLuma(x, canonicalY, 210, 295, 16);
                const int lower = GlyphLuma(x, canonicalY, 220, 320, 14);
                if (upper >= 0) value = upper;
                if (lower >= 0) value = lower;
            }
            else if (scene == Scene::ThreeLineCard)
            {
                const int upper = GlyphLuma(x, canonicalY, 220, 244, 15);
                const int middle = GlyphLuma(x, canonicalY, 202, 278, 18);
                const int lower = GlyphLuma(x, canonicalY, 240, 311, 12);
                if (upper >= 0) value = upper;
                if (middle >= 0) value = middle;
                if (lower >= 0) value = lower;
            }
            else if (scene == Scene::NearBarBackingCard)
            {
                const int line = GlyphLuma(x, canonicalY, 226, 285, 14);
                if (line >= 0) value = line;
            }
            else if (scene == Scene::MidPictureText)
            {
                const int middle = GlyphLuma(x, canonicalY, 250, 150, 12);
                if (middle >= 0) value = middle;
            }
            return value;
        }

        static int Chroma(Scene scene, int y, int phase, bool isV)
        {
            if (scene == Scene::ChromaEdge)
            {
                const int value = (y & 1) ? 576 : 448;
                return isV ? 512 : value;
            }
            // Keep subtitle and active-picture parity fixtures neutral so their
            // detector palette and geometry match the established P210 path.
            (void)phase;
            return 512;
        }

        static uint16_t NativeCode(int code, VideoFrameEncoding encoding)
        {
            return static_cast<uint16_t>(IsEightBitYuv(encoding) ? code >> 2 : code);
        }

        static void WriteLe32(uint8_t* target, uint32_t word)
        {
            target[0] = static_cast<uint8_t>(word);
            target[1] = static_cast<uint8_t>(word >> 8);
            target[2] = static_cast<uint8_t>(word >> 16);
            target[3] = static_cast<uint8_t>(word >> 24);
        }

        static Fixture MakeFixture(VideoFrameEncoding encoding, Scene scene,
            bool topSide = false, int phase = 0)
        {
            Fixture fixture;
            fixture.encoding = encoding;
            fixture.state = new VideoState();
            fixture.state->valid = true;
            fixture.state->displayMode = std::make_shared<DisplayMode>(
                Width, Height, false, 24000, 1000);
            fixture.state->videoFrameEncoding = encoding;
            fixture.state->colorspace = ColorSpace::REC_709;
            fixture.rowBytes = fixture.state->BytesPerRow();
            fixture.pixels = std::make_shared<std::vector<uint8_t>>(
                fixture.rowBytes * Height, 0);

            if (IsEightBitYuv(encoding))
            {
                for (int y = 0; y < Height; ++y)
                {
                    uint8_t* row = fixture.pixels->data() +
                        static_cast<size_t>(y) * fixture.rowBytes;
                    for (int x = 0; x < Width; x += 2)
                    {
                        const size_t offset = static_cast<size_t>(x) * 2;
                        const int u = NativeCode(Chroma(scene, y, phase, false), encoding);
                        const int v = NativeCode(Chroma(scene, y, phase, true), encoding);
                        row[offset] = static_cast<uint8_t>(u);
                        row[offset + 1] = static_cast<uint8_t>(
                            NativeCode(Luma(scene, x, y, topSide, phase), encoding));
                        row[offset + 2] = static_cast<uint8_t>(v);
                        row[offset + 3] = static_cast<uint8_t>(
                            NativeCode(Luma(scene, x + 1, y, topSide, phase), encoding));
                    }
                }
                return fixture;
            }

            const size_t packsPerRow = (Width + 5) / 6;
            for (int y = 0; y < Height; ++y)
            {
                uint8_t* row = fixture.pixels->data() +
                    static_cast<size_t>(y) * fixture.rowBytes;
                for (size_t pack = 0; pack < packsPerRow; ++pack)
                {
                    uint16_t ys[6] = {};
                    uint16_t us[3] = {};
                    uint16_t vs[3] = {};
                    for (int pixel = 0; pixel < 6; ++pixel)
                    {
                        const int x = static_cast<int>((std::min)(
                            pack * 6 + static_cast<size_t>(pixel),
                            static_cast<size_t>(Width - 1)));
                        ys[pixel] = NativeCode(Luma(scene, x, y, topSide, phase), encoding);
                    }
                    for (int sample = 0; sample < 3; ++sample)
                    {
                        us[sample] = NativeCode(Chroma(scene, y, phase, false), encoding);
                        vs[sample] = NativeCode(Chroma(scene, y, phase, true), encoding);
                    }
                    const uint32_t words[4] = {
                        static_cast<uint32_t>(us[0]) | (static_cast<uint32_t>(ys[0]) << 10) |
                            (static_cast<uint32_t>(vs[0]) << 20),
                        static_cast<uint32_t>(ys[1]) | (static_cast<uint32_t>(us[1]) << 10) |
                            (static_cast<uint32_t>(ys[2]) << 20),
                        static_cast<uint32_t>(vs[1]) | (static_cast<uint32_t>(ys[3]) << 10) |
                            (static_cast<uint32_t>(us[2]) << 20),
                        static_cast<uint32_t>(ys[4]) | (static_cast<uint32_t>(vs[2]) << 10) |
                            (static_cast<uint32_t>(ys[5]) << 20),
                    };
                    uint8_t* group = row + pack * 16;
                    for (int word = 0; word < 4; ++word)
                        WriteLe32(group + word * 4, words[word]);
                }
            }
            return fixture;
        }

        template<typename Formatter>
        static Conversion RunFormatter(Fixture& fixture, Formatter& formatter,
            bool is422)
        {
            formatter.OnVideoState(fixture.state);
            Conversion result;
            result.contract = formatter.GetOutputContract();
            result.is422 = is422;
            result.pixels = std::make_shared<std::vector<uint8_t>>(
                static_cast<size_t>(formatter.GetOutFrameSize()), 0);
            VideoFrame frame(fixture.pixels->data(), 1, 0, nullptr);
            Assert::IsTrue(formatter.FormatVideoFrame(frame, result.pixels->data()));
            return result;
        }

        static Conversion Convert(Fixture& fixture, bool p010)
        {
            if (fixture.encoding == VideoFrameEncoding::UYVY ||
                fixture.encoding == VideoFrameEncoding::HDYC)
            {
                if (p010)
                {
                    CUYVYtoP010VideoFrameFormatter formatter;
                    formatter.SetConversionMethod(
                        CUYVYtoP010VideoFrameFormatter::ConversionMethod::SCALAR);
                    return RunFormatter(fixture, formatter, false);
                }
                CUYVYtoP210VideoFrameFormatter formatter;
                formatter.SetConversionMethod(
                    CUYVYtoP210VideoFrameFormatter::ConversionMethod::SCALAR);
                return RunFormatter(fixture, formatter, true);
            }

            if (p010)
            {
                CV210toP010VideoFrameFormatter formatter;
                formatter.SetConversionMethod(
                    CV210toP010VideoFrameFormatter::ConversionMethod::STANDARD);
                formatter.SetChromaDownsampling(
                    CV210toP010VideoFrameFormatter::ChromaDownsampling::AVERAGE);
                return RunFormatter(fixture, formatter, false);
            }
            CV210toP210VideoFrameFormatter formatter;
            formatter.SetConversionMethod(
                CV210toP210VideoFrameFormatter::ConversionMethod::SCALAR);
            return RunFormatter(fixture, formatter, true);
        }

        static AnalysisFrameInput InputFor(Fixture& fixture,
            const Conversion& converted, AnalysisSourceProfile profile,
            bool is422)
        {
            AnalysisFrameInput input;
            input.nativeData = fixture.pixels->data();
            input.nativeBytes = fixture.pixels->size();
            input.nativeRowBytes = fixture.rowBytes;
            input.nativeOwner = fixture.pixels;
            input.convertedData = converted.pixels->data();
            input.convertedBytes = converted.pixels->size();
            input.convertedRowBytes = Width * 2;
            input.convertedChromaRowBytes = Width * 2;
            input.convertedOwner = converted.pixels;
            input.convertedContract = converted.contract;
            input.width = Width;
            input.height = Height;
            input.encoding = fixture.encoding;
            input.colorspace = ColorSpace::REC_709;
            input.sourceGeneration = 23;
            input.profile = profile;
            input.rendererUsesNative = false;
            input.converted422 = is422;
            return input;
        }

        static AnalysisLumaSource LegacyNativeSource(const Fixture& fixture)
        {
            AnalysisLumaSource source;
            source.data = fixture.pixels->data();
            source.dataBytes = fixture.pixels->size();
            source.width = Width;
            source.height = Height;
            source.rowBytes = fixture.rowBytes;
            source.format = AnalysisLumaFormat::NativeYuv422;
            source.encoding = fixture.encoding;
            source.colorspace = ColorSpace::REC_709;
            source.generation = 23;
            return source;
        }

        static AnalysisLumaSource LegacyConvertedSource(const Fixture& fixture,
            const Conversion& converted, bool p010)
        {
            AnalysisLumaSource source;
            source.data = converted.pixels->data();
            source.dataBytes = converted.pixels->size();
            source.width = Width;
            source.height = Height;
            source.rowBytes = Width * 2;
            source.chromaRowBytes = Width * 2;
            source.format = p010 ? AnalysisLumaFormat::P010 : AnalysisLumaFormat::P210;
            source.encoding = fixture.encoding;
            source.colorspace = ColorSpace::REC_709;
            source.generation = 23;
            return source;
        }

        static void AssertSameSamples(const AnalysisLumaSource& expected,
            const AnalysisLumaSource& actual)
        {
            const std::array<int, 8> specialX = { 0, 189, 190, 220, 239, 240, 449, 639 };
            const std::array<int, 12> specialY = { 0, 44, 45, 150, 238, 244,
                278, 311, 314, 315, 325, 359 };
            for (int y = 0; y < Height; y += 13)
            {
                for (int x = 0; x < Width; x += 17)
                {
                    AnalysisLumaSample a, b;
                    Assert::IsTrue(expected.Sample(x, y, a));
                    Assert::IsTrue(actual.Sample(x, y, b));
                    Assert::AreEqual(static_cast<int>(a.luma), static_cast<int>(b.luma));
                    Assert::AreEqual(static_cast<int>(a.chromaU), static_cast<int>(b.chromaU));
                    Assert::AreEqual(static_cast<int>(a.chromaV), static_cast<int>(b.chromaV));
                }
            }
            for (int y : specialY)
                for (int x : specialX)
                {
                    AnalysisLumaSample a, b;
                    Assert::IsTrue(expected.Sample(x, y, a));
                    Assert::IsTrue(actual.Sample(x, y, b));
                    Assert::AreEqual(static_cast<int>(a.luma), static_cast<int>(b.luma));
                    Assert::AreEqual(static_cast<int>(a.chromaU), static_cast<int>(b.chromaU));
                    Assert::AreEqual(static_cast<int>(a.chromaV), static_cast<int>(b.chromaV));
                }
        }

        static void AssertSameActivePicture(const AnalysisLumaSource& expected,
            const AnalysisLumaSource& actual)
        {
            const auto a = ExtractActivePictureEvidence(expected);
            const auto b = ExtractActivePictureEvidence(actual);
            Assert::AreEqual(a.available, b.available);
            Assert::AreEqual(static_cast<int>(a.classification),
                static_cast<int>(b.classification));
            Assert::AreEqual(a.trustedBounds.left, b.trustedBounds.left);
            Assert::AreEqual(a.trustedBounds.top, b.trustedBounds.top);
            Assert::AreEqual(a.trustedBounds.right, b.trustedBounds.right);
            Assert::AreEqual(a.trustedBounds.bottom, b.trustedBounds.bottom);
            Assert::AreEqual(a.trustedBounds.rasterWidth, b.trustedBounds.rasterWidth);
            Assert::AreEqual(a.trustedBounds.rasterHeight, b.trustedBounds.rasterHeight);
        }

        static SubtitleBoxResult Measure(const AnalysisLumaSource& source)
        {
            SubtitleBoxDetector detector;
            return detector.Analyze(source, 45, 315, 1, 2);
        }

        static void AssertSameSubtitleResult(const AnalysisLumaSource& expected,
            const AnalysisLumaSource& actual, int expectedLines, bool topSide)
        {
            const auto a = Measure(expected);
            const auto b = Measure(actual);
            Assert::AreEqual(a.detected, b.detected);
            Assert::AreEqual(a.lineCount, b.lineCount);
            Assert::AreEqual(a.workLimit, b.workLimit);
            Assert::AreEqual(a.bounds.left, b.bounds.left);
            Assert::AreEqual(a.bounds.top, b.bounds.top);
            Assert::AreEqual(a.bounds.right, b.bounds.right);
            Assert::AreEqual(a.bounds.bottom, b.bounds.bottom);
            if (expectedLines > 0)
            {
                Assert::IsTrue(a.detected);
                Assert::AreEqual(expectedLines, a.lineCount);
                if (expectedLines == 3)
                {
                    Assert::IsTrue(a.bounds.top <= (topSide ? Height - 325 : 244));
                    Assert::IsTrue(a.bounds.bottom >= (topSide ? Height - 244 : 325));
                }
            }
            else
                Assert::IsFalse(a.detected);
        }

        static void AssertYuvDomainParity(VideoFrameEncoding encoding, Scene scene,
            bool topSide, int expectedLines, int phase = 0)
        {
            auto fixture = MakeFixture(encoding, scene, topSide, phase);
            auto p210 = Convert(fixture, false);
            auto p010 = Convert(fixture, true);
            auto liveP210Input = InputFor(fixture, p210,
                AnalysisSourceProfile::LiveRenderer, true);
            liveP210Input.rendererUsesNative = false;
            auto liveP010Input = InputFor(fixture, p010,
                AnalysisSourceProfile::LiveRenderer, false);
            liveP010Input.rendererUsesNative = false;
            auto queuedP210Input = InputFor(fixture, p210,
                AnalysisSourceProfile::QueuedCapture, true);
            auto queuedP010Input = InputFor(fixture, p010,
                AnalysisSourceProfile::QueuedCapture, false);
            const auto liveP210 = PrepareAnalysisFrame(liveP210Input);
            const auto liveP010 = PrepareAnalysisFrame(liveP010Input);
            const auto queuedP210 = PrepareAnalysisFrame(queuedP210Input);
            const auto queuedP010 = PrepareAnalysisFrame(queuedP010Input);
            for (const auto* prepared : { &liveP210, &liveP010,
                &queuedP210, &queuedP010 })
            {
                Assert::IsTrue(prepared->IsValid());
                Assert::IsFalse(prepared->usesConverted);
                Assert::AreEqual(static_cast<int>(AnalysisFrameDomain::NativeYuv422),
                    static_cast<int>(prepared->domain));
                Assert::AreEqual(IsEightBitYuv(encoding) ? 8 : 10,
                    static_cast<int>(prepared->colorDepth));
            }
            Assert::AreEqual(liveP210.domainKey, liveP010.domainKey);
            Assert::AreEqual(liveP210.domainKey, queuedP210.domainKey);
            Assert::AreEqual(liveP210.domainKey, queuedP010.domainKey);

            const auto oldFormatted = LegacyConvertedSource(fixture, p210, false);
            const auto oldNative = LegacyNativeSource(fixture);
            AssertSameSamples(oldFormatted, oldNative);
            for (const auto* prepared : { &liveP210, &liveP010,
                &queuedP210, &queuedP010 })
            {
                AssertSameSamples(oldNative, prepared->source);
                AssertSameActivePicture(oldNative, prepared->source);
                AssertSameSubtitleResult(oldNative, prepared->source,
                    expectedLines, topSide);
            }
        }

    public:
        TEST_METHOD(OptionalNativeSubtitleCaptureReportsAcquisitionEvidence)
        {
            char* path = nullptr; size_t length = 0;
            _dupenv_s(&path, &length, "VP_NATIVE_ANALYSIS_FIXTURE");
            const std::string fixture = path ? path : "";
            std::free(path);
            if (fixture.empty()) { Logger::WriteMessage("Native capture diagnostic not requested."); return; }
            std::ifstream metadata(fixture + ".txt");
            AnalysisLumaSource source;
            int format = 0, encoding = 0, colorspace = 0, top = 0, bottom = 0, script = 0;
            metadata >> source.width >> source.height >> source.rowBytes >> source.chromaRowBytes
                >> format >> encoding >> colorspace >> top >> bottom >> script;
            Assert::IsTrue(bool(metadata));
            std::ifstream input(fixture, std::ios::binary | std::ios::ate);
            Assert::IsTrue(bool(input));
            const auto bytes = input.tellg();
            Assert::IsTrue(bytes > 0);
            std::vector<uint8_t> pixels(static_cast<size_t>(bytes));
            input.seekg(0); input.read(reinterpret_cast<char*>(pixels.data()), bytes);
            Assert::IsTrue(bool(input));
            source.data = pixels.data(); source.dataBytes = pixels.size();
            source.format = static_cast<AnalysisLumaFormat>(format);
            source.encoding = static_cast<VideoFrameEncoding>(encoding);
            source.colorspace = static_cast<ColorSpace>(colorspace); source.generation = 1;
            Assert::IsTrue(source.IsValid());
            const auto raw = ExtractActivePictureEvidence(source);
            const auto bars = ExtractSubtitleBarEvidence(source);
            const auto symmetric = EvaluateSymmetricVerticalBarHypothesis(source, raw, true);
            const auto started = std::chrono::steady_clock::now();
            const auto captionBacked = EvaluateCaptionBackedVerticalBarHypothesis(source, raw,
                static_cast<SubtitleScriptHint>(script), 1);
            const auto bootstrapMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now()-started).count();
            SubtitleBoxDetector detector;
            ActivePictureFrameIdentity identity; identity.acceptedSequence = 1;
            identity.transportGeneration = source.generation;
            detector.SetScriptHint(static_cast<SubtitleScriptHint>(script));
            detector.SetNearBarDistance(20);
            const auto measurement = SubtitleBoxLookahead::Measure(detector, source, raw, identity, false);
            SubtitlePictureAuthority shared;
            shared.required = true; shared.identity = identity;
            shared.bounds = captionBacked.trustedBounds;
            const auto worker = SubtitleBoxLookahead::Measure(detector, source, raw,
                identity, false, {}, 0, 0, shared);
            const auto reaffirmStarted = std::chrono::steady_clock::now();
            const auto reaffirmed = EvaluateCurrentCaptionVerticalBarReaffirmation(
                source, raw, shared.bounds, worker, identity, 0, 0,
                static_cast<SubtitleScriptHint>(script));
            const auto reaffirmMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now()-reaffirmStarted).count();
            const auto proofStarted = std::chrono::steady_clock::now();
            const auto subtitleArProof = EvaluateSubtitleAssistedVerticalBarProof(
                source, raw, worker, identity, 0, 0,
                static_cast<SubtitleScriptHint>(script));
            const auto proofMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now()-proofStarted).count();
            SubtitleMeasurementSampler sampler;
            const auto sampledWorker = sampler.Measure(source, worker, {}, 23.976, raw);
            const auto darkness = EvaluateActivePictureGlobalNearBlack(source);
            const auto arStarted = std::chrono::steady_clock::now();
            const auto arDarkness = EvaluateActivePictureArDarkness(source, raw, darkness);
            const auto arDarknessMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now()-arStarted).count();
            if (arDarkness.distributedDarkPicture)
            {
                // Replay startup after an actually dark first frame. Current
                // raw/native pixels can reopen normal admission after its dwell;
                // 24-to-60Hz display repeats must not add confirmation votes.
                using namespace AlphaSourceCrop;
                const auto retention = EvaluateActivePicturePresentationRetention(source, raw.trustedBounds);
                Assert::IsTrue(retention.currentlyPixelSafe);
                Assert::IsTrue(retention.globalNearBlack);
                Assert::IsFalse(retention.EffectiveArNearBlack());
                NearBlackPresentationEpisodeInput episode;
                episode.measurementCurrent = episode.nearBlackEvaluated = true;
                episode.globalNearBlack = true;
                episode.sourceGeneration = source.generation;
                episode.presentationEpoch = 1;
                episode.sourceSequence = 1;
                episode.framesPerSecond = 23.976;
                auto decision = EvaluateNearBlackPresentationEpisode(episode);
                episode.globalNearBlack = arDarkness.EffectiveNearBlack();
                episode.nativeBootstrapContractAvailable = raw.available;
                episode.nativeBootstrapContract = raw.trustedBounds;
                episode.nativeBootstrapRetentionEvaluated = retention.analysisValid && retention.presentationValid;
                episode.nativeBootstrapRetentionSafe = retention.currentlyPixelSafe;
                episode.nativeBootstrapOutwardVisible = retention.outwardVisibleBoundsAvailable;
                episode.nativeBootstrapSourceGeneration = source.generation;
                episode.nativeBootstrapPresentationEpoch = episode.presentationEpoch;
                for (uint64_t sequence = 2; sequence <= 11; ++sequence)
                {
                    episode.previous = decision.state;
                    episode.sourceSequence = episode.nativeBootstrapSourceSequence = sequence;
                    episode.cadenceRepeat = false;
                    decision = EvaluateNearBlackPresentationEpisode(episode);
                    Assert::AreEqual(sequence == 11, decision.bootstrapReleased);
                    if (sequence == 11) break;
                    for (int repeat = 0; repeat < 2; ++repeat)
                    {
                        episode.previous = decision.state;
                        episode.cadenceRepeat = true;
                        const auto votes = decision.state.bootstrapSamples;
                        decision = EvaluateNearBlackPresentationEpisode(episode);
                        Assert::AreEqual(votes, decision.state.bootstrapSamples);
                        Assert::IsFalse(decision.bootstrapReleased);
                    }
                }
                Assert::IsTrue(decision.ended && decision.resetTransitionEvidence);
                Assert::IsFalse(decision.releasedToTrustedCrop);
                const auto admitted = ConstrainNearBlackCropAcquisition(raw, arDarkness.EffectiveNearBlack());
                Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED), int(admitted.classification));
            }
            std::ostringstream out;
            out << "Native acquisition: class=" << static_cast<int>(raw.classification)
                << " raw=" << raw.proposedBounds.top << "-" << raw.proposedBounds.bottom
                << " horizontal=" << ActivePictureAxisStateName(raw.axisEvidence.horizontal.state)
                << ":" << ActivePictureAxisReasonName(raw.axisEvidence.horizontal.reason)
                << " h_complete=" << raw.axisEvidence.horizontal.scanComplete
                << " h_candidate=" << raw.axisEvidence.horizontal.barCandidate
                << " current_flags=" << worker.analyzed << worker.analysisRefresh << worker.pendingRefresh
                << worker.discontinuity << worker.barTrackingAuthority << worker.currentAnchorUsesTrackedEdge
                << " near=" << worker.text.nearBarEligibilityMeasured << worker.text.nearBarEligible
                << " raw_ink=" << bool(worker.ink && worker.ink->rawEvidenceComplete)
                << " distributed_bars=" << bars.topDistributedBoundary << bars.bottomDistributedBoundary
                << " global_p90=" << darkness.lumaP90 << " global_near_black=" << darkness.nearBlack
                << " ar_near_black=" << arDarkness.EffectiveNearBlack()
                << " distributed_picture=" << arDarkness.distributedDarkPicture
                << " textured_tiles=" << arDarkness.texturedTiles
                << " quadrants=" << arDarkness.texturedQuadrants
                << " picture_median=" << arDarkness.pictureMedian
                << " tile_median_spread=" << arDarkness.tileMedianSpread
                << " ar_darkness_ms=" << arDarknessMs
                << " bar_floor=" << raw.top.lumaFloor << "," << raw.bottom.lumaFloor
                << " symmetric=" << static_cast<int>(symmetric.classification) << ":" << symmetric.reason
                << " bars=" << bars.top << "-" << bars.bottom << " reason=" << bars.reason
                << " support=" << bars.topBoundarySupport << "," << bars.bottomBoundarySupport
                << " caption_bootstrap=" << static_cast<int>(captionBacked.classification)
                << ":" << captionBacked.trustedBounds.top << "-" << captionBacked.trustedBounds.bottom
                << " bootstrap_ms=" << bootstrapMs
                << " worker_reaffirm=" << static_cast<int>(reaffirmed.classification)
                << " subtitle_ar_proof=" << subtitleArProof.accepted
                << ":" << subtitleArProof.reason
                << " candidate=" << subtitleArProof.candidate.top << "-" << subtitleArProof.candidate.bottom
                << " flank_support=" << subtitleArProof.topFlankSupport << "," << subtitleArProof.bottomFlankSupport
                << " unknown=" << subtitleArProof.unknownSamples
                << " luma_samples=" << subtitleArProof.lumaSamples
                << " profile=" << subtitleArProof.evidence.verticalBarGuardProfile.reason
                << " mismatch=" << subtitleArProof.evidence.verticalBarGuardProfile.mismatchSamples
                << " first=" << subtitleArProof.evidence.verticalBarGuardProfile.firstMismatchX
                << "," << subtitleArProof.evidence.verticalBarGuardProfile.firstMismatchY
                << " max_delta=" << subtitleArProof.evidence.verticalBarGuardProfile.maxLumaDelta
                << "," << subtitleArProof.evidence.verticalBarGuardProfile.maxChromaDelta
                << " proof_ms=" << proofMs
                << " worker_proof=" << sampledWorker.subtitleArProof.accepted
                << ":" << sampledWorker.subtitleArProof.reason
                << " worker_scan_ms=" << sampledWorker.analysisMs
                << " measured_backing=" << subtitleArProof.usedMeasuredBacking
                << " worker_independent=" << worker.independentBarAnalysis
                << " current_text=" << worker.text.detected                << " worker_reaffirm_ms=" << reaffirmMs
                << " anchor=" << measurement.text.anchor.top << "-" << measurement.text.anchor.bottom
                << " independent_detected=" << measurement.text.detected
                << " lines=" << measurement.text.lineCount
                << " measured=" << measurement.pictureTop << "-" << measurement.pictureBottom
                << " box=" << measurement.text.bounds.left << "," << measurement.text.bounds.top
                << "-" << measurement.text.bounds.right << "," << measurement.text.bounds.bottom;
            Logger::WriteMessage(out.str().c_str());

            // The paused scene may contain no caption. Check native AR and
            // explicit negative glyph expectations without granting a subtitle
            // certificate merely because a fixture was supplied.
            char* nativeExpectedText=nullptr;size_t nativeExpectedLength=0;
            _dupenv_s(&nativeExpectedText,&nativeExpectedLength,"VP_NATIVE_EXPECTED_AR_BOUNDS");
            const std::string nativeExpectedValue=nativeExpectedText?nativeExpectedText:"";
            std::free(nativeExpectedText);
            if(!nativeExpectedValue.empty()) {
                int expectedTop=0,expectedBottom=0;
                std::istringstream expectedInput(nativeExpectedValue);
                expectedInput>>expectedTop>>expectedBottom;
                Assert::IsTrue(bool(expectedInput));
                Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED),int(raw.classification));
                Assert::AreEqual(expectedTop,raw.trustedBounds.top);
                Assert::AreEqual(expectedBottom,raw.trustedBounds.bottom);
                ActivePictureTransitionModel model;
                ActivePictureTransitionDecision decision;
                for(uint64_t sequence=1;sequence<=ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;++sequence)
                    decision=model.Observe(MakeActivePictureObservation(raw,sequence,23.976));
                Assert::IsTrue(decision.publish && decision.stable);
                Assert::AreEqual(expectedTop,decision.bounds.top);
                Assert::AreEqual(expectedBottom,decision.bounds.bottom);
                Logger::WriteMessage("Native AR expected bounds and fresh-frame publication passed.");
            }
            char* linesExpectedText=nullptr;size_t linesExpectedLength=0;
            _dupenv_s(&linesExpectedText,&linesExpectedLength,"VP_NATIVE_EXPECTED_SUBTITLE_LINES");
            const std::string linesExpectedValue=linesExpectedText?linesExpectedText:"";
            std::free(linesExpectedText);
            if(!linesExpectedValue.empty()) {
                const int expectedLines=std::stoi(linesExpectedValue);
                Assert::AreEqual(expectedLines,sampledWorker.text.lineCount);
                Assert::AreEqual(expectedLines>0,sampledWorker.text.detected);
                if(expectedLines==0)Assert::IsFalse(sampledWorker.subtitleArProof.accepted);
            }

            // Optional exact expected output makes a recorded repro a checked
            // regression without committing private captured movie pixels.
            char* expectedText=nullptr;size_t expectedLength=0;
            _dupenv_s(&expectedText,&expectedLength,"VP_NATIVE_EXPECTED_SUBTITLE_AR_BOUNDS");
            const std::string expectedValue=expectedText?expectedText:"";
            std::free(expectedText);
            if(!expectedValue.empty()) {
                int expectedTop=0,expectedBottom=0;
                std::istringstream expectedInput(expectedValue);
                expectedInput>>expectedTop>>expectedBottom;
                Assert::IsTrue(bool(expectedInput));
                Assert::IsTrue(sampledWorker.text.detected && sampledWorker.subtitleArProof.accepted);
                Assert::AreEqual(expectedTop,sampledWorker.subtitleArProof.candidate.top);
                Assert::AreEqual(expectedBottom,sampledWorker.subtitleArProof.candidate.bottom);
                ActivePictureTransitionModel model;
                ActivePictureTransitionDecision decision;
                for(uint64_t sequence=1;sequence<=ActivePictureTransitionModel::INITIAL_CONFIRMATIONS;++sequence) {
                    auto key=worker;key.identity.acceptedSequence=sequence;
                    const auto currentSample=sampler.Measure(source,key,{},23.976,raw);
                    Assert::IsTrue(currentSample.subtitleArProof.IsCurrentFor(source,key.identity,
                        key.policyGeneration,key.continuityGeneration,key.scriptHint));
                    decision=model.Observe(MakeActivePictureObservation(
                        currentSample.subtitleArProof.evidence,sequence,23.976));
                }
                Assert::IsTrue(decision.publish && decision.stable);
                Assert::AreEqual(expectedTop,decision.bounds.top);
                Assert::AreEqual(expectedBottom,decision.bounds.bottom);
                Logger::WriteMessage("Native subtitle AR expected bounds and fresh-frame publication passed.");
            }

            // Opt-in transition replay: the native frame is current, while the
            // published presentation may still carry a previous movie aperture.
            char* retainedText = nullptr; size_t retainedLength = 0;
            _dupenv_s(&retainedText, &retainedLength, "VP_NATIVE_RETAINED_APERTURE");
            const std::string retainedValue = retainedText ? retainedText : "";
            std::free(retainedText);
            if (!retainedValue.empty())
            {
                int retainedTop = 0, retainedBottom = 0;
                std::istringstream retainedInput(retainedValue);
                retainedInput >> retainedTop >> retainedBottom;
                Assert::IsTrue(bool(retainedInput));
                shared.bounds = raw.trustedBounds;
                shared.bounds.top = retainedTop;
                shared.bounds.bottom = retainedBottom;
                const auto transitionStarted = std::chrono::steady_clock::now();
                const auto transition = SubtitleBoxLookahead::Measure(detector, source,
                    raw, identity, false, {}, 0, 0, shared);
                const auto transitionMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now()-transitionStarted).count();
                std::ostringstream transitionLog;
                transitionLog << "Native retained-aperture replay: retained="
                    << retainedTop << "-" << retainedBottom
                    << " measured=" << transition.pictureTop << "-" << transition.pictureBottom
                    << " authority=" << transition.barAuthority
                    << " detected=" << transition.text.detected
                    << " reason=" << transition.barEvidence.reason
                    << " scan_ms=" << transitionMs;
                Logger::WriteMessage(transitionLog.str().c_str());
                const auto retention = EvaluateActivePicturePresentationRetention(source, shared.bounds);
                AlphaSourceCrop::OutwardPictureConfirmationState proofState;
                AlphaSourceCrop::OutwardPictureConfirmationDecision proof;
                for (uint64_t sequence = 1; sequence <= 3; ++sequence)
                {
                    proof = AlphaSourceCrop::ConfirmOutwardPictureTransition(proofState,
                        shared.bounds, raw.trustedBounds, retention, 1, sequence);
                    proofState = proof.state;
                }
                std::ostringstream expansionLog;
                expansionLog << "Native AR expansion replay: authoritative=" << proof.authoritative
                    << " confirmations=" << proof.state.confirmations
                    << " reason=" << proof.diagnosticReason
                    << " bottom_p90=" << retention.expandingBottom.lumaP90
                    << " bottom_texture=" << retention.expandingBottom.texture;
                Logger::WriteMessage(expansionLog.str().c_str());
                Assert::IsTrue(proof.authoritative,
                    L"Current native picture failed to displace the retained movie aperture");
                // A no-caption current frame must not become a caption merely
                // because an older aperture now cuts through picture content.
                if (!measurement.text.detected)
                {
                    Assert::IsFalse(transition.text.detected,
                        L"Retained presentation geometry created a false subtitle");
                    Assert::AreEqual(measurement.pictureTop, transition.pictureTop,
                        L"Subtitle analysis retained an obsolete top picture boundary");
                    Assert::AreEqual(measurement.pictureBottom, transition.pictureBottom,
                        L"Subtitle analysis retained an obsolete bottom picture boundary");
                }
            }
        }
        TEST_METHOD(SubtitleAssistedProofIsParityStableAcrossNativeP210P010AndDecodedDv)
        {
            auto fixture = MakeFixture(VideoFrameEncoding::V210,
                Scene::NearBarBackingCard);
            auto p210Conversion = Convert(fixture, false);
            auto p010Conversion = Convert(fixture, true);
            auto native = LegacyNativeSource(fixture);
            auto p210 = LegacyConvertedSource(fixture, p210Conversion, false);
            auto p010 = LegacyConvertedSource(fixture, p010Conversion, true);
            auto decodedDv = p210;
            decodedDv.format = AnalysisLumaFormat::DecodedDolbyVision422;
            decodedDv.encoding = VideoFrameEncoding::UNKNOWN;

            std::array<AnalysisLumaSource, 4> sources = {
                native, p210, p010, decodedDv };
            int expectedTop = -1, expectedBottom = -1;
            for (size_t i = 0; i < sources.size(); ++i)
            {
                const auto& source = sources[i];
                const auto raw = ExtractActivePictureEvidence(source);
                ActivePictureFrameIdentity identity;
                identity.transportGeneration = source.generation;
                identity.acceptedSequence = 1;
                identity.sourceFrameNumber = 500 + i;
                identity.captureTimestamp = 900000 + i;
                identity.sourceFormatGeneration = 2;
                identity.viewportGeneration = 3;
                identity.rendererGeneration = 4;
                identity.analysisDomainKey = 20 + i;

                SubtitleBoxDetector detector;
                detector.SetScriptHint(SubtitleScriptHint::Latin);
                detector.SetNearBarDistance(20);
                SubtitlePictureAuthority shared;
                shared.required = true;
                shared.identity = identity;
                const auto current = SubtitleBoxLookahead::Measure(detector,
                    source, raw, identity, false, {}, 31, 41, shared);
                const auto proof = EvaluateSubtitleAssistedVerticalBarProof(
                    source, raw, current, identity, 31, 41,
                    SubtitleScriptHint::Latin);
                Assert::IsTrue(current.text.detected && current.ink &&
                    current.independentBarAnalysis,
                    L"Every format must produce the same current cue and independent physical bars.");
                Assert::IsTrue(proof.accepted,
                    L"The same native, formatted, and decoded-DV pixels must prove the same crop.");
                Assert::IsTrue(proof.IsCurrentFor(source, identity, 31, 41,
                    SubtitleScriptHint::Latin));
                if (i == 0)
                {
                    expectedTop = proof.candidate.top;
                    expectedBottom = proof.candidate.bottom;
                }
                else
                {
                    Assert::AreEqual(expectedTop, proof.candidate.top);
                    Assert::AreEqual(expectedBottom, proof.candidate.bottom);
                }
            }
        }
        TEST_METHOD(P210FormatterParityPreservesOneTwoAndThreeLineGeometryAcrossNativeInputs)
        {
            const std::array<VideoFrameEncoding, 3> encodings = {
                VideoFrameEncoding::UYVY, VideoFrameEncoding::HDYC,
                VideoFrameEncoding::V210 };
            for (const auto encoding : encodings)
            {
                AssertYuvDomainParity(encoding, Scene::OneLineCue, false, 1);
                AssertYuvDomainParity(encoding, Scene::TwoLineCue, false, 2);
                AssertYuvDomainParity(encoding, Scene::ThreeLineCard, false, 3);
                AssertYuvDomainParity(encoding, Scene::ThreeLineCard, true, 3);
                AssertYuvDomainParity(encoding, Scene::MidPictureText, false, 0);
                AssertYuvDomainParity(encoding, Scene::MovingTexture, false, 0, 0);
                AssertYuvDomainParity(encoding, Scene::MovingTexture, false, 0, 1);
            }
        }

        TEST_METHOD(P010AndP210UploadChoicesKeepOneNativeAnalysisDomain)
        {
            const std::array<VideoFrameEncoding, 3> encodings = {
                VideoFrameEncoding::UYVY, VideoFrameEncoding::HDYC,
                VideoFrameEncoding::V210 };
            for (const auto encoding : encodings)
            {
                auto fixture = MakeFixture(encoding, Scene::ChromaEdge);
                auto p010 = Convert(fixture, true);
                auto p210 = Convert(fixture, false);
                auto liveP010Input = InputFor(fixture, p010,
                    AnalysisSourceProfile::LiveRenderer, false);
                liveP010Input.rendererUsesNative = false;
                auto liveP210Input = InputFor(fixture, p210,
                    AnalysisSourceProfile::LiveRenderer, true);
                liveP210Input.rendererUsesNative = false;
                auto queuedP010Input = InputFor(fixture, p010,
                    AnalysisSourceProfile::QueuedCapture, false);
                auto queuedP210Input = InputFor(fixture, p210,
                    AnalysisSourceProfile::QueuedCapture, true);
                const auto liveP010 = PrepareAnalysisFrame(liveP010Input);
                const auto liveP210 = PrepareAnalysisFrame(liveP210Input);
                const auto queuedP010 = PrepareAnalysisFrame(queuedP010Input);
                const auto queuedP210 = PrepareAnalysisFrame(queuedP210Input);
                const auto oldP010 = LegacyConvertedSource(fixture, p010, true);
                const auto oldP210 = LegacyConvertedSource(fixture, p210, false);
                const auto native = LegacyNativeSource(fixture);
                for (const auto* prepared : { &liveP010, &liveP210,
                    &queuedP010, &queuedP210 })
                {
                    Assert::IsTrue(prepared->IsValid());
                    Assert::IsFalse(prepared->usesConverted);
                    Assert::AreEqual(static_cast<int>(AnalysisFrameDomain::NativeYuv422),
                        static_cast<int>(prepared->domain));
                    AssertSameSamples(native, prepared->source);
                }
                Assert::AreEqual(liveP010.domainKey, liveP210.domainKey);
                Assert::AreEqual(liveP010.domainKey, queuedP010.domainKey);
                Assert::AreEqual(liveP010.domainKey, queuedP210.domainKey);
                AssertSameSamples(oldP210, native);

                for (int y = 0; y < Height; ++y)
                {
                    for (int x : { 0, 1, 2, 319, 638, 639 })
                    {
                        AnalysisLumaSample canonical, p010Sample, liveSample,
                            queuedSample;
                        Assert::IsTrue(native.Sample(x, y, canonical));
                        Assert::IsTrue(oldP010.Sample(x, y, p010Sample));
                        Assert::IsTrue(liveP010.source.Sample(x, y, liveSample));
                        Assert::AreEqual(static_cast<int>(canonical.luma),
                            static_cast<int>(p010Sample.luma));
                        Assert::AreEqual(static_cast<int>(canonical.luma),
                            static_cast<int>(liveSample.luma));
                        Assert::AreEqual(512, static_cast<int>(p010Sample.chromaU));
                        Assert::AreEqual(512, static_cast<int>(p010Sample.chromaV));
                        Assert::AreEqual(y & 1 ? 576 : 448,
                            static_cast<int>(canonical.chromaU));
                        Assert::AreEqual(512, static_cast<int>(canonical.chromaV));
                        Assert::IsTrue(queuedP010.source.Sample(x, y, queuedSample));
                        Assert::AreEqual(static_cast<int>(canonical.chromaU),
                            static_cast<int>(queuedSample.chromaU));
                        Assert::AreEqual(static_cast<int>(canonical.chromaV),
                            static_cast<int>(queuedSample.chromaV));
                    }
                }
            }
        }

        TEST_METHOD(MissingOrUnsupportedCapturePixelsCannotBorrowRenderConversion)
        {
            auto fixture = MakeFixture(VideoFrameEncoding::UYVY,
                Scene::OneLineCue);
            auto p010 = Convert(fixture, true);
            auto input = InputFor(fixture, p010,
                AnalysisSourceProfile::LiveRenderer, false);
            input.rendererUsesNative = false;
            input.nativeData = nullptr;
            input.nativeOwner.reset();
            Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());

            input.nativeData = fixture.pixels->data();
            input.nativeBytes = fixture.pixels->size();
            input.nativeRowBytes = fixture.rowBytes;
            input.nativeOwner = fixture.pixels;
            input.encoding = VideoFrameEncoding::UNKNOWN;
            Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());
        }

        TEST_METHOD(AnalysisDomainChangesInvalidateEvidenceReuseButPreserveTransportIdentity)
        {
            ActivePictureFrameIdentity queuedIdentity = { 5, 19, 31, 77, 2, 3, 4, 101 };
            ActivePictureFrameIdentity liveIdentity = queuedIdentity;
            liveIdentity.analysisDomainKey = 202;
            Assert::IsTrue(SameActivePictureFrameIdentity(queuedIdentity, liveIdentity));
            Assert::IsFalse(SameAnalysisFrameIdentity(queuedIdentity, liveIdentity));

            SubtitleBoxObservation queued;
            queued.identity = queuedIdentity;
            queued.width = 640;
            queued.height = 360;
            queued.nearBarDistance = 24;
            queued.analyzed = true;
            queued.barAuthority = true;
            queued.pictureTop = 45;
            queued.pictureBottom = 315;
            queued.barEvidence.top = 45;
            queued.barEvidence.bottom = 315;
            queued.text.detected = true;
            SubtitleBoxObservation live = queued;
            live.identity = liveIdentity;
            Assert::IsTrue(SubtitleMeasurementWorker::SameKey(queued, queued));
            Assert::IsFalse(SubtitleMeasurementWorker::SameKey(queued, live));
            Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(queued,
                queuedIdentity, 640, 360, 0, 0, false, {}));
            Assert::IsFalse(SubtitleBoxLookahead::CanReuseMeasurement(queued,
                liveIdentity, 640, 360, 0, 0, false, {}));

            auto nextLiveIdentity = liveIdentity;
            ++nextLiveIdentity.acceptedSequence;
            SubtitleBarTrackingReference reference;
            reference.identity = queuedIdentity;
            reference.width = 640;
            reference.height = 360;
            reference.pictureTop = 45;
            reference.pictureBottom = 315;
            reference.valid = true;
            Assert::IsFalse(reference.IsImmediatePredecessor(nextLiveIdentity,
                640, 360, 0, 0));

            SubtitlePictureAuthority queuedAuthority;
            queuedAuthority.required = true;
            queuedAuthority.bounds = { 0, 45, 640, 315, 640, 360 };
            queuedAuthority.identity = queuedIdentity;
            auto liveAuthority = queuedAuthority;
            liveAuthority.identity = liveIdentity;
            Assert::IsFalse(queuedAuthority.Matches(liveAuthority));
        }

        TEST_METHOD(SourceColorMetadataChangesInvalidateCanonicalEvidenceDomain)
        {
            auto fixture = MakeFixture(VideoFrameEncoding::UYVY,
                Scene::MovingTexture);
            auto p010 = Convert(fixture, true);
            auto p210 = Convert(fixture, false);
            auto input = InputFor(fixture, p010,
                AnalysisSourceProfile::LiveRenderer, false);
            input.rendererUsesNative = false;
            input.transferKey = 2;
            const auto sdr709 = PrepareAnalysisFrame(input);
            Assert::IsTrue(sdr709.IsValid());
            Assert::IsFalse(sdr709.usesConverted);

            input.profile = AnalysisSourceProfile::QueuedCapture;
            input.convertedData = p210.pixels->data();
            input.convertedBytes = p210.pixels->size();
            input.convertedRowBytes = Width * 2;
            input.convertedChromaRowBytes = Width * 2;
            input.convertedOwner = p210.pixels;
            input.convertedContract = p210.contract;
            input.converted422 = true;
            input.colorspace = ColorSpace::BT_2020;
            input.transferKey = 3;
            const auto hdr2020 = PrepareAnalysisFrame(input);
            Assert::IsTrue(hdr2020.IsValid());
            Assert::IsFalse(hdr2020.usesConverted);
            Assert::AreNotEqual(
                static_cast<unsigned long long>(sdr709.domainKey),
                static_cast<unsigned long long>(hdr2020.domainKey));

            ActivePictureFrameIdentity sdrIdentity;
            sdrIdentity.transportGeneration = 7;
            sdrIdentity.acceptedSequence = 11;
            sdrIdentity.sourceFrameNumber = 13;
            sdrIdentity.captureTimestamp = 17;
            sdrIdentity.sourceFormatGeneration = 19;
            sdrIdentity.analysisDomainKey = sdr709.domainKey;
            ActivePictureFrameIdentity hdrIdentity = sdrIdentity;
            hdrIdentity.analysisDomainKey = hdr2020.domainKey;
            Assert::IsTrue(SameActivePictureFrameIdentity(sdrIdentity, hdrIdentity));
            Assert::IsFalse(SameAnalysisFrameIdentity(sdrIdentity, hdrIdentity));
        }

        TEST_METHOD(PreparedCanonicalNativeFramesRetainTheirBackingOwner)
        {
            auto fixture = MakeFixture(VideoFrameEncoding::UYVY, Scene::ThreeLineCard);
            auto converted = Convert(fixture, false);
            std::weak_ptr<std::vector<uint8_t>> nativeLifetime = fixture.pixels;
            std::weak_ptr<std::vector<uint8_t>> convertedLifetime = converted.pixels;
            auto input = InputFor(fixture, converted,
                AnalysisSourceProfile::LiveRenderer, true);
            auto live = PrepareAnalysisFrame(input);
            input.profile = AnalysisSourceProfile::QueuedCapture;
            auto queued = PrepareAnalysisFrame(input);
            fixture.pixels.reset();
            converted.pixels.reset();
            input.nativeOwner.reset();
            input.convertedOwner.reset();
            Assert::IsFalse(nativeLifetime.expired());
            Assert::IsTrue(convertedLifetime.expired());
            AnalysisLumaSample sample;
            Assert::IsTrue(live.source.Sample(240, 311, sample));
            Assert::AreEqual(508, static_cast<int>(sample.luma));
            Assert::IsTrue(queued.source.Sample(240, 311, sample));
            Assert::AreEqual(508, static_cast<int>(sample.luma));
            live = {};
            Assert::IsFalse(nativeLifetime.expired());
            queued = {};
            Assert::IsTrue(nativeLifetime.expired());
        }

        TEST_METHOD(PreparedNativeRgbProfilesShareExactSamplingAndOwnership)
        {
            const std::array<VideoFrameEncoding, 5> encodings = {
                VideoFrameEncoding::BGRA_8BIT, VideoFrameEncoding::ARGB_8BIT,
                VideoFrameEncoding::R210, VideoFrameEncoding::R10b,
                VideoFrameEncoding::R10l };
            for (const auto encoding : encodings)
                for (const auto colorspace : { ColorSpace::REC_709, ColorSpace::BT_2020 })
                {
                    constexpr int width = 4;
                    constexpr int height = 2;
                    auto pixels = std::make_shared<std::vector<uint8_t>>(
                        static_cast<size_t>(width) * height * 4, 0);
                    const bool eightBit = encoding == VideoFrameEncoding::BGRA_8BIT ||
                        encoding == VideoFrameEncoding::ARGB_8BIT;
                    const uint16_t high = eightBit ? 255 : 1023;
                    const uint16_t middle = eightBit ? 128 : 512;
                    const std::array<std::array<uint16_t, 3>, width> rgb = {{
                        {{ 0, 0, 0 }}, {{ high, high, high }},
                        {{ high, 0, 0 }}, {{ 0, middle, high }}
                    }};
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x)
                        {
                            auto* pixel = pixels->data() +
                                static_cast<size_t>(y * width + x) * 4;
                            const auto& channels = rgb[x];
                            if (encoding == VideoFrameEncoding::BGRA_8BIT)
                            {
                                pixel[0] = static_cast<uint8_t>(channels[2]);
                                pixel[1] = static_cast<uint8_t>(channels[1]);
                                pixel[2] = static_cast<uint8_t>(channels[0]);
                            }
                            else if (encoding == VideoFrameEncoding::ARGB_8BIT)
                            {
                                pixel[1] = static_cast<uint8_t>(channels[0]);
                                pixel[2] = static_cast<uint8_t>(channels[1]);
                                pixel[3] = static_cast<uint8_t>(channels[2]);
                            }
                            else
                            {
                                const uint32_t word = encoding == VideoFrameEncoding::R210 ?
                                    (static_cast<uint32_t>(channels[0]) << 20) |
                                    (static_cast<uint32_t>(channels[1]) << 10) |
                                    channels[2] :
                                    (static_cast<uint32_t>(channels[0]) << 22) |
                                    (static_cast<uint32_t>(channels[1]) << 12) |
                                    (static_cast<uint32_t>(channels[2]) << 2);
                                if (encoding == VideoFrameEncoding::R10l)
                                    WriteLe32(pixel, word);
                                else
                                {
                                    pixel[0] = static_cast<uint8_t>(word >> 24);
                                    pixel[1] = static_cast<uint8_t>(word >> 16);
                                    pixel[2] = static_cast<uint8_t>(word >> 8);
                                    pixel[3] = static_cast<uint8_t>(word);
                                }
                            }
                        }
                    AnalysisLumaSource baseline;
                    baseline.data = pixels->data();
                    baseline.dataBytes = pixels->size();
                    baseline.width = width;
                    baseline.height = height;
                    baseline.rowBytes = width * 4;
                    baseline.format = AnalysisLumaFormat::NativeRgb;
                    baseline.encoding = encoding;
                    baseline.colorspace = colorspace;
                    baseline.generation = 7;
                    AnalysisFrameInput input;
                    input.nativeData = pixels->data();
                    input.nativeBytes = pixels->size();
                    input.nativeRowBytes = width * 4;
                    input.nativeOwner = pixels;
                    input.width = width;
                    input.height = height;
                    input.encoding = encoding;
                    input.colorspace = colorspace;
                    input.sourceGeneration = 7;
                    input.profile = AnalysisSourceProfile::QueuedCapture;
                    auto queued = PrepareAnalysisFrame(input);
                    input.profile = AnalysisSourceProfile::LiveRenderer;
                    // A converted upload must not switch the source used by
                    // live analysis when capture pixels have an adapter.
                    input.rendererUsesNative = false;
                    auto live = PrepareAnalysisFrame(input);
                    Assert::IsTrue(queued.IsValid());
                    Assert::IsTrue(live.IsValid());
                    Assert::AreEqual(static_cast<int>(AnalysisFrameDomain::NativeRgb),
                        static_cast<int>(queued.domain));
                    Assert::AreEqual(queued.domainKey, live.domainKey);
                    Assert::AreEqual(static_cast<int>(AnalysisLumaFormat::NativeRgb),
                        static_cast<int>(queued.source.format));
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x)
                        {
                            AnalysisLumaSample expected, actual, liveActual;
                            Assert::IsTrue(baseline.Sample(x, y, expected));
                            Assert::IsTrue(queued.source.Sample(x, y, actual));
                            Assert::IsTrue(live.source.Sample(x, y, liveActual));
                            Assert::AreEqual(static_cast<int>(expected.luma),
                                static_cast<int>(actual.luma));
                            Assert::AreEqual(static_cast<int>(expected.chromaU),
                                static_cast<int>(actual.chromaU));
                            Assert::AreEqual(static_cast<int>(expected.chromaV),
                                static_cast<int>(actual.chromaV));
                            Assert::AreEqual(static_cast<int>(expected.luma),
                                static_cast<int>(liveActual.luma));
                            Assert::AreEqual(static_cast<int>(expected.chromaU),
                                static_cast<int>(liveActual.chromaU));
                            Assert::AreEqual(static_cast<int>(expected.chromaV),
                                static_cast<int>(liveActual.chromaV));
                        }
                    AnalysisLumaSample black, headroom, chromatic;
                    Assert::IsTrue(queued.source.Sample(0, 0, black));
                    Assert::IsTrue(queued.source.Sample(1, 0, headroom));
                    Assert::IsTrue(queued.source.Sample(2, 0, chromatic));
                    Assert::IsTrue(black.luma < 8);
                    Assert::IsTrue(headroom.luma > 1000);
                    Assert::IsTrue(chromatic.chromaV > chromatic.chromaU);
                    auto detached = queued.source;
                    std::weak_ptr<std::vector<uint8_t>> lifetime = pixels;
                    live = {};
                    queued = {};
                    input.nativeOwner.reset();
                    pixels.reset();
                    Assert::IsFalse(lifetime.expired());
                    Assert::IsTrue(detached.Sample(3, 1, chromatic));
                    detached = {};
                    Assert::IsTrue(lifetime.expired());
                }
        }

        TEST_METHOD(PackedTwelveBitRgbUsesOneFullRangeNativeAnalysisDomain)
        {
            // DisplayMode enforces at least 100x100 even though the packed
            // R12 layout itself is testable with a single 8x2 block.
            constexpr int width = 104;
            constexpr int height = 100;
            constexpr size_t rowBytes = static_cast<size_t>(width) * 36 / 8;
            for (const auto encoding : { VideoFrameEncoding::R12B,
                VideoFrameEncoding::R12L })
            {
                auto pixels = MakeR12Pixels(encoding);
                VideoStateComPtr state = new VideoState();
                state->valid = true;
                state->displayMode = std::make_shared<DisplayMode>(
                    width, height, false, 24000, 1000);
                state->videoFrameEncoding = encoding;
                state->colorspace = ColorSpace::REC_709;

                CDeckLinkRGBToP010VideoFrameFormatter formatter;
                formatter.SetConversionMethod(
                    CDeckLinkRGBToP010VideoFrameFormatter::ConversionMethod::SCALAR);
                formatter.OnVideoState(state);
                auto converted = std::make_shared<std::vector<uint8_t>>(
                    static_cast<size_t>(formatter.GetOutFrameSize()), 0);
                VideoFrame frame(pixels->data(), 1, 0, nullptr);
                Assert::IsTrue(formatter.FormatVideoFrame(frame, converted->data()));

                AnalysisLumaSource baseline;
                baseline.data = pixels->data();
                baseline.dataBytes = pixels->size();
                baseline.width = width;
                baseline.height = height;
                baseline.rowBytes = rowBytes;
                baseline.format = AnalysisLumaFormat::NativeRgb;
                baseline.encoding = encoding;
                baseline.colorspace = ColorSpace::REC_709;
                baseline.generation = 59;

                AnalysisFrameInput input;
                input.nativeData = pixels->data();
                input.nativeBytes = pixels->size();
                input.nativeRowBytes = rowBytes;
                input.nativeOwner = pixels;
                input.convertedData = converted->data();
                input.convertedBytes = converted->size();
                input.convertedRowBytes = width * 2;
                input.convertedChromaRowBytes = width * 2;
                input.convertedOwner = converted;
                input.convertedContract = formatter.GetOutputContract();
                input.width = width;
                input.height = height;
                input.encoding = encoding;
                input.colorspace = ColorSpace::REC_709;
                input.transferKey = 7;
                input.sourceGeneration = 59;

                AnalysisLumaSource p010;
                p010.data = converted->data();
                p010.dataBytes = converted->size();
                p010.width = width;
                p010.height = height;
                p010.rowBytes = width * 2;
                p010.chromaRowBytes = width * 2;
                p010.format = AnalysisLumaFormat::P010;
                p010.encoding = encoding;
                p010.colorspace = ColorSpace::REC_709;
                p010.generation = 59;

                uint64_t canonicalKey = 0;
                for (const auto profile : { AnalysisSourceProfile::LiveRenderer,
                    AnalysisSourceProfile::QueuedCapture })
                    for (const bool rendererUsesNative : { false, true })
                        {
                            input.profile = profile;
                            input.rendererUsesNative = rendererUsesNative;
                            const auto prepared = PrepareAnalysisFrame(input);
                            Assert::IsTrue(prepared.IsValid());
                            Assert::IsFalse(prepared.usesConverted);
                            Assert::AreEqual(static_cast<int>(AnalysisFrameDomain::NativeRgb),
                                static_cast<int>(prepared.domain));
                            Assert::AreEqual(12, static_cast<int>(prepared.colorDepth));
                            Assert::AreEqual(0, static_cast<int>(prepared.bitShift));
                            Assert::AreEqual(static_cast<int>(VideoFrameSampleRange::FULL),
                                static_cast<int>(prepared.sampleRange));
                            if (canonicalKey == 0)
                                canonicalKey = prepared.domainKey;
                            else
                                Assert::AreEqual(canonicalKey, prepared.domainKey);
                            for (int y = 0; y < height; ++y)
                                for (int x = 0; x < width; ++x)
                                {
                                    AnalysisLumaSample expected, actual;
                                    Assert::IsTrue(baseline.Sample(x, y, expected));
                                    Assert::IsTrue(prepared.source.Sample(x, y, actual));
                                    Assert::AreEqual(static_cast<int>(expected.luma),
                                        static_cast<int>(actual.luma));
                                    Assert::AreEqual(static_cast<int>(expected.chromaU),
                                        static_cast<int>(actual.chromaU));
                                    Assert::AreEqual(static_cast<int>(expected.chromaV),
                                        static_cast<int>(actual.chromaV));
                                }
                        }

                bool foundChromaFootprintDifference = false;
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        AnalysisLumaSample nativeSample, convertedSample;
                        Assert::IsTrue(baseline.Sample(x, y, nativeSample));
                        Assert::IsTrue(p010.Sample(x, y, convertedSample));
                        Assert::AreEqual(static_cast<int>(nativeSample.luma),
                            static_cast<int>(convertedSample.luma));
                        foundChromaFootprintDifference =
                            foundChromaFootprintDifference ||
                            nativeSample.chromaU != convertedSample.chromaU ||
                            nativeSample.chromaV != convertedSample.chromaV;
                    }
                Assert::IsTrue(foundChromaFootprintDifference,
                    L"P010 chroma averaging must not silently replace per-pixel packed RGB evidence.");
                AnalysisLumaSample nativeRed, convertedRed;
                Assert::IsTrue(baseline.Sample(2, 0, nativeRed));
                Assert::IsTrue(p010.Sample(2, 0, convertedRed));
                Assert::AreEqual(217, static_cast<int>(nativeRed.luma));
                Assert::AreEqual(395, static_cast<int>(nativeRed.chromaU));
                Assert::AreEqual(1023, static_cast<int>(nativeRed.chromaV));
                Assert::AreEqual(217, static_cast<int>(convertedRed.luma));
                Assert::AreEqual(611, static_cast<int>(convertedRed.chromaU));
                Assert::AreEqual(628, static_cast<int>(convertedRed.chromaV));
            }
        }

        TEST_METHOD(DecodedP210FixtureLogsDirectDetectorAndActivePictureCosts)
        {
            constexpr int width = 3840;
            constexpr int height = 2160;
            char* fixturePathValue = nullptr;
            size_t fixturePathLength = 0;
            if (_dupenv_s(&fixturePathValue, &fixturePathLength,
                    "VP_ANALYSIS_P210_FIXTURE") != 0 ||
                fixturePathValue == nullptr || fixturePathLength <= 1)
            {
                std::free(fixturePathValue);
                Logger::WriteMessage(
                    L"Set VP_ANALYSIS_P210_FIXTURE to run the decoded P210 timing diagnostic.");
                return;
            }
            const std::string fixturePath(fixturePathValue);
            std::free(fixturePathValue);
            std::ifstream inputFile(fixturePath, std::ios::binary);
            if (!inputFile)
            {
                Assert::Fail(L"VP_ANALYSIS_P210_FIXTURE does not point to a readable decoded P210 fixture.");
                return;
            }
            const size_t expectedBytes = static_cast<size_t>(width) * height * 4;
            auto pixels = std::make_shared<std::vector<uint8_t>>(expectedBytes);
            inputFile.read(reinterpret_cast<char*>(pixels->data()),
                static_cast<std::streamsize>(pixels->size()));
            Assert::AreEqual(static_cast<std::streamsize>(expectedBytes),
                inputFile.gcount());

            AnalysisLumaSource direct;
            direct.data = pixels->data();
            direct.dataBytes = pixels->size();
            direct.width = width;
            direct.height = height;
            direct.rowBytes = static_cast<size_t>(width) * 2;
            direct.chromaRowBytes = static_cast<size_t>(width) * 2;
            direct.format = AnalysisLumaFormat::P210;
            direct.generation = 1;
            Assert::IsTrue(direct.IsValid());

            // A renderer-only P210 buffer is not a substitute for the
            // captured V210 source when native analysis pixels are missing.
            AnalysisFrameInput convertedOnly;
            convertedOnly.convertedData = pixels->data();
            convertedOnly.convertedBytes = pixels->size();
            convertedOnly.convertedRowBytes = direct.rowBytes;
            convertedOnly.convertedChromaRowBytes = direct.chromaRowBytes;
            convertedOnly.convertedOwner = pixels;
            convertedOnly.convertedContract = {
                VideoFrameSampleRange::LIMITED, 10, 6 };
            convertedOnly.width = width;
            convertedOnly.height = height;
            convertedOnly.encoding = VideoFrameEncoding::V210;
            convertedOnly.colorspace = ColorSpace::REC_709;
            convertedOnly.sourceGeneration = 1;
            Assert::IsFalse(PrepareAnalysisFrame(convertedOnly).IsValid());

            std::array<double, 3> directSubtitleMs = {};
            std::array<double, 3> directActivePictureMs = {};
            SubtitleBoxResult lastSubtitle;
            for (size_t run = 0; run < directSubtitleMs.size(); ++run)
            {
                auto measureSubtitle = [&](double& elapsed)
                {
                    SubtitleBoxDetector detector;
                    const auto begin = std::chrono::steady_clock::now();
                    auto result = detector.Analyze(direct, 276, 1884, 1, 1);
                    elapsed = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - begin).count();
                    return result;
                };
                lastSubtitle = measureSubtitle(directSubtitleMs[run]);

                auto measurePicture = [&](double& elapsed)
                {
                    const auto begin = std::chrono::steady_clock::now();
                    auto result = ExtractActivePictureEvidence(direct);
                    elapsed = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - begin).count();
                    return result;
                };
                (void)measurePicture(directActivePictureMs[run]);
            }
            auto median = [](std::array<double, 3> values)
            {
                std::sort(values.begin(), values.end());
                return values[1];
            };
            std::wstringstream report;
            report << std::fixed << std::setprecision(2)
                << L"Decoded P210 direct analysis (3840x2160, three-run median): SubtitleBoxDetector="
                << median(directSubtitleMs) << L" ms; sparse active-picture extraction="
                << median(directActivePictureMs) << L" ms; detector lines="
                << lastSubtitle.lineCount
                << L"; converted-only renderer storage is rejected as prepared capture evidence.";
            Logger::WriteMessage(report.str().c_str());
        }
    };
}
