#pragma once

#include <IRenderer.h>
#include <ITimingClock.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <string>


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
		size_t frameQueueMaxSize);
	~LibplaceboVideoRenderer() override;

	size_t GetConvertedQueueSize() override;
	bool OnVideoState(VideoStateComPtr& videoState) override;
	bool SupportsDynamicVideoState() const override { return true; }
	void OnVideoFrame(VideoFrame& videoFrame) override;
	HRESULT OnWindowsEvent(LONG_PTR param1, LONG_PTR param2) override;
	void Build() override;
	void Start() override;
	void Stop() override;
	void Reset() override;
	void ResetLiveQueue() override;
	void OnSize() override;
	void OnPaint() override;
	void OnDisplayChange() override;
	void SetFrameQueueMaxSize(size_t size) override;
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
	bool SetScreenProfile(bool scopeScreen, CString& activeProfile) override;
	bool SelectDisplayRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired) override;
	size_t GetFrameQueueSize() override;
	double EntryLatencyMs() const override;
	double ExitLatencyMs() const override;
	uint64_t DroppedFrameCount() const override;
	bool GetOutputModeInfo(CString& details) const override;
	bool GetDisplayLutInfo(CString& details) const override;
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
	};

	void RenderLoop();
	void ClearQueue();
	void BeginQueueGeneration(const char* reason, bool clearStopRequest = false);
	void ClearQueueLocked();
	size_t PrefillTargetLocked() const;
	bool CanDequeueLocked() const;
	void SetState(RendererState state);
	void UpdateFrameRateAndPPM(timingclocktime_t frameTimestamp);
	void ResetFrameRateAndPPM();

	IRendererCallback& m_callback;
	HWND m_videoHwnd = nullptr;
	ITimingClock* m_timingClock = nullptr;
	bool m_useFrameQueue = true;

	mutable std::mutex m_stateMutex;
	VideoStateComPtr m_videoState;
	std::atomic<RendererState> m_state{RendererState::RENDERSTATE_UNKNOWN};

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
	std::atomic<uint64_t> m_droppedFrames{0};
	std::atomic_bool m_sceneDetectionEnabled{false};
	std::atomic<uint64_t> m_sceneDetectorGeneration{1};
	std::atomic<uint64_t> m_sceneDetectedCount{0};
	std::atomic<uint64_t> m_sceneCorrectionDropCount{0};
	std::atomic<uint64_t> m_sceneCorrectionRepeatCount{0};
	std::atomic_bool m_sceneTimingRatesCompatible{false};
	std::atomic<int> m_scenePredictedAction{0};
	std::atomic_bool m_sceneCorrectionPlanned{false};
	std::atomic<double> m_sceneSecondsUntilCorrection{0.0};
	std::atomic<int> m_sceneLastCorrectionAction{0};
	std::atomic<double> m_sceneLastCorrectionSecondsFromDeadline{0.0};
	std::atomic<uint64_t> m_sceneLastCorrectionTick{0};
	std::atomic<int> m_sceneDetectionStatus{0};
	std::atomic<uint64_t> m_frameCounter{0};
	std::atomic<uint64_t> m_sourceSequence{0};
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
};
