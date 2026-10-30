/*
 * Copyright(C) 2026 Andrew Nevard
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <atomic>
#include <cstdlib>
#include <fstream>

#include <DebugLog.h>
#include <CaptureCallbackBoundary.h>

#include "SourcePluginCaptureDevice.h"


namespace
{
	// Short enough that a stuck plugin cannot hold StopCapture for long;
	// stop() wakes the wait anyway.
	constexpr uint32_t WAIT_TIMEOUT_MS = 100;

	// Explicit per-process diagnostic opt-in, for the bit-exact check: save one
	// delivered frame without changing capture, settings or presentation.
	// Frames before this one are skipped so playback has settled.
	constexpr uint64_t CAPTURE_AFTER_FRAMES = 240;

	const std::string& CapturePath()
	{
		static const std::string path = [] {
			char* value = nullptr; size_t size = 0;
			_dupenv_s(&value, &size, "VP_PLUGIN_FRAME_CAPTURE_PATH");
			const std::string result = value ? value : "";
			std::free(value);
			return result;
		}();
		return path;
	}

	// One capture per process, across devices and states.
	std::atomic<bool> g_captureClaimed{ false };

	CString Utf8ToCString(const std::string& utf8)
	{
		if (utf8.empty())
			return CString();

		const int n = MultiByteToWideChar(
			CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
		CString result;
		if (n > 0)
		{
			MultiByteToWideChar(
				CP_UTF8, 0, utf8.data(), (int)utf8.size(), result.GetBuffer(n), n);
			result.ReleaseBuffer(n);
		}
		return result;
	}

	bool TranslateEncoding(uint32_t encoding, VideoFrameEncoding& out)
	{
		switch (encoding)
		{
		case VP_SOURCE_ENCODING_P010: out = VideoFrameEncoding::P010; return true;
		}
		return false;
	}

	bool TranslateEotf(uint32_t eotf, EOTF& out)
	{
		switch (eotf)
		{
		case VP_SOURCE_EOTF_SDR: out = EOTF::SDR; return true;
		case VP_SOURCE_EOTF_PQ:  out = EOTF::PQ;  return true;
		case VP_SOURCE_EOTF_HLG: out = EOTF::HLG; return true;
		}
		return false;
	}

	bool TranslateColorSpace(uint32_t colorspace, ColorSpace& out)
	{
		switch (colorspace)
		{
		case VP_SOURCE_COLORSPACE_REC_601_525: out = ColorSpace::REC_601_525; return true;
		case VP_SOURCE_COLORSPACE_REC_601_625: out = ColorSpace::REC_601_625; return true;
		case VP_SOURCE_COLORSPACE_REC_709:     out = ColorSpace::REC_709;     return true;
		case VP_SOURCE_COLORSPACE_BT_2020:     out = ColorSpace::BT_2020;     return true;
		}
		return false;
	}

	const char* LogLevelName(uint32_t level)
	{
		switch (level)
		{
		case VP_SOURCE_LOG_ERROR:   return "error";
		case VP_SOURCE_LOG_WARNING: return "warning";
		case VP_SOURCE_LOG_INFO:    return "info";
		case VP_SOURCE_LOG_DEBUG:   return "debug";
		}
		return "?";
	}
}


/**
 * One frame lent by the plugin. Created with one reference, which the
 * reader thread drops when the frame callback returns; VP's consumers take
 * their own (VideoFrame::SourceBufferAddRef). The final Release returns
 * the frame to the plugin, from whichever thread that is.
 */
class SourcePluginFrameBuffer:
	public IUnknown
{
public:

	SourcePluginFrameBuffer(SourcePluginCaptureDevice* device, void* handle):
		m_device(device),
		m_handle(handle)
	{
	}

	HRESULT	QueryInterface(REFIID iid, LPVOID* ppv) override
	{
		if (!ppv)
			return E_INVALIDARG;

		*ppv = nullptr;
		if (iid == IID_IUnknown)
		{
			*ppv = this;
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	ULONG AddRef() override
	{
		return ++m_refCount;
	}

	ULONG Release() override
	{
		const ULONG newRefValue = --m_refCount;
		if (newRefValue == 0)
		{
			m_device->m_api->release_frame(m_device->m_instance, m_handle);
			delete this;  // drops the device reference last
		}

		return newRefValue;
	}

private:
	CComPtr<SourcePluginCaptureDevice> m_device;
	void* const m_handle;
	std::atomic<ULONG> m_refCount{1};
};


SourcePluginCaptureDevice::SourcePluginCaptureDevice(
	HMODULE module,
	const vp_source_plugin_api* api,
	const std::string& pluginDirUtf8):
	m_api(api),
	m_nameUtf8(api && api->name ? api->name : ""),
	m_pluginDirUtf8(pluginDirUtf8)
{
	if (!module || !api)
		throw std::runtime_error("Source plugin: no module or function table");
	if (m_nameUtf8.empty())
		throw std::runtime_error("Source plugin: no name");

	m_host.abi_version = VP_SOURCE_PLUGIN_ABI_VERSION;
	m_host.struct_size = sizeof(m_host);
	m_host.context = this;
	m_host.plugin_dir = m_pluginDirUtf8.c_str();
	m_host.log = &SourcePluginCaptureDevice::HostLog;

	if (m_api->create(&m_host, &m_instance) != VP_SOURCE_OK || !m_instance)
		throw std::runtime_error("Source plugin: create failed");

	m_ticksPerSecond = m_api->clock_ticks_per_second(m_instance);
	if (m_ticksPerSecond <= 0)
	{
		m_api->destroy(m_instance);
		m_instance = nullptr;
		throw std::runtime_error("Source plugin: invalid clock rate");
	}

	// Owned from here on.
	m_module = module;

	DEBUGLOG("Source plugin loaded: name=%s dir=%s clock_hz=%lld",
		m_nameUtf8.c_str(), m_pluginDirUtf8.c_str(),
		static_cast<long long>(m_ticksPerSecond));
}


SourcePluginCaptureDevice::~SourcePluginCaptureDevice()
{
	// The dialog stops capture first; this only guards a missed stop.
	if (m_readerThread.joinable())
	{
		m_outputCaptureData.store(false, std::memory_order_release);
		m_captureRunToken.store(0, std::memory_order_release);
		m_api->stop(m_instance);
		if (m_readerThread.get_id() == std::this_thread::get_id())
			m_readerThread.detach();
		else
			m_readerThread.join();
	}

	// Every frame buffer holds a reference to this device, so every frame
	// has been released by now.
	m_api->destroy(m_instance);
	m_instance = nullptr;

	DEBUGLOG("Source plugin unloaded: name=%s", m_nameUtf8.c_str());

	FreeLibrary(m_module);
}


//
// ACaptureDevice
//


void SourcePluginCaptureDevice::SetCallbackHandler(ICaptureDeviceCallback* callback)
{
	m_callback = callback;

	// The dialog waits for this before choosing an input. No video state
	// here: the dialog drops run token 0 as stale.
	if (m_callback)
		m_callback->OnCaptureDeviceState(m_state);
}


CString SourcePluginCaptureDevice::GetName()
{
	return Utf8ToCString(m_nameUtf8);
}


void SourcePluginCaptureDevice::StartCapture(CaptureRunToken captureRunToken)
{
	if (m_outputCaptureData.load(std::memory_order_acquire) || m_readerThread.joinable())
		throw std::runtime_error("StartCapture() called but already started");
	if (captureRunToken == 0)
		throw std::runtime_error("StartCapture() called with invalid capture run token");
	if (m_captureInputId == INVALID_CAPTURE_INPUT_ID)
		throw std::runtime_error("Set input with SetCaptureInput() before calling StartCapture()");

	m_frameBytes = 0;
	m_rejectedFrames = 0;

	// From here on out data can egress
	m_captureRunToken.store(captureRunToken, std::memory_order_release);
	m_outputCaptureData.store(true, std::memory_order_release);

	const int32_t started = m_api->start(m_instance);
	if (started != VP_SOURCE_OK)
	{
		// Not fatal: VP stays up with the input idle; the plugin logged why
		// (e.g. mpv not found). The Dlg set Starting, so report Ready, its
		// one stopped state that still lets the input be left or VP closed
		// (Failed is never removed). The device itself never left Ready.
		m_outputCaptureData.store(false, std::memory_order_release);
		m_captureRunToken.store(0, std::memory_order_release);
		DEBUGLOG("Source plugin capture could not start: name=%s result=%d; input idle, VP continues",
			m_nameUtf8.c_str(), started);
		if (m_callback)
			m_callback->OnCaptureDeviceState(CaptureDeviceState::CAPTUREDEVICESTATE_READY);
		return;
	}

	m_readerThread = std::thread(&SourcePluginCaptureDevice::ReaderLoop, this);

	DEBUGLOG("Source plugin capture started: name=%s run=%llu",
		m_nameUtf8.c_str(), static_cast<unsigned long long>(captureRunToken));

	UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);
}


void SourcePluginCaptureDevice::StopCapture()
{
	if (!m_outputCaptureData.load(std::memory_order_acquire))
		throw std::runtime_error("StopCapture() called while not started");

	// Stop egressing data
	m_outputCaptureData.store(false, std::memory_order_release);
	m_captureRunToken.store(0, std::memory_order_release);

	const int32_t result = m_api->stop(m_instance);
	if (m_readerThread.joinable())
		m_readerThread.join();

	DEBUGLOG("Source plugin capture stopped: name=%s stop_result=%d rejected_frames=%llu",
		m_nameUtf8.c_str(), result,
		static_cast<unsigned long long>(m_rejectedFrames));

	UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_READY);
}


CaptureInputs SourcePluginCaptureDevice::SupportedCaptureInputs()
{
	// One input. The dialog lists inputs by name and never reads the type.
	CaptureInputs captureInputs;
	captureInputs.emplace_back(0, CaptureInputType::HDMI, CString(SOURCE_PLUGIN_INPUT_NAME));
	return captureInputs;
}


void SourcePluginCaptureDevice::SetCaptureInput(const CaptureInputId captureInputId)
{
	if (captureInputId != 0)
		throw std::runtime_error("Source plugin: unknown capture input");

	m_captureInputId = captureInputId;
}


void SourcePluginCaptureDevice::SetFrameOffsetMs(int frameOffsetMs)
{
	m_frameOffsetTicks.store(
		static_cast<timingclocktime_t>(frameOffsetMs) * m_ticksPerSecond / 1000,
		std::memory_order_release);
}


uint64_t SourcePluginCaptureDevice::VideoFrameCapturedCount() const
{
	return m_api->frames_captured(m_instance);
}


uint64_t SourcePluginCaptureDevice::VideoFrameMissedCount() const
{
	return m_api->frames_missed(m_instance);
}


//
// ITimingClock
//


timingclocktime_t SourcePluginCaptureDevice::TimingClockNow()
{
	return m_api->clock_now(m_instance);
}


const TCHAR* SourcePluginCaptureDevice::TimingClockDescription()
{
	return TEXT("Source plugin clock");
}


//
// IUnknown
//


HRESULT	SourcePluginCaptureDevice::QueryInterface(REFIID iid, LPVOID* ppv)
{
	if (!ppv)
		return E_INVALIDARG;

	*ppv = nullptr;
	if (iid == IID_IUnknown)
	{
		*ppv = this;
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}


ULONG SourcePluginCaptureDevice::AddRef(void)
{
	return ++m_refCount;
}


ULONG SourcePluginCaptureDevice::Release(void)
{
	const ULONG newRefValue = --m_refCount;
	if (newRefValue == 0)
		delete this;

	return newRefValue;
}


//
// Reader thread
//


void SourcePluginCaptureDevice::ReaderLoop()
{
	while (true)
	{
		vp_source_frame frame = {};
		const int32_t result = m_api->wait_frame(m_instance, WAIT_TIMEOUT_MS, &frame);

		if (result == VP_SOURCE_WAIT_FRAME &&
			!m_outputCaptureData.load(std::memory_order_acquire))
		{
			m_api->release_frame(m_instance, frame.handle);
			return;
		}

		if (result == VP_SOURCE_WAIT_STOPPED ||
			!m_outputCaptureData.load(std::memory_order_acquire))
			return;

		if (result < 0)
		{
			DEBUGLOG("Source plugin wait_frame failed: name=%s result=%d",
				m_nameUtf8.c_str(), result);
			if (m_callback)
				m_callback->OnCaptureDeviceError(TEXT("Source plugin stopped delivering frames"));
			return;
		}

		CaptureCallbackBoundary([&]() -> HRESULT
		{
			if (result == VP_SOURCE_WAIT_STATE_CHANGED)
				SendVideoState();
			else if (result == VP_SOURCE_WAIT_FRAME)
				DeliverFrame(frame);

			return S_OK;
		}, [this](const char* error)
		{
			DebugLog::Log("Source plugin %s: reader exception contained: %s",
				m_nameUtf8.c_str(), error);
		});
	}
}


void SourcePluginCaptureDevice::SendVideoState()
{
	vp_source_state state = {};
	state.struct_size = sizeof(state);
	const int32_t result = m_api->get_state(m_instance, &state);

	DEBUGLOG("Source plugin state: name=%s result=%d valid=%u size=%ux%u rate=%u/%u "
		"encoding=%u bit_depth=%u eotf=%u colorspace=%u mastering=%u light_level=%u "
		"raw_set=%u primaries=%d transfer=%d matrix=%d range=%d chroma_location=%d sar=%u:%u",
		m_nameUtf8.c_str(), result, state.valid, state.width, state.height,
		state.time_scale, state.frame_duration,
		state.encoding, state.bit_depth, state.eotf, state.colorspace,
		state.hdr10.has_mastering, state.hdr10.has_light_level,
		state.raw.set, state.raw.primaries, state.raw.transfer, state.raw.matrix,
		state.raw.range, state.raw.chroma_location, state.raw.sar_num, state.raw.sar_den);

	VideoStateComPtr videoState = new VideoState();
	videoState->valid = false;
	m_frameBytes = 0;
	m_frameWidth = m_frameHeight = 0;
	m_frameEncoding = VideoFrameEncoding::UNKNOWN;
	m_frameBitDepth = 0;
	m_framesDelivered = 0;

	VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
	EOTF eotf = EOTF::UNKNOWN;
	ColorSpace colorspace = ColorSpace::UNKNOWN;

	// The source's own depth (0: the encoding's); P010 may carry 8-bit values.
	const uint32_t bitDepth = state.bit_depth != 0 ? state.bit_depth :
		(state.encoding == VP_SOURCE_ENCODING_P010 ? 10u : 8u);

	const char* refusal = nullptr;
	if (result != VP_SOURCE_OK)
		refusal = "get_state failed";
	else if (!state.valid)
		refusal = "no signal";
	else if (!TranslateEncoding(state.encoding, encoding))
		refusal = "unknown encoding";
	else if (bitDepth != 8 && !(bitDepth == 10 && encoding == VideoFrameEncoding::P010))
		refusal = "bit depth does not fit the encoding";
	else if (!TranslateEotf(state.eotf, eotf))
		refusal = "unknown eotf";
	else if (!TranslateColorSpace(state.colorspace, colorspace))
		refusal = "unknown colorspace";
	else if (state.width == 0 || state.height == 0 ||
		(state.width % 2) != 0 || (state.height % 2) != 0)
		refusal = "4:2:0 needs an even, non-zero frame size";
	else if (state.time_scale == 0 || state.frame_duration == 0)
		refusal = "no frame rate";

	if (!refusal)
	{
		videoState->valid = true;
		videoState->displayMode = std::make_shared<DisplayMode>(
			state.width, state.height, false, state.time_scale, state.frame_duration);
		videoState->videoFrameEncoding = encoding;
		videoState->eotf = eotf;
		videoState->colorspace = colorspace;

		if (state.hdr10.has_mastering)
		{
			HDRData hdrData;
			hdrData.displayPrimaryRedX = state.hdr10.red_x;
			hdrData.displayPrimaryRedY = state.hdr10.red_y;
			hdrData.displayPrimaryGreenX = state.hdr10.green_x;
			hdrData.displayPrimaryGreenY = state.hdr10.green_y;
			hdrData.displayPrimaryBlueX = state.hdr10.blue_x;
			hdrData.displayPrimaryBlueY = state.hdr10.blue_y;
			hdrData.whitePointX = state.hdr10.white_x;
			hdrData.whitePointY = state.hdr10.white_y;
			hdrData.masteringDisplayMaxLuminance = state.hdr10.max_mastering_luminance;
			hdrData.masteringDisplayMinLuminance = state.hdr10.min_mastering_luminance;
			if (state.hdr10.has_light_level)
			{
				hdrData.maxCll = state.hdr10.max_cll;
				hdrData.maxFall = state.hdr10.max_fall;
			}

			// As DeckLink: only valid HDR data reaches VP.
			if (hdrData.IsValid())
				videoState->hdrData = std::make_shared<HDRData>(hdrData);
			else
				DEBUGLOG("Source plugin %s: HDR10 metadata incomplete, not passed on",
					m_nameUtf8.c_str());
		}

		m_frameBytes = videoState->BytesPerFrame();
		m_frameWidth = state.width;
		m_frameHeight = state.height;
		m_frameEncoding = encoding;
		m_frameBitDepth = bitDepth;
	}
	else
	{
		DEBUGLOG("Source plugin %s: state not valid: %s", m_nameUtf8.c_str(), refusal);
	}

	if (m_callback)
	{
		CaptureDeviceCardStateComPtr cardState = new CaptureDeviceCardState();
		cardState->inputLocked = videoState->valid ? InputLocked::YES : InputLocked::NO;
		cardState->inputDisplayMode = videoState->displayMode;
		if (videoState->valid)
		{
			// P010 is 4:2:0. No hardware link to report: the GUI reads
			// "other" as its PCIe rows, by position.
			cardState->inputEncoding = ColorFormat::YCbCr420;
			cardState->inputBitDepth = bitDepth == 10 ?
				BitDepth::BITDEPTH_10BIT : BitDepth::BITDEPTH_8BIT;
		}
		m_callback->OnCaptureDeviceCardStateChange(cardState);

		m_callback->OnCaptureDeviceVideoStateChange(
			this, m_captureRunToken.load(std::memory_order_acquire), videoState);
	}
}


void SourcePluginCaptureDevice::DeliverFrame(const vp_source_frame& frame)
{
	// Take ownership first so every path below returns the frame.
	CComPtr<SourcePluginFrameBuffer> buffer;
	buffer.Attach(new SourcePluginFrameBuffer(this, frame.handle));

	if (m_frameBytes == 0 || !frame.data || frame.size != m_frameBytes)
	{
		// Log the first and then powers of two, as the dialog does for
		// rejected ingress.
		++m_rejectedFrames;
		if ((m_rejectedFrames & (m_rejectedFrames - 1)) == 0)
			DEBUGLOG("Source plugin %s: frame rejected: count=%llu size=%llu expected=%u counter=%llu",
				m_nameUtf8.c_str(),
				static_cast<unsigned long long>(m_rejectedFrames),
				static_cast<unsigned long long>(frame.size), m_frameBytes,
				static_cast<unsigned long long>(frame.counter));
		return;
	}

	const CaptureRunToken captureRunToken =
		m_captureRunToken.load(std::memory_order_acquire);
	if (captureRunToken == 0 || !m_callback)
		return;

	VideoFrame vpVideoFrame(
		frame.data, frame.counter,
		frame.timestamp + m_frameOffsetTicks.load(std::memory_order_acquire),
		buffer.p, frame.timestamp);
	vpVideoFrame.SetSourceDiscontinuity(
		(frame.flags & VP_SOURCE_FRAME_DISCONTINUITY) != 0);

	++m_framesDelivered;
	if (m_frameEncoding == VideoFrameEncoding::P010 &&
		m_framesDelivered > CAPTURE_AFTER_FRAMES && !CapturePath().empty() &&
		!g_captureClaimed.exchange(true))
		CaptureFrame(frame);

	m_callback->OnCaptureDeviceVideoFrame(this, captureRunToken, vpVideoFrame);
}


// Writes the plugin's frame exactly as VP receives it (his native P010
// path uploads these words unchanged), plus a sidecar. Diagnostic only:
// the write blocks the reader for this one frame.
void SourcePluginCaptureDevice::CaptureFrame(const vp_source_frame& frame) const
{
	const std::string& path = CapturePath();

	std::ofstream pixels(path, std::ios::binary);
	pixels.write(static_cast<const char*>(frame.data), m_frameBytes);
	pixels.flush();

	// color_depth 8: an 8-bit source shifted into the high byte, low byte zero.
	std::ofstream metadata(path + ".txt");
	metadata
		<< "encoding=P010\n"
		<< "width=" << m_frameWidth << '\n'
		<< "height=" << m_frameHeight << '\n'
		<< "layout=Y plane then interleaved CbCr at half height, 16-bit little-endian words, no row padding\n"
		<< "bytes=" << m_frameBytes << '\n'
		<< "color_depth=" << m_frameBitDepth << '\n'
		<< "bit_shift=" << (16 - m_frameBitDepth) << '\n'
		<< "sample_range=limited\n"
		<< "source_counter=" << frame.counter << '\n'
		<< "frames_delivered=" << m_framesDelivered << '\n'
		<< "written=" << (pixels.good() ? "ok" : "failed") << '\n';

	DEBUGLOG("Source plugin %s: frame grab written to %s (counter=%llu)",
		m_nameUtf8.c_str(), path.c_str(), static_cast<unsigned long long>(frame.counter));
}


void SourcePluginCaptureDevice::UpdateState(CaptureDeviceState state)
{
	// GUI thread only: StartCapture and StopCapture.
	assert(state != m_state);
	m_state = state;

	if (m_callback)
		m_callback->OnCaptureDeviceState(state);
}


void SourcePluginCaptureDevice::HostLog(void* context, uint32_t level, const char* messageUtf8)
{
	const SourcePluginCaptureDevice* device =
		static_cast<const SourcePluginCaptureDevice*>(context);

	DebugLog::Log("Source plugin %s [%s]: %s",
		device ? device->m_nameUtf8.c_str() : "?",
		LogLevelName(level),
		messageUtf8 ? messageUtf8 : "");
}
