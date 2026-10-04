/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <LibMWCapture/MWCapture.h>

#include <ACaptureDevice.h>
#include <ITimingClock.h>
#include <VideoFrameEncoding.h>
#include <magewell/MagewellSdkInstance.h>


/**
 * A single pinned capture buffer, reference counted so VP can hold onto it.
 *
 * VideoFrame requires a non-null IUnknown source buffer: the renderer calls
 * AddRef() when it queues a frame and Release() when it is finished with it,
 * and it asserts that its own Release() takes the count back to zero. The
 * count therefore starts at zero and a buffer is only recycled once it
 * returns there, which is also what keeps the renderer from reading a buffer
 * the capture thread has already overwritten.
 *
 * The pool holds shared ownership while capture is active. AddRef() gives a
 * referenced frame its own shared owner, released only when Release() returns
 * to zero. Frames can therefore outlive a format change or capture shutdown.
 */
class MagewellCaptureBuffer:
	public IUnknown,
	public std::enable_shared_from_this<MagewellCaptureBuffer>
{
public:

	explicit MagewellCaptureBuffer(size_t size):
		m_data(size), m_refCount(0) {}

	BYTE* Data() { return m_data.data(); }
	uint32_t Size() const { return (uint32_t)m_data.size(); }
	bool InUse() const { return m_refCount.load(std::memory_order_acquire) > 0; }

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

private:
	friend class MagewellCaptureTestAccess;

	std::vector<BYTE> m_data;
	std::atomic<ULONG> m_refCount;
	std::mutex m_referenceMutex;
	std::shared_ptr<MagewellCaptureBuffer> m_inFlightOwner;
};


/**
 * Magewell Pro Capture family capture device.
 *
 * Mirrors the role BlackMagicDeckLinkCaptureDevice plays for DeckLink
 * hardware. The device is identified by the SDK device path rather than by
 * channel index, because indices are not stable across a device refresh.
 */
class MagewellCaptureDevice:
	public ACaptureDevice,
	public ITimingClock
{
public:

	MagewellCaptureDevice(
		const MagewellSdkInstancePtr& sdkInstance,
		const std::wstring& devicePath,
		const MWCAP_CHANNEL_INFO& channelInfo,
		const CString& displayName);
	virtual ~MagewellCaptureDevice();

	// ACaptureDevice
	void SetCallbackHandler(ICaptureDeviceCallback*) override;
	CString GetName() override;
	bool CanCapture() override;
	void StartCapture(CaptureRunToken captureRunToken) override;
	void StopCapture() override;
	CaptureInputId CurrentCaptureInputId() override;
	CaptureInputs SupportedCaptureInputs() override;
	void SetCaptureInput(const CaptureInputId) override;
	ITimingClock* GetTimingClock() override;
	void SetFrameOffsetMs(int) override;
	double HardwareLatencyMs() const override { return m_hardwareLatencyMs; }
	uint64_t VideoFrameCapturedCount() const override { return m_capturedVideoFrameCount; }
	uint64_t VideoFrameMissedCount() const override { return m_missedVideoFrameCount; }

	// ITimingClock
	timingclocktime_t TimingClockNow() override;
	timingclocktime_t TimingClockTicksPerSecond() const override;
	const TCHAR* TimingClockDescription() override;

	// IUnknown
	HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
	ULONG AddRef() override;
	ULONG Release() override;

private:
	friend class MagewellCaptureTestAccess;

	// Everything the capture thread needs to describe the current signal.
	struct SignalDescription
	{
		bool locked = false;
		unsigned int width = 0;
		unsigned int height = 0;
		bool interlaced = false;
		unsigned int frameDuration100ns = 0;
		MWCAP_VIDEO_COLOR_FORMAT colorFormat = MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN;
		MWCAP_VIDEO_QUANTIZATION_RANGE quantRange = MWCAP_VIDEO_QUANTIZATION_UNKNOWN;
		BYTE bitDepth = 0;
		EOTF eotf = EOTF::UNKNOWN;
		ColorSpace colorSpace = ColorSpace::UNKNOWN;
		bool hasHdrData = false;
		HDRData hdrData;

		bool SameFormatAs(const SignalDescription& other) const;
	};

	HCHANNEL OpenChannel() const;
	// Wrapper which guarantees no exception escapes the thread. An escaping
	// exception on a std::thread calls std::terminate and kills the process
	// with no diagnostic at all.
	void CaptureThread(CaptureRunToken captureRunToken);
	void CaptureThreadBody(CaptureRunToken captureRunToken);

	// Reads signal status, HDMI status and InfoFrames into a description.
	bool ReadSignal(HCHANNEL channel, SignalDescription& signal) const;

	// Parses the HDMI HDR InfoFrame into VP's HDR metadata.
	bool ReadHdrInfoFrame(HCHANNEL channel, SignalDescription& signal) const;

	// Chooses only formats the card reports as supported. RGB8 uses ARGB,
	// RGB10 is repacked into R210, and YCbCr uses P210 repacked into V210.
	static void ChooseCaptureFormat(
		const SignalDescription& signal,
		const std::vector<DWORD>& supportedFormats,
		DWORD& fourcc, VideoFrameEncoding& encoding);
	static void RepackRGB10ToR210(
		const uint8_t* source, uint32_t sourceStride,
		uint8_t* destination, uint32_t destinationStride,
		uint32_t width, uint32_t height);

	static uint32_t BytesPerRowFor(
		VideoFrameEncoding encoding, unsigned int width);

	// The card cannot output v210. It can output P210, which carries the
	// same 10-bit 4:2:2 samples, so frames are captured as P210 and repacked
	// into v210 here. P210 stores MSB-aligned 10-bit codes in 16-bit samples;
	// V210 packs those same codes into 32-bit words. The UV order matches, so
	// this is a pure bit repack with no colour conversion and no loss.
	static void RepackP210ToV210(
		const uint8_t* source, uint32_t sourceStride,
		uint8_t* destination, uint32_t destinationStride,
		uint32_t width, uint32_t height);

	// Reports the wire-side signal to VP: lock state, detected mode, colour
	// format and bit depth, plus PCIe link details. This is what fills the
	// Input and Hardware link panels, and is separate from the captured
	// format we publish as video state.
	void PublishCardState(const SignalDescription& signal);

	bool PublishVideoState(
		const SignalDescription& signal, VideoFrameEncoding encoding,
		CaptureRunToken captureRunToken);
	void UpdateState(CaptureDeviceState state);
	void Error(const CString& error);

	MagewellSdkInstancePtr m_sdkInstance;
	std::wstring m_devicePath;
	MWCAP_CHANNEL_INFO m_channelInfo;
	CString m_displayName;

	bool m_canCapture = false;
	std::vector<CaptureInput> m_captureInputSet;
	CaptureInputId m_captureInputId = INVALID_CAPTURE_INPUT_ID;

	// Held only while capturing. Read by the timing clock, which VP only
	// calls during a capture run.
	std::atomic<HCHANNEL> m_channel{ nullptr };

	std::thread m_captureThread;
	std::atomic_bool m_captureThreadRunning{ false };
	std::atomic_bool m_outputCaptureData{ false };

	std::atomic<timingclocktime_t> m_frameOffsetTicks{ 0 };
	// The card's device clock is absolute time since boot. Record its value
	// before capture starts and subtract it from the clock and every frame,
	// giving VP one stable stream-relative origin. -1 means unset.
	std::atomic<timingclocktime_t> m_timingBaselineTicks{ -1 };
	std::atomic<double> m_hardwareLatencyMs{ 0 };
	std::atomic<uint64_t> m_capturedVideoFrameCount{ 0 };
	std::atomic<uint64_t> m_missedVideoFrameCount{ 0 };

	std::atomic<CaptureDeviceState> m_state{
		CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN };
	ICaptureDeviceCallback* m_callback = nullptr;

	std::atomic<ULONG> m_refCount;
};
