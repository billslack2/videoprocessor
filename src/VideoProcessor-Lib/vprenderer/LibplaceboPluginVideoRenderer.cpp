#include <pch.h>

#include "LibplaceboPluginVideoRenderer.h"
#include "LibplaceboRendererPluginApi.h"
#include "OptionalRendererLayout.h"

#include <ConfigFile.h>
#include <DebugLog.h>

#include <stdexcept>
#include <string>


namespace
{
	struct PluginExports
	{
		HMODULE module = nullptr;
		VideoProcessorLibplaceboCreateRendererFn createRenderer = nullptr;
		VideoProcessorLibplaceboDestroyRendererFn destroyRenderer = nullptr;
	};

	void __cdecl ForwardPluginLog(const char* message)
	{
		if (message)
			DebugLog::Log("%s", message);
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

	PluginExports LoadPlugin()
	{
		PluginExports result;
		const std::wstring executableDirectory = ExecutableDirectory();
		if (executableDirectory.empty())
			return result;

		const std::wstring pluginPath =
			OptionalRendererLayout::PluginPath(executableDirectory);
		DebugLog::Log(
			"VP Renderer plugin probe: path=%ls",
			pluginPath.c_str());
		std::wstring missingPath;
		if (OptionalRendererLayout::FindMissingRuntimeFile(
			executableDirectory, missingPath))
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

		DebugLog::Log(
			"Optional VP Renderer plugin loaded: API=%u path=%ls",
			apiVersion,
			pluginPath.c_str());
		return result;
	}

	const PluginExports& GetPlugin()
	{
		// Keep a validated plugin loaded for the process lifetime. Renderer objects
		// and callbacks may remain active until late in application shutdown.
		static const PluginExports plugin = LoadPlugin();
		return plugin;
	}
}


bool LibplaceboPluginVideoRenderer::IsAvailable()
{
	return GetPlugin().module != nullptr;
}


LibplaceboPluginVideoRenderer::LibplaceboPluginVideoRenderer(
	IRendererCallback& callback,
	HWND videoHwnd,
	ITimingClock* timingClock,
	bool useFrameQueue,
	size_t frameQueueMaxSize,
	VideoConversionOverride videoConversionOverride)
{
	const PluginExports& plugin = GetPlugin();
	if (!plugin.module)
		throw std::runtime_error("optional libplacebo renderer plugin is unavailable");

	ConfigFile rendererConfig;
	std::string rendererConfigPath;
	if (rendererConfig.Load(ConfigFile::RENDERER_FILENAME))
		rendererConfigPath = rendererConfig.GetLoadedPath();
	else if (!rendererConfig.GetWarnings().empty())
		throw std::runtime_error(rendererConfig.GetWarnings().front());

	m_renderer = plugin.createRenderer(
		&callback,
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
}


LibplaceboPluginVideoRenderer::~LibplaceboPluginVideoRenderer()
{
	if (m_renderer && m_destroyRenderer)
		m_destroyRenderer(m_renderer);
	m_renderer = nullptr;
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


void LibplaceboPluginVideoRenderer::Retire() noexcept
{
	if (m_renderer)
		m_renderer->Retire();
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

bool LibplaceboPluginVideoRenderer::SetNativeShaderCompilationOverlay(
	const uint8_t* pixels, size_t byteCount, int width, int height, int stride)
{
	return m_renderer && m_renderer->SetNativeShaderCompilationOverlay(
		pixels, byteCount, width, height, stride);
}

bool LibplaceboPluginVideoRenderer::SetNativeSweepOverlay(
	const uint8_t* pixels, size_t byteCount, int width, int height, int stride)
{
	return m_renderer && m_renderer->SetNativeSweepOverlay(
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


bool LibplaceboPluginVideoRenderer::RefreshShaderRule(
	CString& activeRule,
	bool& rendererRestartRequired)
{
	return m_renderer->RefreshShaderRule(activeRule,
		rendererRestartRequired);
}

bool LibplaceboPluginVideoRenderer::GetShaderCompilationStatus(
	CString& status) const
{
	return m_renderer && m_renderer->GetShaderCompilationStatus(status);
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
