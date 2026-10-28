/*
 * Copyright(C) 2026 Andrew Nevard
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <ACaptureDevice.h>
#include <ITimingClock.h>
#include <SourceCue.h>
#include <source_plugin/vp_source_plugin.h>


// A plugin's one input, as the dialog and the configuration editor list it.
#define SOURCE_PLUGIN_INPUT_NAME TEXT("Player")


/**
 * A source plugin (plugins/sources/*.dll) shown as a capture device.
 *
 * Behaves as the DeckLink device does towards the dialog: READY when the
 * callback is set, CAPTURING after StartCapture, READY after StopCapture;
 * a video state before the first frame of every format; frames stamped on
 * the device's own clock and dropped by the dialog when the run token is
 * stale.
 *
 * One reader thread per capture run waits on the plugin. Each delivered
 * frame carries a buffer object whose final Release returns the frame to
 * the plugin. Buffers hold a reference to this device, so the plugin
 * instance is destroyed (and its DLL unloaded) only after the last frame
 * is released, on whichever thread releases it.
 */
class SourcePluginCaptureDevice:
	public ACaptureDevice,
	public ITimingClock
{
public:

	// Creates the plugin instance on the calling (GUI) thread. Throws on
	// failure, in which case the caller still owns module. On success this
	// device owns module and frees it after destroying the instance.
	SourcePluginCaptureDevice(
		HMODULE module,
		const vp_source_plugin_api* api,
		const std::string& pluginDirUtf8);
	virtual ~SourcePluginCaptureDevice();

	// GUI thread, as the plugin header requires. Logs a rejected command.
	void SendCommand(const char* commandUtf8);

	// ACaptureDevice
	void SetCallbackHandler(ICaptureDeviceCallback*) override;
	CString GetName() override;
	bool CanCapture() override { return true; }
	void StartCapture(CaptureRunToken captureRunToken) override;
	void StopCapture() override;
	CaptureInputId CurrentCaptureInputId() override { return m_captureInputId; }
	CaptureInputs SupportedCaptureInputs() override;
	void SetCaptureInput(const CaptureInputId) override;
	ITimingClock* GetTimingClock() override { return this; }
	void SetFrameOffsetMs(int) override;
	double HardwareLatencyMs() const override { return 0.0; }
	uint64_t VideoFrameCapturedCount() const override;
	uint64_t VideoFrameMissedCount() const override;

	// ITimingClock
	timingclocktime_t TimingClockNow() override;
	timingclocktime_t TimingClockTicksPerSecond() const override { return m_ticksPerSecond; }
	const TCHAR* TimingClockDescription() override;

	// IUnknown
	HRESULT	QueryInterface(REFIID iid, LPVOID* ppv) override;
	ULONG AddRef() override;
	ULONG Release() override;

private:

	friend class SourcePluginFrameBuffer;

	HMODULE m_module = nullptr;
	const vp_source_plugin_api* const m_api;
	const std::string m_nameUtf8;
	const std::string m_pluginDirUtf8;
	vp_source_host m_host = {};
	vp_source_instance* m_instance = nullptr;
	timingclocktime_t m_ticksPerSecond = 0;

	// GUI thread only.
	CaptureDeviceState m_state = CaptureDeviceState::CAPTUREDEVICESTATE_READY;
	CaptureInputId m_captureInputId = INVALID_CAPTURE_INPUT_ID;
	ICaptureDeviceCallback* m_callback = nullptr;
	std::thread m_readerThread;

	std::atomic<timingclocktime_t> m_frameOffsetTicks{0};

	// If false no more data is sent out (DeckLink's pair).
	std::atomic_bool m_outputCaptureData = false;
	std::atomic<CaptureRunToken> m_captureRunToken{0};

	// Reader thread only.
	uint32_t m_frameBytes = 0;          // 0 while the state is not valid
	uint32_t m_frameWidth = 0;
	uint32_t m_frameHeight = 0;
	VideoFrameEncoding m_frameEncoding = VideoFrameEncoding::UNKNOWN;
	uint32_t m_frameBitDepth = 0;
	uint64_t m_framesDelivered = 0;     // since the last state
	uint64_t m_rejectedFrames = 0;
	uint64_t m_rejectedCues = 0;
	std::shared_ptr<const SourceCue> m_lastSubtitleCue;
	std::shared_ptr<const SourceCue> m_lastMessageCue;

	void ReaderLoop();
	void SendVideoState();
	void DeliverFrame(const vp_source_frame& frame);
	// Diagnostic frame grab, enabled only by VP_PLUGIN_FRAME_CAPTURE_PATH.
	void CaptureFrame(const vp_source_frame& frame) const;
	// The cue for one frame: the last one again if its id is unchanged,
	// else a checked copy of the plugin's image.
	std::shared_ptr<const SourceCue> TakeCue(const vp_source_cue& in,
		std::shared_ptr<const SourceCue>& last, const char* kind);
	void UpdateState(CaptureDeviceState state);

	static void HostLog(void* context, uint32_t level, const char* messageUtf8);

	// Starts owned by its creator, which Attach()es it (as the DeckLink
	// discoverer does).
	std::atomic<ULONG> m_refCount{1};
};
