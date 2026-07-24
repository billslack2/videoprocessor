#include <pch.h>

#include "LibplaceboPluginVideoRenderer.h"
#include "LibplaceboRendererPluginApi.h"

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

		const std::wstring pluginPath = executableDirectory +
			L"\\libplacebo\\VideoProcessorLibplacebo.dll";
		if (GetFileAttributesW(pluginPath.c_str()) == INVALID_FILE_ATTRIBUTES)
			return result;

		result.module = LoadLibraryExW(
			pluginPath.c_str(),
			nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!result.module)
		{
			DebugLog::Log(
				"Optional libplacebo renderer plugin could not be loaded: error=%lu",
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
			"Optional libplacebo renderer plugin loaded: API=%u",
			apiVersion);
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
	size_t frameQueueMaxSize)
{
	const PluginExports& plugin = GetPlugin();
	if (!plugin.module)
		throw std::runtime_error("optional libplacebo renderer plugin is unavailable");

	m_renderer = plugin.createRenderer(
		&callback,
		videoHwnd,
		timingClock,
		useFrameQueue,
		frameQueueMaxSize,
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


void LibplaceboPluginVideoRenderer::SetFrameQueueMaxSize(size_t size)
{
	m_renderer->SetFrameQueueMaxSize(size);
}


void LibplaceboPluginVideoRenderer::SetSceneAwareTimingCorrection(bool enabled)
{
	m_renderer->SetSceneAwareTimingCorrection(enabled);
}


bool LibplaceboPluginVideoRenderer::SetScreenProfile(
	bool scopeScreen,
	CString& activeProfile)
{
	return m_renderer->SetScreenProfile(scopeScreen, activeProfile);
}


bool LibplaceboPluginVideoRenderer::SelectDisplayRule(
	const CString& ruleName,
	CString& activeRule,
	bool& rendererRestartRequired)
{
	return m_renderer->SelectDisplayRule(ruleName, activeRule,
		rendererRestartRequired);
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
