// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "CalibrationPatternPolicy.h"
#include "CalibrationPipe.h"
#include "../pattern_lib/CalibrationPattern.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <libplacebo/gpu.h>
#include <libplacebo/swapchain.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The renderer side of automatic calibration: the patch the render loop presents instead of video
// and the output facts the calibration session reads. One Adapter object lives in the renderer's
// Impl; LibplaceboVideoRenderer.cpp keeps one short call per hook point (each marked
// "AUTOCAL-HOOK"; ../INTEGRATION.md lists them).
//
// The fidelity contract: a patch is written as raw integer codes into the swapchain backbuffer in
// its own format. No libplacebo pass (tone / gamut mapping, the target LUT, deband, dither, user
// shaders) runs for it, so the codes reach the display exactly. The swapchain format, its range,
// DXGI colour space, HDR metadata and refresh stay as configured for video.
//
// Threading: Present runs on the render thread with renderMutex held. SetPatterns and
// CollectOutputInfo run on the pipe server's thread (the calibration session's requests).
// FillOutputInfo on any thread. Each piece of cross-thread state has its own mutex or is atomic.
namespace CalibrationPatternHost
{
	// The swapchain the patch is presented on (the renderer's; the adapter never owns it).
	struct Surface
	{
		pl_gpu gpu = nullptr;
		IDXGISwapChain1* vpOwned = nullptr;   // the renderer's own flip swapchain, or null: libplacebo's swapchain
		pl_swapchain swapchain = nullptr;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		// Reads a 10-bit backbuffer (PatternReadback; null texture = libplacebo's current one).
		std::function<bool(ID3D11Texture2D*, std::vector<uint32_t>&, D3D11_TEXTURE2D_DESC&, std::string&)> readR10;
	};

	// The render-thread format facts the output query reports (read under renderMutex).
	struct OutputFacts
	{
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		std::string displayBitDepth;   // "8", "10" or "auto"
		bool limitedRange = true;
		bool bt2020 = false;
		double refreshHz = 0.0;        // 0 = not known
		int transfer = -1;             // pl_color_transfer of the output encoding (-1 = not known: SDR)
		bool available = false;
	};

	// The render loop's take-over (before the dequeue). The loop gives its own queue lock, stop flag,
	// wake-up and render lock.
	struct TakeOverEnv
	{
		std::mutex& queueMutex;
		const bool& stopRequested;               // guarded by queueMutex
		std::condition_variable& queueChanged;
		std::mutex& renderMutex;
		std::function<bool()> present;           // one refresh of the patch (called with renderMutex held)
	};
	enum class TakeOver { NotTaking, Stop, Handled };   // Stop = leave the loop; Handled = `continue`

	// The one predicate the capture thread (drop frames), the dequeue waits (wake) and the loop read.
	inline bool IsTakingOver(const std::atomic_bool& patternsActive) { return patternsActive.load(std::memory_order_acquire); }
	TakeOver TakeOverPresentation(const std::atomic_bool& patternsActive, const TakeOverEnv& env);

	// The renderer-side effects of a patterns session beginning / ending.
	struct PatternLifecycle
	{
		std::function<void(const char*)> beginQueueGeneration;   // drop what is queued
		std::function<void()> notifyQueue;                        // wake the render loop
	};

	// What the pipe server needs from the renderer (the two calls of the calibration contract).
	struct PipeCalls
	{
		std::function<bool(Calibration::PatternOutputInfo&)> getOutput;
		std::function<bool(const Calibration::PatternRequest&, std::string&)> setPattern;
	};

	class Adapter
	{
	public:
		Adapter() = default;
		~Adapter();
		Adapter(const Adapter&) = delete;
		Adapter& operator=(const Adapter&) = delete;

		// Render thread, after the renderer's initialization. Starts the local pipe server when the
		// process environment has VP_CALIBRATION_PIPE=1 (no change otherwise). Idempotent.
		// `patternsActive` is the renderer's flag (it outlives the adapter).
		void StartPipeIfEnabled(const PipeCalls& calls, std::atomic_bool& patternsActive);

		// The renderer's RetireOutput: stops the pipe server (joins its thread), clears the
		// renderer's patterns flag (no queue work: the render loop is gone) and releases the patch
		// texture, before the GPU goes.
		void Retire(pl_gpu gpu);

		// After the renderer's state checks: stores the request (active) or ends the session.
		// `patternsActive` is the renderer's flag. Pipe thread.
		bool SetPatterns(const Calibration::PatternRequest& request, std::atomic_bool& patternsActive,
			const PatternLifecycle& lifecycle);

		// One refresh of the current patch (the caller holds renderMutex and has checked that the
		// output exists). True when a patch was presented.
		bool Present(const Surface& surface);

		// The output query: try-locks renderMutex, publishes `gather()` under it, then answers from
		// the cache. `gather` runs with renderMutex held.
		bool CollectOutputInfo(std::mutex& renderMutex, bool patternsActive,
			const std::function<OutputFacts()>& gather, Calibration::PatternOutputInfo& info);

	private:
		void PublishOutputFacts(const OutputFacts& facts);
		bool FillOutputInfo(Calibration::PatternOutputInfo& info, bool fresh) const;
		bool FillPattern(pl_gpu gpu, pl_tex fbo, DXGI_FORMAT format, const Calibration::PatternRequest& pattern,
			bool& windowFallback);
		void ReadBack(const Surface& surface, ID3D11Texture2D* backbuffer, uint64_t generation);

		std::unique_ptr<CalibrationPipe::Server> pipe;
		std::atomic_bool* rendererPatternsActive = nullptr;

		std::mutex patternMutex;
		Calibration::PatternRequest pattern;
		std::atomic<uint64_t> presentedGeneration{ 0 };
		std::atomic<uint64_t> presentsOfGeneration{ 0 };
		std::atomic<uint32_t> surfaceWidth{ 0 };
		std::atomic<uint32_t> surfaceHeight{ 0 };
		pl_tex patchTexture = nullptr;
		std::atomic<bool> windowFallbackLogged{ false };
		std::atomic<bool> failureLogged{ false };
		mutable std::mutex outputCacheMutex;
		Calibration::PatternOutputInfo outputCache;
		std::atomic<uint64_t> readbackGeneration{ 0 };
		std::atomic<uint32_t> readbackCenter{ 0 };   // R10G10B10A2 words
		std::atomic<uint32_t> readbackCorner{ 0 };
	};
}
