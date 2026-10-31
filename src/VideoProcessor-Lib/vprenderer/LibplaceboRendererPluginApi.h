#pragma once

#include <IRenderer.h>
#include <StartupPresentationReadiness.h>
#include <AnalysisDiagnosticTrace.h>
#include <ITimingClock.h>
#include <VideoConversionOverride.h>

#include <cstddef>
#include <cstdint>


// Increment whenever the factory signature or any shared interface vtable
// or shared snapshot layout changes. The executable and optional renderer DLL are deployed separately,
// so this is the fail-closed ABI boundary for a mismatched pair.
static constexpr uint32_t VP_LIBPLACEBO_PLUGIN_API_VERSION = 34;
static constexpr const char* VP_LIBPLACEBO_FLAVOR_EXPORT = "VideoProcessorLibplaceboIsBeta";
static constexpr const char* VP_LIBPLACEBO_VERSION_EXPORT =
	"VideoProcessorLibplaceboGetApiVersion";
static constexpr const char* VP_LIBPLACEBO_CREATE_EXPORT =
	"VideoProcessorLibplaceboCreateRenderer";
static constexpr const char* VP_LIBPLACEBO_DESTROY_EXPORT =
	"VideoProcessorLibplaceboDestroyRenderer";

using VideoProcessorLibplaceboLogSink = void (__cdecl *)(const char* message);
using VideoProcessorLibplaceboGetApiVersionFn = uint32_t (__cdecl *)();
using VideoProcessorLibplaceboCreateRendererFn = IVideoRenderer* (__cdecl *)(
	IRendererCallback* callback,
	uint32_t rendererGeneration,
	HWND videoHwnd,
	ITimingClock* timingClock,
	bool useFrameQueue,
	size_t frameQueueMaxSize,
	VideoConversionOverride videoConversionOverride,
	const char* rendererConfigPath,
	VideoProcessorLibplaceboLogSink logSink);
using VideoProcessorLibplaceboDestroyRendererFn = void (__cdecl *)(
	IVideoRenderer* renderer);

static constexpr const char* VP_LIBPLACEBO_DIAGNOSTIC_EXPORT = "VideoProcessorLibplaceboConfigureAnalysisDiagnosticSink";
using VideoProcessorLibplaceboConfigureDiagnosticFn = void (__cdecl *)(void*,
    AnalysisDiagnosticTrace::RecordSink, AnalysisDiagnosticTrace::FrameSink,
    AnalysisDiagnosticTrace::FrameCaptureActiveCallback);

// Optional V1 side-channel. Its absence preserves API-29 reveal behavior; it
// changes neither the factory nor any existing shared interface/snapshot.
static constexpr const char* VP_LIBPLACEBO_STARTUP_READINESS_EXPORT =
    "VideoProcessorLibplaceboGetStartupPresentationReadinessV1";
using VideoProcessorLibplaceboGetStartupReadinessFn = uint32_t (__cdecl *)(
    IVideoRenderer*, StartupPresentation::SnapshotV1*);

inline StartupPresentation::QueryStatus QueryOptionalStartupReadiness(
    VideoProcessorLibplaceboGetStartupReadinessFn query, IVideoRenderer* renderer,
    StartupPresentation::SnapshotV1& snapshot)
{
    snapshot = {};
    if (!query || !renderer) return StartupPresentation::QueryStatus::Unsupported;
    const auto status = static_cast<StartupPresentation::QueryStatus>(query(renderer, &snapshot));
    if (status == StartupPresentation::QueryStatus::Busy) return status;
    return status == StartupPresentation::QueryStatus::Available && StartupPresentation::Supported(snapshot)
        ? status : StartupPresentation::QueryStatus::Unsupported;
}
// Optional, outside the API version: the executable looks it up by name and
// shows no renderer HDR status when a plugin lacks it. The status checks its
// own layout through RendererHdrStatus::size.
static constexpr const char* VP_LIBPLACEBO_HDR_STATUS_EXPORT =
	"VideoProcessorLibplaceboGetHdrStatus";
using VideoProcessorLibplaceboGetHdrStatusFn = bool (__cdecl *)(
	const IVideoRenderer* renderer,
	RendererHdrStatus* status);
