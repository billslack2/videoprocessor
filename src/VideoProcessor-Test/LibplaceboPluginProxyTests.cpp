#include "pch.h"
#include "CppUnitTest.h"

#include "../VideoProcessor-Lib/vprenderer/LibplaceboPluginVideoRenderer.h"
#include "../VideoProcessor-Lib/AspectRatioOsd.h"
#include "../VideoProcessor-Lib/vprenderer/OptionalRendererLayout.h"

#include <type_traits>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vector>
#include <set>
#include <string>
#include <algorithm>
#include <cstdio>
#include <ConfigFile.h>
#include <UnifiedProfileRuntime.h>
#include "../VideoProcessor-Lib/vprenderer/LibplaceboRendererPluginApi.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include <chrono>

namespace
{
    std::mutex snapshotTestLogMutex;
    std::ofstream snapshotTestLog;
    std::atomic<unsigned> snapshotTestReads{0};
    std::atomic<unsigned> nativeP210Initializations{0};
    void __cdecl SnapshotTestLog(const char* message)
    {
        if (strstr(message, "native P210 (no CPU repack) upload")) ++nativeP210Initializations;
        if (strstr(message, "Configuration disk read:")) ++snapshotTestReads;
        std::lock_guard<std::mutex> guard(snapshotTestLogMutex);
        if (snapshotTestLog) snapshotTestLog << message << std::endl;
    }
    std::mutex classicPoseLogMutex;
    std::vector<std::string> classicPoseMessages;
    void __cdecl ClassicPoseTestLog(const char* message)
    {
        if(!strstr(message,"VP submitted geometry:") && !strstr(message,"Alpha source crop:") &&
            !strstr(message,"Alpha native presentation commit:") && !strstr(message,"SUBTITLE ") &&
            !strstr(message,"application profile") && !strstr(message,"Renderer configuration snapshot") &&
            !strstr(message,"VP source measurement interval:"))return;
        std::lock_guard<std::mutex> guard(classicPoseLogMutex);
        classicPoseMessages.emplace_back(message);
    }
    struct NativeP210Buffer : IUnknown
    {
        std::atomic<int> references{0};
        HRESULT QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
        ULONG AddRef() override { ++references; return 1; }
        ULONG Release() override { --references; return 0; }
    };
    struct SnapshotTestCallback : IRendererCallback
    {
        std::atomic<RendererState> state{RENDERSTATE_UNKNOWN};
        void OnRendererState(RendererState value, uint32_t) override { state = value; }
        void OnRendererDetailString(const CString&, uint32_t) override {}
    };
}



using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace VideoProcessorTest
{
	TEST_CLASS(LibplaceboPluginProxyTests)
	{
	public:
        TEST_METHOD(BetaUsesSeparatePluginAndRuntimeNames)
        {
            using namespace OptionalRendererLayout;
            Assert::IsTrue(PluginPath(L"C:\\VP", true) == L"C:\\VP\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll");
            Assert::IsTrue(PluginPath(L"C:\\VP") == L"C:\\VP\\vprenderer\\VideoProcessorVPRenderer.dll");
            Assert::AreEqual(L"libplacebo-388.dll", PrivateDependencies(true)[3]);
            Assert::AreEqual(L"libplacebo-360.dll", PrivateDependencies()[3]);
        }
		TEST_METHOD(AspectOsdMapsEveryNamedFormat)
		{
			struct Expected { double value; const wchar_t* ratio; const wchar_t* name; };
			const Expected expected[] = {
				{ 4.0 / 3.0, L"4:3", L"TV" },
				{ 1.37, L"1.37:1", L"Academy" },
				{ 1.43, L"1.43:1", L"IMAX 70mm" },
				{ 1.66, L"1.66:1", L"European widescreen" },
				{ 16.0 / 9.0, L"16:9", L"HDTV" },
				{ 1.85, L"1.85:1", L"Flat" },
				{ 1.90, L"1.90:1", L"Digital IMAX" },
				{ 2.00, L"2.00:1", L"Univisium" },
				{ 2.20, L"2.20:1", L"70mm / Todd-AO" },
				{ 21.0 / 9.0, L"21:9", L"Consumer ultrawide" },
				{ 2.35, L"2.35:1", L"CinemaScope" },
				{ 2.39, L"2.39:1", L"Scope" },
				{ 2.40, L"2.40:1", L"Scope" },
				{ 2.76, L"2.76:1", L"Ultra Panavision 70" }
			};
			for (const auto& item : expected)
			{
				const auto label = AspectRatioOsd::Classify(item.value);
				Assert::IsTrue(label.ratio == item.ratio);
				Assert::IsTrue(label.name == item.name);
			}
			Assert::IsTrue(AspectRatioOsd::Classify(1.50).name == L"nonstandard");
			Assert::IsTrue(AspectRatioOsd::Classify(1.503).ratio == L"1.50:1");
			Assert::IsTrue(AspectRatioOsd::Classify(2.50).ratio == L"2.50:1");
			Assert::IsTrue(AspectRatioOsd::Classify(0.0).ratio == L"---");
		}

		TEST_METHOD(AspectNamesClassifyWithoutChangingMeasurements)
		{
			const double isolatedTargets[] = {
				4.0 / 3.0, 1.37, 1.43, 1.66, 16.0 / 9.0,
				1.85, 1.90, 2.00, 2.20, 2.76
			};
			for (double target : isolatedTargets)
			{
				const auto atTarget = AspectRatioOsd::Classify(target);
				Assert::IsTrue(AspectRatioOsd::Classify(
					target * 0.99).ratio == atTarget.ratio);
				Assert::IsTrue(AspectRatioOsd::Classify(
					target * 1.01).ratio == atTarget.ratio);
				Assert::IsTrue(AspectRatioOsd::Classify(
					target * 0.9899).name == L"nonstandard");
				Assert::IsTrue(AspectRatioOsd::Classify(
					target * 1.0101).name == L"nonstandard");
			}
			const auto atBoundary = AspectRatioOsd::Classify(2.76 * 1.01);
			Assert::IsTrue(atBoundary.ratio == L"2.76:1");
			Assert::IsTrue(AspectRatioOsd::Classify(2.76 * 1.01001).name == L"nonstandard");
			Assert::IsTrue(AspectRatioOsd::Classify(2.39).ratio == L"2.39:1");
			Assert::IsTrue(AspectRatioOsd::Classify(2.40).ratio == L"2.40:1");
			Assert::IsTrue(AspectRatioOsd::Classify(
				2.0 * 2.39 * 2.40 / (2.39 + 2.40)).ratio == L"2.39:1");
			Assert::IsTrue(AspectRatioOsd::Classify(2.345).ratio == L"2.35:1");
		}

        TEST_METHOD(AspectDisplayDeadbandAnchorsToDisplayedValueWithoutDelayingAppliedGeometry)
        {
            AspectRatioOsd osd;
            DetectedPictureAspect r;
            r.sourceGeneration=1; r.available=true; r.provisional=true; r.ratio=1.50;
            Assert::IsTrue(osd.Update(r).detected == L"1.50:1 (nonstandard; provisional)");
            r.ratio=1.509; osd.Update(r); r.ratio=1.514;
            Assert::IsTrue(osd.Update(r).detected == L"1.50:1 (nonstandard; provisional)");
            r.ratio=1.515;
            Assert::IsTrue(osd.Update(r).detected == L"1.51:1 (nonstandard; provisional)");
            r.ratio=1.514; r.provisional=false;
            r.inUseAvailable=true; r.inUseState=PictureAspectUse::DETECTED; r.inUseRatio=2.39;
            Assert::IsTrue(osd.Update(r).detected == L"1.51:1 (nonstandard)");
            r.inUseRatio=2.40;
            Assert::IsTrue(osd.Update(r).inUse == L"2.40:1 (Scope)");
        }
        TEST_METHOD(AspectDisplayResetsForUnavailableNewSourceAndRenderer)
        {
            AspectRatioOsd osd;
            DetectedPictureAspect r; r.sourceGeneration=1; r.available=true; r.ratio=1.50;
            osd.Update(r); r.available=false;
            Assert::IsTrue(osd.Update(r).detected == L"Unavailable");
            r.available=true; r.ratio=1.509;
            Assert::IsTrue(osd.Update(r).detected == L"1.51:1 (nonstandard)");
            r.sourceGeneration=2; r.ratio=1.519;
            Assert::IsTrue(osd.Update(r).detected == L"1.52:1 (nonstandard)");
            osd.Reset(); r.ratio=1.529;
            Assert::IsTrue(osd.Update(r).detected == L"1.53:1 (nonstandard)");
        }

        TEST_METHOD(AspectTelemetryUsesNativeStartupWithoutClaimingDetection)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Submitted(1, 1, 3840.0/2160.0, 3840.0/2160.0, false, true);
            const auto rows = AspectRatioOsd::Format(telemetry.Read());
            Assert::IsTrue(rows.inUse == L"16:9 (HDTV)");
            Assert::IsTrue(rows.detected == L"Unavailable");
            telemetry.Observe(1, 2, 16.0/9.0, true, false);
            telemetry.Submitted(1, 2, 16.0/9.0, 16.0/9.0, false, true);
            Assert::IsTrue(telemetry.Read().inUseState == PictureAspectUse::DETECTED);
        }
        TEST_METHOD(AspectTelemetryKeepsSubmittedScopeWhileMenuCandidateIsLive)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(1, 10, 2.39, true, false);
            telemetry.Submitted(1, 10, 2.39, 16.0/9.0, true, true);
            telemetry.Observe(1, 11, 1.66, true, true);
            telemetry.Submitted(1, 11, 2.39, 16.0/9.0, true, true);
            const auto rows = AspectRatioOsd::Format(telemetry.Read());
            Assert::IsTrue(rows.inUse == L"2.39:1 (Scope; held)");
            Assert::IsTrue(rows.detected == L"1.66:1 (European widescreen; provisional)");
            telemetry.ClearDetection();
            telemetry.Submitted(1, 12, 2.39, 16.0/9.0, true, true);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).detected == L"Unavailable");
            Assert::AreEqual(2.39, telemetry.Read().inUseRatio);
        }
        TEST_METHOD(AspectTelemetryDoesNotHideARealSubmittedAspectChange)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(1, 1, 2.39, true, false);
            telemetry.Submitted(1, 1, 2.39, 16.0/9.0, true, true);
            telemetry.Observe(1, 2, 1.66, true, false);
            telemetry.Submitted(1, 2, 1.66, 16.0/9.0, true, true);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).inUse == L"1.66:1 (European widescreen)");
            telemetry.Observe(1, 3, 2.17, true, false);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).detected == L"2.17:1 (nonstandard)");
        }
        TEST_METHOD(AspectTelemetryPreservesPresentedPacketAndRejectsFailedSubmission)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(1, 20, 2.39, true, false);
            telemetry.Submitted(1, 19, 2.17, 16.0/9.0, true, true);
            Assert::AreEqual(uint64_t(19), telemetry.Read().presentedSequence);
            Assert::AreEqual(uint64_t(20), telemetry.Read().detectedSequence);
            Assert::AreEqual(2.17, telemetry.Read().inUseRatio);
            telemetry.Submitted(1, 21, 1.66, 16.0/9.0, true, false);
            Assert::AreEqual(2.17, telemetry.Read().inUseRatio);
            telemetry.ClearPresentation();
            Assert::IsFalse(telemetry.Read().inUseAvailable);
            Assert::IsTrue(telemetry.Read().available);
        }
        TEST_METHOD(AspectTelemetryResetsAcrossRealSourceChangeAndUsesNonWideRaster)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(1, 1, 2.39, true, false);
            telemetry.Submitted(1, 1, 2.39, 16.0/9.0, true, true);
            telemetry.Source(2);
            Assert::IsFalse(telemetry.Read().inUseAvailable || telemetry.Read().available);
            telemetry.Submitted(2, 1, 4.0/3.0, 4.0/3.0, false, true);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).inUse == L"4:3 (TV)");
            telemetry.Observe(2, 2, 0.0, true, false);
            Assert::IsFalse(telemetry.Read().available);
        }

		TEST_METHOD(RendererMetadataCyclesReuseAcceptedSnapshotWithoutDiskReads)
		{
			// Exercise the real renderer DLL and D3D initialization without capture hardware.
			HMODULE testModule = nullptr;
			Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&SnapshotTestLog), &testModule) != FALSE);
			wchar_t modulePath[MAX_PATH] = {};
			GetModuleFileNameW(testModule, modulePath, MAX_PATH);
			const std::wstring fullPath(modulePath);
			const std::wstring root = fullPath.substr(0, fullPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            const std::wstring pluginPath = root + L"\\..\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll";
#else
			const std::wstring pluginPath = root + L"\\vprenderer\\VideoProcessorVPRenderer.dll";
#endif
			HMODULE plugin = LoadLibraryExW(pluginPath.c_str(), nullptr,
				LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			Assert::IsNotNull(plugin, L"Build the Release renderer DLL before this integration test");
			const auto version = reinterpret_cast<VideoProcessorLibplaceboGetApiVersionFn>(
				GetProcAddress(plugin, VP_LIBPLACEBO_VERSION_EXPORT));
			const auto create = reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(
				GetProcAddress(plugin, VP_LIBPLACEBO_CREATE_EXPORT));
			const auto destroy = reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(
				GetProcAddress(plugin, VP_LIBPLACEBO_DESTROY_EXPORT));
			Assert::IsTrue(version && create && destroy);
			Assert::AreEqual(VP_LIBPLACEBO_PLUGIN_API_VERSION, version());
			char temp[MAX_PATH] = {};
			GetTempPathA(MAX_PATH, temp);
			const std::string path = std::string(temp) + "vp0188-renderer-synthetic.cfg";
			const std::string initial = "[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
				"[vprenderer]\nquality: high\nprofile_update_mode: live\n"
				"[vprenderer.fast]\nshortcut: F4\nquality: fast\n";
			std::ofstream(path) << initial;
			snapshotTestLog.open(root + L"\\vp0188-renderer-integration.log", std::ios::trunc);
			snapshotTestReads = 0;
			SnapshotTestCallback callback;
			struct Cleanup
			{
				HMODULE plugin;
				HWND window;
				IVideoRenderer* renderer = nullptr;
				VideoProcessorLibplaceboDestroyRendererFn destroy;
				~Cleanup()
				{
					if (renderer) { renderer->Stop(); destroy(renderer); }
					if (window) DestroyWindow(window);
					FreeLibrary(plugin);
					std::lock_guard<std::mutex> guard(snapshotTestLogMutex);
					snapshotTestLog.close();
				}
			} cleanup{plugin, CreateWindowExW(0, L"STATIC", L"VP-0188 renderer test",
				WS_POPUP, 0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr), nullptr, destroy};
			Assert::IsNotNull(cleanup.window);
			cleanup.renderer = create(&callback, 188, cleanup.window, nullptr, false, 1,
				VideoConversionOverride::VIDEOCONVERSION_NONE, path.c_str(), SnapshotTestLog);
			Assert::IsNotNull(cleanup.renderer);
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			UnifiedProfileRuntime::Runtime runtime;
			std::string error;
			Assert::IsTrue(runtime.Initialize(config, {}, error), std::wstring(error.begin(), error.end()).c_str());
			CString active;
			bool restart = false, reset = false;
			Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*runtime.GetSnapshot(), active, restart, reset));
			VideoStateComPtr state = new VideoState();
			state->valid = true;
			state->displayMode = std::make_shared<DisplayMode>(320, 180, false, 60000, 1001);
			state->videoFrameEncoding = VideoFrameEncoding::BGRA_8BIT;
			state->eotf = EOTF::PQ;
			state->colorspace = ColorSpace::BT_2020;
			state->hdrData = std::make_shared<HDRData>();
			state->hdrData->masteringDisplayMaxLuminance = 1000;
			state->hdrData->maxCll = 1000;
			state->hdrData->maxFall = 400;
			Assert::IsTrue(cleanup.renderer->OnVideoState(state));
			cleanup.renderer->Build();
			cleanup.renderer->Start();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			while (callback.state != RENDERSTATE_RENDERING && callback.state != RENDERSTATE_FAILED &&
				std::chrono::steady_clock::now() < deadline)
			{
				MSG message;
				while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
				{ TranslateMessage(&message); DispatchMessageW(&message); }
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			Assert::IsTrue(callback.state == RENDERSTATE_RENDERING, L"Renderer did not initialize; see vp0188-renderer-integration.log");
			const unsigned reads = snapshotTestReads.load();
			// Disk now disagrees with the accepted snapshot. Source updates must ignore it.
			std::ofstream(path) << "[general]\nswitch_refresh_rate: never\n[vprenderer]\nquality: balanced\n";
			for (int cycle = 0; cycle < 10; ++cycle)
			{
				VideoStateComPtr missing = new VideoState(*state);
				missing->hdrData.reset();
				Assert::IsTrue(cleanup.renderer->OnVideoState(missing));
				Assert::IsTrue(cleanup.renderer->OnVideoState(state));
			}
			Assert::AreEqual(reads, snapshotTestReads.load(), L"HDR cycles reread the config file");
			UnifiedProfileRuntime::SelectionResult selection;
			Assert::IsTrue(runtime.SelectKey("F4", {}, selection, error));
			Assert::IsTrue(selection.changed);
			Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*selection.snapshot, active, restart, reset));
			Assert::AreEqual(reads, snapshotTestReads.load(), L"Profile selection reread the config file");
			Assert::IsTrue(config.Load(path));
			UnifiedProfileRuntime::RefreshResult reload;
			Assert::IsTrue(runtime.Reload(config, {}, reload, error));
			Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*reload.snapshot, active, restart, reset));
			Assert::AreEqual(reads, snapshotTestReads.load(), L"Accepted host reload was reread by renderer");
			Logger::WriteMessage("VP-0188: real renderer initialized; 10 HDR withdrawal/restoration cycles, profile F4, and explicit reload passed with zero renderer config disk reads.\n");
			DeleteFileA(path.c_str());
		}

        TEST_METHOD(RealRendererUploadsNativeP210AndP010ForSdrAndHdrWithoutConversion)
        { VerifyNativeCaptureAspect(false); }

        TEST_METHOD(BetaRendererPublishesSubmittedAspectForNativeCapture)
        { VerifyNativeCaptureAspect(true); }

        void VerifyNativeCaptureAspect(bool beta)
        {
            HMODULE module = nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&SnapshotTestLog), &module) != FALSE);
            wchar_t modulePath[MAX_PATH] = {};
            GetModuleFileNameW(module, modulePath, MAX_PATH);
            std::wstring path(modulePath); path.resize(path.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            path += L"\\..";
#endif
            path += beta ? L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll" :
                L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin = LoadLibraryExW(path.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct PluginCleanup { HMODULE plugin; ~PluginCleanup(){ FreeLibrary(plugin); } } pluginCleanup{plugin};
            const auto create = reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(
                GetProcAddress(plugin, VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy = reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(
                GetProcAddress(plugin, VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create && destroy);
            VideoStateComPtr state = new VideoState();
            for (const auto nativeEncoding : {VideoFrameEncoding::P210, VideoFrameEncoding::P010})
            {
            state->valid = true; state->videoFrameEncoding = nativeEncoding;
            state->displayMode = std::make_shared<DisplayMode>(130, 130, false, 60000, 1001);
            std::vector<uint16_t> pixels(state->BytesPerFrame()/2);
            for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint16_t((64+(i%877)) << 6);
            for (const auto eotf : {EOTF::SDR, EOTF::PQ})
            {
                state->eotf = eotf; state->colorspace = eotf == EOTF::PQ ? ColorSpace::BT_2020 : ColorSpace::REC_709;
                char temporary[MAX_PATH] = {}, configPath[MAX_PATH] = {};
                Assert::IsTrue(GetTempPathA(MAX_PATH, temporary) != 0);
                Assert::IsTrue(GetTempFileNameA(temporary, "p21", 0, configPath) != 0);
                std::ofstream(configPath) << "[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                    "[vprenderer]\nquality: fast\n";
                SnapshotTestCallback callback; NativeP210Buffer buffer;
                const unsigned initialized = nativeP210Initializations.load();
                {
                    struct Cleanup
                    {
                        HWND window; IVideoRenderer* renderer; VideoProcessorLibplaceboDestroyRendererFn destroy;
                        const char* config;
                        ~Cleanup(){ if(renderer){ renderer->Stop(); destroy(renderer); }
                            if(window) DestroyWindow(window); DeleteFileA(config); }
                    } cleanup{CreateWindowExW(0, L"STATIC", L"Native P210 fixture", WS_POPUP,
                        0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr), nullptr, destroy, configPath};
                    Assert::IsNotNull(cleanup.window);
                    // An explicit P010 consumer must wait for legacy v210 capture.
                    auto* forced = create(&callback, 1, cleanup.window, nullptr, false, 1,
                        VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010, configPath, SnapshotTestLog);
                    Assert::IsNotNull(forced);
                    const bool accepted = forced->OnVideoState(state);
                    destroy(forced);
                    Assert::IsFalse(accepted);
                    cleanup.renderer = create(&callback, 2, cleanup.window, nullptr, false, 1,
                        VideoConversionOverride::VIDEOCONVERSION_NONE, configPath, SnapshotTestLog);
                    Assert::IsNotNull(cleanup.renderer);
                    Assert::IsTrue(cleanup.renderer->OnVideoState(state));
                    cleanup.renderer->Build(); cleanup.renderer->Start();
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                    uint64_t sequence = 1; bool presented = false;
                    while (std::chrono::steady_clock::now() < deadline && callback.state != RENDERSTATE_FAILED)
                    {
                        MSG message;
                        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                        { TranslateMessage(&message); DispatchMessageW(&message); }
                        if (callback.state == RENDERSTATE_RENDERING)
                        {
                            VideoFrame frame(pixels.data(), sequence++, 0, &buffer);
                            cleanup.renderer->OnVideoFrame(frame);
                            CString ingress;
                            presented = cleanup.renderer->HasPresentedLiveFrame() &&
                                cleanup.renderer->GetVideoIngressInfo(ingress) &&
                                ingress.Find(nativeEncoding==VideoFrameEncoding::P010 ? L"P010 (card capture; 10-bit 4:2:0; no CPU repack)" : L"P210 (native capture; no CPU repack)") >= 0;
                            if (presented) break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    }
                    Assert::IsTrue(presented, L"Native P210 failed to upload and present");
                    DetectedPictureAspect aspect;
                    const auto aspectDeadline = std::chrono::steady_clock::now()+std::chrono::seconds(2);
                    do {
                        cleanup.renderer->GetDetectedPictureAspect(aspect);
                        if (aspect.inUseAvailable) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    } while (std::chrono::steady_clock::now()<aspectDeadline);
                    Assert::IsTrue(aspect.inUseAvailable, L"Actual renderer did not publish submitted AR");
                    Assert::AreEqual(1.0,aspect.inUseRatio,0.0001,L"Source is 130x130; target window aspect must not replace it");
                    Assert::IsTrue(aspect.presentedSequence>0);
                    Assert::AreEqual(initialized+(nativeEncoding==VideoFrameEncoding::P210?1u:0u), nativeP210Initializations.load());
                }
                Assert::AreEqual(0, buffer.references.load(), L"Renderer retained a capture buffer after shutdown");
            }
            }
        }

        TEST_METHOD(StandardRendererRetainsUncertainPictureAndAcceptsRealTransitions)
        { VerifyNativeAspectTransitionSequence(false); }
        TEST_METHOD(BetaRendererRetainsUncertainPictureAndAcceptsRealTransitions)
        { VerifyNativeAspectTransitionSequence(true); }

        TEST_METHOD(StandardRendererKeepsNativeAndConfiguredFillSeparate)
        { VerifyNativeAspectTransitionSequence(false,true); }
        TEST_METHOD(BetaRendererKeepsNativeAndConfiguredFillSeparate)
        { VerifyNativeAspectTransitionSequence(true,true); }

        TEST_METHOD(StandardRendererKeepsClassicTranslatedFilledPoseAcrossNaturalSkippedScans)
        { VerifyClassicTranslatedFilledPose(false); }
        TEST_METHOD(BetaRendererKeepsClassicTranslatedFilledPoseAcrossNaturalSkippedScans)
        { VerifyClassicTranslatedFilledPose(true); }

        void VerifyClassicTranslatedFilledPose(bool beta)
        {
            HMODULE self=nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&ClassicPoseTestLog),&self)!=FALSE);
            wchar_t modulePath[MAX_PATH]={};GetModuleFileNameW(self,modulePath,MAX_PATH);
            std::wstring pluginPath(modulePath);pluginPath.resize(pluginPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            pluginPath+=L"\\..";
#endif
            pluginPath+=beta?L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll":
                L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin=LoadLibraryExW(pluginPath.c_str(),nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct ModuleCleanup { HMODULE value; ~ModuleCleanup(){FreeLibrary(value);} } moduleCleanup{plugin};
            const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create&&destroy);
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseMessages.clear();}
            char temp[MAX_PATH]={},configPath[MAX_PATH]={};
            Assert::IsTrue(GetTempPathA(MAX_PATH,temp)!=0 && GetTempFileNameA(temp,"cpt",0,configPath)!=0);
            std::ofstream(configPath)<<"[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                "[vprenderer]\nquality: fast\n"
                "[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
                "[vprenderer.zoom.native]\nautomatic_crop: true\nsubtitle_fit: true\n"
                "crop_narrower_content_to_fill_screen: false\ncrop_wider_content_to_fill_screen: true\n"
                "crop_wider_content_aspect_limit: 2.42:1\n"
                "[vprenderer.subtitles.classic]\ntype: classic\noffset_pixels: 0\n"
                "subtitle_engage_drift_ms: 0\nsubtitle_release_drift_ms: 0\n"
                "subtitle_target_buffer_pixels: 0\nsubtitle_hold_seconds: 2\n";
            // Scaled native version of the logged 250-pixel same-size shift.
            // These exact pixels define both the native plane and caption extent.
            constexpr int width=960,height=540,bar=68,pictureBottom=height-bar;
            constexpr int captionTop=498,captionBottom=514;
            VideoStateComPtr state=new VideoState();state->valid=true;
            state->videoFrameEncoding=VideoFrameEncoding::P210;
            state->displayMode=std::make_shared<DisplayMode>(width,height,false,60000,1001);
            state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
            std::vector<uint16_t> native(state->BytesPerFrame()/2,uint16_t(512<<6));
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                native[size_t(y)*width+x]=uint16_t((y>=bar&&y<pictureBottom?300:64)<<6);
            auto caption=native;
            for(int glyph=0;glyph<12;++glyph)for(int y=captionTop;y<captionBottom;++y)
                for(int x=342+glyph*22;x<356+glyph*22;++x)
                    if(x<345+glyph*22 || x>=353+glyph*22 || (y>=captionTop+6&&y<captionTop+9))
                        caption[size_t(y)*width+x]=uint16_t(700<<6);
            const ActivePictureBounds physical{0,bar,width,pictureBottom,width,height,double(width)/(pictureBottom-bar),
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            // The dense bar renderer includes max(8,height/90) native pixels and
            // the explicitly configured zero offset/reserve. Both extents are even.
            const int shift=captionBottom+(std::max)(8,height/90)-pictureBottom;
            Assert::AreEqual(50,shift);
            auto shifted=physical;shifted.top+=shift;shifted.bottom+=shift;
            AlphaSourceCrop::AspectLimitFillInput fillInput;fillInput.sourceBounds=shifted;
            fillInput.contentReferenceAvailable=true;fillInput.contentReferenceBounds=physical;
            fillInput.trustedContentAuthorityAccepted=true;fillInput.screenAspect=2.35;
            fillInput.cropWiderContentToFillScreen=fillInput.widerLimitConfigured=true;fillInput.widerAspectLimit=2.42;
            const auto filled=AlphaSourceCrop::EvaluateAspectLimitFill(fillInput);
            Assert::IsTrue(filled.applied && filled.sourceBounds.left>0 && filled.sourceBounds.right<width);
            const auto& expected=filled.sourceBounds;
            SnapshotTestCallback callback;NativeP210Buffer buffer;
            struct Cleanup {
                HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;const char* config;
                ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(config);}
            } cleanup{CreateWindowExW(0,L"STATIC",L"Classic translated filled pose fixture",WS_POPUP,
                0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
            Assert::IsNotNull(cleanup.window);
            cleanup.renderer=create(&callback,43,cleanup.window,nullptr,false,1,
                VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,ClassicPoseTestLog);
            Assert::IsNotNull(cleanup.renderer);Assert::IsTrue(cleanup.renderer->OnVideoState(state));
            cleanup.renderer->Build();cleanup.renderer->Start();uint64_t sequence=1;
            auto pump=[&](std::vector<uint16_t>& pixels) {
                MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                if(callback.state==RENDERSTATE_RENDERING) {
                    VideoFrame frame(pixels.data(),sequence++,0,&buffer);cleanup.renderer->OnVideoFrame(frame);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(17));
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);return value;
            };
            auto messages=[] {
                std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseMessages;
            };
            auto source=[](const std::string& line,const char* name) {
                const auto at=line.find(name);return at==std::string::npos?uint64_t(0):uint64_t(std::strtoull(line.c_str()+at+strlen(name),nullptr,10));
            };
            auto bounds=[](const std::string& line,const char* name,double& left,double& top,double& right,double& bottom) {
                const auto at=line.find(name);return at!=std::string::npos &&
                    sscanf_s(line.c_str()+at+strlen(name),"%lf,%lf-%lf,%lf",&left,&top,&right,&bottom)==4;
            };
            auto equal=[&](const std::string& line,const char* name,const ActivePictureBounds& wanted) {
                double l=0,t=0,r=0,b=0;return bounds(line,name,l,t,r,b) &&
                    std::abs(l-wanted.left)<0.01 && std::abs(t-wanted.top)<0.01 &&
                    std::abs(r-wanted.right)<0.01 && std::abs(b-wanted.bottom)<0.01;
            };
            const auto acquireDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            bool nativeAcquired=false;
            do {
                const auto value=pump(native);
                if(value.available&&!value.provisional&&value.inUseAvailable&&
                    std::abs(value.ratio-physical.aspectRatio)<0.005) {
                    for(const auto& line:messages())if(line.find("Alpha native presentation commit:")!=std::string::npos &&
                        equal(line," native_rect=",physical) && line.find("reason=accepted-native-and-presentation-committed")!=std::string::npos)
                        nativeAcquired=true;
                }
            }while(!nativeAcquired&&std::chrono::steady_clock::now()<acquireDeadline&&callback.state!=RENDERSTATE_FAILED);
            if(!nativeAcquired)for(const auto& line:messages())Logger::WriteMessage(line.c_str());
            Assert::IsTrue(nativeAcquired,L"The real plugin must independently acquire the source-defined native bar plane before caption onset.");
            const uint64_t captionFirst=sequence;uint64_t committedPose=0,lastPresented=0;
            bool scheduled=false,skipped=false;unsigned submittedPoseEvents=0;
            std::set<uint64_t> observedPresentedSources;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            do {
                lastPresented=pump(caption).presentedSequence;
                const auto log=messages();
                for(const auto& line:log) {
                    if(line.find("Alpha native presentation commit:")!=std::string::npos && source(line," selected=")>=captionFirst &&
                        equal(line," actual=",expected) && line.find("reason=accepted-native-and-presentation-committed")!=std::string::npos)
                        if(!committedPose)committedPose=source(line," selected=");
                }
                scheduled=skipped=false;submittedPoseEvents=0;
                uint64_t previousIntervalSource=0;
                for(const auto& line:log) {
                    if(line.find("VP source measurement interval:")!=std::string::npos) {
                        const auto intervalSource=source(line," source=");
                        const auto batches=source(line," batches="),requests=source(line," subtitle_requests="),hits=source(line," subtitle_hits=");
                        // This counter interval begins after the prior interval log.
                        // Stable policy suppresses crop-change logs, but actual scan
                        // requests still distinguish dense scans from skipped sources.
                        if(committedPose && previousIntervalSource>=committedPose && batches>0 && requests>=hits) {
                            const auto scans=requests-hits;
                            scheduled|=scans>0;skipped|=scans<batches;
                        }
                        previousIntervalSource=intervalSource;
                    }
                    if(line.find("Alpha source crop:")!=std::string::npos && source(line," sequence=")>=captionFirst) {
                        scheduled|=line.find(" dense_scan=scheduled")!=std::string::npos;
                        skipped|=line.find(" dense_scan=skipped")!=std::string::npos;
                    }
                    if(committedPose && line.find("VP submitted geometry:")!=std::string::npos && source(line," selected=")>=committedPose) {
                        ++submittedPoseEvents;
                        if(!equal(line," actual_source=",expected)) {
                            for(const auto& diagnostic:log)Logger::WriteMessage(diagnostic.c_str());
                            Logger::WriteMessage(line.c_str());
                            Assert::Fail(L"A naturally skipped or scheduled source changed the successfully submitted Classic translated/filled pose.");
                        }
                    }
                }
                if(committedPose && lastPresented>=committedPose)observedPresentedSources.insert(lastPresented);
                if(committedPose && observedPresentedSources.size()>=24 && scheduled && skipped && submittedPoseEvents)break;
            }while(std::chrono::steady_clock::now()<deadline&&callback.state!=RENDERSTATE_FAILED);
            for(const auto& line:messages())Logger::WriteMessage(line.c_str());
            Logger::WriteMessage((std::string("Classic direct pose observations: committed=")+std::to_string(committedPose)+
                " distinct_presented="+std::to_string(observedPresentedSources.size())+
                " scheduled="+std::to_string(scheduled)+" skipped="+std::to_string(skipped)).c_str());
            Assert::IsTrue(committedPose>0,L"Real plugin never committed the independently acquired Classic translation plus configured side fill.");
            Assert::IsTrue(scheduled&&skipped,L"The real renderer must exercise natural scheduled and skipped dense scans; the fixture cannot force a pose.");
            Assert::IsTrue(observedPresentedSources.size()>=24 && submittedPoseEvents>0,
                L"The stable final pose must survive at least 24 distinct observed selected source identities.");
        }

        TEST_METHOD(StandardRendererClassicLifecycleWithNormalDriftAndWiderFill)
        { VerifyClassicLifecycleMatrix(false,false); }
        TEST_METHOD(StandardRendererClassicLifecycleWithNormalDriftAndNarrowerFill)
        { VerifyClassicLifecycleMatrix(false,true); }
        TEST_METHOD(BetaRendererClassicLifecycleWithNormalDriftAndWiderFill)
        { VerifyClassicLifecycleMatrix(true,false); }
        TEST_METHOD(BetaRendererClassicLifecycleWithNormalDriftAndNarrowerFill)
        { VerifyClassicLifecycleMatrix(true,true); }

        void VerifyClassicLifecycleMatrix(bool beta,bool narrower)
        {
            struct ProbeClock final : ITimingClock {
                LARGE_INTEGER frequency{};
                ProbeClock(){Assert::IsTrue(QueryPerformanceFrequency(&frequency)!=FALSE && frequency.QuadPart>0);}
                timingclocktime_t TimingClockNow() override {LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;}
                timingclocktime_t TimingClockTicksPerSecond() const override {return frequency.QuadPart;}
                const TCHAR* TimingClockDescription() override {return _T("Classic lifecycle integration QPC");}
            } clock;
            HMODULE self=nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&ClassicPoseTestLog),&self)!=FALSE);
            wchar_t modulePath[MAX_PATH]={};GetModuleFileNameW(self,modulePath,MAX_PATH);
            std::wstring pluginPath(modulePath);pluginPath.resize(pluginPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            pluginPath+=L"\\..";
#endif
            pluginPath+=beta?L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll":L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin=LoadLibraryExW(pluginPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct ModuleCleanup {HMODULE value;~ModuleCleanup(){FreeLibrary(value);}} moduleCleanup{plugin};
            const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create&&destroy);
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseMessages.clear();}
            char temp[MAX_PATH]={},configPath[MAX_PATH]={};
            Assert::IsTrue(GetTempPathA(MAX_PATH,temp)!=0 && GetTempFileNameA(temp,"clm",0,configPath)!=0);
            std::ofstream(configPath)<<"[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                "[vprenderer]\nquality: fast\n"
                "[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
                "[vprenderer.zoom.scope]\nautomatic_crop: true\nsubtitle_fit: true\n"
                "crop_narrower_content_to_fill_screen: true\ncrop_wider_content_to_fill_screen: true\n"
                "crop_narrower_content_aspect_limit: 2.32:1\ncrop_wider_content_aspect_limit: 2.42:1\n"
                "[vprenderer.subtitles.classic]\ntype: classic\nshortcut: F5\noffset_pixels: 15\n"
                "subtitle_engage_drift_ms: 500\nsubtitle_release_drift_ms: 2000\n"
                "subtitle_target_buffer_pixels: 10\nsubtitle_hold_seconds: 2\n"
                "[vprenderer.subtitles.reconstruction]\ntype: generated_gray\nshortcut: F6\noffset_pixels: 10\n"
                "text_reduction_percent: 30\ncleanup_fill: stretch\nsubtitle_hold_ms: 250\n";
            constexpr int width=960,height=540,captionHeight=16;
            const int bar=narrower?64:68,pictureBottom=height-bar;
            const int captionBottom=pictureBottom+20,captionTop=captionBottom-captionHeight;
            VideoStateComPtr state=new VideoState();state->valid=true;state->videoFrameEncoding=VideoFrameEncoding::P210;
            state->displayMode=std::make_shared<DisplayMode>(width,height,false,narrower?24000:60000,1001);
            state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
            std::vector<uint16_t> native(state->BytesPerFrame()/2,uint16_t(512<<6));
            auto paintPicture=[&](std::vector<uint16_t>& pixels,int top,int bottom) {
                for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                    pixels[size_t(y)*width+x]=uint16_t((y>=top&&y<bottom?300:64)<<6);
            };
            paintPicture(native,bar,pictureBottom);auto caption=native;auto expanded=native;paintPicture(expanded,8,height-8);
            for(int glyph=0;glyph<12;++glyph)for(int y=captionTop;y<captionBottom;++y)
                for(int x=342+glyph*22;x<356+glyph*22;++x)
                    if(x<345+glyph*22 || x>=353+glyph*22 || (y>=captionTop+6&&y<captionTop+9))
                        caption[size_t(y)*width+x]=uint16_t(700<<6);
            const ActivePictureBounds physical{0,bar,width,pictureBottom,width,height,double(width)/(pictureBottom-bar),ActivePictureBounds::BarAxes::TOP_BOTTOM};
            const int target=(std::min)(bar,captionBottom+(std::max)(8,height/90)+15-pictureBottom+10);
            const int shift=(target+1)&~1;
            auto fillFor=[&](ActivePictureBounds bounds) {
                AlphaSourceCrop::AspectLimitFillInput fill;fill.sourceBounds=bounds;fill.contentReferenceAvailable=true;
                fill.contentReferenceBounds=physical;fill.trustedContentAuthorityAccepted=true;fill.screenAspect=2.35;
                fill.cropNarrowerContentToFillScreen=fill.narrowerLimitConfigured=true;fill.narrowerAspectLimit=2.32;
                fill.cropWiderContentToFillScreen=fill.widerLimitConfigured=true;fill.widerAspectLimit=2.42;
                return AlphaSourceCrop::EvaluateAspectLimitFill(fill);
            };
            const auto initialFill=fillFor(physical);auto translated=physical;translated.top+=shift;translated.bottom+=shift;
            const auto translatedFill=fillFor(translated);const auto initial=initialFill.sourceBounds,settled=translatedFill.sourceBounds;
            Assert::IsTrue(initialFill.applied&&translatedFill.applied);
            Assert::IsTrue(narrower?initial.top>physical.top:initial.left>physical.left);
            SnapshotTestCallback callback;NativeP210Buffer buffer;
            struct Cleanup {
                HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;const char* config;
                ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(config);}
            } cleanup{CreateWindowExW(0,L"STATIC",L"Classic normal-drift lifecycle matrix",WS_POPUP,0,0,640,360,
                nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
            Assert::IsNotNull(cleanup.window);
            cleanup.renderer=create(&callback,44,cleanup.window,&clock,true,12,VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,ClassicPoseTestLog);
            Assert::IsNotNull(cleanup.renderer);
            ConfigFile config;Assert::IsTrue(config.Load(configPath));UnifiedProfileRuntime::Runtime runtime;std::string error;
            const bool initialized=runtime.Initialize(config,{},error);
            if(!initialized)Logger::WriteMessage(error.c_str());
            Assert::IsTrue(initialized,std::wstring(error.begin(),error.end()).c_str());
            CString active;bool restart=false,reset=false;
            Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*runtime.GetSnapshot(),active,restart,reset));
            Assert::IsFalse(restart);
            cleanup.renderer->SetQueueFramePolicy(4,4,true);
            cleanup.renderer->SetActivePictureLookaheadFrames(4);
            Assert::IsTrue(cleanup.renderer->OnVideoState(state));cleanup.renderer->Build();cleanup.renderer->Start();
            uint64_t sequence=1;size_t logCursor=0;
            auto messages=[] {std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseMessages;};
            auto dump=[&] {for(const auto& line:messages())Logger::WriteMessage(line.c_str());};
            auto require=[&](bool valid,const wchar_t* message) {if(!valid){dump();Assert::Fail(message);}};
            auto integer=[](const std::string& line,const char* key) {
                const auto at=line.find(key);return at==std::string::npos?uint64_t(0):uint64_t(std::strtoull(line.c_str()+at+strlen(key),nullptr,10));
            };
            auto number=[](const std::string& line,const char* key) {
                const auto at=line.find(key);return at==std::string::npos?0.0:std::strtod(line.c_str()+at+strlen(key),nullptr);
            };
            struct Geometry {uint64_t sequence=0;double left=0,top=0,right=0,bottom=0,pts=0;unsigned mode=0;bool composed=false;};
            auto geometries=[&] {
                std::vector<Geometry> result;const auto log=messages();
                for(;logCursor<log.size();++logCursor) {
                    const auto& line=log[logCursor];if(line.find("VP submitted geometry:")==std::string::npos)continue;
                    Geometry g;g.sequence=integer(line," selected=");g.pts=number(line," source_pts=");g.mode=unsigned(integer(line," mode="));
                    g.composed=integer(line," composed=")!=0;const auto at=line.find(" actual_source=");
                    require(at!=std::string::npos&&sscanf_s(line.c_str()+at+15,"%lf,%lf-%lf,%lf",&g.left,&g.top,&g.right,&g.bottom)==4,
                        L"Actual-plugin matrix could not parse submitted source geometry.");
                    require(std::isfinite(g.pts)&&g.pts>0,L"Actual-plugin matrix requires valid QPC-derived source timestamps.");
                    result.push_back(g);
                }
                return result;
            };
            auto same=[](const Geometry& actual,const ActivePictureBounds& wanted) {
                return std::abs(actual.left-wanted.left)<0.01&&std::abs(actual.top-wanted.top)<0.01&&
                    std::abs(actual.right-wanted.right)<0.01&&std::abs(actual.bottom-wanted.bottom)<0.01;
            };
            auto sameSize=[&](const Geometry& g) {
                return std::abs(g.right-g.left-(settled.right-settled.left))<0.01&&
                    std::abs(g.bottom-g.top-(settled.bottom-settled.top))<0.01;
            };
            const auto sourcePeriod=std::chrono::duration<double>(1001.0/(narrower?24000.0:60000.0));
            auto nextSource=std::chrono::steady_clock::now();
            auto pump=[&](std::vector<uint16_t>& pixels) {
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                if(callback.state==RENDERSTATE_RENDERING){VideoFrame frame(pixels.data(),sequence++,clock.TimingClockNow(),&buffer);cleanup.renderer->OnVideoFrame(frame);}
                nextSource+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(sourcePeriod);
                std::this_thread::sleep_until(nextSource);
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);return value;
            };
            auto scans=[&](uint64_t first) {
                bool scheduled=false,skipped=false;
                for(const auto& line:messages())if(line.find("Alpha source crop:")!=std::string::npos&&integer(line," sequence=")>=first) {
                    scheduled|=line.find(" dense_scan=scheduled")!=std::string::npos;skipped|=line.find(" dense_scan=skipped")!=std::string::npos;
                }
                return scheduled&&(narrower||skipped);
            };
            auto acquireNative=[&] {
                const auto first=sequence,deadlineMs=GetTickCount64()+15000;bool gotActual=false,gotNative=false;
                do {
                    const auto value=pump(native);
                    gotNative=value.available&&!value.provisional&&value.detectedSequence>=first&&std::abs(value.ratio-physical.aspectRatio)<0.005;
                    for(const auto& g:geometries())if(g.sequence>=first&&same(g,initial))gotActual=true;
                    if(gotNative&&gotActual)return;
                }while(GetTickCount64()<deadlineMs&&callback.state!=RENDERSTATE_FAILED);
                require(false,L"Normal-drift fixture failed independent native-plane acquisition or exact configured fill.");
            };
            acquireNative();
            Logger::WriteMessage("Classic lifecycle: caption onset with 500 ms engage drift\n");
            uint64_t onset=sequence,finalSequence=0;double previousTop=initial.top;std::set<int> intermediate;
            std::set<uint64_t> settledSources;const auto engageDeadline=GetTickCount64()+5000;
            do {
                const auto value=pump(caption);
                for(const auto& g:geometries())if(g.sequence>=onset) {
                    require(g.mode==0&&sameSize(g),L"Classic onset resized its picture instead of keeping the configured filled viewport size.");
                    require(g.top>=previousTop-0.01&&g.top>=initial.top-0.01&&g.top<=settled.top+0.01,
                        L"Classic engage drift reversed or left its bounded native-to-caption translation interval.");
                    previousTop=g.top;if(g.top>initial.top+0.01&&g.top<settled.top-0.01)intermediate.insert(int(std::lround(g.top)));
                    if(same(g,settled)&&!finalSequence)finalSequence=g.sequence;
                }
                if(finalSequence&&value.presentedSequence>=finalSequence)settledSources.insert(value.presentedSequence);
                if(finalSequence&&settledSources.size()>=24&&scans(onset))break;
            }while(GetTickCount64()<engageDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(finalSequence>0&&settledSources.size()>=24,L"Classic engage never reached and retained its exact final filled pose for 24 distinct presented sources.");
            require(intermediate.size()>=2,L"The configured 500 ms Classic engage drift snapped instead of exercising intermediate actual poses.");
            require(scans(onset),L"Classic normal-drift onset must exercise natural scheduled scans and, above 30 Hz, skipped dense scans.");
            Logger::WriteMessage("Classic lifecycle: two-second hold then two-second release drift\n");
            const uint64_t removal=sequence;const double removalPts=double(clock.TimingClockNow())/clock.TimingClockTicksPerSecond();
            double releaseStarted=0,releaseFinished=0;previousTop=settled.top;std::set<int> releaseIntermediate;std::set<uint64_t> returnedSources;
            uint64_t returnedSequence=0;const auto releaseDeadline=GetTickCount64()+7000;
            do {
                const auto value=pump(native);
                for(const auto& g:geometries())if(g.sequence>=removal) {
                    require(g.mode==0&&sameSize(g),L"Classic hold/release resized its filled viewport or failed open between dense scans.");
                    require(g.top<=previousTop+0.01&&g.top>=initial.top-0.01&&g.top<=settled.top+0.01,
                        L"Classic release drift reversed direction or jumped outside its bounded interval.");
                    if(g.top<settled.top-0.01&&!releaseStarted)releaseStarted=g.pts;
                    previousTop=g.top;if(g.top>initial.top+0.01&&g.top<settled.top-0.01)releaseIntermediate.insert(int(std::lround(g.top)));
                    if(same(g,initial)&&!returnedSequence){returnedSequence=g.sequence;releaseFinished=g.pts;}
                }
                if(returnedSequence&&value.presentedSequence>=returnedSequence)returnedSources.insert(value.presentedSequence);
                if(returnedSources.size()>=24)break;
            }while(GetTickCount64()<releaseDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(returnedSources.size()>=24&&releaseIntermediate.size()>=2,L"Classic did not exercise and complete its normal hold/release lifecycle.");
            require(releaseStarted-removalPts>=1.7,L"Classic subtitle hold released substantially before the configured two seconds.");
            require(releaseFinished-releaseStarted>=1.2,L"Classic two-second release drift snapped instead of moving through its timed interval.");
            // Re-engage before a real picture transition so a previous caption owner
            // cannot conceal newly exposed broad picture pixels in either bar.
            Logger::WriteMessage("Classic lifecycle: real picture expansion overrides an established caption pose\n");
            const auto reacquireDeadline=GetTickCount64()+5000;bool translatedAgain=false;
            do {pump(caption);for(const auto& g:geometries())translatedAgain|=same(g,settled);}
            while(!translatedAgain&&GetTickCount64()<reacquireDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(translatedAgain,L"Classic failed to establish its caption pose before the real-picture expansion challenge.");
            const uint64_t pictureFirst=sequence;bool pictureAccepted=false;std::set<uint64_t> pictureSources;const auto pictureDeadline=GetTickCount64()+15000;
            do {
                const auto value=pump(expanded);
                // Pixel preservation starts with the first observed submitted source;
                // AR confirmation and the stability window may complete later.
                if(value.presentedSequence>=pictureFirst)
                    require(value.inUseAvailable && value.inUseState!=PictureAspectUse::UNAVAILABLE &&
                        std::isfinite(value.inUseRatio) && value.inUseRatio>0 &&
                        value.inUseRatio<=double(width)/(height-16)+0.00001,
                        L"The first observed expanded-picture output retained the old cropped viewport while AR confirmation was pending.");
                for(const auto& g:geometries())if(g.sequence>=pictureFirst)
                    require(g.top<=8.01&&g.bottom>=height-8-0.01&&g.left<=0.01&&g.right>=width-0.01,
                        L"An old Classic caption pose clipped actual broad picture expansion.");
                pictureAccepted=value.available&&!value.provisional&&value.detectedSequence>=pictureFirst&&std::abs(value.ratio-double(width)/(height-16))<0.005;
                if(pictureAccepted&&value.presentedSequence>=pictureFirst)pictureSources.insert(value.presentedSequence);
                if(pictureAccepted&&pictureSources.size()>=24)break;
            }while(GetTickCount64()<pictureDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(pictureAccepted&&pictureSources.size()>=24,L"Actual broad picture expansion did not replace the old scope detection and presentation.");
            acquireNative();
            Logger::WriteMessage("Classic lifecycle: live application profile switch to Reconstruction\n");
            const auto captionDeadline=GetTickCount64()+5000;translatedAgain=false;
            do {pump(caption);for(const auto& g:geometries())translatedAgain|=same(g,settled);}
            while(!translatedAgain&&GetTickCount64()<captionDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(translatedAgain,L"Classic did not establish its pose before the profile transition challenge.");
            UnifiedProfileRuntime::SelectionResult selection;
            require(runtime.SelectKey("F6",{},selection,error)&&selection.changed,L"Application runtime failed to select the Reconstruction profile.");
            require(selection.snapshot && selection.snapshot->effectiveSelections.count("subtitles") &&
                selection.snapshot->effectiveSelections.at("subtitles").size()==1 &&
                selection.snapshot->effectiveSelections.at("subtitles").front()=="reconstruction",
                L"Profile transition fixture did not publish the requested Reconstruction selection.");
            restart=reset=false;require(cleanup.renderer->ApplyApplicationState(*selection.snapshot,active,restart,reset),L"Actual renderer rejected the Reconstruction profile snapshot.");
            require(!restart,L"Subtitle profile transition unexpectedly requires a renderer rebuild.");
            const uint64_t reconstructionFirst=sequence;uint64_t reconstructionReady=0,compositionSequence=0;bool composed=false;
            std::set<uint64_t> reconstructionSources;const auto reconstructionDeadline=GetTickCount64()+45000;
            do {
                const auto value=pump(caption);
                for(const auto& g:geometries())if(g.sequence>=reconstructionFirst&&g.mode==3) {
                    require(same(g,initial),L"Classic-to-Reconstruction profile transition retained or reintroduced Classic picture displacement/unsafe visibility fitting.");
                    composed|=g.composed;if(!reconstructionReady)reconstructionReady=g.sequence;
                }
                for(const auto& line:messages())if(line.find("SUBTITLE SHADER GEOMETRY:")!=std::string::npos &&
                    integer(line," frame=")>=reconstructionFirst)compositionSequence=integer(line," frame=");
                composed|=compositionSequence>0&&value.presentedSequence>=compositionSequence;
                if(reconstructionReady&&value.presentedSequence>=reconstructionReady)reconstructionSources.insert(value.presentedSequence);
                if(composed&&reconstructionSources.size()>=24)break;
            }while(GetTickCount64()<reconstructionDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(composed&&reconstructionSources.size()>=24,L"Reconstruction profile did not compose real caption pixels while keeping the native picture presentation stable.");
        }

        void VerifyNativeAspectTransitionSequence(bool beta,bool configuredFill=false)
        {
            HMODULE self=nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&SnapshotTestLog),&self)!=FALSE);
            wchar_t modulePath[MAX_PATH]={};
            GetModuleFileNameW(self,modulePath,MAX_PATH);
            std::wstring pluginPath(modulePath);pluginPath.resize(pluginPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            pluginPath+=L"\\..";
#endif
            pluginPath+=beta?L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll":
                L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin=LoadLibraryExW(pluginPath.c_str(),nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct ModuleCleanup { HMODULE value; ~ModuleCleanup(){FreeLibrary(value);} } moduleCleanup{plugin};
            struct FixtureLogCleanup {
                ~FixtureLogCleanup(){std::lock_guard<std::mutex> lock(snapshotTestLogMutex);snapshotTestLog.close();}
            } fixtureLogCleanup;
            { std::lock_guard<std::mutex> lock(snapshotTestLogMutex);
              snapshotTestLog.close();snapshotTestLog.clear();
              snapshotTestLog.open(configuredFill
                  ? (beta?"ar-fill-beta.log":"ar-fill-standard.log")
                  : (beta?"ar-transition-beta.log":"ar-transition-standard.log"),std::ios::trunc); }
            const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create&&destroy);
            char temp[MAX_PATH]={},configPath[MAX_PATH]={};
            Assert::IsTrue(GetTempPathA(MAX_PATH,temp)!=0);
            Assert::IsTrue(GetTempFileNameA(temp,"art",0,configPath)!=0);
            std::ofstream(configPath)<<"[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                "[vprenderer]\nquality: fast\n"
                "[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
                "[vprenderer.zoom.native]\nautomatic_crop: true\nsubtitle_fit: false\n"
                "crop_narrower_content_to_fill_screen: false\ncrop_wider_content_to_fill_screen: "
                <<(configuredFill?"true":"false")<<"\n";
            constexpr int width=640,height=360;
            const int scopeBar=configuredFill?46:44;
            const double nativeScope=double(width)/(height-2*scopeBar);
            VideoStateComPtr state=new VideoState();
            state->valid=true;state->videoFrameEncoding=VideoFrameEncoding::P210;
            state->displayMode=std::make_shared<DisplayMode>(width,height,false,60000,1001);
            state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
            std::vector<uint16_t> scope(state->BytesPerFrame()/2,uint16_t(512<<6));
            std::vector<uint16_t> taller=scope,dark=scope;
            for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
                scope[y*width+x]=uint16_t(((y>=scopeBar&&y<height-scopeBar)?300:64)<<6);
                taller[y*width+x]=uint16_t(((y>=12&&y<348)?300:64)<<6);
                dark[y*width+x]=uint16_t(64<<6);
            }
            SnapshotTestCallback callback;NativeP210Buffer buffer;
            struct Cleanup {
                HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;
                const char* config;
                ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(config);}
            } cleanup{CreateWindowExW(0,L"STATIC",L"AR transition fixture",WS_POPUP,
                0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
            Assert::IsNotNull(cleanup.window);
            cleanup.renderer=create(&callback,42,cleanup.window,nullptr,false,1,
                VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,SnapshotTestLog);
            Assert::IsNotNull(cleanup.renderer);
            Assert::IsTrue(cleanup.renderer->OnVideoState(state));
            cleanup.renderer->Build();cleanup.renderer->Start();
            uint64_t sequence=1;
            auto pump=[&](std::vector<uint16_t>& pixels) {
                MSG message;
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
                    {TranslateMessage(&message);DispatchMessageW(&message);}
                if(callback.state==RENDERSTATE_RENDERING) {
                    VideoFrame frame(pixels.data(),sequence++,0,&buffer);
                    cleanup.renderer->OnVideoFrame(frame);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);
                return value;
            };
            auto acquire=[&](std::vector<uint16_t>& pixels,double expected,double applied,const wchar_t* message) {
                const auto first=sequence;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                DetectedPictureAspect value;
                do {
                    value=pump(pixels);
                    if(value.inUseAvailable&&value.available&&!value.provisional&&
                        value.presentedSequence>=first&&value.detectedSequence>=first&&
                        std::abs(value.inUseRatio-applied)<0.015&&std::abs(value.ratio-expected)<0.015) return value;
                }while(std::chrono::steady_clock::now()<deadline&&callback.state!=RENDERSTATE_FAILED);
                Logger::WriteMessage((std::string("AR actual in_use=")+std::to_string(value.inUseRatio)+
                    " detected="+std::to_string(value.ratio)+" provisional="+std::to_string(value.provisional)).c_str());
                Assert::Fail(message);return value;
            };
            const auto established=acquire(scope,nativeScope,configuredFill?2.35:nativeScope,L"Real renderer did not acquire initial native scope and selected presentation");
            const auto darkFirst=sequence;
            unsigned checked=0;
            // Longer than the legacy two-second ambiguity hold, with distinct input identities.
            const auto darkUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(3200);
            while(std::chrono::steady_clock::now()<darkUntil&&callback.state!=RENDERSTATE_FAILED) {
                const auto value=pump(dark);
                if(value.inUseAvailable&&value.presentedSequence>=darkFirst) {
                    ++checked;
                    Assert::AreEqual(established.inUseRatio,value.inUseRatio,0.0005,
                        L"Uncertain source replaced successfully submitted scope geometry");
                }
            }
            Assert::IsTrue(checked>20,L"Dark interval did not exercise enough actual submissions");
            acquire(taller,640.0/336,640.0/336,L"Real renderer retained old scope over verified taller picture");
            acquire(scope,nativeScope,configuredFill?2.35:nativeScope,L"Real renderer did not return to verified scope and selected presentation");
        }
		TEST_METHOD(ActivePictureLookaheadHasExplicitProxyOverride)
		{
			using ExpectedMethod = void (
				LibplaceboPluginVideoRenderer::*)(size_t);
			Assert::IsTrue((std::is_same_v<
				decltype(&LibplaceboPluginVideoRenderer::
					SetActivePictureLookaheadFrames),
				ExpectedMethod>));
		}

		TEST_METHOD(ShutdownFinalizationHasExplicitProxyOverride)
		{
#if defined(__cpp_noexcept_function_type)
			using ExpectedMethod = bool (
				LibplaceboPluginVideoRenderer::*)() noexcept;
#else
			using ExpectedMethod = bool (
				LibplaceboPluginVideoRenderer::*)();
#endif
			Assert::IsTrue((std::is_same_v<
				decltype(&LibplaceboPluginVideoRenderer::
					FinalizeRetirementForShutdown),
				ExpectedMethod>));
		}

		TEST_METHOD(OptionalRendererUsesOnePrivateDependencyDirectory)
		{
			wchar_t temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathW(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::wstring root = std::wstring(temporaryDirectory) +
				L"VideoProcessor-vp0107-" +
				std::to_wstring(GetCurrentProcessId());
			const std::wstring pluginDirectory =
				OptionalRendererLayout::Directory(root);
			std::wstring missing;
			Assert::IsTrue(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			Assert::AreEqual(OptionalRendererLayout::PluginPath(root).c_str(),
				missing.c_str());
			CreateDirectoryW(root.c_str(), nullptr);
			Assert::IsTrue(CreateDirectoryW(pluginDirectory.c_str(), nullptr) != FALSE);

			auto touch = [](const std::wstring& path)
			{
				const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
					nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				Assert::IsTrue(file != INVALID_HANDLE_VALUE);
				CloseHandle(file);
			};
			touch(OptionalRendererLayout::PluginPath(root));
			for (size_t index = 0;
				index + 1 < OptionalRendererLayout::PrivateDependencies().size();
				++index)
			{
				touch(OptionalRendererLayout::Join(pluginDirectory,
					OptionalRendererLayout::PrivateDependencies()[index]));
			}

			const wchar_t* finalDependency =
				OptionalRendererLayout::PrivateDependencies().back();
			// A same-named root/PATH-style decoy must not satisfy the private
			// plugin contract.
			touch(OptionalRendererLayout::Join(root, finalDependency));
			Assert::IsTrue(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			Assert::AreEqual(OptionalRendererLayout::Join(
				pluginDirectory, finalDependency).c_str(),
				missing.c_str());

			touch(OptionalRendererLayout::Join(
				pluginDirectory, finalDependency));
			Assert::IsFalse(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			DeleteFileW(OptionalRendererLayout::Join(root, finalDependency).c_str());
			DeleteFileW(OptionalRendererLayout::PluginPath(root).c_str());
			for (const wchar_t* dependency :
				OptionalRendererLayout::PrivateDependencies())
			{
				DeleteFileW(OptionalRendererLayout::Join(
					pluginDirectory, dependency).c_str());
			}
			RemoveDirectoryW(pluginDirectory.c_str());
			RemoveDirectoryW(root.c_str());
		}
	};
}
