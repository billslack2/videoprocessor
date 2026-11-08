// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>
#include "CalibrationPatternHost.h"

#include <DebugLog.h>

#include <libplacebo/colorspace.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>

#include <atlbase.h>

#include <chrono>
#include <cstring>

namespace CalibrationPatternHost
{
	TakeOver TakeOverPresentation(const std::atomic_bool& patternsActive, const TakeOverEnv& env)
	{
		if (!IsTakingOver(patternsActive))
			return TakeOver::NotTaking;
		{
			std::lock_guard<std::mutex> queueGuard(env.queueMutex);
			if (env.stopRequested) return TakeOver::Stop;
		}
		bool presented = false;
		{
			std::lock_guard<std::mutex> renderGuard(env.renderMutex);
			presented = env.present();
		}
		if (!presented)
		{
			std::unique_lock<std::mutex> idle(env.queueMutex);
			env.queueChanged.wait_for(idle, std::chrono::milliseconds(20));
		}
		return TakeOver::Handled;
	}

	Adapter::~Adapter()
	{
		// Normally stopped by Retire; this covers an Impl destroyed without it.
		Retire(nullptr);
	}

	void Adapter::StartPipeIfEnabled(const PipeCalls& calls, std::atomic_bool& patternsActive)
	{
		if (pipe || !CalibrationPipe::EnabledByEnvironment())
			return;
		rendererPatternsActive = &patternsActive;
		CalibrationPipe::Handlers handlers;
		handlers.getOutput = calls.getOutput;
		handlers.setPattern = calls.setPattern;
		std::unique_ptr<CalibrationPipe::Server> server(new CalibrationPipe::Server(CalibrationPipe::DefaultPipeName(), handlers));
		std::string error;
		if (!server->Start(error))
		{
			DebugLog::Log("Calibration pipe: not started: %s", error.c_str());
			return;
		}
		pipe = std::move(server);
	}

	void Adapter::Retire(pl_gpu gpu)
	{
		if (pipe)
		{
			pipe->Stop();   // joins the server thread: no request runs after this
			pipe.reset();
			if (rendererPatternsActive && rendererPatternsActive->exchange(false, std::memory_order_acq_rel))
				DebugLog::Log("Calibration patterns: ended with the renderer output");
			{
				std::lock_guard<std::mutex> guard(patternMutex);
				pattern.active = false;
			}
		}
		if (gpu)
			pl_tex_destroy(gpu, &patchTexture);
	}

	bool Adapter::SetPatterns(const Calibration::PatternRequest& request, std::atomic_bool& patternsActive,
		const PatternLifecycle& lifecycle, std::string& reason)
	{
		if (request.active)
		{
			uint32_t containerBits = 0;
			{
				std::lock_guard<std::mutex> guard(outputCacheMutex);
				containerBits = outputCache.containerBits;
			}
			if (!PatchFitsContainer(request.patch, request.background, containerBits))
			{
				reason = containerBits == 8 || containerBits == 10 ?
					"a patch code does not fit the " + std::to_string(containerBits) + "-bit output" :
					std::string("no integer output is known yet (query the output first; a float swapchain takes no patch)");
				return false;
			}
			{
				std::lock_guard<std::mutex> guard(patternMutex);
				pattern = request;
			}
			if (!patternsActive.load(std::memory_order_acquire))
			{
				windowFallbackLogged.store(false, std::memory_order_release);
				failureLogged.store(false, std::memory_order_release);
				patternsActive.store(true, std::memory_order_release);
				lifecycle.beginQueueGeneration("calibration patterns");
				DebugLog::Log("Calibration patterns: started (video paused)");
			}
			lifecycle.notifyQueue();
			return true;
		}
		if (!patternsActive.exchange(false, std::memory_order_acq_rel))
			return true;
		{
			std::lock_guard<std::mutex> guard(patternMutex);
			pattern.active = false;
		}
		lifecycle.beginQueueGeneration("calibration patterns ended");
		lifecycle.notifyQueue();
		DebugLog::Log("Calibration patterns: ended (video resumes)");
		return true;
	}

	bool Adapter::CollectOutputInfo(std::mutex& renderMutex, bool patternsActive,
		const std::function<OutputFacts()>& gather, Calibration::PatternOutputInfo& info)
	{
		// The format fields are render-thread state: read under renderMutex, which the loop releases
		// between refreshes; a cached copy answers when it stays busy.
		uint32_t cached = 0;
		{
			std::lock_guard<std::mutex> guard(outputCacheMutex);
			cached = outputCache.containerBits;
		}
		bool fresh = false;
		const int attempts = FreshOutputAttempts(patternsActive, cached);
		for (int attempt = 0; attempt < attempts && !fresh; ++attempt)
		{
			std::unique_lock<std::mutex> guard(renderMutex, std::try_to_lock);
			if (!guard.owns_lock())
			{
				if (attempts > 1) Sleep(2);
				continue;
			}
			PublishOutputFacts(gather());
			fresh = true;
		}
		return FillOutputInfo(info, fresh);
	}

	void Adapter::PublishOutputFacts(const OutputFacts& facts)
	{
		Calibration::PatternOutputInfo current;
		current.containerBits = ContainerBits(facts.format);
		current.codeBits = CodeBits(current.containerBits, facts.displayBitDepth);
		current.limitedRange = facts.limitedRange;
		current.bt2020 = facts.bt2020;
		const int transfer = facts.transfer;
		current.transfer = transfer == PL_COLOR_TRC_PQ ? Calibration::SignalTransfer::Pq :
			transfer == PL_COLOR_TRC_HLG ? Calibration::SignalTransfer::Hlg : Calibration::SignalTransfer::Sdr;
		current.refreshHz = facts.refreshHz;
		current.available = facts.available;
		if (!current.available) strncpy_s(current.reason, sizeof(current.reason), "the output is not ready", _TRUNCATE);
		std::lock_guard<std::mutex> guard(outputCacheMutex);
		outputCache = current;
	}

	bool Adapter::FillOutputInfo(Calibration::PatternOutputInfo& info, bool fresh) const
	{
		{
			std::lock_guard<std::mutex> guard(outputCacheMutex);
			info = outputCache;
		}
		if (!fresh && info.containerBits == 0)
		{
			strncpy_s(info.reason, sizeof(info.reason), "the VP Renderer is busy; try again", _TRUNCATE);
			return false;
		}
		info.width = surfaceWidth.load(std::memory_order_acquire);
		info.height = surfaceHeight.load(std::memory_order_acquire);
		info.presentedGeneration = presentedGeneration.load(std::memory_order_acquire);
		info.presentsOfGeneration = presentsOfGeneration.load(std::memory_order_acquire);
		info.readbackGeneration = readbackGeneration.load(std::memory_order_acquire);
		const uint32_t center = readbackCenter.load(std::memory_order_acquire);
		const uint32_t corner = readbackCorner.load(std::memory_order_acquire);
		for (int c = 0; c < 3; ++c)
		{
			info.readbackCenter[c] = static_cast<uint16_t>((center >> (10 * c)) & 0x3FF);
			info.readbackCorner[c] = static_cast<uint16_t>((corner >> (10 * c)) & 0x3FF);
		}
		// The HDR verify's tone-curve fields stay empty here (toneCurveCount 0): the SDR LUT run does
		// not need them, and the verify then models the curve itself (hdr_verify_tone_curve bt2390).
		return info.available;
	}

	// PatternReadback: the filled backbuffer's centre and corner pixels.
	void Adapter::ReadBack(const Surface& surface, ID3D11Texture2D* backbuffer, uint64_t generation)
	{
		std::vector<uint32_t> pixels;
		D3D11_TEXTURE2D_DESC desc{};
		std::string error = "no reader";
		if (!surface.readR10 || !surface.readR10(backbuffer, pixels, desc, error) || pixels.empty())
		{
			DebugLog::Log("Calibration patterns: readback unavailable (%s)", error.c_str());
			return;
		}
		const uint32_t centre = pixels[static_cast<size_t>(desc.Height / 2) * desc.Width + desc.Width / 2];
		readbackCenter.store(centre, std::memory_order_release);
		readbackCorner.store(pixels[0], std::memory_order_release);
		readbackGeneration.store(generation, std::memory_order_release);
		DebugLog::Log("Calibration patterns: read back generation %llu (%ux%u): centre %u,%u,%u corner %u,%u,%u",
			static_cast<unsigned long long>(generation), desc.Width, desc.Height,
			centre & 0x3FF, (centre >> 10) & 0x3FF, (centre >> 20) & 0x3FF,
			pixels[0] & 0x3FF, (pixels[0] >> 10) & 0x3FF, (pixels[0] >> 20) & 0x3FF);
	}

	// Writes the patch into fbo: the background, then the window (a cleared texture of the same
	// format blitted 1:1, so the codes stay exact).
	bool Adapter::FillPattern(pl_gpu gpu, pl_tex fbo, DXGI_FORMAT format, const Calibration::PatternRequest& request,
		bool& windowFallback)
	{
		windowFallback = false;
		if (!fbo || !gpu) return false;
		const double maxValue = PatchMaxCode(format);
		if (!(maxValue > 0.0) || !fbo->params.renderable) return false;
		const float patch[4] = {
			static_cast<float>(request.patch[0] / maxValue), static_cast<float>(request.patch[1] / maxValue),
			static_cast<float>(request.patch[2] / maxValue), 1.0f };
		const float background[4] = {
			static_cast<float>(request.background[0] / maxValue), static_cast<float>(request.background[1] / maxValue),
			static_cast<float>(request.background[2] / maxValue), 1.0f };
		const int width = fbo->params.w, height = fbo->params.h;
		const Calibration::PixelRect rect = Calibration::PatchWindowRect(width, height, request.windowAreaPercent);
		const bool fullField = rect.left == 0 && rect.top == 0 && rect.right == width && rect.bottom == height;
		if (fullField)
		{
			pl_tex_clear(gpu, fbo, patch);
			return true;
		}
		bool windowed = fbo->params.blit_dst;
		if (windowed && (!patchTexture || patchTexture->params.w != rect.Width() ||
			patchTexture->params.h != rect.Height() || patchTexture->params.format != fbo->params.format))
		{
			pl_tex_destroy(gpu, &patchTexture);
			pl_tex_params params{};
			params.w = rect.Width();
			params.h = rect.Height();
			params.format = fbo->params.format;
			params.renderable = true;
			params.blit_src = true;
			params.blit_dst = true;   // pl_tex_clear validates blit_dst on the cleared texture
			patchTexture = pl_tex_create(gpu, &params);
			windowed = patchTexture != nullptr;
		}
		if (!windowed)
		{
			// The surface cannot take a blit: full field (exact codes still). Visible in the log once
			// per run; LCD / OLED readings depend on the window size.
			windowFallback = true;
			if (!windowFallbackLogged.exchange(true, std::memory_order_acq_rel))
				DebugLog::Log("Calibration patterns: windowed patch unavailable on this surface (blit_dst=%d); full field shown",
					fbo->params.blit_dst ? 1 : 0);
			pl_tex_clear(gpu, fbo, patch);
			return true;
		}
		pl_tex_clear(gpu, fbo, background);
		pl_tex_clear(gpu, patchTexture, patch);
		pl_tex_blit_params blit{};
		blit.src = patchTexture;
		blit.dst = fbo;
		blit.src_rc.x0 = 0; blit.src_rc.y0 = 0; blit.src_rc.z0 = 0;
		blit.src_rc.x1 = rect.Width(); blit.src_rc.y1 = rect.Height(); blit.src_rc.z1 = 1;
		blit.dst_rc.x0 = rect.left; blit.dst_rc.y0 = rect.top; blit.dst_rc.z0 = 0;
		blit.dst_rc.x1 = rect.right; blit.dst_rc.y1 = rect.bottom; blit.dst_rc.z1 = 1;
		blit.sample_mode = PL_TEX_SAMPLE_NEAREST;
		pl_tex_blit(gpu, &blit);
		return true;
	}

	bool Adapter::Present(const Surface& surface)
	{
		if (!surface.gpu || !surface.swapchain)
			return false;
		Calibration::PatternRequest request;
		{
			std::lock_guard<std::mutex> guard(patternMutex);
			request = pattern;
		}
		if (!request.active)
			return false;
		bool filled = false;
		bool presented = false;
		bool windowFallback = false;
		uint32_t width = 0, height = 0;
		if (surface.vpOwned)
		{
			CComPtr<ID3D11Texture2D> backbuffer;
			if (FAILED(surface.vpOwned->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) || !backbuffer)
				return false;
			pl_d3d11_wrap_params wrapParams{};
			wrapParams.tex = static_cast<ID3D11Resource*>(backbuffer.p);
			pl_tex targetTexture = pl_d3d11_wrap(surface.gpu, &wrapParams);
			if (targetTexture)
			{
				width = static_cast<uint32_t>(targetTexture->params.w);
				height = static_cast<uint32_t>(targetTexture->params.h);
				filled = FillPattern(surface.gpu, targetTexture, surface.format, request, windowFallback);
				pl_gpu_flush(surface.gpu);
				pl_tex_destroy(surface.gpu, &targetTexture);
				if (filled && (request.flags & Calibration::PatternReadback) &&
					readbackGeneration.load(std::memory_order_acquire) != request.generation)
					ReadBack(surface, backbuffer, request.generation);
			}
			backbuffer.Release();
			if (filled)
				presented = SUCCEEDED(surface.vpOwned->Present(1, 0));
		}
		else
		{
			struct pl_swapchain_frame frame{};
			if (!pl_swapchain_start_frame(surface.swapchain, &frame))
				return false;
			if (frame.fbo)
			{
				width = static_cast<uint32_t>(frame.fbo->params.w);
				height = static_cast<uint32_t>(frame.fbo->params.h);
			}
			filled = FillPattern(surface.gpu, frame.fbo, surface.format, request, windowFallback);
			if (filled && (request.flags & Calibration::PatternReadback) &&
				readbackGeneration.load(std::memory_order_acquire) != request.generation)
			{
				pl_gpu_flush(surface.gpu);
				ReadBack(surface, nullptr, request.generation);
			}
			if (!filled)
			{
				struct pl_frame target{};
				pl_frame_from_swapchain(&target, &frame);
				const float black[] = { 0.0f, 0.0f, 0.0f };
				pl_frame_clear(surface.gpu, &target, black);
			}
			presented = pl_swapchain_submit_frame(surface.swapchain);
			if (presented)
				pl_swapchain_swap_buffers(surface.swapchain);
			presented = presented && filled;
		}
		if (!presented)
		{
			if (!failureLogged.exchange(true, std::memory_order_acq_rel))
				DebugLog::Log("Calibration patterns: present failed (filled=%d format=%d)", filled ? 1 : 0,
					static_cast<int>(surface.format));
			return false;
		}
		surfaceWidth.store(width, std::memory_order_release);
		surfaceHeight.store(height, std::memory_order_release);
		if (presentedGeneration.load(std::memory_order_acquire) != request.generation)
		{
			presentsOfGeneration.store(0, std::memory_order_release);
			presentedGeneration.store(request.generation, std::memory_order_release);
		}
		presentsOfGeneration.fetch_add(1, std::memory_order_acq_rel);
		(void)windowFallback;
		return true;
	}
}
