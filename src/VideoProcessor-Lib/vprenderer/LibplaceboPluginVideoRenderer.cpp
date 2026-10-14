#include <pch.h>

#include "LibplaceboPluginVideoRenderer.h"
#include "LibplaceboRendererPluginApi.h"
#include "OptionalRendererLayout.h"

#include <ConfigFile.h>
#include <DebugLog.h>
#include <AnalysisDiagnosticHooks.h>

#include <stdexcept>
#include <string>


namespace
{
    std::atomic<unsigned> activeDiagnosticProducers{0};
	struct PluginExports
	{
		HMODULE module = nullptr;
		VideoProcessorLibplaceboCreateRendererFn createRenderer = nullptr;
		VideoProcessorLibplaceboDestroyRendererFn destroyRenderer = nullptr;
	};

    void __cdecl ForwardAnalysisRecord(void* context,const AnalysisDiagnosticRecordV1* record) {
        if(record) static_cast<AnalysisDiagnosticTrace*>(context)->Submit(*record);
    }
    void __cdecl ForwardAnalysisFrame(void* context,const AnalysisDiagnosticFramePacketV1* frame) {
        if(frame) static_cast<AnalysisDiagnosticTrace*>(context)->SubmitFrame(*frame);
    }
    bool __cdecl AnalysisFramesActive(void* context) {
        return static_cast<AnalysisDiagnosticTrace*>(context)->IsFrameCaptureActive();
    }
    void TraceLifecycle(const char* text) {
        AnalysisDiagnosticRecordV1 r;r.kind=AnalysisDiagnosticRecordKindV1::Event;
        AnalysisTraceText(r.stage,"renderer-lifecycle");AnalysisTraceText(r.reason,text);
        AnalysisDiagnosticTrace::Instance().Submit(r);
    }
	void __cdecl ForwardPluginLog(const char* message)
	{
        if (message) {
            // These already-generated messages describe state resets, never pixels.
            if (strstr(message,"authority reset") || strstr(message,"look-ahead configured") ||
                strstr(message,"presentation target:") || strstr(message,"analysis contract:") ||
                strstr(message,"source generation")) TraceLifecycle(message);
            DebugLog::Log("%s", message);
        }
	}

	std::wstring ExecutableDirectory()
	{
		wchar_t executablePath[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
			return std::wstring();

		std::wstring directory(executablePath, length);
		const size_t separator = directory.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
			return std::wstring();
		directory.resize(separator);
		return directory;
	}

	PluginExports LoadPlugin(bool beta)
	{
		PluginExports result;
		const std::wstring executableDirectory = ExecutableDirectory();
		if (executableDirectory.empty())
			return result;

		const std::wstring pluginPath =
			OptionalRendererLayout::PluginPath(executableDirectory, beta);
		DebugLog::Log(
			"VP Renderer plugin probe: path=%ls",
			pluginPath.c_str());
		std::wstring missingPath;
		if (OptionalRendererLayout::FindMissingRuntimeFile(
			executableDirectory, missingPath, beta))
		{
			DebugLog::Log(
				"VP Renderer plugin unavailable: missing required file path=%ls",
				missingPath.c_str());
			return result;
		}

		result.module = LoadLibraryExW(
			pluginPath.c_str(),
			nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!result.module)
		{
			DebugLog::Log(
				"Optional VP Renderer plugin could not be loaded: error=%lu",
				GetLastError());
			return PluginExports{};
		}

		const auto getApiVersion = reinterpret_cast<VideoProcessorLibplaceboGetApiVersionFn>(
			GetProcAddress(result.module, VP_LIBPLACEBO_VERSION_EXPORT));
		result.createRenderer = reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(
			GetProcAddress(result.module, VP_LIBPLACEBO_CREATE_EXPORT));
		result.destroyRenderer = reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(
			GetProcAddress(result.module, VP_LIBPLACEBO_DESTROY_EXPORT));
		if (!getApiVersion || !result.createRenderer || !result.destroyRenderer)
		{
			DebugLog::Log(
				"Optional libplacebo renderer plugin is missing required exports");
			FreeLibrary(result.module);
			return PluginExports{};
		}

		const auto isBeta = reinterpret_cast<bool (__cdecl*)()>(
            GetProcAddress(result.module, VP_LIBPLACEBO_FLAVOR_EXPORT));
        if (!isBeta || isBeta() != beta) {
            DebugLog::Log("VP Renderer plugin flavor mismatch: path=%ls", pluginPath.c_str());
            FreeLibrary(result.module);
            return PluginExports{};
        }
        const uint32_t apiVersion = getApiVersion();
		if (apiVersion != VP_LIBPLACEBO_PLUGIN_API_VERSION)
		{
			DebugLog::Log(
				"Optional libplacebo renderer plugin API mismatch: expected=%u actual=%u",
				VP_LIBPLACEBO_PLUGIN_API_VERSION,
				apiVersion);
			FreeLibrary(result.module);
			return PluginExports{};
		}

        const auto diagnostics=reinterpret_cast<VideoProcessorLibplaceboConfigureDiagnosticFn>(
            GetProcAddress(result.module,VP_LIBPLACEBO_DIAGNOSTIC_EXPORT));
        if (!diagnostics) {
            DebugLog::Log("VP Renderer missing required analysis diagnostic export");
            FreeLibrary(result.module);return PluginExports{};
        }
        auto& recorder=AnalysisDiagnosticTrace::Instance();
        diagnostics(&recorder,ForwardAnalysisRecord,ForwardAnalysisFrame,AnalysisFramesActive);

		DebugLog::Log(
			"Optional VP Renderer plugin loaded: API=%u path=%ls",
			apiVersion,
			pluginPath.c_str());
		return result;
	}

	const PluginExports& GetPlugin(bool beta)
	{
		// Keep a validated plugin loaded for the process lifetime. Renderer objects
		// and callbacks may remain active until late in application shutdown.
		if (beta) {
			static const PluginExports plugin = LoadPlugin(true);
			return plugin;
		}
		static const PluginExports plugin = LoadPlugin(false);
		return plugin;
	}
}


bool LibplaceboPluginVideoRenderer::IsAvailable(bool beta)
{
	return GetPlugin(beta).module != nullptr;
}


LibplaceboPluginVideoRenderer::LibplaceboPluginVideoRenderer(
	IRendererCallback& callback,
	uint32_t rendererGeneration,
	HWND videoHwnd,
	ITimingClock* timingClock,
	bool useFrameQueue,
	size_t frameQueueMaxSize,
	VideoConversionOverride videoConversionOverride, bool beta)
{
	const PluginExports& plugin = GetPlugin(beta);
	if (!plugin.module)
		throw std::runtime_error("optional libplacebo renderer plugin is unavailable");

	ConfigFile rendererConfig;
	std::string rendererConfigPath;
	if (rendererConfig.Load(ConfigFile::RENDERER_FILENAME))
		rendererConfigPath = rendererConfig.GetLoadedPath();
	else if (!rendererConfig.GetWarnings().empty())
		throw std::runtime_error(rendererConfig.GetWarnings().front());

	TraceLifecycle("renderer-create; detector state will initialize");
	m_renderer = plugin.createRenderer(
		&callback,
		rendererGeneration,
		videoHwnd,
		timingClock,
		useFrameQueue,
		frameQueueMaxSize,
		videoConversionOverride,
		rendererConfigPath.c_str(),
		ForwardPluginLog);
	if (!m_renderer)
		throw std::runtime_error("optional libplacebo renderer plugin failed to create a renderer");
	m_destroyRenderer = plugin.destroyRenderer;
    activeDiagnosticProducers.fetch_add(1, std::memory_order_relaxed);
    AnalysisDiagnosticTrace::Instance().SetProducerAvailable(true,true);
}


LibplaceboPluginVideoRenderer::~LibplaceboPluginVideoRenderer()
{
    TraceLifecycle("renderer-destroy; detector and worker state will be discarded; trace retained");
	if (m_renderer && m_destroyRenderer)
		m_destroyRenderer(m_renderer);
	m_renderer = nullptr;
    if (activeDiagnosticProducers.fetch_sub(1, std::memory_order_relaxed) == 1)
        AnalysisDiagnosticTrace::Instance().SetProducerAvailable(false,false);
}


size_t LibplaceboPluginVideoRenderer::GetConvertedQueueSize()
{
	return m_renderer->GetConvertedQueueSize();
}


bool LibplaceboPluginVideoRenderer::OnVideoState(VideoStateComPtr& videoState)
{
	return m_renderer->OnVideoState(videoState);
}


bool LibplaceboPluginVideoRenderer::SupportsDynamicVideoState() const
{
	return m_renderer->SupportsDynamicVideoState();
}


void LibplaceboPluginVideoRenderer::OnVideoFrame(VideoFrame& videoFrame)
{
	m_renderer->OnVideoFrame(videoFrame);
}


bool LibplaceboPluginVideoRenderer::HasPresentedLiveFrame() const
{
	return m_renderer->HasPresentedLiveFrame();
}


const char* LibplaceboPluginVideoRenderer::PresentedLiveFrameEvidence() const
{
	return m_renderer->PresentedLiveFrameEvidence();
}


HRESULT LibplaceboPluginVideoRenderer::OnWindowsEvent(LONG_PTR param1, LONG_PTR param2)
{
	return m_renderer->OnWindowsEvent(param1, param2);
}


void LibplaceboPluginVideoRenderer::Build()
{
	m_renderer->Build();
}


void LibplaceboPluginVideoRenderer::Start()
{
	m_renderer->Start();
}


void LibplaceboPluginVideoRenderer::Stop()
{
	m_renderer->Stop();
}


void LibplaceboPluginVideoRenderer::StopWithIngressDrain(
	const std::function<void()>& drainAfterGraphStop)
{
	m_renderer->StopWithIngressDrain(drainAfterGraphStop);
}


void LibplaceboPluginVideoRenderer::Retire() noexcept
{
	if (m_renderer)
		m_renderer->Retire();
}


bool LibplaceboPluginVideoRenderer::RetirementSucceeded() const
{
	return !m_renderer || m_renderer->RetirementSucceeded();
}


bool LibplaceboPluginVideoRenderer::FinalizeRetirementForShutdown() noexcept
{
	return m_renderer && m_renderer->FinalizeRetirementForShutdown();
}


void LibplaceboPluginVideoRenderer::Reset()
{
	m_renderer->Reset();
}


void LibplaceboPluginVideoRenderer::ResetLiveQueue()
{
	m_renderer->ResetLiveQueue();
}


void LibplaceboPluginVideoRenderer::OnSize()
{
	m_renderer->OnSize();
}


void LibplaceboPluginVideoRenderer::OnPaint()
{
	m_renderer->OnPaint();
}


void LibplaceboPluginVideoRenderer::OnDisplayChange()
{
	m_renderer->OnDisplayChange();
}

bool LibplaceboPluginVideoRenderer::SupportsNativeStatsOverlay() const
{
	return m_renderer && m_renderer->SupportsNativeStatsOverlay();
}

bool LibplaceboPluginVideoRenderer::SetNativeStatsOverlay(
	const uint8_t* pixels, size_t byteCount, int width, int height, int stride)
{
	return m_renderer && m_renderer->SetNativeStatsOverlay(
		pixels, byteCount, width, height, stride);
}

bool LibplaceboPluginVideoRenderer::SetNativeSweepOverlay(
	const uint8_t* pixels, size_t byteCount, int width, int height, int stride)
{
	return m_renderer && m_renderer->SetNativeSweepOverlay(
		pixels, byteCount, width, height, stride);
}

bool LibplaceboPluginVideoRenderer::SetNativeProfileOverlay(
	const uint8_t* pixels, size_t byteCount, int width, int height, int stride)
{
	return m_renderer && m_renderer->SetNativeProfileOverlay(
		pixels, byteCount, width, height, stride);
}


void LibplaceboPluginVideoRenderer::SetFrameQueueMaxSize(size_t size)
{
	m_renderer->SetFrameQueueMaxSize(size);
}

void LibplaceboPluginVideoRenderer::SetQueueFramePolicy(
	size_t startupPrerollFrames, size_t steadyReserveFrames,
	bool hasSteadyReserveFrames)
{
	m_renderer->SetQueueFramePolicy(startupPrerollFrames,
		steadyReserveFrames, hasSteadyReserveFrames);
}


void LibplaceboPluginVideoRenderer::SetActivePictureLookaheadFrames(
	size_t frames)
{
	m_renderer->SetActivePictureLookaheadFrames(frames);
}


void LibplaceboPluginVideoRenderer::SetSceneAwareTimingCorrection(bool enabled)
{
	m_renderer->SetSceneAwareTimingCorrection(enabled);
}

uint64_t LibplaceboPluginVideoRenderer::SceneAwareCorrectionDropCount() const
{
	return m_renderer->SceneAwareCorrectionDropCount();
}

uint64_t LibplaceboPluginVideoRenderer::SceneAwareCorrectionRepeatCount() const
{
	return m_renderer->SceneAwareCorrectionRepeatCount();
}

uint64_t LibplaceboPluginVideoRenderer::SceneAwareDetectedCount() const
{
	return m_renderer->SceneAwareDetectedCount();
}

bool LibplaceboPluginVideoRenderer::GetSceneDetectionStatus(
	CString& status) const
{
	return m_renderer->GetSceneDetectionStatus(status);
}

bool LibplaceboPluginVideoRenderer::GetSceneTimingPrediction(
	double& secondsUntilCorrection, double& secondsUntilPlan,
	int& action, bool& planned) const
{
	return m_renderer->GetSceneTimingPrediction(
		secondsUntilCorrection, secondsUntilPlan, action, planned);
}

bool LibplaceboPluginVideoRenderer::GetSceneTimingLastCorrection(
	int& action, double& secondsFromDeadline, uint64_t& correctionTick) const
{
	return m_renderer->GetSceneTimingLastCorrection(
		action, secondsFromDeadline, correctionTick);
}

bool LibplaceboPluginVideoRenderer::SceneTimingRatesCompatible() const
{
	return m_renderer->SceneTimingRatesCompatible();
}

bool LibplaceboPluginVideoRenderer::GetSceneTimingStatus(CString& status) const
{
	return m_renderer->GetSceneTimingStatus(status);
}

bool LibplaceboPluginVideoRenderer::GetSceneTimingDueStatus(
	int& action, CString& reason) const
{
	return m_renderer->GetSceneTimingDueStatus(action, reason);
}


bool LibplaceboPluginVideoRenderer::SelectDisplayRule(
	const CString& ruleName,
	CString& activeRule,
	bool& rendererRestartRequired)
{
	return m_renderer->SelectDisplayRule(ruleName, activeRule,
		rendererRestartRequired);
}


bool LibplaceboPluginVideoRenderer::SelectShaderRule(
	const CString& ruleName,
	CString& activeRule,
	bool& rendererRestartRequired)
{
	return m_renderer->SelectShaderRule(ruleName, activeRule,
		rendererRestartRequired);
}


bool LibplaceboPluginVideoRenderer::SelectShaderProfiles(
	const std::vector<std::string>& profileSections,
	CString& activeState,
	bool& rendererRestartRequired)
{
	return m_renderer->SelectShaderProfiles(profileSections, activeState,
		rendererRestartRequired);
}


bool LibplaceboPluginVideoRenderer::GetRenderStallStatus(
	CString& status) const
{
	return m_renderer->GetRenderStallStatus(status);
}


bool LibplaceboPluginVideoRenderer::RefreshShaderRule(
	CString& activeRule,
	bool& rendererRestartRequired)
{
	return m_renderer->RefreshShaderRule(activeRule,
		rendererRestartRequired);
}


std::vector<CString>
LibplaceboPluginVideoRenderer::ActiveShaders() const
{
	return m_renderer->ActiveShaders();
}


bool LibplaceboPluginVideoRenderer::GetActiveShaderSections(
	std::vector<CString>& sections) const
{
	return m_renderer->GetActiveShaderSections(sections);
}


CString LibplaceboPluginVideoRenderer::ActiveShaderRule() const
{
	return m_renderer->ActiveShaderRule();
}


bool LibplaceboPluginVideoRenderer::ApplyApplicationState(
	const UnifiedProfileRuntime::Snapshot& snapshot,
	CString& activeState,
	bool& rendererRestartRequired,
	bool& liveResetRequired)
{
	return m_renderer->ApplyApplicationState(snapshot, activeState,
		rendererRestartRequired, liveResetRequired);
}


void LibplaceboPluginVideoRenderer::ResetSubtitlePreviewMode()
{
	if(m_renderer) m_renderer->ResetSubtitlePreviewMode();
}

bool LibplaceboPluginVideoRenderer::CycleSubtitlePreviewMode(
	SubtitlePreviewMode& mode)
{
	return m_renderer && m_renderer->CycleSubtitlePreviewMode(mode);
}

size_t LibplaceboPluginVideoRenderer::GetFrameQueueSize()
{
	return m_renderer->GetFrameQueueSize();
}


double LibplaceboPluginVideoRenderer::EntryLatencyMs() const
{
	return m_renderer->EntryLatencyMs();
}


double LibplaceboPluginVideoRenderer::ExitLatencyMs() const
{
	return m_renderer->ExitLatencyMs();
}


uint64_t LibplaceboPluginVideoRenderer::DroppedFrameCount() const
{
	return m_renderer->DroppedFrameCount();
}


bool LibplaceboPluginVideoRenderer::GetRenderLoad(RendererRenderLoad& load) const
{
	return m_renderer->GetRenderLoad(load);
}


void LibplaceboPluginVideoRenderer::SetRenderLoadDisplayRefreshRate(
	double refreshRateHz)
{
	m_renderer->SetRenderLoadDisplayRefreshRate(refreshRateHz);
}


bool LibplaceboPluginVideoRenderer::GetOutputModeInfo(CString& details) const
{
	return m_renderer->GetOutputModeInfo(details);
}

bool LibplaceboPluginVideoRenderer::GetOutputContractStatus(
	RendererOutputContract::Status& status) const
{
	return m_renderer->GetOutputContractStatus(status);
}

bool LibplaceboPluginVideoRenderer::RequestRenderedOutputCapture(
	CString& status)
{
	if (!m_renderer)
	{
		status = TEXT("VP Renderer is not active");
		return false;
	}
	return m_renderer->RequestRenderedOutputCapture(status);
}


bool LibplaceboPluginVideoRenderer::GetDisplayLutInfo(CString& details) const
{
	return m_renderer->GetDisplayLutInfo(details);
}

bool LibplaceboPluginVideoRenderer::GetDetectedPictureAspect(
	DetectedPictureAspect& result) const
{
	return m_renderer->GetDetectedPictureAspect(result);
}


bool LibplaceboPluginVideoRenderer::GetVideoIngressInfo(CString& details) const
{
	return m_renderer->GetVideoIngressInfo(details);
}

bool LibplaceboPluginVideoRenderer::GetPresentationTargetTiming(
	double& leadMs, double& captureToTargetMs) const
{
	return m_renderer->GetPresentationTargetTiming(leadMs,
		captureToTargetMs);
}

bool LibplaceboPluginVideoRenderer::GetPresentationTimingStatus(
	CString& status) const
{
	return m_renderer && m_renderer->GetPresentationTimingStatus(status);
}


bool LibplaceboPluginVideoRenderer::GetConversionPerformance(
	double& currentUs, double& avg10s, double& max10s) const
{
	return m_renderer->GetConversionPerformance(currentUs, avg10s, max10s);
}


bool LibplaceboPluginVideoRenderer::GetFrameRateAndPPM(
	double& measuredFps,
	int& ppmDeviation) const
{
	return m_renderer->GetFrameRateAndPPM(measuredFps, ppmDeviation);
}


bool LibplaceboPluginVideoRenderer::GetDolbyVisionLuminanceStatus(
	DolbyVisionLuminanceStatus& status) const
{
	status = {};
	return m_renderer && m_renderer->GetDolbyVisionLuminanceStatus(status);
}
