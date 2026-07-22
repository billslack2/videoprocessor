#pragma once

#include <IRenderer.h>
#include <ITimingClock.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>


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
	void SetFrameQueueMaxSize(size_t size) override;
	void SetSceneAwareTimingCorrection(bool) override {}
	bool SetScreenProfile(bool scopeScreen, CString& activeProfile) override;
	size_t GetFrameQueueSize() override;
	double EntryLatencyMs() const override;
	double ExitLatencyMs() const override;
	uint64_t DroppedFrameCount() const override;
	bool GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const override;

private:
	struct Impl;
	struct QueuedFrame
	{
		VideoFrame frame;
		VideoStateComPtr state;
	};

	void RenderLoop();
	void ClearQueue();
	void SetState(RendererState state);

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
	size_t m_frameQueueMaxSize = 1;
	bool m_stopRequested = false;
	std::thread m_renderThread;

	std::unique_ptr<Impl> m_impl;
	std::atomic<double> m_entryLatencyMs{0.0};
	std::atomic<double> m_exitLatencyMs{0.0};
	std::atomic<uint64_t> m_droppedFrames{0};
	std::atomic<uint64_t> m_frameCounter{0};
	std::atomic<bool> m_scopeScreenActive{false};
};
