#include <pch.h>

#include "LibplaceboVideoRenderer.h"

#include <ConfigFile.h>
#include <DebugLog.h>
#include <video_frame_formatter/CARGBtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>

#pragma warning(push)
#pragma warning(disable: 4244) // conversion warning in an upstream inline helper
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/utils/upload.h>
#pragma warning(pop)

#include <algorithm>
#include <stdexcept>
#include <vector>


namespace
{
	void LibplaceboLog(void*, enum pl_log_level level, const char* message)
	{
		if (!message)
			return;

		const char* label = "info";
		switch (level)
		{
		case PL_LOG_FATAL: label = "fatal"; break;
		case PL_LOG_ERR: label = "error"; break;
		case PL_LOG_WARN: label = "warning"; break;
		case PL_LOG_DEBUG: label = "debug"; break;
		case PL_LOG_TRACE: label = "trace"; break;
		default: break;
		}

		DebugLog::Log("libplacebo [%s]: %s", label, message);
	}

	enum pl_color_primaries TranslatePrimaries(ColorSpace colorspace)
	{
		switch (colorspace)
		{
		case ColorSpace::REC_601_525: return PL_COLOR_PRIM_BT_601_525;
		case ColorSpace::REC_601_576:
		case ColorSpace::REC_601_625: return PL_COLOR_PRIM_BT_601_625;
		case ColorSpace::REC_709: return PL_COLOR_PRIM_BT_709;
		case ColorSpace::P3_D65: return PL_COLOR_PRIM_DISPLAY_P3;
		case ColorSpace::P3_DCI: return PL_COLOR_PRIM_DCI_P3;
		case ColorSpace::P3_D60: return PL_COLOR_PRIM_DISPLAY_P3;
		case ColorSpace::BT_2020: return PL_COLOR_PRIM_BT_2020;
		default: return PL_COLOR_PRIM_UNKNOWN;
		}
	}

	enum pl_color_system TranslateSystem(ColorSpace colorspace)
	{
		switch (colorspace)
		{
		case ColorSpace::REC_601_525:
		case ColorSpace::REC_601_576:
		case ColorSpace::REC_601_625:
			return PL_COLOR_SYSTEM_BT_601;
		case ColorSpace::BT_2020:
			return PL_COLOR_SYSTEM_BT_2020_NC;
		default:
			return PL_COLOR_SYSTEM_BT_709;
		}
	}

	enum pl_color_transfer TranslateTransfer(EOTF eotf)
	{
		switch (eotf)
		{
		case EOTF::PQ: return PL_COLOR_TRC_PQ;
		case EOTF::HLG: return PL_COLOR_TRC_HLG;
		case EOTF::HDR: return PL_COLOR_TRC_GAMMA22;
		case EOTF::SDR: return PL_COLOR_TRC_BT_1886;
		default: return PL_COLOR_TRC_UNKNOWN;
		}
	}

	void SetCiePoint(struct pl_cie_xy& point, double x, double y)
	{
		point.x = static_cast<float>(x);
		point.y = static_cast<float>(y);
	}

	struct pl_color_space TranslateColorSpace(const VideoState& state)
	{
		struct pl_color_space result{};
		result.primaries = TranslatePrimaries(state.colorspace);
		result.transfer = TranslateTransfer(state.eotf);

		// Leave HDR metadata unset when the capture source supplied only the
		// mandatory empty HDRData object. libplacebo can infer safe defaults from
		// the transfer function and primaries; zero mastering primaries/luminance
		// would instead describe an invalid mastering display.
		if (state.hdrData && state.hdrData->IsValid())
		{
			const HDRData& hdr = *state.hdrData;
			SetCiePoint(result.hdr.prim.red, hdr.displayPrimaryRedX, hdr.displayPrimaryRedY);
			SetCiePoint(result.hdr.prim.green, hdr.displayPrimaryGreenX, hdr.displayPrimaryGreenY);
			SetCiePoint(result.hdr.prim.blue, hdr.displayPrimaryBlueX, hdr.displayPrimaryBlueY);
			SetCiePoint(result.hdr.prim.white, hdr.whitePointX, hdr.whitePointY);
			result.hdr.min_luma = static_cast<float>(hdr.masteringDisplayMinLuminance);
			result.hdr.max_luma = static_cast<float>(hdr.masteringDisplayMaxLuminance);
			result.hdr.max_cll = static_cast<float>(hdr.maxCll);
			result.hdr.max_fall = static_cast<float>(hdr.maxFall);
		}

		return result;
	}

	std::unique_ptr<IVideoFrameFormatter> CreateP010Formatter(VideoFrameEncoding encoding)
	{
		switch (encoding)
		{
		case VideoFrameEncoding::V210:
			return std::unique_ptr<IVideoFrameFormatter>(new CV210toP010VideoFrameFormatter());
		case VideoFrameEncoding::UYVY:
			return std::unique_ptr<IVideoFrameFormatter>(new CUYVYtoP010VideoFrameFormatter());
		case VideoFrameEncoding::ARGB_8BIT:
		case VideoFrameEncoding::BGRA_8BIT:
			return std::unique_ptr<IVideoFrameFormatter>(new CARGBtoP010VideoFrameFormatter());
		default:
			throw std::runtime_error(
				"libplacebo currently supports V210, UYVY, ARGB, or BGRA capture input");
		}
	}

	double LoadSdrTargetNits()
	{
		double targetNits = 100.0;
		ConfigFile config;
		std::string value;
		if (config.Load() && config.TryGetString("libplacebo", "sdr_target_nits", value))
		{
			try
			{
				const double parsed = std::stod(ConfigFile::Trim(value));
				if (parsed >= 40.0 && parsed <= 500.0)
					targetNits = parsed;
				else
					DebugLog::Log(
						"libplacebo: sdr_target_nits must be between 40 and 500; using 100");
			}
			catch (const std::exception&)
			{
				DebugLog::Log("libplacebo: invalid sdr_target_nits value; using 100");
			}
		}

		return targetNits;
	}
}


struct LibplaceboVideoRenderer::Impl
{
	pl_log log = nullptr;
	pl_d3d11 d3d11 = nullptr;
	pl_swapchain swapchain = nullptr;
	pl_renderer renderer = nullptr;
	pl_tex textures[2] = { nullptr, nullptr };
	std::unique_ptr<IVideoFrameFormatter> formatter;
	VideoStateComPtr formatterState;
	std::vector<BYTE> convertedFrame;
	struct pl_render_params renderParams{};
	double sdrTargetNits = 100.0;
	std::mutex renderMutex;
	EOTF lastRenderedEotf = EOTF::UNKNOWN;
	ColorSpace lastRenderedColorspace = ColorSpace::UNKNOWN;

	~Impl()
	{
		pl_renderer_destroy(&renderer);
		if (d3d11)
		{
			for (pl_tex& texture : textures)
				pl_tex_destroy(d3d11->gpu, &texture);
		}
		pl_swapchain_destroy(&swapchain);
		pl_d3d11_destroy(&d3d11);
		pl_log_destroy(&log);
	}

	void Initialize(HWND videoHwnd, VideoStateComPtr& state)
	{
		struct pl_log_params logParams{};
		logParams.log_cb = LibplaceboLog;
		logParams.log_level = PL_LOG_INFO;
		log = pl_log_create(PL_API_VER, &logParams);
		if (!log)
			throw std::runtime_error("Failed to create libplacebo log context");

		struct pl_d3d11_params deviceParams = pl_d3d11_default_params;
		deviceParams.allow_software = false;
		deviceParams.min_feature_level = D3D_FEATURE_LEVEL_10_0;
		deviceParams.max_frame_latency = 2;
		d3d11 = pl_d3d11_create(log, &deviceParams);
		if (!d3d11)
			throw std::runtime_error("Failed to create libplacebo D3D11 device");

		struct pl_d3d11_swapchain_params swapchainParams{};
		swapchainParams.window = videoHwnd;
		swapchainParams.color_bits = 10;
		swapchain = pl_d3d11_create_swapchain(d3d11, &swapchainParams);
		if (!swapchain)
			throw std::runtime_error("Failed to create libplacebo D3D11 swapchain");

		struct pl_color_space outputColor = pl_color_space_bt709;
		pl_swapchain_colorspace_hint(swapchain, &outputColor);

		RECT client{};
		if (!GetClientRect(videoHwnd, &client))
			throw std::runtime_error("Failed to query libplacebo render window size");
		int width = std::max<LONG>(1, client.right - client.left);
		int height = std::max<LONG>(1, client.bottom - client.top);
		if (!pl_swapchain_resize(swapchain, &width, &height))
			throw std::runtime_error("Failed to initialize libplacebo swapchain size");

		renderer = pl_renderer_create(log, d3d11->gpu);
		if (!renderer)
			throw std::runtime_error("Failed to create libplacebo renderer");

		formatter = CreateP010Formatter(state->videoFrameEncoding);
		formatter->OnVideoState(state);
		formatterState = state;
		convertedFrame.resize(static_cast<size_t>(formatter->GetOutFrameSize()));
		renderParams = pl_render_high_quality_params;
		sdrTargetNits = LoadSdrTargetNits();

		DebugLog::Log(
			"libplacebo initialized: D3D11, P010 upload, SDR Rec.709 output at %.1f nits, peak detection enabled",
			sdrTargetNits);
	}

	bool Render(const VideoFrame& videoFrame, VideoStateComPtr& statePtr)
	{
		std::lock_guard<std::mutex> guard(renderMutex);
		const VideoState& state = *statePtr;

		if (!formatterState || formatterState->colorspace != state.colorspace)
		{
			formatter->OnVideoState(statePtr);
			formatterState = statePtr;
		}

		if (lastRenderedEotf != EOTF::UNKNOWN &&
			(lastRenderedEotf != state.eotf || lastRenderedColorspace != state.colorspace))
		{
			// Flush at the exact queued-frame boundary, not when metadata first
			// arrives. Older queued frames still belong to the prior color state.
			pl_renderer_flush_cache(renderer);
		}

		if (!formatter->FormatVideoFrame(videoFrame, convertedFrame.data()))
			return false;

		const int width = static_cast<int>(state.displayMode->FrameWidth());
		const int height = static_cast<int>(state.displayMode->FrameHeight());
		const size_t rowBytes = static_cast<size_t>(width) * sizeof(uint16_t);
		const BYTE* yPixels = convertedFrame.data();
		const BYTE* uvPixels = yPixels + rowBytes * static_cast<size_t>(height);

		struct pl_plane_data planes[2]{};
		planes[0].type = PL_FMT_UNORM;
		planes[0].width = width;
		planes[0].height = height;
		planes[0].component_size[0] = 16;
		planes[0].component_map[0] = PL_CHANNEL_Y;
		planes[0].pixel_stride = sizeof(uint16_t);
		planes[0].row_stride = rowBytes;
		planes[0].pixels = yPixels;

		planes[1].type = PL_FMT_UNORM;
		planes[1].width = (width + 1) / 2;
		planes[1].height = (height + 1) / 2;
		planes[1].component_size[0] = 16;
		planes[1].component_size[1] = 16;
		planes[1].component_map[0] = PL_CHANNEL_CB;
		planes[1].component_map[1] = PL_CHANNEL_CR;
		planes[1].pixel_stride = sizeof(uint16_t) * 2;
		planes[1].row_stride = rowBytes;
		planes[1].pixels = uvPixels;

		struct pl_frame image{};
		image.num_planes = 2;
		for (int plane = 0; plane < 2; ++plane)
		{
			if (!pl_upload_plane(d3d11->gpu, &image.planes[plane], &textures[plane], &planes[plane]))
				return false;
			image.planes[plane].shift_x = 0.0f;
			image.planes[plane].shift_y = 0.0f;
			image.planes[plane].flipped = state.invertedVertical;
		}

		image.repr.sys = TranslateSystem(state.colorspace);
		image.repr.levels =
			state.videoFrameEncoding == VideoFrameEncoding::ARGB_8BIT ||
			state.videoFrameEncoding == VideoFrameEncoding::BGRA_8BIT
			? PL_COLOR_LEVELS_FULL
			: PL_COLOR_LEVELS_LIMITED;
		image.repr.alpha = PL_ALPHA_NONE;
		image.repr.bits.sample_depth = 16;
		image.repr.bits.color_depth = 10;
		image.repr.bits.bit_shift = 6;
		image.color = TranslateColorSpace(state);
		image.crop.x0 = 0.0f;
		image.crop.y0 = 0.0f;
		image.crop.x1 = static_cast<float>(width);
		image.crop.y1 = static_cast<float>(height);
		pl_frame_set_chroma_location(&image, PL_CHROMA_LEFT);

		struct pl_swapchain_frame swapchainFrame{};
		if (!pl_swapchain_start_frame(swapchain, &swapchainFrame))
			return false;

		struct pl_frame target{};
		pl_frame_from_swapchain(&target, &swapchainFrame);
		// The colorspace hint requests SDR Rec.709, while this returned value is
		// the swapchain's actual negotiated colorspace and must remain authoritative.
		target.color.hdr.min_luma = 0.1f;
		target.color.hdr.max_luma = static_cast<float>(sdrTargetNits);
		pl_rect2df_aspect_copy(&target.crop, &image.crop, 0.0f);

		const bool rendered = pl_render_image(renderer, &image, &target, &renderParams);
		const bool submitted = pl_swapchain_submit_frame(swapchain);
		if (submitted)
			pl_swapchain_swap_buffers(swapchain);
		if (rendered && submitted)
		{
			lastRenderedEotf = state.eotf;
			lastRenderedColorspace = state.colorspace;
		}
		return rendered && submitted;
	}

	void Resize(HWND videoHwnd)
	{
		std::lock_guard<std::mutex> guard(renderMutex);
		if (!swapchain)
			return;

		RECT client{};
		if (!GetClientRect(videoHwnd, &client))
			return;
		int width = std::max<LONG>(1, client.right - client.left);
		int height = std::max<LONG>(1, client.bottom - client.top);
		if (!pl_swapchain_resize(swapchain, &width, &height))
			DebugLog::Log("libplacebo: swapchain resize failed (%d x %d)", width, height);
	}

	bool IsGpuFailed()
	{
		std::lock_guard<std::mutex> guard(renderMutex);
		return d3d11 && pl_gpu_is_failed(d3d11->gpu);
	}
};


LibplaceboVideoRenderer::LibplaceboVideoRenderer(
	IRendererCallback& callback,
	HWND videoHwnd,
	ITimingClock* timingClock,
	bool useFrameQueue,
	size_t frameQueueMaxSize) :
	m_callback(callback),
	m_videoHwnd(videoHwnd),
	m_timingClock(timingClock),
	m_useFrameQueue(useFrameQueue),
	m_frameQueueMaxSize(std::max<size_t>(1, frameQueueMaxSize))
{
	m_callback.OnRendererDetailString(TEXT("libplacebo D3D11 (experimental, HDR to SDR)"));
}


LibplaceboVideoRenderer::~LibplaceboVideoRenderer()
{
	if (m_renderThread.joinable())
	{
		{
			std::lock_guard<std::mutex> guard(m_queueMutex);
			m_stopRequested = true;
		}
		m_queueChanged.notify_all();
		m_renderThread.join();
	}
	ClearQueue();
	m_impl.reset();
}


bool LibplaceboVideoRenderer::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("null video state is invalid");

	std::lock_guard<std::mutex> guard(m_stateMutex);
	if (m_videoState &&
		(videoState->valid == false ||
		 !videoState->displayMode ||
		 !m_videoState->displayMode ||
		 *videoState->displayMode != *m_videoState->displayMode ||
		 videoState->videoFrameEncoding != m_videoState->videoFrameEncoding))
	{
		return false;
	}

	const EOTF previousEotf = m_videoState ? m_videoState->eotf : EOTF::UNKNOWN;
	const ColorSpace previousColorspace = m_videoState ? m_videoState->colorspace : ColorSpace::UNKNOWN;
	m_videoState = new VideoState(*videoState);

	const bool sourceColorTransition =
		previousEotf != EOTF::UNKNOWN &&
		(previousEotf != m_videoState->eotf || previousColorspace != m_videoState->colorspace);
	if (sourceColorTransition)
	{
		DebugLog::Log(
			"libplacebo source transition accepted in place: %s/%s -> %s/%s; output remains SDR Rec.709",
			CStringA(ToString(previousEotf)).GetString(),
			CStringA(ToString(previousColorspace)).GetString(),
			CStringA(ToString(m_videoState->eotf)).GetString(),
			CStringA(ToString(m_videoState->colorspace)).GetString());
	}

	return true;
}


void LibplaceboVideoRenderer::OnVideoFrame(VideoFrame& videoFrame)
{
	if (m_state.load(std::memory_order_acquire) != RendererState::RENDERSTATE_RENDERING)
		return;

	const uint64_t counter = m_frameCounter.fetch_add(1, std::memory_order_relaxed);
	if (m_timingClock && counter % 20 == 0)
	{
		m_entryLatencyMs.store(
			TimingClockDiffMs(
				videoFrame.GetTimingTimestamp(),
				m_timingClock->TimingClockNow(),
				m_timingClock->TimingClockTicksPerSecond()),
			std::memory_order_relaxed);
	}

	VideoStateComPtr frameState;
	{
		std::lock_guard<std::mutex> guard(m_stateMutex);
		frameState = m_videoState;
	}
	if (!frameState)
	{
		m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		const size_t queueLimit = m_useFrameQueue ? m_frameQueueMaxSize : 1;
		while (m_frameQueue.size() >= queueLimit)
		{
			m_frameQueue.front().frame.SourceBufferRelease();
			m_frameQueue.pop_front();
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
		}
		videoFrame.SourceBufferAddRef();
		try
		{
			m_frameQueue.push_back({ videoFrame, frameState });
		}
		catch (const std::exception& e)
		{
			videoFrame.SourceBufferRelease();
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
			DebugLog::Log("libplacebo frame enqueue failed: %s", e.what());
			return;
		}
	}
	m_queueChanged.notify_one();
}


HRESULT LibplaceboVideoRenderer::OnWindowsEvent(LONG_PTR, LONG_PTR)
{
	return S_OK;
}


void LibplaceboVideoRenderer::Build()
{
	VideoStateComPtr state;
	{
		std::lock_guard<std::mutex> guard(m_stateMutex);
		if (!m_videoState || !m_videoState->valid || !m_videoState->displayMode)
			throw std::runtime_error("libplacebo requires a valid video state before Build");
		state = m_videoState;
	}

	std::unique_ptr<Impl> impl(new Impl());
	impl->Initialize(m_videoHwnd, state);
	m_impl = std::move(impl);
	SetState(RendererState::RENDERSTATE_READY);
}


void LibplaceboVideoRenderer::Start()
{
	if (!m_impl || m_state.load(std::memory_order_acquire) != RendererState::RENDERSTATE_READY)
		throw std::runtime_error("libplacebo renderer is not ready");

	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		m_stopRequested = false;
	}
	m_renderThread = std::thread(&LibplaceboVideoRenderer::RenderLoop, this);
	SetState(RendererState::RENDERSTATE_RENDERING);
}


void LibplaceboVideoRenderer::Stop()
{
	{
		std::lock_guard<std::mutex> guard(m_queueMutex);
		m_stopRequested = true;
	}
	m_queueChanged.notify_all();
	if (m_renderThread.joinable())
		m_renderThread.join();
	ClearQueue();
	SetState(RendererState::RENDERSTATE_STOPPED);
}


void LibplaceboVideoRenderer::Reset()
{
	ClearQueue();
	if (m_impl && m_impl->renderer)
	{
		std::lock_guard<std::mutex> guard(m_impl->renderMutex);
		pl_renderer_flush_cache(m_impl->renderer);
	}
	m_frameCounter.store(0, std::memory_order_relaxed);
	DebugLog::Log("libplacebo renderer reset: queue cleared and renderer cache flushed");
}


void LibplaceboVideoRenderer::ResetLiveQueue()
{
	ClearQueue();
}


void LibplaceboVideoRenderer::OnSize()
{
	if (m_impl)
		m_impl->Resize(m_videoHwnd);
}


void LibplaceboVideoRenderer::OnPaint()
{
	// The flip-model swapchain retains the last presented frame.
}


void LibplaceboVideoRenderer::SetFrameQueueMaxSize(size_t size)
{
	std::lock_guard<std::mutex> guard(m_queueMutex);
	m_frameQueueMaxSize = std::max<size_t>(1, size);
	while (m_frameQueue.size() > m_frameQueueMaxSize)
	{
		m_frameQueue.front().frame.SourceBufferRelease();
		m_frameQueue.pop_front();
		m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
	}
}


size_t LibplaceboVideoRenderer::GetFrameQueueSize()
{
	std::lock_guard<std::mutex> guard(m_queueMutex);
	return m_frameQueue.size();
}


size_t LibplaceboVideoRenderer::GetConvertedQueueSize()
{
	return 0;
}


double LibplaceboVideoRenderer::EntryLatencyMs() const
{
	return m_entryLatencyMs.load(std::memory_order_relaxed);
}


double LibplaceboVideoRenderer::ExitLatencyMs() const
{
	return m_exitLatencyMs.load(std::memory_order_relaxed);
}


uint64_t LibplaceboVideoRenderer::DroppedFrameCount() const
{
	return m_droppedFrames.load(std::memory_order_relaxed);
}


bool LibplaceboVideoRenderer::GetConversionPerformance(
	double& currentUs, double& avg10s, double& max10s) const
{
	if (!m_impl || !m_impl->formatter)
		return false;
	std::lock_guard<std::mutex> guard(m_impl->renderMutex);
	m_impl->formatter->GetConversionPerformance(currentUs, avg10s, max10s);
	return currentUs > 0.0 || avg10s > 0.0 || max10s > 0.0;
}


void LibplaceboVideoRenderer::RenderLoop()
{
	unsigned int consecutiveFailures = 0;
	for (;;)
	{
		VideoFrame frame;
		VideoStateComPtr state;
		{
			std::unique_lock<std::mutex> lock(m_queueMutex);
			m_queueChanged.wait(lock, [this]() { return m_stopRequested || !m_frameQueue.empty(); });
			if (m_stopRequested)
				break;
			frame = m_frameQueue.front().frame;
			state = m_frameQueue.front().state;
			m_frameQueue.pop_front();
		}

		bool rendered = false;
		try
		{
			rendered = state && m_impl->Render(frame, state);
		}
		catch (const std::exception& e)
		{
			DebugLog::Log("libplacebo render failure: %s", e.what());
		}
		catch (...)
		{
			DebugLog::Log("libplacebo render failure: unknown exception");
		}

		if (!rendered)
		{
			frame.SourceBufferRelease();
			m_droppedFrames.fetch_add(1, std::memory_order_relaxed);
			const bool gpuFailed = m_impl->IsGpuFailed();
			if (gpuFailed)
			{
				DebugLog::Log(
					"libplacebo renderer failure requires reconstruction: gpu_failed=%d consecutive_failures=%u",
					1,
					consecutiveFailures);
				SetState(RendererState::RENDERSTATE_FAILED);
				break;
			}

			// Swapchain acquisition may be unavailable by design while the render
			// window is hidden or minimized. Do not turn that presentation pause
			// into a device failure or carry it into a later visible failure streak.
			if (!IsWindowVisible(m_videoHwnd) || IsIconic(m_videoHwnd))
			{
				consecutiveFailures = 0;
				continue;
			}

			if (++consecutiveFailures == 1)
				DebugLog::Log("libplacebo failed to render a visible frame; retrying");
			if (consecutiveFailures >= 300)
			{
				DebugLog::Log(
					"libplacebo renderer failed 300 consecutive visible frames with a healthy GPU; marking renderer failed for swapchain reconstruction");
				SetState(RendererState::RENDERSTATE_FAILED);
				break;
			}
			continue;
		}
		consecutiveFailures = 0;

		if (m_timingClock)
		{
			m_exitLatencyMs.store(
				TimingClockDiffMs(
					frame.GetTimingTimestamp(),
					m_timingClock->TimingClockNow(),
					m_timingClock->TimingClockTicksPerSecond()),
				std::memory_order_relaxed);
		}

		frame.SourceBufferRelease();
	}
}


void LibplaceboVideoRenderer::ClearQueue()
{
	std::lock_guard<std::mutex> guard(m_queueMutex);
	for (QueuedFrame& queuedFrame : m_frameQueue)
		queuedFrame.frame.SourceBufferRelease();
	m_frameQueue.clear();
}


void LibplaceboVideoRenderer::SetState(RendererState state)
{
	m_state.store(state, std::memory_order_release);
	m_callback.OnRendererState(state);
}
