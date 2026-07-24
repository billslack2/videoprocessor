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
	void SetFrameQueueMaxSize(size_t size) override;
	void SetSceneAwareTimingCorrection(bool enabled) override;
	bool SetScreenProfile(bool scopeScreen, CString& activeProfile) override;
	bool SelectDisplayRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired) override;
	size_t GetFrameQueueSize() override;
	double EntryLatencyMs() const override;
	double ExitLatencyMs() const override;
	uint64_t DroppedFrameCount() const override;
	bool GetConversionPerformance(
		double& currentUs, double& avg10s, double& max10s) const override;
	bool GetFrameRateAndPPM(double& measuredFps, int& ppmDeviation) const override;

private:
	IVideoRenderer* m_renderer = nullptr;
	void (__cdecl *m_destroyRenderer)(IVideoRenderer*) = nullptr;
};
