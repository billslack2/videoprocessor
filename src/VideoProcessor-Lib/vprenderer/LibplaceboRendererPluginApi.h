#pragma once

#include <IRenderer.h>
#include <ITimingClock.h>
#include <VideoConversionOverride.h>

#include <cstddef>
#include <cstdint>


// Increment whenever the factory signature or any shared interface vtable
// or shared snapshot layout changes. The executable and optional renderer DLL are deployed separately,
// so this is the fail-closed ABI boundary for a mismatched pair.
static constexpr uint32_t VP_LIBPLACEBO_PLUGIN_API_VERSION = 26;
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

// Optional, outside the API version: the executable looks it up by name and
// shows no renderer HDR status when a plugin lacks it. The status checks its
// own layout through RendererHdrStatus::size.
static constexpr const char* VP_LIBPLACEBO_HDR_STATUS_EXPORT =
	"VideoProcessorLibplaceboGetHdrStatus";
using VideoProcessorLibplaceboGetHdrStatusFn = bool (__cdecl *)(
	const IVideoRenderer* renderer,
	RendererHdrStatus* status);
