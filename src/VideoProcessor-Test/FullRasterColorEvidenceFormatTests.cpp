#include "pch.h"
#include <ActivePictureEvidence.h>
#include "CppUnitTest.h"
#include <chrono>
#include <sstream>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace VideoProcessorTest
{
namespace
{
void Put16(uint8_t* p, int value) { const unsigned v = value << 6; p[0] = v & 255; p[1] = v >> 8; }
void Put32(uint8_t* p, uint32_t value, bool big = false)
{
    for (int i = 0; i < 4; ++i) p[big ? 3 - i : i] = static_cast<uint8_t>(value >> (8 * i));
}
struct TestPlane
{
    int width, height;
    size_t pitch;
    std::vector<uint8_t> bytes;
    bool p010;
    TestPlane(int w, int h, bool subsample = false) : width(w), height(h), pitch(w * 2 + 32),
        bytes(pitch * (h + (subsample ? h / 2 : h)), 0), p010(subsample)
    {
        // Chroma is constant vertically: P010 and P210 represent this same signal exactly.
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                Put16(bytes.data() + pitch * y + x * 2, 84 + ((x * 17 + y * 13) % 11));
        for (int y = 0; y < (subsample ? h / 2 : h); ++y)
            for (int x = 0; x < w; x += 2)
            {
                Put16(bytes.data() + pitch * h + pitch * y + x * 2, 508 + (x / 2) % 3);
                Put16(bytes.data() + pitch * h + pitch * y + x * 2 + 2, 515);
            }
    }
    AnalysisLumaSource Source() const
    {
        return { bytes.data(), bytes.size(), width, height, pitch, pitch,
            p010 ? AnalysisLumaFormat::P010 : AnalysisLumaFormat::P210,
            VideoFrameEncoding::V210, ColorSpace::BT_2020, 1 };
    }
};
void EqualEvidence(const FullRasterColorEvidence& a, const FullRasterColorEvidence& b)
{
    Assert::AreEqual(a.evaluated, b.evaluated);
    Assert::AreEqual(a.candidateSupported, b.candidateSupported);
    Assert::AreEqual(a.precisionSupported, b.precisionSupported);
    Assert::AreEqual(a.sampleCount, b.sampleCount);
    for (int i = 0; i < 4; ++i)
    {
        const auto& x = a.edges[i]; const auto& y = b.edges[i];
        Assert::AreEqual(x.medianY, y.medianY);
        Assert::AreEqual(x.medianU, y.medianU);
        Assert::AreEqual(x.medianV, y.medianV);
        Assert::AreEqual(x.dispersionY, y.dispersionY);
        Assert::AreEqual(x.dispersionU, y.dispersionU);
        Assert::AreEqual(x.dispersionV, y.dispersionV);
        Assert::AreEqual(x.maxBackgroundDeltaY, y.maxBackgroundDeltaY);
        Assert::AreEqual(x.maxBackgroundDeltaUV, y.maxBackgroundDeltaUV);
        Assert::AreEqual(x.supportedCells, y.supportedCells);
        Assert::AreEqual(x.candidateSupported, y.candidateSupported);
    }
}
}
TEST_CLASS(FullRasterColorEvidenceFormatTests)
{
public:
    TEST_METHOD(NativeTenBitRgbIsSampledButEightBitRgbAbstains)
    {
        constexpr int w = 384, h = 216;
        std::vector<uint8_t> bytes(w * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const unsigned level = 80 + ((x / 2) * 17 + y * 13) % 11;
                Put32(bytes.data() + (y * w + x) * 4,
                    ((level + 8) << 20) | (level << 10) | level, true);
            }
        AnalysisLumaSource source = { bytes.data(), bytes.size(), w, h, w * 4, 0,
            AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::R210, ColorSpace::REC_709, 1 };
        const auto tenBit = EvaluateFullRasterColorEvidence(source);
        Assert::IsTrue(tenBit.evaluated);
        Assert::IsTrue(tenBit.precisionSupported);
        Assert::AreEqual<size_t>(4608, tenBit.sampleCount);
        // Pairwise-identical RGB permits exact 4:2:2 planar representation;
        // compare every statistic with the native RGB sampler result.
        TestPlane planar(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                AnalysisLumaSample pixel;
                Assert::IsTrue(source.Sample(x, y, pixel));
                Put16(planar.bytes.data() + planar.pitch * y + x * 2, pixel.luma);
                if ((x & 1) == 0)
                {
                    Put16(planar.bytes.data() + planar.pitch * h + planar.pitch * y + x * 2, pixel.chromaU);
                    Put16(planar.bytes.data() + planar.pitch * h + planar.pitch * y + x * 2 + 2, pixel.chromaV);
                }
            }
        EqualEvidence(tenBit, EvaluateFullRasterColorEvidence(planar.Source()));
        source.encoding = VideoFrameEncoding::BGRA_8BIT;
        const auto eightBit = EvaluateFullRasterColorEvidence(source);
        Assert::IsFalse(eightBit.evaluated);
        Assert::IsFalse(eightBit.candidateSupported);
        Assert::AreEqual<size_t>(0, eightBit.sampleCount);
    }
    TEST_METHOD(EightBitPackedAndConvertedYuvDoNotAcquireFalsePrecision)
    {
        TestPlane frame(384, 216, true);
        auto source = frame.Source();
        source.encoding = VideoFrameEncoding::UYVY;
        const auto converted = EvaluateFullRasterColorEvidence(source);
        Assert::IsFalse(converted.evaluated);
        Assert::AreEqual<size_t>(0, converted.sampleCount);
        source.format = AnalysisLumaFormat::NativeYuv422;
        source.chromaRowBytes = 0;
        const auto native = EvaluateFullRasterColorEvidence(source);
        Assert::IsFalse(native.evaluated);
        Assert::AreEqual<size_t>(0, native.sampleCount);
    }
    TEST_METHOD(SamplingBudgetIsResolutionIndependentAndBenchmarkIsReported)
    {
        for (const int scale : { 1, 2 })
        {
            TestPlane frame(3840 * scale, 2160 * scale, true);
            const auto source = frame.Source();
            const auto measured = EvaluateFullRasterColorEvidence(source);
            Assert::IsTrue(measured.evaluated);
            Assert::AreEqual<size_t>(4608, measured.sampleCount);
            const auto started = std::chrono::steady_clock::now();
            size_t totalSamples = 0;
            for (int iteration = 0; iteration < 100; ++iteration)
                totalSamples += EvaluateFullRasterColorEvidence(source).sampleCount;
            const double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            Assert::AreEqual<size_t>(460800, totalSamples);
            std::ostringstream report;
            report << "Color evidence benchmark: " << source.width << 'x' << source.height
                << ", 100 iterations, " << elapsed << " ms total, " << elapsed / 100
                << " ms/inspection, 4608 source samples/inspection";
            Logger::WriteMessage(report.str().c_str());
        }
    }
};
}
