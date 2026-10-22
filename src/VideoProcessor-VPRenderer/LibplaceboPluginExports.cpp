#include <pch.h>

#include <ConfigFile.h>
#include "version.h"
#include <DebugLog.h>
#include <vprenderer/LibplaceboRendererPluginApi.h>
#include <vprenderer/LibplaceboVideoRenderer.h>


extern "C" __declspec(dllexport) uint32_t __cdecl
VideoProcessorLibplaceboGetApiVersion()
{
	return VP_LIBPLACEBO_PLUGIN_API_VERSION;
}


extern "C" __declspec(dllexport) IVideoRenderer* __cdecl
VideoProcessorLibplaceboCreateRenderer(
	IRendererCallback* callback,
	uint32_t rendererGeneration,
	HWND videoHwnd,
	ITimingClock* timingClock,
	bool useFrameQueue,
	size_t frameQueueMaxSize,
	VideoConversionOverride videoConversionOverride,
	const char* rendererConfigPath,
	VideoProcessorLibplaceboLogSink logSink)
{
	DebugLog::SetExternalSink(logSink);
	DebugLog::Log("VP build identity: module=renderer commit=%ls branch=%ls dirty=%d build=%ls",
		VERSION_URL, VERSION_BRANCH, VERSION_DIRTY ? 1 : 0, VERSION_DESCRIBE);
	ConfigFile::SetRendererConfigurationPath(
		rendererConfigPath ? rendererConfigPath : "");
	if (!callback)
	{
		DebugLog::Log("libplacebo plugin factory rejected a null renderer callback");
		return nullptr;
	}

	try
	{
		return new LibplaceboVideoRenderer(
			*callback,
			rendererGeneration,
			videoHwnd,
			timingClock,
			useFrameQueue,
			frameQueueMaxSize,
			videoConversionOverride);
	}
	catch (const std::exception& e)
	{
		DebugLog::Log("libplacebo plugin factory failed: %s", e.what());
	}
	catch (...)
	{
		DebugLog::Log("libplacebo plugin factory failed: unknown exception");
	}
	return nullptr;
}


extern "C" __declspec(dllexport) void __cdecl
VideoProcessorLibplaceboDestroyRenderer(IVideoRenderer* renderer)
{
	delete renderer;
}

// Every renderer this plugin creates is a LibplaceboVideoRenderer.
extern "C" __declspec(dllexport) bool __cdecl
VideoProcessorLibplaceboGetHdrStatus(
	const IVideoRenderer* renderer,
	RendererHdrStatus* status)
{
	if (!renderer || !status || !status->Supported())
		return false;
	return static_cast<const LibplaceboVideoRenderer*>(renderer)->GetHdrStatus(*status);
}

extern "C" __declspec(dllexport) bool __cdecl VideoProcessorLibplaceboIsBeta()
{
#if defined(VP_LIBPLACEBO_BETA)
    return true;
#else
    return false;
#endif
}

extern "C" __declspec(dllexport) void __cdecl
VideoProcessorLibplaceboConfigureAnalysisDiagnosticSink(void* context,
    AnalysisDiagnosticTrace::RecordSink records, AnalysisDiagnosticTrace::FrameSink frames,
    AnalysisDiagnosticTrace::FrameCaptureActiveCallback active)
{
    AnalysisDiagnosticTrace::SetExternalSinks(context,records,frames,active);
}

extern "C" __declspec(dllexport) uint32_t __cdecl
VideoProcessorLibplaceboGetStartupPresentationReadinessV1(
    IVideoRenderer* renderer, StartupPresentation::SnapshotV1* snapshot)
{
    using namespace StartupPresentation;
    if (!renderer || !snapshot || !Supported(*snapshot))
        return static_cast<uint32_t>(QueryStatus::Unsupported);
    auto* vp = dynamic_cast<LibplaceboVideoRenderer*>(renderer);
    if (!vp) return static_cast<uint32_t>(QueryStatus::Unsupported);
    SnapshotV1 current;
    if (!vp->GetStartupPresentationReadiness(current))
        return static_cast<uint32_t>(QueryStatus::Busy);
    *snapshot = current;
    return static_cast<uint32_t>(QueryStatus::Available);
}
