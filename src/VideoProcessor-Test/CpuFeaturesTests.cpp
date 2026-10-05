#include "pch.h"
#include "CppUnitTest.h"
#include <VideoFrame.h>
#include <VideoState.h>
#include <CpuFeatures.h>
#include <video_frame_formatter/CARGBtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CDeckLinkRGBToP010VideoFrameFormatter.h>
#include <video_frame_formatter/CR210toRGB48VideoFrameFormatter.h>
#include <video_frame_formatter/CR12BtoRGB48VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP210VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP210VideoFrameFormatter.h>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    namespace
    {
        CpuFeatures::Snapshot SupportedCpu()
        {
            return {7, CpuFeatures::RequiredLeaf1, CpuFeatures::RequiredLeaf7, 6};
        }

        template<class Formatter>
        void CompareModes(VideoFrameEncoding encoding, unsigned width, unsigned height,
            typename Formatter::ConversionMethod scalarMethod,
            typename Formatter::ConversionMethod requestedMethod)
        {
            VideoStateComPtr state = new VideoState();
            state->valid = true;
            state->displayMode = std::make_shared<DisplayMode>(width, height, false, 60000, 1001);
            state->videoFrameEncoding = encoding;
            state->colorspace = ColorSpace::BT_2020;
            Formatter scalar, candidate;
            scalar.SetConversionMethod(scalarMethod);
            candidate.SetConversionMethod(requestedMethod);
            scalar.OnVideoState(state);
            candidate.OnVideoState(state);
            std::vector<BYTE> input(state->BytesPerFrame());
            uint32_t random = 0x12345678;
            for (auto& byte : input)
            {
                random = random * 1664525u + 1013904223u;
                byte = static_cast<BYTE>(random >> 24);
            }
            std::vector<BYTE> expected(scalar.GetOutFrameSize(), 0xaa);
            std::vector<BYTE> actual(candidate.GetOutFrameSize(), 0x55);
            VideoFrame frame(input.data(), 1, 0, nullptr);
            Assert::IsTrue(scalar.FormatVideoFrame(frame, expected.data()));
            Assert::IsTrue(candidate.FormatVideoFrame(frame, actual.data()));
            Assert::IsTrue(expected == actual, L"Automatic/explicit SIMD must preserve scalar output, including fallback");
        }

        template<class Formatter>
        void CompareAllModes(VideoFrameEncoding encoding, unsigned width, unsigned height)
        {
            CompareModes<Formatter>(encoding, width, height,
                Formatter::ConversionMethod::SCALAR, Formatter::ConversionMethod::AUTO);
            CompareModes<Formatter>(encoding, width, height,
                Formatter::ConversionMethod::SCALAR, Formatter::ConversionMethod::AVX2);
        }
    }

    TEST_CLASS(CpuFeaturesTests)
    {
    public:
        TEST_METHOD(XgetbvRequiresBothCpuAndOsXsave)
        {
            Assert::IsFalse(CpuFeatures::CanReadXcr0(0));
            Assert::IsFalse(CpuFeatures::CanReadXcr0(1u << 26));
            Assert::IsFalse(CpuFeatures::CanReadXcr0(1u << 27));
            Assert::IsTrue(CpuFeatures::CanReadXcr0((1u << 26) | (1u << 27)));
        }
        TEST_METHOD(DiagnosticsAgreeWithDispatch)
        {
            const auto& d = CpuFeatures::GetDiagnostics();
            Assert::IsTrue(d.xgetbvRead == CpuFeatures::CanReadXcr0(d.cpu.leaf1Ecx));
            Assert::IsTrue(CpuFeatures::SupportsAvx2Kernels() ==
                (!d.disabledByEnvironment && CpuFeatures::CanUseAvx2Kernels(d.cpu)));
        }
        TEST_METHOD(InstructionProbeExecutesOrReportsUnsupported)
        {
            const auto result = CpuFeatures::RunAvx2Probe();
            const auto& cpu = CpuFeatures::GetDiagnostics().cpu;
            const bool instructionAvailable = (cpu.leaf7Ebx & (1u << 5)) && (cpu.xcr0 & 6) == 6;
            Assert::IsTrue(result == (instructionAvailable ? CpuFeatures::ProbeResult::Pass :
                CpuFeatures::ProbeResult::Unsupported));
        }
        TEST_METHOD(Avx2TargetAcceptsCompleteCpuAndOsState)
        {
            Assert::IsTrue(CpuFeatures::CanUseAvx2Kernels(SupportedCpu()));
            auto cpu = SupportedCpu();
            cpu.maxLeaf = 32; cpu.xcr0 |= 0xe0;
            Assert::IsTrue(CpuFeatures::CanUseAvx2Kernels(cpu));
        }
        TEST_METHOD(SandyBridgeAvxDoesNotEnableAvx2)
        {
            const CpuFeatures::Snapshot sandyBridge = {0x0d, (1u<<26)|(1u<<27)|(1u<<28), 0, 7};
            Assert::IsFalse(CpuFeatures::CanUseAvx2Kernels(sandyBridge));
            auto cpu = SupportedCpu(); cpu.maxLeaf = 6;
            Assert::IsFalse(CpuFeatures::CanUseAvx2Kernels(cpu));
        }
        TEST_METHOD(Avx2RequiresXsaveOsxsaveAvxAndFma)
        {
            for (const auto bit : {12, 26, 27, 28})
            {
                auto cpu = SupportedCpu(); cpu.leaf1Ecx &= ~(1u << bit);
                Assert::IsFalse(CpuFeatures::CanUseAvx2Kernels(cpu));
            }
        }
        TEST_METHOD(Avx2RequiresBothXmmAndYmmOsState)
        {
            for (const uint64_t state : {0ull, 1ull, 2ull, 4ull, 0xe0ull})
            {
                auto cpu = SupportedCpu(); cpu.xcr0 = state;
                Assert::IsFalse(CpuFeatures::CanUseAvx2Kernels(cpu));
            }
        }
        TEST_METHOD(Avx2CompilerTargetRequiresAvx2AndBmi)
        {
            for (const auto bit : {3, 5, 8})
            {
                auto cpu = SupportedCpu(); cpu.leaf7Ebx &= ~(1u << bit);
                Assert::IsFalse(CpuFeatures::CanUseAvx2Kernels(cpu));
            }
        }
        TEST_METHOD(DiagnosticDisableCannotAdvertiseAvx2)
        {
            wchar_t disabled[2] = {};
            const bool forced = GetEnvironmentVariableW(L"VP_DISABLE_AVX2", disabled, 2) == 1 && disabled[0] == L'1';
            if (forced)
                Assert::IsFalse(CpuFeatures::SupportsAvx2Kernels());
            Logger::WriteMessage(CpuFeatures::SupportsAvx2Kernels() ? "AVX2 kernels enabled" : "Baseline fallback enabled");
        }
        TEST_METHOD(AllConvertersAutomaticAndExplicitSimdMatchScalar)
        {
            // Run this test in separate normal and VP_DISABLE_AVX2=1 processes.
            // Cover vector tails, the v210 helper threshold, and RGB helper work.
            for (const auto size : {std::make_pair(128u, 100u), std::make_pair(130u, 722u), std::make_pair(1920u, 1080u)})
            {
                const auto w = size.first, h = size.second;
                CompareAllModes<CARGBtoP010VideoFrameFormatter>(VideoFrameEncoding::ARGB_8BIT,w,h);
                CompareAllModes<CARGBtoP010VideoFrameFormatter>(VideoFrameEncoding::BGRA_8BIT,w,h);
                CompareAllModes<CR210toRGB48VideoFrameFormatter>(VideoFrameEncoding::R210,w,h);
                CompareAllModes<CR12BtoRGB48VideoFrameFormatter>(VideoFrameEncoding::R12B,(w+7)&~7u,h);
                for (const auto encoding : {VideoFrameEncoding::UYVY, VideoFrameEncoding::HDYC})
                {
                    CompareAllModes<CUYVYtoP010VideoFrameFormatter>(encoding,w,h);
                    CompareAllModes<CUYVYtoP210VideoFrameFormatter>(encoding,w,h);
                }
                CompareAllModes<CV210toP210VideoFrameFormatter>(VideoFrameEncoding::V210,w,h);
                for (const auto encoding : {VideoFrameEncoding::R210, VideoFrameEncoding::R10b, VideoFrameEncoding::R10l, VideoFrameEncoding::R12B, VideoFrameEncoding::R12L})
                    CompareAllModes<CDeckLinkRGBToP010VideoFrameFormatter>(encoding,(w+7)&~7u,h);
                using V210 = CV210toP010VideoFrameFormatter;
                for (const auto method : {V210::ConversionMethod::AUTO, V210::ConversionMethod::SIMD})
                    CompareModes<V210>(VideoFrameEncoding::V210,w,h,V210::ConversionMethod::STANDARD,method);
            }
        }
    };
}
