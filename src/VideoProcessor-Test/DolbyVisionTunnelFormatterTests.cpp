/*
 * Copyright(C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "pch.h"
#include "CppUnitTest.h"
#include <VideoFrame.h>
#include <VideoState.h>
#include <CpuFeatures.h>
#include <DolbyVisionTunnelInput.h>
#include <vprenderer/AlphaNativeRgbIngress.h>
#include <video_frame_formatter/CRGBDolbyVisionTunnelFormatter.h>
#include <algorithm>
#include "DolbyVisionTunnelTestSender.h"
#include <vprenderer/LibplaceboRendererPluginApi.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>
#include <array>
#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    namespace
    {
        using namespace DolbyVisionTestSender;
        using Formatter = CRGBDolbyVisionTunnelFormatter;

        void __cdecl TunnelRendererLog(const char*) {}
        struct TunnelRendererCallback : IRendererCallback
        {
            std::atomic<RendererState> state{ RENDERSTATE_UNKNOWN };
            void OnRendererState(RendererState next, uint32_t) override { state = next; }
            void OnRendererDetailString(const CString&, uint32_t) override {}
        };
        // Pixels are owned by SenderPicture until the renderer is stopped.
        struct BorrowedTunnelBuffer : IUnknown
        {
            HRESULT QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
            ULONG AddRef() override { return 1; }
            ULONG Release() override { return 0; }
        };



    }

    TEST_CLASS(DolbyVisionTunnelFormatterTests)
    {
    public:


        TEST_METHOD(RealRendererDecodesRgb8WithNativeUploadEnabled)
        {
            // The native-RGB shortcut used to skip formatter creation entirely.
            // Exercise the real renderer DLL, conversion worker, upload and DM
            // status publication with conversion NONE, for both packed layouts.
            HMODULE module = nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&TunnelRendererLog), &module) != FALSE);
            wchar_t modulePath[MAX_PATH] = {};
            GetModuleFileNameW(module, modulePath, MAX_PATH);
            std::wstring path(modulePath);
            path.resize(path.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            path += L"\\..\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll";
#else
            path += L"\\vprenderer\\VideoProcessorVPRenderer.dll";
#endif
            HMODULE plugin = LoadLibraryExW(path.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin, L"Build both Release renderer DLLs before running the tunnel integration test");
            struct PluginCleanup { HMODULE plugin; ~PluginCleanup(){ FreeLibrary(plugin); } } pluginCleanup{plugin};
            const auto version = reinterpret_cast<VideoProcessorLibplaceboGetApiVersionFn>(
                GetProcAddress(plugin, VP_LIBPLACEBO_VERSION_EXPORT));
            const auto create = reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(
                GetProcAddress(plugin, VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy = reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(
                GetProcAddress(plugin, VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(version && create && destroy);
            Assert::AreEqual(VP_LIBPLACEBO_PLUGIN_API_VERSION, version());
            for (const auto encoding : { VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
            {
                char temporary[MAX_PATH] = {}, configPath[MAX_PATH] = {};
                Assert::IsTrue(GetTempPathA(MAX_PATH, temporary) != 0);
                Assert::IsTrue(GetTempFileNameA(temporary, "dvt", 0, configPath) != 0);
                std::ofstream(configPath) << "[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                    "[vprenderer]\nquality: fast\n";
                TunnelRendererCallback callback;
                BorrowedTunnelBuffer buffer;
                SenderPicture picture(false, 130, 128, encoding);
                picture.state->eotf = EOTF::PQ; picture.state->colorspace = ColorSpace::BT_2020;
                picture.state->tunnelSdrRgbRange = TunnelSdrRgbRange::Limited;
                picture.Embed(Packetize(DmBody()));
                struct Cleanup
                {
                    HWND window; IVideoRenderer* renderer; VideoProcessorLibplaceboDestroyRendererFn destroy;
                    const char* config;
                    ~Cleanup(){ if(renderer){ renderer->Stop(); destroy(renderer); }
                        if(window) DestroyWindow(window); DeleteFileA(config); }
                } cleanup{CreateWindowExW(0, L"STATIC", L"DV RGB fixture", WS_POPUP,
                    0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr), nullptr, destroy, configPath};
                Assert::IsNotNull(cleanup.window);
                cleanup.renderer = create(&callback, 1, cleanup.window, nullptr, false, 1,
                    VideoConversionOverride::VIDEOCONVERSION_NONE, configPath, TunnelRendererLog);
                Assert::IsNotNull(cleanup.renderer);
                Assert::IsTrue(cleanup.renderer->OnVideoState(picture.state));
                cleanup.renderer->Build(); cleanup.renderer->Start();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                uint64_t sequence = 1;
                bool trusted = false;
                while (std::chrono::steady_clock::now() < deadline && callback.state != RENDERSTATE_FAILED)
                {
                    MSG message;
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                    { TranslateMessage(&message); DispatchMessageW(&message); }
                    if (callback.state == RENDERSTATE_RENDERING)
                    {
                        VideoFrame frame(picture.input.data(), sequence++, 0, &buffer);
                        cleanup.renderer->OnVideoFrame(frame);
                        DolbyVisionLuminanceStatus status;
                        CString ingress;
                        trusted = cleanup.renderer->HasPresentedLiveFrame() &&
                            cleanup.renderer->GetDolbyVisionLuminanceStatus(status) &&
                            status.tunnelActive && status.available && status.sceneAvailable &&
                            cleanup.renderer->GetVideoIngressInfo(ingress) &&
                            ingress.Find(L"Dolby Vision RGB tunnel (12-bit 4:2:2)") >= 0;
                        if (trusted) break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                }
                Assert::IsTrue(trusted, L"RGB8 tunnel never reached decoded upload and DM status publication");
            }
        }


        TEST_METHOD(UnknownWireRangePermitsValidatedDvButNeverGuessesSdr)
        {
            SenderPicture picture(false, 130, 128, VideoFrameEncoding::ARGB_8BIT);
            picture.state->tunnelSdrRgbRange = TunnelSdrRgbRange::Unknown;
            Formatter formatter; formatter.OnVideoState(picture.state);
            Assert::IsFalse(Deliver(formatter, picture, 1).allowSdrFallback);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 2), 3072);
            picture.Embed(Packetize({}, 3, 3, true, true));
            Deliver(formatter, picture, 3);
            std::fill(picture.input.begin(), picture.input.end(), 0);
            const auto unknown = Deliver(formatter, picture, 4);
            Assert::IsFalse(unknown.valid || unknown.allowSdrFallback);
        }

        TEST_METHOD(Rgb8ScalarAndAvx2PreserveEveryTwelveBitCodeAndIgnoreAlpha)
        {
            for (const auto encoding : { VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
            for (const uint32_t width : { 130U, 1922U })
            {
                // Odd heights and non-SIMD tails exercise both serial and worker rows.
                SenderPicture picture(false, width, width == 130 ? 129 : 1081, encoding);
                picture.Embed(Packetize(DmBody(2048, false, 157)));
                for (const auto method : { Formatter::ConversionMethod::SCALAR, Formatter::ConversionMethod::AVX2 })
                {
                    if (method == Formatter::ConversionMethod::AVX2 && !CpuFeatures::SupportsAvx2Kernels())
                        continue;
                    Formatter formatter;
                    formatter.SetConversionMethod(method);
                    formatter.OnVideoState(picture.state);
                    std::vector<BYTE> output;
                    const auto metadata = Deliver(formatter, picture, 1, &output);
                    AssertPeak(metadata, 2048);
                    Assert::IsFalse(metadata.fullRange);
                    AssertExactSamples(picture, output);
                    const auto contract = formatter.GetOutputContract();
                    Assert::AreEqual(12U, unsigned(contract.colorDepth));
                    Assert::AreEqual(4U, unsigned(contract.bitShift));
                    Assert::IsTrue(contract.sampleRange == VideoFrameSampleRange::LIMITED);
                    const size_t alpha = encoding == VideoFrameEncoding::ARGB_8BIT ? 0 : 3;
                    for (size_t i = alpha; i < picture.input.size(); i += 4)
                        picture.input[i] ^= 0xFF;
                    AssertPeak(Deliver(formatter, picture, 2, &output), 2048);
                    AssertExactSamples(picture, output);
                }
            }
        }

        TEST_METHOD(Rgb8PacketRedundancyContinuationAndTransitionRecovery)
        {
            for (const auto encoding : { VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
            {
                SenderPicture picture(false, 130, 128, encoding);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                auto sdr = Deliver(formatter, picture, 1);
                Assert::IsFalse(sdr.valid || sdr.requiresBlackFrame);
                Assert::IsTrue(sdr.allowSdrFallback);
                Assert::AreEqual(2U, unsigned(sdr.rgbBitAlignment));
                picture.Embed(Packetize(DmBody(), 1, 1));
                picture.CorruptCopy(0, 0); picture.CorruptCopy(0, 1);
                AssertPeak(Deliver(formatter, picture, 2), 3072);
                picture.Embed(Packetize(DmBody(2048, true, 157), 1, 1));
                picture.CorruptAllCopies(1);
                AssertPeak(Deliver(formatter, picture, 3), 3072);
                picture.CorruptAllCopies(0);
                const auto lost = Deliver(formatter, picture, 4);
                Assert::IsTrue(lost.requiresBlackFrame);
                Assert::IsFalse(lost.allowSdrFallback);
                picture.Embed(Packetize({}, 1, 1, true));
                Assert::IsFalse(Deliver(formatter, picture, 5).requiresBlackFrame);
                picture.Embed(Packetize(DmBody(2048), 1, 2));
                AssertPeak(Deliver(formatter, picture, 6), 3072);
                picture.Embed(Packetize({}, 2, 2, true));
                AssertPeak(Deliver(formatter, picture, 7), 2048);
                picture.Embed(Packetize({}, 2, 2, true, true));
                AssertPeak(Deliver(formatter, picture, 8), 2048);
                std::fill(picture.input.begin(), picture.input.end(), 0);
                const auto menu = Deliver(formatter, picture, 10);
                Assert::IsFalse(menu.valid || menu.requiresBlackFrame);
                Assert::IsTrue(menu.allowSdrFallback);
                Assert::AreEqual(2U, unsigned(menu.rgbBitAlignment));
                for (size_t pixel = 0; pixel < picture.intensity.size(); ++pixel)
                    picture.Pack(pixel);
                picture.Embed(Packetize(DmBody(1024), 3, 3));
                std::vector<BYTE> output;
                AssertPeak(Deliver(formatter, picture, 11, &output), 1024);
                AssertExactSamples(picture, output);
            }
        }

        TEST_METHOD(Rgb8RejectsInvalidHeaderAndBlanksAfterMetadataLossLimit)
        {
            for (const auto encoding : { VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
            {
                SenderPicture picture(false, 130, 128, encoding);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                auto packets = Packetize(DmBody());
                packets[0][2] |= 2; FinishPacket(packets[0]);
                picture.Embed(packets);
                Assert::IsFalse(Deliver(formatter, picture, 1).valid);
                picture.Embed(Packetize(DmBody()));
                AssertPeak(Deliver(formatter, picture, 2), 3072);
                picture.CorruptAllCopies();
                for (uint64_t counter = 3; counter < 27; ++counter)
                {
                    const auto dm = Deliver(formatter, picture, counter);
                    Assert::IsTrue(dm.requiresBlackFrame);
                    Assert::IsFalse(dm.allowSdrFallback);
                }
                std::vector<BYTE> output;
                const auto expired = Deliver(formatter, picture, 27, &output);
                Assert::IsFalse(expired.valid || expired.allowSdrFallback);
                Assert::IsTrue(std::all_of(output.begin(), output.end(), [](BYTE b){ return b == 0; }));
                picture.Embed(Packetize(DmBody(2048)));
                AssertPeak(Deliver(formatter, picture, 28), 2048);
            }
        }

        TEST_METHOD(LayoutChangeResetsDolbyLockAndCannotReuseOldDm)
        {
            Formatter formatter;
            for (const auto encoding : { VideoFrameEncoding::R210, VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT })
            {
                SenderPicture picture(false, 130, 128, encoding);
                formatter.OnVideoState(picture.state);
                Assert::IsFalse(Deliver(formatter, picture, 1).valid);
                picture.Embed(Packetize({}, 3, 3, true));
                const auto absent = Deliver(formatter, picture, 2);
                Assert::IsFalse(absent.valid || absent.allowSdrFallback);
                Assert::IsTrue(absent.requiresBlackFrame);
                picture.Embed(Packetize(DmBody()));
                AssertPeak(Deliver(formatter, picture, 3), 3072);
            }
        }

        TEST_METHOD(TunnelAdmissionRequiresOptInRendererSdrAndPreservedSupportedRgb)
        {
            VideoStateComPtr state = new VideoState();
            state->valid = true; state->eotf = EOTF::SDR;
            for (const auto encoding : { VideoFrameEncoding::ARGB_8BIT, VideoFrameEncoding::BGRA_8BIT, VideoFrameEncoding::R210 })
            {
                state->videoFrameEncoding = encoding;
                Assert::IsTrue(DolbyVisionTunnelCanEnable(true, true, *state));
                Assert::IsFalse(DolbyVisionTunnelCanEnable(false, true, *state));
                Assert::IsFalse(DolbyVisionTunnelCanEnable(true, false, *state));
                Assert::IsFalse(AlphaCanUseNativeRgbUpload(encoding, true));
                state->rgbTunnelBytesPreserved = false;
                Assert::IsFalse(DolbyVisionTunnelCanEnable(true, true, *state));
                VideoStateComPtr copied = new VideoState(*state);
                Assert::IsFalse(copied->rgbTunnelBytesPreserved);
                state->rgbTunnelBytesPreserved = true;
                for (const auto eotf : { EOTF::UNKNOWN, EOTF::HDR, EOTF::PQ, EOTF::HLG })
                {
                    state->eotf = eotf;
                    Assert::IsFalse(DolbyVisionTunnelCanEnable(true, true, *state));
                }
                state->eotf = EOTF::SDR; state->valid = false;
                Assert::IsFalse(DolbyVisionTunnelCanEnable(true, true, *state));
                state->valid = true;
            }
            Assert::IsTrue(AlphaCanUseNativeRgbUpload(VideoFrameEncoding::ARGB_8BIT));
            Assert::IsTrue(AlphaCanUseNativeRgbUpload(VideoFrameEncoding::BGRA_8BIT));
            for (const auto encoding : { VideoFrameEncoding::UNKNOWN, VideoFrameEncoding::V210, VideoFrameEncoding::UYVY,
                VideoFrameEncoding::HDYC, VideoFrameEncoding::R10b, VideoFrameEncoding::R10l,
                VideoFrameEncoding::R12B, VideoFrameEncoding::R12L, VideoFrameEncoding::H265 })
            {
                state->videoFrameEncoding = encoding;
                Assert::IsFalse(DolbyVisionTunnelCanEnable(true, true, *state));
                state->displayMode = std::make_shared<DisplayMode>(130, 128, false, 24, 1);
                state->dolbyVisionTunnel = true;
                Formatter formatter;
                Assert::ExpectException<std::runtime_error>([&] { formatter.OnVideoState(state); });
            }
        }

        TEST_METHOD(IndependentSenderHasKnownCrcAndPackingAnchors)
        {
            const uint8_t reference[] = { '1','2','3','4','5','6','7','8','9' };
            Assert::AreEqual(0x0376E6E7U, SenderCrc(reference, sizeof(reference)));
            const auto packets = Packetize(DmBody());
            // Fixed footer of the complete single-packet fixture, not a roundtrip assertion.
            const uint8_t footer[] = { 0xBD, 0x58, 0x9A, 0x44 };
            Assert::IsTrue(std::equal(footer, footer + 4, packets[0].data() + 124));
            Assert::AreEqual(0U, SenderCrc(packets[0].data(), packets[0].size()));
            SenderPicture picture(false);
            // Y=0xABC, C=0x123 => RGB8=(0x12,0xAB,0x3C), r210=0x0122AC3C.
            picture.intensity[3073] = 0xABC;
            picture.chroma[3073] = 0x123;
            picture.Pack(3073);
            const size_t at = (3073 / 130) * picture.state->BytesPerRow() + (3073 % 130) * 4;
            const uint8_t known[] = { 0x01, 0x22, 0xAC, 0x3C };
            Assert::IsTrue(std::equal(known, known + 4, picture.input.data() + at));
            picture.Embed(packets);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            std::vector<BYTE> output;
            AssertPeak(Deliver(formatter, picture, 1, &output), 3072);
            AssertExactSamples(picture, output);
        }

        TEST_METHOD(SinglePacketReadsSignedMatricesOffsetsAndRange)
        {
            for (const bool high8 : { false, true })
            {
                SenderPicture picture(high8);
                picture.Embed(Packetize(DmBody(3072, false)));
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                const auto dm = Deliver(formatter, picture, 1);
                AssertPeak(dm, 3072);
                Assert::IsFalse(dm.fullRange);
                Assert::AreEqual(-0.5f, dm.nonlinear[0][1]);
                Assert::AreEqual(0.25f, dm.nonlinear[0][2]);
                Assert::AreEqual(0.0625f, dm.nonlinearOffset[0]);
                Assert::AreEqual(0.5f, dm.nonlinearOffset[1]);
                Assert::AreEqual(1.0f, dm.linear[0][0]);
                Assert::AreEqual(0.25f, dm.linear[2][2]);
                Assert::AreEqual(62U, static_cast<unsigned>(dm.sourceMinPq));
                Assert::AreEqual(3696U, static_cast<unsigned>(dm.sourceMaxPq));
                Assert::AreEqual(high8 ? 1U : 2U, static_cast<unsigned>(dm.rgbBitAlignment));
                const auto contract = formatter.GetOutputContract();
                Assert::IsTrue(contract.sampleRange == VideoFrameSampleRange::LIMITED);
                Assert::AreEqual(12U, static_cast<unsigned>(contract.colorDepth));
                Assert::AreEqual(4U, static_cast<unsigned>(contract.bitShift));
            }
        }

        TEST_METHOD(AllTwelveBitCodesSurviveScalarAndAvx2WithPaddingAndTails)
        {
            for (const bool high8 : { false, true })
            {
                // DisplayMode requires both dimensions >=100. Cover 0/2/6 SIMD tail pixels.
                for (const unsigned width : { 128U, 130U, 134U })
                {
                    SenderPicture picture(high8, width, 100);
                    picture.Embed(Packetize(DmBody()));
                    for (const auto method : { Formatter::ConversionMethod::SCALAR,
                        Formatter::ConversionMethod::AVX2 })
                    {
                        if (method == Formatter::ConversionMethod::AVX2 &&
                            !CpuFeatures::SupportsAvx2Kernels())
                        {
                            Logger::WriteMessage(L"AVX2 qualification skipped: CPU/OS lacks AVX2 support" );
                            continue;
                        }
                        Formatter formatter;
                        formatter.SetConversionMethod(method);
                        formatter.OnVideoState(picture.state);
                        std::vector<BYTE> output;
                        AssertPeak(Deliver(formatter, picture, 1, &output), 3072);
                        AssertExactSamples(picture, output);
                    }
                }
            }
        }

        TEST_METHOD(ThreadedRowsAndUnevenTailPreserveEverySample)
        {
            for (const bool high8 : { false, true })
            {
                SenderPicture picture(high8, 1922, 1081);
                picture.Embed(Packetize(DmBody()));
                for (const auto method : { Formatter::ConversionMethod::SCALAR,
                    Formatter::ConversionMethod::AVX2 })
                {
                    if (method == Formatter::ConversionMethod::AVX2 &&
                        !CpuFeatures::SupportsAvx2Kernels())
                    {
                        Logger::WriteMessage(L"AVX2 qualification skipped: CPU/OS lacks AVX2 support" );
                        continue;
                    }
                    Formatter formatter;
                    formatter.SetConversionMethod(method);
                    formatter.OnVideoState(picture.state);
                    std::vector<BYTE> output;
                    AssertPeak(Deliver(formatter, picture, 1, &output), 3072);
                    AssertExactSamples(picture, output);
                }
            }
        }

        TEST_METHOD(AlignmentChangeWithNoMetadataUsesNewLayoutAndActiveDm)
        {
            SenderPicture high(true), low(false);
            Formatter formatter;
            formatter.OnVideoState(high.state);
            high.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, high, 1), 3072);
            low.Embed(Packetize({}, 3, 3, true));
            std::vector<BYTE> output;
            AssertPeak(Deliver(formatter, low, 2, &output), 3072);
            AssertExactSamples(low, output);
            high.Embed(Packetize({}, 3, 3, true));
            AssertPeak(Deliver(formatter, high, 3, &output), 3072);
            AssertExactSamples(high, output);
        }
        TEST_METHOD(ContinuationBoundariesAndUnknownBlocksPreserveL1)
        {
            // Body lengths 119, 120, 124, 244, 512. L1 crosses both first
            // and later packet boundaries; 512 is the ETSI five-packet example.
            for (const size_t unknown : { size_t(32), size_t(33), size_t(37),
                size_t(157), size_t(425) })
            {
                for (const bool high8 : { false, true })
                {
                    SenderPicture picture(high8);
                    picture.Embed(Packetize(DmBody(2048, true, unknown)));
                    Formatter formatter;
                    formatter.OnVideoState(picture.state);
                    const auto dm = Deliver(formatter, picture, 1);
                    AssertPeak(dm, 2048);
                    Assert::AreEqual(1024U, static_cast<unsigned>(dm.sceneAvgPq));
                }
            }
        }

        TEST_METHOD(RepeatedCopiesRecoverFirstAndContinuationCorruption)
        {
            for (const bool high8 : { false, true })
            {
                SenderPicture picture(high8);
                picture.Embed(Packetize(DmBody(2048, true, 157)));
                picture.CorruptCopy(0, 0);
                picture.CorruptCopy(0, 1);
                picture.CorruptCopy(1, 0);
                picture.CorruptCopy(2, 0);
                picture.CorruptCopy(2, 1);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                AssertPeak(Deliver(formatter, picture, 1), 2048);
            }
        }

        TEST_METHOD(DamagedContinuationNeverReplacesActiveMetadata)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 1), 3072);
            picture.Embed(Packetize(DmBody(2048, true, 157)));
            picture.CorruptAllCopies(1);
            AssertPeak(Deliver(formatter, picture, 2), 3072);
            picture.Embed(Packetize(DmBody(2048, true, 157)));
            AssertPeak(Deliver(formatter, picture, 3), 2048);
        }

        TEST_METHOD(MismatchedContinuationIdsFlagsAndKindsAreRejected)
        {
            for (const size_t field : { size_t(0), size_t(1), size_t(2) })
            {
                SenderPicture picture(true);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                picture.Embed(Packetize(DmBody()));
                AssertPeak(Deliver(formatter, picture, 1), 3072);
                auto packets = Packetize(DmBody(2048, true, 157));
                packets[1][field] ^= field == 0 ? 0x40 : 1;
                FinishPacket(packets[1]);
                picture.Embed(packets);
                AssertPeak(Deliver(formatter, picture, 2), 3072);
            }
        }

        TEST_METHOD(DeclaredLengthBoundsAndMalformedExtensionsDoNotReplaceDm)
        {
            for (const uint16_t declared : { uint16_t(70), uint16_t(120), uint16_t(0x2F01) })
            {
                SenderPicture picture(false);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                picture.Embed(Packetize(DmBody()));
                AssertPeak(Deliver(formatter, picture, 1), 3072);
                auto packets = Packetize(DmBody(2048));
                packets[0][3] = static_cast<uint8_t>(declared >> 8);
                packets[0][4] = static_cast<uint8_t>(declared);
                FinishPacket(packets[0]);
                picture.Embed(packets);
                AssertPeak(Deliver(formatter, picture, 2), 3072);
            }
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 1), 3072);
            auto bad = DmBody(2048);
            Put32(bad.data() + 71, 7); // six payload bytes available
            picture.Embed(Packetize(bad));
            AssertPeak(Deliver(formatter, picture, 2), 3072);
            bad = DmBody(4096); // L1 values must be 12-bit
            picture.Embed(Packetize(bad));
            AssertPeak(Deliver(formatter, picture, 3), 3072);
        }

        TEST_METHOD(DeclaredPacketCountCannotReadBeyondActiveRaster)
        {
            SenderPicture picture(false, 100, 100); // minimum mode; only three packets fit
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 1), 3072);
            auto packets = Packetize(DmBody());
            packets[0][0] = 0x40; // valid first-of-many kind
            packets[0][3] = 2;
            packets[0][4] = 0; // valid declared 512-byte body needs five packets
            FinishPacket(packets[0]);
            picture.Embed(packets);
            AssertPeak(Deliver(formatter, picture, 2), 3072);
        }

        TEST_METHOD(UnsupportedHeaderCannotAuthorizeDolbyVisionLock)
        {
            // Reserved metadata type/version, reserved EOS bits, and an
            // affected ID that is neither current nor immediate next.
            for (const unsigned variant : { 0U, 1U, 2U, 3U })
            {
                SenderPicture picture(false);
                auto packets = Packetize(DmBody());
                if (variant == 0) packets[0][0] |= 0x10;
                if (variant == 1) packets[0][0] |= 0x02;
                if (variant == 2) packets[0][2] |= 0x02;
                if (variant == 3) packets[0][1] = 0x53;
                FinishPacket(packets[0]);
                picture.Embed(packets);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                const auto dm = Deliver(formatter, picture, 1);
                Assert::IsFalse(dm.valid);
                Assert::IsTrue(dm.allowSdrFallback);
            }
        }
        TEST_METHOD(ValidHeaderBlocksSdrEvenWhenBodyIsUnsupported)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            auto body = DmBody();
            body[60] = 10; // unsupported tunnel code depth
            picture.Embed(Packetize(body));
            const auto dm = Deliver(formatter, picture, 1);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
        }

        TEST_METHOD(NoMetadataRetainsActiveDmAndDoesNotResurrectAbsentDm)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize({}, 3, 3, true));
            auto dm = Deliver(formatter, picture, 1);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 2), 3072);
            for (uint64_t counter = 3; counter < 40; ++counter)
            {
                picture.Embed(Packetize({}, 3, 3, true));
                AssertPeak(Deliver(formatter, picture, counter), 3072);
            }
            picture.Embed(Packetize({}, 4, 4, true));
            dm = Deliver(formatter, picture, 40);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
        }

        TEST_METHOD(AdvanceMetadataAppliesOnlyToImmediateNextPicture)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody(), 15, 15));
            AssertPeak(Deliver(formatter, picture, 10), 3072);
            picture.Embed(Packetize(DmBody(2048), 15, 0)); // ID wrap
            AssertPeak(Deliver(formatter, picture, 11), 3072);
            picture.Embed(Packetize({}, 0, 0, true));
            picture.CorruptAllCopies(); // contiguous counter alone associates pending DM
            AssertPeak(Deliver(formatter, picture, 12), 2048);
        }

        TEST_METHOD(DroppedNextPictureWaitsForHeaderNamingAffectedId)
        {
            SenderPicture picture(true);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody(), 1, 1));
            AssertPeak(Deliver(formatter, picture, 10), 3072);
            picture.Embed(Packetize(DmBody(2048), 1, 2));
            AssertPeak(Deliver(formatter, picture, 11), 3072);
            picture.CorruptAllCopies();
            AssertPeak(Deliver(formatter, picture, 13), 3072);
            picture.Embed(Packetize({}, 2, 2, true));
            AssertPeak(Deliver(formatter, picture, 14), 2048);
        }

        TEST_METHOD(UnrelatedCurrentIdDiscardsPendingAndActiveDm)
        {
            SenderPicture picture(true);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody(), 1, 1));
            AssertPeak(Deliver(formatter, picture, 10), 3072);
            picture.Embed(Packetize(DmBody(2048), 1, 2));
            AssertPeak(Deliver(formatter, picture, 11), 3072);
            picture.Embed(Packetize({}, 3, 3, true));
            const auto dm = Deliver(formatter, picture, 13);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
            picture.Embed(Packetize({}, 2, 2, true));
            Assert::IsFalse(Deliver(formatter, picture, 14).valid);
        }

        TEST_METHOD(EosNoMetadataAllowsSdrAfterBoundaryIncludingCounterGap)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 10), 3072);
            picture.Embed(Packetize({}, 3, 3, true, true));
            AssertPeak(Deliver(formatter, picture, 11), 3072);
            // A blank RGB frame contains no CRC-valid metadata.
            std::fill(picture.input.begin(), picture.input.end(), 0);
            const auto dm = Deliver(formatter, picture, 14);
            Assert::IsFalse(dm.valid);
            Assert::IsTrue(dm.allowSdrFallback);
            picture.Embed(Packetize(DmBody(2048)));
            AssertPeak(Deliver(formatter, picture, 15), 2048);
        }

        TEST_METHOD(MovieExitBlanksFirstUntrustedPictureButRetainsDmForRecovery)
        {
            for (const bool high8 : { false, true })
            {
                SenderPicture picture(high8);
                Formatter formatter;
                formatter.OnVideoState(picture.state);
                picture.Embed(Packetize(DmBody()));
                Assert::IsFalse(Deliver(formatter, picture, 1).requiresBlackFrame);

                // HDMI goes blank before capture publishes its new menu mode.
                // Cached DV decoding of this RGB picture used to create color.
                std::fill(picture.input.begin(), picture.input.end(), 0);
                const auto lost = Deliver(formatter, picture, 2);
                AssertPeak(lost, 3072); // cache survives a brief interruption
                Assert::IsTrue(lost.requiresBlackFrame);
                Assert::IsFalse(lost.allowSdrFallback);

                SenderPicture resumed(high8);
                resumed.Embed(Packetize({}, 3, 3, true));
                std::vector<BYTE> output;
                const auto recovered = Deliver(formatter, resumed, 3, &output);
                AssertPeak(recovered, 3072);
                Assert::IsFalse(recovered.requiresBlackFrame);
                AssertExactSamples(resumed, output);
            }
        }

        TEST_METHOD(BlankingPersistsThroughLostDmAndStopsOnFreshDmOrEos)
        {
            SenderPicture picture(false);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            Deliver(formatter, picture, 1);
            std::fill(picture.input.begin(), picture.input.end(), 0);
            for (uint64_t counter = 2; counter <= 25; ++counter)
                Assert::IsTrue(Deliver(formatter, picture, counter).requiresBlackFrame);
            picture.Embed(Packetize({}, 3, 3, true));
            const auto noDm = Deliver(formatter, picture, 26);
            Assert::IsFalse(noDm.valid);
            Assert::IsTrue(noDm.requiresBlackFrame);
            picture.Embed(Packetize(DmBody(2048)));
            Assert::IsFalse(Deliver(formatter, picture, 27).requiresBlackFrame);
            picture.Embed(Packetize({}, 3, 3, true, true));
            Assert::IsFalse(Deliver(formatter, picture, 28).requiresBlackFrame);
            std::fill(picture.input.begin(), picture.input.end(), 0);
            const auto sdr = Deliver(formatter, picture, 29);
            Assert::IsFalse(sdr.requiresBlackFrame);
            Assert::IsTrue(sdr.allowSdrFallback);
        }

        TEST_METHOD(RecoveredHeaderAndDamagedContinuationDoNotBlankKnownDvPixels)
        {
            SenderPicture picture(true);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            Deliver(formatter, picture, 1);
            picture.Embed(Packetize(DmBody(2048, true, 157)));
            picture.CorruptCopy(0, 0);
            picture.CorruptCopy(0, 1); // third header copy still authenticates DV
            picture.CorruptAllCopies(1); // old DM remains usable for same ID
            const auto dm = Deliver(formatter, picture, 2);
            AssertPeak(dm, 3072);
            Assert::IsFalse(dm.requiresBlackFrame);
        }

        TEST_METHOD(LossLimitSuspendsUntilFreshCompleteDmWithoutSdrFallback)
        {
            SenderPicture picture(true);
            Formatter formatter;
            formatter.OnVideoState(picture.state);
            picture.Embed(Packetize(DmBody()));
            AssertPeak(Deliver(formatter, picture, 1), 3072);
            picture.CorruptAllCopies();
            for (uint64_t counter = 2; counter < 25; ++counter)
                AssertPeak(Deliver(formatter, picture, counter), 3072);
            std::vector<BYTE> output;
            auto dm = Deliver(formatter, picture, 25, &output);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
            Assert::IsTrue(std::all_of(output.begin(), output.end(), [](BYTE b) { return b == 0; }));
            picture.Embed(Packetize({}, 3, 3, true));
            dm = Deliver(formatter, picture, 26);
            Assert::IsFalse(dm.valid || dm.allowSdrFallback);
            picture.Embed(Packetize(DmBody(2048)));
            AssertPeak(Deliver(formatter, picture, 27), 2048);
            formatter.OnVideoState(picture.state);
            std::fill(picture.input.begin(), picture.input.end(), 0);
            dm = Deliver(formatter, picture, 28);
            Assert::IsFalse(dm.valid);
            Assert::IsTrue(dm.allowSdrFallback);
        }
    };
}