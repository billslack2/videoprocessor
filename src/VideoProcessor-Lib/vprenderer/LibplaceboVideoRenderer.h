#pragma once

#include <IRenderer.h>
#include <ITimingClock.h>
#include <VideoConversionOverride.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <map>
#include <mutex>
#include <thread>
#include <string>
#include <vector>


// In-process D3D11 renderer. HDR input is color-managed and tone-mapped to
// an SDR Rec.709 swapchain; this backend deliberately does not emit HDR.
class LibplaceboVideoRenderer final : public IVideoRenderer
{
public:
	LibplaceboVideoRenderer(
		IRendererCallback& callback,
		HWND videoHwnd,
		ITimingClock* timingClock,
		bool useFrameQueue,
		size_t frameQueueMaxSize,
		VideoConversionOverride videoConversionOverride);
	~LibplaceboVideoRenderer() override;

	size_t GetConvertedQueueSize() override;
	bool OnVideoState(VideoStateComPtr& videoState) override;
	bool SupportsDynamicVideoState() const override { return true; }
	void OnVideoFrame(VideoFrame& videoFrame) override;
	bool HasPresentedLiveFrame() const override
	{
		return m_hasPresentedLiveFrame.load(std::memory_order_acquire);
	}
	const char* PresentedLiveFrameEvidence() const override
	{
		return "swapchain-submitted";
	}
	HRESULT OnWindowsEvent(LONG_PTR param1, LONG_PTR param2) override;
	void Build() override;
	void Start() override;
	void Stop() override;
	void Retire() noexcept override;
	void Reset() override;
	void ResetLiveQueue() override;
	void OnSize() override;
	void OnPaint() override;
	void OnDisplayChange() override;
	void SetFrameQueueMaxSize(size_t size) override;
	void SetQueueFramePolicy(size_t startupPrerollFrames,
		size_t steadyReserveFrames, bool hasSteadyReserveFrames) override;
	void SetSceneAwareTimingCorrection(bool enabled) override;
	uint64_t SceneAwareCorrectionDropCount() const override;
	uint64_t SceneAwareCorrectionRepeatCount() const override;
	uint64_t SceneAwareDetectedCount() const override;
	bool GetSceneDetectionStatus(CString& status) const override;
	bool GetSceneTimingPrediction(double& secondsUntilCorrection,
		double& secondsUntilPlan, int& action, bool& planned) const override;
	bool GetSceneTimingLastCorrection(int& action,
		double& secondsFromDeadline, uint64_t& correctionTick) const override;
	bool SceneTimingRatesCompatible() const override;
	bool GetSceneTimingStatus(CString& status) const override;
	bool GetSceneTimingDueStatus(int& action, CString& reason) const override;
	bool SetScreenProfile(bool scopeScreen, CString& activeProfile,
		bool& rendererRestartRequired) override;
	bool SelectDisplayRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired) override;
	bool SelectShaderRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired) override;
	bool RefreshShaderRule(CString& activeRule,
		bool& rendererRestartRequired) override;
	std::vector<CString> ActiveShaders() const override;
	CString ActiveShaderRule() const override;
	bool ApplyApplicationState(const UnifiedProfileRuntime::Snapshot& snapshot,
		CString& activeState,
		bool& rendererRestartRequired) override;
	size_t GetFrameQueueSize() override;
	double EntryLatencyMs() const override;
	double ExitLatencyMs() const override;
	uint64_t DroppedFrameCount() const override;
	bool GetOutputModeInfo(CString& details) const override;
	bool GetDisplayLutInfo(CString& details) const override;
	bool GetVideoIngressInfo(CString& details) const override;
	bool GetPresentationTargetTiming(double& leadMs,
		double& captureToTargetMs) const override;
	bool SupportsNativeStatsOverlay() const override { return true; }
	bool SetNativeStatsOverlay(const uint8_t* pixels, size_t byteCount,
		int width, int height, int stride) override;
	bool GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const override;
	bool GetFrameRateAndPPM(double& measuredFps, int& ppmDeviation) const override;

private:
	struct Impl;
	struct QueuedFrame
	{
		VideoFrame frame;
		VideoStateComPtr state;
		uint64_t generation = 0;
		uint64_t sourceSequence = 0;
		int64_t enqueueQpc = 0;
		bool cadenceRepeat = false;
		uint64_t cadenceActionId = 0;
		uint64_t cadencePolicyGeneration = 0;
		uint64_t cadenceDetectorGeneration = 0;
		uint64_t cadencePresentationDebt = 0;
		uint32_t cadencePresentId = 0;
		double cadenceDeadlineSeconds = 0.0;
	};

	void RenderLoop();
	void ClearQueue(const char* reason = "queue clear");
	void BeginQueueGeneration(const char* reason, bool clearStopRequest = false);
	void ClearQueueLocked(const char* reason);
	size_t PrefillTargetLocked() const;
	bool CanDequeueLocked() const;
	void SetState(RendererState state);
	void UpdateFrameRateAndPPM(timingclocktime_t frameTimestamp);
	void ResetFrameRateAndPPM();
	bool ApplyScreenProfile(bool scopeScreen, CString& activeProfile,
		bool persistLegacyState);

	IRendererCallback& m_callback;
	HWND m_videoHwnd = nullptr;
	ITimingClock* m_timingClock = nullptr;
	bool m_useFrameQueue = true;
	VideoConversionOverride m_videoConversionOverride =
		VideoConversionOverride::VIDEOCONVERSION_NONE;

	mutable std::mutex m_stateMutex;
	VideoStateComPtr m_videoState;
	std::atomic<RendererState> m_state{RendererState::RENDERSTATE_UNKNOWN};
	std::string m_requestedShaderSelector;
	uint64_t m_shaderRendererGeneration = 0;
	mutable uint64_t m_lastReportedShaderStatusSerial = 0;

	mutable std::mutex m_queueMutex;
	std::condition_variable m_queueChanged;
	std::deque<QueuedFrame> m_frameQueue;
	size_t m_frameQueueDesiredDepth = 1;
	size_t m_frameQueueMaxSize = 1;
	uint64_t m_queueGeneration = 0;
	uint64_t m_overflowLoggedGeneration = 0;
	bool m_startupPrefillPending = false;
	int64_t m_queueDepthWindowStartNs = 0;
	size_t m_queueDepthWindowMin = 0;
	size_t m_queueDepthWindowMax = 0;
	uint64_t m_queueDepthWindowDequeues = 0;
	bool m_queueDepthWindowHasSamples = false;
	bool m_stopRequested = false;
	std::thread m_renderThread;

	std::unique_ptr<Impl> m_impl;
	std::atomic<double> m_entryLatencyMs{0.0};
	std::atomic<double> m_exitLatencyMs{0.0};
	std::atomic_bool m_presentationTargetTimingKnown{false};
	std::atomic<double> m_presentationTargetLeadMs{0.0};
	std::atomic<double> m_captureToPresentationTargetMs{0.0};
	std::atomic<uint64_t> m_droppedFrames{0};
	std::atomic<uint64_t> m_missingFrameStateDrops{0};
	std::atomic<uint64_t> m_renderFailureDrops{0};
	std::atomic_bool m_sceneDetectionEnabled{false};
	std::atomic<uint64_t> m_sceneDetectorGeneration{1};
	std::atomic<uint64_t> m_sceneDetectedCount{0};
	std::atomic<uint64_t> m_sceneCorrectionDropCount{0};
	std::atomic<uint64_t> m_sceneCorrectionRepeatCount{0};
	std::atomic_bool m_sceneTimingRatesCompatible{false};
	std::atomic<int> m_scenePredictedAction{0};
	std::atomic_bool m_sceneCorrectionPlanned{false};
	std::atomic<double> m_sceneSecondsUntilCorrection{0.0};
	std::atomic<double> m_sceneSecondsUntilPlan{0.0};
	std::atomic<int> m_sceneLastCorrectionAction{0};
	std::atomic<double> m_sceneLastCorrectionSecondsFromDeadline{0.0};
	std::atomic<uint64_t> m_sceneLastCorrectionTick{0};
	std::atomic<int> m_sceneTimingStatus{0};
	std::atomic<uint32_t> m_sceneTimingRateSamples{0};
	std::atomic<double> m_sceneTimingMismatchPpm{0.0};
	// One coherent due/action/reason snapshot: low two bits encode action
	// (1=drop, 2=repeat), remaining bits encode AlphaCadenceBlockReason.
	std::atomic<uint32_t> m_sceneCorrectionDueState{0};
	std::atomic<int> m_sceneDetectionStatus{0};
	std::atomic<uint64_t> m_frameCounter{0};
	std::atomic<uint64_t> m_sourceSequence{0};
	std::atomic_bool m_hasPresentedLiveFrame{false};
	// Capture-timestamp cadence diagnostics.  This intentionally mirrors the
	// DirectShow renderer measurement but is diagnostic-only: the optional
	// renderer does not feed or alter source PPM correction.
	std::atomic<timingclocktime_t> m_firstPpmFrameTimestamp{0};
	std::atomic<timingclocktime_t> m_lastPpmFrameTimestamp{0};
	std::atomic<timingclocktime_t> m_lastPpmPublishTimestamp{0};
	std::atomic<timingclocktime_t> m_expectedPpmFrameTicks{0};
	std::atomic<uint64_t> m_ppmFrameCount{0};
	std::atomic<double> m_measuredFrameRate{0.0};
	std::atomic<int> m_ppmDeviation{0};
	std::atomic_bool m_hasPpmData{false};
	std::atomic<bool> m_scopeScreenActive{false};
	std::atomic<uint64_t> m_screenProfileRequestSerial{0};
	std::atomic<int64_t> m_screenProfileRequestNs{0};
	// The automatic [display_rules] selection that built the current renderer.
	// A change requests a normal renderer rebuild so output settings cannot change
	// underneath queued frames.
	std::string m_activeDisplayRule;
	// A shortcut-selected profile.  It takes precedence over [display_rules]
	// and persists across renderer reconstruction until another profile (or
	// automatic mode) is selected.
	std::string m_manualDisplayRule;
	// One manual selection per independent unified profile group. This state is
	// copied into rebuilt renderer instances only after the selection is accepted.
	std::map<std::string, std::string> m_manualUnifiedProfiles;
};
