#pragma once

#include <IRenderer.h>
#include <ITimingClock.h>
#include <VideoConversionOverride.h>


// Core-side proxy for the optional VideoProcessorLibplacebo.dll plugin. The
// main executable has no import from either the plugin or libplacebo itself.
class LibplaceboPluginVideoRenderer final : public IVideoRenderer
{
public:
	static bool IsAvailable();

	LibplaceboPluginVideoRenderer(
		IRendererCallback& callback,
		HWND videoHwnd,
		ITimingClock* timingClock,
		bool useFrameQueue,
		size_t frameQueueMaxSize,
		VideoConversionOverride videoConversionOverride);
	~LibplaceboPluginVideoRenderer() override;

	size_t GetConvertedQueueSize() override;
	bool OnVideoState(VideoStateComPtr& videoState) override;
	bool SupportsDynamicVideoState() const override;
	void OnVideoFrame(VideoFrame& videoFrame) override;
	bool HasPresentedLiveFrame() const override;
	const char* PresentedLiveFrameEvidence() const override;
	uint64_t PresentedFrameCount() const override;
	bool PersistShaderCache() override;
	void SetNonCapturingPreparationMode(bool enabled) override;
	bool ReloadConfiguredShaderPrewarm() override;
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
	void SetActivePictureLookaheadFrames(size_t frames) override;
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
	bool SelectDisplayRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired) override;
	bool SelectShaderRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired) override;
	bool RefreshShaderRule(CString& activeRule,
		bool& rendererRestartRequired) override;
	std::vector<CString> ActiveShaders() const override;
	bool GetActiveShaderSections(
		std::vector<CString>& sections) const override;
	CString ActiveShaderRule() const override;
	bool ApplyApplicationState(const UnifiedProfileRuntime::Snapshot& snapshot,
		CString& activeState,
		bool& rendererRestartRequired,
		bool& liveResetRequired) override;
	size_t GetFrameQueueSize() override;
	double EntryLatencyMs() const override;
	double ExitLatencyMs() const override;
	uint64_t DroppedFrameCount() const override;
	bool GetOutputModeInfo(CString& details) const override;
	bool GetOutputContractStatus(
		RendererOutputContract::Status& status) const override;
	bool RequestRenderedOutputCapture(CString& status) override;
	bool GetDisplayLutInfo(CString& details) const override;
	bool GetVideoIngressInfo(CString& details) const override;
	bool GetPresentationTargetTiming(double& leadMs,
		double& captureToTargetMs) const override;
	bool GetPresentationTimingStatus(CString& status) const override;
	bool SupportsNativeStatsOverlay() const override;
	bool SetNativeStatsOverlay(const uint8_t* pixels, size_t byteCount,
		int width, int height, int stride) override;
	bool SetNativeSweepOverlay(const uint8_t* pixels, size_t byteCount,
		int width, int height, int stride) override;
	bool GetConversionPerformance(
		double& currentUs, double& avg10s, double& max10s) const override;
	bool GetFrameRateAndPPM(double& measuredFps, int& ppmDeviation) const override;

private:
	IVideoRenderer* m_renderer = nullptr;
	void (__cdecl *m_destroyRenderer)(IVideoRenderer*) = nullptr;
};
