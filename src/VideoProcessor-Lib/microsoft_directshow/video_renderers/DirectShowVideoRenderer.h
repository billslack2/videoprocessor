/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <dshow.h>
#include <dxva.h>

#include <IRenderer.h>
#include <PixelValueRange.h>
#include <VideoState.h>
#include <video_frame_formatter/IVideoFrameFormatter.h>
#include <ITimingClock.h>
#include <VideoConversionOverride.h>
#include <LiveFrameCounterTracker.h>
#include <microsoft_directshow/live_source_filter/CLiveSource.h>
#include <microsoft_directshow/live_source_filter/ALiveSourceVideoOutputPin.h>
#include <microsoft_directshow/DirectShowTimingClock.h>
#include <microsoft_directshow/video_renderers/DirectShowGraphExecutor.h>
#include <deque>
#include <atomic>
#include <mutex>
#include <shared_mutex>


/**
 * Abstract DirectShow video renderer
 */
class DirectShowVideoRenderer:
	public IVideoRenderer
{
public:

	DirectShowVideoRenderer(
		IRendererCallback& callback,
		HWND videoHwnd,
		HWND eventHwnd,
		UINT eventMsg,
		ITimingClock* timingClock,
		DirectShowStartStopTimeMethod timestamp,
		bool useFrameQueue,
		size_t frameQueueMaxSize,
		VideoConversionOverride videoConversionOverride);
	virtual ~DirectShowVideoRenderer();

	size_t GetConvertedQueueSize() override;

	// IVideoRenderer
	bool OnVideoState(VideoStateComPtr&) override;
	void OnVideoFrame(VideoFrame& videoFrame) override;
	bool HasPresentedLiveFrame() const override
	{
		if (!m_resetReadyForReveal.load(std::memory_order_acquire))
			return false;
		if (!m_useFrameQueue)
		{
			return HasSufficientDownstreamPreroll(
				m_unbufferedDeliverySuccessCount.load(
					std::memory_order_acquire));
		}
		RendererLivenessSnapshot snapshot;
		return GetLivenessSnapshot(snapshot) &&
			HasCurrentEpochDownstreamDelivery(snapshot);
	}
	const char* PresentedLiveFrameEvidence() const override
	{
		return m_useFrameQueue ?
			"current-epoch-downstream-prerolled" :
			"unbuffered-downstream-prerolled";
	}
	bool GetLivenessSnapshot(RendererLivenessSnapshot& snapshot) const override;
	bool GetLatencySnapshot(RendererLatencySnapshot& snapshot) const override;
	HRESULT OnWindowsEvent(LONG_PTR param1, LONG_PTR param2) override;
	void Build() override;
	void Start() override;
	void Stop() override;
	void StopWithIngressDrain(
		const std::function<void()>& drainAfterGraphStop) override;
	void Reset() override;
	void ResetWithIngressDrain(
		const std::function<void()>& drainAfterGraphStop) override;
	bool RetargetWindowWithIngressDrain(
		uintptr_t targetWindow,
		const std::function<void()>& drainAfterGraphStop) override;
	void ResetLiveQueue() override;
	void SetOutputReadinessDeliveryReserve(size_t reserveFrames) override;
	void SetQueueFramePolicy(size_t startupPrerollFrames,
		size_t steadyReserveFrames, bool steadyReserveConfigured) override;
	void SetPresentationLeadFrames(
		size_t frames, bool configured) override;
	void SetActivePictureLookaheadFrames(size_t frames) override;
	void SetActivePictureLookaheadMode(ActivePictureLookaheadMode mode) override;
	void SetResetRequestSink(
		std::shared_ptr<IRendererResetRequestSink> sink) override;
	void Retire() noexcept override;
	void OnSize() override;
	void SetFrameQueueMaxSize(size_t) override;
	void SetSceneAwareTimingCorrection(bool) override;
	void SetSceneCorrectionUpstreamSample(bool) override;
	void SetSubtitleRepositioning(bool) override;
	void SetSubtitleRepositioningMode(SubtitleRepositionMode mode) override;
	void SetSceneTimingRates(double displayRefreshRateHz, double measuredCaptureRateHz) override;
	void SetSceneTimingReadiness(bool ready, uint64_t intervalsObserved) override;
	void SetSceneTimingPhase(int64_t vblankQpc, int64_t refreshPeriodQpc, int64_t qpcFrequency) override;
	size_t GetFrameQueueSize() override;
	double EntryLatencyMs() const override;
	double ExitLatencyMs() const override;
	uint64_t DroppedFrameCount() const override;
	uint64_t SceneAwareCorrectionDropCount() const override;
	uint64_t SceneAwareCorrectionRepeatCount() const override;
	uint64_t SceneAwareDetectedCount() const override;
	uint64_t SceneAwareLateCandidateCount() const override;
	bool GetSceneTimingPrediction(double& secondsUntilCorrection,
		double& secondsUntilPlan, int& action, bool& planned) const override;
	bool GetSceneTimingLastCorrection(int& action,
		double& secondsFromDeadline, uint64_t& correctionTick) const override;
	bool SceneTimingRatesCompatible() const override;
	
	// Get conversion performance from the video frame formatter
	bool GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const override
	{
		std::shared_lock<std::shared_mutex> lock(
			m_liveSourceLifetimeMutex);
		if (m_videoFramFormatter)
		{
			m_videoFramFormatter->GetConversionPerformance(currentUs, avg10s, max10s);
			return (currentUs > 0.0 || avg10s > 0.0 || max10s > 0.0);
		}
		return false;
	}

	// Get current PPM correction information (override for RATIONAL_RATIONAL support)
	bool GetPPMCorrectionInfo(int& ppmValue, bool& hasCorrection, CString& source) const override;
	
	// Get frame rate measurement and PPM deviation (for timing diagnostics)
	bool GetFrameRateAndPPM(double& measuredFps, int& ppmDeviation) const override;
	bool GetDetectedDisplayRefreshRate(double& refreshRateHz) const override;
	bool GetActivePictureAspectRatio(double& aspectRatio) const;
	bool GetActivePictureRectangle(ActivePictureRectangle& rectangle) const;

protected:
	enum GraphCommandKey : DirectShowGraphExecutor::CoalescingKey
	{
		GRAPH_COMMAND_EVENT_DRAIN = 1,
		GRAPH_COMMAND_RESIZE = 2,
		GRAPH_COMMAND_FRAME_QUEUE_SIZE = 3,
		GRAPH_COMMAND_SCENE_CORRECTION = 4,
		GRAPH_COMMAND_SCENE_SAMPLE = 5,
		GRAPH_COMMAND_SUBTITLE_MODE = 6,
		GRAPH_COMMAND_SCENE_RATES = 7,
		GRAPH_COMMAND_SCENE_READINESS = 8,
		GRAPH_COMMAND_SCENE_PHASE = 9,
		GRAPH_COMMAND_SHADER_SELECT = 10,
		GRAPH_COMMAND_SHADER_REFRESH = 11,
		GRAPH_COMMAND_APPLICATION_STATE = 13,
		GRAPH_COMMAND_PAINT = 14,
		GRAPH_COMMAND_VIDEO_STATE = 15,
		GRAPH_COMMAND_HDR_STATE = 16,
		// Low-rate, read-only madVR diagnostics must run in the graph owner's
		// COM apartment. It is intentionally separate from graph control.
		GRAPH_COMMAND_MADVR_RUNTIME_TELEMETRY = 17,
		GRAPH_COMMAND_MADVR_NATIVE_OSD = 18
	};

	template<typename Function>
	auto InvokeOnGraphThread(Function&& function) -> decltype(function())
	{
		return m_graphExecutor.Invoke(std::forward<Function>(function));
	}
	bool PostCoalescedGraphCommand(
		DirectShowGraphExecutor::CoalescingKey key,
		std::function<void()> function)
	{
		return m_graphExecutor.PostCoalesced(key, std::move(function));
	}
	bool IsGraphThread() const noexcept
	{
		return m_graphExecutor.IsOwnerThread();
	}
	void AssertGraphThread() const
	{
		assert(IsGraphThread());
	}

	IRendererCallback& m_callback;
	HWND m_videoHwnd;
	HWND m_eventHwnd;
	UINT m_eventMsg;
	ITimingClock* m_timingClock;
	VideoStateComPtr m_videoState;
	std::mutex m_videoStateAdmissionMutex;
	VideoStateComPtr m_admissionVideoState;
	DirectShowStartStopTimeMethod m_timestamp;
	bool m_useFrameQueue;
	size_t m_frameQueueMaxSize;
	// The dialog can publish this policy while Build() is queued on the graph
	// thread.  Retain it so a newly-created live source receives it instead of
	// silently falling back to its default queue behaviour.
	std::atomic<size_t> m_queueStartupPrerollFrames{0};
	std::atomic<size_t> m_queueSteadyTargetFrames{0};
	std::atomic_bool m_queueSteadyTargetConfigured{false};
	std::atomic<size_t> m_presentationLeadFrames{0};
	std::atomic_bool m_presentationLeadFramesConfigured{false};
	std::atomic<size_t> m_activePictureLookaheadFrames{0};
	std::atomic<ActivePictureLookaheadMode> m_activePictureLookaheadMode{
		ActivePictureLookaheadMode::OFF};
	VideoConversionOverride m_videoConversionOverride;
	DXVA_NominalRange m_forceNominalRange = DXVA_NominalRange::DXVA_NominalRange_Unknown;
	DXVA_VideoTransferFunction m_forceVideoTransferFunction = DXVA_VideoTransferFunction::DXVA_VideoTransFunc_Unknown;
	DXVA_VideoTransferMatrix m_forceVideoTransferMatrix = DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_Unknown;
	DXVA_VideoPrimaries m_forceVideoPrimaries = DXVA_VideoPrimaries::DXVA_VideoPrimaries_Unknown;

	LONG m_renderBoxWidth = 0;
	LONG m_renderBoxHeight = 0;

	IGraphBuilder* m_pGraph = nullptr;
	IMediaControl* m_pControl = nullptr;
	IMediaEventEx* m_pEvent = nullptr;
	IVideoWindow* m_videoWindow = nullptr;
	IFilterGraph2* m_pGraph2 = nullptr;
	IMediaFilter* m_mediaFilter = nullptr;
	IAMGraphStreams* m_amGraphStreams = nullptr;
	IReferenceClock* m_referenceClock = nullptr;
	IVideoFrameFormatter* m_videoFramFormatter = nullptr;
	AM_MEDIA_TYPE m_pmt;
	CLiveSource* m_liveSource = nullptr;
	mutable std::shared_mutex m_liveSourceLifetimeMutex;
	std::shared_ptr<IRendererResetRequestSink> m_resetRequestSink;
	IBaseFilter* m_pLav = nullptr;
	IBaseFilter* m_pRenderer = nullptr;

	uint64_t m_frameCounter = 0;
	LiveFrameCounterTracker m_captureFrameCounterTracker;
	LiveSourceGapRecoveryPolicy m_sourceGapRecoveryPolicy;
	uint64_t m_missingFrameCounter = 0;
	double m_frameLatencyEntry = 0.0;
	std::atomic<uint64_t> m_unbufferedDeliverySuccessCount{0};
	std::atomic_bool m_resetReadyForReveal{false};
	// PPM measurement variables
	mutable timingclocktime_t m_firstFrameTime = 0;
	mutable timingclocktime_t m_lastFrameTime = 0;  
	mutable uint64_t m_frameCountForPPM = 0;
	// Updated on the capture callback and read by the UI/display-rate sampler.
	// Keep this published snapshot race-free without adding a lock to the live
	// frame path.
	mutable std::atomic<double> m_measuredFrameRate = 0.0;
	mutable std::atomic<int> m_ppmDeviation = 0;
	mutable std::atomic_bool m_hasPPMData = false;
	// Published by the graph-owner's IMadVRInfo query and read by the UI. It is
	// cleared for every graph lifetime boundary, so a prior HDMI mode can never
	// become the selected timing source for the next renderer instance.
	std::atomic<double> m_madVRDetectedRefreshRateHz{0.0};
	// Capture callbacks must never query renderer COM interfaces directly.
	// They coalesce one owner-thread, read-only snapshot at most every 30 s.
	std::atomic<ULONGLONG> m_lastMadVRRuntimeTelemetryTick{0};
	
	// Cumulative cadence is measured from the last full renderer restart. The
	// estimate is published periodically, but its measurement interval is never
	// reset between publications.
	mutable timingclocktime_t m_lastPpmMeasurementPublishTime = 0;

	// Handle Directshow graph events
	void OnGraphEvent(long evCode, LONG_PTR param1, LONG_PTR param2);
	HRESULT OnWindowsEventOnGraphThread();

	// Helper for state setting and callbacks
	void SetState(RendererState state);
	void PublishPendingStateCallback();
	void WakeForOwnerCompletion() const;
	void QueueRendererRestartCompletion()
	{
		m_pendingRendererRestart.store(true, std::memory_order_release);
		WakeForOwnerCompletion();
	}

	// This is the whole thing, with everything included
	virtual void GraphBuild();
	virtual void GraphTeardown();
	void GraphTeardownNoThrow() noexcept;
	bool GraphResourcesReleased() const noexcept;
	virtual void GraphRun();
	virtual void GraphStop();

	virtual void FilterGraphBuild();
	virtual void FilterGraphDestroy();

	// Window management
	virtual void WindowSetup();
	virtual void WindowTeardown();
	virtual void ResolveVideoWindowPlacement(LONG hostWidth,
		LONG hostHeight, bool fullscreen, LONG& x, LONG& y,
		LONG& width, LONG& height) const;
	void ApplyVideoWindowPlacement();
	bool IsVideoHostFullscreen() const;

	// Live source filter management
	virtual void LiveSourceBuildAndConnect();
	virtual void LiveSourceDisconnect();
	virtual void LiveSourceDestroy();

	// If called the implementation should instantiate a renderer
	// in m_pRenderer.
	// Most probably by calling CoCreateInstance(...).
	// A nullptr return will signal an error
	virtual void RendererBuild() = 0;

	// Add renderer to the graph and connect
	virtual void RendererConnect() = 0;
	virtual void RendererDestroy();
	void RefreshDownstreamPrimeTarget(
		const char* telemetrySource = "graph-connect-or-reset");
	void LogMadVRRuntimeInfo(const char* source, bool requireAnyKnownValue);
	void MaybeScheduleMadVRRuntimeTelemetry();

	virtual void MediaTypeGenerate() = 0;

	// PPM calculation helper
	void UpdatePPMMeasurement(timingclocktime_t frameTime) const;
	void ResetPPMMeasurement() const;

private:
	// Use SetState()
	std::atomic<RendererState> m_state{RendererState::RENDERSTATE_UNKNOWN};
	mutable std::mutex m_completionMutex;
	std::deque<RendererState> m_pendingStateCompletions;
	std::atomic_bool m_pendingRendererRestart{false};
	std::atomic_bool m_graphTeardownComplete{false};
	std::atomic_bool m_retired{false};
	DirectShowGraphExecutor m_graphExecutor;
};
