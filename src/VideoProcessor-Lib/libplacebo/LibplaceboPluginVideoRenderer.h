#pragma once

#include <IRenderer.h>
#include <ITimingClock.h>


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
		size_t frameQueueMaxSize);
	~LibplaceboPluginVideoRenderer() override;

	size_t GetConvertedQueueSize() override;
	bool OnVideoState(VideoStateComPtr& videoState) override;
	bool SupportsDynamicVideoState() const override;
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
	bool GetSceneTimingStatus(CString& status) const override;
	bool SetScreenProfile(bool scopeScreen, CString& activeProfile,
		bool& rendererRestartRequired) override;
	bool SelectDisplayRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired) override;
	bool SelectUnifiedProfileKey(const CString& key, CString& activeProfiles,
		bool& rendererRestartRequired) override;
	size_t GetFrameQueueSize() override;
	double EntryLatencyMs() const override;
	double ExitLatencyMs() const override;
	uint64_t DroppedFrameCount() const override;
	bool GetOutputModeInfo(CString& details) const override;
	bool GetDisplayLutInfo(CString& details) const override;
	bool SupportsNativeStatsOverlay() const override;
	bool SetNativeStatsOverlay(const uint8_t* pixels, size_t byteCount,
		int width, int height, int stride) override;
	bool GetConversionPerformance(
		double& currentUs, double& avg10s, double& max10s) const override;
	bool GetFrameRateAndPPM(double& measuredFps, int& ppmDeviation) const override;

private:
	IVideoRenderer* m_renderer = nullptr;
	void (__cdecl *m_destroyRenderer)(IVideoRenderer*) = nullptr;
};
