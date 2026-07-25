#pragma once

#include <IRenderer.h>
#include <ITimingClock.h>

#include <cstddef>
#include <cstdint>


static constexpr uint32_t VP_LIBPLACEBO_PLUGIN_API_VERSION = 3;
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
	HWND videoHwnd,
	ITimingClock* timingClock,
	bool useFrameQueue,
	size_t frameQueueMaxSize,
	VideoProcessorLibplaceboLogSink logSink);
using VideoProcessorLibplaceboDestroyRendererFn = void (__cdecl *)(
	IVideoRenderer* renderer);
