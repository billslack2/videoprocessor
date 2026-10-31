#include "pch.h"
#include "CppUnitTest.h"

#include "../VideoProcessor-Lib/vprenderer/LibplaceboPluginVideoRenderer.h"
#include "../VideoProcessor-Lib/AspectRatioOsd.h"
#include "../VideoProcessor-Lib/vprenderer/OptionalRendererLayout.h"

#include <type_traits>
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
