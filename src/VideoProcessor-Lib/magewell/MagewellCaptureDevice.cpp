/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "MagewellCaptureDevice.h"

#include <MWFOURCC.h>
#include <LibMWCapture/MWHDMIPackets.h>

#include <DebugLog.h>
#include <VideoState.h>

#include <stdexcept>


// The Magewell device clock is expressed in 100ns units, which is 10,000,000
// ticks per second. Confirmed by the SDK's own frame duration maths, where
// frames per second is 10000000 / dwFrameDuration.
static const timingclocktime_t MAGEWELL_CLOCK_TICKS_SECOND = 10000000LL;

// Number of pinned capture buffers. The renderer may still be reading the
// previous frame when the next arrives, so buffers are cycled rather than
// reused immediately.
static const int MAGEWELL_CAPTURE_BUFFER_COUNT = 8;


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
	return ++m_refCount;
}


ULONG STDMETHODCALLTYPE MagewellCaptureBuffer::Release()
{
	// Deliberately does not delete. The buffer pool owns this object for the
	// whole capture run; reaching zero just marks it free for reuse.
	return --m_refCount;
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

	DWORD sources[16] = {};
	DWORD sourceCount = ARRAYSIZE(sources);
	if (MWGetVideoInputSourceArray(channel, sources, &sourceCount) == MW_SUCCEEDED)
	{
		DWORD seen = 0;
		for (DWORD index = 0; index < sourceCount; ++index)
		{
			const DWORD type = INPUT_TYPE(sources[index]);
			if ((seen & type) != 0)
				continue;
			seen |= type;

			switch (type)
			{
			case MWCAP_VIDEO_INPUT_TYPE_HDMI:
				m_captureInputSet.push_back(CaptureInput(
					static_cast<CaptureInputId>(MWCAP_VIDEO_INPUT_TYPE_HDMI),
					CaptureInputType::HDMI, TEXT("HDMI")));
				break;
			case MWCAP_VIDEO_INPUT_TYPE_SDI:
				m_captureInputSet.push_back(CaptureInput(
					static_cast<CaptureInputId>(MWCAP_VIDEO_INPUT_TYPE_SDI),
					CaptureInputType::SDI_ELECTRICAL, TEXT("SDI")));
				break;
			case MWCAP_VIDEO_INPUT_TYPE_COMPONENT:
				m_captureInputSet.push_back(CaptureInput(
					static_cast<CaptureInputId>(MWCAP_VIDEO_INPUT_TYPE_COMPONENT),
					CaptureInputType::COMPONENT, TEXT("Component")));
				break;
			case MWCAP_VIDEO_INPUT_TYPE_CVBS:
				m_captureInputSet.push_back(CaptureInput(
					static_cast<CaptureInputId>(MWCAP_VIDEO_INPUT_TYPE_CVBS),
					CaptureInputType::COMPOSITE, TEXT("Composite")));
				break;
			case MWCAP_VIDEO_INPUT_TYPE_YC:
				m_captureInputSet.push_back(CaptureInput(
					static_cast<CaptureInputId>(MWCAP_VIDEO_INPUT_TYPE_YC),
					CaptureInputType::S_VIDEO, TEXT("S-Video")));
				break;
			default:
				break;
			}
		}
	}

	MWCloseChannel(channel);

	if (!m_captureInputSet.empty())
		m_captureInputId = m_captureInputSet.front().id;

	m_canCapture = !m_captureInputSet.empty();
	m_state = m_canCapture
		? CaptureDeviceState::CAPTUREDEVICESTATE_READY
		: CaptureDeviceState::CAPTUREDEVICESTATE_FAILED;
}


MagewellCaptureDevice::~MagewellCaptureDevice()
{
	StopCapture();
	m_callback = nullptr;
}


HCHANNEL MagewellCaptureDevice::OpenChannel() const
{
	// MWOpenChannelByPath takes a mutable buffer, so a local copy is passed
	// rather than the stored string's buffer.
	WCHAR path[128] = {};
	wcsncpy_s(path, m_devicePath.c_str(), _TRUNCATE);
	return MWOpenChannelByPath(path);
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
	if (m_captureThreadRunning.load(std::memory_order_acquire))
		throw std::runtime_error("StartCapture() called but already started");
	if (captureRunToken == 0)
		throw std::runtime_error("StartCapture() called with invalid capture run token");
	if (!m_canCapture)
		throw std::runtime_error("StartCapture() called on a device which cannot capture");

	const HCHANNEL channel = OpenChannel();
	if (!channel)
		throw std::runtime_error("Failed to open the Magewell capture channel");

	m_channel.store(channel, std::memory_order_release);
	m_capturedVideoFrameCount = 0;
	m_missedVideoFrameCount = 0;
	m_timingBaselineTicks.store(-1, std::memory_order_release);

	UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_STARTING);

	m_captureThreadRunning.store(true, std::memory_order_release);
	m_outputCaptureData.store(true, std::memory_order_release);

	try
	{
		m_captureThread = std::thread(
			&MagewellCaptureDevice::CaptureThread, this, captureRunToken);
	}
	catch (...)
	{
		m_captureThreadRunning.store(false, std::memory_order_release);
		m_outputCaptureData.store(false, std::memory_order_release);
		m_channel.store(nullptr, std::memory_order_release);
		MWCloseChannel(channel);
		throw;
	}

	DebugLog::Log("MagewellCaptureDevice::StartCapture() started");
}


void MagewellCaptureDevice::StopCapture()
{
	m_outputCaptureData.store(false, std::memory_order_release);
	m_captureThreadRunning.store(false, std::memory_order_release);

	if (m_captureThread.joinable())
		m_captureThread.join();

	const HCHANNEL channel = m_channel.exchange(nullptr, std::memory_order_acq_rel);
	if (channel)
		MWCloseChannel(channel);

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
	HCHANNEL channel, SignalDescription& signal)
{
	DWORD validFlags = 0;
	if (MWGetHDMIInfoFrameValidFlag(channel, &validFlags) != MW_SUCCEEDED)
		return false;
	if ((validFlags & MWCAP_HDMI_INFOFRAME_MASK_HDR) == 0)
		return false;

	HDMI_INFOFRAME_PACKET packet = {};
	if (MWGetHDMIInfoFramePacket(
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
	if (MWGetVideoSignalStatus(channel, &status) != MW_SUCCEEDED)
		return false;

	signal = SignalDescription();
	signal.locked = (status.state == MWCAP_VIDEO_SIGNAL_LOCKED);
	if (!signal.locked)
		return true;

	signal.width = static_cast<unsigned int>(status.cx);
	signal.height = static_cast<unsigned int>(status.cy);
	signal.interlaced = status.bInterlaced != FALSE;
	signal.frameDuration100ns = status.dwFrameDuration;
	signal.colorFormat = status.colorFormat;
	signal.quantRange = status.quantRange;

	MWCAP_INPUT_SPECIFIC_STATUS specific = {};
	if (MWGetInputSpecificStatus(channel, &specific) == MW_SUCCEEDED &&
		specific.bValid &&
		specific.dwVideoInputType == MWCAP_VIDEO_INPUT_TYPE_HDMI)
	{
		signal.bitDepth = specific.hdmiStatus.byBitDepth;
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
	DWORD& fourcc, VideoFrameEncoding& encoding)
{
	// An 8-bit RGB source is captured as ARGB, which is a repack rather than a
	// colour conversion, so nothing is lost. Everything else is captured as
	// V210, the same 10-bit 4:2:2 encoding the DeckLink path produces, which
	// keeps the whole downstream pipeline unchanged.
	// A bit depth of zero means the HDMI status was not readable yet, which
	// happens while the link is still training. Treating it as 8-bit would
	// silently pick the 8-bit path for a deeper source, so only an explicit
	// depth of 8 or less selects ARGB.
	if (signal.colorFormat == MWCAP_VIDEO_COLOR_FORMAT_RGB &&
		signal.bitDepth > 0 && signal.bitDepth <= 8)
	{
		// ARGB rather than BGRA: VP's DeckLink default for 8-bit RGB is ARGB,
		// so this keeps both vendors on the same downstream renderer route.
		fourcc = MWFOURCC_ARGB;
		encoding = VideoFrameEncoding::ARGB_8BIT;
		return;
	}

	// The card cannot produce v210: asking for it succeeds and writes
	// nothing. P210 carries the same 10-bit 4:2:2 data and is supported, so
	// it is captured and repacked into v210 below.
	fourcc = MWFOURCC_P210;
	encoding = VideoFrameEncoding::V210;
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

	// The wire format, which is not necessarily what we capture. Showing it
	// makes any conversion visible, for example a 12-bit RGB source being
	// captured as 10-bit YCbCr.
	switch (signal.colorFormat)
	{
	case MWCAP_VIDEO_COLOR_FORMAT_RGB:
		cardState->inputEncoding = ColorFormat::RGB444;
		break;
	case MWCAP_VIDEO_COLOR_FORMAT_YUV601:
	case MWCAP_VIDEO_COLOR_FORMAT_YUV709:
	case MWCAP_VIDEO_COLOR_FORMAT_YUV2020:
	case MWCAP_VIDEO_COLOR_FORMAT_YUV2020C:
		cardState->inputEncoding = ColorFormat::YCbCr422;
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

	if (signal.quantRange == MWCAP_VIDEO_QUANTIZATION_FULL)
		cardState->other.push_back(CString(_T("Quantization range: full")));
	else if (signal.quantRange == MWCAP_VIDEO_QUANTIZATION_LIMITED)
		cardState->other.push_back(CString(_T("Quantization range: limited")));

	const HCHANNEL channel = m_channel.load(std::memory_order_acquire);
	if (channel)
	{
		MWCAP_PCIE_CAPTURE_INFO pcie = {};
		if (MWGetFamilyInfo(channel, &pcie, sizeof(pcie)) == MW_SUCCEEDED)
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
	const SignalDescription& signal, CaptureRunToken captureRunToken)
{
	if (!m_callback)
		return false;

	DWORD fourcc = 0;
	VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
	ChooseCaptureFormat(signal, fourcc, encoding);

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
			(encoding == VideoFrameEncoding::R210 ? "R210" : "ARGB"),
		(int)signal.colorSpace, (int)signal.eotf, signal.hasHdrData ? 1 : 0);

	m_callback->OnCaptureDeviceVideoStateChange(
		this, captureRunToken, videoState);
	return true;
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
		Error(CString(exception.what()));
	}
	catch (...)
	{
		DebugLog::Log("Magewell capture thread aborted on an unknown exception");
		Error(TEXT("Unknown error in the Magewell capture thread"));
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
	std::vector<std::unique_ptr<MagewellCaptureBuffer>> buffers;
	// Buffers the renderer may still be reading when the format changes.
	// Destroying those would free memory out from under it, which is an
	// intermittent crash on every HDR/SDR transition. They are parked here
	// and destroyed only once their reference count returns to zero.
	std::vector<std::unique_ptr<MagewellCaptureBuffer>> retiredBuffers;
	int bufferIndex = 0;

	SignalDescription current;
	bool haveState = false;
	DWORD fourcc = 0;
	VideoFrameEncoding encoding = VideoFrameEncoding::UNKNOWN;
	uint32_t stride = 0;
	uint32_t frameSize = 0;
	// Geometry the card writes with, which differs from the geometry VP
	// reads when a repack sits between them.
	uint32_t captureStride = 0;
	uint32_t captureFrameSize = 0;
	bool repackP210 = false;
	std::vector<uint8_t> scratch;

	// Buffers are plain heap memory the SDK copies into. They are never
	// pinned, so there is nothing to unpin and, crucially, the driver never
	// holds a pointer into memory we are about to free. A buffer still
	// referenced by the renderer is left alive by its own reference count.
	bool capturing = false;
	bool dumpNextFrame = false;
	auto reapRetiredBuffers = [&]()
	{
		for (size_t i = retiredBuffers.size(); i > 0; --i)
		{
			auto& candidate = retiredBuffers[i - 1];
			if (candidate && !candidate->InUse())
				retiredBuffers.erase(retiredBuffers.begin() + (i - 1));
		}
	};

	auto releaseBuffers = [&]()
	{
		// A buffer the renderer still holds must outlive this pool. Park it
		// rather than free it; idle buffers are released immediately.
		for (auto& candidate : buffers)
		{
			if (candidate && candidate->InUse())
				retiredBuffers.push_back(std::move(candidate));
		}
		buffers.clear();
		reapRetiredBuffers();
	};

	if (!captureEvent || !notifyEvent)
	{
		Error(TEXT("Failed to create Magewell capture events"));
		goto cleanup;
	}

	if (MWStartVideoCapture(channel, captureEvent) != MW_SUCCEEDED)
	{
		Error(TEXT("Failed to start Magewell video capture"));
		goto cleanup;
	}
	capturing = true;

	notify = MWRegisterNotify(channel, notifyEvent,
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

	DebugLog::Log("Magewell capture build MARKER-M: input panel + PCIe link reporting");

	// Ask the card which output colour formats its conversion engine can
	// actually produce. A FOURCC existing in the SDK header does not mean
	// this model implements it; an unsupported one succeeds and writes
	// nothing, which reads downstream as a blank frame.
	{
		int formatCount = 0;
		if (MWGetVideoCaptureSupportColorFormat(channel, nullptr, &formatCount)
			== MW_SUCCEEDED && formatCount > 0)
		{
			std::vector<DWORD> formats(formatCount);
			if (MWGetVideoCaptureSupportColorFormat(
				channel, formats.data(), &formatCount) == MW_SUCCEEDED)
			{
				for (int f = 0; f < formatCount; ++f)
				{
					const DWORD cc = formats[f];
					DebugLog::Log(
						"Magewell supported capture format[%d]: 0x%08X '%c%c%c%c'",
						f, cc,
						(char)(cc & 0xFF), (char)((cc >> 8) & 0xFF),
						(char)((cc >> 16) & 0xFF), (char)((cc >> 24) & 0xFF));
				}
			}
		}
		else
		{
			DebugLog::Log("Magewell supported capture format query failed");
		}
	}

	while (m_captureThreadRunning.load(std::memory_order_acquire))
	{
		if (WaitForSingleObject(notifyEvent, 100) != WAIT_OBJECT_0)
			continue;

		ULONGLONG notifyStatus = 0;
		if (MWGetNotifyStatus(channel, notify, &notifyStatus) != MW_SUCCEEDED)
			continue;

		const bool formatEvent = (notifyStatus & (
			MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE |
			MWCAP_NOTIFY_INPUT_SPECIFIC_CHANGE |
			MWCAP_NOTIFY_HDMI_INFOFRAME_HDR |
			MWCAP_NOTIFY_HDMI_INFOFRAME_AVI)) != 0;

		if (formatEvent || !haveState)
		{
			SignalDescription signal;
			if (!ReadSignal(channel, signal))
				continue;

			PublishCardState(signal);

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
					DebugLog::Log("Magewell signal lost");
				haveState = false;
				if (capturing)
				{
					MWStopVideoCapture(channel);
					capturing = false;
				}
				releaseBuffers();
				current = signal;
				continue;
			}

			if (!haveState || !signal.SameFormatAs(current))
			{
				// Stop capture before touching the buffer set. A frame copy in
				// flight must not land in a buffer that is about to be freed.
				if (capturing)
				{
					MWStopVideoCapture(channel);
					capturing = false;
				}
				releaseBuffers();
				current = signal;

				ChooseCaptureFormat(current, fourcc, encoding);
				try
				{
					stride = BytesPerRowFor(encoding, current.width);
				}
				catch (const std::exception& exception)
				{
					Error(CString(exception.what()));
					goto cleanup;
				}
				frameSize = stride * current.height;

				// v210 is delivered to VP but captured from the card as P210,
				// which is planar: luma plane then chroma plane, both at
				// width * 2 bytes per row.
				repackP210 = (encoding == VideoFrameEncoding::V210);
				if (repackP210)
				{
					// P210 is planar and a different size to v210, so the card
					// writes into scratch and the repack fills the delivery
					// buffer.
					captureStride = current.width * 2;
					captureFrameSize = captureStride * current.height * 2;
					scratch.assign(captureFrameSize, 0);
				}
				else
				{
					// ARGB needs no repack, and RGB10 shares R210's stride so it
					// is transformed in place. Neither needs scratch.
					captureStride = stride;
					captureFrameSize = frameSize;
					scratch.clear();
				}

				DebugLog::Log(
					"Magewell capture geometry: card fourcc=0x%08X stride=%u "
					"size=%u -> VP stride=%u size=%u repack=%d",
					fourcc, captureStride, captureFrameSize,
					stride, frameSize, repackP210 ? 1 : 0);

				releaseBuffers();
				buffers.reserve(MAGEWELL_CAPTURE_BUFFER_COUNT);
				for (int i = 0; i < MAGEWELL_CAPTURE_BUFFER_COUNT; ++i)
					buffers.push_back(
						std::make_unique<MagewellCaptureBuffer>(frameSize));
				bufferIndex = 0;

				if (MWStartVideoCapture(channel, captureEvent) != MW_SUCCEEDED)
				{
					Error(TEXT("Failed to restart Magewell capture after format change"));
					goto cleanup;
				}
				capturing = true;
				dumpNextFrame = true;

				if (!PublishVideoState(current, captureRunToken))
					continue;
				haveState = true;
			}

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
		if (MWGetVideoBufferInfo(channel, &bufferInfo) != MW_SUCCEEDED)
			continue;

		MWCAP_VIDEO_FRAME_INFO frameInfo = {};
		if (MWGetVideoFrameInfo(
			channel, bufferInfo.iNewestBufferedFullFrame, &frameInfo) != MW_SUCCEEDED)
			continue;

		// Take the next buffer the renderer has finished with. A buffer still
		// referenced downstream must not be overwritten, so if every buffer is
		// held the frame is dropped and counted rather than corrupting one.
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
			++m_missedVideoFrameCount;
			continue;
		}

		// The output colour format and range are stated explicitly so the
		// card's conversion matches what the video state promises downstream.
		// Let the SDK derive the output colour format, range and saturation
		// from the input signal and the target FOURCC. Every Magewell example
		// passes UNKNOWN here. Forcing explicit values overrides the SDK's own
		// conversion and produces incorrect colour, a solid green frame being
		// the visible result for V210.


		if (MWCaptureVideoFrameToVirtualAddressEx(
			channel,
			MWCAP_VIDEO_FRAME_ID_NEWEST_BUFFERED,
			repackP210 ? scratch.data() : buffer->Data(),
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
			repackP210
				? (current.colorSpace == ColorSpace::BT_2020
					? MWCAP_VIDEO_COLOR_FORMAT_YUV2020
					: MWCAP_VIDEO_COLOR_FORMAT_YUV709)
				: MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN,
			repackP210
				? MWCAP_VIDEO_QUANTIZATION_LIMITED
				: MWCAP_VIDEO_QUANTIZATION_UNKNOWN,
			MWCAP_VIDEO_SATURATION_UNKNOWN) != MW_SUCCEEDED)
			continue;

		if (WaitForSingleObject(captureEvent, 200) != WAIT_OBJECT_0)
		{
			++m_missedVideoFrameCount;
			continue;
		}

		MWCAP_VIDEO_CAPTURE_STATUS captureStatus = {};
		const MW_RESULT captureStatusResult =
			MWGetVideoCaptureStatus(channel, &captureStatus);

		// Convert the card's P210 into the v210 the renderer expects. Done
		// before the frame is handed over so everything downstream sees the
		// same layout the DeckLink path produces natively.
		if (repackP210 && dumpNextFrame)
		{
			const uint16_t* ly = reinterpret_cast<const uint16_t*>(scratch.data());
			const size_t lumaCount = (size_t)captureStride / 2 * current.height;
			uint16_t lo16 = 0xFFFF, hi16 = 0;
			uint16_t lo10 = 0x3FF, hi10 = 0;
			unsigned long long sum10 = 0;
			for (size_t i = 0; i < lumaCount; i += 97) // sample
			{
				const uint16_t raw = ly[i];
				const uint16_t v10 = raw >> 6;
				if (raw < lo16) lo16 = raw; if (raw > hi16) hi16 = raw;
				if (v10 < lo10) lo10 = v10; if (v10 > hi10) hi10 = v10;
				sum10 += v10;
			}
			DebugLog::Log(
				"Magewell P210 luma: raw16[min=%u max=%u] shifted10[min=%u max=%u avg=%llu] \nfirst_raw=%04X %04X %04X %04X",
				lo16, hi16, lo10, hi10,
				(lumaCount ? sum10 / (lumaCount/97 + 1) : 0),
				ly[0], ly[1], ly[2], ly[3]);
		}

		if (repackP210)
			RepackP210ToV210(
				scratch.data(), captureStride,
				buffer->Data(), stride,
				current.width, current.height);

		// One-shot diagnostic: dump the first captured frame's raw bytes plus
		// the sizes both sides expect. Lets us compare what the SDK wrote
		// against what the renderer will read, instead of inferring it.
		if (dumpNextFrame)
		{
			dumpNextFrame = false;
			DebugLog::Log(
				"Magewell frame1 dump: fourcc=0x%08X stride=%u frameSize=%u statusRc=%d "
				"first32=%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X "
				"%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X "
				"%02X%02X%02X%02X %02X%02X%02X%02X",
				fourcc, stride, frameSize, (int)captureStatusResult,
				buffer->Data()[0], buffer->Data()[1], buffer->Data()[2], buffer->Data()[3],
				buffer->Data()[4], buffer->Data()[5], buffer->Data()[6], buffer->Data()[7],
				buffer->Data()[8], buffer->Data()[9], buffer->Data()[10], buffer->Data()[11],
				buffer->Data()[12], buffer->Data()[13], buffer->Data()[14], buffer->Data()[15],
				buffer->Data()[16], buffer->Data()[17], buffer->Data()[18], buffer->Data()[19],
				buffer->Data()[20], buffer->Data()[21], buffer->Data()[22], buffer->Data()[23],
				buffer->Data()[24], buffer->Data()[25], buffer->Data()[26], buffer->Data()[27],
				buffer->Data()[28], buffer->Data()[29], buffer->Data()[30], buffer->Data()[31]);

			// Also sample the middle of the frame, so an all-zero buffer is
			// obvious versus a buffer that is filled but wrongly ordered.
			const uint32_t mid = frameSize / 2;
			DebugLog::Log(
				"Magewell frame1 mid[%u]: %02X%02X%02X%02X %02X%02X%02X%02X",
				mid,
				buffer->Data()[mid+0], buffer->Data()[mid+1],
				buffer->Data()[mid+2], buffer->Data()[mid+3],
				buffer->Data()[mid+4], buffer->Data()[mid+5],
				buffer->Data()[mid+6], buffer->Data()[mid+7]);
		}

		const timingclocktime_t rawCaptureTicks =
			static_cast<timingclocktime_t>(frameInfo.allFieldStartTimes[0]);

		// Establish the run baseline on the first frame, then express every
		// frame time relative to it. This is what turns the card's absolute
		// device clock into the stream-relative time VP's DirectShow
		// scheduler expects, and is the difference between smooth playback and
		// the scheduler stalling on a timestamp far in its own future.
		if (m_timingBaselineTicks.load(std::memory_order_acquire) < 0)
			m_timingBaselineTicks.store(rawCaptureTicks, std::memory_order_release);
		const timingclocktime_t captureTimestamp =
			rawCaptureTicks - m_timingBaselineTicks.load(std::memory_order_acquire);

		LONGLONG deviceTime = 0;
		if (MWGetDeviceTime(channel, &deviceTime) == MW_SUCCEEDED)
			m_hardwareLatencyMs.store(
				(double)(deviceTime - rawCaptureTicks) / 10000.0);

		reapRetiredBuffers();

		const uint64_t counter = ++m_capturedVideoFrameCount;

		VideoFrame videoFrame(
			buffer->Data(),
			counter,
			captureTimestamp + m_frameOffsetTicks.load(),
			static_cast<IUnknown*>(buffer),
			captureTimestamp);

		if (captureStatusResult != MW_SUCCEEDED)
		{
			++m_missedVideoFrameCount;
			continue;
		}

		if (counter == 1 || counter % 300 == 0)
			DebugLog::Log("Magewell delivering frame %llu, latency %.1f ms",
				(unsigned long long)counter, m_hardwareLatencyMs.load());

		if (m_callback && m_outputCaptureData.load(std::memory_order_acquire))
			m_callback->OnCaptureDeviceVideoFrame(this, captureRunToken, videoFrame);
	}

cleanup:
	// Order matters. Notifications are stopped, then capture is stopped, and
	// only once the driver can no longer be copying into a buffer are the
	// buffers freed. Freeing a buffer the driver is still writing into is a
	// kernel-level fault, so this sequence is not optional.
	if (notify != nullptr)
		MWUnregisterNotify(channel, notify);

	if (capturing)
		MWStopVideoCapture(channel);

	releaseBuffers();

	// The retirement list is a local, so anything still parked in it would be
	// destroyed on return. Wait briefly for the renderer to release the last
	// frames it holds; the wait is bounded so shutdown can never hang.
	for (int attempt = 0; attempt < 200 && !retiredBuffers.empty(); ++attempt)
	{
		reapRetiredBuffers();
		if (retiredBuffers.empty())
			break;
		Sleep(5);
	}

	if (!retiredBuffers.empty())
		DebugLog::Log(
			"Magewell shutdown: %zu buffer(s) still referenced downstream",
			retiredBuffers.size());

	if (notifyEvent)
		CloseHandle(notifyEvent);
	if (captureEvent)
		CloseHandle(captureEvent);

	DebugLog::Log("MagewellCaptureDevice capture thread ended, %llu frames",
		(unsigned long long)m_capturedVideoFrameCount.load());
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

	UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_FAILED);

	if (m_callback)
		m_callback->OnCaptureDeviceError(error);
}


//
// ITimingClock
//


timingclocktime_t MagewellCaptureDevice::TimingClockNow()
{
	const HCHANNEL channel = m_channel.load(std::memory_order_acquire);
	if (!channel)
		throw std::runtime_error("Magewell timing clock read without a channel");

	LONGLONG deviceTime = 0;
	if (MWGetDeviceTime(channel, &deviceTime) != MW_SUCCEEDED)
		throw std::runtime_error("Could not read the Magewell hardware clock");

	// Share the frame baseline so the clock and the frame timestamps use one
	// zero. Before the first frame sets it, fall through to raw device time.
	const timingclocktime_t baseline =
		m_timingBaselineTicks.load(std::memory_order_acquire);
	const timingclocktime_t rebased = (baseline < 0)
		? static_cast<timingclocktime_t>(deviceTime)
		: static_cast<timingclocktime_t>(deviceTime) - baseline;
	return rebased + m_frameOffsetTicks.load();
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
