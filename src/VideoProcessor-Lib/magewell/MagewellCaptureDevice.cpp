/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "MagewellCaptureDevice.h"
#include "MagewellInput.h"

#include <MWFOURCC.h>
#include <LibMWCapture/MWHDMIPackets.h>

#include <DebugLog.h>
#include <VideoState.h>

#include <stdexcept>
#include <algorithm>
#include <chrono>


// The Magewell device clock is expressed in 100ns units, which is 10,000,000
// ticks per second. Confirmed by the SDK's own frame duration maths, where
// frames per second is 10000000 / dwFrameDuration.
static const timingclocktime_t MAGEWELL_CLOCK_TICKS_SECOND = 10000000LL;

// Number of pinned capture buffers. The renderer may still be reading the
// previous frame when the next arrives, so buffers are cycled rather than
// reused immediately.
static const int MAGEWELL_CAPTURE_BUFFER_COUNT = 8;

template <class F> class MagewellScopeExit
{
public:
	explicit MagewellScopeExit(F callback): m_callback(std::move(callback)) {}
	~MagewellScopeExit() noexcept
	{
		try { m_callback(); }
		catch (...) { DebugLog::Log("Magewell cleanup callback failed"); }
	}
	MagewellScopeExit(const MagewellScopeExit&) = delete;
	MagewellScopeExit& operator=(const MagewellScopeExit&) = delete;
private:
	F m_callback;
};


//
// MagewellCaptureBuffer
//


HRESULT STDMETHODCALLTYPE MagewellCaptureBuffer::QueryInterface(
	REFIID riid, void** ppvObject)
{
	if (!ppvObject)
		return E_INVALIDARG;

	*ppvObject = nullptr;

	if (riid == IID_IUnknown)
	{
		*ppvObject = static_cast<IUnknown*>(this);
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}


ULONG STDMETHODCALLTYPE MagewellCaptureBuffer::AddRef()
{
	std::lock_guard<std::mutex> lock(m_referenceMutex);
	if (m_refCount == 0)
		m_inFlightOwner = shared_from_this();
	return ++m_refCount;
}


ULONG STDMETHODCALLTYPE MagewellCaptureBuffer::Release()
{
	std::shared_ptr<MagewellCaptureBuffer> releasedOwner;
	ULONG result;
	{
		std::lock_guard<std::mutex> lock(m_referenceMutex);
		result = --m_refCount;
		if (result == 0)
			releasedOwner = std::move(m_inFlightOwner);
	}
	// Keep the object alive through the end of Release even if shutdown has
	// already discarded the capture pool.
	return result;
}


//
// SignalDescription
//


bool MagewellCaptureDevice::SignalDescription::SameFormatAs(
	const SignalDescription& other) const
{
	return locked == other.locked &&
		width == other.width &&
		height == other.height &&
		interlaced == other.interlaced &&
		frameDuration100ns == other.frameDuration100ns &&
		colorFormat == other.colorFormat &&
		quantRange == other.quantRange &&
		bitDepth == other.bitDepth &&
		inputSampling == other.inputSampling &&
		eotf == other.eotf &&
		colorSpace == other.colorSpace &&
		hasHdrData == other.hasHdrData &&
		hdrData == other.hdrData;
}


//
// Constructor & destructor
//


MagewellCaptureDevice::MagewellCaptureDevice(
	const MagewellSdkInstancePtr& sdkInstance,
	const std::wstring& devicePath,
	const MWCAP_CHANNEL_INFO& channelInfo,
	const CString& displayName):
	m_sdkInstance(sdkInstance),
	m_devicePath(devicePath),
	m_channelInfo(channelInfo),
	m_displayName(displayName),
	m_refCount(0)
{
	if (!m_sdkInstance)
		throw std::runtime_error("No Magewell SDK instance given in constructor");
	if (m_devicePath.empty())
		throw std::runtime_error("No Magewell device path given in constructor");

	// The channel is opened once here purely to establish that the device is
	// usable and to read its connector set. It is closed again immediately;
	// a handle is only held for the duration of a capture run.
	const HCHANNEL channel = OpenChannel();
	if (!channel)
	{
		m_canCapture = false;
		m_state = CaptureDeviceState::CAPTUREDEVICESTATE_FAILED;
		return;
	}
	auto closeChannelFn = [&]()
	{
		m_sdkInstance->Api().MWCloseChannel(channel);
	};
	MagewellScopeExit<decltype(closeChannelFn)> closeChannel(closeChannelFn);

	DWORD sourceCount = 0;
	if (m_sdkInstance->Api().MWGetVideoInputSourceArray(channel, nullptr, &sourceCount) == MW_SUCCEEDED &&
		sourceCount > 0 && sourceCount <= 256)
	{
		std::vector<DWORD> sources(sourceCount);
		if (m_sdkInstance->Api().MWGetVideoInputSourceArray(channel, sources.data(), &sourceCount)
			!= MW_SUCCEEDED || sourceCount > sources.size())
			sourceCount = 0;
		sources.resize(sourceCount);
		for (DWORD index = 0; index < sourceCount; ++index)
		{
			CaptureInputType inputType;
			CString name;
			if (!MagewellInputDescription(sources[index], sources,
				inputType, name))
				continue;
			m_captureInputSet.emplace_back(
				static_cast<CaptureInputId>(sources[index]), inputType, std::move(name));
		}
	}

	if (!m_captureInputSet.empty())
	{
		DWORD selectedSource = 0;
		if (m_sdkInstance->Api().MWGetVideoInputSource(channel, &selectedSource) == MW_SUCCEEDED)
		{
			for (const auto& input : m_captureInputSet)
				if (input.id == selectedSource)
					m_captureInputId = input.id;
		}
		if (m_captureInputId == INVALID_CAPTURE_INPUT_ID)
			m_captureInputId = m_captureInputSet.front().id;
	}
	m_canCapture = !m_captureInputSet.empty();
	m_state = m_canCapture
		? CaptureDeviceState::CAPTUREDEVICESTATE_READY
		: CaptureDeviceState::CAPTUREDEVICESTATE_FAILED;
}


MagewellCaptureDevice::~MagewellCaptureDevice()
{
	m_callback = nullptr;
	try { StopCapture(); }
	catch (...) { DebugLog::Log("Magewell StopCapture failed in destructor"); }
}


HCHANNEL MagewellCaptureDevice::OpenChannel() const
{
	// MWOpenChannelByPath takes a mutable buffer, so a local copy is passed
	// rather than the stored string's buffer.
	WCHAR path[128] = {};
	wcsncpy_s(path, m_devicePath.c_str(), _TRUNCATE);
	return m_sdkInstance->Api().MWOpenChannelByPath(path);
}


//
// ACaptureDevice
//


void MagewellCaptureDevice::SetCallbackHandler(ICaptureDeviceCallback* callback)
{
	m_callback = callback;

	if (m_callback)
		m_callback->OnCaptureDeviceState(m_state);
}


CString MagewellCaptureDevice::GetName()
{
	return m_displayName;
}


bool MagewellCaptureDevice::CanCapture()
{
	return m_canCapture;
}


void MagewellCaptureDevice::StartCapture(CaptureRunToken captureRunToken)
{
	if (m_captureThreadRunning.load(std::memory_order_acquire) ||
		m_state.load() == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		throw std::runtime_error("StartCapture() called but already started");
	if (captureRunToken == 0)
		throw std::runtime_error("StartCapture() called with invalid capture run token");
	if (!m_canCapture)
		throw std::runtime_error("StartCapture() called on a device which cannot capture");

	// The GUI records STARTING before this call. Expected setup failures must
	// report an error and return to READY, so it cannot remain stuck there.
	m_state.store(CaptureDeviceState::CAPTUREDEVICESTATE_STARTING);
	HCHANNEL channel = nullptr;
	try
	{
		if (m_captureThread.joinable())
			m_captureThread.join();
		channel = OpenChannel();
		if (!channel)
			throw std::runtime_error("Failed to open the Magewell capture channel");
		if (m_sdkInstance->Api().MWGetInputSourceScan(
			channel, &m_originalScanEnabled) != MW_SUCCEEDED)
			throw std::runtime_error("Failed to read Magewell input AutoScan state");
		m_restoreScan = true;
		// AutoScan may choose any connected input, overriding this device's
		// configured connector. Disable it before selecting the full source ID.
		if (m_sdkInstance->Api().MWSetInputSourceScan(channel, FALSE) != MW_SUCCEEDED)
			throw std::runtime_error("Failed to disable Magewell input AutoScan");
		if (m_sdkInstance->Api().MWSetVideoInputSource(
			channel, static_cast<DWORD>(m_captureInputId)) != MW_SUCCEEDED)
			throw std::runtime_error("Failed to select the configured Magewell video input");
		DWORD selectedSource = 0;
		BOOLEAN scanEnabled = TRUE;
		if (m_sdkInstance->Api().MWGetInputSourceScan(channel, &scanEnabled) != MW_SUCCEEDED ||
			scanEnabled != FALSE ||
			m_sdkInstance->Api().MWGetVideoInputSource(channel, &selectedSource) != MW_SUCCEEDED ||
			selectedSource != static_cast<DWORD>(m_captureInputId))
			throw std::runtime_error("Magewell did not retain the configured video input");
		LONGLONG startDeviceTime = 0;
		if (m_sdkInstance->Api().MWGetDeviceTime(channel, &startDeviceTime)
			!= MW_SUCCEEDED)
			throw std::runtime_error("Failed to establish Magewell capture clock baseline");
		{
			std::lock_guard<std::mutex> clockLock(m_clockMutex);
			m_lastClockTicks = 0;
			m_timingBaselineTicks.store(
				static_cast<timingclocktime_t>(startDeviceTime),
				std::memory_order_release);
			m_channel.store(channel, std::memory_order_release);
		}
		m_capturedVideoFrameCount = 0;
		m_missedVideoFrameCount = 0;
		m_captureThreadRunning.store(true, std::memory_order_release);
		m_outputCaptureData.store(true, std::memory_order_release);
		m_captureThread = std::thread(
			&MagewellCaptureDevice::CaptureThread, this, captureRunToken);
	}
	catch (const std::exception& exception)
	{
		m_captureThreadRunning.store(false, std::memory_order_release);
		m_outputCaptureData.store(false, std::memory_order_release);
		{
			std::lock_guard<std::mutex> clockLock(m_clockMutex);
			m_channel.store(nullptr, std::memory_order_release);
			if (channel)
			{
				RestoreInputScan(channel);
				m_sdkInstance->Api().MWCloseChannel(channel);
			}
		}
		try { Error(CString(exception.what())); }
		catch (...) { DebugLog::Log("Magewell setup error callback threw"); }
		try { UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_READY); }
		catch (...) { DebugLog::Log("Magewell READY callback threw"); }
		return;
	}
	catch (...)
	{
		m_captureThreadRunning.store(false, std::memory_order_release);
		m_outputCaptureData.store(false, std::memory_order_release);
		{
			std::lock_guard<std::mutex> clockLock(m_clockMutex);
			m_channel.store(nullptr, std::memory_order_release);
			if (channel)
			{
				RestoreInputScan(channel);
				m_sdkInstance->Api().MWCloseChannel(channel);
			}
		}
		try { Error(TEXT("Unknown Magewell capture setup failure")); }
		catch (...) { DebugLog::Log("Magewell setup error callback threw"); }
		try { UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_READY); }
		catch (...) { DebugLog::Log("Magewell READY callback threw"); }
		return;
	}

	DebugLog::Log("MagewellCaptureDevice::StartCapture() started");
}


void MagewellCaptureDevice::StopCapture()
{
	m_outputCaptureData.store(false, std::memory_order_release);
	m_captureThreadRunning.store(false, std::memory_order_release);

	if (m_captureThread.joinable())
		m_captureThread.join();

	{
		std::lock_guard<std::mutex> clockLock(m_clockMutex);
		const HCHANNEL channel = m_channel.exchange(nullptr, std::memory_order_acq_rel);
		if (channel)
		{
			RestoreInputScan(channel);
			m_sdkInstance->Api().MWCloseChannel(channel);
		}
	}

	UpdateState(m_canCapture
		? CaptureDeviceState::CAPTUREDEVICESTATE_READY
		: CaptureDeviceState::CAPTUREDEVICESTATE_FAILED);
}


CaptureInputId MagewellCaptureDevice::CurrentCaptureInputId()
{
	return m_captureInputId;
}


CaptureInputs MagewellCaptureDevice::SupportedCaptureInputs()
{
	return m_captureInputSet;
}


void MagewellCaptureDevice::SetCaptureInput(const CaptureInputId captureInputId)
{
	if (m_captureThreadRunning.load(std::memory_order_acquire) ||
		m_state.load() == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		throw std::runtime_error("Cannot change the Magewell input during capture");
	const auto found = std::find_if(m_captureInputSet.begin(),
		m_captureInputSet.end(), [captureInputId](const CaptureInput& input)
		{ return input.id == captureInputId; });
	if (found == m_captureInputSet.end())
		throw std::runtime_error("Unsupported Magewell video input source");
	m_captureInputId = captureInputId;
}


ITimingClock* MagewellCaptureDevice::GetTimingClock()
{
	if (m_state.load() != CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		return nullptr;

	return this;
}


void MagewellCaptureDevice::SetFrameOffsetMs(int frameOffsetMs)
{
	static_assert(MAGEWELL_CLOCK_TICKS_SECOND % 1000 == 0,
		"MAGEWELL_CLOCK_TICKS_SECOND must be mod 1k for this conversion");
	const timingclocktime_t ticksPerMs = MAGEWELL_CLOCK_TICKS_SECOND / 1000;
	m_frameOffsetTicks.store(frameOffsetMs * ticksPerMs);
}


//
// Signal reading
//


bool MagewellCaptureDevice::ReadHdrInfoFrame(
	HCHANNEL channel, SignalDescription& signal) const
{
	DWORD validFlags = 0;
	if (m_sdkInstance->Api().MWGetHDMIInfoFrameValidFlag(channel, &validFlags) != MW_SUCCEEDED)
		return false;
	if ((validFlags & MWCAP_HDMI_INFOFRAME_MASK_HDR) == 0)
		return false;

	HDMI_INFOFRAME_PACKET packet = {};
	if (m_sdkInstance->Api().MWGetHDMIInfoFramePacket(
		channel, MWCAP_HDMI_INFOFRAME_ID_HDR, &packet) != MW_SUCCEEDED)
		return false;

	const HDMI_HDR_INFOFRAME_PAYLOAD& hdr = packet.hdrInfoFramePayload;

	switch (hdr.byEOTF)
	{
	case 0: signal.eotf = EOTF::SDR; break;
	case 1: signal.eotf = EOTF::HDR; break;
	case 2: signal.eotf = EOTF::PQ;  break;
	case 3: signal.eotf = EOTF::HLG; break;
	default: signal.eotf = EOTF::UNKNOWN; break;
	}

	// Static metadata descriptor 0 is the only one defined by CTA-861.3.
	if (hdr.byMetadataDescriptorID != 0)
		return signal.eotf != EOTF::UNKNOWN;

	auto word = [](BYTE lsb, BYTE msb) -> double
	{
		return static_cast<double>(
			static_cast<unsigned int>(lsb) |
			(static_cast<unsigned int>(msb) << 8));
	};

	// Chromaticity values carry a 0.00002 unit, luminance is in cd/m2 except
	// the minimum which uses a 0.0001 unit.
	//
	// NOTE: CTA-861.3 numbers the primaries 0, 1 and 2 without naming them.
	// Red, green, blue is assumed here. The raw values are logged below so the
	// ordering can be confirmed against content with known mastering data.
	const double chroma = 0.00002;

	signal.hdrData.displayPrimaryRedX =
		word(hdr.display_primaries_lsb_x0, hdr.display_primaries_msb_x0) * chroma;
	signal.hdrData.displayPrimaryRedY =
		word(hdr.display_primaries_lsb_y0, hdr.display_primaries_msb_y0) * chroma;
	signal.hdrData.displayPrimaryGreenX =
		word(hdr.display_primaries_lsb_x1, hdr.display_primaries_msb_x1) * chroma;
	signal.hdrData.displayPrimaryGreenY =
		word(hdr.display_primaries_lsb_y1, hdr.display_primaries_msb_y1) * chroma;
	signal.hdrData.displayPrimaryBlueX =
		word(hdr.display_primaries_lsb_x2, hdr.display_primaries_msb_x2) * chroma;
	signal.hdrData.displayPrimaryBlueY =
		word(hdr.display_primaries_lsb_y2, hdr.display_primaries_msb_y2) * chroma;

	signal.hdrData.whitePointX =
		word(hdr.white_point_lsb_x, hdr.white_point_msb_x) * chroma;
	signal.hdrData.whitePointY =
		word(hdr.white_point_lsb_y, hdr.white_point_msb_y) * chroma;

	signal.hdrData.masteringDisplayMaxLuminance = word(
		hdr.max_display_mastering_lsb_luminance,
		hdr.max_display_mastering_msb_luminance);
	signal.hdrData.masteringDisplayMinLuminance = word(
		hdr.min_display_mastering_lsb_luminance,
		hdr.min_display_mastering_msb_luminance) * 0.0001;

	signal.hdrData.maxCll = word(
		hdr.maximum_content_light_level_lsb,
		hdr.maximum_content_light_level_msb);
	signal.hdrData.maxFall = word(
		hdr.maximum_frame_average_light_level_lsb,
		hdr.maximum_frame_average_light_level_msb);

	signal.hasHdrData = true;

	DebugLog::Log(
		"Magewell HDR InfoFrame: eotf=%d primaries=(%.4f,%.4f)(%.4f,%.4f)(%.4f,%.4f) "
		"white=(%.4f,%.4f) maxLum=%.0f minLum=%.4f maxCLL=%.0f maxFALL=%.0f",
		(int)hdr.byEOTF,
		signal.hdrData.displayPrimaryRedX, signal.hdrData.displayPrimaryRedY,
		signal.hdrData.displayPrimaryGreenX, signal.hdrData.displayPrimaryGreenY,
		signal.hdrData.displayPrimaryBlueX, signal.hdrData.displayPrimaryBlueY,
		signal.hdrData.whitePointX, signal.hdrData.whitePointY,
		signal.hdrData.masteringDisplayMaxLuminance,
		signal.hdrData.masteringDisplayMinLuminance,
		signal.hdrData.maxCll, signal.hdrData.maxFall);

	return true;
}


bool MagewellCaptureDevice::ReadSignal(
	HCHANNEL channel, SignalDescription& signal) const
{
	MWCAP_VIDEO_SIGNAL_STATUS status = {};
	if (m_sdkInstance->Api().MWGetVideoSignalStatus(channel, &status) != MW_SUCCEEDED)
		return false;

	signal = SignalDescription();
	signal.locked = (status.state == MWCAP_VIDEO_SIGNAL_LOCKED);
	if (!signal.locked)
		return true;

	signal.width = static_cast<unsigned int>(status.cx);
	signal.height = static_cast<unsigned int>(status.cy);
	signal.interlaced = status.bInterlaced != FALSE;
	signal.frameDuration100ns = status.dwFrameDuration;
	if (status.cx < 100 || status.cy < 100 ||
		signal.width > 10000 || signal.height > 10000 ||
		signal.frameDuration100ns == 0 ||
		signal.frameDuration100ns >= MAGEWELL_CLOCK_TICKS_SECOND ||
		static_cast<double>(MAGEWELL_CLOCK_TICKS_SECOND) /
			signal.frameDuration100ns < 23.0 ||
		static_cast<double>(MAGEWELL_CLOCK_TICKS_SECOND) /
			signal.frameDuration100ns > 120.0)
		throw std::runtime_error("Magewell reported invalid locked signal geometry or frame duration");
	signal.colorFormat = status.colorFormat;
	signal.quantRange = status.quantRange;

	MWCAP_INPUT_SPECIFIC_STATUS specific = {};
	if (m_sdkInstance->Api().MWGetInputSpecificStatus(channel, &specific) == MW_SUCCEEDED &&
		specific.bValid)
	{
		if (specific.dwVideoInputType == MWCAP_VIDEO_INPUT_TYPE_HDMI)
		{
			signal.bitDepth = specific.hdmiStatus.byBitDepth;
			signal.inputSampling = static_cast<int>(specific.hdmiStatus.pixelEncoding);
		}
		else if (specific.dwVideoInputType == MWCAP_VIDEO_INPUT_TYPE_SDI)
		{
			switch (specific.sdiStatus.sdiBitDepth)
			{
			case SDI_BIT_DEPTH_8BIT: signal.bitDepth = 8; break;
			case SDI_BIT_DEPTH_10BIT: signal.bitDepth = 10; break;
			case SDI_BIT_DEPTH_12BIT: signal.bitDepth = 12; break;
			default: break;
			}
			switch (specific.sdiStatus.sdiSamplingStruct)
			{
			case SDI_SAMPLING_444_RGB:
			case SDI_SAMPLING_4444_RGBA:
				signal.inputSampling = 0; break;
			case SDI_SAMPLING_422_YCbCr:
			case SDI_SAMPLING_4224_YCbCrA:
				signal.inputSampling = 1; break;
			case SDI_SAMPLING_444_YCbCr:
			case SDI_SAMPLING_4444_YCbCrA:
				signal.inputSampling = 2; break;
			case SDI_SAMPLING_420_YCbCr:
				signal.inputSampling = 3; break;
			default: break;
			}
		}
	}

	// HDR metadata is only carried over HDMI. Absence is the normal case.
	if (!ReadHdrInfoFrame(channel, signal))
		signal.eotf = EOTF::SDR;

	// Colour space follows the wire where the source declares it. RGB sources
	// usually declare no colorimetry at all, so the same vertical-line
	// fallback the DeckLink path uses is applied here.
	switch (signal.colorFormat)
	{
	case MWCAP_VIDEO_COLOR_FORMAT_YUV2020:
	case MWCAP_VIDEO_COLOR_FORMAT_YUV2020C:
		signal.colorSpace = ColorSpace::BT_2020;
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_YUV709:
		signal.colorSpace = ColorSpace::REC_709;
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_YUV601:
		signal.colorSpace = (signal.height > 576)
			? ColorSpace::REC_709
			: (signal.height == 576 ? ColorSpace::REC_601_576 : ColorSpace::REC_601_525);
		break;
	default:
		// PQ and HLG imply BT.2020 content regardless of what the wire says.
		if (signal.eotf == EOTF::PQ || signal.eotf == EOTF::HLG)
			signal.colorSpace = ColorSpace::BT_2020;
		else if (signal.height >= 720)
			signal.colorSpace = ColorSpace::REC_709;
		else if (signal.height == 576)
			signal.colorSpace = ColorSpace::REC_601_576;
		else
			signal.colorSpace = ColorSpace::REC_601_525;
		break;
	}

	return true;
}


void MagewellCaptureDevice::ChooseCaptureFormat(
	const SignalDescription& signal,
	const std::vector<DWORD>& supportedFormats,
	DWORD& fourcc, VideoFrameEncoding& encoding)
{
	const auto supports = [&supportedFormats](DWORD candidate)
	{
		return std::find(supportedFormats.begin(), supportedFormats.end(),
			candidate) != supportedFormats.end();
	};
	if (signal.colorFormat == MWCAP_VIDEO_COLOR_FORMAT_RGB)
	{
		if (signal.bitDepth > 0 && signal.bitDepth <= 8 &&
			supports(MWFOURCC_ARGB))
		{
			fourcc = MWFOURCC_ARGB;
			encoding = VideoFrameEncoding::ARGB_8BIT;
			return;
		}
		if (signal.bitDepth == 10 && supports(MWFOURCC_RGB10))
		{
			fourcc = MWFOURCC_RGB10;
			encoding = VideoFrameEncoding::R10l;
			return;
		}
		// Preserve the earlier deep-RGB compatibility route: the card converts
		// 12-bit RGB to limited-range 10-bit YCbCr P210, repacked below as V210.
		// This is intentionally lossy (12 -> 10 bits and 4:4:4 -> 4:2:2), not
		// native 12-bit RGB delivery. Keep unknown depths and other missing
		// capabilities rejected rather than silently degrading every RGB mode.
		if (signal.bitDepth != 12)
			throw std::runtime_error(
				"Magewell RGB input requires ARGB for 8-bit, RGB10 for 10-bit, or P210 conversion for 12-bit; unknown depth is unsupported");
	}
	if (signal.colorFormat != MWCAP_VIDEO_COLOR_FORMAT_RGB &&
		signal.colorFormat != MWCAP_VIDEO_COLOR_FORMAT_YUV601 &&
		signal.colorFormat != MWCAP_VIDEO_COLOR_FORMAT_YUV709 &&
		signal.colorFormat != MWCAP_VIDEO_COLOR_FORMAT_YUV2020 &&
		signal.colorFormat != MWCAP_VIDEO_COLOR_FORMAT_YUV2020C)
		throw std::runtime_error("Magewell input color format is unsupported or unknown");
	if ((signal.width & 1u) != 0)
		throw std::runtime_error("Magewell P210 requires an even input width");
	if (!supports(MWFOURCC_P210))
		throw std::runtime_error(
			"Magewell card does not support P210 required for YCbCr capture");
	fourcc = MWFOURCC_P210;
	encoding = VideoFrameEncoding::V210;
}


void MagewellCaptureDevice::RepackRGB10ToR10l(
	const uint8_t* source, uint32_t sourceStride,
	uint8_t* destination, uint32_t destinationStride,
	uint32_t width, uint32_t height)
{
	// SDK RGB10 uses DXGI_FORMAT_R10G10B10A2_UNORM: red occupies the low
	// ten bits, then green, blue and two alpha bits. VP's R10l stores the
	// same component codes in a little-endian R10:G10:B10:00 word, with
	// each destination row padded to 64 pixels. No quantization occurs here.
	for (uint32_t row = 0; row < height; ++row)
	{
		const uint8_t* input = source + (size_t)row * sourceStride;
		uint8_t* output = destination + (size_t)row * destinationStride;
		for (uint32_t pixel = 0; pixel < width; ++pixel)
		{
			uint32_t packed;
			memcpy(&packed, input + (size_t)pixel * 4, sizeof(packed));
			const uint32_t red = packed & 0x3FF;
			const uint32_t green = (packed >> 10) & 0x3FF;
			const uint32_t blue = (packed >> 20) & 0x3FF;
			const uint32_t r10l = (red << 22) | (green << 12) | (blue << 2);
			memcpy(output + (size_t)pixel * 4, &r10l, sizeof(r10l));
		}
	}
}


void MagewellCaptureDevice::RepackP210ToV210(
	const uint8_t* source, uint32_t sourceStride,
	uint8_t* destination, uint32_t destinationStride,
	uint32_t width, uint32_t height)
{
	// P210: a full-height luma plane of 16-bit samples followed by a
	// full-height interleaved chroma plane, both at the same stride.
	const uint32_t lumaPlaneBytes = sourceStride * height;

	// v210 packs six pixels into four little-endian 32-bit words, three
	// 10-bit components per word. Whole groups are written by straight-line
	// code with no per-sample bounds test; only a trailing partial group
	// needs clamping, and at the widths this card produces there is none.
	const uint32_t wholeGroups = width / 6;
	const uint32_t tailStart = wholeGroups * 6;

	for (uint32_t row = 0; row < height; ++row)
	{
		const uint16_t* luma = reinterpret_cast<const uint16_t*>(
			source + (size_t)row * sourceStride);
		const uint16_t* chroma = reinterpret_cast<const uint16_t*>(
			source + lumaPlaneBytes + (size_t)row * sourceStride);
		uint32_t* out = reinterpret_cast<uint32_t*>(
			destination + (size_t)row * destinationStride);

		for (uint32_t group = 0; group < wholeGroups; ++group)
		{
			const uint32_t l0 = luma[0] >> 6;
			const uint32_t l1 = luma[1] >> 6;
			const uint32_t l2 = luma[2] >> 6;
			const uint32_t l3 = luma[3] >> 6;
			const uint32_t l4 = luma[4] >> 6;
			const uint32_t l5 = luma[5] >> 6;

			const uint32_t c0 = chroma[0] >> 6;
			const uint32_t c1 = chroma[1] >> 6;
			const uint32_t c2 = chroma[2] >> 6;
			const uint32_t c3 = chroma[3] >> 6;
			const uint32_t c4 = chroma[4] >> 6;
			const uint32_t c5 = chroma[5] >> 6;

			out[0] = c0 | (l0 << 10) | (c1 << 20);
			out[1] = l1 | (c2 << 10) | (l2 << 20);
			out[2] = c3 | (l3 << 10) | (c4 << 20);
			out[3] = l4 | (c5 << 10) | (l5 << 20);

			luma += 6;
			chroma += 6;
			out += 4;
		}

		if (tailStart < width)
		{
			uint32_t l[6];
			uint32_t c[6];
			for (uint32_t i = 0; i < 6; ++i)
			{
				// Repeat the last sample rather than reading past the row.
				const uint32_t index =
					(tailStart + i < width) ? (tailStart + i) : (width - 1);
				l[i] = reinterpret_cast<const uint16_t*>(
					source + (size_t)row * sourceStride)[index] >> 6;
				c[i] = reinterpret_cast<const uint16_t*>(
					source + lumaPlaneBytes +
					(size_t)row * sourceStride)[index] >> 6;
			}

			out[0] = c[0] | (l[0] << 10) | (c[1] << 20);
			out[1] = l[1] | (c[2] << 10) | (l[2] << 20);
			out[2] = c[3] | (l[3] << 10) | (c[4] << 20);
			out[3] = l[4] | (c[5] << 10) | (l[5] << 20);
		}
	}
}


uint32_t MagewellCaptureDevice::BytesPerRowFor(
	VideoFrameEncoding encoding, unsigned int width)
{
	// Must agree exactly with VideoState::BytesPerRow(), because the renderer
	// derives the frame size from the video state rather than from us.
	switch (encoding)
	{
	case VideoFrameEncoding::V210:
		return ((width + 47) / 48) * 128;
	case VideoFrameEncoding::BGRA_8BIT:
	case VideoFrameEncoding::ARGB_8BIT:
		return width * 4;
	case VideoFrameEncoding::R10l:
		return ((width + 63) / 64) * 256;
	default:
		throw std::runtime_error("Unsupported Magewell capture encoding");
	}
}


void MagewellCaptureDevice::PublishCardState(const SignalDescription& signal)
{
	if (!m_callback)
		return;

	CaptureDeviceCardStateComPtr cardState = new CaptureDeviceCardState();
	if (!cardState)
		return;

	cardState->inputLocked = signal.locked ? InputLocked::YES : InputLocked::NO;

	if (signal.locked && signal.frameDuration100ns > 0)
		cardState->inputDisplayMode = std::make_shared<DisplayMode>(
			signal.width, signal.height, signal.interlaced,
			static_cast<unsigned int>(MAGEWELL_CLOCK_TICKS_SECOND),
			signal.frameDuration100ns);

	// Show the wire format independently of the supported capture output.
	switch (signal.colorFormat)
	{
	case MWCAP_VIDEO_COLOR_FORMAT_RGB:
		cardState->inputEncoding = ColorFormat::RGB444;
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_YUV601:
	case MWCAP_VIDEO_COLOR_FORMAT_YUV709:
	case MWCAP_VIDEO_COLOR_FORMAT_YUV2020:
	case MWCAP_VIDEO_COLOR_FORMAT_YUV2020C:
		cardState->inputEncoding = signal.inputSampling == 1
			? ColorFormat::YCbCr422 : ColorFormat::UNKNOWN;
		break;
	default:
		cardState->inputEncoding = ColorFormat::UNKNOWN;
		break;
	}

	switch (signal.bitDepth)
	{
	case 8:  cardState->inputBitDepth = BitDepth::BITDEPTH_8BIT;  break;
	case 10: cardState->inputBitDepth = BitDepth::BITDEPTH_10BIT; break;
	case 12: cardState->inputBitDepth = BitDepth::BITDEPTH_12BIT; break;
	default: cardState->inputBitDepth = BitDepth::UNKNOWN;        break;
	}

	CString text;
	if (signal.inputSampling == 2)
		cardState->other.push_back(CString(_T("Wire sampling: YCbCr 4:4:4")));
	else if (signal.inputSampling == 3)
		cardState->other.push_back(CString(_T("Wire sampling: YCbCr 4:2:0")));

	if (signal.quantRange == MWCAP_VIDEO_QUANTIZATION_FULL)
		cardState->other.push_back(CString(_T("Quantization range: full")));
	else if (signal.quantRange == MWCAP_VIDEO_QUANTIZATION_LIMITED)
		cardState->other.push_back(CString(_T("Quantization range: limited")));

	const HCHANNEL channel = m_channel.load(std::memory_order_acquire);
	if (channel)
	{
		MWCAP_PCIE_CAPTURE_INFO pcie = {};
		if (m_sdkInstance->Api().MWGetFamilyInfo(channel, &pcie, sizeof(pcie)) == MW_SUCCEEDED)
		{
			text.Format(_T("PCIe link width: %u"), (unsigned int)pcie.byLinkWidth);
			cardState->other.push_back(text);

			text.Format(_T("PCIe link speed: %u"), (unsigned int)pcie.byLinkType);
			cardState->other.push_back(text);

			text.Format(_T("PCIe max payload: %u"), (unsigned int)pcie.wMaxPayloadSize);
			cardState->other.push_back(text);
		}
	}

	m_callback->OnCaptureDeviceCardStateChange(cardState);
}


bool MagewellCaptureDevice::PublishVideoState(
	const SignalDescription& signal, VideoFrameEncoding encoding,
	CaptureRunToken captureRunToken)
{
	if (!m_callback)
		return false;

	// Assignment rather than Attach: assignment add-refs, Attach does not,
	// and without that reference the state is destroyed the moment this
	// pointer goes out of scope while the renderer still holds it.
	VideoStateComPtr videoState = new VideoState();
	if (!videoState)
		throw std::runtime_error("Failed to allocate VideoState");

	// The Magewell frame duration is in 100ns units, which maps directly onto
	// a 10,000,000 tick time scale.
	videoState->displayMode = std::make_shared<DisplayMode>(
		signal.width, signal.height, signal.interlaced,
		static_cast<unsigned int>(MAGEWELL_CLOCK_TICKS_SECOND),
		signal.frameDuration100ns);
	videoState->videoFrameEncoding = encoding;
	videoState->eotf = signal.eotf;
	videoState->colorspace = signal.colorSpace;
	videoState->invertedVertical = false;

	// Only attach HDR metadata when the source actually sent some. An empty
	// block is not harmless: the renderer reads it as present-but-zero and
	// does colour volume maths on zero primaries.
	if (signal.hasHdrData && signal.hdrData.IsValid())
	{
		videoState->hdrData = std::make_shared<HDRData>();
		*(videoState->hdrData) = signal.hdrData;
	}
	videoState->valid = true;

	DebugLog::Log(
		"Magewell video state: %ux%u%s @ %.3f Hz, %u-bit, capture=%s, "
		"colorspace=%d, eotf=%d, hdr_metadata=%d",
		signal.width, signal.height, signal.interlaced ? "i" : "p",
		signal.frameDuration100ns > 0
			? (double)MAGEWELL_CLOCK_TICKS_SECOND / signal.frameDuration100ns : 0.0,
		(unsigned int)signal.bitDepth,
		encoding == VideoFrameEncoding::V210 ? "V210" :
			(encoding == VideoFrameEncoding::R10l ? "R10l" : "ARGB"),
		(int)signal.colorSpace, (int)signal.eotf, signal.hasHdrData ? 1 : 0);

	m_callback->OnCaptureDeviceVideoStateChange(
		this, captureRunToken, videoState);
	return true;
}


void MagewellCaptureDevice::PublishInvalidVideoState(
	CaptureRunToken captureRunToken)
{
	if (!m_callback)
		return;
	VideoStateComPtr invalidState = new VideoState();
	invalidState->valid = false;
	m_callback->OnCaptureDeviceVideoStateChange(
		this, captureRunToken, invalidState);
}


//
// Capture thread
//


void MagewellCaptureDevice::CaptureThread(CaptureRunToken captureRunToken)
{
	// An exception escaping a std::thread function calls std::terminate, which
	// kills the whole application with no message and no log line. Every path
	// out of the capture body is therefore contained here, including exceptions
	// raised by VP's own frame and state callbacks.
	try
	{
		CaptureThreadBody(captureRunToken);
	}
	catch (const std::exception& exception)
	{
		DebugLog::Log("Magewell capture thread aborted: %s", exception.what());
		if (m_state.load() == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		{
			try { PublishInvalidVideoState(captureRunToken); }
			catch (...) { DebugLog::Log("Magewell invalid video state callback threw"); }
		}
		try { Error(CString(exception.what())); }
		catch (...) { DebugLog::Log("Magewell error callback threw"); }
	}
	catch (...)
	{
		DebugLog::Log("Magewell capture thread aborted on an unknown exception");
		if (m_state.load() == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
		{
			try { PublishInvalidVideoState(captureRunToken); }
			catch (...) { DebugLog::Log("Magewell invalid video state callback threw"); }
		}
		try { Error(TEXT("Unknown error in the Magewell capture thread")); }
		catch (...) { DebugLog::Log("Magewell error callback threw"); }
	}
	m_captureThreadRunning.store(false, std::memory_order_release);
	if (m_state.load() == CaptureDeviceState::CAPTUREDEVICESTATE_STARTING)
	{
		{
			std::lock_guard<std::mutex> clockLock(m_clockMutex);
			const HCHANNEL channel = m_channel.exchange(
				nullptr, std::memory_order_acq_rel);
			if (channel)
			{
				RestoreInputScan(channel);
				m_sdkInstance->Api().MWCloseChannel(channel);
			}
		}
		try { UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_READY); }
		catch (...) { DebugLog::Log("Magewell READY callback threw"); }
	}
}


void MagewellCaptureDevice::CaptureThreadBody(CaptureRunToken captureRunToken)
{
	const HCHANNEL channel = m_channel.load(std::memory_order_acquire);
	if (!channel)
		return;

	HANDLE captureEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	HANDLE notifyEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	HNOTIFY notify = nullptr;
	std::vector<std::shared_ptr<MagewellCaptureBuffer>> buffers;
	int bufferIndex = 0;

	SignalDescription current;
	bool haveState = false;
	bool haveCardState = false;
	DWORD fourcc = 0;
	VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
	uint32_t stride = 0;
	uint32_t frameSize = 0;
	// Geometry the card writes with, which differs from the geometry VP
	// reads when a repack sits between them.
	uint32_t captureStride = 0;
	uint32_t captureFrameSize = 0;
	bool repackP210 = false;
	bool repackRGB10 = false;
	std::vector<uint8_t> scratch;
	std::vector<DWORD> supportedFormats;
	bool capturing = false;
	std::string lastUnsupportedReason;
	auto lastSignalPoll = std::chrono::steady_clock::now();
	auto lastFrameNotification = lastSignalPoll;
	int notifyFailures = 0, signalFailures = 0, bufferFailures = 0;
	int copyFailures = 0, frameInfoFailures = 0, clockFailures = 0;
	const auto sdkFailure = [](int& failures, const char* message)
	{
		if (++failures >= 5)
			throw std::runtime_error(message);
	};
	const auto unsupportedSignal = [&](const char* reason)
	{
		if (haveState)
			PublishInvalidVideoState(captureRunToken);
		if (capturing)
		{
			m_sdkInstance->Api().MWStopVideoCapture(channel);
			capturing = false;
		}
		buffers.clear();
		haveState = false;
		if (lastUnsupportedReason != reason)
		{
			lastUnsupportedReason = reason;
			Error(CString(reason));
		}
	};

	// Every in-flight renderer reference retains its own shared owner in
	// MagewellCaptureBuffer. Clearing this pool on a format change or an
	// exception cannot free a buffer still being read downstream.
	auto stopCaptureFn = [&]()
	{
		if (notify != nullptr)
			m_sdkInstance->Api().MWUnregisterNotify(channel, notify);
		if (capturing)
			m_sdkInstance->Api().MWStopVideoCapture(channel);
		buffers.clear();
		if (notifyEvent) CloseHandle(notifyEvent);
		if (captureEvent) CloseHandle(captureEvent);
		DebugLog::Log("MagewellCaptureDevice capture thread ended, %llu frames",
			(unsigned long long)m_capturedVideoFrameCount.load());
	};
	MagewellScopeExit<decltype(stopCaptureFn)> stopCapture(stopCaptureFn);

	if (!captureEvent || !notifyEvent)
	{
		Error(TEXT("Failed to create Magewell capture events"));
		goto cleanup;
	}

	if (m_sdkInstance->Api().MWStartVideoCapture(channel, captureEvent) != MW_SUCCEEDED)
	{
		Error(TEXT("Failed to start Magewell video capture"));
		goto cleanup;
	}
	capturing = true;

	notify = m_sdkInstance->Api().MWRegisterNotify(channel, notifyEvent,
		MWCAP_NOTIFY_VIDEO_FRAME_BUFFERED |
		MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE |
		MWCAP_NOTIFY_INPUT_SPECIFIC_CHANGE |
		MWCAP_NOTIFY_HDMI_INFOFRAME_HDR |
		MWCAP_NOTIFY_HDMI_INFOFRAME_AVI);
	if (notify == nullptr)
	{
		Error(TEXT("Failed to register for Magewell notifications"));
		goto cleanup;
	}

	UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);

	// Ask the card which output colour formats its conversion engine can
	// actually produce. A FOURCC existing in the SDK header does not mean
	// this model implements it; an unsupported one succeeds and writes
	// nothing, which reads downstream as a blank frame.
	{
		int formatCount = 0;
		if (m_sdkInstance->Api().MWGetVideoCaptureSupportColorFormat(channel, nullptr, &formatCount)
			!= MW_SUCCEEDED || formatCount <= 0 || formatCount > 256)
			throw std::runtime_error("Magewell capture format capability query failed");
		supportedFormats.resize(formatCount);
		if (m_sdkInstance->Api().MWGetVideoCaptureSupportColorFormat(
			channel, supportedFormats.data(), &formatCount) != MW_SUCCEEDED ||
			formatCount <= 0 || formatCount > static_cast<int>(supportedFormats.size()))
			throw std::runtime_error("Magewell capture format capability list failed");
		supportedFormats.resize(formatCount);
	}

	while (m_captureThreadRunning.load(std::memory_order_acquire))
	{
		ULONGLONG notifyStatus = 0;
		const DWORD waitResult = WaitForSingleObject(notifyEvent, 100);
		if (waitResult == WAIT_TIMEOUT)
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - lastSignalPoll < std::chrono::milliseconds(250))
				continue;
			lastSignalPoll = now;
			notifyStatus = MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE;
		}
		else if (waitResult != WAIT_OBJECT_0)
			throw std::runtime_error("Magewell notification wait failed; capture stopped. Restart required");
		else if (m_sdkInstance->Api().MWGetNotifyStatus(channel, notify, &notifyStatus) != MW_SUCCEEDED)
		{
			sdkFailure(notifyFailures, "Magewell notifications repeatedly failed; capture stopped. Restart required");
			continue;
		}
		else
		{
			notifyFailures = 0;
			if (notifyStatus & MWCAP_NOTIFY_VIDEO_FRAME_BUFFERED)
				lastFrameNotification = std::chrono::steady_clock::now();
		}

		const bool formatEvent = (notifyStatus & (
			MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE |
			MWCAP_NOTIFY_INPUT_SPECIFIC_CHANGE |
			MWCAP_NOTIFY_HDMI_INFOFRAME_HDR |
			MWCAP_NOTIFY_HDMI_INFOFRAME_AVI)) != 0;

		if (formatEvent || !haveState)
		{
			SignalDescription signal;
			bool signalRead = false;
			try { signalRead = ReadSignal(channel, signal); }
			catch (const std::exception& exception)
			{
				current = signal;
				unsupportedSignal(exception.what());
				continue;
			}
			if (!signalRead)
			{
				sdkFailure(signalFailures, "Magewell signal status repeatedly failed; capture stopped. Restart required");
				continue;
			}
			signalFailures = 0;

			if (!haveCardState || !signal.SameFormatAs(current))
			{
				PublishCardState(signal);
				haveCardState = true;
			}

			if (!signal.SameFormatAs(current))
				DebugLog::Log(
					"Magewell signal read: locked=%d %ux%u colorFormat=%d "
					"bitDepth=%u quant=%d eotf=%d (event=%d)",
					signal.locked ? 1 : 0, signal.width, signal.height,
					(int)signal.colorFormat, (unsigned int)signal.bitDepth,
					(int)signal.quantRange, (int)signal.eotf,
					formatEvent ? 1 : 0);

			if (!signal.locked)
			{
				if (haveState)
				{
					VideoStateComPtr invalidState = new VideoState();
					invalidState->valid = false;
					if (m_callback)
						m_callback->OnCaptureDeviceVideoStateChange(
							this, captureRunToken, invalidState);
					DebugLog::Log("Magewell signal lost");
				}
				haveState = false;
				if (capturing)
				{
					m_sdkInstance->Api().MWStopVideoCapture(channel);
					capturing = false;
				}
				buffers.clear();
				current = signal;
				continue;
			}

			if (!haveState || !signal.SameFormatAs(current))
			{
				// Stop capture before touching the buffer set. A frame copy in
				// flight must not land in a buffer that is about to be freed.
				if (capturing)
				{
					m_sdkInstance->Api().MWStopVideoCapture(channel);
					capturing = false;
				}
				buffers.clear();
				current = signal;

				try { ChooseCaptureFormat(current, supportedFormats, fourcc, encoding); }
				catch (const std::exception& exception)
				{
					unsupportedSignal(exception.what());
					continue;
				}
				if (current.colorFormat == MWCAP_VIDEO_COLOR_FORMAT_RGB &&
					current.bitDepth == 12)
					DebugLog::Log("Magewell compatibility conversion: RGB 12-bit input -> P210/V210 10-bit YCbCr 4:2:2; precision and chroma reduced");
				stride = BytesPerRowFor(encoding, current.width);
				const uint64_t deliveryBytes =
					static_cast<uint64_t>(stride) * current.height;
				if (deliveryBytes == 0 || deliveryBytes > 128ULL * 1024 * 1024)
					throw std::runtime_error("Magewell capture frame exceeds supported size");
				frameSize = static_cast<uint32_t>(deliveryBytes);

				// v210 is delivered to VP but captured from the card as P210,
				// which is planar: luma plane then chroma plane, both at
				// width * 2 bytes per row.
				repackP210 = (encoding == VideoFrameEncoding::V210);
				repackRGB10 = (encoding == VideoFrameEncoding::R10l);
				if (repackP210)
				{
					// P210 is planar and a different size to v210, so the card
					// writes into scratch and the repack fills the delivery
					// buffer.
					captureStride = current.width * 2;
					const uint64_t sourceBytes =
						static_cast<uint64_t>(captureStride) * current.height * 2;
					if (sourceBytes > 128ULL * 1024 * 1024)
						throw std::runtime_error("Magewell P210 frame exceeds supported size");
					captureFrameSize = static_cast<uint32_t>(sourceBytes);
					scratch.assign(captureFrameSize, 0);
				}
				else if (repackRGB10)
				{
					captureStride = current.width * 4;
					const uint64_t sourceBytes =
						static_cast<uint64_t>(captureStride) * current.height;
					if (sourceBytes > 128ULL * 1024 * 1024)
						throw std::runtime_error("Magewell RGB10 frame exceeds supported size");
					captureFrameSize = static_cast<uint32_t>(sourceBytes);
					scratch.assign(captureFrameSize, 0);
				}
				else
				{
					// ARGB is delivered directly without a repack.
					captureStride = stride;
					captureFrameSize = frameSize;
					scratch.clear();
				}

				DebugLog::Log(
					"Magewell capture geometry: card fourcc=0x%08X stride=%u "
					"size=%u -> VP stride=%u size=%u repack=%d",
					fourcc, captureStride, captureFrameSize,
					stride, frameSize, repackP210 ? 1 : 0);

				buffers.clear();
				buffers.reserve(MAGEWELL_CAPTURE_BUFFER_COUNT);
				for (int i = 0; i < MAGEWELL_CAPTURE_BUFFER_COUNT; ++i)
					buffers.push_back(
						std::make_shared<MagewellCaptureBuffer>(frameSize,
							m_memoryBudget));
				bufferIndex = 0;

				if (m_sdkInstance->Api().MWStartVideoCapture(channel, captureEvent) != MW_SUCCEEDED)
					throw std::runtime_error("Failed to restart Magewell capture after format change");
				capturing = true;

				if (!PublishVideoState(current, encoding, captureRunToken))
					continue;
				haveState = true;
				lastUnsupportedReason.clear();
				lastFrameNotification = std::chrono::steady_clock::now();
			}

			if (haveState &&
				std::chrono::steady_clock::now() - lastFrameNotification >
					std::chrono::seconds(2))
				throw std::runtime_error(
					"Magewell stopped reporting buffered frames for two seconds; capture stopped. Restart required");

			if (formatEvent)
				continue;
		}

		if ((notifyStatus & MWCAP_NOTIFY_VIDEO_FRAME_BUFFERED) == 0)
			continue;
		if (!haveState || buffers.empty())
			continue;
		if (!m_outputCaptureData.load(std::memory_order_acquire))
			continue;

		MWCAP_VIDEO_BUFFER_INFO bufferInfo = {};
		if (m_sdkInstance->Api().MWGetVideoBufferInfo(channel, &bufferInfo) != MW_SUCCEEDED)
		{
			sdkFailure(bufferFailures, "Magewell video buffer status repeatedly failed; capture stopped. Restart required");
			continue;
		}
		bufferFailures = 0;

		// Take the next buffer the renderer has finished with. Grow within the
		// Magewell memory budget when all are held; configured renderer queue
		// and lookahead depth can exceed the initial eight buffers.
		MagewellCaptureBuffer* buffer = nullptr;
		for (size_t attempt = 0; attempt < buffers.size(); ++attempt)
		{
			MagewellCaptureBuffer* candidate = buffers[bufferIndex].get();
			bufferIndex = (bufferIndex + 1) % (int)buffers.size();
			if (candidate && !candidate->InUse())
			{
				buffer = candidate;
				break;
			}
		}

		if (!buffer)
		{
			buffers.push_back(std::make_shared<MagewellCaptureBuffer>(
				frameSize, m_memoryBudget));
			buffer = buffers.back().get();
			bufferIndex = 0;
			DebugLog::Log("Magewell capture pool expanded to %u buffers",
				static_cast<unsigned int>(buffers.size()));
		}

		// The output colour format and range are stated explicitly so the
		// card's conversion matches what the video state promises downstream.
		// State the output matrix and range so captured bytes agree with the
		// existing VP converters. ARGB expects full RGB; R10l expects limited
		// RGB; V210 expects limited YCbCr in the signaled color space.


		if (m_sdkInstance->Api().MWCaptureVideoFrameToVirtualAddressEx(
			channel,
			MWCAP_VIDEO_FRAME_ID_NEWEST_BUFFERED,
			(repackP210 || repackRGB10) ? scratch.data() : buffer->Data(),
			captureFrameSize,
			captureStride,
			FALSE,                                  // not bottom up
			nullptr,                                // context
			fourcc,
			(int)current.width,
			(int)current.height,
			0,                                      // process switches
			0,                                      // partial notify
			nullptr,                                // OSD image
			nullptr,                                // OSD rects
			0,                                      // OSD rect count
			100,                                    // contrast
			0,                                      // brightness
			100,                                    // saturation
			0,                                      // hue
			MWCAP_VIDEO_DEINTERLACE_WEAVE,
			MWCAP_VIDEO_ASPECT_RATIO_IGNORE,
			nullptr,                                // source rect
			nullptr,                                // destination rect
			0,                                      // aspect X
			0,                                      // aspect Y
			(repackRGB10 || encoding == VideoFrameEncoding::ARGB_8BIT)
				? MWCAP_VIDEO_COLOR_FORMAT_RGB :
				repackP210
					? (current.colorSpace == ColorSpace::BT_2020
						? MWCAP_VIDEO_COLOR_FORMAT_YUV2020
						: (current.colorSpace == ColorSpace::REC_601_525 ||
							current.colorSpace == ColorSpace::REC_601_576)
							? MWCAP_VIDEO_COLOR_FORMAT_YUV601
							: MWCAP_VIDEO_COLOR_FORMAT_YUV709)
					: MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN,
			encoding == VideoFrameEncoding::ARGB_8BIT
				? MWCAP_VIDEO_QUANTIZATION_FULL
				: MWCAP_VIDEO_QUANTIZATION_LIMITED,
			MWCAP_VIDEO_SATURATION_UNKNOWN) != MW_SUCCEEDED)
		{
			sdkFailure(copyFailures, "Magewell frame copy repeatedly failed; capture stopped. Restart required");
			continue;
		}
		copyFailures = 0;

		if (WaitForSingleObject(captureEvent, 200) != WAIT_OBJECT_0)
			throw std::runtime_error("Magewell frame copy timed out; capture stopped to avoid a late write into reused memory");

		MWCAP_VIDEO_CAPTURE_STATUS captureStatus = {};
		const MW_RESULT captureStatusResult =
			m_sdkInstance->Api().MWGetVideoCaptureStatus(channel, &captureStatus);
		if (captureStatusResult != MW_SUCCEEDED ||
			captureStatus.bFrameCompleted == FALSE)
			throw std::runtime_error("Magewell frame copy did not complete; capture stopped before reusing its memory");
		MWCAP_VIDEO_FRAME_INFO frameInfo = {};
		if (m_sdkInstance->Api().MWGetVideoFrameInfo(
			channel, captureStatus.iFrame, &frameInfo) != MW_SUCCEEDED)
		{
			sdkFailure(frameInfoFailures, "Magewell frame information repeatedly failed; capture stopped. Restart required");
			continue;
		}
		frameInfoFailures = 0;

		// Convert the card's P210 into the v210 the renderer expects. Done
		// before the frame is handed over so everything downstream sees the
		// same layout the DeckLink path produces natively.
		if (repackP210)
			RepackP210ToV210(
				scratch.data(), captureStride,
				buffer->Data(), stride,
				current.width, current.height);
		else if (repackRGB10)
			RepackRGB10ToR10l(
				scratch.data(), captureStride,
				buffer->Data(), stride,
				current.width, current.height);

		const timingclocktime_t rawCaptureTicks =
			static_cast<timingclocktime_t>(frameInfo.allFieldStartTimes[0]);

		// The baseline was fixed before capture started. An older buffered
		// frame must not move the stream clock backwards.
		const timingclocktime_t baseline =
			m_timingBaselineTicks.load(std::memory_order_acquire);
		if (rawCaptureTicks < baseline)
			continue;
		const timingclocktime_t captureTimestamp =
			rawCaptureTicks - baseline;

		LONGLONG deviceTime = 0;
		if (m_sdkInstance->Api().MWGetDeviceTime(channel, &deviceTime) == MW_SUCCEEDED)
		{
			m_hardwareLatencyMs.store(
				(double)(deviceTime - rawCaptureTicks) / 10000.0);
			clockFailures = 0;
		}
		else
		{
			sdkFailure(clockFailures, "Magewell hardware clock repeatedly failed; capture stopped. Restart required");
			continue;
		}


		const uint64_t counter = ++m_capturedVideoFrameCount;

		VideoFrame videoFrame(
			buffer->Data(),
			counter,
			captureTimestamp + m_frameOffsetTicks.load(),
			static_cast<IUnknown*>(buffer),
			captureTimestamp);

		if (counter == 1 || counter % 300 == 0)
			DebugLog::Log("Magewell delivering frame %llu, latency %.1f ms",
				(unsigned long long)counter, m_hardwareLatencyMs.load());

		if (m_callback && m_outputCaptureData.load(std::memory_order_acquire))
			m_callback->OnCaptureDeviceVideoFrame(this, captureRunToken, videoFrame);
	}

cleanup:
	// The scope exit above also runs when callbacks, allocations or SDK calls
	// throw. It stops capture before dropping the pool and closing events.
	return;
}


void MagewellCaptureDevice::UpdateState(CaptureDeviceState state)
{
	if (m_state.load() == state)
		return;

	m_state.store(state);

	if (m_callback)
		m_callback->OnCaptureDeviceState(state);
}


void MagewellCaptureDevice::Error(const CString& error)
{
	DebugLog::Log("MagewellCaptureDevice error: %S", error.GetString());

	if (m_callback)
		m_callback->OnCaptureDeviceError(error);
}


void MagewellCaptureDevice::RestoreInputScan(HCHANNEL channel) noexcept
{
	if (!m_restoreScan)
		return;
	m_restoreScan = false;
	try
	{
		if (m_sdkInstance->Api().MWSetInputSourceScan(
			channel, m_originalScanEnabled) != MW_SUCCEEDED)
			DebugLog::Log("Magewell failed to restore input AutoScan state");
	}
	catch (...)
	{
		DebugLog::Log("Magewell input AutoScan restoration threw");
	}
}


//
// ITimingClock
//


timingclocktime_t MagewellCaptureDevice::TimingClockNow()
{
	std::lock_guard<std::mutex> clockLock(m_clockMutex);
	const HCHANNEL channel = m_channel.load(std::memory_order_acquire);
	if (!channel)
		return m_lastClockTicks;

	LONGLONG deviceTime = 0;
	try
	{
		if (m_sdkInstance->Api().MWGetDeviceTime(channel, &deviceTime) != MW_SUCCEEDED)
			return m_lastClockTicks;
	}
	catch (...)
	{
		return m_lastClockTicks;
	}

	// The baseline is set before capture starts, so the clock and every frame
	// use the same zero even before the first frame is delivered.
	const timingclocktime_t baseline =
		m_timingBaselineTicks.load(std::memory_order_acquire);
	if (baseline < 0)
		return m_lastClockTicks;
	const timingclocktime_t rebased =
		static_cast<timingclocktime_t>(deviceTime) - baseline;
	if (rebased > m_lastClockTicks)
		m_lastClockTicks = rebased;
	return m_lastClockTicks;
}


timingclocktime_t MagewellCaptureDevice::TimingClockTicksPerSecond() const
{
	return MAGEWELL_CLOCK_TICKS_SECOND;
}


const TCHAR* MagewellCaptureDevice::TimingClockDescription()
{
	return TEXT("Magewell hardware clock");
}


//
// IUnknown
//


HRESULT MagewellCaptureDevice::QueryInterface(REFIID iid, LPVOID* ppv)
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


ULONG MagewellCaptureDevice::AddRef(void)
{
	return ++m_refCount;
}


ULONG MagewellCaptureDevice::Release(void)
{
	const ULONG newRefValue = --m_refCount;
	if (newRefValue == 0)
		delete this;

	return newRefValue;
}
