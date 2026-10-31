#include "pch.h"
#include "CppUnitTest.h"

#include "../VideoProcessor-Lib/vprenderer/LibplaceboPluginVideoRenderer.h"
#include "../VideoProcessor-Lib/AspectRatioOsd.h"
#include "../VideoProcessor-Lib/vprenderer/OptionalRendererLayout.h"

#include <type_traits>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vector>
#include <map>
#include <sstream>
#include <AnalysisLumaSource.h>
#include <SubtitleMeasurementWorker.h>
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
            !strstr(message,"VP source measurement interval:") && !strstr(message,"VP aspect telemetry:") && !strstr(message,"VP subtitle background preparation:") &&
            !strstr(message,"Subtitle shader preparation rejected:"))return;
        std::lock_guard<std::mutex> guard(classicPoseLogMutex);
        classicPoseMessages.emplace_back(message);
    }
    std::vector<AnalysisDiagnosticRecordV1> classicPoseRecords;
    bool classicPoseAllRecordStages=false;
    void __cdecl ClassicPoseTestRecord(void*,const AnalysisDiagnosticRecordV1* record)
    {
        if(!record || record->structSize!=sizeof(*record))return;
        std::lock_guard<std::mutex> guard(classicPoseLogMutex);
        if(!classicPoseAllRecordStages && strcmp(record->stage,"submitted-presentation") && strcmp(record->stage,"presentation") && strcmp(record->stage,"ar-decision") &&
            strcmp(record->stage,"subtitle-output"))return;
        classicPoseRecords.push_back(*record);
    }
    // OSD publication can precede the success-only final record. Consume the last
    // completed selected output in vector order, never an in-flight raw attempt.
    void UseCompletedSubmittedPresentation(DetectedPictureAspect& observed)
    {
        std::lock_guard<std::mutex> guard(classicPoseLogMutex);
        for(auto it=classicPoseRecords.rbegin();it!=classicPoseRecords.rend();++it) {
            if(strcmp(it->stage,"submitted-presentation") || !(it->flags&1u) ||
                it->transportGeneration!=observed.sourceGeneration || it->acceptedSequence>observed.presentedSequence)continue;
            observed.presentedSequence=it->acceptedSequence;
            const double w=it->metrics[10]-it->metrics[8],h=it->metrics[11]-it->metrics[9];
            observed.inUseAvailable=w>0 && h>0;observed.inUseRatio=observed.inUseAvailable?w/h:0;
            observed.inUseState=observed.inUseAvailable?PictureAspectUse::PRESENTATION:PictureAspectUse::UNAVAILABLE;
            return;
        }
        observed.presentedSequence=0;observed.inUseAvailable=false;
    }
    bool IsExactCompletedOutput(const AnalysisDiagnosticRecordV1& record,uint64_t generation,unsigned mode)
    {
        return !strcmp(record.stage,"submitted-presentation") && (record.flags&1u)!=0 &&
            record.transportGeneration==generation && record.rendererGeneration==generation &&
            record.acceptedSequence>0 && record.counters[1]==record.acceptedSequence && record.counters[3]==mode &&
            record.counters[4]>0 && (record.flags&1024u)==0 && std::abs(record.metrics[2])<0.01 &&
            (record.flags&32u)!=0 && (record.flags&64u)!=0 && record.counters[5]==record.acceptedSequence;
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
            const auto diagnostics=reinterpret_cast<VideoProcessorLibplaceboConfigureDiagnosticFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_DIAGNOSTIC_EXPORT));
            Assert::IsNotNull(reinterpret_cast<void*>(diagnostics));
            struct DiagnosticCleanup {VideoProcessorLibplaceboConfigureDiagnosticFn configure;
                ~DiagnosticCleanup(){configure(nullptr,nullptr,nullptr,nullptr);}} diagnosticCleanup{diagnostics};
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseRecords.clear();classicPoseAllRecordStages=false;}
            diagnostics(nullptr,ClassicPoseTestRecord,nullptr,nullptr);
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
            uint64_t sequence=1;size_t finalRecordCursor=0;
            auto messages=[] {std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseMessages;};
            auto dump=[&] {for(const auto& line:messages())Logger::WriteMessage(line.c_str());};
            auto require=[&](bool valid,const wchar_t* message) {if(!valid){dump();Assert::Fail(message);}};
            auto integer=[](const std::string& line,const char* key) {
                const auto at=line.find(key);return at==std::string::npos?uint64_t(0):uint64_t(std::strtoull(line.c_str()+at+strlen(key),nullptr,10));
            };

            struct Geometry {uint64_t sequence=0;double left=0,top=0,right=0,bottom=0,pts=0;unsigned mode=0;bool composed=false,poseEligible=false;int routedTranslation=0;double poseDelta=0;};
            auto geometries=[&] {
                std::vector<Geometry> result;std::vector<AnalysisDiagnosticRecordV1> trace;
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);trace=classicPoseRecords;}
                for(;finalRecordCursor<trace.size();++finalRecordCursor) {
                    const auto& record=trace[finalRecordCursor];
                    if(strcmp(record.stage,"submitted-presentation") || !(record.flags&1u))continue;
                    require(record.acceptedSequence>0 && record.counters[1]==record.acceptedSequence,
                        L"Actual successful output lacks a coherent selected source identity.");
                    Geometry g;g.sequence=record.counters[1];g.pts=record.metrics[0];g.mode=unsigned(record.counters[3]);
                    g.left=record.metrics[8];g.top=record.metrics[9];g.right=record.metrics[10];g.bottom=record.metrics[11];
                    g.composed=(record.flags&2u)!=0;g.poseEligible=(record.flags&1024u)!=0;
                    g.poseDelta=record.metrics[2];g.routedTranslation=int(std::lround(record.metrics[4]));
                    require(std::isfinite(g.pts) && g.pts>0 && std::isfinite(g.left) && std::isfinite(g.top) &&
                        std::isfinite(g.right) && std::isfinite(g.bottom) && g.left<g.right && g.top<g.bottom,
                        L"Actual successful output has invalid native source coordinates or QPC timestamps.");
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
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);UseCompletedSubmittedPresentation(value);return value;
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
            const uint64_t reconstructionFirst=sequence;uint64_t reconstructionReady=0,observationDeadline=0;unsigned lastMode=0;
            std::set<uint64_t> reconstructionSources;const auto preparationDeadline=GetTickCount64()+90000;
            do {
                const auto value=pump(caption);
                for(const auto& g:geometries())if(g.sequence>=reconstructionFirst) {
                    lastMode=g.mode;if(g.mode!=3)continue;
                    require(!g.poseEligible && std::abs(g.poseDelta)<0.01,L"Classic-to-Reconstruction advanced output applied a Classic translation pose.");
                    if(g.composed && same(g,initial) && !reconstructionReady) {
                        reconstructionReady=g.sequence;observationDeadline=GetTickCount64()+5000;
                    }
                    if(reconstructionReady)require(same(g,initial),L"Ready Classic-to-Reconstruction output changed its native presentation.");
                    else require(g.left<=initial.left+0.01 && g.right>=initial.right-0.01 &&
                        g.top<=initial.top+0.01 && g.bottom>=captionBottom-0.01,
                        L"Unready Reconstruction output clipped the current raw caption or native picture instead of preserving outward visibility.");
                }
                if(reconstructionReady && value.presentedSequence>=reconstructionReady) {
                    bool hasPresentation=false,composed=false;
                    std::vector<AnalysisDiagnosticRecordV1> trace;
                    {std::lock_guard<std::mutex> lock(classicPoseLogMutex);trace=classicPoseRecords;}
                    for(const auto& record:trace)if(record.acceptedSequence==value.presentedSequence) {
                        if(!strcmp(record.stage,"submitted-presentation"))composed=(record.flags&2u)!=0;
                        if(!strcmp(record.stage,"submitted-presentation")) {
                            hasPresentation=true;
                            require(IsExactCompletedOutput(record,value.sourceGeneration,3),L"Reconstruction final output lacks current mode/source/certificate identity.");
                            require(record.displayed[0]==initial.left && record.displayed[1]==initial.top &&
                                record.displayed[2]==initial.right && record.displayed[3]==initial.bottom,
                                L"Prepared Reconstruction exact-source output lost native picture presentation.");
                        }
                    }
                    require(hasPresentation,L"Prepared Reconstruction lacks exact-source presentation diagnostics.");
                    if(lastMode==3 && composed)reconstructionSources.insert(value.presentedSequence);
                }
                if(reconstructionSources.size()>=24)break;
            }while(GetTickCount64()<(reconstructionReady?observationDeadline:preparationDeadline) && callback.state!=RENDERSTATE_FAILED);
            require(reconstructionReady>0,L"Reconstruction did not acquire an actual composed native output within the bounded cold-preparation budget.");
            require(reconstructionSources.size()>=24,L"Ready Reconstruction did not present 24 distinct actually composed native sources within five seconds.");
        }

        void VerifyClassicUhdBarOverlay(bool beta,bool bothBars,bool reconstruction=false,bool nativeV210=false,bool crossingEdge=false,bool nativePreflightOnly=false,bool blackMode=false)
        {
            constexpr bool narrower=false;
            const unsigned advancedMode=static_cast<unsigned>(blackMode?SubtitlePreviewMode::BlackBackground:SubtitlePreviewMode::GeneratedGrayBackground);
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
            const auto diagnostics=reinterpret_cast<VideoProcessorLibplaceboConfigureDiagnosticFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_DIAGNOSTIC_EXPORT));
            Assert::IsNotNull(reinterpret_cast<void*>(diagnostics));
            struct DiagnosticCleanup {VideoProcessorLibplaceboConfigureDiagnosticFn configure;
                ~DiagnosticCleanup(){configure(nullptr,nullptr,nullptr,nullptr);}} diagnosticCleanup{diagnostics};
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseRecords.clear();classicPoseAllRecordStages=false;}
            diagnostics(nullptr,ClassicPoseTestRecord,nullptr,nullptr);
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
                "[vprenderer.subtitles.reconstruction]\ntype: "<<(blackMode?"black":"generated_gray")<<"\nshortcut: F6\noffset_pixels: 10\n"
                "text_reduction_percent: 30\ncleanup_fill: stable_surround\nsubtitle_hold_ms: 250\nsubtitle_near_bar_px: 20\nsubtitle_generated_gray_color: 000000\nsubtitle_generated_gray_max_luminance: .25\nsubtitle_generated_gray_opacity: .65\nsubtitle_generated_gray_border_width: 1\nsubtitle_generated_gray_border_opacity: .4\nsubtitle_generated_gray_blur_px: 30\nrounded_corners: true\nminimum_headroom_pixels: 30\n";
            constexpr int width=3840,height=2160;
            const int captionHeight=crossingEdge?50:22;
            const int bar=250,pictureBottom=1906;
            const int captionBottom=1940,captionTop=captionBottom-captionHeight;
            VideoStateComPtr state=new VideoState();state->valid=true;state->videoFrameEncoding=nativeV210?VideoFrameEncoding::V210:VideoFrameEncoding::P210;
            state->displayMode=std::make_shared<DisplayMode>(width,height,false,narrower?24000:60000,1001);
            state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
            std::vector<uint16_t> native(size_t(width)*height*2,uint16_t(512<<6));
            auto paintPicture=[&](std::vector<uint16_t>& pixels,int top,int bottom) {
                for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                    pixels[size_t(y)*width+x]=uint16_t((y>=top&&y<bottom?300:64)<<6);
            };
            paintPicture(native,bar,pictureBottom);
            // Cross-edge Latin acquisition requires distributed dark scene backing.
            // This full-width native scene band is ABOVE the detector's maximum
            // black cutoff104, below the existing glyph backing limit133, and has
            // no finite subtitle-card boundary at the physical bar.
            if(crossingEdge)for(int y=1840;y<pictureBottom;++y)for(int x=0;x<width;++x)
                native[size_t(y)*width+x]=uint16_t(128<<6);
            auto caption=native;auto expanded=native;paintPicture(expanded,8,height-8);
            for(int glyph=0;glyph<12;++glyph)for(int y=captionTop;y<captionBottom;++y)
                for(int x=1400+glyph*64;x<1432+glyph*64;++x)
                    if(x<1406+glyph*64 || x>=1426+glyph*64 || (y>=captionTop+captionHeight/2-2&&y<captionTop+captionHeight/2+2))
                        caption[size_t(y)*width+x]=uint16_t(892<<6);
            auto both=caption;
            // Pixel owners outlive Cleanup, which drains renderer references first.
            std::vector<uint16_t> residual[2],motionOnly[2];
            std::map<const uint16_t*,std::vector<uint32_t>> packedFrames;
            // Sparse bar UI crosses the trusted top boundary, as in the live
            // 134..262 top row; the separate bottom cue remains present.
            for(int glyph=0;glyph<16;++glyph)for(int y=134;y<262;++y)
                for(int x=158+glyph*144;x<238+glyph*144;++x)
                    if(x<174+glyph*144 || x>=222+glyph*144 || (y>=190&&y<206))
                        both[size_t(y)*width+x]=uint16_t(892<<6);
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
            Assert::IsFalse(initialFill.applied || translatedFill.applied,
                L"Live native aspect must remain just below the configured narrower-fill limit.");

            if(reconstruction) {
                residual[0]=caption;residual[1]=caption;
                for(int phase=0;phase<2;++phase) {
                    for(int y=bar;y<pictureBottom;++y)for(int x=0;x<width;++x)
                        residual[phase][size_t(y)*width+x]=uint16_t(((crossingEdge && y>=1840)?
                            110+18*((x/64+y/64+phase)&1):300+40*((x/64+y/64+phase)&1))<<6);
                    for(int glyph=0;glyph<12;++glyph)for(int y=captionTop;y<captionBottom;++y)
                        for(int x=1400+glyph*64;x<1432+glyph*64;++x)
                            if(x<1406+glyph*64 || x>=1426+glyph*64 || (y>=captionTop+captionHeight/2-2&&y<captionTop+captionHeight/2+2))
                                residual[phase][size_t(y)*width+x]=uint16_t(892<<6);
                    motionOnly[phase]=residual[phase];
                    // An off-center solid UI badge is not a subtitle glyph row.
                    // Bottom text ownership cannot authorize erasing this badge.
                    for(int y=140;y<200;++y)for(int x=40;x<220;++x)
                        residual[phase][size_t(y)*width+x]=uint16_t(892<<6);
                }

            }
            if(nativeV210) {
                const size_t pitch=state->BytesPerRow();
                Assert::IsTrue(pitch==10240,L"UHD native V210 capture must use the live 10240-byte stride.");
                auto pack=[&](const std::vector<uint16_t>& pixels) {
                    auto& words=packedFrames[pixels.data()];words.resize(pitch*height/4);
                    for(int y=0;y<height;++y)for(int x=0;x<width;x+=6) {
                        const auto yv=[&](int i){return uint32_t(pixels[size_t(y)*width+x+i]>>6);};
                        const auto uv=[&](int i){return uint32_t(pixels[size_t(width)*height+size_t(y)*width+x+i]>>6);};
                        auto* out=words.data()+size_t(y)*pitch/4+size_t(x/6)*4;
                        out[0]=uv(0)|(yv(0)<<10)|(uv(1)<<20);
                        out[1]=yv(1)|(uv(2)<<10)|(yv(2)<<20);
                        out[2]=uv(3)|(yv(3)<<10)|(uv(4)<<20);
                        out[3]=yv(4)|(uv(5)<<10)|(yv(5)<<20);
                    }
                    AnalysisLumaSource source{reinterpret_cast<const uint8_t*>(words.data()),words.size()*4,width,height,
                        pitch,0,AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,1};
                    // Verify all six packing slots, glyph edges and chroma directly.
                    for(int y:{0,134,140,199,249,250,captionTop,captionBottom-1,1905,1906,2159})
                        for(int x:{0,1,2,3,4,5,40,157,158,173,219,1400,1405,1406,1431,3839}) {
                            AnalysisLumaSample p;Assert::IsTrue(source.Sample(x,y,p),L"Native V210 fixture sample was unavailable.");
                            Assert::IsTrue(p.luma==(pixels[size_t(y)*width+x]>>6) && p.chromaU==512 && p.chromaV==512,
                                L"Native V210 fixture changed a 10-bit source luma/chroma sample.");
                        }
                };
                pack(native);pack(caption);pack(both);
                if(reconstruction){pack(residual[0]);pack(residual[1]);pack(motionOnly[0]);pack(motionOnly[1]);}
            }
            if(reconstruction) {
                // Exercise the exact source producer and native worker contract before
                // allocating a GPU renderer or waiting for shader preparation.
                AnalysisLumaSource pixels;
                if(nativeV210) {
                    const auto& packed=packedFrames.at(caption.data());
                    pixels={reinterpret_cast<const uint8_t*>(packed.data()),packed.size()*4,width,height,state->BytesPerRow(),0,
                        AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
                } else pixels={reinterpret_cast<const uint8_t*>(caption.data()),caption.size()*2,width,height,
                    size_t(width)*2,size_t(width)*2,AnalysisLumaFormat::P210,VideoFrameEncoding::P210,ColorSpace::REC_709,7};
                auto sampler=std::make_unique<SubtitleMeasurementSampler>();
                auto observations=std::make_unique<std::vector<SubtitleBoxObservation>>();observations->reserve(4);
                SubtitleBarTrackingReference prior;
                const auto firstTick=clock.TimingClockNow();
                for(uint64_t index=0;index<4;++index) {
                    auto key=std::make_unique<SubtitleBoxObservation>();
                    key->identity.transportGeneration=7;key->identity.rendererGeneration=7;
                    key->identity.acceptedSequence=key->identity.sourceFrameNumber=1+index*2;
                    key->identity.captureTimestamp=firstTick+index*clock.TimingClockTicksPerSecond()*1001/30000;
                    key->identity.sourceFormatGeneration=17;key->identity.analysisDomainKey=19;key->identity.viewportGeneration=5;
                    key->width=width;key->height=height;key->policyGeneration=1;key->continuityGeneration=1;
                    key->analysisStride=2;key->nearBarDistance=20;key->optimizationMode=0;key->scriptHint=SubtitleScriptHint::Latin;
                    key->sharedPicture.required=true;key->sharedPicture.bounds=physical;key->sharedPicture.identity=key->identity;
                    auto measured=sampler->Measure(pixels,*key,prior,60000.0/1001);
                    Logger::WriteMessage((std::string("UHD native fixture preflight: encoding=")+(nativeV210?"V210":"P210")+
                        " cross="+std::to_string(crossingEdge)+" sequence="+std::to_string(key->identity.acceptedSequence)+
                        " analyzed="+std::to_string(measured.analyzed)+" bar_authority="+std::to_string(measured.barAuthority)+
                        " search="+std::to_string(measured.text.topSearchComplete)+"/"+std::to_string(measured.text.bottomSearchComplete)+
                        " components="+std::to_string(measured.text.diagnosticComponentCount)+" lines="+std::to_string(measured.text.lineCount)+
                        " reason="+measured.text.diagnosticReason+" bars="+measured.barEvidence.reason).c_str());
                    prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(measured);
                    observations->push_back(std::move(measured));
                }
                for(const auto& observed:*observations)Assert::IsTrue(observed.analyzed && observed.barAuthority &&
                    observed.text.detected && !observed.text.workLimit && observed.text.topSearchComplete && observed.text.bottomSearchComplete,
                    L"Exact native subtitle fixture failed complete current acquisition; see preflight reasons. GPU run deliberately not started.");
                auto preview=std::make_unique<SubtitleBoxPreview>();
                *preview=SubtitleBoxLookahead::Resolve(observations->data(),observations->size(),1,1);
                auto presentation=std::make_unique<SubtitleBoxPresentation>();
                presentation->RefreshReferencePixels(*preview,pixels);
                const auto acquired=presentation->Consume(*preview,250,1001.0/60);
                Logger::WriteMessage((std::string("UHD native four-frame acquisition: ")+presentation->DecisionReason()+
                    " lines="+std::to_string(acquired.lineCount)+" detected="+std::to_string(acquired.detected)+
                    " pending="+std::to_string(acquired.confirmationPending)).c_str());
                Assert::IsTrue(acquired.detected && !acquired.held && !acquired.confirmationPending,
                    L"Exact native fixture did not acquire a strong four-frame current cue; GPU run deliberately not started.");
                if(crossingEdge) {
                    auto sourceFor=[&](const std::vector<uint16_t>& frame) {
                        if(nativeV210) {
                            const auto& words=packedFrames.at(frame.data());
                            return AnalysisLumaSource{reinterpret_cast<const uint8_t*>(words.data()),words.size()*4,width,height,
                                state->BytesPerRow(),0,AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
                        }
                        return AnalysisLumaSource{reinterpret_cast<const uint8_t*>(frame.data()),frame.size()*2,width,height,
                            size_t(width)*2,size_t(width)*2,AnalysisLumaFormat::P210,VideoFrameEncoding::P210,ColorSpace::REC_709,7};
                    };
                    const auto raw=ExtractActivePictureEvidence(sourceFor(native));
                    Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
                        raw.trustedBounds.left==0 && raw.trustedBounds.top==bar && raw.trustedBounds.right==width && raw.trustedBounds.bottom==pictureBottom,
                        L"Dark native scene backing must not become an encoded bar or change measured physical boundaries.");
                    auto padding=std::make_unique<SubtitlePaddingGuard>();SubtitlePaddingPolicy policy;
                    policy.glyphRelative=true;policy.minimumHeadroomPixels=30;
                    const auto safe=padding->Consume(acquired,*preview,{60,40,21},policy);const auto display=padding->DisplayBounds();
                    auto compositor=std::make_unique<SubtitleCutPastePresentation>();
                    auto geometry=compositor->Consume(acquired,*preview,safe,10,&display);
                    geometry=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(geometry,{0,bar,width,pictureBottom},10),30,safe,true,false);
                    Assert::IsTrue(geometry.valid,L"Cross-edge preflight acquired no real fitted/reduced composition geometry.");
                    bool intersects=false;
                    auto changedInside=[&](const SubtitleBoxRect& r) {
                        for(int y=(std::max)(bar,r.top);y<(std::min)(pictureBottom,r.bottom);++y)
                            for(int x=(std::max)(0,r.left);x<(std::min)(width,r.right);++x)
                                if(caption[size_t(y)*width+x]!=motionOnly[1][size_t(y)*width+x])return true;
                        return false;
                    };
                    if(geometry.generatedCleanupRowCount) {
                        for(unsigned row=0;row<geometry.generatedCleanupRowCount;++row)intersects|=changedInside(geometry.generatedCleanupRows[row]);
                    } else if(geometry.generatedCleanup.Valid())intersects=changedInside(geometry.generatedCleanup);
                    Logger::WriteMessage((std::string("Cross-edge preflight cleanup: complete_card=")+std::to_string(acquired.provenSourcePanel.Valid())+
                        " cleanup="+std::to_string(geometry.generatedCleanup.left)+","+std::to_string(geometry.generatedCleanup.top)+"-"+
                        std::to_string(geometry.generatedCleanup.right)+","+std::to_string(geometry.generatedCleanup.bottom)+
                        " rows="+std::to_string(geometry.generatedCleanupRowCount)+" actual_changed_row_intersection="+std::to_string(intersects)).c_str());
                    Assert::IsTrue(intersects,L"Acquired cross-edge fixture has no real erase-row intersection with native motion; GPU coverage not established.");
                    auto guard=std::make_unique<SubtitleCadenceGuard>();
                    Assert::IsTrue(guard->RememberAccepted(*preview,acquired,pixels,physical,geometry,1001.0/60,250,blackMode?3:5),
                        L"Cross-edge real native geometry failed cadence seeding before GPU initialization.");
                    auto skipped=std::make_unique<SubtitleBoxPreview>();skipped->policyGeneration=1;skipped->continuityGeneration=1;
                    skipped->current=observations->front();auto& current=skipped->current;
                    current.identity.acceptedSequence=2;current.identity.sourceFrameNumber=2;
                    current.identity.captureTimestamp=firstTick+clock.TimingClockTicksPerSecond()*1001/60000;
                    current.sharedPicture.identity=current.identity;current.analyzed=false;current.cadenceSkipped=true;
                    current.text={};current.ink.reset();current.barAuthority=false;current.independentBarAnalysis=false;
                    auto proof=std::make_unique<SubtitleCadenceProof>();*proof=guard->Prepare(*skipped,sourceFor(motionOnly[1]),physical,1001.0/60,250);
                    Logger::WriteMessage((std::string("Cross-edge native motion proof: reason=")+proof->reason+" samples="+std::to_string(proof->samples)).c_str());
                    Assert::IsTrue(!proof->valid && !strcmp(proof->reason,"cadence-current-cleanup-changed") && proof->samples>0,
                        L"Native cross-edge motion did not demonstrate sampled unowned actual-cleanup change; GPU coverage not established.");
                }
                if(nativePreflightOnly)return;
            }
            SnapshotTestCallback callback;NativeP210Buffer buffer;
            struct Cleanup {
                HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;const char* config;
                ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(config);}
            } cleanup{CreateWindowExW(0,L"STATIC",L"Classic UHD simultaneous bar UI",WS_POPUP,0,0,2000,1125,
                nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
            Assert::IsNotNull(cleanup.window);
            cleanup.renderer=create(&callback,44,cleanup.window,&clock,true,16,VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,ClassicPoseTestLog);
            Assert::IsNotNull(cleanup.renderer);
            ConfigFile config;Assert::IsTrue(config.Load(configPath));UnifiedProfileRuntime::Runtime runtime;std::string error;
            const bool initialized=runtime.Initialize(config,{},error);
            if(!initialized)Logger::WriteMessage(error.c_str());
            Assert::IsTrue(initialized,std::wstring(error.begin(),error.end()).c_str());
            CString active;bool restart=false,reset=false;
            Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*runtime.GetSnapshot(),active,restart,reset));
            Assert::IsFalse(restart);
            cleanup.renderer->SetQueueFramePolicy(4,9,true);
            cleanup.renderer->SetActivePictureLookaheadFrames(4);
            Assert::IsTrue(cleanup.renderer->OnVideoState(state));cleanup.renderer->Build();cleanup.renderer->Start();
            uint64_t sequence=1;size_t finalRecordCursor=0;
            auto messages=[] {std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseMessages;};
            auto dump=[&] {for(const auto& line:messages())Logger::WriteMessage(line.c_str());};
            auto require=[&](bool valid,const wchar_t* message) {if(!valid){dump();Assert::Fail(message);}};
            auto integer=[](const std::string& line,const char* key) {
                const auto at=line.find(key);return at==std::string::npos?uint64_t(0):uint64_t(std::strtoull(line.c_str()+at+strlen(key),nullptr,10));
            };

            struct Geometry {uint64_t sequence=0;double left=0,top=0,right=0,bottom=0,pts=0;unsigned mode=0;bool composed=false,poseEligible=false;int routedTranslation=0;double poseDelta=0;};
            auto geometries=[&] {
                std::vector<Geometry> result;std::vector<AnalysisDiagnosticRecordV1> trace;
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);trace=classicPoseRecords;}
                for(;finalRecordCursor<trace.size();++finalRecordCursor) {
                    const auto& record=trace[finalRecordCursor];
                    if(strcmp(record.stage,"submitted-presentation") || !(record.flags&1u))continue;
                    require(record.acceptedSequence>0 && record.counters[1]==record.acceptedSequence,
                        L"Actual successful output lacks a coherent selected source identity.");
                    Geometry g;g.sequence=record.counters[1];g.pts=record.metrics[0];g.mode=unsigned(record.counters[3]);
                    g.left=record.metrics[8];g.top=record.metrics[9];g.right=record.metrics[10];g.bottom=record.metrics[11];
                    g.composed=(record.flags&2u)!=0;g.poseEligible=(record.flags&1024u)!=0;
                    g.poseDelta=record.metrics[2];g.routedTranslation=int(std::lround(record.metrics[4]));
                    require(std::isfinite(g.pts) && g.pts>0 && std::isfinite(g.left) && std::isfinite(g.top) &&
                        std::isfinite(g.right) && std::isfinite(g.bottom) && g.left<g.right && g.top<g.bottom,
                        L"Actual successful output has invalid native source coordinates or QPC timestamps.");
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
                if(callback.state==RENDERSTATE_RENDERING){const void* data=nativeV210?static_cast<const void*>(packedFrames.at(pixels.data()).data()):static_cast<const void*>(pixels.data());VideoFrame frame(data,sequence++,clock.TimingClockNow(),&buffer);cleanup.renderer->OnVideoFrame(frame);}
                nextSource+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(sourcePeriod);
                std::this_thread::sleep_until(nextSource);
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);UseCompletedSubmittedPresentation(value);return value;
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
            uint64_t acceptedNativeSequence=0;
            auto nativeMatches=[&](const std::string& line) {
                Geometry g;const auto at=line.find(" native_rect=");
                return at!=std::string::npos && sscanf_s(line.c_str()+at+13,"%lf,%lf-%lf,%lf",
                    &g.left,&g.top,&g.right,&g.bottom)==4 && same(g,physical);
            };
            for(const auto& line:messages())if(line.find("Alpha native presentation commit:")!=std::string::npos && nativeMatches(line))
                acceptedNativeSequence=integer(line," accepted=");
            require(acceptedNativeSequence>0,L"UHD fixture did not acquire a source-owned accepted native contract.");
            auto verifyNativeContract=[&](uint64_t first) {
                for(const auto& line:messages())if(line.find("Alpha native presentation commit:")!=std::string::npos &&
                    integer(line," selected=")>=first)
                    require(integer(line," native=")==1 && nativeMatches(line) &&
                        integer(line," accepted=")==acceptedNativeSequence,
                        L"Sparse UI changed the independently accepted native picture contract.");
            };
            Logger::WriteMessage("Classic UHD live reproduction: bottom cue with no optional fill\n");
            const uint64_t onset=sequence;uint64_t settledFirst=0;double previousTop=initial.top;
            std::set<uint64_t> settledSources;std::set<int> intermediate;
            const auto engageDeadline=GetTickCount64()+10000;
            do {
                const auto value=pump(caption);
                for(const auto& g:geometries())if(g.sequence>=onset) {
                    require(g.mode==0 && sameSize(g),L"UHD Classic bottom cue resized its no-fill native viewport.");
                    require(g.top>=previousTop-0.01 && g.top>=initial.top-0.01 && g.top<=settled.top+0.01,
                        L"UHD Classic bottom-cue drift reversed or left its measured translation interval.");
                    previousTop=g.top;
                    if(g.top>initial.top+0.01 && g.top<settled.top-0.01)intermediate.insert(int(std::lround(g.top)));
                    if(same(g,settled) && !settledFirst)settledFirst=g.sequence;
                }
                if(settledFirst && value.presentedSequence>=settledFirst)settledSources.insert(value.presentedSequence);
                if(settledSources.size()>=24 && scans(onset))break;
            }while(GetTickCount64()<engageDeadline && callback.state!=RENDERSTATE_FAILED);
            require(settledSources.size()>=24 && intermediate.size()>=2 && scans(onset),
                L"UHD Classic no-fill bottom cue failed its timed ramp, 24-source stable pose, or natural dense cadence.");
            if(reconstruction) {
                UnifiedProfileRuntime::SelectionResult selection;
                require(runtime.SelectKey("F6",{},selection,error) && selection.changed,
                    L"UHD Reconstruction fixture could not select its actual application profile.");
                restart=reset=false;
                require(cleanup.renderer->ApplyApplicationState(*selection.snapshot,active,restart,reset) && !restart,
                    L"UHD Reconstruction profile was not accepted as a live renderer update.");
                const uint64_t requested=sequence;uint64_t ready=0,observationDeadline=0,warmSettle=0;unsigned lastMode=0;
                bool warmPinned=false;int32_t warmBounds[4]={};
                std::set<uint64_t> warmSources;const auto preparationDeadline=GetTickCount64()+90000;
                auto records=[] {std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseRecords;};
                do {
                    const auto value=pump(caption);
                    for(const auto& g:geometries())if(g.sequence>=requested) {
                        lastMode=g.mode;
                        if(g.mode==advancedMode) {
                            require(!g.poseEligible && std::abs(g.poseDelta)<0.01,L"UHD advanced preparation applied a Classic translation pose.");
                            if(g.composed && same(g,initial) && !ready) {
                                ready=g.sequence;observationDeadline=GetTickCount64()+5000;warmSettle=GetTickCount64()+500;
                            }
                            if(ready && !crossingEdge)require(same(g,initial),L"Ready UHD Reconstruction changed its native presentation.");
                            else if(ready)require(g.left<=0.01 && g.right>=width-0.01 && g.top<=bar+0.01 &&
                                g.bottom>=(g.composed?pictureBottom:captionBottom)-0.01,
                                L"Ready cross-edge output hid native picture or current uncomposed caption pixels.");
                            else require(g.left<=initial.left+0.01 && g.right>=initial.right-0.01 &&
                                g.top<=initial.top+0.01 && g.bottom>=captionBottom-0.01,
                                L"Unready UHD advanced output hid current raw caption pixels or native picture instead of preserving a safe visibility envelope.");
                        }
                    }
                    if(ready && value.presentedSequence>=ready) {
                        const auto trace=records();const AnalysisDiagnosticRecordV1* presentation=nullptr;

                        for(const auto& record:trace)if(record.acceptedSequence==value.presentedSequence) {
                            if(!strcmp(record.stage,"submitted-presentation"))presentation=&record;

                        }
                        require(presentation && IsExactCompletedOutput(*presentation,value.sourceGeneration,advancedMode),
                            L"Prepared UHD output lacks exact final source/mode/certificate identity.");
                        if(!crossingEdge) {
                            require(presentation->displayed[0]==initial.left && presentation->displayed[1]==initial.top &&
                                presentation->displayed[2]==initial.right && presentation->displayed[3]==initial.bottom,
                                L"Prepared UHD Reconstruction changed its native presentation during the observation window.");
                            if(lastMode==advancedMode && (presentation->flags&2u)!=0)warmSources.insert(value.presentedSequence);
                        } else {
                            const bool composed=(presentation->flags&2u)!=0;
                            require(presentation->published[0]==0 && presentation->published[1]==bar &&
                                presentation->published[2]==width && presentation->published[3]==pictureBottom,
                                L"Cross-edge initialization changed accepted native picture geometry.");
                            require(presentation->displayed[0]<=0 && presentation->displayed[2]>=width &&
                                presentation->displayed[1]<=bar && presentation->displayed[3]>=(composed?pictureBottom:captionBottom),
                                L"Cross-edge initialization lost current source content visibility.");
                            verifyNativeContract(requested);
                            if(GetTickCount64()>=warmSettle) {
                                if(!warmPinned){for(int e=0;e<4;++e)warmBounds[e]=presentation->displayed[e];warmPinned=true;}
                                for(int e=0;e<4;++e)require(warmBounds[e]==presentation->displayed[e],
                                    L"Cross-edge initialization alternated its settled safe viewport across composition states.");
                                if(lastMode==advancedMode)warmSources.insert(value.presentedSequence);
                            }
                        }
                    }
                    if(warmSources.size()>=24)break;
                }while(GetTickCount64()<(ready?observationDeadline:preparationDeadline) && callback.state!=RENDERSTATE_FAILED);
                if(!ready) {
                    std::string preparation="no shader preparation completion/rejection logged",acquisition="no raw acquisition result logged";
                    for(const auto& line:messages()) {
                        if(line.find("VP subtitle background preparation:")!=std::string::npos ||
                            line.find("Subtitle shader preparation rejected:")!=std::string::npos)preparation=line;
                        if(line.find("SUBTITLE BBOX:")!=std::string::npos)acquisition=line;
                    }
                    Logger::WriteMessage((std::string("UHD first composed native-output readiness expired: last_actual_mode=")+
                        std::to_string(lastMode)+"; "+preparation+"; "+acquisition).c_str());
                }
                require(ready>0,L"UHD Reconstruction acquired no actual mode3 composed native output within its bounded 90-second cold-preparation budget.");
                require(warmSources.size()>=24,crossingEdge?
                    L"Ready cross-edge Reconstruction did not retain one safe viewport for 24 distinct sources after acquisition.":
                    L"Ready UHD Reconstruction did not present 24 distinct actually composed native sources within its separate five-second observation budget.");
                bool cleanupIntersection=false,cleanupChangedRejection=false;
                auto inspectMovingCleanup=[&](uint64_t first,uint64_t presented) {
                    for(const auto& line:messages()) {
                        const auto frame=integer(line," frame=");if(frame<first || frame>presented)continue;
                        if(line.find("SUBTITLE CADENCE:")!=std::string::npos &&
                            line.find(" reason=cadence-current-cleanup-changed ")!=std::string::npos && integer(line," samples=")>0)
                            cleanupChangedRejection=true;
                        if(line.find("SUBTITLE CAPTURE:")!=std::string::npos) {
                            const auto at=line.find(" cleanup=");int l=0,t=0,r=0,d=0;
                            if(at!=std::string::npos && sscanf_s(line.c_str()+at+9,"%d,%d-%d,%d",&l,&t,&r,&d)==4)
                                for(int y=(std::max)(bar,t);y<(std::min)(pictureBottom,d) && !cleanupIntersection;++y)
                                    for(int x=(std::max)(0,l);x<(std::min)(width,r);++x)
                                        if(motionOnly[0][size_t(y)*width+x]!=motionOnly[1][size_t(y)*width+x]){cleanupIntersection=true;break;}
                        }
                    }
                };
                if(crossingEdge) {
                    {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseAllRecordStages=true;}
                    const uint64_t motionFirst=sequence;const auto motionSettle=GetTickCount64()+500,motionDeadline=GetTickCount64()+12000;
                    std::set<uint64_t> stableMotion,composedMotion,uncomposedMotion;bool pinned=false;int32_t fixed[4]={};unsigned phase=0;
                    do {
                        const auto value=pump(motionOnly[phase++%2]);
                        for(const auto& g:geometries())if(g.sequence>=motionFirst)
                            require(g.mode==advancedMode && !g.poseEligible && std::abs(g.poseDelta)<0.01 && g.left<=0.01 && g.right>=width-0.01 &&
                                g.top<=bar+0.01 && g.bottom>=(g.composed?pictureBottom:captionBottom)-0.01,
                                L"Moving cross-edge caption output clipped native picture or an uncomposed source cue.");
                        if(value.presentedSequence>=motionFirst) {
                            const auto trace=records();const AnalysisDiagnosticRecordV1* p=nullptr;
                            for(const auto& r:trace)if(r.acceptedSequence==value.presentedSequence) {
                                if(!strcmp(r.stage,"submitted-presentation"))p=&r;
                            }
                            require(p && IsExactCompletedOutput(*p,value.sourceGeneration,advancedMode),
                                L"Moving cross-edge output lacks exact final source/mode/certificate identity.");
                            const bool composed=(p->flags&2u)!=0;
                            require(p->published[0]==0 && p->published[1]==bar && p->published[2]==width && p->published[3]==pictureBottom,
                                L"Motion in the source erasure footprint changed accepted native picture geometry.");
                            const bool currentVisible=p->displayed[0]<=0 && p->displayed[2]>=width && p->displayed[1]<=bar &&
                                p->displayed[3]>=(composed?pictureBottom:captionBottom);
                            if(!currentVisible) {
                                std::ostringstream report;
                                report<<"CROSS EXACT-SOURCE FAILURE: presented="<<value.presentedSequence<<" next_input="<<sequence
                                    <<" source_generation="<<value.sourceGeneration<<" publication="<<value.publicationGeneration
                                    <<" detected_sequence="<<value.detectedSequence<<" detected_ratio="<<value.ratio<<" provisional="<<value.provisional
                                    <<" actual_in_use="<<value.inUseRatio<<" in_use_state="<<static_cast<unsigned>(value.inUseState)
                                    <<" qpc_now="<<clock.TimingClockNow()<<" qpc_frequency="<<clock.TimingClockTicksPerSecond()
                                    <<" phase="<<phase<<" composed_from_subtitle_output="<<composed
                                    <<" selected_p_flags="<<p->flags<<" selected_p_reason="<<p->reason
                                    <<" selected_final_flags="<<p->flags<<" selected_final_processing="<<p->counters[0]
                                    <<" selected_final_selected="<<p->counters[1]<<" selected_final_reason="<<p->reason;
                                Logger::WriteMessage(report.str().c_str());
                                // Preserve every captured stage for this exact source,
                                // including duplicate stages and their instance/context.
                                const auto failureTrace=records();
                                for(const auto& r:failureTrace)if(r.acceptedSequence==value.presentedSequence) {
                                    std::ostringstream row;
                                    row<<"CROSS RECORD: stage="<<r.stage<<" record_sequence="<<r.recordSequence<<" timestamp_ns="<<r.timestampNs
                                        <<" instance="<<r.rendererInstanceKey<<" frame_identity="<<r.frameIdentity<<" accepted="<<r.acceptedSequence
                                        <<" transport="<<r.transportGeneration<<" frame="<<r.sourceFrameNumber<<" capture_qpc="<<r.captureTimestamp
                                        <<" format="<<r.sourceFormatGeneration<<" domain="<<r.analysisDomainKey<<" viewport="<<r.viewportGeneration
                                        <<" renderer="<<r.rendererGeneration<<" policy="<<r.policyGeneration<<" continuity="<<r.continuityGeneration
                                        <<" flags="<<r.flags<<" result="<<r.resultCode<<" cadence_repeat="<<r.cadenceRepeat
                                        <<" reason="<<r.reason<<" detail="<<r.detail;
                                    const auto rect=[&](const char* label,const int32_t* b){row<<' '<<label<<'='<<b[0]<<','<<b[1]<<'-'<<b[2]<<','<<b[3];};
                                    rect("published",r.published);rect("displayed",r.displayed);rect("candidate",r.candidate);rect("subtitle",r.subtitle);
                                    for(int i=0;i<8;++i)row<<" counter"<<i<<'='<<r.counters[i];
                                    for(int i=0;i<24;++i)row<<" metric"<<i<<'='<<r.metrics[i];
                                    Logger::WriteMessage(row.str().c_str());
                                }
                                std::string latest="no submitted geometry log",selected="no geometry at/before selected source";
                                for(const auto& line:messages())if(line.find("VP submitted geometry:")!=std::string::npos) {
                                    latest=line;if(integer(line," selected=")<=value.presentedSequence)selected=line;
                                }
                                Logger::WriteMessage((std::string("CROSS LAST GEOMETRY AT/BEFORE PRESENTED: ")+selected).c_str());
                                Logger::WriteMessage((std::string("CROSS LATEST SUBMITTED GEOMETRY: ")+latest).c_str());
                            }
                            require(currentVisible,L"Moving source erasure footprint lost current picture/caption visibility.");
                            if(GetTickCount64()>=motionSettle) {
                                if(!pinned){for(int e=0;e<4;++e)fixed[e]=p->displayed[e];pinned=true;}
                                for(int e=0;e<4;++e)require(fixed[e]==p->displayed[e],
                                    L"Bottom-only moving cleanup alternated composed/uncomposed viewport size or position after acquisition.");
                                stableMotion.insert(value.presentedSequence);
                                if(composed)composedMotion.insert(value.presentedSequence);else uncomposedMotion.insert(value.presentedSequence);
                            }
                            inspectMovingCleanup(motionFirst,value.presentedSequence);
                        }
                        if(stableMotion.size()>=24 && !composedMotion.empty() && !uncomposedMotion.empty() && cleanupIntersection && cleanupChangedRejection)break;
                    }while(GetTickCount64()<motionDeadline && callback.state!=RENDERSTATE_FAILED);
                    Logger::WriteMessage((std::string("Cross-edge bottom-only motion: stable=")+std::to_string(stableMotion.size())+
                        " composed="+std::to_string(composedMotion.size())+" uncomposed="+std::to_string(uncomposedMotion.size())+
                        " cleanup_hull_intersection="+std::to_string(cleanupIntersection)+" actual_erase_change_rejection="+std::to_string(cleanupChangedRejection)).c_str());
                    require(cleanupIntersection && cleanupChangedRejection,
                        L"No actual cleanup-row movement proved: require intersecting generated cleanup plus sampled cadence-current-cleanup-changed; no moving-erasure coverage established.");
                    require(stableMotion.size()>=24 && !composedMotion.empty() && !uncomposedMotion.empty(),
                        L"Bottom-only moving cleanup did not preserve one stable viewport over 24 distinct sources spanning naturally composed and uncomposed outputs.");
                }
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseAllRecordStages=false;}
                const uint64_t residualFirst=sequence;std::set<uint64_t> residualSources,composedSources,uncomposedSources;
                bool stableEnvelope=false;int32_t stableBounds[4]={};
                const auto stableAfter=GetTickCount64()+500;
                const auto residualDeadline=GetTickCount64()+12000;unsigned phase=0;
                do {
                    const auto value=pump(residual[phase++%2]);
                    for(const auto& g:geometries())if(g.sequence>=residualFirst) {
                        require(g.mode==advancedMode && g.left<=0.01 && g.right>=width-0.01 && g.top<=140.01 &&
                            g.bottom>=(g.composed?pictureBottom:captionBottom)-0.01,
                            L"Reconstruction composition hid unowned top-bar UI or left an uncomposed bottom caption outside the viewport.");
                    }
                    if(value.presentedSequence>=residualFirst) {
                        const auto trace=records();const AnalysisDiagnosticRecordV1* presentation=nullptr;

                        for(const auto& record:trace)if(record.acceptedSequence==value.presentedSequence) {
                            if(!strcmp(record.stage,"submitted-presentation"))presentation=&record;

                        }
                        require(presentation && IsExactCompletedOutput(*presentation,value.sourceGeneration,advancedMode),
                            L"UHD residual UI lacks exact final source/mode/certificate identity.");
                        require(presentation->published[0]==0 && presentation->published[1]==bar &&
                            presentation->published[2]==width && presentation->published[3]==pictureBottom,
                            L"Residual UI changed the source-owned native picture contract.");
                        const bool composed=(presentation->flags&2u)!=0;
                        require(presentation->displayed[0]<=0 && presentation->displayed[2]>=width &&
                            presentation->displayed[1]<=140 && presentation->displayed[3]>=(composed?pictureBottom:captionBottom),
                            L"An exact submitted Reconstruction source used bottom-caption ownership to hide unrelated bar UI.");
                        require(value.inUseAvailable && std::isfinite(value.inUseRatio) && value.inUseRatio>0 &&
                            value.inUseRatio<=double(width)/(pictureBottom-140)+0.00001,
                            L"Reconstruction actual submitted viewport cannot preserve residual top-bar UI and native picture.");
                        verifyNativeContract(residualFirst);
                                                if(GetTickCount64()>=stableAfter) {
                            if(!stableEnvelope){for(int edge=0;edge<4;++edge)stableBounds[edge]=presentation->displayed[edge];stableEnvelope=true;}
                            for(int edge=0;edge<4;++edge)require(stableBounds[edge]==presentation->displayed[edge],
                                L"Persistent residual UI made Reconstruction alternate its settled submitted size or position.");
                            residualSources.insert(value.presentedSequence);
                            if(composed)composedSources.insert(value.presentedSequence);else uncomposedSources.insert(value.presentedSequence);
                        }
                    }
                                        if(crossingEdge)inspectMovingCleanup(residualFirst,value.presentedSequence);

                    if(residualSources.size()>=24 && composedSources.size()>=24 && (!crossingEdge || (cleanupIntersection && cleanupChangedRejection)))break;
                }while(GetTickCount64()<residualDeadline && callback.state!=RENDERSTATE_FAILED);
                require(residualSources.size()>=24 && composedSources.size()>=24,
                    L"UHD moving-picture residual UI did not survive 24 distinct actually composed and presented sources.");
                Logger::WriteMessage((std::string("Residual UI observed composition states: composed=")+std::to_string(composedSources.size())+
                    " uncomposed="+std::to_string(uncomposedSources.size())+" moving_source_footprint_intersection="+std::to_string(cleanupIntersection)).c_str());
                if(crossingEdge)require(cleanupIntersection && cleanupChangedRejection,
                    L"Cross-edge fixture acquired no actual cleanup rectangle intersection plus sampled cadence cleanup-change rejection; no moving-cleanup coverage established.");
                // Exact current top-band clearance retires that edge independently.
                // The still-occupied raw bottom band may retain its safe margin
                // through composed/uncomposed outputs without changing native AR.
                if(!crossingEdge) {
                    const uint64_t topClearFirst=sequence;std::set<uint64_t> topCleared;
                    const auto topDeadline=GetTickCount64()+12000;bool pinned=false;int32_t stable[4]={};
                    do {
                        const auto value=pump(caption);
                        for(const auto& g:geometries())if(g.sequence>=topClearFirst)
                            require(g.mode==advancedMode && !g.poseEligible && std::abs(g.poseDelta)<0.01 && g.left<=0.01 && g.right>=width-0.01 &&
                                g.top<=bar+0.01 && g.bottom>=(g.composed?pictureBottom:captionBottom)-0.01,
                                L"Top-UI clearance clipped native picture or the still-current uncomposed bottom caption.");
                        if(value.presentedSequence>=topClearFirst) {
                            const auto trace=records();const AnalysisDiagnosticRecordV1* p=nullptr;
                            for(const auto& r:trace)if(r.acceptedSequence==value.presentedSequence) {
                                if(!strcmp(r.stage,"submitted-presentation"))p=&r;
                            }
                            require(p && IsExactCompletedOutput(*p,value.sourceGeneration,advancedMode),
                                L"Top-UI clearance lacks exact final source/mode/certificate identity.");
                            const bool composed=(p->flags&2u)!=0;
                            require(p->published[0]==0 && p->published[1]==bar && p->published[2]==width && p->published[3]==pictureBottom,
                                L"Clearing only top UI changed source-derived native geometry.");
                            require(p->displayed[0]<=0 && p->displayed[2]>=width && p->displayed[1]<=bar &&
                                p->displayed[3]>=(composed?pictureBottom:captionBottom),
                                L"Top-UI clearance hid a current bottom caption before erasure was owned.");
                            require(p->displayed[0]==0 && p->displayed[1]==bar && p->displayed[2]==width,
                                L"Exact current raw top clearance did not immediately retire the unrelated top visibility margin.");
                            if(!pinned){for(int e=0;e<4;++e)stable[e]=p->displayed[e];pinned=true;}
                            for(int e=0;e<4;++e)require(stable[e]==p->displayed[e],
                                L"After retiring the clear top edge, the occupied bottom viewport alternated with composition readiness.");
                            topCleared.insert(value.presentedSequence);
                            verifyNativeContract(topClearFirst);
                        }
                        if(topCleared.size()>=24)break;
                    }while(GetTickCount64()<topDeadline && callback.state!=RENDERSTATE_FAILED);
                    require(topCleared.size()>=24,L"Current top clearance did not retire the top margin while retaining one safe bottom viewport for 24 distinct sources.");
                }
                // Only actual raw absence of ALL bar content must retire every
                // presentation margin. An empty cue does not need composition.
                const uint64_t clearFirst=sequence;std::set<uint64_t> cleared;const auto clearDeadline=GetTickCount64()+12000;
                do {
                    const auto value=pump(native);geometries();
                    const auto trace=records();bool exact=false,rawAbsent=false;
                    for(const auto& record:trace)if(record.acceptedSequence==value.presentedSequence) {
                        if(!strcmp(record.stage,"submitted-presentation")) {
                            exact=record.displayed[0]==initial.left && record.displayed[1]==initial.top &&
                                record.displayed[2]==initial.right && record.displayed[3]==initial.bottom &&
                                record.published[0]==0 && record.published[1]==bar && record.published[2]==width && record.published[3]==pictureBottom;
                            // Source-bound current absence was frozen with THIS successful
                            // selected packet; no raw-attempt/retry trace is joined here.
                            rawAbsent=IsExactCompletedOutput(record,value.sourceGeneration,advancedMode) && (record.flags&8192u)!=0 &&
                                record.counters[1]==record.acceptedSequence && record.counters[0]==record.acceptedSequence &&
                                record.counters[3]==advancedMode && record.counters[4]==acceptedNativeSequence;
                        }
                    }
                    if(value.presentedSequence>=clearFirst && exact && rawAbsent)cleared.insert(value.presentedSequence);
                    if(cleared.size()>=24)break;
                }while(GetTickCount64()<clearDeadline && callback.state!=RENDERSTATE_FAILED);
                require(cleared.size()>=24,L"Removing all bar content did not restore 24 exact current raw-absence native outputs and retire the visibility envelope.");                require(runtime.SelectKey("F5",{},selection,error) && selection.changed,L"UHD fixture could not return to Classic.");
                restart=reset=false;require(cleanup.renderer->ApplyApplicationState(*selection.snapshot,active,restart,reset) && !restart,
                    L"Reconstruction-to-Classic transition required a restart or rejected live controller intent.");
                const uint64_t classicFirst=sequence;uint64_t final=0;std::set<uint64_t> resumed;std::set<int> ramp;double lastTop=initial.top;
                const auto classicDeadline=GetTickCount64()+10000;
                do {
                    const auto value=pump(caption);
                    for(const auto& g:geometries())if(g.sequence>=classicFirst && g.mode==0) {
                        require(sameSize(g) && g.top>=initial.top-0.01 && g.top>=lastTop-0.01 && g.top<=settled.top+0.01 &&
                            std::abs(g.top-initial.top-g.routedTranslation)<0.01,
                            L"Returning to Classic lost its bounded native-size monotonic engage drift.");
                        lastTop=g.top;if(g.top>initial.top+0.01 && g.top<settled.top-0.01)ramp.insert(int(std::lround(g.top)));
                        if(same(g,settled) && !final)final=g.sequence;
                    }
                    if(final && value.presentedSequence>=final)resumed.insert(value.presentedSequence);
                    if(resumed.size()>=24)break;
                }while(GetTickCount64()<classicDeadline && callback.state!=RENDERSTATE_FAILED);
                verifyNativeContract(classicFirst);
                require(ramp.size()>=2,L"A genuinely new caption after current raw absence did not exercise the configured 500 ms Classic engage drift.");
                require(resumed.size()>=24,L"Classic controller did not resume its valid routed intent and stable final pose after Reconstruction.");
                return;
            }
            if(!bothBars)return;
            Logger::WriteMessage("Classic UHD live reproduction: opposite sparse top UI while bottom cue remains\n");
            const uint64_t bothFirst=sequence;uint64_t fitFirst=0;Geometry fixedFit;
            std::set<uint64_t> fitSources;bool scanSeen=false,skipSeen=false;
            const auto bothDeadline=GetTickCount64()+10000;
            do {
                const auto value=pump(both);
                for(const auto& g:geometries())if(g.sequence>=bothFirst) {
                    require(g.mode==0 && g.left<=0.01 && g.right>=width-0.01 && g.top<=134.01 && g.bottom>=1939.99,
                        L"Classic simultaneous top-bar UI and bottom caption were clipped by a scheduled/skipped translated pose.");
                    if(!fitFirst){fitFirst=g.sequence;fixedFit=g;}
                    else require(std::abs(g.left-fixedFit.left)<0.01 && std::abs(g.top-fixedFit.top)<0.01 &&
                        std::abs(g.right-fixedFit.right)<0.01 && std::abs(g.bottom-fixedFit.bottom)<0.01,
                        L"Classic simultaneous bar content alternated its successfully submitted safe visibility envelope.");
                }
                if(value.presentedSequence>=bothFirst) {
                    require(value.inUseAvailable && std::isfinite(value.inUseRatio) && value.inUseRatio>0 &&
                        value.inUseRatio<=double(width)/(1940-134)+0.00001,
                        L"Classic first observed both-bar output retained a viewport too short to preserve both overlays.");
                    verifyNativeContract(bothFirst);

                    if(fitFirst)fitSources.insert(value.presentedSequence);
                }
                uint64_t previousIntervalSource=0;
                for(const auto& line:messages()) {
                    if(line.find("Alpha source crop:")!=std::string::npos && integer(line," sequence=")>=bothFirst) {
                        scanSeen|=line.find(" dense_scan=scheduled")!=std::string::npos;
                        skipSeen|=line.find(" dense_scan=skipped")!=std::string::npos;
                    }
                    if(line.find("VP source measurement interval:")!=std::string::npos) {
                        const auto end=integer(line," source="),batches=integer(line," batches=");
                        const auto requests=integer(line," subtitle_requests="),hits=integer(line," subtitle_hits=");
                        if(previousIntervalSource>=bothFirst && batches>0 && requests>=hits) {
                            scanSeen|=requests-hits>0;skipSeen|=requests-hits<batches;
                        }
                        previousIntervalSource=end;
                    }
                }
                if(fitSources.size()>=24 && scanSeen && skipSeen)break;
            }while(GetTickCount64()<bothDeadline && callback.state!=RENDERSTATE_FAILED);
            require(fitSources.size()>=24 && scanSeen && skipSeen,
                L"Both-bar safe visibility did not survive 24 distinct actual presented sources with real dense scans and skips.");
        }

        TEST_METHOD(StandardRendererClassicUhdBottomCaptionOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(false,false); }
        TEST_METHOD(BetaRendererClassicUhdBottomCaptionOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(true,false); }
        TEST_METHOD(StandardRendererClassicUhdBothBarUiOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(false,true); }
        TEST_METHOD(BetaRendererClassicUhdBothBarUiOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(true,true); }

        TEST_METHOD(StandardRendererReconstructionUhdResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(false,true,true); }
        TEST_METHOD(BetaRendererReconstructionUhdResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(true,true,true); }
        TEST_METHOD(StandardRendererClassicUhdV210BothBarUiOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(false,true,false,true); }
        TEST_METHOD(BetaRendererClassicUhdV210BothBarUiOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(true,true,false,true); }
        TEST_METHOD(StandardRendererReconstructionUhdV210ResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(false,true,true,true); }
        TEST_METHOD(BetaRendererReconstructionUhdV210ResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(true,true,true,true); }
        TEST_METHOD(StandardRendererReconstructionUhdV210CrossEdgeMovingCleanup)
        { VerifyClassicUhdBarOverlay(false,true,true,true,true); }
        TEST_METHOD(BetaRendererReconstructionUhdV210CrossEdgeMovingCleanup)
        { VerifyClassicUhdBarOverlay(true,true,true,true,true); }
        TEST_METHOD(UhdP210CrossEdgeFixtureNativePreflight)
        { VerifyClassicUhdBarOverlay(false,true,true,false,true,true); }
        TEST_METHOD(UhdV210CrossEdgeFixtureNativePreflight)
        { VerifyClassicUhdBarOverlay(false,true,true,true,true,true); }
        TEST_METHOD(StandardRendererBlackUhdV210ResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(false,true,true,true,false,false,true); }
        TEST_METHOD(BetaRendererBlackUhdV210ResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(true,true,true,true,false,false,true); }
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
