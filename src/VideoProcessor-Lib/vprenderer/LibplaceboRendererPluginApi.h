#pragma once
#include <SourceQueueDisposition.h>

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
static constexpr uint32_t VP_LIBPLACEBO_PLUGIN_API_VERSION = 35;
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

// Optional diagnostic-only side channel; no shared snapshot or factory ABI change.
static constexpr const char* VP_LIBPLACEBO_LOGGING_EXPORT = "VideoProcessorLibplaceboConfigureLoggingV1";
using VideoProcessorLibplaceboConfigureLoggingFn = void (__cdecl *)(bool enhanced);

// Explicit regression factory only: never selected by the host, config or environment.
// This models unavailable optional reuse; it cannot grant pixel or crop authority.
struct LibplaceboRegressionOptionsV1 {
    uint32_t size = sizeof(LibplaceboRegressionOptionsV1);
    uint32_t version = 1;
    uint32_t flags = 0;
};
static constexpr uint32_t VP_REGRESSION_DECLINE_CUE_PROOF = 1u;
static constexpr uint32_t VP_REGRESSION_MISSING_ANALYSIS_WINDOW = 4u;
static constexpr uint32_t VP_REGRESSION_SOURCE_TICKET_PROOF_WINDOW = 8u;
// Remove the exact-byte escape only in the existing both-missing test interval.
static constexpr uint32_t VP_REGRESSION_WITHHOLD_EQUIVALENCE_WINDOW = 16u;
// Relative to the first genuinely consumed current cue receipt, never wall time.
static constexpr uint64_t VP_REGRESSION_RAW_MISSING_BEGIN = 16;
static constexpr uint64_t VP_REGRESSION_RAW_MISSING_END = 80;
static constexpr uint64_t VP_REGRESSION_BOTH_MISSING_BEGIN = 48;
static constexpr uint64_t VP_REGRESSION_BOTH_MISSING_END = 50;
inline bool ValidLibplaceboRegressionOptions(const LibplaceboRegressionOptionsV1* options) {
    return options && options->size == sizeof(LibplaceboRegressionOptionsV1) &&
        options->version == 1 && !(options->flags & ~(VP_REGRESSION_DECLINE_CUE_PROOF | VP_REGRESSION_MISSING_ANALYSIS_WINDOW | VP_REGRESSION_SOURCE_TICKET_PROOF_WINDOW | VP_REGRESSION_WITHHOLD_EQUIVALENCE_WINDOW)) &&
        (!(options->flags & VP_REGRESSION_WITHHOLD_EQUIVALENCE_WINDOW) ||
            (options->flags & VP_REGRESSION_MISSING_ANALYSIS_WINDOW));
}
static constexpr const char* VP_LIBPLACEBO_REGRESSION_CREATE_EXPORT =
    "VideoProcessorLibplaceboCreateRendererForRegressionV1";
using VideoProcessorLibplaceboCreateRegressionRendererFn = IVideoRenderer* (__cdecl *)(
    IRendererCallback*, uint32_t, HWND, ITimingClock*, bool, size_t,
    VideoConversionOverride, const char*, VideoProcessorLibplaceboLogSink,
    const LibplaceboRegressionOptionsV1*);

// Optional diagnostic extension; existing vtables and shared layouts unchanged.
static constexpr const char* VP_LIBPLACEBO_QUEUE_DISPOSITION_EXPORT =
    "VideoProcessorLibplaceboGetProcessQueueDispositionsV1";
